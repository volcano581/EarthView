#include "VectorTileRenderer.h"
#include "Camera.h"
#include "Constants.h"
#include "FrameProfiler.h"
#include "MapFeatures.h"
#include "MercatorProjection.h"
#include "ShaderUtils.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFontMetricsF>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QPointer>
#include <QStringList>
#include <QThreadPool>
#include <QtGlobal>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

using LineVertex = LineBatchRenderer::LineVertex;

// Vector tiles are made for 512 px display (OpenMapTiles), so reading one zoom level coarser
// than the 256 px raster grid keeps the same detail with a quarter of the tiles to read and
// decode. Styling uses the displayed zoom (tile zoom + this offset).
constexpr int kVectorTileZoomOffset = 1;
// Cap on tiles kept from earlier views while new ones load.
constexpr int kMaxRetainedTiles = 384;
// Pick tolerances (screen px).
constexpr double kPickPointRadius = 10.0;
constexpr double kPickLineDistance = 6.0;
// Longest straight piece kept in Mercator units (about 0.6 degree): on the globe a piece is a
// straight line in the azimuthal projection, so long ones (tile edges at low zoom) are split.
constexpr double kMaxSegment = 0.01;
// Largest globe texture side.
constexpr int kMaxGlobeTexture = 4096;

bool nearlyEqual(const QPointF& a, const QPointF& b)
{
    constexpr double epsilon = 1.0e-9;
    return std::abs(a.x() - b.x()) <= epsilon && std::abs(a.y() - b.y()) <= epsilon;
}

int openRingSize(const QVector<QPointF>& path)
{
    int size = path.size();
    while (size > 1 && nearlyEqual(path.at(size - 1), path.first()))
        --size;
    return size;
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

QPointF tilePixelToMercator(const QPointF& point, const QRectF& bounds)
{
    const double u = point.x() / static_cast<double>(GIS::TILE_SIZE);
    const double v = point.y() / static_cast<double>(GIS::TILE_SIZE);
    return QPointF(
        bounds.left() + (bounds.right() - bounds.left()) * u,
        bounds.top() + (bounds.bottom() - bounds.top()) * v);
}

QPointF mercatorToTilePixel(const QPointF& mercator, const QRectF& bounds)
{
    return QPointF(
        (mercator.x() - bounds.left()) / (bounds.right() - bounds.left()) * GIS::TILE_SIZE,
        (mercator.y() - bounds.top()) / (bounds.bottom() - bounds.top()) * GIS::TILE_SIZE);
}

/// True for a segment (tile pixels) that lies on or beyond one edge of the tile: a polygon's
/// clip edge or the tile's buffer. The neighbouring tile draws its own part, and outlining clip
/// edges would draw the tile grid across lakes and seas.
bool outsideTileEdge(const QPointF& a, const QPointF& b)
{
    constexpr double low = 0.5;
    constexpr double high = GIS::TILE_SIZE - 0.5;
    return (a.x() <= low && b.x() <= low) || (a.x() >= high && b.x() >= high)
        || (a.y() <= low && b.y() <= low) || (a.y() >= high && b.y() >= high);
}

/// Appends the points after @p a up to and including @p b, at most kMaxSegment apart.
/// @p a is a copy: callers pass the last point of @p out, which appending may move.
void appendSubdivided(QVector<QPointF>& out, const QPointF a, const QPointF b)
{
    const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
    const int pieces = qMax(1, int(std::ceil(length / kMaxSegment)));
    for (int i = 1; i <= pieces; ++i)
        out.append(a + (b - a) * (double(i) / pieces));
}

LineVertex coloredVertex(const QPointF& point, const QColor& color)
{
    return LineBatchRenderer::vertex(point.x(), point.y(), color);
}

/// Triangle fan around the ring's centre. The fans are not drawn directly: front/back stencil
/// ops turn them into the non-zero winding fill, so concave rings and holes cancel (any origin
/// gives the right fill; the centre keeps the triangles from overlapping much, which is what
/// the GPU pays for).
void appendWindingFan(QVector<LineVertex>& vertices, const QVector<QPointF>& ring, const QColor& color)
{
    QPointF origin;
    for (const QPointF& point : ring)
        origin += point;
    origin /= qMax<qsizetype>(1, ring.size());
    for (int i = 0; i < ring.size(); ++i) {
        const QPointF& a = ring.at(i);
        const QPointF& b = ring.at((i + 1) % ring.size());
        if (nearlyEqual(a, b))
            continue;
        vertices.append(coloredVertex(origin, color));
        vertices.append(coloredVertex(a, color));
        vertices.append(coloredVertex(b, color));
    }
}

QVector<LineVertex> coverVertices(float width, float height, const QColor& color)
{
    return {
        LineBatchRenderer::vertex(0.0, 0.0, color), LineBatchRenderer::vertex(width, 0.0, color),
        LineBatchRenderer::vertex(width, height, color), LineBatchRenderer::vertex(0.0, 0.0, color),
        LineBatchRenderer::vertex(width, height, color), LineBatchRenderer::vertex(0.0, height, color)
    };
}

quint64 fillKey(int drawOrder, const QColor& color)
{
    return (static_cast<quint64>(static_cast<quint32>(drawOrder)) << 32) | color.rgba();
}

QColor colorOfKey(quint64 key)
{
    return QColor::fromRgba(static_cast<QRgb>(key & 0xffffffffu));
}

QString tagString(const MbTilesVectorFeature& feature, const char* key)
{
    return feature.tags.value(QLatin1String(key)).toString();
}

QString featureLabel(const QString& layer, const MbTilesVectorFeature& feature)
{
    if (layer == QLatin1String("housenumber"))
        return tagString(feature, "housenumber").trimmed();
    static const char* kKeys[] = { "name", "name:en", "name_en", "name_int", "name:latin" };
    for (const char* key : kKeys) {
        const QString text = tagString(feature, key).trimmed();
        if (!text.isEmpty())
            return text;
    }
    return QString();
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    double t = lengthSquared > 0.0 ? QPointF::dotProduct(p - a, ab) / lengthSquared : 0.0;
    t = qBound(0.0, t, 1.0);
    const QPointF closest = a + ab * t;
    return std::hypot(p.x() - closest.x(), p.y() - closest.y());
}

/// Even-odd over every ring, so holes and multipolygons count right.
bool insidePolygon(const QPointF& p, const QVector<QVector<QPointF>>& rings)
{
    bool inside = false;
    for (const QVector<QPointF>& ring : rings) {
        const int n = ring.size();
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const QPointF& a = ring.at(i);
            const QPointF& b = ring.at(j);
            if ((a.y() > p.y()) != (b.y() > p.y())
                && p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()) {
                inside = !inside;
            }
        }
    }
    return inside;
}

void setInstanceColor(VectorTileRenderer::LineInstance* instance, const QColor& color)
{
    instance->color[0] = quint8(color.red());
    instance->color[1] = quint8(color.green());
    instance->color[2] = quint8(color.blue());
    instance->color[3] = quint8(color.alpha());
}

