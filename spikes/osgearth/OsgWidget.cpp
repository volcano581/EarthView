#include "OsgWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <osg/ShapeDrawable>
#include <osgDB/ReadFile>

#include <osgEarth/EarthManipulator>
#include <osgEarth/GeoTransform>
#include <osgEarth/GLUtils>
#include <osgEarth/LogarithmicDepthBuffer>
#include <osgEarth/MapNode>
#include <osgEarth/Sky>
#include <osgEarth/Terrain>
#include <osgEarth/Viewpoint>

#include <algorithm>
#include <cmath>

using namespace osgEarth;
using namespace osgEarth::Util;

namespace {
constexpr double kEarthRadius = 6378137.0;
constexpr double kOrbitRadiusMeters = 2000.0;
constexpr double kHeightAboveGroundMeters = 300.0;
constexpr double kOrbitSeconds = 60.0;
constexpr double kPi = 3.14159265358979323846;

int toOsgButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return 1;
    case Qt::MiddleButton: return 2;
    case Qt::RightButton: return 3;
    default: return 0;
    }
}
}

OsgWidget::OsgWidget(const QString& earthFile, double lat, double lon, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_earthFile(earthFile)
    , m_lat(lat)
    , m_lon(lon)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_viewer = new osgViewer::Viewer();
    m_viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    m_window = m_viewer->setUpViewerAsEmbeddedInWindow(0, 0, width(), height());

    connect(&m_frameTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    m_frameTimer.start(0);
    m_clock.start();
}

OsgWidget::~OsgWidget()
{
    makeCurrent();
    m_viewer = nullptr;
    doneCurrent();
}

void OsgWidget::initializeGL()
{
    m_window->setDefaultFboId(defaultFramebufferObject());

    // osgEarth's shaders need generic vertex attributes and matrix uniforms. A normal
    // viewer gets these from GL3RealizeOperation; an embedded window is never realized.
    osg::State* state = m_window->getState();
    state->setUseVertexAttributeAliasing(true);
    state->setUseModelViewAndProjectionUniforms(true);

    buildScene();
}

void OsgWidget::buildScene()
{
    osg::ref_ptr<osg::Node> loaded = osgDB::readRefNodeFile(m_earthFile.toStdString());
    m_mapNode = MapNode::get(loaded.get());
    if (!m_mapNode.valid()) {
        qFatal("Failed to load earth file: %s", qPrintable(m_earthFile));
        return;
    }

    osg::Camera* camera = m_viewer->getCamera();
    GLUtils::setGlobalDefaults(camera->getOrCreateStateSet());

    // Log depth keeps close-range (stealth view) terrain free of z-fighting.
    static LogarithmicDepthBuffer logDepth;
    logDepth.install(camera);

    osg::ref_ptr<SkyNode> sky = SkyNode::create();
    sky->setDateTime(DateTime(2026, 6, 1, 7.0));
    sky->addChild(loaded.get());
    sky->attach(m_viewer.get(), 0);

    // Test entity: a cone standing in for a vehicle/aircraft model.
    m_entity = new GeoTransform();
    m_entityAttitude = new osg::MatrixTransform();
    m_entityAttitude->addChild(createEntityModel());
    m_entity->addChild(m_entityAttitude.get());
    m_mapNode->addChild(m_entity.get());

    m_manip = new EarthManipulator();
    m_manip->getSettings()->setTetherMode(EarthManipulator::TETHER_CENTER_AND_HEADING);
    m_manip->getSettings()->setMinMaxDistance(10.0, 4.0e7);
    // Without this, home() frames the whole scene bound, which the sky dome makes huge.
    m_manip->setHomeViewpoint(homeViewpoint());
    m_viewer->setCameraManipulator(m_manip.get());
    m_viewer->setSceneData(sky.get());

    updateEntity();
    // goHome() runs after the first frame: before that the manipulator has not attached
    // to the MapNode yet and silently drops the viewpoint.
}

osg::Node* OsgWidget::createEntityModel() const
{
    auto* cone = new osg::ShapeDrawable(new osg::Cone(osg::Vec3(), 25.0f, 120.0f));
    cone->setColor(osg::Vec4(1.0f, 0.2f, 0.1f, 1.0f));

    // Cone axis is +Z; point it along local +Y (north in the ENU frame).
    auto* align = new osg::MatrixTransform(osg::Matrix::rotate(-kPi / 2.0, osg::X_AXIS));
    align->addChild(cone);
    return align;
}

void OsgWidget::updateEntity()
{
    const double t = 2.0 * kPi * (m_clock.elapsed() / 1000.0) / kOrbitSeconds;
    const double east = kOrbitRadiusMeters * std::sin(t);
    const double north = kOrbitRadiusMeters * std::cos(t);

    const double latRad = m_lat * kPi / 180.0;
    const double lat = m_lat + (north / kEarthRadius) * 180.0 / kPi;
    const double lon = m_lon + (east / (kEarthRadius * std::cos(latRad))) * 180.0 / kPi;

    const SpatialReference* srs = m_mapNode->getMapSRS()->getGeographicSRS();
    // The terrain only exists after the first frame, and getHeight() fails until the
    // tile under the entity is loaded; keep the last good height meanwhile.
    double groundMsl = 0.0;
    if (Terrain* terrain = m_mapNode->getTerrain()) {
        if (terrain->getHeight(srs, lon, lat, &groundMsl))
            m_groundMsl = groundMsl;
    }

    m_entity->setPosition(GeoPoint(srs, lon, lat, m_groundMsl + kHeightAboveGroundMeters, ALTMODE_ABSOLUTE));

    // Velocity direction of the circle; heading is clockwise from north.
    const double heading = std::atan2(std::cos(t), -std::sin(t));
    m_entityAttitude->setMatrix(osg::Matrix::rotate(-heading, osg::Z_AXIS));
}

void OsgWidget::tether(double rangeMeters, double pitchDeg)
{
    Viewpoint vp;
    vp.setNode(m_entity.get());
    vp.heading() = Angle(0.0, Units::DEGREES);
    vp.pitch() = Angle(pitchDeg, Units::DEGREES);
    vp.range() = Distance(rangeMeters, Units::METERS);
    m_manip->setViewpoint(vp, 1.5);
}

Viewpoint OsgWidget::homeViewpoint() const
{
    Viewpoint vp;
    vp.focalPoint() = GeoPoint(m_mapNode->getMapSRS()->getGeographicSRS(), m_lon, m_lat, 0.0, ALTMODE_ABSOLUTE);
    vp.heading() = Angle(0.0, Units::DEGREES);
    vp.pitch() = Angle(-35.0, Units::DEGREES);
    vp.range() = Distance(25000.0, Units::METERS);
    return vp;
}

void OsgWidget::goHome()
{
    m_manip->clearViewpoint();
    m_manip->setViewpoint(homeViewpoint(), 0.0);
}

void OsgWidget::resizeGL(int w, int h)
{
    const int pw = int(w * devicePixelRatioF());
    const int ph = int(h * devicePixelRatioF());
    m_window->resized(0, 0, pw, ph);
    eventQueue()->windowResize(0, 0, pw, ph);
    m_viewer->getCamera()->setViewport(0, 0, pw, ph);
    m_viewer->getCamera()->setProjectionMatrixAsPerspective(35.0, double(pw) / std::max(ph, 1), 1.0, 1.0e7);
}

void OsgWidget::paintGL()
{
    // Qt may recreate its FBO on resize; OSG must render into the current one.
    m_window->setDefaultFboId(defaultFramebufferObject());
    if (m_mapNode.valid())
        updateEntity();
    m_viewer->frame();

    if (!m_homeApplied && m_mapNode.valid()) {
        m_homeApplied = true;
        goHome();
    }
}

QString OsgWidget::viewpointDescription() const
{
    if (!m_manip.valid())
        return QString();
    osg::Vec3d eye, center, up;
    m_viewer->getCamera()->getViewMatrixAsLookAt(eye, center, up);
    double zNear = 0.0, zFar = 0.0, fovy = 0.0, aspect = 0.0;
    m_viewer->getCamera()->getProjectionMatrixAsPerspective(fovy, aspect, zNear, zFar);
    return QStringLiteral("%1 | eye distance from Earth centre %2 km | near %3 far %4")
        .arg(QString::fromStdString(m_manip->getViewpoint().toString()))
        .arg(eye.length() / 1000.0, 0, 'f', 1)
        .arg(zNear).arg(zFar);
}

osgGA::EventQueue* OsgWidget::eventQueue() const
{
    return m_window->getEventQueue();
}

void OsgWidget::mousePressEvent(QMouseEvent* e)
{
    eventQueue()->mouseButtonPress(scaledX(e->position().x()), scaledY(e->position().y()), toOsgButton(e->button()));
}

void OsgWidget::mouseReleaseEvent(QMouseEvent* e)
{
    eventQueue()->mouseButtonRelease(scaledX(e->position().x()), scaledY(e->position().y()), toOsgButton(e->button()));
}

void OsgWidget::mouseMoveEvent(QMouseEvent* e)
{
    eventQueue()->mouseMotion(scaledX(e->position().x()), scaledY(e->position().y()));
}

void OsgWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    eventQueue()->mouseDoubleButtonPress(scaledX(e->position().x()), scaledY(e->position().y()), toOsgButton(e->button()));
}

void OsgWidget::wheelEvent(QWheelEvent* e)
{
    eventQueue()->mouseScroll(e->angleDelta().y() > 0
        ? osgGA::GUIEventAdapter::SCROLL_UP
        : osgGA::GUIEventAdapter::SCROLL_DOWN);
}

void OsgWidget::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_T: tether(600.0, -15.0); return;   // chase camera
    case Qt::Key_F: tether(60.0, -3.0); return;     // close, near eye level ("stealth")
    case Qt::Key_U: m_manip->clearViewpoint(); return;
    case Qt::Key_H: goHome(); return;
    default: break;
    }
    if (!e->text().isEmpty())
        eventQueue()->keyPress(e->text().at(0).toLatin1());
}

void OsgWidget::keyReleaseEvent(QKeyEvent* e)
{
    if (!e->text().isEmpty())
        eventQueue()->keyRelease(e->text().at(0).toLatin1());
}
