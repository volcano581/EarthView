#include "earthview3d/GlobeView3D.h"
#include "earthview3d/EntityDiff.h"
#include "earthview3d/ModelTable.h"
#include "earthview3d/Picking.h"
#include "earthview3d/Runtime.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

#include <osg/AutoTransform>
#include <osg/Depth>
#include <osg/Geometry>
#include <osg/LOD>
#include <osg/LineWidth>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osgText/Font>
#include <osgText/Text>
#include <osg/ref_ptr>
#include <osgDB/ReadFile>
#include <osgDB/Registry>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>

#include <osgEarth/AnnotationUtils>
#include <osgEarth/Common>
#include <osgEarth/EarthManipulator>
#include <osgEarth/ElevationPool>
#include <osgEarth/GeoTransform>
#include <osgEarth/GLUtils>
#include <osgEarth/LogarithmicDepthBuffer>
#include <osgEarth/MapNode>
#include <osgEarth/Registry>
#include <osgEarth/ScreenSpaceLayout>
#include <osgEarth/Sky>
#include <osgEarth/Terrain>
#include <osgEarth/TextSymbol>
#include <osgEarth/VerticalDatum>
#include <osgEarth/Viewpoint>
#include <osgEarth/VisibleLayer>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <set>
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
        osg::ref_ptr<osg::AutoTransform> highlight;
        osg::ref_ptr<osg::Geode> labelGeode;
        osg::ref_ptr<osgText::Text> labelText;
        ResolvedStyle style;
        bool haveContent = false;
        bool visible = true;
        double groundAltM = 0.0;    // last terrain height found (tiles may still be loading)
        bool haveGround = false;
        bool haveGroundOffset = false;
        double groundOffsetM = 0.0; // terrain - host altitude (clamped entities; I-015 overlay)
        double headingDeg = 0.0;    // latest attitude, for the first-person camera
        double pitchDeg = 0.0;
        double rollDeg = 0.0;
    };

    Private(GlobeView3D* view, const MapConfig& config, double homeLat, double homeLon)
        : q(view), config(config), homeLat(homeLat), homeLon(homeLon) {}

    void buildScene();
    osg::Node* createShape(int shape, std::uint32_t rgba) const;
    osg::Node* createContent(const ResolvedStyle& style);
    osg::Node* loadModel(const QString& file);
    osg::AutoTransform* createSelectionRing() const;
    double contentCentreZ(const ResolvedStyle& style) const;
    osgText::Font* loadLabelFont() const;
    void applySelectionLook(EntityVisual& visual, bool selected) const;
    std::uint64_t pickAt(double x, double y) const;
    EntityVisual& createEntity(const EntityState3D& state);
    void updateEntity(EntityVisual& visual, const EntityState3D& state);
    void removeEntity(std::uint64_t id);
    void enterMode(CameraMode mode);
    bool eyePose(CameraPose* pose) const;
    void applyPose(const CameraPose& pose);
    bool groundAt(double latDeg, double lonDeg, double* altM) const;
    bool terrainHeight(double latDeg, double lonDeg, double* altM) const;
    void installTileCallback();
    void resolveHeightRequests();
    bool targetPose(double dt, CameraPose* pose);
    void updateCamera(double dt);
    void hideOwnEntity(std::uint64_t id);
    void updateHud(double dt);
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
    osg::ref_ptr<TextSymbol> labelSymbol;   // shared by all labels
    osg::ref_ptr<osgText::Font> labelFont;  // null = osgEarth's default font
    bool labelsVisible = true;
    QPointF pressPos;
    bool leftPressed = false;

    EntityDiff diff;
    std::unordered_map<std::uint64_t, EntityVisual> entities;
    std::uint64_t selectedId = 0;
    bool homeApplied = false;
    // A camera mode requested before the home viewpoint was applied (first frame), or before
    // any entity exists, waits here; otherwise the home viewpoint, applied right after frame 1,
    // would replace it (E3 review).
    bool modePending = false;
    CameraMode pendingMode = CameraMode::Orbit;

    // Cameras (E4). Orbit = EarthManipulator; the other modes set the view matrix directly
    // (manipulator detached) so the eye is clamped above the terrain every frame.
    CameraMode mode = CameraMode::Orbit;
    bool chaseFreeLook = false;
    double chaseRangeM = 250.0;
    double chaseElevationDeg = 20.0;
    double stealthEyeHeightM = 10.0;
    double stealthBackM = 60.0;
    double lookYawDeg = 0.0;        // mouse look offsets (chase free-look, stealth, first person)
    double lookPitchDeg = 0.0;
    double smoothHeadingDeg = 0.0;  // target heading, low-pass filtered (chase/stealth follow)
    bool haveSmoothHeading = false;
    double freeLookBaseDeg = 0.0;   // chase free-look: heading frozen when free look starts
    CameraPose flyPose;             // free-fly state
    double flySpeedScale = 1.0;
    std::set<int> keysDown;         // free-fly movement keys
    CameraPose blendFrom;           // smooth transition into a mode
    bool blending = false;
    QElapsedTimer blendClock;
    TerrainClearance eyeGround;     // last good terrain height under the eye
    std::uint64_t hiddenId = 0;     // first person: the target's own marker/label are hidden

    // Terrain services (E5): cached heights of the drawn terrain, refreshed per loaded tile.
    mutable HeightCache heights;
    osg::ref_ptr<TerrainCallback> tileCallback;
    osg::observer_ptr<Terrain> tileCallbackTerrain;
    const VerticalDatum* egm96 = nullptr;   // owned by osgEarth's datum cache
    bool groundOffsetOverlay = false;
    GroundOffsetStats groundOffsets;        // filled per setEntities() call
    QElapsedTimer stepClock;
    QPointF lastMousePos;
    bool looking = false;           // left button dragged beyond the click slop

    // HUD overlay (child QLabel; composited over the GL content by Qt).
    QLabel* hud = nullptr;
    bool hudVisible = true;
    QElapsedTimer hudClock;
    osg::Vec3d lastEyeWorld;
    bool haveLastEye = false;
    double eyeSpeedMps = 0.0;
    QElapsedTimer frameClock;
    double frameMsAvg = 0.0;        // smoothed frame time, reported by viewpointDescription()

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

    // HUD overlay (E4): a child label, composited by Qt over the GL content.
    d->hud = new QLabel(this);
    d->hud->setObjectName(QStringLiteral("earthview3dHud"));
    d->hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    d->hud->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(0, 0, 0, 150); color: #e8f4e8; padding: 6px 8px; border-radius: 3px;"
        " font-family: Consolas, 'DejaVu Sans Mono', monospace; font-size: 12px; }"));
    d->hud->move(10, 10);

    connect(&d->frameTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    d->frameTimer.start(0);
}

