#include "TileRenderer.h"
#include "Camera.h"
#include "ShaderUtils.h"
#include "TMSLoader.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QVector2D>
#include <cstddef>

namespace {
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
    , m_vbo(0)
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
        if (m_vbo) {
            f->glDeleteBuffers(1, &m_vbo);
        }
        if (m_vao) {
            f->glDeleteVertexArrays(1, &m_vao);
        }
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

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    f->glGenVertexArrays(1, &m_vao);
    f->glGenBuffers(1, &m_vbo);
    f->glBindVertexArray(m_vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
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

    m_gpuResourcesInitialized = true;
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

    auto& tiles = m_tileLoader->getActiveTiles();
    for (auto it = tiles.begin(); it != tiles.end(); ++it) {
        auto& tile = it.value();
        if (tile.isLoading)
            continue;

        const QString cacheKey = tile.textureCacheKey.isEmpty() ? it.key() : tile.textureCacheKey;

        if (tile.textureId == 0 && m_tileLoader->getTextureManager()->hasTexture(cacheKey)) {
            tile.textureId = m_tileLoader->getTextureManager()->getTexture(cacheKey);
        }

        if (tile.textureId == 0 && !tile.image.isNull()) {
            tile.textureId = m_tileLoader->getTextureManager()->createTexture(tile.image, cacheKey);
            tile.image = QImage();
        }

        if (tile.textureId == 0)
            continue;

        glBindTexture(GL_TEXTURE_2D, tile.textureId);
        QVector<TileVertex> vertices;

        if (!m_camera->isOrthographic()) {
            QPointF topLeft = m_camera->mercatorToScreen(tile.mercatorBounds.topLeft());
            QPointF bottomRight = m_camera->mercatorToScreen(tile.mercatorBounds.bottomRight());
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
        }
        else {
            const int subdivisions = 24;
            const double left = tile.mercatorBounds.left();
            const double right = tile.mercatorBounds.right();
            const double top = tile.mercatorBounds.top();
            const double bottom = tile.mercatorBounds.bottom();

            vertices.reserve(subdivisions * subdivisions * 6);
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
                    if (!m_camera->projectMercatorToScreen(QPointF(mercatorX0, mercatorY0), &p00)
                        || !m_camera->projectMercatorToScreen(QPointF(mercatorX1, mercatorY0), &p10)
                        || !m_camera->projectMercatorToScreen(QPointF(mercatorX1, mercatorY1), &p11)
                        || !m_camera->projectMercatorToScreen(QPointF(mercatorX0, mercatorY1), &p01)) {
                        continue;
                    }

                    appendQuad(
                        vertices,
                        p00,
                        p10,
                        p11,
                        p01,
                        static_cast<float>(u0),
                        static_cast<float>(v0),
                        static_cast<float>(u1),
                        static_cast<float>(v1));
                }
            }
        }

        if (vertices.isEmpty())
            continue;

        f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        f->glBufferData(
            GL_ARRAY_BUFFER,
            vertices.size() * static_cast<qsizetype>(sizeof(TileVertex)),
            vertices.constData(),
            GL_STREAM_DRAW);
        f->glDrawArrays(GL_TRIANGLES, 0, vertices.size());
    }

    m_tileProgram.release();
    f->glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}
