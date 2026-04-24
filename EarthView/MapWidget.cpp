#include "MapWidget.h"
#include "Camera.h"
#include "TMSLoader.h"
#include "TileRenderer.h"
#include "BorderRenderer.h"
#include "GridRenderer.h"
#include "Constants.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLExtraFunctions>
#include <QStringList>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <cmath>

namespace {
QString defaultBorderShapefilePath()
{
    const QString relativePath = "World_Countries/World_Countries_Generalized.shp";
    const QStringList candidates = {
        QDir::current().filePath(relativePath),
        QDir(QCoreApplication::applicationDirPath()).filePath(relativePath),
        QDir(QCoreApplication::applicationDirPath()).filePath(QString("../") + relativePath)
    };

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
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
    , m_isPanning(false)
    , m_texturesVisible(true)
    , m_bordersVisible(true)
    , m_gridVisible(true)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(16);
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
    delete m_tileLoader;
    doneCurrent();
}

void MapWidget::setTileServerUrl(const QString& url)
{
    m_tileLoader->setTileUrl(url);
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

void MapWidget::setTexturesVisible(bool visible)
{
    if (m_texturesVisible == visible)
        return;

    m_texturesVisible = visible;
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

void MapWidget::initializeGL()
{
    initializeOpenGLFunctions();

    // Create tile renderer after OpenGL context is ready
    m_tileRenderer = new TileRenderer(m_camera, m_tileLoader, this);
    m_borderRenderer = new BorderRenderer(m_camera, this);
    m_gridRenderer = new GridRenderer(m_camera, this);
    if (m_pendingBorderFilePath.isEmpty()) {
        m_pendingBorderFilePath = defaultBorderShapefilePath();
    }
    QString borderError;
    m_borderRenderer->loadShapefile(m_pendingBorderFilePath, &borderError);
    m_tileLoader->updateVisibleTiles();

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

    m_camera->applyOpenGLTransform();
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

    glDisable(GL_DEPTH_TEST);

    if (m_gridVisible && m_gridRenderer) {
        QPainter painter(this);
        m_gridRenderer->renderLabels(painter);
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
    painter.setRenderHint(QPainter::Antialiasing);

    const QString fpsText = QString("FPS: %1").arg(m_fpsCounter.fps(), 0, 'f', 1);

    QFont font = painter.font();
    font.setPointSize(11);
    font.setBold(true);
    painter.setFont(font);

    QFontMetrics metrics(font);
    QRect textRect = metrics.boundingRect(fpsText);
    textRect.adjust(-8, -6, 8, 6);
    textRect.moveTopLeft(QPoint(10, 10));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));
    painter.drawRoundedRect(textRect, 6, 6);

    painter.setPen(QColor(0, 255, 0));
    painter.drawText(textRect, Qt::AlignCenter, fpsText);
}

void MapWidget::drawGlobeBackdrop()
{
    const double radius = m_camera->getOrthographicRadius();
    const double centerX = width() / 2.0;
    const double centerY = height() / 2.0;
    const int segments = 96;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.03f, 0.08f, 0.13f, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(centerX, centerY);
    for (int i = 0; i <= segments; ++i) {
        const double angle = 2.0 * M_PI * i / segments;
        glVertex2f(
            centerX + std::cos(angle) * radius,
            centerY + std::sin(angle) * radius);
    }
    glEnd();

    glLineWidth(2.0f);
    glColor4f(0.65f, 0.85f, 1.0f, 0.8f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; ++i) {
        const double angle = 2.0 * M_PI * i / segments;
        glVertex2f(
            centerX + std::cos(angle) * radius,
            centerY + std::sin(angle) * radius);
    }
    glEnd();

    glEnable(GL_DEPTH_TEST);
}
