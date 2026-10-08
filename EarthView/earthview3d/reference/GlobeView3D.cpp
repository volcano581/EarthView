#include "GlobeView3D.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <osg/AutoTransform>
#include <osg/ShapeDrawable>
#include <osgDB/ReadFile>

#include <osgEarth/EarthManipulator>
#include <osgEarth/GeoTransform>
#include <osgEarth/GLUtils>
#include <osgEarth/LogarithmicDepthBuffer>
#include <osgEarth/MapNode>
#include <osgEarth/Sky>
#include <osgEarth/Terrain>

#include <algorithm>
#include <cmath>

using namespace osgEarth;
using namespace osgEarth::Util;

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

// Entity markers are modelled in metres at roughly real size and never drawn
// smaller than this many pixels, so they stay visible from orbit.
constexpr float kMinMarkerPixels = 14.0f;

int toOsgButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return 1;
    case Qt::MiddleButton: return 2;
    case Qt::RightButton: return 3;
    default: return 0;
    }
}

osg::Vec4 toColor(std::uint32_t rgba)
{
    return osg::Vec4(((rgba >> 24) & 0xFF) / 255.0f, ((rgba >> 16) & 0xFF) / 255.0f,
                     ((rgba >> 8) & 0xFF) / 255.0f, 1.0f);
}

QSurfaceFormat osgEarthSurfaceFormat()
{
    // osgEarth from vcpkg (OSG without GL3 core) needs a compatibility profile;
    // host applications often default to a core profile, so set this per widget.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 6);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    return format;
}
} // namespace