/// Thick-line instances for one styled line along @p path (tile pixels).
void appendLine(QVector<VectorTileRenderer::LineInstance>& out, const QVector<QPointF>& path, const QRectF& bounds,
                const MapTheme::Line& line)
{
    double distance = 0.0;
    for (int i = 1; i < path.size(); ++i) {
        const QPointF a = path.at(i - 1);
        const QPointF b = path.at(i);
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (length > 0.0 && !outsideTileEdge(a, b)) {
            const QPointF ma = tilePixelToMercator(a, bounds);
            const QPointF mb = tilePixelToMercator(b, bounds);
            const double mercatorLength = std::hypot(mb.x() - ma.x(), mb.y() - ma.y());
            const int pieces = qMax(1, int(std::ceil(mercatorLength / kMaxSegment)));
            for (int piece = 0; piece < pieces; ++piece) {
                const QPointF from = ma + (mb - ma) * (double(piece) / pieces);
                const QPointF to = ma + (mb - ma) * (double(piece + 1) / pieces);
                VectorTileRenderer::LineInstance instance;
                instance.segment[0] = float(from.x());
                instance.segment[1] = float(from.y());
                instance.segment[2] = float(to.x());
                instance.segment[3] = float(to.y());
                instance.width[0] = line.width * 0.5f;
                instance.width[1] = line.growth;
                instance.width[2] = float(distance + length * piece / pieces);
                instance.dash[0] = line.dash;
                instance.dash[1] = line.gap;
                setInstanceColor(&instance, line.color);
                out.append(instance);
            }
        }
        distance += length;
    }
}

} // namespace

VectorTileRenderer::VectorTileRenderer(Camera* camera, QObject* parent, Mode mode)
    : QObject(parent)
    , m_camera(camera)
    , m_mode(mode)
    , m_theme(QStringLiteral("standard"))
    , m_themeGeneration(0)
    , m_quadVbo(0)
    , m_coverVao(0)
    , m_compositeVao(0)
    , m_globeTarget(nullptr)
    , m_gpuResourcesInitialized(false)
    , m_enabled(true)
    , m_requestGeneration(0)
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

    for (const CachedTile& tile : std::as_const(m_tileCache))
        releaseMesh(tile.mesh);
    deleteReleasedMeshes();
    m_coverBuffer.destroy();
    m_compositeBuffer.destroy();
    if (m_coverVao)
        f->glDeleteVertexArrays(1, &m_coverVao);
    if (m_compositeVao)
        f->glDeleteVertexArrays(1, &m_compositeVao);
    if (m_quadVbo)
        f->glDeleteBuffers(1, &m_quadVbo);
    delete m_globeTarget;
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
    if (!m_enabled)
        clearCache();
}

void VectorTileRenderer::setHiddenLayers(const QStringList& layers)
{
    const QSet<QString> hidden(layers.cbegin(), layers.cend());
    if (hidden == m_hiddenLayers)
        return;
    m_hiddenLayers = hidden;
    emit contentChanged();
}

void VectorTileRenderer::setTheme(const QString& themeId)
{
    const QString id = MapTheme::theme(themeId).id();
    if (id == m_theme)
        return;
    m_theme = id;
    ++m_themeGeneration;

    // Restyle what is loaded from the decoded tiles, on the reader threads.
    for (const QString& key : m_tileCache.keys())
        restyleTile(key);
    emit contentChanged();
}

void VectorTileRenderer::completeRestyle(const QString& key, const CachedTile& tile, quint64 themeGeneration)
{
    if (themeGeneration != m_themeGeneration)
        return;
    auto it = m_tileCache.find(key);
    if (it == m_tileCache.end())
        return;
    releaseMesh(it->mesh);
    it->mesh = tile.mesh;
    it->labels = tile.labels;
    emit contentChanged();
}

QStringList VectorTileRenderer::seenLayers() const
{
    return QStringList(m_seenLayers.cbegin(), m_seenLayers.cend());
}

void VectorTileRenderer::noteLayers(const MbTilesVectorTile& tile)
{
    bool grew = false;
    for (const MbTilesVectorLayer& layer : tile.layers) {
        if (!m_seenLayers.contains(layer.name)) {
            m_seenLayers.insert(layer.name);
            grew = true;
        }
    }
    if (grew)
        emit layersDiscovered();
}

bool VectorTileRenderer::isLayerDrawn(const QString& layer) const
{
    return layer.isEmpty() || !m_hiddenLayers.contains(layer);
}

bool VectorTileRenderer::hasVectorLayers() const
{
    for (const TmsLoader::TileSourceLayer& layer : m_layers) {
        if (layer.sourceType == TmsLoader::TileSourceLayer::SourceType::VectorMbTiles)
            return true;
    }
    return false;
}

