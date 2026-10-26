#include "earthview3d/TerrainServices.h"

#include <QtTest>

#include <thread>

using namespace earthview3d;

class TestTerrainServices : public QObject
{
    Q_OBJECT

private slots:
    void missThenHit();
    void sameCellSharesHeight();
    void invalidateExtent();
    void invalidateAcrossAntimeridian();
    void boundedSize();
    void requestsDeduplicated();
    void concurrentAccess();
    void groundOffsetStats();
};

void TestTerrainServices::missThenHit()
{
    HeightCache cache;
    double h = 0.0;
    QVERIFY(!cache.lookup(33.6844, 73.0479, &h));
    cache.store(33.6844, 73.0479, 512.5);
    QVERIFY(cache.lookup(33.6844, 73.0479, &h));
    QCOMPARE(h, 512.5);
    QCOMPARE(cache.hits(), std::uint64_t(1));
    QCOMPARE(cache.misses(), std::uint64_t(1));
}

void TestTerrainServices::sameCellSharesHeight()
{
    HeightCache cache(1.0e-4);   // ~11 m cells
    cache.store(33.68441, 73.04791, 500.0);
    double h = 0.0;
    QVERIFY(cache.lookup(33.68444, 73.04794, &h));   // same cell
    QCOMPARE(h, 500.0);
    QVERIFY(!cache.lookup(33.68460, 73.04791, &h));  // next cell north
}

void TestTerrainServices::invalidateExtent()
{
    HeightCache cache;
    cache.store(33.0, 73.0, 1.0);
    cache.store(34.5, 73.0, 2.0);
    cache.invalidate(32.0, 72.0, 34.0, 74.0);
    double h = 0.0;
    QVERIFY(!cache.lookup(33.0, 73.0, &h));
    QVERIFY(cache.lookup(34.5, 73.0, &h));
    QCOMPARE(cache.size(), std::size_t(1));
}

void TestTerrainServices::invalidateAcrossAntimeridian()
{
    HeightCache cache;
    cache.store(10.0, 179.5, 1.0);
    cache.store(10.0, -179.5, 2.0);
    cache.store(10.0, 0.0, 3.0);
    cache.invalidate(9.0, 179.0, 11.0, -179.0);   // west > east: wraps
    double h = 0.0;
    QVERIFY(!cache.lookup(10.0, 179.5, &h));
    QVERIFY(!cache.lookup(10.0, -179.5, &h));
    QVERIFY(cache.lookup(10.0, 0.0, &h));
}

void TestTerrainServices::boundedSize()
{
    HeightCache cache(1.0e-5, 100);
    for (int i = 0; i < 250; ++i)
        cache.store(30.0 + i * 0.001, 70.0, i);
    QVERIFY(cache.size() <= 100);
    double h = 0.0;
    QVERIFY(cache.lookup(30.0 + 249 * 0.001, 70.0, &h));   // newest survives
    QCOMPARE(h, 249.0);
}

void TestTerrainServices::requestsDeduplicated()
{
    HeightCache cache(1.0e-4);
    cache.request(33.68441, 73.04791);
    cache.request(33.68444, 73.04794);   // same cell: merged
    cache.request(34.0, 73.0);
    QCOMPARE(cache.pendingCount(), std::size_t(2));
    auto first = cache.takeRequests(1);
    QCOMPARE(first.size(), std::size_t(1));
    QCOMPARE(first[0].first, 33.68441);
    cache.request(33.68441, 73.04791);   // taken, so it can be queued again
    QCOMPARE(cache.takeRequests(10).size(), std::size_t(2));
    QCOMPARE(cache.pendingCount(), std::size_t(0));
}

void TestTerrainServices::concurrentAccess()
{
    HeightCache cache;
    auto worker = [&cache](int base) {
        for (int i = 0; i < 2000; ++i) {
            const double lat = 30.0 + (base * 2000 + i) * 1.0e-4;
            double h = 0.0;
            if (!cache.lookup(lat, 70.0, &h))
                cache.request(lat, 70.0);
            cache.store(lat, 70.0, i);
        }
    };
    std::thread a(worker, 0);
    std::thread b(worker, 1);
    std::thread c([&cache] {
        for (int i = 0; i < 200; ++i) {
            cache.takeRequests(50);
            cache.invalidate(30.0, 69.0, 30.1, 71.0);
        }
    });
    a.join();
    b.join();
    c.join();
    QVERIFY(cache.size() <= 4000);
}

void TestTerrainServices::groundOffsetStats()
{
    GroundOffsetStats s;
    QCOMPARE(s.meanM(), 0.0);
    s.add(-20.0);
    s.add(-10.0);
    s.add(-30.0);
    QCOMPARE(s.count, 3);
    QCOMPARE(s.minM, -30.0);
    QCOMPARE(s.maxM, -10.0);
    QCOMPARE(s.meanM(), -20.0);
}

QTEST_GUILESS_MAIN(TestTerrainServices)
#include "tst_terrainservices.moc"
