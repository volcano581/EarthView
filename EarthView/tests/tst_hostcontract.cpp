// Host contract test (roadmap E8).
//
// DOCTRINE_WIRING.md §2 is a contract between this repository and an adapter that lives in
// Doctrine, off GitHub: the host fills EntityState3D from its own snapshot and drives
// GlobeView3D through its public API. Nothing in CI compiles that adapter, so the contract
// could drift — a renamed field, a changed unit, a signature with an extra argument — and
// only be found when someone next opens the Conquer tree.
//
// This file stands in for the adapter. It carries a struct shaped like Doctrine's
// RenderEntity as the wiring document describes it, converts it exactly as the mapping table
// says, and then pins the public API with static assertions. A rename breaks the build; a
// changed type or signature fails the assertion. The GlobeView3D parts are compile-time only
// (decltype never odr-uses anything), so this runs on Linux too, where the osgEarth widget is
// not built — the public headers are osgEarth-free by D-011, which is what makes that work.

#include <earthview3d/CameraControl.h>
#include <earthview3d/DtedMosaic.h>
#include <earthview3d/EarthFileCatalog.h>
#include <earthview3d/EntityDiff.h>
#include <earthview3d/EntityState3D.h>
#include <earthview3d/GlobeView3D.h>
#include <earthview3d/MapConfig.h>
#include <earthview3d/ModelTable.h>
#include <earthview3d/Picking.h>
#include <earthview3d/RuntimePaths.h>
#include <earthview3d/ScreenScale.h>
#include <earthview3d/TerrainServices.h>

#include <QtTest>

#include <cmath>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

using namespace earthview3d;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;

/// Doctrine's RenderEntity, as DOCTRINE_WIRING.md §2 records it (Conquer, 2026-10-08).
/// Deliberately *not* EntityState3D: different names, radians, MSL, a char array label. The
/// adapter's whole job is this gap.
struct DoctrineRenderEntity
{
    std::uint64_t id = 0;        ///< flecs entity id
    double lat_rad = 0.0;
    double lon_rad = 0.0;
    double alt_m = 0.0;          ///< the sim stores mean sea level
    double heading_rad = 0.0;
    int unitType = 0;            ///< 0 infantry, 1 air, 2 armour, 3 artillery
    int side = 0;                ///< 0 unknown, 1 own, 2 enemy, 3 neutral
    std::uint32_t symbolColor = 0xFFFFFFFFu;
    char label[8] = {};
    bool visible = true;
};

