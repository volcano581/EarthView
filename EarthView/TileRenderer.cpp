#include "tilerenderer.h"
#include "camera.h"
#include "tmsloader.h"

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

    const auto& tiles = m_tileLoader->getActiveTiles();
    for (const auto& tile : tiles) {
        if (tile.textureId == 0 || tile.isLoading)
            continue;

        QPointF topLeft = m_camera->mercatorToScreen(tile.mercatorBounds.topLeft());
        QPointF bottomRight = m_camera->mercatorToScreen(tile.mercatorBounds.bottomRight());

        glBindTexture(GL_TEXTURE_2D, tile.textureId);

        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(topLeft.x(), topLeft.y());
        glTexCoord2f(1, 0); glVertex2f(bottomRight.x(), topLeft.y());
        glTexCoord2f(1, 1); glVertex2f(bottomRight.x(), bottomRight.y());
        glTexCoord2f(0, 1); glVertex2f(topLeft.x(), bottomRight.y());
        glEnd();
    }

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}