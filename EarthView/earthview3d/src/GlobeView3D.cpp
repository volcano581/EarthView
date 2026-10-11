#include "earthview3d/GlobeView3D.h"
#include "earthview3d/EntityDiff.h"
#include "earthview3d/ModelTable.h"
#include "earthview3d/Picking.h"
#include "earthview3d/Runtime.h"

#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>

#include <osg/AutoTransform>
#include <osg/Geometry>
#include <osg/LOD>
#include <osg/LineWidth>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osgText/Text>
#include <osg/ref_ptr>
#include <osgDB/ReadFile>
#include <osgDB/Registry>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>

#include <osgEarth/AnnotationUtils>
#include <osgEarth/Common>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GeoTransform>
#include <osgEarth/GLUtils>
#include <osgEarth/LogarithmicDepthBuffer>
#include <osgEarth/MapNode>
#include <osgEarth/ScreenSpaceLayout>
#include <osgEarth/Sky>
#include <osgEarth/Terrain>
#include <osgEarth/TextSymbol>
#include <osgEarth/Viewpoint>

#include <algorithm>
#include <cfloat>
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
constexpr float kSelectionRingPixels = 18.0f;   // radius of the selection highlight
constexpr double kPickRadiusPixels = 12.0;
constexpr double kClickSlopPixels = 4.0;      // a press/release further apart is a drag

osg::Matrix attitudeMatrix(double headingDeg, double pitchDeg, double rollDeg)
{
    // Local frame of a GeoTransform: X east, Y north, Z up; models face +Y (D-005).
    // OSG multiplies row vectors, so the left-most matrix applies first: roll, pitch, heading.
    return osg::Matrix::rotate(rollDeg * kDegToRad, osg::Y_AXIS)
         * osg::Matrix::rotate(pitchDeg * kDegToRad, osg::X_AXIS)
         * osg::Matrix::rotate(-headingDeg * kDegToRad, osg::Z_AXIS);
}

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
        osg::ref_ptr<osg::Node> highlight;
        osg::ref_ptr<osg::Geode> labelGeode;
        osg::ref_ptr<osgText::Text> labelText;
        ResolvedStyle style;
        bool haveContent = false;
        bool visible = true;
        double groundAltM = 0.0;    // last terrain height found (tiles may still be loading)
        bool haveGround = false;
    };

    Private(GlobeView3D* view, const MapConfig& config, double homeLat, double homeLon)
        : q(view), config(config), homeLat(homeLat), homeLon(homeLon) {}

    void buildScene();
    osg::Node* createShape(int shape, std::uint32_t rgba) const;
    osg::Node* createContent(const ResolvedStyle& style);
    osg::Node* loadModel(const QString& file);
    osg::Node* createSelectionRing() const;
    void applySelectionLook(EntityVisual& visual, bool selected) const;
    std::uint64_t pickAt(double x, double y) const;
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

    ModelTable modelTable;
    bool haveModelTable = false;
    std::unordered_map<QString, osg::ref_ptr<osg::Node>> modelCache;   // null = failed to load
    osg::ref_ptr<osg::StateSet> labelStateSet;
    bool labelsVisible = true;
    QPointF pressPos;
    bool leftPressed = false;

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
    d->modelCache.clear();
    d->labelStateSet = nullptr;
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

    // Labels: one shared state set in osgEarth's screen-space layout bin, decluttered.
    labelStateSet = new osg::StateSet();
    ScreenSpaceLayout::activate(labelStateSet.get());
    ScreenSpaceLayout::setDeclutteringEnabled(true);

    const QString tablePath = config.resolvedModelTable();
    if (!tablePath.isEmpty()) {
        QString tableError;
        haveModelTable = ModelTable::fromJsonFile(tablePath, &modelTable, &tableError);
        if (!haveModelTable)
            emit q->loadFailed(QStringLiteral("Model table ignored (placeholders used): %1").arg(tableError));
    }

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

osg::Node* GlobeView3D::Private::loadModel(const QString& file)
{
    auto it = modelCache.find(file);
    if (it != modelCache.end())
        return it->second.get();
    osg::ref_ptr<osg::Node> model = osgDB::readRefNodeFile(file.toStdString());
    if (!model.valid())
        emit q->loadFailed(QStringLiteral("Model %1 could not be loaded; using a placeholder").arg(file));
    modelCache.emplace(file, model);   // remember failures too, so the file is tried once
    return model.get();
}