/// The adapter of DOCTRINE_WIRING.md §2, in full. @p geoidHeight is the view's own
/// geoidHeight(lat, lon): the sim's altitude is orthometric and the 3D view is ellipsoidal
/// (D-021), so MSL + N = HAE.
EntityState3D toEntityState(const DoctrineRenderEntity& source,
                            const std::function<double(double, double)>& geoidHeight)
{
    EntityState3D state;
    state.id = source.id;
    state.latDeg = source.lat_rad * kRadToDeg;
    state.lonDeg = source.lon_rad * kRadToDeg;
    state.altM = source.alt_m + geoidHeight(state.latDeg, state.lonDeg);
    state.headingDeg = source.heading_rad * kRadToDeg;
    state.pitchDeg = 0.0;        // not in the snapshot yet
    state.rollDeg = 0.0;
    state.shape = source.unitType;
    state.rgba = source.symbolColor;
    switch (source.side) {
    case 1: state.force = Force::Friendly; break;
    case 2: state.force = Force::Hostile; break;
    case 3: state.force = Force::Neutral; break;
    default: state.force = Force::Unknown; break;
    }
    state.label = QString::fromLatin1(source.label, int(qstrnlen(source.label, sizeof(source.label))));
    state.clampToTerrain = source.unitType != 1;   // everything but air (I-015)
    state.visible = source.visible;
    return state;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// The contract's data: every field the mapping table names, with its type.
// A renamed field stops this compiling; a changed type fails the assertion.
// ─────────────────────────────────────────────────────────────────────────────
static_assert(std::is_same_v<decltype(EntityState3D::id), std::uint64_t>);
static_assert(std::is_same_v<decltype(EntityState3D::latDeg), double>);
static_assert(std::is_same_v<decltype(EntityState3D::lonDeg), double>);
static_assert(std::is_same_v<decltype(EntityState3D::altM), double>);
static_assert(std::is_same_v<decltype(EntityState3D::headingDeg), double>);
static_assert(std::is_same_v<decltype(EntityState3D::pitchDeg), double>);
static_assert(std::is_same_v<decltype(EntityState3D::rollDeg), double>);
static_assert(std::is_same_v<decltype(EntityState3D::kindKey), QString>);
static_assert(std::is_same_v<decltype(EntityState3D::shape), int>);
static_assert(std::is_same_v<decltype(EntityState3D::rgba), std::uint32_t>);
static_assert(std::is_same_v<decltype(EntityState3D::force), Force>);
static_assert(std::is_same_v<decltype(EntityState3D::label), QString>);
static_assert(std::is_same_v<decltype(EntityState3D::clampToTerrain), bool>);
static_assert(std::is_same_v<decltype(EntityState3D::visible), bool>);
// A host builds these in its own containers and copies them per frame.
static_assert(std::is_default_constructible_v<EntityState3D>);
static_assert(std::is_copy_constructible_v<EntityState3D>);
static_assert(std::is_same_v<std::underlying_type_t<Force>, std::uint8_t>);
static_assert(std::is_same_v<decltype(&entityColor), std::uint32_t (*)(const EntityState3D&)>);

// ─────────────────────────────────────────────────────────────────────────────
// The contract's API. decltype only: nothing here is odr-used, so the test needs no osgEarth
// and does not link the widget.
// ─────────────────────────────────────────────────────────────────────────────
static_assert(std::is_base_of_v<QWidget, GlobeView3D>);
static_assert(std::is_constructible_v<GlobeView3D, const MapConfig&, double, double, QWidget*>);

// The entity feed, driven from the pre-frame callback.
static_assert(std::is_same_v<decltype(&GlobeView3D::setPreFrameCallback),
                             void (GlobeView3D::*)(std::function<void()>)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::setEntities),
                             void (GlobeView3D::*)(const std::vector<EntityState3D>&)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::entityCount), int (GlobeView3D::*)() const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::mapConfig), const MapConfig& (GlobeView3D::*)() const>);

// Selection and picking (E3).
static_assert(std::is_same_v<decltype(&GlobeView3D::selectedId), std::uint64_t (GlobeView3D::*)() const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::setSelected), void (GlobeView3D::*)(quint64)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::selectedLabel), QString (GlobeView3D::*)() const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::setLabelsVisible), void (GlobeView3D::*)(bool)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::entityPosition),
                             bool (GlobeView3D::*)(std::uint64_t, double*, double*, double*) const>);

// Cameras (E4).
static_assert(std::is_same_v<decltype(&GlobeView3D::setCameraMode), void (GlobeView3D::*)(CameraMode)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::cameraMode), CameraMode (GlobeView3D::*)() const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::chaseSelected), void (GlobeView3D::*)()>);
static_assert(std::is_same_v<decltype(&GlobeView3D::stealthSelected), void (GlobeView3D::*)()>);
static_assert(std::is_same_v<decltype(&GlobeView3D::firstPersonSelected), void (GlobeView3D::*)()>);
static_assert(std::is_same_v<decltype(&GlobeView3D::freeFly), void (GlobeView3D::*)()>);
static_assert(std::is_same_v<decltype(&GlobeView3D::untether), void (GlobeView3D::*)()>);
static_assert(std::is_same_v<decltype(&GlobeView3D::goHome), void (GlobeView3D::*)()>);
static_assert(std::is_same_v<decltype(&GlobeView3D::selectNextEntity), void (GlobeView3D::*)()>);
static_assert(std::is_same_v<decltype(&GlobeView3D::setStealthEyeHeight), void (GlobeView3D::*)(double)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::setChaseFreeLook), void (GlobeView3D::*)(bool)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::setHudVisible), void (GlobeView3D::*)(bool)>);