void VectorTileRenderer::initializeGpuResources()
{
    if (m_gpuResourcesInitialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(&m_geometryProgram, QStringLiteral("vector_geometry.vert"),
                                  QStringLiteral("colored_line.frag"), &errorMessage)
        || !ShaderUtils::loadProgram(&m_screenProgram, QStringLiteral("colored_2d.vert"),
                                     QStringLiteral("colored_line.frag"), &errorMessage)
        || !ShaderUtils::loadProgram(&m_lineProgram, QStringLiteral("vector_line.vert"),
                                     QStringLiteral("vector_line.frag"), &errorMessage)
        || !ShaderUtils::loadProgram(&m_compositeProgram, QStringLiteral("globe_composite.vert"),
                                     QStringLiteral("globe_composite.frag"), &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);

    static constexpr float quadCorners[] = { 0, -1, 1, -1, 1, 1, 0, -1, 1, 1, 0, 1 };
    f->glGenBuffers(1, &m_quadVbo);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    f->glBufferData(GL_ARRAY_BUFFER, sizeof(quadCorners), quadCorners, GL_STATIC_DRAW);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (!m_coverBuffer.initialize(GL_ARRAY_BUFFER) || !m_compositeBuffer.initialize(GL_ARRAY_BUFFER))
        return;
    f->glGenVertexArrays(1, &m_coverVao);
    f->glGenVertexArrays(1, &m_compositeVao);
    m_gpuResourcesInitialized = true;
}

void VectorTileRenderer::releaseMesh(const std::shared_ptr<TileMesh>& mesh)
{
    if (!mesh)
        return;
    for (GLuint buffer : { mesh->vbo, mesh->lineVbo }) {
        if (buffer)
            m_releasedBuffers.append(buffer);
    }
    for (GLuint vao : { mesh->vao, mesh->lineVao }) {
        if (vao)
            m_releasedVertexArrays.append(vao);
    }
    mesh->vbo = mesh->lineVbo = mesh->vao = mesh->lineVao = 0;
    mesh->uploaded = false;
}

void VectorTileRenderer::deleteReleasedMeshes()
{
    if (m_releasedBuffers.isEmpty() && m_releasedVertexArrays.isEmpty())
        return;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (!f)
        return;
    if (!m_releasedBuffers.isEmpty())
        f->glDeleteBuffers(static_cast<GLsizei>(m_releasedBuffers.size()), m_releasedBuffers.constData());
    if (!m_releasedVertexArrays.isEmpty())
        f->glDeleteVertexArrays(static_cast<GLsizei>(m_releasedVertexArrays.size()), m_releasedVertexArrays.constData());
    m_releasedBuffers.clear();
    m_releasedVertexArrays.clear();
}

void VectorTileRenderer::clearCache()
{
    ++m_requestGeneration;
    ++m_themeGeneration;  // pending restyles are for tiles that are going
    for (const CachedTile& tile : std::as_const(m_tileCache))
        releaseMesh(tile.mesh);
    m_tileCache.clear();
    m_visibleKeys.clear();
    m_pendingTileReads.clear();
    m_retainedKeys.clear();
    m_labelHits.clear();
    if (m_wantedTiles)
        m_wantedTiles->clear();
}

QList<int> VectorTileRenderer::vectorLayerIndicesForZoom(int zoomLevel) const
{
    QList<int> indices;
    for (int i = 0; i < m_layers.size(); ++i) {
        const TmsLoader::TileSourceLayer& layer = m_layers.at(i);
        if (layer.sourceType != TmsLoader::TileSourceLayer::SourceType::VectorMbTiles)
            continue;

        // A negative bound means "not given".
        const int minZoom = layer.minZoom >= 0 ? layer.minZoom : 0;
        const int maxZoom = layer.maxZoom >= 0 ? layer.maxZoom : GIS::MAX_TILE_ZOOM;
        const bool displayZoomMatches = zoomLevel >= minZoom && zoomLevel <= maxZoom;
        const bool canUseAsLowerResolutionUnderlay =
            zoomLevel > maxZoom
            && zoomLevel > layer.maxTileZoom
            && layer.maxTileZoom >= layer.minTileZoom;

        if (displayZoomMatches || canUseAsLowerResolutionUnderlay)
            indices.append(i);
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
    if (minTileZoom > maxTileZoom)
        std::swap(minTileZoom, maxTileZoom);
    return qBound(minTileZoom, zoomLevel, maxTileZoom);
}

QString VectorTileRenderer::tileKey(int layerIndex, int z, int x, int y) const
{
    return QString("%1_%2_%3_%4").arg(layerIndex).arg(z).arg(x).arg(y);
}

void VectorTileRenderer::updateVisibleTiles()
{
    if (!m_enabled || !m_camera)
        return;

    const int cameraZoomLevel = qMax(0, m_camera->getTileZoomLevel() - kVectorTileZoomOffset);
    const QList<int> layerIndices = vectorLayerIndicesForZoom(cameraZoomLevel);
    QSet<QString> newVisibleKeys;

    constexpr qint64 maxTilesPerLayer = 2048;
    for (int layerIndex : layerIndices) {
        const int tileZoomLevel = tileZoomForLayer(cameraZoomLevel, layerIndex);
        const QRect tileRange = m_camera->getTileRange(tileZoomLevel);
        const qint64 requestedTileCount = static_cast<qint64>(tileRange.width()) * tileRange.height();
        if (tileRange.width() <= 0 || tileRange.height() <= 0 || requestedTileCount > maxTilesPerLayer) {
            qWarning() << "Skipping vector tile update with excessive tile range" << tileRange
                       << "at zoom" << tileZoomLevel << "for layer" << layerIndex << "count" << requestedTileCount;
            continue;
        }

        for (int x = tileRange.x(); x < tileRange.x() + tileRange.width(); ++x) {
            for (int y = tileRange.y(); y < tileRange.y() + tileRange.height(); ++y) {
                const QString key = tileKey(layerIndex, tileZoomLevel, x, y);
                newVisibleKeys.insert(key);
                if (m_tileCache.contains(key) || m_pendingTileReads.contains(key))
                    continue;
                fetchVectorTile(layerIndex, tileZoomLevel, x, y);
            }
        }
    }

    bool changed = false;
    if (newVisibleKeys != m_visibleKeys) {
        // Keep what was on screen until the new view's tiles are in.
        for (const QString& key : std::as_const(m_visibleKeys)) {
            if (!newVisibleKeys.contains(key) && m_tileCache.contains(key))
                m_retainedKeys.insert(key);
        }
        for (const QString& key : newVisibleKeys)
            m_retainedKeys.remove(key);
        if (m_retainedKeys.size() > kMaxRetainedTiles)
            m_retainedKeys.clear();
        m_visibleKeys = newVisibleKeys;
        changed = true;
    }

    // Reads for tiles no longer in view are dropped before they reach the file.
    QList<QString> staleReads;
    for (const QString& key : std::as_const(m_pendingTileReads)) {
        if (!m_visibleKeys.contains(key))
            staleReads.append(key);
    }
    for (const QString& key : staleReads) {
        m_pendingTileReads.remove(key);
        if (m_wantedTiles)
            m_wantedTiles->remove(key);
    }
    if (m_pendingTileReads.isEmpty() && !m_retainedKeys.isEmpty()) {
        m_retainedKeys.clear();
        changed = true;
    }

    QList<QString> keysToRemove;
    for (auto it = m_tileCache.cbegin(); it != m_tileCache.cend(); ++it) {
        if (!m_visibleKeys.contains(it.key()) && !m_retainedKeys.contains(it.key()))
            keysToRemove.append(it.key());
    }
    for (const QString& key : keysToRemove) {
        releaseMesh(m_tileCache.value(key).mesh);
        m_tileCache.remove(key);
        changed = true;
    }

    if (changed)
        emit contentChanged();
}

void VectorTileRenderer::fetchVectorTile(int layerIndex, int z, int x, int y)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size())
        return;

    const QString key = tileKey(layerIndex, z, x, y);
    if (m_tileCache.contains(key) || m_pendingTileReads.contains(key))
        return;

    const TmsLoader::TileSourceLayer layer = m_layers.at(layerIndex);
    if (layer.sourceType != TmsLoader::TileSourceLayer::SourceType::VectorMbTiles)
        return;

    m_pendingTileReads.insert(key);
    if (!m_wantedTiles)
        m_wantedTiles = std::make_shared<MbTilesRequestSet>();
    m_wantedTiles->insert(key);
    const std::shared_ptr<MbTilesRequestSet> wanted = m_wantedTiles;

    const int wrappedX = MercatorProjection::wrapTileX(x, z);
    const quint64 generation = m_requestGeneration;
    const quint64 themeGeneration = m_themeGeneration;
    const Mode mode = m_mode;
    const QString themeId = m_theme;
    QPointer<VectorTileRenderer> renderer(this);

    // Newest first; jobs for tiles that left the view return without reading. Decoding and the
    // tile's geometry are done here too, off the UI thread.
    MbTilesReader::threadPool()->start([renderer, wanted, key, layer, layerIndex, z, x, wrappedX, y, generation,
                                        themeGeneration, mode, themeId]() {
        if (!wanted->contains(key))
            return;
        QString errorMessage;
        CachedTile cachedTile;
        cachedTile.layerIndex = layerIndex;
        cachedTile.z = z;
        cachedTile.x = x;
        cachedTile.y = y;
        cachedTile.mercatorBounds = MercatorProjection::tileToMercatorBounds(z, x, y);
        cachedTile.tile = layer.mbTilesPath.isEmpty()
            ? MbTilesReader::readVectorTileFromUrl(layer.urlTemplate, z, wrappedX, y, layer.tmsYOrigin, &errorMessage)
            : MbTilesReader::readVectorTile(layer.mbTilesPath, z, wrappedX, y, layer.tmsYOrigin, &errorMessage);
        buildTileContent(&cachedTile, mode, themeId);

        if (!renderer)
            return;
        QMetaObject::invokeMethod(
            renderer.data(),
            [renderer, key, cachedTile, errorMessage, generation, themeGeneration]() {
                if (renderer)
                    renderer->completeVectorTileRead(key, cachedTile, errorMessage, generation, themeGeneration);
            },
            Qt::QueuedConnection);
    }, ++m_readPriority);
}

