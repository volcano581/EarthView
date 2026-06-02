#include "TerrainRenderer.h"
#include "Camera.h"
#include "Constants.h"
#include "DemLoader.h"
#include "FrameProfiler.h"
#include "ShaderUtils.h"

#include <QDebug>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QVector2D>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {
constexpr int kTerrainSubdivisions = 64;

bool hasUsableBounds(const QRectF& bounds)
{
    return std::isfinite(bounds.left())
        && std::isfinite(bounds.right())
        && std::isfinite(bounds.top())
        && std::isfinite(bounds.bottom())
        && bounds.left() != bounds.right()
        && bounds.top() != bounds.bottom();
}

bool boundsIntersect(const QRectF& a, const QRectF& b)
{
    const double aLeft = qMin(a.left(), a.right());
    const double aRight = qMax(a.left(), a.right());
    const double aBottom = qMin(a.top(), a.bottom());
    const double aTop = qMax(a.top(), a.bottom());
    const double bLeft = qMin(b.left(), b.right());
    const double bRight = qMax(b.left(), b.right());
    const double bBottom = qMin(b.top(), b.bottom());
    const double bTop = qMax(b.top(), b.bottom());
    return aLeft <= bRight && aRight >= bLeft && aBottom <= bTop && aTop >= bBottom;
}
}

TerrainRenderer::TerrainRenderer(Camera* camera, DemLoader* demLoader, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_demLoader(demLoader)
    , m_gpuResourcesInitialized(false)
    , m_enabled(true)
    , m_pitchDegrees(58.0f)
    , m_verticalExaggeration(4.0f)
    , m_drawWireframe(true)
{
    initializeOpenGLFunctions();
}

TerrainRenderer::~TerrainRenderer()
{
    if (!m_gpuResourcesInitialized)
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (!f)
        return;

    for (auto it = m_tileGeometry.begin(); it != m_tileGeometry.end(); ++it) {
        destroyTileGeometry(&it.value());
    }
    m_tileGeometry.clear();

    if (m_demLoader) {
        for (auto& tile : m_demLoader->activeTiles()) {
            if (tile.textureId != 0) {
                f->glDeleteTextures(1, &tile.textureId);
                tile.textureId = 0;
            }
        }
    }
}

void TerrainRenderer::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void TerrainRenderer::setPitchDegrees(float pitchDegrees)
{
    m_pitchDegrees = qBound(15.0f, pitchDegrees, 75.0f);
    if (m_camera) {
        m_camera->setTerrainPitchDegrees(m_pitchDegrees);
    }
}

void TerrainRenderer::setVerticalExaggeration(float exaggeration)
{
    m_verticalExaggeration = qBound(0.5f, exaggeration, 12.0f);
    if (m_camera) {
        m_camera->setTerrainVerticalExaggeration(m_verticalExaggeration);
    }
}

void TerrainRenderer::initializeGpuResources()
{
    if (m_gpuResourcesInitialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            &m_program,
            QStringLiteral("terrain_height.vert"),
            QStringLiteral("terrain_height.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    m_gpuResourcesInitialized = true;
}

GLuint TerrainRenderer::createElevationTexture(DemTile* tile)
{
    if (!tile)
        return 0;

    const bool hasElevationSamples =
        !tile->elevationSamples.isEmpty()
        && tile->elevationWidth > 0
        && tile->elevationHeight > 0
        && tile->elevationSamples.size() == tile->elevationWidth * tile->elevationHeight;

    QVector<float> fallbackSamples;
    const float* sampleData = nullptr;
    int width = tile->elevationWidth;
    int height = tile->elevationHeight;
    float minValue = tile->minElevation;
    float maxValue = tile->maxElevation;

    if (hasElevationSamples) {
        sampleData = tile->elevationSamples.constData();
    }
    else if (!tile->image.isNull()) {
        QImage source = tile->image;
        if (source.format() != QImage::Format_Grayscale16
            && source.format() != QImage::Format_Grayscale8
            && source.format() != QImage::Format_RGB32
            && source.format() != QImage::Format_ARGB32
            && source.format() != QImage::Format_RGBA8888) {
            source = tile->image.convertToFormat(QImage::Format_Grayscale16);
        }

        width = source.width();
        height = source.height();
        fallbackSamples.resize(width * height);
        minValue = std::numeric_limits<float>::max();
        maxValue = std::numeric_limits<float>::lowest();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float value = 0.0f;
                if (source.format() == QImage::Format_Grayscale16) {
                    const auto* row = reinterpret_cast<const quint16*>(source.constScanLine(y));
                    value = static_cast<float>(row[x]);
                }
                else if (source.format() == QImage::Format_Grayscale8) {
                    value = static_cast<float>(source.constScanLine(y)[x]);
                }
                else {
                    value = static_cast<float>(qGray(source.pixel(x, y)));
                }

                fallbackSamples[y * width + x] = value;
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
            }
        }
        sampleData = fallbackSamples.constData();
    }

    if (!sampleData || width <= 0 || height <= 0)
        return 0;

    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || minValue >= maxValue) {
        minValue = 0.0f;
        maxValue = 1.0f;
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,
        width,
        height,
        0,
        GL_RED,
        GL_FLOAT,
        sampleData);
    glBindTexture(GL_TEXTURE_2D, 0);

    tile->minElevation = minValue;
    tile->maxElevation = maxValue;
    return textureId;
}

