#include "CityRenderer.h"
#include "Camera.h"
#include "Constants.h"
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <algorithm>

CityRenderer::CityRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
{
}

bool CityRenderer::loadDirectory(const QString& rootPath, QString* errorMessage)
{
    return m_loader.discover(rootPath, errorMessage);
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
    const int maxLabels = maxLabelsForZoom(zoom);
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
            if (!pointInExtent(point.mercator, extent))
                continue;

            QPointF screen;
            if (!m_camera->projectMercatorToScreen(point.mercator, &screen))
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

            painter.setBrush(markerColor);
            painter.setPen(QPen(QColor(10, 22, 32, 210), 1));
            painter.drawEllipse(screen, markerSize, markerSize);

            painter.setPen(textColor);
            painter.drawText(textRect, Qt::AlignCenter, point.name);

            occupiedRects.append(collisionRect);
            ++labelsDrawn;
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
