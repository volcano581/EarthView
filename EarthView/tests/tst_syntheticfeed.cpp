#include "SyntheticFeed.h"
#include "Geodesy.h"

#include <QtTest>

#include <cmath>
#include <set>

using earthview3d::EntityState3D;
using earthview3d::demo::FeedPublisher;
using earthview3d::demo::SnapshotBuffer;
using earthview3d::demo::SyntheticFeed;

namespace {
/// Horizontal distance in metres between two geodetic points (via ECEF, same height).
double distanceM(double lat1, double lon1, double lat2, double lon2)
{
    const glm::dvec3 a = scene3d::Geodesy::geodeticToEcef(lat1, lon1, 0.0);
    const glm::dvec3 b = scene3d::Geodesy::geodeticToEcef(lat2, lon2, 0.0);
    return glm::length(a - b);
}

/// Bearing (deg, clockwise from north) from point 1 to point 2 in the local ENU frame.
double bearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    const glm::dmat3 enu = scene3d::Geodesy::enuFrame(lat1, lon1);
    const glm::dvec3 d = scene3d::Geodesy::geodeticToEcef(lat2, lon2, 0.0)
        - scene3d::Geodesy::geodeticToEcef(lat1, lon1, 0.0);
    double b = std::atan2(glm::dot(d, enu[0]), glm::dot(d, enu[1])) * 180.0 / 3.14159265358979323846;
    return b < 0.0 ? b + 360.0 : b;
}

double angleDiffDeg(double a, double b)
{
    double d = std::fmod(std::abs(a - b), 360.0);
    return d > 180.0 ? 360.0 - d : d;
}
} // namespace

/**
 * @brief Unit tests for the demo host simulator (SyntheticFeed, SnapshotBuffer, FeedPublisher).
 */
class TestSyntheticFeed : public QObject
{
    Q_OBJECT

private slots:
    void countIdsAndKinds();
    void deterministic();
    void staysNearOrigin();
    void headingFollowsMotion();
    void groundClampedAirAloft();
    void zeroAndNegativeCount();
    void bufferCopiesOnlyNewTicks();
    void publisherRunsAndStops();
};

void TestSyntheticFeed::countIdsAndKinds()
{
    SyntheticFeed::Settings s;
    s.entityCount = 13;
    const auto entities = SyntheticFeed(s).stateAt(0.0);
    QCOMPARE(int(entities.size()), 13);
    std::set<std::uint64_t> ids;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        QVERIFY(entities[i].id != 0);
        ids.insert(entities[i].id);
        QCOMPARE(entities[i].shape, SyntheticFeed::shapeOf(SyntheticFeed::kindOf(int(i))));
        QVERIFY(!entities[i].label.isEmpty());
    }
    QCOMPARE(int(ids.size()), 13);
    QCOMPARE(entities[3].label, QStringLiteral("AIR-04"));
}

void TestSyntheticFeed::deterministic()
{
    SyntheticFeed::Settings s;
    s.entityCount = 8;
    const auto a = SyntheticFeed(s).stateAt(12.5);
    const auto b = SyntheticFeed(s).stateAt(12.5);
    for (std::size_t i = 0; i < a.size(); ++i) {
        QCOMPARE(a[i].latDeg, b[i].latDeg);
        QCOMPARE(a[i].lonDeg, b[i].lonDeg);
        QCOMPARE(a[i].headingDeg, b[i].headingDeg);
    }
    const auto later = SyntheticFeed(s).stateAt(60.0);
    QVERIFY(a[1].latDeg != later[1].latDeg || a[1].lonDeg != later[1].lonDeg);
}

void TestSyntheticFeed::staysNearOrigin()
{
    SyntheticFeed::Settings s;
    s.entityCount = 40;
    s.originLatDeg = -12.0;
    s.originLonDeg = 130.5;
    const SyntheticFeed feed(s);
    for (double t : {0.0, 100.0, 3600.0}) {
        for (const EntityState3D& e : feed.stateAt(t)) {
            const double d = distanceM(s.originLatDeg, s.originLonDeg, e.latDeg, e.lonDeg);
            QVERIFY2(d < 10000.0, qPrintable(QStringLiteral("%1 at %2 m").arg(e.label).arg(d)));
        }
    }
}

