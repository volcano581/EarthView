#include "mapwidget.h"
#include "camera.h"
#include "tmsloader.h"
#include "tilerenderer.h"
#include "borderrenderer.h"
#include <QOpenGLExtraFunctions>

MapWidget::MapWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_camera(new Camera(this))
    , m_tileLoader(new TmsLoader(m_camera, this))
    , m_tileRenderer(nullptr)
    , m_isPanning(false)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        if (m_isPanning) {
            update();
        }
        });
    m_updateTimer->start();

    connect(m_camera, &Camera::cameraChanged, this, &MapWidget::onCameraChanged);
}

MapWidget::~MapWidget()
{
    makeCurrent();
    delete m_tileRenderer;
    delete m_borderRenderer;
    delete m_tileLoader;
    doneCurrent();
}

void MapWidget::setTileServerUrl(const QString& url)
{
    m_tileLoader->setTileUrl(url);
}

void MapWidget::loadWorldBorders()
{
    
}

void MapWidget::initializeGL()
{
    initializeOpenGLFunctions();

    // Create tile renderer after OpenGL context is ready
    m_tileRenderer = new TileRenderer(m_camera, m_tileLoader, this);
    m_borderRenderer = new BorderRenderer(m_camera, this);
    m_borderRenderer->loadWorldBorders();

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

    // Render TMS tiles
    if (m_tileRenderer) {
        m_tileRenderer->render();
    }

    // Render world borders on top
    m_borderRenderer->render();
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