#include "earthview3d/GlobeView3D.h"
#include "earthview3d/EntityDiff.h"
#include "earthview3d/Runtime.h"

#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>

#include <osg/AutoTransform>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osg/ref_ptr>
#include <osgDB/ReadFile>
#include <osgDB/Registry>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>

#include <osgEarth/Common>
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
#include <unordered_map>

using namespace osgEarth;
using namespace osgEarth::Util;

namespace earthview3d {

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

void initializeOsgEarthOnce(const MapConfig& config)
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    // osgEarth's registry reads the default cache location once, when it is created.
    const QString cacheDir = config.resolvedCacheDir();
    if (!cacheDir.isEmpty() && qEnvironmentVariableIsEmpty("OSGEARTH_CACHE_PATH")) {
        QDir().mkpath(cacheDir);
        qputenv("OSGEARTH_CACHE_PATH", QDir::toNativeSeparators(cacheDir).toLocal8Bit());
    }
    osgEarth::initialize();
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Embedded-osgEarth requirements learned in the spike and the host test (§3.3):
//   - per-widget compatibility-profile surface format (the host's default may be core)
//   - vertex-attribute aliasing + matrix uniforms set by hand (no realize operation)
//   - Qt's FBO id handed to OSG every frame
//   - MapNode::getTerrain() is null until the first frame
//   - home viewpoint registered with EarthManipulator, applied after the first frame
//   - no logarithmic depth buffer by default; small near/far ratio instead (D-010)
// ─────────────────────────────────────────────────────────────────────────────
class GlobeView3D::Private
{
public:
    struct EntityVisual
    {
        osg::ref_ptr<GeoTransform> geo;
        osg::ref_ptr<osg::MatrixTransform> attitude;
        int shape = -1;
        std::uint32_t rgba = 0;
        QString label;
        double groundAltM = 0.0;    // last terrain height found (tiles may still be loading)
        bool haveGround = false;
    };

    Private(GlobeView3D* view, const MapConfig& config, double homeLat, double homeLon)
        : q(view), config(config), homeLat(homeLat), homeLon(homeLon) {}

    void buildScene();
    osg::Node* createShape(int shape, std::uint32_t rgba) const;
    EntityVisual& createEntity(const EntityState3D& state);
    void updateEntity(EntityVisual& visual, const EntityState3D& state);
    void removeEntity(std::uint64_t id);
    void tether(double rangeMeters, double pitchDeg);
    Viewpoint homeViewpoint() const;
    osgGA::EventQueue* eventQueue() const { return window->getEventQueue(); }
    float scaledX(double x) const { return float(x * q->devicePixelRatioF()); }
    float scaledY(double y) const { return float(y * q->devicePixelRatioF()); }

    GlobeView3D* q;
    MapConfig config;
    double homeLat;
    double homeLon;

    osg::ref_ptr<osgViewer::Viewer> viewer;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> window;
    osg::ref_ptr<MapNode> mapNode;
    osg::ref_ptr<EarthManipulator> manip;
    osg::ref_ptr<osg::Group> entityRoot;

    EntityDiff diff;
    std::unordered_map<std::uint64_t, EntityVisual> entities;
    std::uint64_t selectedId = 0;
    bool homeApplied = false;