GlobeView3D::~GlobeView3D()
{
    // Release every OSG object while the context is current so GL resources are freed.
    makeCurrent();
    osg::ref_ptr<Terrain> terrain;
    if (d->tileCallback.valid() && d->tileCallbackTerrain.lock(terrain))
        terrain->removeTerrainCallback(d->tileCallback.get());
    d->tileCallback = nullptr;
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
    // Same state as osgEarth's LabelNode: layout bin, no depth test/writes, no lighting.
    labelStateSet = new osg::StateSet();
    ScreenSpaceLayout::activate(labelStateSet.get());
    labelStateSet->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS, 0.0, 1.0, false),
                                        osg::StateAttribute::ON | osg::StateAttribute::PROTECTED);
    GLUtils::setLighting(labelStateSet.get(), osg::StateAttribute::OFF | osg::StateAttribute::PROTECTED);
    ScreenSpaceLayout::setDeclutteringEnabled(qEnvironmentVariableIntValue("EARTHVIEW3D_NO_DECLUTTER") != 1);

    // osg::Referenced objects live on the heap, held by ref_ptr (createTextDrawable keeps a
    // reference to the symbol and would delete a stack object).
    labelSymbol = new TextSymbol();
    labelSymbol->fill().mutable_value().color() = Color::White;
    labelSymbol->halo().mutable_value().color() = Color::Black;
    labelSymbol->alignment() = TextSymbol::ALIGN_LEFT_CENTER;
    labelSymbol->declutter() = true;
    labelFont = loadLabelFont();

    // EGM96 geoid (embedded in osgEarth's vdatum_egm96 plugin; works offline). Used for MSL
    // readouts; the DTED layer itself is declared vdatum="egm96" in the earth file (D-021).
    egm96 = VerticalDatum::get("egm96");
    if (!egm96)
        emit q->loadFailed(QStringLiteral("EGM96 vertical datum not available (osgdb_osgearth_vdatum_egm96 plugin missing); MSL readouts disabled"));

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

double GlobeView3D::Private::contentCentreZ(const ResolvedStyle& style) const
{
    if (!style.modelFile.isEmpty()) {
        auto it = modelCache.find(style.modelFile);
        if (it != modelCache.end() && it->second.valid())
            return it->second->getBound().center().z() * style.scale;
    }
    switch (style.shape) {   // centres used by createShape(), at real size
    case 0: return 2.0;      // sphere
    case 2: return 1.5;      // box
    default: return 0.0;     // cone and cylinder are centred on the origin after alignment
    }
}

osgText::Font* GlobeView3D::Private::loadLabelFont() const
{
    // Loaded here, after configureRuntime() put the freetype plugin on the path: osgEarth
    // resolves its own default font when its DLL loads, which may be too early (E3 review).
    QString name = qEnvironmentVariable("EARTHVIEW3D_LABEL_FONT");
#ifdef _WIN32
    if (name.isEmpty())
        name = QStringLiteral("arial.ttf");
#endif
    if (!name.isEmpty()) {
        osg::ref_ptr<osgText::Font> font = osgText::readRefFontFile(name.toStdString());
        if (font.valid())
            return font.release();
        emit q->loadFailed(QStringLiteral("Label font %1 could not be loaded; using the default font").arg(name));
    }
    return Registry::instance()->getDefaultFont();
}