// Terrain services (E5).
static_assert(std::is_same_v<decltype(&GlobeView3D::heightAt),
                             bool (GlobeView3D::*)(double, double, double*) const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::sourceHeightAt),
                             bool (GlobeView3D::*)(double, double, double*, double*) const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::geoidHeight), double (GlobeView3D::*)(double, double) const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::groundOffsetStats),
                             GroundOffsetStats (GlobeView3D::*)() const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::setGroundOffsetOverlay), void (GlobeView3D::*)(bool)>);

// Earth-file layers (E6) and diagnostics.
static_assert(std::is_same_v<decltype(&GlobeView3D::setLayerVisible),
                             bool (GlobeView3D::*)(const QString&, bool)>);
static_assert(std::is_same_v<decltype(&GlobeView3D::layerVisible), bool (GlobeView3D::*)(const QString&) const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::layerNames), QStringList (GlobeView3D::*)() const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::viewpointDescription), QString (GlobeView3D::*)() const>);
static_assert(std::is_same_v<decltype(&GlobeView3D::saveSnapshot), bool (GlobeView3D::*)(const QString&)>);

// Map configuration (E1/E6) and the catalog the host builds its map list from (E11, F3).
static_assert(std::is_same_v<decltype(MapConfig::earthFile), QString>);
static_assert(std::is_same_v<decltype(MapConfig::dataRoot), QString>);
static_assert(std::is_same_v<decltype(MapConfig::cacheDir), QString>);
static_assert(std::is_same_v<decltype(MapConfig::modelTable), QString>);
static_assert(std::is_same_v<decltype(MapConfig::sky), bool>);
static_assert(std::is_same_v<decltype(&MapConfig::validate), bool (MapConfig::*)(QString*) const>);
static_assert(std::is_same_v<decltype(&EarthFileCatalog::build),
                             QList<MapCatalogEntry> (*)(const QStringList&, const QString&, const QString&,
                                                        QStringList*)>);
static_assert(std::is_same_v<decltype(&EarthFileCatalog::defaultSearchDirectories),
                             QStringList (*)(const QString&)>);
static_assert(std::is_same_v<decltype(&DtedMosaic::ensureMosaic),
                             QString (*)(const QString&, const QString&, QString*, QStringList*)>);
static_assert(std::is_same_v<decltype(MapCatalogEntry::label), QString>);
static_assert(std::is_same_v<decltype(MapCatalogEntry::earthFile), QString>);

class TestHostContract : public QObject
{
    Q_OBJECT

private slots:
    void adapterFillsEveryDocumentedField();
    void altitudeCrossesFromMslToEllipsoid();
    void forceDrivesTheColourAndRgbaIsTheFallback();
    void snapshotRulesHoldForIdZeroAndDuplicates();
    void removedEntitiesAreReportedOnce();
    void cameraModesAreTheDocumentedFive();
};

void TestHostContract::adapterFillsEveryDocumentedField()
{
    DoctrineRenderEntity source;
    source.id = 4242;
    source.lat_rad = 33.6844 / kRadToDeg;
    source.lon_rad = 73.0479 / kRadToDeg;
    source.alt_m = 540.0;
    source.heading_rad = 90.0 / kRadToDeg;
    source.unitType = 2;        // armour
    source.side = 2;            // enemy
    source.symbolColor = 0x2E9E4FFFu;
    qstrncpy(source.label, "ARM-07", sizeof(source.label));
    source.visible = true;

    const EntityState3D state = toEntityState(source, [](double, double) { return 0.0; });
    QCOMPARE(state.id, std::uint64_t(4242));
    QVERIFY(qFuzzyCompare(state.latDeg, 33.6844));
    QVERIFY(qFuzzyCompare(state.lonDeg, 73.0479));
    QVERIFY(qFuzzyCompare(state.headingDeg, 90.0));
    QCOMPARE(state.shape, 2);
    QCOMPARE(state.force, Force::Hostile);
    QCOMPARE(state.label, QStringLiteral("ARM-07"));
    QVERIFY(state.clampToTerrain);          // ground unit
    QVERIFY(state.visible);
    QCOMPARE(state.pitchDeg, 0.0);          // not in the host snapshot yet
    QCOMPARE(state.rollDeg, 0.0);
    QVERIFY(state.kindKey.isEmpty());       // no SISO kind yet: the shape is the fallback

    source.unitType = 1;                    // air: altM is used as given
    QVERIFY(!toEntityState(source, [](double, double) { return 0.0; }).clampToTerrain);
}