bool TerrainRenderer::ensureTileGeometry(const QString& key, const QRectF& bounds)
{
    auto it = m_tileGeometry.find(key);
    if (it != m_tileGeometry.end() && it->vertexCount > 0 && it->bounds == bounds)
        return true;

    if (it != m_tileGeometry.end()) {
        destroyTileGeometry(&it.value());
    }

    QVector<TerrainVertex> vertices;
    vertices.reserve(kTerrainSubdivisions * kTerrainSubdivisions * 6);
    appendTileMesh(&vertices, bounds);
    if (vertices.isEmpty())
        return false;

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    TileGeometry geometry;
    f->glGenVertexArrays(1, &geometry.vao);
    f->glGenBuffers(1, &geometry.vbo);
    f->glBindVertexArray(geometry.vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, geometry.vbo);
    f->glBufferData(
        GL_ARRAY_BUFFER,
        vertices.size() * static_cast<qsizetype>(sizeof(TerrainVertex)),
        vertices.constData(),
        GL_STATIC_DRAW);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TerrainVertex),
        reinterpret_cast<void*>(offsetof(TerrainVertex, mercator)));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TerrainVertex),
        reinterpret_cast<void*>(offsetof(TerrainVertex, texCoord)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);

    geometry.vertexCount = static_cast<GLsizei>(vertices.size());
    geometry.bounds = bounds;
    m_tileGeometry.insert(key, geometry);
    FrameProfiler::recordCount(QStringLiteral("terrain.meshBuilds"));
    FrameProfiler::recordCount(QStringLiteral("terrain.meshVertices"), vertices.size());
    return true;
}

void TerrainRenderer::destroyTileGeometry(TileGeometry* geometry)
{
    if (!geometry)
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (!f)
        return;

    if (geometry->vbo) {
        f->glDeleteBuffers(1, &geometry->vbo);
        geometry->vbo = 0;
    }
    if (geometry->vao) {
        f->glDeleteVertexArrays(1, &geometry->vao);
        geometry->vao = 0;
    }
    geometry->vertexCount = 0;
}

void TerrainRenderer::appendTileMesh(QVector<TerrainVertex>* vertices, const QRectF& bounds) const
{
    if (!vertices)
        return;

    const double left = bounds.left();
    const double right = bounds.right();
    const double top = bounds.top();
    const double bottom = bounds.bottom();

    auto appendVertex = [&](double mercatorX, double mercatorY, double u, double v) {
        TerrainVertex vertex;
        vertex.mercator[0] = static_cast<float>(mercatorX);
        vertex.mercator[1] = static_cast<float>(mercatorY);
        vertex.texCoord[0] = static_cast<float>(u);
        vertex.texCoord[1] = static_cast<float>(v);
        vertices->append(vertex);
    };

    for (int y = 0; y < kTerrainSubdivisions; ++y) {
        const double v0 = y / static_cast<double>(kTerrainSubdivisions);
        const double v1 = (y + 1) / static_cast<double>(kTerrainSubdivisions);
        const double mercatorY0 = top + (bottom - top) * v0;
        const double mercatorY1 = top + (bottom - top) * v1;
        for (int x = 0; x < kTerrainSubdivisions; ++x) {
            const double u0 = x / static_cast<double>(kTerrainSubdivisions);
            const double u1 = (x + 1) / static_cast<double>(kTerrainSubdivisions);
            const double mercatorX0 = left + (right - left) * u0;
            const double mercatorX1 = left + (right - left) * u1;

            appendVertex(mercatorX0, mercatorY0, u0, v0);
            appendVertex(mercatorX1, mercatorY0, u1, v0);
            appendVertex(mercatorX1, mercatorY1, u1, v1);
            appendVertex(mercatorX0, mercatorY0, u0, v0);
            appendVertex(mercatorX1, mercatorY1, u1, v1);
            appendVertex(mercatorX0, mercatorY1, u0, v1);
        }
    }
}

