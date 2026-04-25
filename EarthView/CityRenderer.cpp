#include "CityRenderer.h"
#include "Camera.h"
#include "Constants.h"
#include "ShaderUtils.h"
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QPainter>
#include <QPen>
#include <QVector2D>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
struct MarkerVertex {
    float position[2];
    float color[4];
    float size;
};

MarkerVertex markerVertex(const QPointF& screen, const QColor& color, float size)
{
    return {
        { static_cast<float>(screen.x()), static_cast<float>(screen.y()) },
        {
            static_cast<float>(color.redF()),
            static_cast<float>(color.greenF()),
            static_cast<float>(color.blueF()),
            static_cast<float>(color.alphaF())
        },
        size
    };
}

void visibleWorldCopyRange(const Camera* camera, int* firstCopy, int* lastCopy)
{
    if (!firstCopy || !lastCopy)
        return;

    *firstCopy = 0;
    *lastCopy = 0;

    if (!camera || camera->isOrthographic() || !camera->isHorizontalWrapEnabled())
        return;

    const double worldWidth = GIS::MAX_MERCATOR_X - GIS::MIN_MERCATOR_X;
    if (worldWidth <= 0.0)
        return;

    const QRectF extent = camera->getVisibleMercatorExtent();
    *firstCopy = static_cast<int>(std::ceil((extent.left() - GIS::MAX_MERCATOR_X) / worldWidth));
    *lastCopy = static_cast<int>(std::floor((extent.right() - GIS::MIN_MERCATOR_X) / worldWidth));

    if (*lastCopy < *firstCopy) {
        *firstCopy = 0;
        *lastCopy = 0;
    }
}
}

CityRenderer::CityRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_vbo(0)
    , m_vao(0)
    , m_gpuResourcesInitialized(false)
{
    initializeOpenGLFunctions();
}

CityRenderer::~CityRenderer()
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

void CityRenderer::initializeGpuResources()
{
    if (m_gpuResourcesInitialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            &m_markerProgram,
            QStringLiteral("point_marker.vert"),
            QStringLiteral("point_marker.frag"),
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
        sizeof(MarkerVertex),
        reinterpret_cast<void*>(offsetof(MarkerVertex, position)));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(MarkerVertex),
        reinterpret_cast<void*>(offsetof(MarkerVertex, color)));
    f->glEnableVertexAttribArray(2);
    f->glVertexAttribPointer(
        2,
        1,
        GL_FLOAT,
        GL_FALSE,
        sizeof(MarkerVertex),
        reinterpret_cast<void*>(offsetof(MarkerVertex, size)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);

    m_gpuResourcesInitialized = true;
}

bool CityRenderer::loadDirectory(const QString& rootPath, QString* errorMessage)
{
    return m_loader.discover(rootPath, errorMessage);
}