void TestHostContract::altitudeCrossesFromMslToEllipsoid()
{
    // D-021: the 3D view is ellipsoidal, the sim is orthometric. Near Islamabad N ~ -47 m, so
    // 540 m MSL is ~493 m HAE. Getting this wrong is a ~47 m error, which is what the
    // adapter's one line of altitude arithmetic exists to prevent.
    DoctrineRenderEntity source;
    source.id = 1;
    source.alt_m = 540.0;
    const auto geoid = [](double, double) { return -47.0; };
    QVERIFY(qFuzzyCompare(toEntityState(source, geoid).altM, 493.0));
}

void TestHostContract::forceDrivesTheColourAndRgbaIsTheFallback()
{
    EntityState3D state;
    state.rgba = 0x123456FFu;
    state.force = Force::Unspecified;
    QCOMPARE(entityColor(state), 0x123456FFu);   // the host's own colour wins

    state.force = Force::Hostile;
    const std::uint32_t hostile = entityColor(state);
    state.force = Force::Friendly;
    const std::uint32_t friendly = entityColor(state);
    QVERIFY(hostile != 0x123456FFu);
    QVERIFY(friendly != hostile);
}

void TestHostContract::snapshotRulesHoldForIdZeroAndDuplicates()
{
    // "Entries with id == 0 are ignored; a repeated id in one snapshot keeps the first entry."
    EntityDiff diff;
    std::vector<EntityState3D> snapshot(4);
    snapshot[0].id = 0;                     // ignored
    snapshot[1].id = 7;
    snapshot[2].id = 7;                     // duplicate: the first one wins
    snapshot[3].id = 9;

    const EntityDiff::Changes first = diff.apply(snapshot);
    QCOMPARE(first.added, (std::vector<std::size_t>{1, 3}));
    QVERIFY(first.updated.empty());
    QVERIFY(first.removed.empty());

    const EntityDiff::Changes second = diff.apply(snapshot);
    QVERIFY(second.added.empty());
    QCOMPARE(second.updated, (std::vector<std::size_t>{1, 3}));
}

void TestHostContract::removedEntitiesAreReportedOnce()
{
    EntityDiff diff;
    std::vector<EntityState3D> snapshot(2);
    snapshot[0].id = 1;
    snapshot[1].id = 2;
    diff.apply(snapshot);

    snapshot.pop_back();
    const EntityDiff::Changes changes = diff.apply(snapshot);
    QCOMPARE(changes.removed, (std::vector<std::uint64_t>{2}));
    QVERIFY(diff.apply(snapshot).removed.empty());   // gone, not reported again
}

void TestHostContract::cameraModesAreTheDocumentedFive()
{
    // A host menu switches modes by name; dropping or reordering one silently changes what a
    // stored setting means, so the set and its order are part of the contract.
    QCOMPARE(int(CameraMode::Orbit), 0);
    QCOMPARE(int(CameraMode::Chase), 1);
    QCOMPARE(int(CameraMode::Stealth), 2);
    QCOMPARE(int(CameraMode::FirstPerson), 3);
    QCOMPARE(int(CameraMode::FreeFly), 4);
    QCOMPARE(cameraModeName(CameraMode::Orbit), QStringLiteral("orbit"));
    CameraMode parsed = CameraMode::Orbit;
    QVERIFY(parseCameraMode(QStringLiteral("stealth"), &parsed));
    QCOMPARE(parsed, CameraMode::Stealth);
    QVERIFY(!parseCameraMode(QStringLiteral("nonsense"), &parsed));
}

QTEST_GUILESS_MAIN(TestHostContract)   // compile-time API checks; nothing is rendered
#include "tst_hostcontract.moc"