osg::Node* GlobeView3D::Private::createContent(const ResolvedStyle& style)
{
    osg::Node* marker = createShape(style.shape, style.rgba);
    osg::Node* model = style.modelFile.isEmpty() ? nullptr : loadModel(style.modelFile);
    if (!model)
        return marker;

    // Model when it covers enough pixels, the always-visible marker (icon) below that.
    auto* scaled = new osg::MatrixTransform(osg::Matrix::scale(style.scale, style.scale, style.scale));
    scaled->addChild(model);   // shared between entities of the same kind
    const float iconBelow = float(haveModelTable ? modelTable.iconBelowPixels() : 12.0);
    auto* lod = new osg::LOD();
    lod->setRangeMode(osg::LOD::PIXEL_SIZE_ON_SCREEN);
    lod->addChild(scaled, iconBelow, FLT_MAX);
    lod->addChild(marker, 0.0f, iconBelow);
    return lod;
}

osg::Node* GlobeView3D::Private::createSelectionRing() const
{
    // Unit circle facing the screen, scaled to kSelectionRingPixels.
    constexpr int kSegments = 48;
    auto* vertices = new osg::Vec3Array();
    for (int i = 0; i < kSegments; ++i) {
        const double a = 2.0 * kPi * i / kSegments;
        vertices->push_back(osg::Vec3(float(std::cos(a)), float(std::sin(a)), 0.0f));
    }
    auto* colors = new osg::Vec4Array();
    colors->push_back(osg::Vec4(1.0f, 0.85f, 0.1f, 1.0f));
    auto* ring = new osg::Geometry();
    ring->setVertexArray(vertices);
    ring->setColorArray(colors, osg::Array::BIND_OVERALL);
    ring->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, 0, kSegments));
    ring->getOrCreateStateSet()->setAttributeAndModes(new osg::LineWidth(2.5f));
    ring->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    ring->getOrCreateStateSet()->setRenderBinDetails(100, "RenderBin");

    auto* preScale = new osg::MatrixTransform(osg::Matrix::scale(kSelectionRingPixels, kSelectionRingPixels, 1.0));
    preScale->addChild(ring);
    auto* screen = new osg::AutoTransform();
    screen->setAutoRotateMode(osg::AutoTransform::ROTATE_TO_SCREEN);
    screen->setAutoScaleToScreen(true);
    screen->addChild(preScale);
    return screen;
}

void GlobeView3D::Private::applySelectionLook(EntityVisual& visual, bool selected) const
{
    visual.highlight->setNodeMask(selected ? ~0u : 0u);
    if (visual.labelText.valid())
        visual.labelText->setColor(selected ? osg::Vec4(1.0f, 0.85f, 0.1f, 1.0f) : osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));
    // Selected labels win decluttering.
    if (visual.labelText.valid())
        ScreenSpaceLayoutData::getOrCreate(visual.labelText.get())->setPriority(selected ? 1000.0f : 0.0f);
}

GlobeView3D::Private::EntityVisual& GlobeView3D::Private::createEntity(const EntityState3D& state)
{
    EntityVisual& visual = entities[state.id];
    visual.geo = new GeoTransform();
    visual.attitude = new osg::MatrixTransform();
    visual.geo->addChild(visual.attitude.get());

    visual.highlight = createSelectionRing();
    visual.geo->addChild(visual.highlight.get());

    TextSymbol symbol;
    symbol.fill().mutable_value().color() = Color::White;
    symbol.halo().mutable_value().color() = Color::Black;
    symbol.alignment() = TextSymbol::ALIGN_LEFT_CENTER;
    symbol.declutter() = true;
    visual.labelText = AnnotationUtils::createTextDrawable(std::string(), &symbol, osg::BoundingBox());
    visual.labelText->setDataVariance(osg::Object::DYNAMIC);
    ScreenSpaceLayoutData::getOrCreate(visual.labelText.get())->setPixelOffset(osg::Vec2s(12, 0));
    visual.labelGeode = new osg::Geode();
    visual.labelGeode->setStateSet(labelStateSet.get());
    visual.labelGeode->addDrawable(visual.labelText.get());
    visual.labelGeode->setNodeMask(labelsVisible ? ~0u : 0u);
    visual.geo->addChild(visual.labelGeode.get());

    applySelectionLook(visual, state.id == selectedId);
    entityRoot->addChild(visual.geo.get());
    return visual;
}