void CityRenderer::renderMarkers()
{
    if (!m_camera || !hasData())
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized || !m_markerProgram.isLinked())
        return;

    const double zoom = m_camera->getZoomLevel();
    const QList<int> layerIndices = m_loader.layerIndicesForZoom(zoom);
    if (layerIndices.isEmpty())
        return;

    for (int layerIndex : layerIndices) {
        QString loadMessage;
        if (!m_loader.ensureLayerLoaded(layerIndex, &loadMessage)) {
            qWarning() << loadMessage;
        }
        else if (!loadMessage.isEmpty()) {
            qDebug() << loadMessage;
        }
    }

    const QRectF extent = m_camera->getVisibleMercatorExtent();
    const double worldWidth = GIS::MAX_MERCATOR_X - GIS::MIN_MERCATOR_X;
    int firstCopy = 0;
    int lastCopy = 0;
    visibleWorldCopyRange(m_camera, &firstCopy, &lastCopy);

    QVector<MarkerVertex> vertices;
    vertices.reserve(maxLabelsForZoom(zoom) * 2);
    const int maxMarkers = maxLabelsForZoom(zoom) * 2;

    for (int layerIndex : layerIndices) {
        const CityLoader::CityLayer* layer = m_loader.layer(layerIndex);
        if (!layer || !layer->loaded)
            continue;

        const float markerDiameter = static_cast<float>(markerSizeForRank(layer->rank) * 2 + 2);
        const QColor markerColor = markerColorForRank(layer->rank);

        for (const CityLoader::CityPoint& point : layer->points) {
            if (vertices.size() >= maxMarkers)
                break;

            for (int copy = firstCopy; copy <= lastCopy && vertices.size() < maxMarkers; ++copy) {
                QPointF mercator = point.mercator;
                mercator.rx() += copy * worldWidth;
                if (!pointInExtent(mercator, extent))
                    continue;

                QPointF screen;
                if (!m_camera->projectMercatorToScreen(mercator, &screen))
                    continue;
                if (screen.x() < -80.0 || screen.x() > m_camera->getViewportWidth() + 80.0
                    || screen.y() < -30.0 || screen.y() > m_camera->getViewportHeight() + 30.0) {
                    continue;
                }

                vertices.append(markerVertex(screen, markerColor, markerDiameter));
            }
        }
    }

    if (vertices.isEmpty())
        return;

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);

    m_markerProgram.bind();
    m_markerProgram.setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));

    f->glBindVertexArray(m_vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    f->glBufferData(
        GL_ARRAY_BUFFER,
        vertices.size() * static_cast<qsizetype>(sizeof(MarkerVertex)),
        vertices.constData(),
        GL_STREAM_DRAW);
    f->glDrawArrays(GL_POINTS, 0, vertices.size());
    f->glBindVertexArray(0);
    m_markerProgram.release();
    glDisable(GL_PROGRAM_POINT_SIZE);
}

void CityRenderer::renderLabels(QPainter& painter)
{
    if (!m_camera || !hasData())
        return;

    const double zoom = m_camera->getZoomLevel();
    const QList<int> layerIndices = m_loader.layerIndicesForZoom(zoom);
    if (layerIndices.isEmpty())
        return;

    for (int layerIndex : layerIndices) {
        QString loadMessage;
        if (!m_loader.ensureLayerLoaded(layerIndex, &loadMessage)) {
            qWarning() << loadMessage;
        }
        else if (!loadMessage.isEmpty()) {
            qDebug() << loadMessage;
        }
    }

    const QRectF extent = m_camera->getVisibleMercatorExtent();
    const double worldWidth = GIS::MAX_MERCATOR_X - GIS::MIN_MERCATOR_X;
    int firstCopy = 0;
    int lastCopy = 0;
    visibleWorldCopyRange(m_camera, &firstCopy, &lastCopy);
    const int maxLabels = maxLabelsForZoom(zoom);
    const bool drawMarkerFallback = !m_gpuResourcesInitialized || !m_markerProgram.isLinked();
    int labelsDrawn = 0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    QFont font = painter.font();
    font.setPointSize(zoom >= 11.0 ? 9 : 10);
    font.setBold(zoom < 11.0);
    painter.setFont(font);
    QFontMetrics metrics(font);

    QVector<QRect> occupiedRects;
    occupiedRects.reserve(maxLabels);

    for (int layerIndex : layerIndices) {
        const CityLoader::CityLayer* layer = m_loader.layer(layerIndex);
        if (!layer || !layer->loaded)
            continue;

        const int markerSize = markerSizeForRank(layer->rank);
        const QColor markerColor = markerColorForRank(layer->rank);
        const QColor textColor = textColorForRank(layer->rank);

        for (const CityLoader::CityPoint& point : layer->points) {
            if (labelsDrawn >= maxLabels)
                break;

            for (int copy = firstCopy; copy <= lastCopy && labelsDrawn < maxLabels; ++copy) {
                QPointF mercator = point.mercator;
                mercator.rx() += copy * worldWidth;
                if (!pointInExtent(mercator, extent))
                    continue;

                QPointF screen;
                if (!m_camera->projectMercatorToScreen(mercator, &screen))
                    continue;
                if (screen.x() < -80.0 || screen.x() > m_camera->getViewportWidth() + 80.0
                    || screen.y() < -30.0 || screen.y() > m_camera->getViewportHeight() + 30.0) {
                    continue;
                }

                QRect textRect = metrics.boundingRect(point.name).adjusted(-5, -3, 5, 3);
                textRect.moveTopLeft(QPoint(
                    static_cast<int>(screen.x()) + markerSize + 4,
                    static_cast<int>(screen.y()) - textRect.height() / 2));

                QRect collisionRect = textRect.adjusted(-8, -5, 8, 5);
                collisionRect |= QRect(
                    static_cast<int>(screen.x()) - markerSize - 2,
                    static_cast<int>(screen.y()) - markerSize - 2,
                    markerSize * 2 + 4,
                    markerSize * 2 + 4);

                bool collides = false;
                for (const QRect& occupied : occupiedRects) {
                    if (occupied.intersects(collisionRect)) {
                        collides = true;
                        break;
                    }
                }
                if (collides)
                    continue;

                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 145));
                painter.drawRoundedRect(textRect, 3, 3);

                if (drawMarkerFallback) {
                    painter.setBrush(markerColor);
                    painter.setPen(QPen(QColor(10, 22, 32, 210), 1));
                    painter.drawEllipse(screen, markerSize, markerSize);
                }

                painter.setPen(textColor);
                painter.drawText(textRect, Qt::AlignCenter, point.name);

                occupiedRects.append(collisionRect);
                ++labelsDrawn;
            }
        }
    }

    painter.restore();
}

