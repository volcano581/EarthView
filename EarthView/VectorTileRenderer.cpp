#include "VectorTileRenderer.h"
#include "Camera.h"
#include "Constants.h"
#include "MercatorProjection.h"
#include "ShaderUtils.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QStringList>
#include <QtGlobal>
#include <QVector2D>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

    bool nearlyEqual(const QPointF& a, const QPointF& b)
{
    constexpr double epsilon = 1.0e-6;
    return std::abs(a.x() - b.x()) <= epsilon
        && std::abs(a.y() - b.y()) <= epsilon;
}

    struct UnitVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

int openRingSize(const QVector<QPointF>& path)
{
    int size = path.size();
    while (size > 1 && nearlyEqual(path.at(size - 1), path.first())) {
        --size;
    }
    return size;
}

UnitVec3 latLonToUnitVec(double latDeg, double lonDeg)
{
    const double lat = qDegreesToRadians(latDeg);
    const double lon = qDegreesToRadians(lonDeg);
    const double cosLat = std::cos(lat);

    return {
        cosLat * std::cos(lon),
        cosLat * std::sin(lon),
        std::sin(lat)
    };
}

double dot(const UnitVec3& a, const UnitVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

UnitVec3 normalize(const UnitVec3& v)
{
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0)
        return {};
    return { v.x / len, v.y / len, v.z / len };
}

UnitVec3 mercatorToUnitVec(const QPointF& mercator)
{
    const QPointF latLon = MercatorProjection::mercatorToLatLon(
        mercator.x(),
        mercator.y());

    return latLonToUnitVec(latLon.x(), latLon.y());
}

QPointF unitVecToMercator(const UnitVec3& v)
{
    const UnitVec3 n = normalize(v);

    const double lat = std::asin(qBound(-1.0, n.z, 1.0));
    const double lon = std::atan2(n.y, n.x);

    return MercatorProjection::latLonToMercator(
        qRadiansToDegrees(lat),
        qRadiansToDegrees(lon));
}

QPointF horizonIntersectionMercator(
    const QPointF& aMercator,
    const QPointF& bMercator,
    const UnitVec3& viewCenter)
{
    const UnitVec3 a = mercatorToUnitVec(aMercator);
    const UnitVec3 b = mercatorToUnitVec(bMercator);

    const double da = dot(viewCenter, a);
    const double db = dot(viewCenter, b);
    const double denom = da - db;

    if (std::abs(denom) <= 1.0e-12)
        return aMercator;

    const double t = qBound(0.0, da / denom, 1.0);

    UnitVec3 p {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };

    return unitVecToMercator(p);
}

QVector<QPointF> clipRingToVisibleHemisphere(
    const QVector<QPointF>& ring,
    const Camera* camera)
{
    QVector<QPointF> clipped;

    if (!camera || ring.size() < 3)
        return clipped;

    const QPointF centerLatLon = MercatorProjection::mercatorToLatLon(
        camera->getCenterMercator().x(),
        camera->getCenterMercator().y());

    const UnitVec3 viewCenter = latLonToUnitVec(
        centerLatLon.x(),
        centerLatLon.y());

    const int ringSize = openRingSize(ring);
    if (ringSize < 3)
        return clipped;

    constexpr double epsilon = 1.0e-10;

    for (int i = 0; i < ringSize; ++i) {
        const QPointF current = ring.at(i);
        const QPointF next = ring.at((i + 1) % ringSize);

        const double currentDot = dot(viewCenter, mercatorToUnitVec(current));
        const double nextDot = dot(viewCenter, mercatorToUnitVec(next));

        const bool currentVisible = currentDot >= -epsilon;
        const bool nextVisible = nextDot >= -epsilon;

        if (currentVisible && nextVisible) {
            if (clipped.isEmpty() || !nearlyEqual(clipped.last(), next))
                clipped.append(next);
        }
        else if (currentVisible && !nextVisible) {
            const QPointF intersection =
                horizonIntersectionMercator(current, next, viewCenter);

            if (clipped.isEmpty() || !nearlyEqual(clipped.last(), intersection))
                clipped.append(intersection);
        }
        else if (!currentVisible && nextVisible) {
            const QPointF intersection =
                horizonIntersectionMercator(current, next, viewCenter);

            if (clipped.isEmpty() || !nearlyEqual(clipped.last(), intersection))
                clipped.append(intersection);

            if (!nearlyEqual(clipped.last(), next))
                clipped.append(next);
        }
    }

    while (clipped.size() > 1 && nearlyEqual(clipped.last(), clipped.first())) {
        clipped.removeLast();
    }

    return clipped;
}
using CoordinateMode = LineBatchRenderer::CoordinateMode;

QString tagString(const MbTilesVectorFeature& feature, const QString& key)
{
    return feature.tags.value(key).toString();
}

QColor landColor(const QString& layerName, const MbTilesVectorFeature& feature)
{
    const QString klass = tagString(feature, QStringLiteral("class"));
    if (layerName == QStringLiteral("park") || klass == QStringLiteral("park")
        || klass == QStringLiteral("wood") || klass == QStringLiteral("forest")) {
        return QColor(199, 224, 188, 210);
    }
    if (klass == QStringLiteral("grass") || klass == QStringLiteral("meadow")) {
        return QColor(213, 232, 202, 210);
    }
    if (klass == QStringLiteral("industrial")) {
        return QColor(231, 222, 217, 210);
    }
    return QColor(226, 229, 210, 205);
}

