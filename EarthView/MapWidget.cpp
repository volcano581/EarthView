#include "MapWidget.h"
#include "Camera.h"
#include "TMSLoader.h"
#include "TileRenderer.h"
#include "BorderRenderer.h"
#include "GridRenderer.h"
#include "CityRenderer.h"
#include "Constants.h"
#include "ShaderUtils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QStringList>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector4D>
#include <cmath>
#include <cstddef>

namespace {
struct ScreenVertex {
    float position[2];
};

ScreenVertex screenVertex(double x, double y)
{
    return { { static_cast<float>(x), static_cast<float>(y) } };
}

QString defaultBorderShapefilePath()
{
    const QString relativePath = "Data/Borders/World_Countries_Generalized_Shapefile/World_Countries_Generalized.shp";
    const QStringList candidates = {
        QDir::current().filePath("Data/Borders/World_Countries_Generalized.shp"),
        QDir::current().filePath(relativePath),
        QDir(QCoreApplication::applicationDirPath()).filePath("Data/Borders/World_Countries_Generalized.shp"),
        QDir(QCoreApplication::applicationDirPath()).filePath(relativePath),
        QDir(QCoreApplication::applicationDirPath()).filePath("../Data/Borders/World_Countries_Generalized.shp"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QString("../") + relativePath)
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QDir::current().filePath(relativePath);
}

QString defaultCitiesDirectoryPath()
{
    const QString relativePath = "Data/Cities";
    const QStringList candidates = {
        QDir::current().filePath(relativePath),
        QDir(QCoreApplication::applicationDirPath()).filePath(relativePath),
        QDir(QCoreApplication::applicationDirPath()).filePath(QString("../") + relativePath)
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QDir::current().filePath(relativePath);
}
}

MapWidget::MapWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_camera(new Camera(this))
    , m_tileLoader(new TmsLoader(m_camera, this))
    , m_tileRenderer(nullptr)
    , m_borderRenderer(nullptr)
    , m_gridRenderer(nullptr)
    , m_cityRenderer(nullptr)
    , m_isPanning(false)
    , m_texturesVisible(true)
    , m_bordersVisible(true)
    , m_gridVisible(true)
    , m_citiesVisible(true)
    , m_solidProgram(nullptr)
    , m_shapeVbo(0)
    , m_shapeVao(0)
    , m_shapeResourcesInitialized(false)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSwapInterval(0);
    setFormat(format);

    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_updateTimer = new QTimer(this);
    m_updateTimer->setTimerType(Qt::PreciseTimer);
    m_updateTimer->setInterval(0);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        update();
        });
    m_updateTimer->start();

    connect(m_camera, &Camera::cameraChanged, this, &MapWidget::onCameraChanged);
    connect(m_tileLoader, &TmsLoader::tileLoaded, this, QOverload<>::of(&MapWidget::update));
    connect(m_tileLoader, &TmsLoader::tileFailed, this, QOverload<>::of(&MapWidget::update));
}

MapWidget::~MapWidget()
{
    makeCurrent();
    delete m_tileRenderer;
    delete m_borderRenderer;
    delete m_gridRenderer;
    delete m_cityRenderer;
    delete m_tileLoader;
    if (m_shapeResourcesInitialized) {
        QOpenGLContext* current = QOpenGLContext::currentContext();
        QOpenGLExtraFunctions* f = current ? current->extraFunctions() : nullptr;
        if (f) {
            if (m_shapeVbo) {
                f->glDeleteBuffers(1, &m_shapeVbo);
            }
            if (m_shapeVao) {
                f->glDeleteVertexArrays(1, &m_shapeVao);
            }
        }
    }
    delete m_solidProgram;
    doneCurrent();
}

void MapWidget::setTileServerUrl(const QString& url)
{
    TmsLoader::TileSourceLayer layer;
    layer.name = "Tile source";
    layer.urlTemplate = url;
    setTileSourceLayers({ layer });
}

void MapWidget::setTileSourceLayers(const QList<TmsLoader::TileSourceLayer>& layers)
{
    const bool hasContext = context() && isValid();
    if (hasContext) {
        makeCurrent();
    }

    m_tileLoader->setTileSourceLayers(layers);

    if (hasContext) {
        doneCurrent();
    }
    update();
}

bool MapWidget::loadWorldBorders()
{
    return loadBorderShapefile(defaultBorderShapefilePath());
}

bool MapWidget::loadBorderShapefile(const QString& filePath, QString* errorMessage)
{
    if (filePath.isEmpty())
        return false;

    m_pendingBorderFilePath = filePath;
    if (!m_borderRenderer) {
        return QFileInfo::exists(filePath);
    }

    bool loaded = m_borderRenderer->loadShapefile(filePath, errorMessage);
    if (loaded) {
        update();
    }
    return loaded;
}