bool CityRenderer::pointInExtent(const QPointF& mercator, const QRectF& extent) const
{
    const double minX = std::min(extent.left(), extent.right());
    const double maxX = std::max(extent.left(), extent.right());
    const double minY = std::min(extent.top(), extent.bottom());
    const double maxY = std::max(extent.top(), extent.bottom());

    if (mercator.y() < minY || mercator.y() > maxY)
        return false;

    const double width = maxX - minX;
    if (width >= 2.0 * M_PI - 0.0001)
        return true;

    return mercator.x() >= minX && mercator.x() <= maxX;
}

int CityRenderer::maxLabelsForZoom(double zoomLevel) const
{
    if (zoomLevel < 8.0)
        return 140;
    if (zoomLevel < 11.0)
        return 300;
    if (zoomLevel < 14.0)
        return 650;
    return 900;
}

int CityRenderer::markerSizeForRank(CityLoader::PlaceRank rank) const
{
    switch (rank) {
    case CityLoader::PlaceRank::City:
        return 4;
    case CityLoader::PlaceRank::Town:
        return 3;
    case CityLoader::PlaceRank::Village:
        return 3;
    case CityLoader::PlaceRank::Hamlet:
        return 2;
    }

    return 3;
}

QColor CityRenderer::markerColorForRank(CityLoader::PlaceRank rank) const
{
    switch (rank) {
    case CityLoader::PlaceRank::City:
        return QColor(255, 205, 92);
    case CityLoader::PlaceRank::Town:
        return QColor(129, 215, 255);
    case CityLoader::PlaceRank::Village:
        return QColor(176, 232, 160);
    case CityLoader::PlaceRank::Hamlet:
        return QColor(235, 235, 235);
    }

    return QColor(235, 235, 235);
}

QColor CityRenderer::textColorForRank(CityLoader::PlaceRank rank) const
{
    switch (rank) {
    case CityLoader::PlaceRank::City:
        return QColor(255, 242, 205);
    case CityLoader::PlaceRank::Town:
        return QColor(226, 247, 255);
    case CityLoader::PlaceRank::Village:
        return QColor(232, 255, 226);
    case CityLoader::PlaceRank::Hamlet:
        return QColor(245, 245, 245);
    }

    return QColor(245, 245, 245);
}
