#include "TileRenderer.h"
#include "Camera.h"
#include "TMSLoader.h"

namespace {
void renderScreenQuad(const QPointF& topLeft, const QPointF& bottomRight)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(topLeft.x(), topLeft.y());
    glTexCoord2f(1, 0); glVertex2f(bottomRight.x(), topLeft.y());
    glTexCoord2f(1, 1); glVertex2f(bottomRight.x(), bottomRight.y());
    glTexCoord2f(0, 1); glVertex2f(topLeft.x(), bottomRight.y());
    glEnd();
}
}

TileRenderer::TileRenderer(Camera* camera, TmsLoader* tileLoader, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_tileLoader(tileLoader)
{
    initializeOpenGLFunctions();
}

void TileRenderer::render()
{
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

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

        if (!m_camera->isOrthographic()) {
            QPointF topLeft = m_camera->mercatorToScreen(tile.mercatorBounds.topLeft());
            QPointF bottomRight = m_camera->mercatorToScreen(tile.mercatorBounds.bottomRight());
            renderScreenQuad(topLeft, bottomRight);
            continue;
        }

        const int subdivisions = 24;
        const double left = tile.mercatorBounds.left();
        const double right = tile.mercatorBounds.right();
        const double top = tile.mercatorBounds.top();
        const double bottom = tile.mercatorBounds.bottom();

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

                glBegin(GL_QUADS);
                glTexCoord2f(u0, v0); glVertex2f(p00.x(), p00.y());
                glTexCoord2f(u1, v0); glVertex2f(p10.x(), p10.y());
                glTexCoord2f(u1, v1); glVertex2f(p11.x(), p11.y());
                glTexCoord2f(u0, v1); glVertex2f(p01.x(), p01.y());
                glEnd();
            }
        }
    }

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}