QColor baseLandColor()
{
    return QColor(226, 229, 210, 255);
}

QColor roadColor(const MbTilesVectorFeature& feature)
{
    const QString klass = tagString(feature, QStringLiteral("class"));
    if (klass == QStringLiteral("motorway") || klass == QStringLiteral("trunk")) {
        return QColor(232, 146, 83, 235);
    }
    if (klass == QStringLiteral("primary")) {
        return QColor(238, 183, 92, 235);
    }
    if (klass == QStringLiteral("secondary") || klass == QStringLiteral("tertiary")) {
        return QColor(245, 219, 132, 230);
    }
    if (klass == QStringLiteral("path") || klass == QStringLiteral("track")) {
        return QColor(210, 205, 184, 220);
    }
    if (klass == QStringLiteral("rail")) {
        return QColor(150, 145, 145, 210);
    }
    return QColor(250, 248, 235, 225);
}

QColor fillColorForFeature(const QString& layerName, const MbTilesVectorFeature& feature, int zoomLevel)
{
    if (feature.type != 3)
        return QColor(0, 0, 0, 0);

    if (layerName == QStringLiteral("water")) {
        return QColor(91, 150, 190, 220);
    }
    if (layerName == QStringLiteral("landcover")
        || layerName == QStringLiteral("landuse")
        || layerName == QStringLiteral("park")) {
        return landColor(layerName, feature);
    }
    if (layerName == QStringLiteral("building") && zoomLevel >= 13) {
        return QColor(214, 205, 196, 190);
    }
    if (layerName == QStringLiteral("aeroway")) {
        return QColor(220, 220, 216, 175);
    }

    return QColor(0, 0, 0, 0);
}

int fillDrawOrderForLayer(const QString& layerName)
{
    if (layerName == QStringLiteral("landcover")
        || layerName == QStringLiteral("landuse")
        || layerName == QStringLiteral("park")) {
        return 10;
    }
    if (layerName == QStringLiteral("aeroway")) {
        return 15;
    }
    if (layerName == QStringLiteral("water")) {
        return 20;
    }
    if (layerName == QStringLiteral("building")) {
        return 30;
    }
    return 25;
}

QColor lineColorForFeature(const QString& layerName, const MbTilesVectorFeature& feature, int zoomLevel)
{
    if (layerName == QStringLiteral("transportation")
        || layerName == QStringLiteral("transportation_name")) {
        return roadColor(feature);
    }
    if (layerName == QStringLiteral("waterway")) {
        return QColor(86, 151, 192, 220);
    }
    if (layerName == QStringLiteral("boundary")) {
        return QColor(130, 120, 120, 150);
    }
    if (layerName == QStringLiteral("building") && zoomLevel >= 14) {
        return QColor(160, 150, 145, 130);
    }
    if (layerName == QStringLiteral("water")) {
        return QColor(54, 116, 165, 135);
    }
    if (feature.type == 2) {
        return QColor(150, 150, 140, 180);
    }

    return QColor(0, 0, 0, 0);
}

QString featureLabel(const MbTilesVectorFeature& feature)
{
    const QStringList keys = {
        QStringLiteral("name"),
        QStringLiteral("name:en"),
        QStringLiteral("name_int"),
        QStringLiteral("ref"),
        QStringLiteral("housenumber")
    };

    for (const QString& key : keys) {
        const QString text = tagString(feature, key).trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }

    return QString();
}

bool labelLayerVisible(const QString& layerName, int zoomLevel)
{
    if (layerName == QStringLiteral("place"))
        return zoomLevel >= 5;
    if (layerName == QStringLiteral("transportation_name"))
        return zoomLevel >= 12;
    if (layerName == QStringLiteral("poi"))
        return zoomLevel >= 15;

    return false;
}

int maxVectorLabelsForZoom(int zoomLevel)
{
    if (zoomLevel < 8)
        return 120;
    if (zoomLevel < 12)
        return 260;
    if (zoomLevel < 15)
        return 420;
    return 650;
}

LineBatchRenderer::LineVertex coloredVertex(const QPointF& point, const QColor& color)
{
    return LineBatchRenderer::vertex(point.x(), point.y(), color);
}

void appendColoredQuad(
    QVector<LineBatchRenderer::LineVertex>& vertices,
    const QPointF& p00,
    const QPointF& p10,
    const QPointF& p11,
    const QPointF& p01,
    const QColor& color)
{
    vertices.append(coloredVertex(p00, color));
    vertices.append(coloredVertex(p10, color));
    vertices.append(coloredVertex(p11, color));
    vertices.append(coloredVertex(p00, color));
    vertices.append(coloredVertex(p11, color));
    vertices.append(coloredVertex(p01, color));
}

void appendProjectedSegment(
    QVector<LineBatchRenderer::LineVertex>& vertices,
    const Camera* camera,
    const QPointF& a,
    const QPointF& b,
    const QColor& color,
    CoordinateMode mode)
{
    if (mode == CoordinateMode::Mercator) {
        LineBatchRenderer::appendSegment(vertices, a, b, color);
        return;
    }

    QPointF screenA;
    QPointF screenB;
    if (!camera
        || !camera->projectMercatorToScreen(a, &screenA)
        || !camera->projectMercatorToScreen(b, &screenB)) {
        return;
    }

    LineBatchRenderer::appendSegment(vertices, screenA, screenB, color);
}