bool MapWidget::loadCities(const QString& directoryPath, QString* errorMessage)
{
    const QString resolvedPath = directoryPath.isEmpty()
        ? defaultCitiesDirectoryPath()
        : directoryPath;

    m_pendingCitiesDirectoryPath = resolvedPath;
    if (!m_cityRenderer) {
        return QFileInfo(resolvedPath).isDir();
    }

    const bool loaded = m_cityRenderer->loadDirectory(resolvedPath, errorMessage);
    if (loaded) {
        update();
    }
    return loaded;
}

void MapWidget::setTexturesVisible(bool visible)
{
    if (m_texturesVisible == visible)
        return;

    m_texturesVisible = visible;
    const bool hasContext = context() && isValid();
    if (hasContext) {
        makeCurrent();
    }

    m_tileLoader->setLoadingEnabled(visible);

    if (hasContext) {
        doneCurrent();
    }
    update();
}

void MapWidget::setBordersVisible(bool visible)
{
    if (m_bordersVisible == visible)
        return;

    m_bordersVisible = visible;
    update();
}

void MapWidget::setGridVisible(bool visible)
{
    if (m_gridVisible == visible)
        return;

    m_gridVisible = visible;
    update();
}

void MapWidget::setCitiesVisible(bool visible)
{
    if (m_citiesVisible == visible)
        return;

    m_citiesVisible = visible;
    update();
}

void MapWidget::initializeShapeResources()
{
    if (m_shapeResourcesInitialized)
        return;

    m_solidProgram = new QOpenGLShaderProgram();
    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            m_solidProgram,
            QStringLiteral("solid_2d.vert"),
            QStringLiteral("solid_2d.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        delete m_solidProgram;
        m_solidProgram = nullptr;
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    f->glGenVertexArrays(1, &m_shapeVao);
    f->glGenBuffers(1, &m_shapeVbo);
    f->glBindVertexArray(m_shapeVao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_shapeVbo);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(ScreenVertex),
        reinterpret_cast<void*>(offsetof(ScreenVertex, position)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);

    m_shapeResourcesInitialized = true;
}


void MapWidget::initializeGL()
{
    initializeOpenGLFunctions();

        // Create tile renderer after OpenGL context is ready
    m_tileRenderer = new TileRenderer(m_camera, m_tileLoader, this);
    m_borderRenderer = new BorderRenderer(m_camera, this);
    m_gridRenderer = new GridRenderer(m_camera, this);
    m_cityRenderer = new CityRenderer(m_camera, this);
    if (m_pendingBorderFilePath.isEmpty()) {
        m_pendingBorderFilePath = defaultBorderShapefilePath();
    }
    if (m_pendingCitiesDirectoryPath.isEmpty()) {
        m_pendingCitiesDirectoryPath = defaultCitiesDirectoryPath();
    }
    QString borderError;
    m_borderRenderer->loadShapefile(m_pendingBorderFilePath, &borderError);
    QString citiesError;
    m_cityRenderer->loadDirectory(m_pendingCitiesDirectoryPath, &citiesError);
    m_tileLoader->setLoadingEnabled(m_texturesVisible);
    if (m_texturesVisible) {
        m_tileLoader->updateVisibleTiles();
    }

    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void MapWidget::resizeGL(int w, int h)
{
    m_camera->setViewportSize(w, h);
    glViewport(0, 0, w, h);
}

void MapWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_fpsCounter.frameRendered();

    if (m_camera->isOrthographic()) {
        drawGlobeBackdrop();
    }

    // Render TMS tiles
    if (m_texturesVisible && m_tileRenderer) {
        m_tileRenderer->render();
    }

    // Render world borders on top
    if (m_bordersVisible && m_borderRenderer) {
        m_borderRenderer->render();
    }

    if (m_gridVisible && m_gridRenderer) {
        m_gridRenderer->render();
    }

    if (m_citiesVisible && m_cityRenderer) {
        m_cityRenderer->renderMarkers();
    }

    glDisable(GL_DEPTH_TEST);

    if ((m_gridVisible && m_gridRenderer) || (m_citiesVisible && m_cityRenderer)) {
        QPainter painter(this);
        if (m_gridVisible && m_gridRenderer) {
            m_gridRenderer->renderLabels(painter);
        }
        if (m_citiesVisible && m_cityRenderer) {
            m_cityRenderer->renderLabels(painter);
        }
    }

    drawFpsOverlay();
    glEnable(GL_DEPTH_TEST);
}

void MapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_lastMousePos = event->pos();
        m_isPanning = true;
        setCursor(Qt::ClosedHandCursor);
    }
}

void MapWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_camera->pan(QPointF(delta.x(), delta.y()));
        m_lastMousePos = event->pos();
        update();
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
    }
}

void MapWidget::wheelEvent(QWheelEvent* event)
{
    float delta = event->angleDelta().y() > 0 ? 0.5f : -0.5f;
    m_camera->zoom(delta, QPointF(event->position()));
    update();
}

void MapWidget::keyPressEvent(QKeyEvent* event)
{
    const float PAN_STEP = 50.0f;

    switch (event->key()) {
    case Qt::Key_Up:
        m_camera->pan(QPointF(0, PAN_STEP));
        break;
    case Qt::Key_Down:
        m_camera->pan(QPointF(0, -PAN_STEP));
        break;
    case Qt::Key_Left:
        m_camera->pan(QPointF(PAN_STEP, 0));
        break;
    case Qt::Key_Right:
        m_camera->pan(QPointF(-PAN_STEP, 0));
        break;
    case Qt::Key_Plus:
        m_camera->zoom(0.5f);
        break;
    case Qt::Key_Minus:
        m_camera->zoom(-0.5f);
        break;
    case Qt::Key_Home:
        m_camera->setCenter(QPointF(0, 0));
        m_camera->setZoomLevel(2.0);
        break;
    default:
        QOpenGLWidget::keyPressEvent(event);
    }
    update();
}

void MapWidget::onCameraChanged()
{
    update();
}

void MapWidget::drawFpsOverlay()
{
    QPainter painter(this);
   

    const double fps = m_fpsCounter.fps();
    
    const QString fpsText = QString("FPS: %1").arg(fps, 0, 'f', 0);

    QFont font = painter.font();
    font.setPointSize(11);
    font.setFamily("Arial"); 
    
    painter.setFont(font);

    QFontMetrics fm(painter.font());

    const int x = 10;
    const int y = 10;
    

    const int textWidth = fm.horizontalAdvance(fpsText + "     ");
    const int textHeight = fm.height();

    QRect boxRect(
        x,
        y,
        qMax(92, textWidth + 20),
        qMax(28, textHeight + 20)
    );

    QRect textRect = boxRect.adjusted(
        8,6,-8,-6
    );

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));
    painter.drawRoundedRect(boxRect, 8, 6);

    painter.setPen(QColor(0, 255, 0));
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, fpsText);
}
void MapWidget::drawGlobeBackdrop()
{
    initializeShapeResources();
    if (!m_shapeResourcesInitialized || !m_solidProgram || !m_solidProgram->isLinked())
        return;

    const double radius = m_camera->getOrthographicRadius();
    const double centerX = width() / 2.0;
    const double centerY = height() / 2.0;
    const int segments = 96;

    QVector<ScreenVertex> fanVertices;
    fanVertices.reserve(segments + 2);
    fanVertices.append(screenVertex(centerX, centerY));
    for (int i = 0; i <= segments; ++i) {
        const double angle = 2.0 * M_PI * i / segments;
        fanVertices.append(screenVertex(
            centerX + std::cos(angle) * radius,
            centerY + std::sin(angle) * radius));
    }

    QVector<ScreenVertex> outlineVertices;
    outlineVertices.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        const double angle = 2.0 * M_PI * i / segments;
        outlineVertices.append(screenVertex(
            centerX + std::cos(angle) * radius,
            centerY + std::sin(angle) * radius));
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_solidProgram->bind();
    m_solidProgram->setUniformValue(
        "u_viewportSize",
        QVector2D(static_cast<float>(width()), static_cast<float>(height())));

    f->glBindVertexArray(m_shapeVao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_shapeVbo);
    f->glBufferData(
        GL_ARRAY_BUFFER,
        fanVertices.size() * static_cast<qsizetype>(sizeof(ScreenVertex)),
        fanVertices.constData(),
        GL_STREAM_DRAW);
    m_solidProgram->setUniformValue("u_color", QVector4D(0.03f, 0.08f, 0.13f, 1.0f));
    f->glDrawArrays(GL_TRIANGLE_FAN, 0, fanVertices.size());

    glLineWidth(2.0f);
    f->glBufferData(
        GL_ARRAY_BUFFER,
        outlineVertices.size() * static_cast<qsizetype>(sizeof(ScreenVertex)),
        outlineVertices.constData(),
        GL_STREAM_DRAW);
    m_solidProgram->setUniformValue("u_color", QVector4D(0.65f, 0.85f, 1.0f, 0.8f));
    f->glDrawArrays(GL_LINE_LOOP, 0, outlineVertices.size());

    f->glBindVertexArray(0);
    m_solidProgram->release();
}