osg::AutoTransform* GlobeView3D::Private::createSelectionRing() const
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

    // Zero-size alignment box at the anchor: a default osg::BoundingBox is invalid
    // (xMax = -FLT_MAX) and would place the text far off screen.
    const osg::BoundingBox anchor(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    visual.labelText = AnnotationUtils::createTextDrawable(std::string(), labelSymbol.get(), anchor);
    visual.labelText->setDataVariance(osg::Object::DYNAMIC);
    if (labelFont.valid())
        visual.labelText->setFont(labelFont.get());
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
        // Centre the selection ring on the marker/model rather than its ground point.
        visual.highlight->setPosition(osg::Vec3d(0.0, 0.0, contentCentreZ(style)));
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
        if (terrainHeight(state.latDeg, state.lonDeg, &terrainM)) {
            visual.groundAltM = terrainM;
            visual.haveGround = true;
        }
        if (visual.haveGround) {
            altM = visual.groundAltM;
            visual.groundOffsetM = altM - state.altM;
            visual.haveGroundOffset = true;
            groundOffsets.add(visual.groundOffsetM);
        }
    }
    visual.geo->setPosition(GeoPoint(wgs84, state.lonDeg, state.latDeg, altM, ALTMODE_ABSOLUTE));
    visual.headingDeg = state.headingDeg;
    visual.pitchDeg = state.pitchDeg;
    visual.rollDeg = state.rollDeg;
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

    d->groundOffsets = GroundOffsetStats();
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
    d->modePending = false;
    if (!d->manip.valid())
        return;
    d->enterMode(CameraMode::Orbit);
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
    if (auto now = d->entities.find(id); now != d->entities.end() && id != d->hiddenId)
        d->applySelectionLook(now->second, true);
    emit selectionChanged(selectedLabel());
}