void GlobeView3D::Private::updateEntity(EntityVisual& visual, const EntityState3D& state)
{
    const ResolvedStyle style = resolveStyle(state, haveModelTable ? &modelTable : nullptr);
    if (!visual.haveContent || style.modelFile != visual.style.modelFile || style.shape != visual.style.shape
        || style.rgba != visual.style.rgba || style.scale != visual.style.scale) {
        visual.attitude->removeChildren(0, visual.attitude->getNumChildren());
        visual.attitude->addChild(createContent(style));
        visual.haveContent = true;
    }
    if (style.label != visual.style.label || visual.labelText->getText().empty())
        visual.labelText->setText(style.label.toStdString(), osgText::String::ENCODING_UTF8);
    visual.style = style;
    visual.visible = style.visible;
    visual.geo->setNodeMask(style.visible ? ~0u : 0u);

    const SpatialReference* wgs84 = mapNode->getMapSRS()->getGeographicSRS();
    double altM = state.altM;
    if (style.clampToTerrain) {
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
    visual.attitude->setMatrix(
        attitudeMatrix(style.headingOffsetDeg, style.pitchOffsetDeg, style.rollOffsetDeg)
        * attitudeMatrix(state.headingDeg, state.pitchDeg, state.rollDeg));
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

std::uint64_t GlobeView3D::Private::pickAt(double x, double y) const
{
    const osg::Camera* camera = viewer->getCamera();
    if (!camera->getViewport())
        return 0;
    const double dpr = q->devicePixelRatioF();
    const osg::Matrixd view = camera->getViewMatrix();
    const osg::Matrixd vpw = view * camera->getProjectionMatrix() * camera->getViewport()->computeWindowMatrix();
    const double windowHeight = camera->getViewport()->height();

    std::vector<PickCandidate> candidates;
    candidates.reserve(entities.size());
    for (const auto& [id, visual] : entities) {
        osg::Vec3d world;
        if (!visual.geo->getPosition().toWorld(world))
            continue;
        const osg::Vec3d eye = world * view;
        const osg::Vec3d screen = world * vpw;
        PickCandidate c;
        c.id = id;
        c.x = screen.x() / dpr;
        c.y = (windowHeight - screen.y()) / dpr;   // OSG window origin is bottom-left
        c.distanceM = eye.length();
        c.inFront = eye.z() < 0.0;
        c.visible = visual.visible;
        candidates.push_back(c);
    }
    return pickNearest(candidates, x, y, kPickRadiusPixels);
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
    setSelected(it->first);
}

void GlobeView3D::setSelected(quint64 id)
{
    if (id != 0 && !d->entities.count(id))
        id = 0;
    if (auto old = d->entities.find(d->selectedId); old != d->entities.end())
        d->applySelectionLook(old->second, false);
    d->selectedId = id;
    if (auto now = d->entities.find(id); now != d->entities.end())
        d->applySelectionLook(now->second, true);
    emit selectionChanged(selectedLabel());
}

void GlobeView3D::setLabelsVisible(bool visible)
{
    d->labelsVisible = visible;
    for (auto& [id, visual] : d->entities)
        visual.labelGeode->setNodeMask(visible ? ~0u : 0u);
}

std::uint64_t GlobeView3D::selectedId() const
{
    return d->selectedId;
}

bool GlobeView3D::entityPosition(std::uint64_t id, double* latDeg, double* lonDeg, double* altM) const
{
    auto it = d->entities.find(id);
    if (it == d->entities.end())
        return false;
    const GeoPoint& p = it->second.geo->getPosition();
    if (latDeg)
        *latDeg = p.y();
    if (lonDeg)
        *lonDeg = p.x();
    if (altM)
        *altM = p.z();
    return true;
}

QString GlobeView3D::selectedLabel() const
{
    auto it = d->entities.find(d->selectedId);
    if (it == d->entities.end())
        return QString();
    return it->second.style.label;
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
    if (e->button() == Qt::LeftButton) {
        d->pressPos = e->position();
        d->leftPressed = true;
    }
    d->eventQueue()->mouseButtonPress(d->scaledX(e->position().x()), d->scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::mouseReleaseEvent(QMouseEvent* e)
{
    // A left click (not a drag) picks the entity nearest the cursor.
    if (e->button() == Qt::LeftButton && d->leftPressed && d->mapNode.valid()) {
        d->leftPressed = false;
        const QPointF delta = e->position() - d->pressPos;
        if (std::hypot(delta.x(), delta.y()) <= kClickSlopPixels) {
            if (const std::uint64_t id = d->pickAt(e->position().x(), e->position().y())) {
                setSelected(id);
                emit entityPicked(id);
            }
        }
    }
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