void VectorTileRenderer::buildTileContent(CachedTile* cachedTile, Mode mode, const QString& themeId)
{
    const MapTheme& theme = MapTheme::theme(themeId);
    const bool overlay = mode == Mode::Overlay;
    const int zoomLevel = cachedTile->z + kVectorTileZoomOffset;
    const QRectF& bounds = cachedTile->mercatorBounds;
    QMap<QPair<quint64, QString>, QVector<LineVertex>> fills;   // (draw order | colour, layer)
    QMap<QString, QVector<LineVertex>> hairlines;              // layer
    QMap<QPair<int, QString>, QVector<LineInstance>> lines;    // (draw order, layer)
    auto labels = std::make_shared<QVector<LabelCandidate>>();

    auto mercatorRing = [&bounds](const QVector<QPointF>& path, int size) {
        QVector<QPointF> ring;
        ring.reserve(size + 4);
        ring.append(tilePixelToMercator(path.at(0), bounds));
        for (int i = 1; i <= size; ++i)
            appendSubdivided(ring, ring.last(), tilePixelToMercator(path.at(i % size), bounds));
        ring.removeLast();  // back at the first point
        return ring;
    };

    if (!overlay) {
        // The map's land under everything: the tile's square as an area, edges split for the globe.
        const QVector<QPointF> square = { QPointF(0, 0), QPointF(GIS::TILE_SIZE, 0),
                                          QPointF(GIS::TILE_SIZE, GIS::TILE_SIZE), QPointF(0, GIS::TILE_SIZE) };
        appendWindingFan(fills[{ fillKey(0, theme.background()), QString() }], mercatorRing(square, 4),
                         theme.background());
    }

    for (int layerIndex = 0; layerIndex < cachedTile->tile.layers.size(); ++layerIndex) {
        const MbTilesVectorLayer& layer = cachedTile->tile.layers.at(layerIndex);
        const bool labelled = MapTheme::isLabelLayer(layer.name);

        for (int featureIndex = 0; featureIndex < layer.features.size(); ++featureIndex) {
            const MbTilesVectorFeature& feature = layer.features.at(featureIndex);
            if (feature.paths.isEmpty() || feature.paths.first().isEmpty())
                continue;

            if (labelled) {
                const QString text = featureLabel(layer.name, feature);
                if (!text.isEmpty()) {
                    LabelCandidate candidate;
                    candidate.text = text;
                    candidate.layer = layer.name;
                    candidate.klass = tagString(feature, "class");
                    bool rankOk = false;
                    candidate.rank = feature.tags.value(QStringLiteral("rank")).toInt(&rankOk);
                    if (!rankOk)
                        candidate.rank = -1;
                    candidate.layerIndex = layerIndex;
                    candidate.featureIndex = featureIndex;
                    if (feature.type == 2) {
                        // The longest straight piece inside the tile carries the name.
                        double best = 0.0;
                        for (const QVector<QPointF>& path : feature.paths) {
                            for (int i = 1; i < path.size(); ++i) {
                                const double length = std::hypot(path.at(i).x() - path.at(i - 1).x(),
                                                                 path.at(i).y() - path.at(i - 1).y());
                                if (length > best && !outsideTileEdge(path.at(i - 1), path.at(i))) {
                                    best = length;
                                    candidate.lineFrom = tilePixelToMercator(path.at(i - 1), bounds);
                                    candidate.lineTo = tilePixelToMercator(path.at(i), bounds);
                                }
                            }
                        }
                        if (best <= 0.0)
                            continue;
                        candidate.hasLine = true;
                        candidate.mercator = (candidate.lineFrom + candidate.lineTo) * 0.5;
                    }
                    else if (feature.type == 1) {
                        const QPointF point = feature.paths.first().first();
                        if (point.x() < 0 || point.y() < 0 || point.x() > GIS::TILE_SIZE || point.y() > GIS::TILE_SIZE)
                            continue;  // in the buffer: the neighbouring tile has it
                        candidate.mercator = tilePixelToMercator(point, bounds);
                    }
                    else {
                        QPointF sum;
                        const QVector<QPointF>& ring = feature.paths.first();
                        for (const QPointF& point : ring)
                            sum += point;
                        candidate.mercator = tilePixelToMercator(sum / ring.size(), bounds);
                    }
                    labels->append(candidate);
                }
            }

            const MapTheme::FeatureStyle style = theme.featureStyle(layer.name, feature.tags, feature.type, zoomLevel, overlay);
            if (style.fill.alpha() > 0 && feature.type == 3) {
                QVector<LineVertex>& vertices = fills[{ fillKey(style.fillOrder, style.fill), layer.name }];
                for (const QVector<QPointF>& path : feature.paths) {
                    const int pointCount = openRingSize(path);
                    if (pointCount < 3 || std::abs(signedRingArea(path, pointCount)) <= 1.0e-6)
                        continue;
                    appendWindingFan(vertices, mercatorRing(path, pointCount), style.fill);
                }
            }
            if (style.outline.alpha() > 0 && feature.type == 3) {
                QVector<LineVertex>& vertices = hairlines[layer.name];
                for (const QVector<QPointF>& path : feature.paths) {
                    for (int i = 1; i < path.size(); ++i) {
                        if (outsideTileEdge(path.at(i - 1), path.at(i)))
                            continue;
                        QVector<QPointF> pieces{ tilePixelToMercator(path.at(i - 1), bounds) };
                        appendSubdivided(pieces, pieces.first(), tilePixelToMercator(path.at(i), bounds));
                        for (int p = 1; p < pieces.size(); ++p) {
                            vertices.append(coloredVertex(pieces.at(p - 1), style.outline));
                            vertices.append(coloredVertex(pieces.at(p), style.outline));
                        }
                    }
                }
            }
            if (feature.type == 2 || feature.type == 3) {
                for (const MapTheme::Line* line : { &style.casing, &style.line, &style.overline }) {
                    if (!line->isVisible())
                        continue;
                    QVector<LineInstance>& instances = lines[{ line->order, layer.name }];
                    for (const QVector<QPointF>& path : feature.paths)
                        appendLine(instances, path, bounds, *line);
                }
            }
        }
    }

    auto mesh = std::make_shared<TileMesh>();
    for (auto it = fills.cbegin(); it != fills.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        mesh->fills.append({ it.key().first, it.key().second, int(mesh->vertices.size()), int(it.value().size()) });
        mesh->vertices += it.value();
    }
    for (auto it = hairlines.cbegin(); it != hairlines.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        mesh->hairlines.append({ 0, it.key(), int(mesh->vertices.size()), int(it.value().size()) });
        mesh->vertices += it.value();
    }
    for (auto it = lines.cbegin(); it != lines.cend(); ++it) {
        if (it.value().isEmpty())
            continue;
        mesh->lineRanges.append({ quint64(it.key().first), it.key().second, int(mesh->lines.size()), int(it.value().size()) });
        mesh->lines += it.value();
    }

    cachedTile->mesh = mesh;
    cachedTile->labels = labels;
}

void VectorTileRenderer::completeVectorTileRead(
    const QString& key,
    const CachedTile& tile,
    const QString& errorMessage,
    quint64 generation,
    quint64 themeGeneration)
{
    if (generation != m_requestGeneration || !m_pendingTileReads.contains(key))
        return;

    m_pendingTileReads.remove(key);
    if (m_wantedTiles)
        m_wantedTiles->remove(key);
    if (m_pendingTileReads.isEmpty() && !m_retainedKeys.isEmpty()) {
        // The new view is complete: let go of the old one.
        for (const QString& retainedKey : std::as_const(m_retainedKeys)) {
            releaseMesh(m_tileCache.value(retainedKey).mesh);
            m_tileCache.remove(retainedKey);
        }
        m_retainedKeys.clear();
    }

    if (!errorMessage.isEmpty() && !tile.tile.isEmpty())
        qDebug() << errorMessage;
    else if (!errorMessage.isEmpty())
        qWarning() << errorMessage;

    if (!m_visibleKeys.contains(key))
        return;

    m_tileCache.insert(key, tile);
    noteLayers(tile.tile);
    if (themeGeneration != m_themeGeneration)
        restyleTile(key);  // the theme changed while the tile was being read
    emit contentChanged();
}