void GlobeView3D::setLabelsVisible(bool visible)
{
    d->labelsVisible = visible;
    for (auto& [id, visual] : d->entities)
        visual.labelGeode->setNodeMask(visible && id != d->hiddenId ? ~0u : 0u);
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

// ── Cameras (E4) ─────────────────────────────────────────────────────────────

namespace {
bool needsTarget(CameraMode mode)
{
    return mode == CameraMode::Chase || mode == CameraMode::Stealth || mode == CameraMode::FirstPerson;
}

constexpr double kBlendSeconds = 1.0;          // smooth transition into a mode
constexpr double kHeadingFilterSeconds = 0.6;  // chase/stealth follow the target heading softly
constexpr double kLookDegPerPixel = 0.2;
constexpr double kGroundEyeHeightM = 2.5;      // first-person eye above a ground entity's origin
constexpr double kTargetCentreM = 2.0;         // chase/stealth look at the marker centre
} // namespace

void GlobeView3D::Private::enterMode(CameraMode newMode)
{
    if (!manip.valid())
        return;
    if (!homeApplied || (needsTarget(newMode) && entities.empty())) {
        modePending = true;
        pendingMode = newMode;
        return;
    }
    modePending = false;
    if (needsTarget(newMode)) {
        if (selectedId == 0 || !entities.count(selectedId))
            q->selectNextEntity();
        if (!entities.count(selectedId))
            return;
    }

    CameraPose current;
    const bool haveCurrent = eyePose(&current);
    if (newMode == CameraMode::Orbit) {
        if (mode != CameraMode::Orbit) {
            // Hand the current eye back to the manipulator; it orbits from where we are.
            viewer->setCameraManipulator(manip.get(), false);
            manip->setByMatrix(viewer->getCamera()->getInverseViewMatrix());
        }
    }
    else {
        if (mode == CameraMode::Orbit) {
            manip->clearViewpoint();
            viewer->setCameraManipulator(nullptr, false);
        }
        lookYawDeg = 0.0;
        lookPitchDeg = 0.0;
        haveSmoothHeading = false;
        keysDown.clear();
        if (newMode == CameraMode::FreeFly && haveCurrent) {
            flyPose = current;
            flyPose.rollDeg = 0.0;
        }
        blendFrom = current;
        blending = haveCurrent && newMode != CameraMode::FreeFly;
        blendClock.restart();
    }
    const bool changed = newMode != mode;
    mode = newMode;
    stepClock.restart();
    qInfo().noquote() << QStringLiteral("earthview3d: camera %1%2").arg(cameraModeName(mode))
                             .arg(needsTarget(mode) ? QStringLiteral(" on entity %1").arg(selectedId) : QString());
    if (changed)
        emit q->cameraModeChanged(mode);
}

bool GlobeView3D::Private::eyePose(CameraPose* pose) const
{
    if (!mapNode.valid())
        return false;
    const osg::Matrixd inv = viewer->getCamera()->getInverseViewMatrix();
    const osg::Vec3d eye = inv.getTrans();
    const osg::Vec3d look = osg::Vec3d(0.0, 0.0, -1.0) * inv - eye;
    const SpatialReference* geo = mapNode->getMapSRS()->getGeographicSRS();
    GeoPoint point;
    if (!point.fromWorld(geo, eye))
        return false;
    osg::Matrixd localToWorld;
    geo->createLocalToWorld(osg::Vec3d(point.x(), point.y(), point.z()), localToWorld);
    const osg::Vec3d local = osg::Matrixd::transform3x3(localToWorld, look);   // world → east/north/up
    pose->latDeg = point.y();
    pose->lonDeg = point.x();
    pose->altM = point.z();
    pose->headingDeg = normalizeHeading(osg::RadiansToDegrees(std::atan2(local.x(), local.y())));
    pose->pitchDeg = osg::RadiansToDegrees(std::atan2(local.z(), std::hypot(local.x(), local.y())));
    pose->rollDeg = 0.0;
    return true;
}

void GlobeView3D::Private::applyPose(const CameraPose& pose)
{
    const SpatialReference* geo = mapNode->getMapSRS()->getGeographicSRS();
    osg::Matrixd localToWorld;   // east/north/up frame at the eye (ECEF, double)
    geo->createLocalToWorld(osg::Vec3d(pose.lonDeg, pose.latDeg, pose.altM), localToWorld);
    // OSG cameras look down -Z with +Y up (row-vector order: roll, then camera → looking north,
    // then pitch about east, then heading about up).
    const osg::Matrixd cameraToWorld =
        osg::Matrixd::rotate(osg::DegreesToRadians(-pose.rollDeg), osg::Z_AXIS)
        * osg::Matrixd::rotate(osg::DegreesToRadians(90.0 + pose.pitchDeg), osg::X_AXIS)
        * osg::Matrixd::rotate(osg::DegreesToRadians(-pose.headingDeg), osg::Z_AXIS)
        * localToWorld;
    viewer->getCamera()->setViewMatrix(osg::Matrixd::inverse(cameraToWorld));
}

bool GlobeView3D::Private::groundAt(double latDeg, double lonDeg, double* altM) const
{
    return terrainHeight(latDeg, lonDeg, altM);
}

bool GlobeView3D::Private::terrainHeight(double latDeg, double lonDeg, double* altM) const
{
    // GUI thread only: Terrain::getHeight intersects the drawn tiles.
    if (heights.lookup(latDeg, lonDeg, altM))
        return true;
    Terrain* terrain = mapNode.valid() ? mapNode->getTerrain() : nullptr;
    double h = 0.0;
    if (!terrain || !terrain->getHeight(mapNode->getMapSRS()->getGeographicSRS(), lonDeg, latDeg, &h))
        return false;
    heights.store(latDeg, lonDeg, h);
    if (altM)
        *altM = h;
    return true;
}

namespace {
// Drops cached heights under every tile that loads or refines.
class TileInvalidator : public TerrainCallback
{
public:
    explicit TileInvalidator(HeightCache* cache) : m_cache(cache) {}
    void onTileUpdate(const TileKey& key, osg::Node*, TerrainCallbackContext&) override
    {
        const GeoExtent e = key.getExtent().transform(SpatialReference::get("wgs84"));
        if (e.isValid())
            m_cache->invalidate(e.south(), e.west(), e.north(), e.east());
    }

private:
    HeightCache* m_cache;   // lives in GlobeView3D::Private; callback removed in ~GlobeView3D
};
} // namespace

void GlobeView3D::Private::installTileCallback()
{
    Terrain* terrain = mapNode.valid() ? mapNode->getTerrain() : nullptr;
    if (tileCallback.valid() || !terrain)   // the terrain exists only after the first frame
        return;
    tileCallback = new TileInvalidator(&heights);
    terrain->addTerrainCallback(tileCallback.get());
    tileCallbackTerrain = terrain;
}

void GlobeView3D::Private::resolveHeightRequests()
{
    // Points asked for from other threads; resolved here, then available from the cache.
    for (const auto& [lat, lon] : heights.takeRequests(256))
        terrainHeight(lat, lon, nullptr);
}

bool GlobeView3D::Private::targetPose(double dt, CameraPose* pose)
{
    if (mode == CameraMode::FreeFly) {
        // Movement in the look direction; speed from height above ground (AGL).
        const double agl = eyeGround.hasGround() ? flyPose.altM - eyeGround.groundAltM() : flyPose.altM;
        double speed = freeFlySpeed(agl) * flySpeedScale;
        if (keysDown.count(Qt::Key_Shift))
            speed *= 4.0;
        double forward = 0.0;
        double right = 0.0;
        double up = 0.0;
        if (keysDown.count(Qt::Key_W)) forward += 1.0;
        if (keysDown.count(Qt::Key_S)) forward -= 1.0;
        if (keysDown.count(Qt::Key_D)) right += 1.0;
        if (keysDown.count(Qt::Key_A)) right -= 1.0;
        if (keysDown.count(Qt::Key_E)) up += 1.0;
        if (keysDown.count(Qt::Key_Q)) up -= 1.0;
        const double step = speed * dt;
        const double pitch = osg::DegreesToRadians(flyPose.pitchDeg);
        if (forward != 0.0)
            offsetLatLon(flyPose.latDeg, flyPose.lonDeg, flyPose.headingDeg, forward * step * std::cos(pitch),
                         &flyPose.latDeg, &flyPose.lonDeg);
        if (right != 0.0)
            offsetLatLon(flyPose.latDeg, flyPose.lonDeg, flyPose.headingDeg + 90.0, right * step,
                         &flyPose.latDeg, &flyPose.lonDeg);
        flyPose.altM += forward * step * std::sin(pitch) + up * step;
        *pose = flyPose;
        return true;
    }

    auto it = entities.find(selectedId);
    if (it == entities.end())
        return false;
    const EntityVisual& target = it->second;
    const GeoPoint& at = target.geo->getPosition();
    const double lat = at.y();
    const double lon = at.x();
    const double alt = at.z();

    if (mode == CameraMode::FirstPerson) {
        pose->latDeg = lat;
        pose->lonDeg = lon;
        pose->altM = alt + (target.style.clampToTerrain ? kGroundEyeHeightM : 0.0);
        pose->headingDeg = normalizeHeading(target.headingDeg + lookYawDeg);
        pose->pitchDeg = std::clamp(target.pitchDeg + lookPitchDeg, -89.0, 89.0);
        pose->rollDeg = target.rollDeg;
        return true;
    }

    // Chase / stealth: follow the target heading through a low-pass filter so turns are smooth.
    if (!haveSmoothHeading) {
        smoothHeadingDeg = target.headingDeg;
        haveSmoothHeading = true;
    }
    else {
        double diff = normalizeHeading(target.headingDeg - smoothHeadingDeg);
        if (diff > 180.0)
            diff -= 360.0;
        const double k = 1.0 - std::exp(-dt / kHeadingFilterSeconds);
        smoothHeadingDeg = normalizeHeading(smoothHeadingDeg + diff * k);
    }
    const double centreAlt = alt + kTargetCentreM;

    if (mode == CameraMode::Chase) {
        const double base = chaseFreeLook ? freeLookBaseDeg : smoothHeadingDeg;
        const double azimuth = base + lookYawDeg;
        const double elevation = std::clamp(chaseElevationDeg + lookPitchDeg, 2.0, 85.0);
        const double horizontal = chaseRangeM * std::cos(osg::DegreesToRadians(elevation));
        offsetLatLon(lat, lon, azimuth + 180.0, horizontal, &pose->latDeg, &pose->lonDeg);
        pose->altM = centreAlt + chaseRangeM * std::sin(osg::DegreesToRadians(elevation));
    }
    else {   // stealth: eye height above the terrain under the eye
        offsetLatLon(lat, lon, smoothHeadingDeg + lookYawDeg + 180.0, stealthBackM, &pose->latDeg, &pose->lonDeg);
        double ground = 0.0;
        const bool ok = groundAt(pose->latDeg, pose->lonDeg, &ground);
        pose->altM = (ok ? ground : alt) + stealthEyeHeightM;
    }
    lookAtAngles(*pose, lat, lon, centreAlt, &pose->headingDeg, &pose->pitchDeg);
    if (mode == CameraMode::Stealth)
        pose->pitchDeg = std::clamp(pose->pitchDeg + lookPitchDeg, -89.0, 89.0);
    pose->rollDeg = 0.0;
    return true;
}

void GlobeView3D::Private::hideOwnEntity(std::uint64_t id)
{
    if (id == hiddenId)
        return;
    if (auto old = entities.find(hiddenId); old != entities.end()) {
        old->second.attitude->setNodeMask(~0u);
        old->second.labelGeode->setNodeMask(labelsVisible ? ~0u : 0u);
        applySelectionLook(old->second, hiddenId == selectedId);
    }
    if (auto now = entities.find(id); now != entities.end()) {
        now->second.attitude->setNodeMask(0u);
        now->second.labelGeode->setNodeMask(0u);
        now->second.highlight->setNodeMask(0u);
    }
    hiddenId = id;
}

void GlobeView3D::Private::updateCamera(double dt)
{
    // First person looks out of the target: its own marker/model would block the view.
    hideOwnEntity(mode == CameraMode::FirstPerson ? selectedId : 0);
    if (mode == CameraMode::Orbit)
        return;
    CameraPose pose;
    if (!targetPose(dt, &pose)) {   // target removed: back to orbit where we are
        enterMode(CameraMode::Orbit);
        return;
    }
    if (blending) {
        const double t = double(blendClock.nsecsElapsed()) / 1.0e9 / kBlendSeconds;
        pose = blendPose(blendFrom, pose, t);
        if (t >= 1.0)
            blending = false;
    }
    // Never below terrain + 2 m: query under the final eye, keep the last good height.
    double ground = 0.0;
    eyeGround.update(groundAt(pose.latDeg, pose.lonDeg, &ground), ground);
    pose.altM = eyeGround.clamp(pose.altM);
    if (mode == CameraMode::FreeFly)
        flyPose.altM = pose.altM;
    applyPose(pose);
}

void GlobeView3D::Private::updateHud(double dt)
{
    if (!hud)
        return;
    CameraPose pose;
    if (!eyePose(&pose))
        return;
    const osg::Vec3d eye = viewer->getCamera()->getInverseViewMatrix().getTrans();
    if (haveLastEye && dt > 0.0) {
        const double speed = (eye - lastEyeWorld).length() / dt;
        eyeSpeedMps = 0.8 * eyeSpeedMps + 0.2 * speed;
    }
    lastEyeWorld = eye;
    haveLastEye = true;
    if (!hudVisible || (hudClock.isValid() && hudClock.elapsed() < 100))
        return;
    hudClock.restart();

    HudData data;
    data.mode = mode;
    data.chaseFreeLook = chaseFreeLook;
    data.headingDeg = pose.headingDeg;
    data.pitchDeg = pose.pitchDeg;
    data.altM = pose.altM;
    double ground = 0.0;
    if (mode != CameraMode::Orbit && eyeGround.hasGround()) {
        data.haveAgl = true;
        data.aglM = pose.altM - eyeGround.groundAltM();
    }
    else if (groundAt(pose.latDeg, pose.lonDeg, &ground)) {
        data.haveAgl = true;
        data.aglM = pose.altM - ground;
    }
    if (egm96) {
        data.haveMsl = true;
        data.mslM = egm96->hae2msl(pose.latDeg, pose.lonDeg, pose.altM);
    }
    data.speedMps = eyeSpeedMps;
    if (needsTarget(mode))
        data.target = q->selectedLabel();
    if (groundOffsetOverlay) {
        data.showGroundOffset = true;
        data.groundOffsets = groundOffsets;
        if (auto it = entities.find(selectedId); it != entities.end() && it->second.haveGroundOffset) {
            data.haveTargetOffset = true;
            data.targetOffsetM = it->second.groundOffsetM;
        }
    }
    hud->setText(formatHud(data));
    hud->adjustSize();
}

bool GlobeView3D::setLayerVisible(const QString& name, bool visible)
{
    if (!d->mapNode.valid())
        return false;
    auto* layer = dynamic_cast<VisibleLayer*>(d->mapNode->getMap()->getLayerByName(name.toStdString()));
    if (!layer)
        return false;
    layer->setVisible(visible);
    return true;
}

bool GlobeView3D::layerVisible(const QString& name) const
{
    if (!d->mapNode.valid())
        return false;
    auto* layer = dynamic_cast<VisibleLayer*>(d->mapNode->getMap()->getLayerByName(name.toStdString()));
    return layer && layer->getVisible();
}

QStringList GlobeView3D::layerNames() const
{
    QStringList names;
    if (!d->mapNode.valid())
        return names;
    LayerVector layers;
    d->mapNode->getMap()->getLayers(layers);
    for (const auto& layer : layers)
        names << QString::fromStdString(layer->getName());
    return names;
}

bool GlobeView3D::heightAt(double latDeg, double lonDeg, double* altM) const
{
    if (QThread::currentThread() == thread())
        return d->terrainHeight(latDeg, lonDeg, altM);
    // Other threads must not touch the scene graph: answer from the cache, queue a miss.
    if (d->heights.lookup(latDeg, lonDeg, altM))
        return true;
    d->heights.request(latDeg, lonDeg);
    return false;
}

bool GlobeView3D::sourceHeightAt(double latDeg, double lonDeg, double* altM, double* resolutionM) const
{
    if (!d->mapNode.valid())
        return false;
    const SpatialReference* geo = d->mapNode->getMapSRS()->getGeographicSRS();
    const ElevationSample sample = d->mapNode->getMap()->getElevationPool()->getSample(
        GeoPoint(geo, lonDeg, latDeg, 0.0, ALTMODE_ABSOLUTE), nullptr);
    const double h = sample.elevation().as(Units::METERS);
    if (!(h > -1.0e5))   // NO_DATA_VALUE
        return false;
    if (altM)
        *altM = h;
    if (resolutionM)
        *resolutionM = sample.resolution().as(Units::METERS);
    return true;
}

bool GlobeView3D::haveGeoid() const
{
    return d->egm96 != nullptr;
}

double GlobeView3D::geoidHeight(double latDeg, double lonDeg) const
{
    return d->egm96 ? d->egm96->msl2hae(latDeg, lonDeg, 0.0) : 0.0;
}

void GlobeView3D::setGroundOffsetOverlay(bool show)
{
    d->groundOffsetOverlay = show;
}

bool GlobeView3D::groundOffsetOverlay() const
{
    return d->groundOffsetOverlay;
}

GroundOffsetStats GlobeView3D::groundOffsetStats() const
{
    return d->groundOffsets;
}

void GlobeView3D::setCameraMode(CameraMode mode)
{
    d->enterMode(mode);
}

CameraMode GlobeView3D::cameraMode() const
{
    return d->mode;
}

void GlobeView3D::setStealthEyeHeight(double metres)
{
    d->stealthEyeHeightM = std::clamp(metres, kMinTerrainClearanceM, 1000.0);
}

double GlobeView3D::stealthEyeHeight() const
{
    return d->stealthEyeHeightM;
}

void GlobeView3D::setChaseFreeLook(bool freeLook)
{
    if (freeLook && !d->chaseFreeLook)
        d->freeLookBaseDeg = d->smoothHeadingDeg + d->lookYawDeg;
    d->chaseFreeLook = freeLook;
    d->lookYawDeg = 0.0;
}

bool GlobeView3D::chaseFreeLook() const
{
    return d->chaseFreeLook;
}

void GlobeView3D::setHudVisible(bool visible)
{
    d->hudVisible = visible;
    if (d->hud)
        d->hud->setVisible(visible);
}

bool GlobeView3D::hudVisible() const
{
    return d->hudVisible;
}

void GlobeView3D::chaseSelected() { setCameraMode(CameraMode::Chase); }
void GlobeView3D::stealthSelected() { setCameraMode(CameraMode::Stealth); }
void GlobeView3D::firstPersonSelected() { setCameraMode(CameraMode::FirstPerson); }
void GlobeView3D::freeFly() { setCameraMode(CameraMode::FreeFly); }

void GlobeView3D::untether()
{
    d->modePending = false;
    setCameraMode(CameraMode::Orbit);
}

QString GlobeView3D::viewpointDescription() const
{
    if (!d->manip.valid())
        return QString();
    if (d->mode == CameraMode::Orbit)
        return QStringLiteral("orbit: %1, frame %2 ms").arg(QString::fromStdString(d->manip->getViewpoint().toString()))
            .arg(d->frameMsAvg, 0, 'f', 1);
    CameraPose pose;
    d->eyePose(&pose);
    QString text = QStringLiteral("%1: lat %2, lon %3, alt %4 m").arg(cameraModeName(d->mode))
        .arg(pose.latDeg, 0, 'f', 6).arg(pose.lonDeg, 0, 'f', 6).arg(pose.altM, 0, 'f', 1);
    if (d->eyeGround.hasGround())
        text += QStringLiteral(" (AGL %1 m)").arg(pose.altM - d->eyeGround.groundAltM(), 0, 'f', 1);
    text += QStringLiteral(", hdg %1, pitch %2").arg(pose.headingDeg, 0, 'f', 1).arg(pose.pitchDeg, 0, 'f', 1);
    double lat = 0.0;
    double lon = 0.0;
    double alt = 0.0;
    if (needsTarget(d->mode) && entityPosition(d->selectedId, &lat, &lon, &alt)) {
        const osg::Vec3d eye = d->viewer->getCamera()->getInverseViewMatrix().getTrans();
        osg::Vec3d target;
        GeoPoint(d->mapNode->getMapSRS()->getGeographicSRS(), lon, lat, alt, ALTMODE_ABSOLUTE).toWorld(target);
        text += QStringLiteral(", target %1 range %2 m").arg(d->selectedId).arg((target - eye).length(), 0, 'f', 0);
    }
    return text + QStringLiteral(", frame %1 ms").arg(d->frameMsAvg, 0, 'f', 1);
}

bool GlobeView3D::saveSnapshot(const QString& pngPath)
{
    return grab().save(pngPath);   // includes the HUD overlay
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

    const double dt = d->stepClock.isValid() ? std::min(double(d->stepClock.nsecsElapsed()) / 1.0e9, 0.25) : 0.0;
    d->stepClock.restart();
    if (d->mapNode.valid()) {
        d->installTileCallback();
        d->resolveHeightRequests();
        d->updateCamera(dt);
    }
    d->viewer->frame();

    // Before the first frame the manipulator is not attached to the MapNode yet.
    if (!d->homeApplied && d->mapNode.valid()) {
        d->homeApplied = true;
        d->manip->setViewpoint(d->homeViewpoint(), 0.0);
    }
    else if (d->modePending && (d->pendingMode == CameraMode::FreeFly || !d->entities.empty())) {
        d->enterMode(d->pendingMode);
    }

    // Diagnostics: smoothed frame time (viewpointDescription) and the HUD.
    if (d->frameClock.isValid()) {
        const double ms = double(d->frameClock.nsecsElapsed()) / 1.0e6;
        d->frameMsAvg = d->frameMsAvg <= 0.0 ? ms : 0.9 * d->frameMsAvg + 0.1 * ms;
    }
    d->frameClock.restart();
    if (d->mapNode.valid())
        d->updateHud(dt);
}

void GlobeView3D::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        d->pressPos = e->position();
        d->lastMousePos = e->position();
        d->leftPressed = true;
        d->looking = false;
    }
    if (d->mode == CameraMode::Orbit)
        d->eventQueue()->mouseButtonPress(d->scaledX(e->position().x()), d->scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::mouseReleaseEvent(QMouseEvent* e)
{
    // A left click (not a drag) picks the entity nearest the cursor.
    if (e->button() == Qt::LeftButton && d->leftPressed && d->mapNode.valid()) {
        d->leftPressed = false;
        const QPointF delta = e->position() - d->pressPos;
        if (!d->looking && std::hypot(delta.x(), delta.y()) <= kClickSlopPixels) {
            if (const std::uint64_t id = d->pickAt(e->position().x(), e->position().y())) {
                setSelected(id);
                emit entityPicked(id);
            }
        }
        d->looking = false;
    }
    if (d->mode == CameraMode::Orbit)
        d->eventQueue()->mouseButtonRelease(d->scaledX(e->position().x()), d->scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::mouseMoveEvent(QMouseEvent* e)
{
    if (d->mode == CameraMode::Orbit) {
        d->eventQueue()->mouseMotion(d->scaledX(e->position().x()), d->scaledY(e->position().y()));
        return;
    }
    // Left-drag looks around (chase free-look orbits the target; free-fly turns the eye).
    if (!d->leftPressed)
        return;
    const QPointF fromPress = e->position() - d->pressPos;
    if (!d->looking && std::hypot(fromPress.x(), fromPress.y()) > kClickSlopPixels)
        d->looking = true;
    const QPointF delta = e->position() - d->lastMousePos;
    d->lastMousePos = e->position();
    if (!d->looking)
        return;
    const double yaw = delta.x() * kLookDegPerPixel;
    const double pitch = -delta.y() * kLookDegPerPixel;
    if (d->mode == CameraMode::FreeFly) {
        d->flyPose.headingDeg = normalizeHeading(d->flyPose.headingDeg + yaw);
        d->flyPose.pitchDeg = std::clamp(d->flyPose.pitchDeg + pitch, -89.0, 89.0);
    }
    else if (d->mode != CameraMode::Chase || d->chaseFreeLook) {
        d->lookYawDeg += yaw;
        d->lookPitchDeg = std::clamp(d->lookPitchDeg + pitch, -80.0, 80.0);
    }
}

void GlobeView3D::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (d->mode == CameraMode::Orbit)
        d->eventQueue()->mouseDoubleButtonPress(d->scaledX(e->position().x()), d->scaledY(e->position().y()), toOsgButton(e->button()));
}

void GlobeView3D::wheelEvent(QWheelEvent* e)
{
    const bool in = e->angleDelta().y() > 0;
    switch (d->mode) {
    case CameraMode::Orbit:
        d->eventQueue()->mouseScroll(in ? osgGA::GUIEventAdapter::SCROLL_UP : osgGA::GUIEventAdapter::SCROLL_DOWN);
        return;
    case CameraMode::Chase:
        d->chaseRangeM = std::clamp(d->chaseRangeM * (in ? 0.85 : 1.15), 20.0, 5000.0);
        return;
    case CameraMode::Stealth:
        setStealthEyeHeight(d->stealthEyeHeightM * (in ? 0.85 : 1.15));
        return;
    case CameraMode::FreeFly:
        d->flySpeedScale = std::clamp(d->flySpeedScale * (in ? 1.25 : 0.8), 0.05, 20.0);
        return;
    case CameraMode::FirstPerson:
        return;
    }
}

void GlobeView3D::keyPressEvent(QKeyEvent* e)
{
    if (d->mode == CameraMode::FreeFly) {
        switch (e->key()) {
        case Qt::Key_W: case Qt::Key_A: case Qt::Key_S: case Qt::Key_D:
        case Qt::Key_Q: case Qt::Key_E: case Qt::Key_Shift:
            d->keysDown.insert(e->key());
            return;
        default: break;
        }
    }
    if (e->isAutoRepeat())
        return;
    switch (e->key()) {
    case Qt::Key_N: selectNextEntity(); return;
    case Qt::Key_O: untether(); return;
    case Qt::Key_T: chaseSelected(); return;
    case Qt::Key_F: stealthSelected(); return;
    case Qt::Key_P: firstPersonSelected(); return;
    case Qt::Key_G: freeFly(); return;
    case Qt::Key_L: setChaseFreeLook(!d->chaseFreeLook); return;
    case Qt::Key_J: setHudVisible(!d->hudVisible); return;
    case Qt::Key_K: setGroundOffsetOverlay(!d->groundOffsetOverlay); return;
    case Qt::Key_R: setLayerVisible(QStringLiteral("Graticule"), !layerVisible(QStringLiteral("Graticule"))); return;
    case Qt::Key_U: untether(); return;
    case Qt::Key_H: goHome(); return;
    default: break;
    }
    if (d->mode == CameraMode::Orbit && !e->text().isEmpty())
        d->eventQueue()->keyPress(e->text().at(0).toLatin1());
}

void GlobeView3D::keyReleaseEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat())
        return;
    d->keysDown.erase(e->key());
    if (d->mode == CameraMode::Orbit && !e->text().isEmpty())
        d->eventQueue()->keyRelease(e->text().at(0).toLatin1());
}

void GlobeView3D::focusOutEvent(QFocusEvent* e)
{
    d->keysDown.clear();   // no stuck free-fly keys after switching windows
    QOpenGLWidget::focusOutEvent(e);
}

} // namespace earthview3d
