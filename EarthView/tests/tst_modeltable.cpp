#include "earthview3d/ModelTable.h"

#include <QtTest>

using earthview3d::EntityState3D;
using earthview3d::Force;
using earthview3d::ModelEntry;
using earthview3d::ModelTable;
using earthview3d::resolveStyle;

namespace {
ModelTable table(const QByteArray& json, const QString& baseDir = QStringLiteral("/data/models"))
{
    ModelTable t;
    QString error;
    if (!ModelTable::fromJson(json, baseDir, &t, &error))
        qFatal("%s", qPrintable(error));
    return t;
}

const QByteArray kTable = R"({
  "iconBelowPixels": 16,
  "models": [
    { "kind": "*", "shape": 0 },
    { "kind": "ground/*", "scale": 2.0, "clampToTerrain": true },
    { "kind": "ground/armour", "file": "t72.osgb", "hpr": [90, 0, 0], "shape": 2 },
    { "kind": "1.1.225.1.1.*", "file": "/abs/tank.gltf" },
    { "kind": "1.1.225.1.1.3.?", "file": "variant.osgb" },
    { "kind": "air/*", "file": "jet.osgb", "scale": 0.5 }
  ] })";
} // namespace

/**
 * @brief Unit tests for ModelTable matching, entity colours and resolved styles (E3).
 */
class TestModelTable : public QObject
{
    Q_OBJECT

private slots:
    void parses();
    void mostSpecificWins_data();
    void mostSpecificWins();
    void noMatch();
    void tieGoesToEarlierEntry();
    void resolvesFiles();
    void parseErrors_data();
    void parseErrors();
    void forceColours();
    void resolveWithoutTable();
    void resolveWithTable();
    void visibilityAndLabelFallback();
};

void TestModelTable::parses()
{
    const ModelTable t = table(kTable);
    QCOMPARE(t.entries().size(), std::size_t(6));
    QCOMPARE(t.iconBelowPixels(), 16.0);
    const ModelEntry& armour = t.entries()[2];
    QCOMPARE(armour.headingOffsetDeg, 90.0);
    QCOMPARE(armour.shape, 2);
    QCOMPARE(t.entries()[1].scale, 2.0);
    QVERIFY(t.entries()[1].clampToTerrain);
    QCOMPARE(t.entries()[0].scale, 1.0);
    QCOMPARE(ModelTable::specificity(QStringLiteral("ground/*")), 7);
}

void TestModelTable::mostSpecificWins_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("pattern");
    QTest::newRow("exact beats glob") << "ground/armour" << "ground/armour";
    QTest::newRow("prefix glob") << "ground/infantry" << "ground/*";
    QTest::newRow("catch-all") << "sea/frigate" << "*";
    QTest::newRow("siso family") << "1.1.225.1.1.1.0" << "1.1.225.1.1.*";
    QTest::newRow("siso single-char wildcard") << "1.1.225.1.1.3.7" << "1.1.225.1.1.3.?";
    QTest::newRow("? needs exactly one char") << "1.1.225.1.1.3.10" << "1.1.225.1.1.*";
    QTest::newRow("dots are literal") << "1x1x225x1x1x3x0" << "*";
}

void TestModelTable::mostSpecificWins()
{
    QFETCH(QString, kind);
    QFETCH(QString, pattern);
    const ModelTable t = table(kTable);
    const ModelEntry* e = t.match(kind);
    QVERIFY(e);
    QCOMPARE(e->pattern, pattern);
}