void VectorTileRenderer::restyleTile(const QString& key)
{
    const auto it = m_tileCache.constFind(key);
    if (it == m_tileCache.cend())
        return;
    CachedTile tile = it.value();
    const quint64 themeGeneration = m_themeGeneration;
    const Mode mode = m_mode;
    const QString themeId = m_theme;
    QPointer<VectorTileRenderer> renderer(this);
    MbTilesReader::threadPool()->start([renderer, tile, key, mode, themeId, themeGeneration]() mutable {
        buildTileContent(&tile, mode, themeId);
        if (!renderer)
            return;
        QMetaObject::invokeMethod(renderer.data(), [renderer, key, tile, themeGeneration]() {
            if (renderer)
                renderer->completeRestyle(key, tile, themeGeneration);
        }, Qt::QueuedConnection);
    }, ++m_readPriority);
}

QList<QString> VectorTileRenderer::drawOrderKeys() const
{
    // Retained tiles of the previous view first, so the new view's tiles draw over them.
    QList<QString> keys = m_retainedKeys.values();
    std::sort(keys.begin(), keys.end());
    QList<QString> visibleKeys = m_visibleKeys.values();
    std::sort(visibleKeys.begin(), visibleKeys.end());
    keys.append(visibleKeys);
    return keys;
}

void VectorTileRenderer::uploadMesh(TileMesh* mesh)
{
    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    FrameProfiler::Scope uploadScope(QStringLiteral("upload.vectorTileMesh"));
    FrameProfiler::recordCount(QStringLiteral("upload.vectorTileMesh.vertices"), mesh->vertices.size());
    FrameProfiler::recordCount(QStringLiteral("upload.vectorTileMesh.lines"), mesh->lines.size());

    if (!mesh->vertices.isEmpty()) {
        f->glGenVertexArrays(1, &mesh->vao);
        f->glGenBuffers(1, &mesh->vbo);
        f->glBindVertexArray(mesh->vao);
        f->glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
        f->glBufferData(GL_ARRAY_BUFFER, mesh->vertices.size() * qsizetype(sizeof(LineVertex)),
                        mesh->vertices.constData(), GL_STATIC_DRAW);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                                 reinterpret_cast<void*>(offsetof(LineVertex, position)));
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                                 reinterpret_cast<void*>(offsetof(LineVertex, color)));
    }
    if (!mesh->lines.isEmpty()) {
        f->glGenVertexArrays(1, &mesh->lineVao);
        f->glGenBuffers(1, &mesh->lineVbo);
        f->glBindVertexArray(mesh->lineVao);
        f->glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        f->glBindBuffer(GL_ARRAY_BUFFER, mesh->lineVbo);
        f->glBufferData(GL_ARRAY_BUFFER, mesh->lines.size() * qsizetype(sizeof(LineInstance)),
                        mesh->lines.constData(), GL_STATIC_DRAW);
        for (GLuint location = 1; location <= 4; ++location) {
            f->glEnableVertexAttribArray(location);
            f->glVertexAttribDivisor(location, 1);
        }
    }
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);
    // The GPU has them now; picking reads the decoded tile.
    mesh->vertices = QVector<LineVertex>();
    mesh->lines = QVector<LineInstance>();
    mesh->uploaded = true;
}

void VectorTileRenderer::setProjectionUniforms(QOpenGLShaderProgram* program, const Projection& projection) const
{
    program->setUniformValue("u_viewportSize", projection.viewport);
    program->setUniformValue("u_projection", projection.mode);
    program->setUniformValue("u_centerMercator", projection.centerMercator);
    program->setUniformValue("u_pixelsPerMercator", projection.pixelsPerMercator);
    program->setUniformValue("u_centerLatLon", projection.centerLatLon);
    program->setUniformValue("u_aeBounds", projection.aeBounds);
}

void VectorTileRenderer::drawGeometry(const Projection& projection)
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context->extraFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    struct TileDraw {
        const TileMesh* mesh;
        float zoomDelta;
        float tilePixelScale;
    };
    QVector<TileDraw> draws;
    const double cameraZoom = m_camera->getZoomLevel();
    for (const QString& key : drawOrderKeys()) {
        const auto it = m_tileCache.constFind(key);
        if (it == m_tileCache.cend() || !it->mesh)
            continue;
        TileMesh* mesh = it->mesh.get();
        if (!mesh->uploaded)
            uploadMesh(mesh);
        const QRectF& bounds = it->mercatorBounds;
        double tileWidthPx = bounds.width() * projection.pixelsPerMercator;
        if (projection.mode == 1) {
            const double lat = std::atan(std::sinh(bounds.center().y()));
            tileWidthPx = bounds.width() * std::cos(lat) * projection.aePixelsPerRadian;
        }
        draws.append({ mesh, float(cameraZoom - (it->z + kVectorTileZoomOffset)), float(tileWidthPx / GIS::TILE_SIZE) });
    }

    // Areas: one stencil pass per colour over every tile, then one cover (non-zero winding).
    QMap<quint64, QVector<QPair<const TileMesh*, const DrawRange*>>> batches;
    for (const TileDraw& draw : std::as_const(draws)) {
        for (const DrawRange& range : draw.mesh->fills) {
            if (isLayerDrawn(range.layer))
                batches[range.key].append({ draw.mesh, &range });
        }
    }
    if (!batches.isEmpty() && context->format().stencilBufferSize() > 0) {
        QVector<LineVertex> covers;
        covers.reserve(batches.size() * 6);
        for (auto it = batches.cbegin(); it != batches.cend(); ++it)
            covers += coverVertices(projection.viewport.x(), projection.viewport.y(), colorOfKey(it.key()));
        const StreamingBuffer::UploadResult upload = m_coverBuffer.upload(
            covers.constData(), covers.size() * static_cast<qsizetype>(sizeof(LineVertex)), GL_STREAM_DRAW);
        f->glBindVertexArray(m_coverVao);
        f->glBindBuffer(GL_ARRAY_BUFFER, m_coverBuffer.id());
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, color)));
        f->glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (upload.ok) {
            // The stencil starts at zero (the frame's clear) and each cover puts back zero
            // where it drew, so no clear between colours.
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xff);
            int coverIndex = 0;
            for (auto it = batches.cbegin(); it != batches.cend(); ++it, ++coverIndex) {
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glStencilFunc(GL_ALWAYS, 0, 0xff);
                // Non-zero winding fill: front faces add, back faces subtract.
                glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
                glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);

                m_geometryProgram.bind();
                setProjectionUniforms(&m_geometryProgram, projection);
                for (const auto& part : it.value()) {
                    f->glBindVertexArray(part.first->vao);
                    f->glDrawArrays(GL_TRIANGLES, part.second->first, part.second->count);
                    FrameProfiler::recordCount(QStringLiteral("draw.calls"));
                }

                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glStencilFunc(GL_NOTEQUAL, 0, 0xff);
                glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
                m_screenProgram.bind();
                m_screenProgram.setUniformValue("u_viewportSize", projection.viewport);
                f->glBindVertexArray(m_coverVao);
                f->glDrawArrays(GL_TRIANGLES, coverIndex * 6, 6);
                FrameProfiler::recordCount(QStringLiteral("draw.calls"));
            }
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glDisable(GL_STENCIL_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            m_screenProgram.release();
        }
    }
    else if (!batches.isEmpty()) {
        static bool warnedAboutStencil = false;
        if (!warnedAboutStencil) {
            qWarning() << "Vector MBTiles polygon fills require an OpenGL stencil buffer.";
            warnedAboutStencil = true;
        }
    }

    // Hairlines (building outlines, shores over imagery).
    m_geometryProgram.bind();
    setProjectionUniforms(&m_geometryProgram, projection);
    glLineWidth(1.0f);
    for (const TileDraw& draw : std::as_const(draws)) {
        for (const DrawRange& range : draw.mesh->hairlines) {
            if (!isLayerDrawn(range.layer))
                continue;
            f->glBindVertexArray(draw.mesh->vao);
            f->glDrawArrays(GL_LINES, range.first, range.count);
            FrameProfiler::recordCount(QStringLiteral("draw.calls"));
        }
    }
    m_geometryProgram.release();

    // Thick lines in draw order over all tiles: every casing under every road.
    QMap<quint64, QVector<QPair<const TileDraw*, const DrawRange*>>> lineBatches;
    for (const TileDraw& draw : std::as_const(draws)) {
        for (const DrawRange& range : draw.mesh->lineRanges) {
            if (isLayerDrawn(range.layer))
                lineBatches[range.key].append({ &draw, &range });
        }
    }
    if (!lineBatches.isEmpty()) {
        m_lineProgram.bind();
        setProjectionUniforms(&m_lineProgram, projection);
        const int zoomDeltaLocation = m_lineProgram.uniformLocation("u_zoomDelta");
        const int tileScaleLocation = m_lineProgram.uniformLocation("u_tilePixelScale");
        for (auto it = lineBatches.cbegin(); it != lineBatches.cend(); ++it) {
            for (const auto& part : it.value()) {
                const TileMesh* mesh = part.first->mesh;
                m_lineProgram.setUniformValue(zoomDeltaLocation, part.first->zoomDelta);
                m_lineProgram.setUniformValue(tileScaleLocation, part.first->tilePixelScale);
                f->glBindVertexArray(mesh->lineVao);
                f->glBindBuffer(GL_ARRAY_BUFFER, mesh->lineVbo);
                const qsizetype base = qsizetype(part.second->first) * qsizetype(sizeof(LineInstance));
                f->glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineInstance),
                                         reinterpret_cast<void*>(base + offsetof(LineInstance, segment)));
                f->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(LineInstance),
                                         reinterpret_cast<void*>(base + offsetof(LineInstance, width)));
                f->glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(LineInstance),
                                         reinterpret_cast<void*>(base + offsetof(LineInstance, dash)));
                f->glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(LineInstance),
                                         reinterpret_cast<void*>(base + offsetof(LineInstance, color)));
                f->glDrawArraysInstanced(GL_TRIANGLES, 0, 6, part.second->count);
                FrameProfiler::recordCount(QStringLiteral("draw.calls"));
                FrameProfiler::recordCount(QStringLiteral("draw.vectorLines.instances"), part.second->count);
            }
        }
        f->glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_lineProgram.release();
    }

    f->glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

