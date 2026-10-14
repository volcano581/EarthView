#include <earthview3d/ScreenScale.h>

#include <QtTest>

#include <cmath>

using namespace earthview3d::screenscale;

/**
 * @brief Marker screen-size arithmetic (I-018): markers stay readable without ever growing
 * into shapes long enough to graze the terrain.
 */
class TestScreenScale : public QObject
{
    Q_OBJECT

private slots:
    void metresPerPixelIsProportionalToDistance();
    void metresPerPixelRejectsDegenerateCameras();
    void markerKeepsRealSizeUpClose();
    void markerGrowsToTheMinimumPixelSize();
    void markerIsCappedWhateverTheCameraSays();
    void markerRejectsDegenerateInput();
    void pixelSizeChoosesModelOrIcon();
};

void TestScreenScale::metresPerPixelIsProportionalToDistance()
{
    // 1000 px high, 35 deg vertical fov, 1 km away: frustum height 2*1000*tan(17.5 deg).
    const double expected = 2.0 * 1000.0 * std::tan(17.5 * M_PI / 180.0) / 1000.0;
    QVERIFY(qFuzzyCompare(metresPerPixel(1000.0, 35.0, 1000), expected));
    QVERIFY(qFuzzyCompare(metresPerPixel(2000.0, 35.0, 1000), 2.0 * expected));
    // Twice the pixels, half the ground per pixel.
    QVERIFY(qFuzzyCompare(metresPerPixel(1000.0, 35.0, 2000), 0.5 * expected));
}

void TestScreenScale::metresPerPixelRejectsDegenerateCameras()
{
    QCOMPARE(metresPerPixel(0.0, 35.0, 1000), 0.0);
    QCOMPARE(metresPerPixel(-10.0, 35.0, 1000), 0.0);
    QCOMPARE(metresPerPixel(1000.0, 0.0, 1000), 0.0);
    QCOMPARE(metresPerPixel(1000.0, 180.0, 1000), 0.0);
    QCOMPARE(metresPerPixel(1000.0, 35.0, 0), 0.0);
}

void TestScreenScale::markerKeepsRealSizeUpClose()
{
    // A 7.5 m vehicle 0.05 m per pixel away already covers 150 px: never drawn below real size.
    QCOMPARE(markerScale(7.5, 0.05, 14.0, 50000.0), 1.0);
}

void TestScreenScale::markerGrowsToTheMinimumPixelSize()
{
    // 4 m marker, 100 m per pixel: 14 px asks for 1400 m, i.e. 350x.
    QVERIFY(qFuzzyCompare(markerScale(4.0, 100.0, 14.0, 50000.0), 350.0));
    // and the marker is then exactly kMinMarkerPixels across.
    QVERIFY(qFuzzyCompare(pixelSize(4.0 * 350.0, 100.0), 14.0));
}

void TestScreenScale::markerIsCappedWhateverTheCameraSays()
{
    // A stale/absurd metres-per-pixel (the osg::AutoTransform failure mode) cannot produce a
    // marker longer than the cap: 4 m * 12500 = 50 km, not thousands of kilometres.
    QVERIFY(qFuzzyCompare(markerScale(4.0, 1.0e6, 14.0, 50000.0), 12500.0));
    QVERIFY(markerScale(4.0, 1.0e9, 14.0, 50000.0) * 4.0 <= 50000.0);
    // No cap requested: the scale is whatever the pixel size asks for.
    QVERIFY(qFuzzyCompare(markerScale(4.0, 1.0e6, 14.0, 0.0), 3.5e6));
}

void TestScreenScale::markerRejectsDegenerateInput()
{
    QCOMPARE(markerScale(0.0, 100.0, 14.0, 50000.0), 1.0);
    QCOMPARE(markerScale(4.0, 0.0, 14.0, 50000.0), 1.0);
    QCOMPARE(markerScale(4.0, 100.0, 0.0, 50000.0), 1.0);
}

void TestScreenScale::pixelSizeChoosesModelOrIcon()
{
    // 7.5 m model, 12 px threshold: the model wins below 0.625 m per pixel.
    QVERIFY(pixelSize(7.5, 0.5) >= 12.0);
    QVERIFY(pixelSize(7.5, 1.0) < 12.0);
    QCOMPARE(pixelSize(7.5, 0.0), 0.0);
    QCOMPARE(pixelSize(0.0, 1.0), 0.0);
}

QTEST_MAIN(TestScreenScale)
#include "tst_screenscale.moc"
