#include "earthview3d/EntityDiff.h"

#include <QtTest>

using earthview3d::EntityDiff;
using earthview3d::EntityState3D;

namespace {
EntityState3D entity(std::uint64_t id)
{
    EntityState3D e;
    e.id = id;
    return e;
}

std::vector<std::uint64_t> idsAt(const std::vector<EntityState3D>& snapshot, const std::vector<std::size_t>& indices)
{
    std::vector<std::uint64_t> ids;
    for (std::size_t i : indices)
        ids.push_back(snapshot[i].id);
    return ids;
}
} // namespace

/**
 * @brief Unit tests for EntityDiff (add/update/remove between host snapshots).
 */
class TestEntityDiff : public QObject
{
    Q_OBJECT

private slots:
    void firstSnapshotAddsAll();
    void addUpdateRemove();
    void emptySnapshotRemovesAll();
    void ignoresIdZero();
    void duplicateIdsFirstWins();
    void removedIsSortedAndReAddWorks();
    void invisibleEntitiesStayPresent();
};

void TestEntityDiff::firstSnapshotAddsAll()
{
    EntityDiff diff;
    const std::vector<EntityState3D> snap = {entity(3), entity(1), entity(2)};
    const EntityDiff::Changes c = diff.apply(snap);
    QCOMPARE(idsAt(snap, c.added), (std::vector<std::uint64_t>{3, 1, 2}));
    QVERIFY(c.updated.empty());
    QVERIFY(c.removed.empty());
    QCOMPARE(diff.size(), std::size_t(3));
    QVERIFY(diff.contains(2));
}

void TestEntityDiff::addUpdateRemove()
{
    EntityDiff diff;
    diff.apply({entity(1), entity(2), entity(3)});

    const std::vector<EntityState3D> snap = {entity(2), entity(4), entity(3)};
    const EntityDiff::Changes c = diff.apply(snap);
    QCOMPARE(idsAt(snap, c.added), (std::vector<std::uint64_t>{4}));
    QCOMPARE(idsAt(snap, c.updated), (std::vector<std::uint64_t>{2, 3}));
    QCOMPARE(c.removed, (std::vector<std::uint64_t>{1}));
    QVERIFY(!diff.contains(1));
    QVERIFY(diff.contains(4));
}

void TestEntityDiff::emptySnapshotRemovesAll()
{
    EntityDiff diff;
    diff.apply({entity(5), entity(6)});
    const EntityDiff::Changes c = diff.apply({});
    QVERIFY(c.added.empty());
    QVERIFY(c.updated.empty());
    QCOMPARE(c.removed, (std::vector<std::uint64_t>{5, 6}));
    QCOMPARE(diff.size(), std::size_t(0));
}

void TestEntityDiff::ignoresIdZero()
{
    EntityDiff diff;
    const std::vector<EntityState3D> snap = {entity(0), entity(7)};
    const EntityDiff::Changes c = diff.apply(snap);
    QCOMPARE(idsAt(snap, c.added), (std::vector<std::uint64_t>{7}));
    QVERIFY(!diff.contains(0));
}

void TestEntityDiff::duplicateIdsFirstWins()
{
    EntityDiff diff;
    std::vector<EntityState3D> snap = {entity(9), entity(9)};
    snap[0].label = QStringLiteral("first");
    snap[1].label = QStringLiteral("second");
    const EntityDiff::Changes c = diff.apply(snap);
    QCOMPARE(c.added.size(), std::size_t(1));
    QCOMPARE(snap[c.added.front()].label, QStringLiteral("first"));
    QCOMPARE(diff.size(), std::size_t(1));

    const EntityDiff::Changes again = diff.apply(snap);
    QVERIFY(again.added.empty());
    QCOMPARE(again.updated, (std::vector<std::size_t>{0}));
}

void TestEntityDiff::removedIsSortedAndReAddWorks()
{
    EntityDiff diff;
    diff.apply({entity(30), entity(10), entity(20)});
    const EntityDiff::Changes c = diff.apply({});
    QCOMPARE(c.removed, (std::vector<std::uint64_t>{10, 20, 30}));

    const std::vector<EntityState3D> back = {entity(20)};
    const EntityDiff::Changes readd = diff.apply(back);
    QCOMPARE(idsAt(back, readd.added), (std::vector<std::uint64_t>{20}));
}

void TestEntityDiff::invisibleEntitiesStayPresent()
{
    EntityDiff diff;
    diff.apply({entity(1)});
    std::vector<EntityState3D> snap = {entity(1)};
    snap[0].visible = false;
    const EntityDiff::Changes c = diff.apply(snap);
    QCOMPARE(c.updated, (std::vector<std::size_t>{0}));
    QVERIFY(c.removed.empty());
}

QTEST_APPLESS_MAIN(TestEntityDiff)
#include "tst_entitydiff.moc"