void appendProjectedTileBackground(
    QVector<LineBatchRenderer::LineVertex>& vertices,
    const Camera* camera,
    const QRectF& bounds,
    const QColor& color,
    CoordinateMode mode)
{
    if (mode == CoordinateMode::Mercator) {
        appendColoredQuad(
            vertices,
            bounds.topLeft(),
            QPointF(bounds.right(), bounds.top()),
            bounds.bottomRight(),
            QPointF(bounds.left(), bounds.bottom()),
            color);
        return;
    }

    if (!camera)
        return;

    constexpr int subdivisions = 16;
    const double left = bounds.left();
    const double right = bounds.right();
    const double top = bounds.top();
    const double bottom = bounds.bottom();
    for (int y = 0; y < subdivisions; ++y) {
        const double v0 = y / static_cast<double>(subdivisions);
        const double v1 = (y + 1) / static_cast<double>(subdivisions);
        const double mercatorY0 = top + (bottom - top) * v0;
        const double mercatorY1 = top + (bottom - top) * v1;

        for (int x = 0; x < subdivisions; ++x) {
            const double u0 = x / static_cast<double>(subdivisions);
            const double u1 = (x + 1) / static_cast<double>(subdivisions);
            const double mercatorX0 = left + (right - left) * u0;
            const double mercatorX1 = left + (right - left) * u1;

            QPointF p00;
            QPointF p10;
            QPointF p11;
            QPointF p01;
            if (!camera->projectMercatorToScreen(QPointF(mercatorX0, mercatorY0), &p00)
                || !camera->projectMercatorToScreen(QPointF(mercatorX1, mercatorY0), &p10)
                || !camera->projectMercatorToScreen(QPointF(mercatorX1, mercatorY1), &p11)
                || !camera->projectMercatorToScreen(QPointF(mercatorX0, mercatorY1), &p01)) {
                continue;
            }

            appendColoredQuad(vertices, p00, p10, p11, p01, color);
        }
    }
}




double signedRingArea(const QVector<QPointF>& path, int size)
{
    double area = 0.0;
    for (int i = 0; i < size; ++i) {
        const QPointF& a = path.at(i);
        const QPointF& b = path.at((i + 1) % size);
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area * 0.5;
}

// void appendProjectedWindingPath(
//     QVector<LineBatchRenderer::LineVertex>& vertices,
//     const Camera* camera,
//     const QVector<QPointF>& mercatorPath,
//     const QColor& color,
//     CoordinateMode mode)
// {
//     const int ringSize = openRingSize(mercatorPath);
//     if (ringSize < 3)
//         return;

//     // These edge fans are not drawn directly; front/back stencil ops turn them
//     // into the MVT winding fill, so concave rings and holes cancel correctly.
//     if (mode == CoordinateMode::Mercator) {
//         const QPointF origin = mercatorPath.first();
//         for (int i = 0; i < ringSize; ++i) {
//             const QPointF& a = mercatorPath.at(i);
//             const QPointF& b = mercatorPath.at((i + 1) % ringSize);
//             if (nearlyEqual(a, b))
//                 continue;

//             vertices.append(coloredVertex(origin, color));
//             vertices.append(coloredVertex(a, color));
//             vertices.append(coloredVertex(b, color));
//         }
//         return;
//     }

//     if (!camera)
//         return;

//     const QPointF origin(
//         camera->getViewportWidth() * 0.5,
//         camera->getViewportHeight() * 0.5);
//     for (int i = 0; i < ringSize; ++i) {
//         QPointF a;
//         QPointF b;
//         if (!camera->projectMercatorToScreen(mercatorPath.at(i), &a)
//             || !camera->projectMercatorToScreen(mercatorPath.at((i + 1) % ringSize), &b)
//             || nearlyEqual(a, b)) {
//             continue;
//         }

//         vertices.append(coloredVertex(origin, color));
//         vertices.append(coloredVertex(a, color));
//         vertices.append(coloredVertex(b, color));
//     }
// }

void appendProjectedWindingPath(
    QVector<LineBatchRenderer::LineVertex>& vertices,
    const Camera* camera,
    const QVector<QPointF>& mercatorPath,
    const QColor& color,
    CoordinateMode mode)
{
    const int ringSize = openRingSize(mercatorPath);
    if (ringSize < 3)
        return;

    if (mode == CoordinateMode::Mercator) {
        const QPointF origin = mercatorPath.first();
        for (int i = 0; i < ringSize; ++i) {
            const QPointF& a = mercatorPath.at(i);
            const QPointF& b = mercatorPath.at((i + 1) % ringSize);
            if (nearlyEqual(a, b))
                continue;

            vertices.append(coloredVertex(origin, color));
            vertices.append(coloredVertex(a, color));
            vertices.append(coloredVertex(b, color));
        }
        return;
    }

    if (!camera)
    return;

QVector<QPointF> clippedMercatorPath =
    clipRingToVisibleHemisphere(mercatorPath, camera);

const int clippedSize = openRingSize(clippedMercatorPath);
if (clippedSize < 3)
    return;

QVector<QPointF> screenPath;
screenPath.reserve(clippedSize);

for (int i = 0; i < clippedSize; ++i) {
    QPointF screenPoint;
    if (!camera->projectMercatorToScreen(clippedMercatorPath.at(i), &screenPoint))
        continue;

    if (screenPath.isEmpty() || !nearlyEqual(screenPath.last(), screenPoint)) {
        screenPath.append(screenPoint);
    }
}

const int screenSize = openRingSize(screenPath);
if (screenSize < 3)
    return;

const QPointF origin = screenPath.first();

for (int i = 0; i < screenSize; ++i) {
    const QPointF& a = screenPath.at(i);
    const QPointF& b = screenPath.at((i + 1) % screenSize);

    if (nearlyEqual(a, b))
        continue;

    vertices.append(coloredVertex(origin, color));
    vertices.append(coloredVertex(a, color));
    vertices.append(coloredVertex(b, color));
}
}

QVector<LineBatchRenderer::LineVertex> fillCoverVertices(int width, int height, const QColor& color)
{
    QVector<LineBatchRenderer::LineVertex> vertices;
    vertices.reserve(6);
    vertices.append(LineBatchRenderer::vertex(0.0, 0.0, color));
    vertices.append(LineBatchRenderer::vertex(width, 0.0, color));
    vertices.append(LineBatchRenderer::vertex(width, height, color));
    vertices.append(LineBatchRenderer::vertex(0.0, 0.0, color));
    vertices.append(LineBatchRenderer::vertex(width, height, color));
    vertices.append(LineBatchRenderer::vertex(0.0, height, color));
    return vertices;
}

}