void TestModelTable::noMatch()
{
    const ModelTable t = table(R"({"models":[{"kind":"air/*"}]})");
    QVERIFY(!t.match(QStringLiteral("ground/armour")));
    QVERIFY(!t.match(QString()));   // empty key never matches, even "*"
    QVERIFY(!table(kTable).match(QString()));
    QVERIFY(ModelTable().isEmpty());
}

void TestModelTable::tieGoesToEarlierEntry()
{
    const ModelTable t = table(R"({"models":[{"kind":"a?c","file":"first"},{"kind":"ab?","file":"second"}]})");
    QCOMPARE(t.match(QStringLiteral("abc"))->file, QStringLiteral("first"));
}

void TestModelTable::resolvesFiles()
{
    const ModelTable t = table(kTable, QStringLiteral("/data/models"));
    QCOMPARE(t.resolvedFile(*t.match(QStringLiteral("ground/armour"))), QStringLiteral("/data/models/t72.osgb"));
    QCOMPARE(t.resolvedFile(*t.match(QStringLiteral("1.1.225.1.1.1.0"))), QStringLiteral("/abs/tank.gltf"));
    QVERIFY(t.resolvedFile(*t.match(QStringLiteral("ground/infantry"))).isEmpty());
}

void TestModelTable::parseErrors_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("expected");
    QTest::newRow("not json") << QByteArray("{") << QStringLiteral("Invalid JSON");
    QTest::newRow("no models") << QByteArray("{}") << QStringLiteral("\"models\"");
    QTest::newRow("no kind") << QByteArray(R"({"models":[{"file":"x"}]})") << QStringLiteral("\"kind\" is required");
    QTest::newRow("bad hpr") << QByteArray(R"({"models":[{"kind":"a","hpr":[1,2]}]})") << QStringLiteral("hpr");
    QTest::newRow("bad scale") << QByteArray(R"({"models":[{"kind":"a","scale":0}]})") << QStringLiteral("scale");
    QTest::newRow("unknown key") << QByteArray(R"({"models":[{"kind":"a","fil":"x"}]})") << QStringLiteral("\"fil\"");
    QTest::newRow("bad icon size") << QByteArray(R"({"iconBelowPixels":-1,"models":[]})") << QStringLiteral("iconBelowPixels");
}

void TestModelTable::parseErrors()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, expected);
    ModelTable t;
    QString error;
    QVERIFY(!ModelTable::fromJson(json, QString(), &t, &error));
    QVERIFY2(error.contains(expected), qPrintable(error));
}

void TestModelTable::forceColours()
{
    EntityState3D e;
    e.rgba = 0x11223344u;
    QCOMPARE(earthview3d::entityColor(e), 0x11223344u);   // unspecified: host colour
    e.force = Force::Friendly;
    QCOMPARE(earthview3d::entityColor(e), 0x80E0FFFFu);
    e.force = Force::Hostile;
    QCOMPARE(earthview3d::entityColor(e), 0xFF8080FFu);
    e.force = Force::Neutral;
    QCOMPARE(earthview3d::entityColor(e), 0xAAFFAAFFu);
    e.force = Force::Unknown;
    QCOMPARE(earthview3d::entityColor(e), 0xFFFF80FFu);
}

void TestModelTable::resolveWithoutTable()
{
    EntityState3D e;
    e.id = 5;
    e.shape = 3;
    e.kindKey = QStringLiteral("ground/armour");
    e.clampToTerrain = true;
    const auto s = resolveStyle(e, nullptr);
    QVERIFY(s.modelFile.isEmpty());
    QCOMPARE(s.shape, 3);
    QCOMPARE(s.scale, 1.0);
    QVERIFY(s.clampToTerrain);
}

void TestModelTable::resolveWithTable()
{
    const ModelTable t = table(kTable);
    EntityState3D e;
    e.kindKey = QStringLiteral("ground/armour");
    e.shape = 0;
    auto s = resolveStyle(e, &t);
    QCOMPARE(s.modelFile, QStringLiteral("/data/models/t72.osgb"));
    QCOMPARE(s.shape, 2);                 // table overrides the placeholder
    QCOMPARE(s.headingOffsetDeg, 90.0);
    QVERIFY(!s.clampToTerrain);           // exact entry does not clamp, host did not ask

    e.kindKey = QStringLiteral("ground/infantry");
    s = resolveStyle(e, &t);
    QVERIFY(s.modelFile.isEmpty());
    QCOMPARE(s.scale, 2.0);
    QVERIFY(s.clampToTerrain);            // the table asks for clamping

    e.kindKey = QStringLiteral("unmatched-but-catch-all");
    e.shape = 1;
    s = resolveStyle(e, &t);
    QCOMPARE(s.shape, 0);                 // "*" sets shape 0
}

void TestModelTable::visibilityAndLabelFallback()
{
    EntityState3D e;
    e.id = 42;
    e.visible = false;
    auto s = resolveStyle(e, nullptr);
    QVERIFY(!s.visible);
    QCOMPARE(s.label, QStringLiteral("#42"));
    e.label = QStringLiteral("T-72 #3");
    e.visible = true;
    s = resolveStyle(e, nullptr);
    QVERIFY(s.visible);
    QCOMPARE(s.label, QStringLiteral("T-72 #3"));
}

QTEST_APPLESS_MAIN(TestModelTable)
#include "tst_modeltable.moc"