void VectorTileRenderer::renderGlobe()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context->extraFunctions();
    const double radius = m_camera->getOrthographicRadius();
    const double width = m_camera->getViewportWidth();
    const double height = m_camera->getViewportHeight();
    if (radius <= 0.0 || width <= 0.0 || height <= 0.0)
        return;

    // The part of the globe on screen, in orthographic unit coordinates, then the extent of
    // that part in the azimuthal equidistant projection (the texture to draw).
    const double u0 = qMax(-1.0, -width * 0.5 / radius);
    const double u1 = qMin(1.0, width * 0.5 / radius);
    const double v0 = qMax(-1.0, -height * 0.5 / radius);
    const double v1 = qMin(1.0, height * 0.5 / radius);
    double ax0 = std::numeric_limits<double>::max();
    double ay0 = ax0;
    double ax1 = -ax0;
    double ay1 = -ax0;
    auto include = [&](double u, double v) {
        double rho = std::hypot(u, v);
        if (rho > 1.0) {
            u /= rho;
            v /= rho;
            rho = 1.0;
        }
        const double k = rho > 1e-12 ? std::asin(rho) / rho : 1.0;
        ax0 = qMin(ax0, u * k);
        ax1 = qMax(ax1, u * k);
        ay0 = qMin(ay0, v * k);
        ay1 = qMax(ay1, v * k);
    };
    constexpr int samples = 32;
    for (int i = 0; i <= samples; ++i) {
        const double t = double(i) / samples;
        include(u0 + (u1 - u0) * t, v0);
        include(u0 + (u1 - u0) * t, v1);
        include(u0, v0 + (v1 - v0) * t);
        include(u1, v0 + (v1 - v0) * t);
    }
    // The texture must hold the whole visible area: where the rim of the globe is on screen,
    // the extremes lie on the rim between the samples.
    const double padX = (ax1 - ax0) * 0.02;
    const double padY = (ay1 - ay0) * 0.02;
    ax0 -= padX;
    ax1 += padX;
    ay0 -= padY;
    ay1 += padY;

    // About one texture pixel per screen pixel at the centre.
    const int limit = qMin<int>(kMaxGlobeTexture, m_maxTextureSize);
    const int neededWidth = qBound(16, int(std::ceil((ax1 - ax0) * radius)), limit);
    const int neededHeight = qBound(16, int(std::ceil((ay1 - ay0) * radius)), limit);
    if (!m_globeTarget || m_globeTarget->width() < neededWidth || m_globeTarget->height() < neededHeight
        || m_globeTarget->width() > 2 * neededWidth + 512 || m_globeTarget->height() > 2 * neededHeight + 512) {
        delete m_globeTarget;
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setInternalTextureFormat(GL_RGBA8);
        const int allocWidth = qMin(limit, (neededWidth + 255) / 256 * 256);
        const int allocHeight = qMin(limit, (neededHeight + 255) / 256 * 256);
        m_globeTarget = new QOpenGLFramebufferObject(allocWidth, allocHeight, format);
        f->glBindTexture(GL_TEXTURE_2D, m_globeTarget->texture());
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (!m_globeTarget->isValid())
        return;

    const QPointF centerLatLon = MercatorProjection::mercatorToLatLon(
        m_camera->getCenterMercator().x(), m_camera->getCenterMercator().y());
    Projection projection;
    projection.mode = 1;
    projection.viewport = QVector2D(neededWidth, neededHeight);
    projection.centerLatLon = QVector2D(float(qDegreesToRadians(centerLatLon.x())), float(qDegreesToRadians(centerLatLon.y())));
    projection.aeBounds = QVector4D(float(ax0), float(ay0), float(ax1), float(ay1));
    projection.aePixelsPerRadian = neededWidth / (ax1 - ax0);

    // Draw the map into the texture: premultiplied colour, so it composites exactly. Cleared
    // with glClearBuffer (no clear-colour state to save) and no glGet: on threaded drivers a
    // glGet waits for the GPU queue to drain. The base map's texture starts as water: the
    // Mercator data stops at 85 degrees, beyond is the polar sea.
    m_globeTarget->bind();
    glViewport(0, 0, neededWidth, neededHeight);
    const QColor water = MapTheme::theme(m_theme).water();
    const GLfloat clearColor[4] = { m_mode == Mode::BaseMap ? float(water.redF()) : 0.0f,
                                    m_mode == Mode::BaseMap ? float(water.greenF()) : 0.0f,
                                    m_mode == Mode::BaseMap ? float(water.blueF()) : 0.0f,
                                    m_mode == Mode::BaseMap ? 1.0f : 0.0f };
    glStencilMask(0xff);
    f->glClearBufferfv(GL_COLOR, 0, clearColor);
    f->glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0);
    glEnable(GL_BLEND);
    f->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    drawGeometry(projection);
    f->glBindFramebuffer(GL_FRAMEBUFFER, context->defaultFramebufferObject());
    glViewport(0, 0, m_targetSize.width(), m_targetSize.height());

    // Onto the globe: a quad over the globe's square on screen.
    const float cx = float(width * 0.5);
    const float cy = float(height * 0.5);
    const float r = float(radius);
    const float left = qMax(0.0f, cx - r);
    const float right = qMin(float(width), cx + r);
    const float top = qMax(0.0f, cy - r);
    const float bottom = qMin(float(height), cy + r);
    const float quad[] = { left, top, right, top, right, bottom, left, top, right, bottom, left, bottom };
    m_compositeBuffer.upload(quad, sizeof(quad), GL_STREAM_DRAW);
    f->glBindVertexArray(m_compositeVao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_compositeBuffer.id());
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_compositeProgram.bind();
    m_compositeProgram.setUniformValue("u_viewportSize", QVector2D(float(width), float(height)));
    m_compositeProgram.setUniformValue("u_globeCenter", QVector2D(cx, cy));
    m_compositeProgram.setUniformValue("u_radius", r);
    m_compositeProgram.setUniformValue("u_aeBounds", projection.aeBounds);
    m_compositeProgram.setUniformValue("u_texScale",
        QVector2D(float(neededWidth) / m_globeTarget->width(), float(neededHeight) / m_globeTarget->height()));
    m_compositeProgram.setUniformValue("u_texture", 0);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, m_globeTarget->texture());
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    f->glDrawArrays(GL_TRIANGLES, 0, 6);
    FrameProfiler::recordCount(QStringLiteral("draw.calls"));
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    f->glBindVertexArray(0);
    m_compositeProgram.release();
}