void TestSyntheticFeed::headingFollowsMotion()
{
    SyntheticFeed::Settings s;
    s.entityCount = 8;
    const SyntheticFeed feed(s);
    const auto a = feed.stateAt(100.0);
    const auto b = feed.stateAt(100.5);
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double moved = bearingDeg(a[i].latDeg, a[i].lonDeg, b[i].latDeg, b[i].lonDeg);
        QVERIFY2(angleDiffDeg(moved, a[i].headingDeg) < 3.0,
                 qPrintable(QStringLiteral("%1: heading %2, moved %3").arg(a[i].label).arg(a[i].headingDeg).arg(moved)));
        QVERIFY(a[i].headingDeg >= 0.0 && a[i].headingDeg < 360.0);
    }
    // Speed matches the scripted speed (air 120 m/s over 0.5 s = 60 m, along a 4 km circle).
    const double airMoved = distanceM(a[3].latDeg, a[3].lonDeg, b[3].latDeg, b[3].lonDeg);
    QVERIFY(std::abs(airMoved - 60.0) < 0.5);
}

void TestSyntheticFeed::groundClampedAirAloft()
{
    SyntheticFeed::Settings s;
    s.entityCount = 8;
    s.groundAltitudeM = 500.0;
    s.airAltitudeM = 2000.0;
    for (const EntityState3D& e : SyntheticFeed(s).stateAt(0.0)) {
        if (e.shape == SyntheticFeed::shapeOf(SyntheticFeed::Air)) {
            QVERIFY(!e.clampToTerrain);
            QCOMPARE(e.altM, 2000.0);
        }
        else {
            QVERIFY(e.clampToTerrain);
            QCOMPARE(e.altM, 500.0);
        }
        QVERIFY(e.visible);
    }
}

void TestSyntheticFeed::zeroAndNegativeCount()
{
    SyntheticFeed::Settings s;
    s.entityCount = 0;
    QVERIFY(SyntheticFeed(s).stateAt(1.0).empty());
    s.entityCount = -5;
    QVERIFY(SyntheticFeed(s).stateAt(1.0).empty());
}

void TestSyntheticFeed::bufferCopiesOnlyNewTicks()
{
    SnapshotBuffer buffer;
    std::uint64_t lastTick = 0;
    std::vector<EntityState3D> out;
    QVERIFY(!buffer.consumeIfNew(&lastTick, &out));

    EntityState3D e;
    e.id = 7;
    buffer.publish({e});
    QVERIFY(buffer.consumeIfNew(&lastTick, &out));
    QCOMPARE(lastTick, std::uint64_t(1));
    QCOMPARE(out.size(), std::size_t(1));
    QCOMPARE(out[0].id, std::uint64_t(7));

    out.clear();
    QVERIFY(!buffer.consumeIfNew(&lastTick, &out));   // no new tick: no copy
    QVERIFY(out.empty());

    buffer.publish({});
    buffer.publish({e, e});
    QVERIFY(buffer.consumeIfNew(&lastTick, &out));    // only the latest snapshot
    QCOMPARE(out.size(), std::size_t(2));
    QCOMPARE(lastTick, std::uint64_t(3));
}

void TestSyntheticFeed::publisherRunsAndStops()
{
    SyntheticFeed::Settings s;
    s.entityCount = 20;
    const SyntheticFeed feed(s);
    SnapshotBuffer buffer;
    FeedPublisher publisher(feed, &buffer, 200.0);
    publisher.start();
    QVERIFY(publisher.isRunning());

    // Consume concurrently, like the GUI thread does per frame.
    std::uint64_t lastTick = 0;
    std::vector<EntityState3D> out;
    int copies = 0;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 300) {
        if (buffer.consumeIfNew(&lastTick, &out)) {
            ++copies;
            QCOMPARE(int(out.size()), 20);
        }
        QThread::msleep(5);
    }
    publisher.stop();
    QVERIFY(!publisher.isRunning());
    QVERIFY(copies > 5);
    const std::uint64_t ticks = buffer.tick();
    QVERIFY(ticks >= std::uint64_t(copies));
    QThread::msleep(30);
    QCOMPARE(buffer.tick(), ticks);   // nothing published after stop()
}

QTEST_GUILESS_MAIN(TestSyntheticFeed)
#include "tst_syntheticfeed.moc"