void TerrainRenderer::render()
{
    if (!m_enabled || !m_camera || !m_demLoader || !m_camera->isTerrain3DView())
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized || !m_program.isLinked())
        return;

    const QRectF visible = m_camera->getVisibleMercatorExtent();
    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_program.bind();
    m_program.setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));
    m_program.setUniformValue(
        "u_centerMercator",
        QVector2D(
            static_cast<float>(m_camera->getCenterMercator().x()),
            static_cast<float>(m_camera->getCenterMercator().y())));
    m_program.setUniformValue(
        "u_pixelsPerMercator",
        static_cast<float>(GIS::EARTH_RADIUS / m_camera->getResolution()));
    m_program.setUniformValue("u_pixelsPerMeter", static_cast<float>(1.0 / m_camera->getResolution()));
    m_program.setUniformValue("u_earthRadius", static_cast<float>(GIS::EARTH_RADIUS));
    m_program.setUniformValue("u_pitchRadians", qDegreesToRadians(m_camera->terrainPitchDegrees()));
    m_program.setUniformValue(
        "u_yawRadians",
        m_camera->isStealthViewEnabled() ? qDegreesToRadians(m_camera->cameraYawDegrees()) : 0.0f);
    m_program.setUniformValue("u_verticalExaggeration", m_camera->terrainVerticalExaggeration());
    m_program.setUniformValue(
        "u_screenAnchor",
        QVector2D(
            static_cast<float>(m_camera->terrainScreenAnchor().x()),
            static_cast<float>(m_camera->terrainScreenAnchor().y())));
    m_program.setUniformValue("u_focalPixels", static_cast<float>(m_camera->terrainFocalPixels()));
    m_program.setUniformValue("u_viewDistanceMeters", static_cast<float>(m_camera->terrainViewDistanceMeters()));
    m_program.setUniformValue("u_heightTexture", 0);
    m_program.setUniformValue("u_opacity", 1.0f);
    m_program.setUniformValue("u_wireMode", 0);

    f->glActiveTexture(GL_TEXTURE0);
    for (auto it = m_demLoader->activeTiles().begin(); it != m_demLoader->activeTiles().end(); ++it) {
        DemTile& tile = it.value();
        if (!tile.metadataReady || !tile.imageReady || !hasUsableBounds(tile.mercatorBounds))
            continue;
        if (!boundsIntersect(visible, tile.mercatorBounds))
            continue;

        if (tile.textureId == 0) {
            tile.textureId = createElevationTexture(&tile);
            tile.image = QImage();
            tile.elevationSamples.clear();
            tile.elevationSamples.squeeze();
        }
        if (tile.textureId == 0)
            continue;
        if (!ensureTileGeometry(it.key(), tile.mercatorBounds))
            continue;

        const TileGeometry& geometry = m_tileGeometry[it.key()];
        m_program.setUniformValue("u_minHeight", tile.minElevation);
        m_program.setUniformValue("u_maxHeight", tile.maxElevation);
        glBindTexture(GL_TEXTURE_2D, tile.textureId);
        f->glBindVertexArray(geometry.vao);
        f->glDrawArrays(GL_TRIANGLES, 0, geometry.vertexCount);
        FrameProfiler::recordCount(QStringLiteral("draw.calls"));
        FrameProfiler::recordCount(QStringLiteral("draw.terrain.vertices"), geometry.vertexCount);

        if (m_drawWireframe) {
            m_program.setUniformValue("u_wireMode", 1);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(1.0f);
            f->glDrawArrays(GL_TRIANGLES, 0, geometry.vertexCount);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            m_program.setUniformValue("u_wireMode", 0);
            FrameProfiler::recordCount(QStringLiteral("draw.calls"));
        }
    }

    f->glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_program.release();
}