void VectorTileRenderer::render()
{
    // Buffers of tiles dropped while disabled are freed here too (the context is current).
    deleteReleasedMeshes();
    if (!m_enabled || !m_camera || !hasVectorLayers())
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized)
        return;

    updateVisibleTiles();
    if (m_camera->isOrthographic()) {
        renderGlobe();
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Projection projection;
    projection.mode = 0;
    projection.viewport = QVector2D(float(m_camera->getViewportWidth()), float(m_camera->getViewportHeight()));
    projection.centerMercator = QVector2D(float(m_camera->getCenterMercator().x()), float(m_camera->getCenterMercator().y()));
    projection.pixelsPerMercator = float(GIS::EARTH_RADIUS / m_camera->getResolution());
    drawGeometry(projection);
}

QFont VectorTileRenderer::labelFont(const MapTheme::LabelStyle& style)
{
    const quint64 key = quint64(qRound(style.size * 10.0f)) | (quint64(style.weight) << 16) | (quint64(style.italic) << 28)
        | (quint64(qRound(style.letterSpacing * 10.0f)) << 32);
    auto it = m_fonts.find(key);
    if (it != m_fonts.end())
        return it.value();
    QFont font = QApplication::font();
    font.setPixelSize(qMax(6, qRound(style.size)));
    font.setWeight(QFont::Weight(style.weight));
    font.setItalic(style.italic);
    if (style.letterSpacing > 0.0f)
        font.setLetterSpacing(QFont::AbsoluteSpacing, style.letterSpacing);
    m_fonts.insert(key, font);
    return font;
}

void VectorTileRenderer::appendLabels(QVector<TextRenderer::Label>& labels)
{
    m_labelHits.clear();
    if (!m_enabled || !m_camera || !hasVectorLayers())
        return;

    const MapTheme& theme = MapTheme::theme(m_theme);
    const bool overlay = m_mode == Mode::Overlay;
    const int zoomLevel = qBound(0, int(std::floor(m_camera->getZoomLevel() + 0.25)), 22);
    const quint64 stamp = (m_themeGeneration << 8) | quint64(zoomLevel);
    const int maxLabels = zoomLevel < 8 ? 150 : (zoomLevel < 12 ? 300 : (zoomLevel < 15 ? 450 : 700));
    const int width = m_camera->getViewportWidth();
    const int height = m_camera->getViewportHeight();

    struct Entry {
        const LabelCandidate* candidate;
        const QString* tileKey;
    };
    QVector<Entry> entries;
    QList<QString> keys = m_visibleKeys.values();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        const auto tileIt = m_tileCache.constFind(key);
        if (tileIt == m_tileCache.cend() || !tileIt->labels)
            continue;
        for (const LabelCandidate& candidate : *tileIt->labels) {
            if (!isLayerDrawn(candidate.layer))
                continue;
            if (candidate.stamp != stamp) {
                candidate.stamp = stamp;
                candidate.style = theme.labelStyle(candidate.layer, candidate.klass, candidate.rank, zoomLevel, overlay);
                if (candidate.style.visible) {
                    candidate.shownText = candidate.style.uppercase ? candidate.text.toUpper() : candidate.text;
                    const QFontMetricsF metrics(labelFont(candidate.style));
                    candidate.textSize = QSizeF(metrics.horizontalAdvance(candidate.shownText), metrics.height());
                }
            }
            if (candidate.style.visible)
                entries.append({ &candidate, &tileIt.key() });
        }
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.candidate->style.priority < b.candidate->style.priority;
    });

    // Collision test against a coarse grid of the labels already placed.
    constexpr int kCell = 64;
    const int columns = width / kCell + 4;
    const int rows = height / kCell + 4;
    QVector<QVector<QRectF>> grid(columns * rows);
    auto cellRange = [&](const QRectF& rect, int* x0, int* y0, int* x1, int* y1) {
        *x0 = qBound(0, int(rect.left() + 2 * kCell) / kCell, columns - 1);
        *y0 = qBound(0, int(rect.top() + 2 * kCell) / kCell, rows - 1);
        *x1 = qBound(0, int(rect.right() + 2 * kCell) / kCell, columns - 1);
        *y1 = qBound(0, int(rect.bottom() + 2 * kCell) / kCell, rows - 1);
    };

    int placed = 0;
    for (const Entry& entry : std::as_const(entries)) {
        if (placed >= maxLabels)
            break;
        const LabelCandidate& candidate = *entry.candidate;
        const MapTheme::LabelStyle& style = candidate.style;

        QPointF screen;
        if (!m_camera->projectMercatorToScreen(candidate.mercator, &screen))
            continue;
        if (screen.x() < -100.0 || screen.x() > width + 100.0 || screen.y() < -40.0 || screen.y() > height + 40.0)
            continue;

        const qreal halo = style.halo.alpha() > 0 ? style.haloWidth : 0.0;
        const qreal markerSize = style.marker != MapTheme::Marker::None ? qMax<qreal>(7.0, style.size * 0.75) : 0.0;
        const qreal boxWidth = candidate.textSize.width() + 2.0 * halo + 2.0 + (markerSize > 0.0 ? markerSize + 4.0 : 0.0);
        const qreal boxHeight = candidate.textSize.height() + 2.0 * halo;
        qreal angle = 0.0;
        if (style.alongLine && candidate.hasLine) {
            QPointF from;
            QPointF to;
            if (!m_camera->projectMercatorToScreen(candidate.lineFrom, &from) || !m_camera->projectMercatorToScreen(candidate.lineTo, &to))
                continue;
            const QPointF along = to - from;
            const double length = std::hypot(along.x(), along.y());
            if (length < boxWidth + 6.0)
                continue;  // the piece of road is shorter than its name here
            angle = qRadiansToDegrees(std::atan2(along.y(), along.x()));
            if (angle > 90.0)
                angle -= 180.0;
            else if (angle < -90.0)
                angle += 180.0;
            screen = (from + to) * 0.5;
        }

        QRectF rect(0, 0, boxWidth, boxHeight);
        if (markerSize > 0.0)
            rect.moveTopLeft(QPointF(screen.x() - halo - markerSize * 0.5 - 0.5, screen.y() - boxHeight * 0.5));
        else
            rect.moveCenter(screen);
        // Rotated names: collide with their bounding box.
        QRectF collision = rect;
        if (angle != 0.0) {
            const double radians = qDegreesToRadians(angle);
            const double w = std::abs(boxWidth * std::cos(radians)) + std::abs(boxHeight * std::sin(radians));
            const double h = std::abs(boxWidth * std::sin(radians)) + std::abs(boxHeight * std::cos(radians));
            collision = QRectF(0, 0, w, h);
            collision.moveCenter(screen);
        }
        collision.adjust(-3, -2, 3, 2);

        int x0, y0, x1, y1;
        cellRange(collision, &x0, &y0, &x1, &y1);
        bool collides = false;
        for (int y = y0; y <= y1 && !collides; ++y) {
            for (int x = x0; x <= x1 && !collides; ++x) {
                for (const QRectF& occupied : std::as_const(grid[y * columns + x])) {
                    if (occupied.intersects(collision)) {
                        collides = true;
                        break;
                    }
                }
            }
        }
        if (collides)
            continue;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x)
                grid[y * columns + x].append(collision);
        }

        TextRenderer::Label label;
        label.text = candidate.shownText;
        label.rect = rect;
        label.font = labelFont(style);
        label.textColor = style.color;
        label.backgroundColor = QColor(0, 0, 0, 0);
        label.haloColor = style.halo;
        label.haloWidth = halo;
        label.angle = angle;
        label.alignment = markerSize > 0.0 ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter;
        if (markerSize > 0.0) {
            label.marker = style.marker == MapTheme::Marker::Circle ? TextRenderer::Marker::Circle
                : (style.marker == MapTheme::Marker::Triangle ? TextRenderer::Marker::Triangle : TextRenderer::Marker::Square);
            label.markerColor = style.markerColor;
            label.markerSize = markerSize;
        }
        labels.append(label);
        m_labelHits.append({ collision, *entry.tileKey, candidate.layerIndex, candidate.featureIndex });
        ++placed;
    }
}

