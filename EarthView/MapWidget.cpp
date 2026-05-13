#include "MapWidget.h"
#include "Camera.h"
#include "TMSLoader.h"
#include "TileRenderer.h"
#include "BorderRenderer.h"
#include "GridRenderer.h"
#include "CityRenderer.h"
#include "VectorTileRenderer.h"
#include "Constants.h"
#include "ShaderUtils.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QStringList>
#include <QFont>
#include <QFontMetrics>
#include <QSurfaceFormat>
#include <QtGlobal>
#include <QVector2D>
#include <QVector4D>
#include <cmath>
#include <cstddef>

namespace {
struct ScreenVertex {
    float position[2];
};

struct ScaleBarInfo {
    bool valid = false;
    QRectF backgroundRect;
    QPointF lineStart;
    QPointF lineEnd;
    double tickHeight = 8.0;
    QString label;
};

ScreenVertex screenVertex(double x, double y)
{
    return { { static_cast<float>(x), static_cast<float>(y) } };
}

double niceScaleDistance(double meters)
{
    if (meters <= 0.0 || !std::isfinite(meters))
        return 0.0;

    const double exponent = std::floor(std::log10(meters));
    const double base = std::pow(10.0, exponent);
    const double fraction = meters / base;
    double niceFraction = 1.0;

    if (fraction >= 5.0) {
        niceFraction = 5.0;
    }
    else if (fraction >= 2.0) {
        niceFraction = 2.0;
    }

    return niceFraction * base;
}

QString scaleDistanceLabel(double meters)
{
    if (meters >= 1000.0) {
        const double kilometers = meters / 1000.0;
        return QString("%1 km").arg(kilometers, 0, 'f', kilometers >= 10.0 ? 0 : 1);
    }

    return QString("%1 m").arg(std::round(meters), 0, 'f', 0);
}

ScaleBarInfo scaleBarInfo(const Camera* camera, int viewportWidth, int viewportHeight)
{
    ScaleBarInfo info;
    if (!camera || viewportWidth < 160 || viewportHeight < 120)
        return info;

    const double metersPerPixel = camera->getResolution();
    if (metersPerPixel <= 0.0 || !std::isfinite(metersPerPixel))
        return info;

    const double targetPixels = qBound(90.0, viewportWidth * 0.18, 180.0);
    const double meters = niceScaleDistance(metersPerPixel * targetPixels);
    if (meters <= 0.0)
        return info;

    const double pixelWidth = meters / metersPerPixel;
    if (pixelWidth <= 8.0)
        return info;

    const double left = 18.0;
    const double bottom = viewportHeight - 18.0;
    const double lineY = bottom - 12.0;
    const double top = lineY - 32.0;
    const double right = left + pixelWidth + 28.0;

    info.valid = true;
    info.backgroundRect = QRectF(
        QPointF(left - 10.0, top),
        QPointF(qMin<double>(right, viewportWidth - 10.0), bottom));
    info.lineStart = QPointF(left, lineY);
    info.lineEnd = QPointF(left + pixelWidth, lineY);
    info.tickHeight = 8.0;
    info.label = scaleDistanceLabel(meters);
    return info;
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
    , m_vectorTileRenderer(nullptr)
    , m_lineBatchRenderer(nullptr)
    , m_textRenderer(nullptr)
    , m_lineBatchMode(LineBatchRenderer::CoordinateMode::Mercator)
    , m_isPanning(false)
    , m_texturesVisible(true)
    , m_bordersVisible(true)
    , m_gridVisible(true)
    , m_citiesVisible(true)
    , m_solidProgram(nullptr)
    , m_shapeVbo(0)
    , m_shapeVao(0)
    , m_shapeResourcesInitialized(false)
    , m_lineBatchDirty(true)
    , m_mapLabelsDirty(true)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
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
    delete m_vectorTileRenderer;
    delete m_lineBatchRenderer;
    delete m_textRenderer;
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
    m_tileSourceLayers = layers;
    const bool hasContext = context() && isValid();
    if (hasContext) {
        makeCurrent();
    }

    m_tileLoader->setTileSourceLayers(layers);
    if (m_vectorTileRenderer) {
        m_vectorTileRenderer->setTileSourceLayers(layers);
    }

    if (hasContext) {
        doneCurrent();
    }
    invalidateLineBatch();
    invalidateMapLabels();
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
        invalidateLineBatch();
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
        invalidateMapLabels();
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
    if (m_vectorTileRenderer) {
        m_vectorTileRenderer->setEnabled(visible);
    }

    if (hasContext) {
        doneCurrent();
    }
    invalidateMapLabels();
    update();
}

void MapWidget::setBordersVisible(bool visible)
{
    if (m_bordersVisible == visible)
        return;

    m_bordersVisible = visible;
    invalidateLineBatch();
    update();
}

void MapWidget::setGridVisible(bool visible)
{
    if (m_gridVisible == visible)
        return;

    m_gridVisible = visible;
    invalidateLineBatch();
    invalidateMapLabels();
    update();
}

void MapWidget::setCitiesVisible(bool visible)
{
    if (m_citiesVisible == visible)
        return;

    m_citiesVisible = visible;
    invalidateMapLabels();
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
    if (context()) {
        const QSurfaceFormat actualFormat = context()->format();
        qDebug() << "Actual OpenGL context:"
                 << actualFormat.majorVersion() << "." << actualFormat.minorVersion()
                 << "profile" << actualFormat.profile()
                 << "renderer" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    }

    // Create tile renderer after OpenGL context is ready
    m_tileRenderer = new TileRenderer(m_camera, m_tileLoader, this);
    m_borderRenderer = new BorderRenderer(m_camera, this);
    m_gridRenderer = new GridRenderer(m_camera, this);
    m_cityRenderer = new CityRenderer(m_camera, this);
    m_vectorTileRenderer = new VectorTileRenderer(m_camera, this);
    m_vectorTileRenderer->setTileSourceLayers(m_tileSourceLayers);
    m_vectorTileRenderer->setEnabled(m_texturesVisible);
    m_lineBatchRenderer = new LineBatchRenderer(m_camera, this);
    m_textRenderer = new TextRenderer(m_camera, this);
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
    invalidateLineBatch();
    invalidateMapLabels();

    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void MapWidget::resizeGL(int w, int h)
{
    m_camera->setViewportSize(w, h);
    glViewport(0, 0, w, h);
    invalidateLineBatch();
    invalidateMapLabels();
}

void MapWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    m_fpsCounter.frameRendered();

    if (m_camera->isOrthographic()) {
        drawGlobeBackdrop();
    }

    // Render TMS tiles
    if (m_texturesVisible && m_tileRenderer) {
        m_tileRenderer->render();
    }
    if (m_texturesVisible && m_vectorTileRenderer) {
        m_vectorTileRenderer->render();
    }

    if (((m_bordersVisible && m_borderRenderer) || (m_gridVisible && m_gridRenderer))
        && m_lineBatchRenderer) {
        rebuildLineBatchIfNeeded();
        m_lineBatchRenderer->render(m_lineBatchMode);
    }

    if (m_citiesVisible && m_cityRenderer) {
        m_cityRenderer->renderMarkers();
    }

    glDisable(GL_DEPTH_TEST);

    rebuildMapLabelsIfNeeded();
    QVector<TextRenderer::Label> textLabels = m_cachedMapLabels;
    appendFpsOverlay(textLabels);
    appendScaleBarOverlay(textLabels);
    drawScaleBarOverlay();
    if (m_textRenderer) {
        m_textRenderer->render(textLabels);
    }

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
    invalidateLineBatch();
    invalidateMapLabels();
    update();
}

void MapWidget::invalidateMapLabels()
{
    m_mapLabelsDirty = true;
}

void MapWidget::invalidateLineBatch()
{
    m_lineBatchDirty = true;
}

void MapWidget::rebuildLineBatchIfNeeded()
{
    if (!m_lineBatchDirty || !m_lineBatchRenderer)
        return;

    m_cachedLineVertices.clear();
    m_lineBatchMode = m_camera->isOrthographic()
        ? LineBatchRenderer::CoordinateMode::Screen
        : LineBatchRenderer::CoordinateMode::Mercator;

    if (m_lineBatchMode == LineBatchRenderer::CoordinateMode::Mercator) {
        if (m_bordersVisible && m_borderRenderer) {
            m_borderRenderer->appendMercatorLines(m_cachedLineVertices);
        }
        if (m_gridVisible && m_gridRenderer) {
            m_gridRenderer->appendMercatorLines(m_cachedLineVertices);
        }
    }
    else {
        if (m_bordersVisible && m_borderRenderer) {
            m_borderRenderer->appendScreenLines(m_cachedLineVertices);
        }
        if (m_gridVisible && m_gridRenderer) {
            m_gridRenderer->appendScreenLines(m_cachedLineVertices);
        }
    }

    m_lineBatchRenderer->setVertices(m_cachedLineVertices);
    m_lineBatchDirty = false;
}

void MapWidget::rebuildMapLabelsIfNeeded()
{
    if (!m_mapLabelsDirty)
        return;

    m_cachedMapLabels.clear();
    if (m_gridVisible && m_gridRenderer) {
        m_gridRenderer->appendLabels(m_cachedMapLabels);
    }
    if (m_texturesVisible && m_vectorTileRenderer) {
        m_vectorTileRenderer->appendLabels(m_cachedMapLabels);
    }
    if (m_citiesVisible && m_cityRenderer) {
        m_cityRenderer->appendLabels(m_cachedMapLabels);
    }
    m_mapLabelsDirty = false;
}

void MapWidget::appendFpsOverlay(QVector<TextRenderer::Label>& labels)
{
    const double fps = m_fpsCounter.fps();
    const QString fpsText = QString("FPS: %1").arg(fps, 0, 'f', 0);

    QFont font;
    font.setPointSize(11);
    font.setFamily("Arial");

    QFontMetrics fm(font);

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

    TextRenderer::Label label;
    label.text = fpsText;
    label.rect = QRectF(boxRect);
    label.font = font;
    label.textColor = QColor(0, 255, 0);
    label.backgroundColor = QColor(0, 0, 0, 170);
    label.textMargins = QMargins(8, 6, 8, 6);
    label.radius = 6;
    label.alignment = Qt::AlignLeft | Qt::AlignVCenter;
    labels.append(label);
}

void MapWidget::appendScaleBarOverlay(QVector<TextRenderer::Label>& labels)
{
    const ScaleBarInfo info = scaleBarInfo(m_camera, width(), height());
    if (!info.valid)
        return;

    QFont font;
    font.setPointSize(9);
    font.setFamily("Arial");
    font.setBold(true);

    QFontMetrics metrics(font);
    QRect textRect = metrics.boundingRect(info.label).adjusted(-4, -2, 4, 2);
    textRect.moveCenter(QPoint(
        static_cast<int>((info.lineStart.x() + info.lineEnd.x()) * 0.5),
        static_cast<int>(info.lineStart.y() - 15.0)));

    TextRenderer::Label label;
    label.text = info.label;
    label.rect = QRectF(textRect);
    label.font = font;
    label.textColor = QColor(240, 250, 255);
    label.backgroundColor = QColor(0, 0, 0, 0);
    label.radius = 0;
    label.alignment = Qt::AlignCenter;
    labels.append(label);
}

void MapWidget::drawScaleBarOverlay()
{
    const ScaleBarInfo info = scaleBarInfo(m_camera, width(), height());
    if (!info.valid)
        return;

    initializeShapeResources();
    if (!m_shapeResourcesInitialized || !m_solidProgram || !m_solidProgram->isLinked())
        return;

    const QRectF background = info.backgroundRect;
    QVector<ScreenVertex> backgroundVertices;
    backgroundVertices.reserve(6);
    backgroundVertices.append(screenVertex(background.left(), background.top()));
    backgroundVertices.append(screenVertex(background.right(), background.top()));
    backgroundVertices.append(screenVertex(background.right(), background.bottom()));
    backgroundVertices.append(screenVertex(background.left(), background.top()));
    backgroundVertices.append(screenVertex(background.right(), background.bottom()));
    backgroundVertices.append(screenVertex(background.left(), background.bottom()));

    const double tickTop = info.lineStart.y() - info.tickHeight;
    const double tickBottom = info.lineStart.y() + 2.0;
    QVector<ScreenVertex> lineVertices;
    lineVertices.reserve(6);
    lineVertices.append(screenVertex(info.lineStart.x(), info.lineStart.y()));
    lineVertices.append(screenVertex(info.lineEnd.x(), info.lineEnd.y()));
    lineVertices.append(screenVertex(info.lineStart.x(), tickTop));
    lineVertices.append(screenVertex(info.lineStart.x(), tickBottom));
    lineVertices.append(screenVertex(info.lineEnd.x(), tickTop));
    lineVertices.append(screenVertex(info.lineEnd.x(), tickBottom));

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
        backgroundVertices.size() * static_cast<qsizetype>(sizeof(ScreenVertex)),
        backgroundVertices.constData(),
        GL_STREAM_DRAW);
    m_solidProgram->setUniformValue("u_color", QVector4D(0.0f, 0.0f, 0.0f, 0.55f));
    f->glDrawArrays(GL_TRIANGLES, 0, backgroundVertices.size());

    glLineWidth(2.0f);
    f->glBufferData(
        GL_ARRAY_BUFFER,
        lineVertices.size() * static_cast<qsizetype>(sizeof(ScreenVertex)),
        lineVertices.constData(),
        GL_STREAM_DRAW);
    m_solidProgram->setUniformValue("u_color", QVector4D(0.92f, 0.98f, 1.0f, 0.95f));
    f->glDrawArrays(GL_LINES, 0, lineVertices.size());

    f->glBindVertexArray(0);
    m_solidProgram->release();
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