VectorTileRenderer::VectorTileRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_lineRenderer(new LineBatchRenderer(camera, this))
    , m_fillVbo(0)
    , m_fillVao(0)
    , m_backgroundQuadVbo(0)
    , m_backgroundInstanceVbo(0)
    , m_backgroundVao(0)
    , m_backgroundFirst(0)
    , m_backgroundCount(0)
    , m_fillViewportWidth(0)
    , m_fillViewportHeight(0)
    , m_batchResolution(0.0)
    , m_coordinateMode(CoordinateMode::Mercator)
    , m_gpuResourcesInitialized(false)
    , m_batchesDirty(true)
    , m_fillBufferDirty(true)
    , m_backgroundInstanceBufferDirty(true)
    , m_hasBatchCameraState(false)
    , m_batchWasOrthographic(false)
    , m_enabled(true)
{
    initializeOpenGLFunctions();
}

VectorTileRenderer::~VectorTileRenderer()
{
    if (!m_gpuResourcesInitialized)
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (!f)
        return;

    if (m_fillVbo) {
        f->glDeleteBuffers(1, &m_fillVbo);
    }
    if (m_fillVao) {
        f->glDeleteVertexArrays(1, &m_fillVao);
    }
    if (m_backgroundQuadVbo) {
        f->glDeleteBuffers(1, &m_backgroundQuadVbo);
    }
    if (m_backgroundInstanceVbo) {
        f->glDeleteBuffers(1, &m_backgroundInstanceVbo);
    }
    if (m_backgroundVao) {
        f->glDeleteVertexArrays(1, &m_backgroundVao);
    }
}

void VectorTileRenderer::setTileSourceLayers(const QList<TmsLoader::TileSourceLayer>& layers)
{
    m_layers = layers;
    clearCache();
}

void VectorTileRenderer::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    if (!m_enabled) {
        clearCache();
    }
}

bool VectorTileRenderer::hasVectorLayers() const
{
    for (const TmsLoader::TileSourceLayer& layer : m_layers) {
        if (layer.sourceType == TmsLoader::TileSourceLayer::SourceType::VectorMbTiles) {
            return true;
        }
    }
    return false;
}

void VectorTileRenderer::initializeGpuResources()
{
    if (m_gpuResourcesInitialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            &m_screenFillProgram,
            QStringLiteral("colored_2d.vert"),
            QStringLiteral("colored_line.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    if (!ShaderUtils::loadProgram(
            &m_mercatorFillProgram,
            QStringLiteral("colored_mercator.vert"),
            QStringLiteral("colored_line.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    if (!ShaderUtils::loadProgram(
            &m_mercatorBackgroundProgram,
            QStringLiteral("instanced_rect_mercator.vert"),
            QStringLiteral("colored_line.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    f->glGenVertexArrays(1, &m_fillVao);
    f->glGenBuffers(1, &m_fillVbo);
    setupFillVertexArray();
    f->glGenVertexArrays(1, &m_backgroundVao);
    f->glGenBuffers(1, &m_backgroundQuadVbo);
    f->glGenBuffers(1, &m_backgroundInstanceVbo);
    setupBackgroundInstanceArray();
    m_gpuResourcesInitialized = true;
}

void VectorTileRenderer::setupFillVertexArray()
{
    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindVertexArray(m_fillVao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_fillVbo);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(LineBatchRenderer::LineVertex),
        reinterpret_cast<void*>(offsetof(LineBatchRenderer::LineVertex, position)));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(LineBatchRenderer::LineVertex),
        reinterpret_cast<void*>(offsetof(LineBatchRenderer::LineVertex, color)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);
}

void VectorTileRenderer::setupBackgroundInstanceArray()
{
    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    static constexpr float quadVertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };

    f->glBindVertexArray(m_backgroundVao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_backgroundQuadVbo);
    f->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    f->glBindBuffer(GL_ARRAY_BUFFER, m_backgroundInstanceVbo);
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BackgroundInstance),
        reinterpret_cast<void*>(offsetof(BackgroundInstance, rect)));
    f->glVertexAttribDivisor(1, 1);
    f->glEnableVertexAttribArray(2);
    f->glVertexAttribPointer(
        2,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BackgroundInstance),
        reinterpret_cast<void*>(offsetof(BackgroundInstance, color)));
    f->glVertexAttribDivisor(2, 1);

    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);
}