GlobeView3D::GlobeView3D(const QString& earthFile, double homeLatDeg, double homeLonDeg, QWidget* parent)
    : QOpenGLWidget(parent)
    , m_earthFile(earthFile)
    , m_homeLat(homeLatDeg)
    , m_homeLon(homeLonDeg)
{
    setFormat(osgEarthSurfaceFormat());
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_viewer = new osgViewer::Viewer();
    m_viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    m_window = m_viewer->setUpViewerAsEmbeddedInWindow(0, 0, width(), height());

    connect(&m_frameTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    m_frameTimer.start(0);
}

GlobeView3D::~GlobeView3D()
{
    makeCurrent();
    m_entities.clear();
    m_viewer = nullptr;
    doneCurrent();
}

void GlobeView3D::initializeGL()
{
    m_window->setDefaultFboId(defaultFramebufferObject());

    osg::State* state = m_window->getState();
    state->setUseVertexAttributeAliasing(true);
    state->setUseModelViewAndProjectionUniforms(true);

    buildScene();
}

void GlobeView3D::buildScene()
{
    osg::ref_ptr<osg::Node> loaded = osgDB::readRefNodeFile(m_earthFile.toStdString());
    m_mapNode = MapNode::get(loaded.get());
    if (!m_mapNode.valid()) {
        emit loadFailed(QStringLiteral("Could not load earth file %1").arg(m_earthFile));
        return;
    }

    osg::Camera* camera = m_viewer->getCamera();
    GLUtils::setGlobalDefaults(camera->getOrCreateStateSet());

    // osgEarth's LogarithmicDepthBuffer drops the terrain next to the camera in
    // this embedded setup (ground-level stealth views showed black/red-yellow
    // below the horizon), so it is opt-in. A small near/far ratio keeps close
    // ground visible instead. Diagnostics: DOCTRINE_3D_LOGDEPTH=1, DOCTRINE_3D_NO_SKY=1.
    if (qEnvironmentVariableIntValue("DOCTRINE_3D_LOGDEPTH") == 1) {
        static LogarithmicDepthBuffer logDepth;
        logDepth.install(camera);
    }
    camera->setNearFarRatio(0.00002);

    osg::ref_ptr<osg::Group> root;
    if (qEnvironmentVariableIntValue("DOCTRINE_3D_NO_SKY") != 1) {
        osg::ref_ptr<SkyNode> sky = SkyNode::create();
        sky->setDateTime(DateTime(2026, 6, 1, 7.0));
        sky->attach(m_viewer.get(), 0);
        root = sky;
    }
    else {
        root = new osg::Group();
    }
    root->addChild(loaded.get());

    m_entityRoot = new osg::Group();
    GLUtils::setLighting(m_entityRoot->getOrCreateStateSet(), osg::StateAttribute::OFF);
    m_mapNode->addChild(m_entityRoot.get());

    m_manip = new EarthManipulator();
    m_manip->getSettings()->setTetherMode(EarthManipulator::TETHER_CENTER_AND_HEADING);
    m_manip->getSettings()->setMinMaxDistance(5.0, 4.0e7);
    m_manip->getSettings()->setTerrainAvoidanceEnabled(true);
    m_manip->getSettings()->setTerrainAvoidanceMinimumDistance(5.0);
    m_manip->setHomeViewpoint(homeViewpoint());
    m_viewer->setCameraManipulator(m_manip.get());
    m_viewer->setSceneData(root.get());
}

osg::Node* GlobeView3D::createShape(int shape, std::uint32_t rgba) const
{
    // Shapes face local +Y (north at heading 0); sizes in metres.
    osg::Shape* geometry = nullptr;
    osg::Matrix align;
    switch (shape) {
    case 1: // air: cone pointing forward
        geometry = new osg::Cone(osg::Vec3(), 8.0f, 30.0f);
        align = osg::Matrix::rotate(-kPi / 2.0, osg::X_AXIS);
        break;
    case 2: // armour: hull-shaped box
        geometry = new osg::Box(osg::Vec3(0.0f, 0.0f, 1.5f), 3.6f, 7.5f, 3.0f);
        break;
    case 3: // artillery: barrel-like cylinder lying along +Y
        geometry = new osg::Cylinder(osg::Vec3(0.0f, 0.0f, 1.5f), 1.2f, 8.0f);
        align = osg::Matrix::rotate(-kPi / 2.0, osg::X_AXIS);
        break;
    default: // infantry / unknown: sphere
        geometry = new osg::Sphere(osg::Vec3(0.0f, 0.0f, 2.0f), 2.0f);
        break;
    }

    auto* drawable = new osg::ShapeDrawable(geometry);
    drawable->setColor(toColor(rgba));

    auto* aligned = new osg::MatrixTransform(align);
    aligned->addChild(drawable);

    // Real size up close, at least kMinMarkerPixels when far away. With screen
    // auto-scaling OSG sets scale = metres-per-pixel (clamped to the minimum), so
    // pre-scaling by k and clamping at 1/k gives max(mpp * k, 1) overall.
    const double modelSize = std::max(1.0f, drawable->getBound().radius() * 2.0f);
    const double k = kMinMarkerPixels / modelSize;
    auto* preScale = new osg::MatrixTransform(osg::Matrix::scale(k, k, k));
    preScale->addChild(aligned);

    auto* autoScale = new osg::AutoTransform();
    autoScale->setAutoScaleToScreen(true);
    autoScale->setMinimumScale(1.0 / k);
    autoScale->addChild(preScale);
    return autoScale;
}

GlobeView3D::EntityVisual& GlobeView3D::createEntity(const EntityState3D& state)
{
    EntityVisual& visual = m_entities[state.id];
    visual.geo = new GeoTransform();
    visual.attitude = new osg::MatrixTransform();
    visual.attitude->addChild(createShape(state.shape, state.rgba));
    visual.geo->addChild(visual.attitude.get());
    visual.shape = state.shape;
    visual.rgba = state.rgba;
    m_entityRoot->addChild(visual.geo.get());
    return visual;
}

void GlobeView3D::updateEntity(EntityVisual& visual, const EntityState3D& state)
{
    if (state.shape != visual.shape || state.rgba != visual.rgba) {
        visual.attitude->removeChildren(0, visual.attitude->getNumChildren());
        visual.attitude->addChild(createShape(state.shape, state.rgba));
        visual.shape = state.shape;
        visual.rgba = state.rgba;
    }
    visual.label = state.label;

    const SpatialReference* wgs84 = m_mapNode->getMapSRS()->getGeographicSRS();
    double altM = state.altM;
    if (state.clampToTerrain) {
        double terrainM = 0.0;
        Terrain* terrain = m_mapNode->getTerrain();
        if (terrain && terrain->getHeight(wgs84, state.lonDeg, state.latDeg, &terrainM)) {
            visual.groundAltM = terrainM;
            visual.haveGround = true;
        }
        if (visual.haveGround)
            altM = visual.groundAltM;
    }
    visual.geo->setPosition(GeoPoint(wgs84, state.lonDeg, state.latDeg, altM, ALTMODE_ABSOLUTE));
    visual.attitude->setMatrix(osg::Matrix::rotate(-state.headingDeg * kDegToRad, osg::Z_AXIS));
}

void GlobeView3D::setEntities(const std::vector<EntityState3D>& entities)
{
    if (!m_mapNode.valid())
        return;

    for (auto& [id, visual] : m_entities)
        visual.seen = false;

    for (const EntityState3D& state : entities) {
        auto it = m_entities.find(state.id);
        EntityVisual& visual = it != m_entities.end() ? it->second : createEntity(state);
        updateEntity(visual, state);
        visual.seen = true;
    }

    for (auto it = m_entities.begin(); it != m_entities.end();) {
        if (!it->second.seen) {
            m_entityRoot->removeChild(it->second.geo.get());
            if (it->first == m_selectedId) {
                m_selectedId = 0;
                m_manip->clearViewpoint();
                emit selectionChanged(QString());
            }
            it = m_entities.erase(it);
        }
        else {
            ++it;
        }
    }
}

Viewpoint GlobeView3D::homeViewpoint() const
{
    Viewpoint vp;
    vp.focalPoint() = GeoPoint(SpatialReference::get("wgs84"), m_homeLon, m_homeLat, 0.0, ALTMODE_ABSOLUTE);
    vp.heading() = Angle(0.0, Units::DEGREES);
    vp.pitch() = Angle(-35.0, Units::DEGREES);
    vp.range() = Distance(40000.0, Units::METERS);
    return vp;
}

void GlobeView3D::goHome()
{
    if (!m_manip.valid())
        return;
    m_manip->clearViewpoint();
    m_manip->setViewpoint(homeViewpoint(), 1.0);
}

void GlobeView3D::selectNextEntity()
{
    if (m_entities.empty())
        return;
    auto it = m_entities.find(m_selectedId);
    it = (it == m_entities.end() || std::next(it) == m_entities.end()) ? m_entities.begin() : std::next(it);
    m_selectedId = it->first;
    emit selectionChanged(selectedLabel());
}

QString GlobeView3D::selectedLabel() const
{
    auto it = m_entities.find(m_selectedId);
    if (it == m_entities.end())
        return QString();
    return it->second.label.isEmpty() ? QStringLiteral("#%1").arg(m_selectedId) : it->second.label;
}

void GlobeView3D::tether(double rangeMeters, double pitchDeg)
{
    if (!m_manip.valid())
        return;
    if (m_selectedId == 0 || !m_entities.count(m_selectedId))
        selectNextEntity();
    auto it = m_entities.find(m_selectedId);
    if (it == m_entities.end())
        return;

    Viewpoint vp;
    vp.setNode(it->second.geo.get());
    vp.heading() = Angle(0.0, Units::DEGREES);
    vp.pitch() = Angle(pitchDeg, Units::DEGREES);
    vp.range() = Distance(rangeMeters, Units::METERS);
    m_manip->setViewpoint(vp, 1.5);
}

void GlobeView3D::chaseSelected() { tether(250.0, -20.0); }
void GlobeView3D::stealthSelected() { tether(60.0, -10.0); }   // eye ~10 m up: lower risks going under rising ground

void GlobeView3D::untether()
{
    if (m_manip.valid())
        m_manip->clearViewpoint();
}

QString GlobeView3D::viewpointDescription() const
{
    if (!m_manip.valid())
        return QString();
    return QString::fromStdString(m_manip->getViewpoint().toString());
}

bool GlobeView3D::saveSnapshot(const QString& pngPath)
{
    return grabFramebuffer().save(pngPath);
}

void GlobeView3D::resizeGL(int w, int h)
{
    const int pw = int(w * devicePixelRatioF());
    const int ph = int(h * devicePixelRatioF());
    m_window->resized(0, 0, pw, ph);
    eventQueue()->windowResize(0, 0, pw, ph);
    m_viewer->getCamera()->setViewport(0, 0, pw, ph);
    m_viewer->getCamera()->setProjectionMatrixAsPerspective(35.0, double(pw) / std::max(ph, 1), 1.0, 1.0e7);
}

void GlobeView3D::paintGL()
{
    // Qt may recreate its FBO on resize; OSG must render into the current one.
    m_window->setDefaultFboId(defaultFramebufferObject());
    if (m_mapNode.valid() && m_preFrame)
        m_preFrame();
    m_viewer->frame();

    // Before the first frame the manipulator is not attached to the MapNode yet.
    if (!m_homeApplied && m_mapNode.valid()) {
        m_homeApplied = true;
        m_manip->setViewpoint(homeViewpoint(), 0.0);
    }
}

osgGA::EventQueue* GlobeView3D::eventQueue() const
{
    return m_window->getEventQueue();
}

void GlobeView3D::mousePressEvent(QMouseEvent* e)
{
    eventQueue()->mouseButtonPress(scaledX(e->position().x()), scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::mouseReleaseEvent(QMouseEvent* e)
{
    eventQueue()->mouseButtonRelease(scaledX(e->position().x()), scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::mouseMoveEvent(QMouseEvent* e)
{
    eventQueue()->mouseMotion(scaledX(e->position().x()), scaledY(e->position().y()));
}

void GlobeView3D::mouseDoubleClickEvent(QMouseEvent* e)
{
    eventQueue()->mouseDoubleButtonPress(scaledX(e->position().x()), scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::wheelEvent(QWheelEvent* e)
{
    eventQueue()->mouseScroll(e->angleDelta().y() > 0
        ? osgGA::GUIEventAdapter::SCROLL_UP
        : osgGA::GUIEventAdapter::SCROLL_DOWN);
}

void GlobeView3D::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_N: selectNextEntity(); return;
    case Qt::Key_T: chaseSelected(); return;
    case Qt::Key_F: stealthSelected(); return;
    case Qt::Key_U: untether(); return;
    case Qt::Key_H: goHome(); return;
    default: break;
    }
    if (!e->text().isEmpty())
        eventQueue()->keyPress(e->text().at(0).toLatin1());
}

void GlobeView3D::keyReleaseEvent(QKeyEvent* e)
{
    if (!e->text().isEmpty())
        eventQueue()->keyRelease(e->text().at(0).toLatin1());
}
