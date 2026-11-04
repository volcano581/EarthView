#include "TileRenderer.h"
#include "Camera.h"
#include "FrameProfiler.h"
#include "MercatorProjection.h"
#include "ShaderUtils.h"
#include "TMSLoader.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QVector4D>
#include <QtMath>
#include <QList>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QVector2D>
#include <algorithm>
#include <cstddef>

namespace {
// New textures per frame stop after this much time; the rest go up next frame, so a burst of
// tiles never costs one long frame.
constexpr qint64 kUploadBudgetNs = 4 * 1000 * 1000;

struct TileVertex {
    float position[2];
    float texCoord[2];
};

TileVertex tileVertex(const QPointF& position, float u, float v)
{
    return {
        { static_cast<float>(position.x()), static_cast<float>(position.y()) },
        { u, v }
    };
}

void appendQuad(
    QVector<TileVertex>& vertices,
    const QPointF& p00,
    const QPointF& p10,
    const QPointF& p11,
    const QPointF& p01,
    float u0,
    float v0,
    float u1,
    float v1)
{
    vertices.append(tileVertex(p00, u0, v0));
    vertices.append(tileVertex(p10, u1, v0));
    vertices.append(tileVertex(p11, u1, v1));
    vertices.append(tileVertex(p00, u0, v0));
    vertices.append(tileVertex(p11, u1, v1));
    vertices.append(tileVertex(p01, u0, v1));
}
}