void VectorTileRenderer::clearCache()
{
    m_tileCache.clear();
    m_visibleKeys.clear();
    m_backgroundInstances.clear();
    m_backgroundFillVertices.clear();
    m_fillBatches.clear();
    m_fillUploadVertices.clear();
    m_fillDraws.clear();
    m_lineVertices.clear();
    m_backgroundFirst = 0;
    m_backgroundCount = 0;
    m_batchesDirty = true;
    m_fillBufferDirty = true;
    m_backgroundInstanceBufferDirty = true;
    m_hasBatchCameraState = false;
}

QList<int> VectorTileRenderer::vectorLayerIndicesForZoom(int zoomLevel) const
{
    QList<int> indices;
    for (int i = 0; i < m_layers.size(); ++i) {
        const TmsLoader::TileSourceLayer& layer = m_layers.at(i);
        if (layer.sourceType != TmsLoader::TileSourceLayer::SourceType::VectorMbTiles)
            continue;

        const bool displayZoomMatches = zoomLevel >= layer.minZoom && zoomLevel <= layer.maxZoom;
        const bool canUseAsLowerResolutionUnderlay =
            zoomLevel > layer.maxZoom
            && zoomLevel > layer.maxTileZoom
            && layer.maxTileZoom >= layer.minTileZoom;

        if (displayZoomMatches || canUseAsLowerResolutionUnderlay) {
            indices.append(i);
        }
    }

    std::sort(indices.begin(), indices.end(), [this, zoomLevel](int a, int b) {
        const int zoomA = tileZoomForLayer(zoomLevel, a);
        const int zoomB = tileZoomForLayer(zoomLevel, b);
        if (zoomA != zoomB)
            return zoomA < zoomB;

        return a < b;
    });

    return indices;
}

int VectorTileRenderer::tileZoomForLayer(int zoomLevel, int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= m_layers.size())
        return zoomLevel;

    const TmsLoader::TileSourceLayer& layer = m_layers.at(layerIndex);
    int minTileZoom = qBound(0, layer.minTileZoom, GIS::MAX_TILE_ZOOM);
    int maxTileZoom = qBound(0, layer.maxTileZoom, GIS::MAX_TILE_ZOOM);
    if (minTileZoom > maxTileZoom) {
        std::swap(minTileZoom, maxTileZoom);
    }

    return qBound(minTileZoom, zoomLevel, maxTileZoom);
}

QString VectorTileRenderer::tileKey(int layerIndex, int z, int x, int y) const
{
    return QString("%1_%2_%3_%4").arg(layerIndex).arg(z).arg(x).arg(y);
}

void VectorTileRenderer::updateVisibleTiles()
{
    if (!m_enabled || !m_camera) {
        return;
    }

    const int cameraZoomLevel = m_camera->getTileZoomLevel();
    const QList<int> layerIndices = vectorLayerIndicesForZoom(cameraZoomLevel);
    QSet<QString> newVisibleKeys;

    constexpr qint64 maxTilesPerLayer = 2048;
    for (int layerIndex : layerIndices) {
        const TmsLoader::TileSourceLayer& layer = m_layers.at(layerIndex);
        const int tileZoomLevel = tileZoomForLayer(cameraZoomLevel, layerIndex);
        const QRect tileRange = m_camera->getTileRange(tileZoomLevel);
        const qint64 requestedTileCount = static_cast<qint64>(tileRange.width()) * tileRange.height();
        if (tileRange.width() <= 0 || tileRange.height() <= 0 || requestedTileCount > maxTilesPerLayer) {
            qWarning() << "Skipping vector tile update with excessive tile range"
                       << tileRange
                       << "at zoom" << tileZoomLevel
                       << "for layer" << layerIndex
                       << "count" << requestedTileCount;
            continue;
        }

        for (int x = tileRange.x(); x < tileRange.x() + tileRange.width(); ++x) {
            for (int y = tileRange.y(); y < tileRange.y() + tileRange.height(); ++y) {
                const QString key = tileKey(layerIndex, tileZoomLevel, x, y);
                newVisibleKeys.insert(key);
                if (m_tileCache.contains(key))
                    continue;

                QString errorMessage;
                const MbTilesVectorTile vectorTile = MbTilesReader::readVectorTile(
                    layer.mbTilesPath,
                    tileZoomLevel,
                    MercatorProjection::wrapTileX(x, tileZoomLevel),
                    y,
                    layer.tmsYOrigin,
                    &errorMessage);

                if (!errorMessage.isEmpty() && !vectorTile.isEmpty()) {
                    qDebug() << errorMessage;
                }
                else if (!errorMessage.isEmpty()) {
                    qWarning() << errorMessage;
                }

                CachedTile cachedTile;
                cachedTile.layerIndex = layerIndex;
                cachedTile.z = tileZoomLevel;
                cachedTile.x = x;
                cachedTile.y = y;
                cachedTile.mercatorBounds = MercatorProjection::tileToMercatorBounds(tileZoomLevel, x, y);
                cachedTile.tile = vectorTile;
                m_tileCache.insert(key, cachedTile);
                m_batchesDirty = true;
            }
        }
    }

    if (newVisibleKeys != m_visibleKeys) {
        m_visibleKeys = newVisibleKeys;
        m_batchesDirty = true;
    }

    QList<QString> keysToRemove;
    for (auto it = m_tileCache.cbegin(); it != m_tileCache.cend(); ++it) {
        if (!m_visibleKeys.contains(it.key())) {
            keysToRemove.append(it.key());
        }
    }
    if (!keysToRemove.isEmpty()) {
        for (const QString& key : keysToRemove) {
            m_tileCache.remove(key);
        }
        m_batchesDirty = true;
    }
}