VectorTileRenderer::PickedFeature VectorTileRenderer::pick(const QPointF& screenPos) const
{
    PickedFeature picked;
    if (!m_enabled || !m_camera || !hasVectorLayers())
        return picked;

    QString bestKey;
    int bestLayer = -1;
    int bestFeature = -1;

    // Names first: what the user sees.
    for (auto it = m_labelHits.crbegin(); it != m_labelHits.crend(); ++it) {
        if (it->rect.contains(screenPos)) {
            bestKey = it->tileKey;
            bestLayer = it->layerIndex;
            bestFeature = it->featureIndex;
            break;
        }
    }

    const QPointF mercator = m_camera->screenToMercator(screenPos);
    QPointF check;
    if (bestFeature < 0 && m_camera->projectMercatorToScreen(mercator, &check)
        && (check - screenPos).manhattanLength() <= 2.0) {
        double bestScore = std::numeric_limits<double>::max();
        for (const QString& key : m_visibleKeys) {
            const auto tileIt = m_tileCache.constFind(key);
            if (tileIt == m_tileCache.cend())
                continue;
            const CachedTile& tile = tileIt.value();
            if (!tile.mercatorBounds.normalized().contains(mercator))
                continue;

            // Screen pixels per tile pixel near the click.
            QPointF a;
            QPointF b;
            m_camera->projectMercatorToScreen(mercator, &a);
            m_camera->projectMercatorToScreen(mercator + QPointF(tile.mercatorBounds.width() / GIS::TILE_SIZE, 0.0), &b);
            const double screenPerTilePixel = qMax(1e-6, std::hypot(b.x() - a.x(), b.y() - a.y()));
            const QPointF local = mercatorToTilePixel(mercator, tile.mercatorBounds);

            for (int layerIndex = 0; layerIndex < tile.tile.layers.size(); ++layerIndex) {
                const MbTilesVectorLayer& layer = tile.tile.layers.at(layerIndex);
                if (!isLayerDrawn(layer.name))
                    continue;
                const bool areaLayer = MapFeatures::isAreaLayer(layer.name) && layer.name != QLatin1String("building");
                if (areaLayer && m_mode == Mode::Overlay)
                    continue;

                for (int featureIndex = 0; featureIndex < layer.features.size(); ++featureIndex) {
                    const MbTilesVectorFeature& feature = layer.features.at(featureIndex);
                    // The land cover under everything is only worth a card when it has a name.
                    if (areaLayer && featureLabel(layer.name, feature).isEmpty())
                        continue;

                    double score = std::numeric_limits<double>::max();
                    if (feature.type == 1) {
                        for (const QVector<QPointF>& path : feature.paths) {
                            for (const QPointF& point : path) {
                                const double d = std::hypot(point.x() - local.x(), point.y() - local.y()) * screenPerTilePixel;
                                if (d <= kPickPointRadius)
                                    score = qMin(score, d);
                            }
                        }
                    }
                    else if (feature.type == 2) {
                        for (const QVector<QPointF>& path : feature.paths) {
                            for (int i = 1; i < path.size(); ++i) {
                                const double d = distanceToSegment(local, path.at(i - 1), path.at(i)) * screenPerTilePixel;
                                if (d <= kPickLineDistance)
                                    score = qMin(score, 100.0 + d);
                            }
                        }
                    }
                    else if (feature.type == 3 && insidePolygon(local, feature.paths)) {
                        double area = 0.0;
                        for (const QVector<QPointF>& path : feature.paths)
                            area += std::abs(signedRingArea(path, openRingSize(path)));
                        // Smaller areas win (a building over the park it stands in).
                        score = 1000.0 + area + (areaLayer ? 1.0e9 : 0.0);
                    }
                    if (score < bestScore) {
                        bestScore = score;
                        bestKey = key;
                        bestLayer = layerIndex;
                        bestFeature = featureIndex;
                    }
                }
            }
        }
    }

    const auto tileIt = m_tileCache.constFind(bestKey);
    if (bestFeature < 0 || tileIt == m_tileCache.cend() || bestLayer >= tileIt->tile.layers.size())
        return picked;
    const MbTilesVectorLayer& layer = tileIt->tile.layers.at(bestLayer);
    if (bestFeature >= layer.features.size())
        return picked;
    const MbTilesVectorFeature& feature = layer.features.at(bestFeature);

    picked.valid = true;
    picked.layerName = layer.name;
    picked.geometryType = feature.type;
    picked.id = feature.id;
    picked.tags = feature.tags;
    picked.mercatorAnchor = mercator;

    // Every visible piece of the same object (same layer and id), or just this one.
    auto appendPaths = [&picked](const MbTilesVectorFeature& piece, const QRectF& bounds) {
        for (const QVector<QPointF>& path : piece.paths) {
            QVector<QPointF> mercatorPath;
            mercatorPath.reserve(path.size());
            for (const QPointF& point : path)
                mercatorPath.append(tilePixelToMercator(point, bounds));
            picked.mercatorPaths.append(mercatorPath);
        }
    };
    if (feature.id == 0) {
        appendPaths(feature, tileIt->mercatorBounds);
        return picked;
    }
    for (const QString& key : m_visibleKeys) {
        const auto other = m_tileCache.constFind(key);
        if (other == m_tileCache.cend())
            continue;
        for (const MbTilesVectorLayer& otherLayer : other->tile.layers) {
            if (otherLayer.name != layer.name)
                continue;
            for (const MbTilesVectorFeature& piece : otherLayer.features) {
                if (piece.id == feature.id && piece.type == feature.type)
                    appendPaths(piece, other->mercatorBounds);
            }
        }
    }
    return picked;
}