TileRenderer::TileRenderer(Camera* camera, TmsLoader* tileLoader, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_tileLoader(tileLoader)
    , m_gridVao(0)
    , m_gridVbo(0)
    , m_gridVertexCount(0)
    , m_vao(0)
    , m_gpuResourcesInitialized(false)
{
    initializeOpenGLFunctions();
}

TileRenderer::~TileRenderer()
{
    if (!m_gpuResourcesInitialized)
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (f) {
        m_vertexBuffer.destroy();
        if (m_vao) {
            f->glDeleteVertexArrays(1, &m_vao);
        }
        if (m_gridVbo)
            f->glDeleteBuffers(1, &m_gridVbo);
        if (m_gridVao)
            f->glDeleteVertexArrays(1, &m_gridVao);
    }
}

void TileRenderer::initializeGpuResources()
{
    if (m_gpuResourcesInitialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            &m_tileProgram,
            QStringLiteral("textured_quad.vert"),
            QStringLiteral("textured_quad.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    if (!ShaderUtils::loadProgram(
            &m_globeProgram,
            QStringLiteral("textured_globe.vert"),
            QStringLiteral("textured_globe.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    f->glGenVertexArrays(1, &m_vao);
    if (!m_vertexBuffer.initialize(GL_ARRAY_BUFFER))
        return;

    setupVertexArray();

    // The globe grid: kGlobeGridCells^2 quads over the unit tile, built once.
    constexpr int kGlobeGridCells = 24;
    QVector<float> grid;
    grid.reserve(kGlobeGridCells * kGlobeGridCells * 12);
    for (int y = 0; y < kGlobeGridCells; ++y) {
        for (int x = 0; x < kGlobeGridCells; ++x) {
            const float u0 = float(x) / kGlobeGridCells;
            const float u1 = float(x + 1) / kGlobeGridCells;
            const float v0 = float(y) / kGlobeGridCells;
            const float v1 = float(y + 1) / kGlobeGridCells;
            grid << u0 << v0 << u1 << v0 << u1 << v1 << u0 << v0 << u1 << v1 << u0 << v1;
        }
    }
    m_gridVertexCount = GLsizei(grid.size() / 2);
    f->glGenVertexArrays(1, &m_gridVao);
    f->glGenBuffers(1, &m_gridVbo);
    f->glBindVertexArray(m_gridVao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    f->glBufferData(GL_ARRAY_BUFFER, grid.size() * qsizetype(sizeof(float)), grid.constData(), GL_STATIC_DRAW);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);

    m_gpuResourcesInitialized = true;
}

void TileRenderer::setupVertexArray()
{
    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindVertexArray(m_vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer.id());
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TileVertex),
        reinterpret_cast<void*>(offsetof(TileVertex, position)));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TileVertex),
        reinterpret_cast<void*>(offsetof(TileVertex, texCoord)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);
}

void TileRenderer::render()
{
    if (!m_camera || !m_tileLoader)
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized || !m_tileProgram.isLinked())
        return;

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindVertexArray(m_vao);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    f->glActiveTexture(GL_TEXTURE0);

    m_tileProgram.bind();
    m_tileProgram.setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));
    m_tileProgram.setUniformValue("u_texture", 0);

    auto byZoomThenLayer = [](const QMap<QString, TmsLoader::TileInfo>& tiles) {
        QList<QString> keys = tiles.keys();
        std::sort(keys.begin(), keys.end(), [&tiles](const QString& a, const QString& b) {
            const auto& tileA = tiles.find(a).value();
            const auto& tileB = tiles.find(b).value();
            if (tileA.tileZoomLevel != tileB.tileZoomLevel)
                return tileA.tileZoomLevel < tileB.tileZoomLevel;
            if (tileA.layerIndex != tileB.layerIndex)
                return tileA.layerIndex < tileB.layerIndex;
            return a < b;
        });
        return keys;
    };

    // The previous view's tiles first, under the current ones that have loaded.
    TextureManager* textures = m_tileLoader->getTextureManager();
    struct DrawItem {
        GLuint textureId;
        QRectF mercatorBounds;
    };
    QVector<DrawItem> drawItems;
    const QMap<QString, TmsLoader::TileInfo>& retained = m_tileLoader->getRetainedTiles();
    for (const QString& key : byZoomThenLayer(retained)) {
        const TmsLoader::TileInfo& tile = retained.find(key).value();
        // Evicted meanwhile: its id may belong to another tile now.
        if (textures->getTexture(tile.textureCacheKey) != tile.textureId)
            continue;
        drawItems.append({ tile.textureId, tile.mercatorBounds });
    }

    auto& tiles = m_tileLoader->getActiveTiles();
    QElapsedTimer uploadClock;
    uploadClock.start();
    bool uploaded = false;
    for (const QString& key : byZoomThenLayer(tiles)) {
        auto& tile = tiles.find(key).value();
        if (tile.isLoading)
            continue;

        const QString cacheKey = tile.textureCacheKey.isEmpty() ? key : tile.textureCacheKey;

        if (tile.textureId != 0 && textures->getTexture(cacheKey) != tile.textureId)
            tile.textureId = 0;  // Evicted: take it from the cache again or wait for a reload.
        if (tile.textureId == 0 && textures->hasTexture(cacheKey)) {
            tile.textureId = textures->getTexture(cacheKey);
        }

        if (tile.textureId == 0 && !tile.image.isNull() && uploadClock.nsecsElapsed() < kUploadBudgetNs) {
            FrameProfiler::Scope uploadScope(QStringLiteral("upload.rasterTile"));
            tile.textureId = textures->createTexture(tile.image, cacheKey);
            tile.image = QImage();
            uploaded = true;
        }

        if (tile.textureId == 0)
            continue;
        textures->touch(cacheKey);
        drawItems.append({ tile.textureId, tile.mercatorBounds });
    }
    if (uploaded)
        m_tileLoader->releaseRetainedTilesWhenLoaded();

    if (m_camera->isOrthographic()) {
        // Globe: the shared grid projected on the GPU, one draw per tile, no per-frame CPU work.
        m_tileProgram.release();
        if (!m_globeProgram.isLinked()) {
            f->glBindVertexArray(0);
            return;
        }
        const QPointF centerLatLon = MercatorProjection::mercatorToLatLon(
            m_camera->getCenterMercator().x(), m_camera->getCenterMercator().y());
        m_globeProgram.bind();
        m_globeProgram.setUniformValue("u_viewportSize",
            QVector2D(float(m_camera->getViewportWidth()), float(m_camera->getViewportHeight())));
        m_globeProgram.setUniformValue("u_centerLatLon",
            QVector2D(float(qDegreesToRadians(centerLatLon.x())), float(qDegreesToRadians(centerLatLon.y()))));
        m_globeProgram.setUniformValue("u_radius", float(m_camera->getOrthographicRadius()));
        m_globeProgram.setUniformValue("u_texture", 0);
        f->glBindVertexArray(m_gridVao);
        for (const DrawItem& tile : std::as_const(drawItems)) {
            glBindTexture(GL_TEXTURE_2D, tile.textureId);
            m_globeProgram.setUniformValue("u_tileBounds",
                QVector4D(float(tile.mercatorBounds.left()), float(tile.mercatorBounds.top()),
                          float(tile.mercatorBounds.right()), float(tile.mercatorBounds.bottom())));
            f->glDrawArrays(GL_TRIANGLES, 0, m_gridVertexCount);
            FrameProfiler::recordCount(QStringLiteral("draw.calls"));
            FrameProfiler::recordCount(QStringLiteral("draw.rasterTiles.tiles"));
        }
        m_globeProgram.release();
        f->glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
        return;
    }

    for (const DrawItem& tile : std::as_const(drawItems)) {
        glBindTexture(GL_TEXTURE_2D, tile.textureId);
        QVector<TileVertex> vertices;
        const QPointF topLeft = m_camera->mercatorToScreen(tile.mercatorBounds.topLeft());
        const QPointF bottomRight = m_camera->mercatorToScreen(tile.mercatorBounds.bottomRight());
        vertices.reserve(6);
        appendQuad(
            vertices,
            topLeft,
            QPointF(bottomRight.x(), topLeft.y()),
            bottomRight,
            QPointF(topLeft.x(), bottomRight.y()),
            0.0f,
            0.0f,
            1.0f,
            1.0f);

        const StreamingBuffer::UploadResult upload = m_vertexBuffer.upload(
            vertices.constData(),
            vertices.size() * static_cast<qsizetype>(sizeof(TileVertex)),
            GL_STREAM_DRAW);
        if (!upload.ok)
            continue;
        if (upload.bufferRecreated) {
            setupVertexArray();
            f->glBindVertexArray(m_vao);
        }
        f->glDrawArrays(GL_TRIANGLES, 0, vertices.size());
        FrameProfiler::recordCount(QStringLiteral("draw.calls"));
        FrameProfiler::recordCount(QStringLiteral("draw.rasterTiles.tiles"));
        FrameProfiler::recordCount(QStringLiteral("draw.rasterTiles.vertices"), vertices.size());
    }

    m_tileProgram.release();
    f->glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}