QPointF VectorTileRenderer::tilePixelToMercator(const QPointF& point, const QRectF& bounds) const
{
    const double u = point.x() / static_cast<double>(GIS::TILE_SIZE);
    const double v = point.y() / static_cast<double>(GIS::TILE_SIZE);
    const double left = bounds.left();
    const double right = bounds.right();
    const double top = bounds.top();
    const double bottom = bounds.bottom();
    return QPointF(
        left + (right - left) * u,
        top + (bottom - top) * v);
}

void VectorTileRenderer::rebuildBatches()
{
    if (!m_batchesDirty)
        return;

    m_backgroundInstances.clear();
    m_backgroundFillVertices.clear();
    m_fillBatches.clear();
    m_fillUploadVertices.clear();
    m_fillDraws.clear();
    m_lineVertices.clear();
    m_backgroundFirst = 0;
    m_backgroundCount = 0;

    m_coordinateMode = m_camera && m_camera->isOrthographic()
        ? CoordinateMode::Screen
        : CoordinateMode::Mercator;
    m_fillViewportWidth = m_camera ? m_camera->getViewportWidth() : 0;
    m_fillViewportHeight = m_camera ? m_camera->getViewportHeight() : 0;

    QList<QString> keys = m_visibleKeys.values();
    std::sort(keys.begin(), keys.end());

    auto appendFillToBatch = [this](int drawOrder, const QColor& color, const QVector<QPointF>& mercatorPath) {
        qsizetype batchIndex = -1;
        for (qsizetype i = 0; i < m_fillBatches.size(); ++i) {
            if (m_fillBatches.at(i).drawOrder == drawOrder && m_fillBatches.at(i).color == color) {
                batchIndex = i;
                break;
            }
        }
        if (batchIndex < 0) {
            FillBatch batch;
            batch.color = color;
            batch.drawOrder = drawOrder;
            m_fillBatches.append(batch);
            batchIndex = m_fillBatches.size() - 1;
        }

        appendProjectedWindingPath(
            m_fillBatches[batchIndex].windingVertices,
            m_camera,
            mercatorPath,
            color,
            m_coordinateMode);
    };

    for (const QString& key : keys) {
        const auto tileIt = m_tileCache.constFind(key);
        if (tileIt == m_tileCache.cend())
            continue;

        const CachedTile& cachedTile = tileIt.value();
        const int zoomLevel = cachedTile.z;
        if (m_coordinateMode == CoordinateMode::Mercator) {
            BackgroundInstance instance;
            instance.rect[0] = static_cast<float>(cachedTile.mercatorBounds.left());
            instance.rect[1] = static_cast<float>(cachedTile.mercatorBounds.top());
            instance.rect[2] = static_cast<float>(cachedTile.mercatorBounds.right());
            instance.rect[3] = static_cast<float>(cachedTile.mercatorBounds.bottom());
            const QColor color = baseLandColor();
            instance.color[0] = static_cast<float>(color.redF());
            instance.color[1] = static_cast<float>(color.greenF());
            instance.color[2] = static_cast<float>(color.blueF());
            instance.color[3] = static_cast<float>(color.alphaF());
            m_backgroundInstances.append(instance);
        }
        else {
            appendProjectedTileBackground(
                m_backgroundFillVertices,
                m_camera,
                cachedTile.mercatorBounds,
                baseLandColor(),
                m_coordinateMode);
        }

        if (cachedTile.tile.isEmpty())
            continue;

        for (const MbTilesVectorLayer& layer : cachedTile.tile.layers) {
            for (const MbTilesVectorFeature& feature : layer.features) {
                const QColor fillColor = fillColorForFeature(layer.name, feature, zoomLevel);
                if (fillColor.alpha() > 0 && feature.type == 3) {
                    for (const QVector<QPointF>& path : feature.paths) {
                        if (path.size() < 3)
                            continue;

                        QVector<QPointF> mercatorPath;
                        const int pointCount = openRingSize(path);
                        if (pointCount < 3 || std::abs(signedRingArea(path, pointCount)) <= 1.0e-6)
                            continue;

                        mercatorPath.reserve(pointCount);
                        for (int i = 0; i < pointCount; ++i) {
                            mercatorPath.append(tilePixelToMercator(path.at(i), cachedTile.mercatorBounds));
                        }

                        appendFillToBatch(fillDrawOrderForLayer(layer.name), fillColor, mercatorPath);
                    }
                }

                const QColor lineColor = lineColorForFeature(layer.name, feature, zoomLevel);
                if (lineColor.alpha() <= 0)
                    continue;

                if (feature.type == 2 || feature.type == 3) {
                    for (const QVector<QPointF>& path : feature.paths) {
                        if (path.size() < 2)
                            continue;

                        for (int i = 1; i < path.size(); ++i) {
                            const QPointF previous = tilePixelToMercator(path.at(i - 1), cachedTile.mercatorBounds);
                            const QPointF current = tilePixelToMercator(path.at(i), cachedTile.mercatorBounds);
                            appendProjectedSegment(
                                m_lineVertices,
                                m_camera,
                                previous,
                                current,
                                lineColor,
                                m_coordinateMode);
                        }
                    }
                }
            }
        }
    }

    std::sort(m_fillBatches.begin(), m_fillBatches.end(), [](const FillBatch& a, const FillBatch& b) {
        if (a.drawOrder != b.drawOrder)
            return a.drawOrder < b.drawOrder;
        return a.color.rgba() < b.color.rgba();
    });

    if (!m_backgroundFillVertices.isEmpty()) {
        m_backgroundFirst = m_fillUploadVertices.size();
        m_backgroundCount = m_backgroundFillVertices.size();
        m_fillUploadVertices += m_backgroundFillVertices;
    }

    for (const FillBatch& batch : m_fillBatches) {
        if (batch.windingVertices.isEmpty())
            continue;

        FillDraw draw;
        draw.color = batch.color;
        draw.windingFirst = m_fillUploadVertices.size();
        draw.windingCount = batch.windingVertices.size();
        m_fillUploadVertices += batch.windingVertices;

        const QVector<LineBatchRenderer::LineVertex> coverVertices =
            fillCoverVertices(m_fillViewportWidth, m_fillViewportHeight, batch.color);
        draw.coverFirst = m_fillUploadVertices.size();
        draw.coverCount = coverVertices.size();
        m_fillUploadVertices += coverVertices;
        m_fillDraws.append(draw);
    }

    m_lineRenderer->setVertices(m_lineVertices);
    m_fillBufferDirty = true;
    m_backgroundInstanceBufferDirty = true;
    if (m_camera) {
        m_batchCenterMercator = m_camera->getCenterMercator();
        m_batchResolution = m_camera->getResolution();
        m_batchWasOrthographic = m_camera->isOrthographic();
        m_hasBatchCameraState = true;
    }
    m_batchesDirty = false;
}