    std::function<void()> preFrame;
    QTimer frameTimer;
};

GlobeView3D::GlobeView3D(const MapConfig& config, double homeLatDeg, double homeLonDeg, QWidget* parent)
    : QOpenGLWidget(parent)
    , d(std::make_unique<Private>(this, config, homeLatDeg, homeLonDeg))
{
    if (!isRuntimeConfigured())
        configureRuntime(RuntimePaths::buildTreeDefaults());
    initializeOsgEarthOnce(config);
    if (!config.dataRoot.isEmpty()) {
        const std::string dataRoot = config.dataRoot.toStdString();
        osgDB::FilePathList& dataPaths = osgDB::Registry::instance()->getDataFilePathList();
        if (std::find(dataPaths.begin(), dataPaths.end(), dataRoot) == dataPaths.end())
            dataPaths.push_front(dataRoot);
    }

    setFormat(osgEarthSurfaceFormat());
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    d->viewer = new osgViewer::Viewer();
    d->viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    d->window = d->viewer->setUpViewerAsEmbeddedInWindow(0, 0, width(), height());

    connect(&d->frameTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    d->frameTimer.start(0);
}

GlobeView3D::~GlobeView3D()
{
    // Release every OSG object while the context is current so GL resources are freed.
    makeCurrent();
    d->entities.clear();
    d->entityRoot = nullptr;
    d->manip = nullptr;
    d->mapNode = nullptr;
    d->window = nullptr;
    d->viewer = nullptr;
    doneCurrent();
}

const MapConfig& GlobeView3D::mapConfig() const
{
    return d->config;
}

void GlobeView3D::setPreFrameCallback(std::function<void()> callback)
{
    d->preFrame = std::move(callback);
}

void GlobeView3D::initializeGL()
{
    d->window->setDefaultFboId(defaultFramebufferObject());

    osg::State* state = d->window->getState();
    state->setUseVertexAttributeAliasing(true);
    state->setUseModelViewAndProjectionUniforms(true);

    d->buildScene();
}

void GlobeView3D::Private::buildScene()
{
    QString error;
    if (!config.validate(&error)) {
        emit q->loadFailed(error);
        return;
    }

    const QString earthFile = config.resolvedEarthFile();
    osg::ref_ptr<osg::Node> loaded = osgDB::readRefNodeFile(earthFile.toStdString());
    mapNode = MapNode::get(loaded.get());
    if (!mapNode.valid()) {
        emit q->loadFailed(QStringLiteral("Could not load earth file %1").arg(earthFile));
        return;
    }

    osg::Camera* camera = viewer->getCamera();
    GLUtils::setGlobalDefaults(camera->getOrCreateStateSet());

    // osgEarth's LogarithmicDepthBuffer drops the terrain next to the camera in
    // this embedded setup (ground-level stealth views showed black/red-yellow
    // below the horizon), so it is opt-in. A small near/far ratio keeps close
    // ground visible instead (D-010).
    if (qEnvironmentVariableIntValue("EARTHVIEW3D_LOGDEPTH") == 1) {
        static LogarithmicDepthBuffer logDepth;
        logDepth.install(camera);
    }
    camera->setNearFarRatio(0.00002);

    osg::ref_ptr<osg::Group> root;
    if (config.sky && qEnvironmentVariableIntValue("EARTHVIEW3D_NO_SKY") != 1) {
        const QDateTime t = config.effectiveSkyTime();
        const double hours = t.time().hour() + t.time().minute() / 60.0 + t.time().second() / 3600.0;
        osg::ref_ptr<SkyNode> sky = SkyNode::create();
        sky->setDateTime(DateTime(t.date().year(), t.date().month(), t.date().day(), hours));
        sky->attach(viewer.get(), 0);
        root = sky;
    }
    else {
        root = new osg::Group();
    }
    root->addChild(loaded.get());

    entityRoot = new osg::Group();
    GLUtils::setLighting(entityRoot->getOrCreateStateSet(), osg::StateAttribute::OFF);
    mapNode->addChild(entityRoot.get());

    manip = new EarthManipulator();
    manip->getSettings()->setTetherMode(EarthManipulator::TETHER_CENTER_AND_HEADING);
    manip->getSettings()->setMinMaxDistance(5.0, 4.0e7);
    manip->getSettings()->setTerrainAvoidanceEnabled(true);
    manip->getSettings()->setTerrainAvoidanceMinimumDistance(5.0);
    manip->setHomeViewpoint(homeViewpoint());
    viewer->setCameraManipulator(manip.get());
    viewer->setSceneData(root.get());
}

osg::Node* GlobeView3D::Private::createShape(int shape, std::uint32_t rgba) const
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

GlobeView3D::Private::EntityVisual& GlobeView3D::Private::createEntity(const EntityState3D& state)
{
    EntityVisual& visual = entities[state.id];
    visual.geo = new GeoTransform();
    visual.attitude = new osg::MatrixTransform();
    visual.attitude->addChild(createShape(state.shape, state.rgba));
    visual.geo->addChild(visual.attitude.get());
    visual.shape = state.shape;
    visual.rgba = state.rgba;
    entityRoot->addChild(visual.geo.get());
    return visual;
}

void GlobeView3D::Private::updateEntity(EntityVisual& visual, const EntityState3D& state)
{
    if (state.shape != visual.shape || state.rgba != visual.rgba) {
        visual.attitude->removeChildren(0, visual.attitude->getNumChildren());
        visual.attitude->addChild(createShape(state.shape, state.rgba));
        visual.shape = state.shape;
        visual.rgba = state.rgba;
    }
    visual.label = state.label;
    visual.geo->setNodeMask(state.visible ? ~0u : 0u);

    const SpatialReference* wgs84 = mapNode->getMapSRS()->getGeographicSRS();
    double altM = state.altM;
    if (state.clampToTerrain) {
        double terrainM = 0.0;
        Terrain* terrain = mapNode->getTerrain();
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

void GlobeView3D::Private::removeEntity(std::uint64_t id)
{
    auto it = entities.find(id);
    if (it == entities.end())
        return;
    entityRoot->removeChild(it->second.geo.get());
    entities.erase(it);
    if (id == selectedId) {
        selectedId = 0;
        manip->clearViewpoint();
        emit q->selectionChanged(QString());
    }
}

void GlobeView3D::setEntities(const std::vector<EntityState3D>& entities)
{
    if (!d->mapNode.valid())
        return;

    const EntityDiff::Changes changes = d->diff.apply(entities);
    for (std::size_t index : changes.added) {
        const EntityState3D& state = entities[index];
        d->updateEntity(d->createEntity(state), state);
    }
    for (std::size_t index : changes.updated) {
        const EntityState3D& state = entities[index];
        d->updateEntity(d->entities[state.id], state);
    }
    for (std::uint64_t id : changes.removed)
        d->removeEntity(id);
}

int GlobeView3D::entityCount() const
{
    return int(d->entities.size());
}

Viewpoint GlobeView3D::Private::homeViewpoint() const
{
    Viewpoint vp;
    vp.focalPoint() = GeoPoint(SpatialReference::get("wgs84"), homeLon, homeLat, 0.0, ALTMODE_ABSOLUTE);
    vp.heading() = Angle(0.0, Units::DEGREES);
    vp.pitch() = Angle(-35.0, Units::DEGREES);
    vp.range() = Distance(40000.0, Units::METERS);
    return vp;
}

void GlobeView3D::goHome()
{
    if (!d->manip.valid())
        return;
    d->manip->clearViewpoint();
    d->manip->setViewpoint(d->homeViewpoint(), 1.0);
}

void GlobeView3D::selectNextEntity()
{
    if (d->entities.empty())
        return;
    auto it = d->entities.find(d->selectedId);
    it = (it == d->entities.end() || std::next(it) == d->entities.end()) ? d->entities.begin() : std::next(it);
    d->selectedId = it->first;
    emit selectionChanged(selectedLabel());
}

std::uint64_t GlobeView3D::selectedId() const
{
    return d->selectedId;
}

QString GlobeView3D::selectedLabel() const
{
    auto it = d->entities.find(d->selectedId);
    if (it == d->entities.end())
        return QString();
    return it->second.label.isEmpty() ? QStringLiteral("#%1").arg(d->selectedId) : it->second.label;
}

void GlobeView3D::Private::tether(double rangeMeters, double pitchDeg)
{
    if (!manip.valid())
        return;
    if (selectedId == 0 || !entities.count(selectedId))
        q->selectNextEntity();
    auto it = entities.find(selectedId);
    if (it == entities.end())
        return;

    Viewpoint vp;
    vp.setNode(it->second.geo.get());
    vp.heading() = Angle(0.0, Units::DEGREES);
    vp.pitch() = Angle(pitchDeg, Units::DEGREES);
    vp.range() = Distance(rangeMeters, Units::METERS);
    manip->setViewpoint(vp, 1.5);
}

void GlobeView3D::chaseSelected() { d->tether(250.0, -20.0); }
void GlobeView3D::stealthSelected() { d->tether(60.0, -10.0); }   // eye ~10 m up: lower risks going under rising ground

void GlobeView3D::untether()
{
    if (d->manip.valid())
        d->manip->clearViewpoint();
}

QString GlobeView3D::viewpointDescription() const
{
    if (!d->manip.valid())
        return QString();
    return QString::fromStdString(d->manip->getViewpoint().toString());
}

bool GlobeView3D::saveSnapshot(const QString& pngPath)
{
    return grabFramebuffer().save(pngPath);
}

void GlobeView3D::resizeGL(int w, int h)
{
    const int pw = int(w * devicePixelRatioF());
    const int ph = int(h * devicePixelRatioF());
    d->window->resized(0, 0, pw, ph);
    d->eventQueue()->windowResize(0, 0, pw, ph);
    d->viewer->getCamera()->setViewport(0, 0, pw, ph);
    d->viewer->getCamera()->setProjectionMatrixAsPerspective(35.0, double(pw) / std::max(ph, 1), 1.0, 1.0e7);
}

void GlobeView3D::paintGL()
{
    // Qt may recreate its FBO on resize; OSG must render into the current one.
    d->window->setDefaultFboId(defaultFramebufferObject());
    if (d->mapNode.valid() && d->preFrame)
        d->preFrame();
    d->viewer->frame();

    // Before the first frame the manipulator is not attached to the MapNode yet.
    if (!d->homeApplied && d->mapNode.valid()) {
        d->homeApplied = true;
        d->manip->setViewpoint(d->homeViewpoint(), 0.0);
    }
}

void GlobeView3D::mousePressEvent(QMouseEvent* e)
{
    d->eventQueue()->mouseButtonPress(d->scaledX(e->position().x()), d->scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::mouseReleaseEvent(QMouseEvent* e)
{
    d->eventQueue()->mouseButtonRelease(d->scaledX(e->position().x()), d->scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::mouseMoveEvent(QMouseEvent* e)
{
    d->eventQueue()->mouseMotion(d->scaledX(e->position().x()), d->scaledY(e->position().y()));
}

void GlobeView3D::mouseDoubleClickEvent(QMouseEvent* e)
{
    d->eventQueue()->mouseDoubleButtonPress(d->scaledX(e->position().x()), d->scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::wheelEvent(QWheelEvent* e)
{
    d->eventQueue()->mouseScroll(e->angleDelta().y() > 0
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
        d->eventQueue()->keyPress(e->text().at(0).toLatin1());
}

void GlobeView3D::keyReleaseEvent(QKeyEvent* e)
{
    if (!e->text().isEmpty())
        d->eventQueue()->keyRelease(e->text().at(0).toLatin1());
}

} // namespace earthview3d