void VectorTileRenderer::bindFillProgram(QOpenGLShaderProgram* program, LineBatchRenderer::CoordinateMode mode)
{
    if (!program || !program->isLinked())
        return;

    program->bind();
    program->setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));

    if (mode == CoordinateMode::Mercator) {
        program->setUniformValue(
            "u_centerMercator",
            QVector2D(
                static_cast<float>(m_camera->getCenterMercator().x()),
                static_cast<float>(m_camera->getCenterMercator().y())));
        program->setUniformValue(
            "u_pixelsPerMercator",
            static_cast<float>(GIS::EARTH_RADIUS / m_camera->getResolution()));
    }
}

void VectorTileRenderer::drawInstancedBackground()
{
    if (m_backgroundInstances.isEmpty()
        || m_coordinateMode != CoordinateMode::Mercator
        || !m_mercatorBackgroundProgram.isLinked()) {
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    m_mercatorBackgroundProgram.bind();
    m_mercatorBackgroundProgram.setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));
    m_mercatorBackgroundProgram.setUniformValue(
        "u_centerMercator",
        QVector2D(
            static_cast<float>(m_camera->getCenterMercator().x()),
            static_cast<float>(m_camera->getCenterMercator().y())));
    m_mercatorBackgroundProgram.setUniformValue(
        "u_pixelsPerMercator",
        static_cast<float>(GIS::EARTH_RADIUS / m_camera->getResolution()));

    f->glBindVertexArray(m_backgroundVao);
    if (m_backgroundInstanceBufferDirty) {
        f->glBindBuffer(GL_ARRAY_BUFFER, m_backgroundInstanceVbo);
        f->glBufferData(
            GL_ARRAY_BUFFER,
            m_backgroundInstances.size() * static_cast<qsizetype>(sizeof(BackgroundInstance)),
            m_backgroundInstances.constData(),
            GL_DYNAMIC_DRAW);
        m_backgroundInstanceBufferDirty = false;
    }
    f->glDrawArraysInstanced(
        GL_TRIANGLES,
        0,
        6,
        static_cast<GLsizei>(m_backgroundInstances.size()));
    f->glBindVertexArray(0);
    m_mercatorBackgroundProgram.release();
}

void VectorTileRenderer::drawFillBatch()
{
    if (m_backgroundInstances.isEmpty() && m_fillUploadVertices.isEmpty())
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
        return;

    QOpenGLShaderProgram* windingProgram = m_coordinateMode == CoordinateMode::Mercator
        ? &m_mercatorFillProgram
        : &m_screenFillProgram;
    if (!windingProgram->isLinked() || !m_screenFillProgram.isLinked())
        return;

    QOpenGLExtraFunctions* f = context->extraFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    drawInstancedBackground();

    if (m_fillUploadVertices.isEmpty()) {
        glDepthMask(GL_TRUE);
        return;
    }

    f->glBindVertexArray(m_fillVao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_fillVbo);
    if (m_fillBufferDirty) {
        f->glBufferData(
            GL_ARRAY_BUFFER,
            m_fillUploadVertices.size() * static_cast<qsizetype>(sizeof(LineBatchRenderer::LineVertex)),
            m_fillUploadVertices.constData(),
            GL_DYNAMIC_DRAW);
        m_fillBufferDirty = false;
    }

    if (m_backgroundCount > 0) {
        bindFillProgram(windingProgram, m_coordinateMode);
        f->glDrawArrays(
            GL_TRIANGLES,
            static_cast<GLint>(m_backgroundFirst),
            static_cast<GLsizei>(m_backgroundCount));
    }

    bool stencilEnabled = false;
    if (!m_fillDraws.isEmpty()) {
        if (context->format().stencilBufferSize() <= 0) {
            static bool warnedAboutStencil = false;
            if (!warnedAboutStencil) {
                qWarning() << "Vector MBTiles polygon fills require an OpenGL stencil buffer.";
                warnedAboutStencil = true;
            }
        }
        else {
            glEnable(GL_STENCIL_TEST);
            stencilEnabled = true;
        }
    }

    for (const FillDraw& draw : m_fillDraws) {
        if (draw.windingCount <= 0 || draw.coverCount <= 0)
            continue;
        if (!stencilEnabled)
            break;

        glStencilMask(0xff);
        glClearStencil(0);
        glClear(GL_STENCIL_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_ALWAYS, 0, 0xff);
        // Non-zero winding fill: front faces add, back faces subtract.
        glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);

        bindFillProgram(windingProgram, m_coordinateMode);
        f->glDrawArrays(
            GL_TRIANGLES,
            static_cast<GLint>(draw.windingFirst),
            static_cast<GLsizei>(draw.windingCount));

        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0x00);
        glStencilFunc(GL_NOTEQUAL, 0, 0xff);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

        bindFillProgram(&m_screenFillProgram, CoordinateMode::Screen);
        f->glDrawArrays(
            GL_TRIANGLES,
            static_cast<GLint>(draw.coverFirst),
            static_cast<GLsizei>(draw.coverCount));
    }

    glStencilMask(0xff);
    if (stencilEnabled) {
        glClear(GL_STENCIL_BUFFER_BIT);
    }
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    f->glBindVertexArray(0);
    windingProgram->release();
    m_screenFillProgram.release();
}

void VectorTileRenderer::render()
{
    if (!m_enabled || !m_camera || !hasVectorLayers())
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized)
        return;

    updateVisibleTiles();
    const bool orthographic = m_camera->isOrthographic();
    if (!m_hasBatchCameraState || orthographic != m_batchWasOrthographic) {
        m_batchesDirty = true;
    }
    else if (orthographic
        && (m_camera->getCenterMercator() != m_batchCenterMercator
            || m_camera->getResolution() != m_batchResolution)) {
        m_batchesDirty = true;
    }
    if (m_camera->getViewportWidth() != m_fillViewportWidth
        || m_camera->getViewportHeight() != m_fillViewportHeight) {
        m_batchesDirty = true;
    }
    rebuildBatches();

    drawFillBatch();
    m_lineRenderer->render(m_coordinateMode, 1.35f);
}

void VectorTileRenderer::appendLabels(QVector<TextRenderer::Label>& labels)
{
    if (!m_enabled || !m_camera || !hasVectorLayers())
        return;

    updateVisibleTiles();

    const int zoomLevel = m_camera->getTileZoomLevel();
    const int maxLabels = maxVectorLabelsForZoom(zoomLevel);
    int labelsDrawn = 0;

    QFont font = QApplication::font();
    font.setPointSize(zoomLevel >= 12 ? 8 : 9);
    font.setBold(zoomLevel < 12);
    QFontMetrics metrics(font);

    QVector<QRect> occupiedRects;
    occupiedRects.reserve(maxLabels);

    QList<QString> keys = m_visibleKeys.values();
    std::sort(keys.begin(), keys.end());

    for (const QString& key : keys) {
        if (labelsDrawn >= maxLabels)
            break;

        const auto tileIt = m_tileCache.constFind(key);
        if (tileIt == m_tileCache.cend() || tileIt.value().tile.isEmpty())
            continue;

        const CachedTile& cachedTile = tileIt.value();
        for (const MbTilesVectorLayer& layer : cachedTile.tile.layers) {
            if (!labelLayerVisible(layer.name, zoomLevel))
                continue;

            for (const MbTilesVectorFeature& feature : layer.features) {
                if (labelsDrawn >= maxLabels)
                    break;

                const QString text = featureLabel(feature);
                if (text.isEmpty() || feature.paths.isEmpty() || feature.paths.first().isEmpty())
                    continue;

                QPointF anchorMercator;
                const QVector<QPointF>& path = feature.paths.first();
                for (const QPointF& point : path) {
                    anchorMercator += tilePixelToMercator(point, cachedTile.mercatorBounds);
                }
                anchorMercator /= path.size();

                QPointF screen;
                if (!m_camera->projectMercatorToScreen(anchorMercator, &screen))
                    continue;
                if (screen.x() < -80.0 || screen.x() > m_camera->getViewportWidth() + 80.0
                    || screen.y() < -30.0 || screen.y() > m_camera->getViewportHeight() + 30.0) {
                    continue;
                }

                QRect textRect = metrics.boundingRect(text).adjusted(-5, -3, 5, 3);
                textRect.moveCenter(screen.toPoint());
                QRect collisionRect = textRect.adjusted(-8, -5, 8, 5);

                bool collides = false;
                for (const QRect& occupied : occupiedRects) {
                    if (occupied.intersects(collisionRect)) {
                        collides = true;
                        break;
                    }
                }
                if (collides)
                    continue;

                TextRenderer::Label label;
                label.text = text;
                label.rect = QRectF(textRect);
                label.font = font;
                label.textColor = QColor(238, 242, 238);
                label.backgroundColor = QColor(0, 0, 0, 100);
                label.radius = 3;
                labels.append(label);

                occupiedRects.append(collisionRect);
                ++labelsDrawn;
            }
        }
    }
}
