#include "GridRenderer.h"
#include "Camera.h"
#include "MercatorProjection.h"
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QtMath>
#include <cmath>

GridRenderer::GridRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
{
    initializeOpenGLFunctions();
}

void GridRenderer::render()
{
    if (!m_camera)
        return;

    const double step = gridStepDegrees();
    const double startLon = std::ceil(-180.0 / step) * step;
    const double startLat = std::ceil(-80.0 / step) * step;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);
    glColor4f(0.9f, 0.95f, 1.0f, 0.35f);

    for (double lon = startLon; lon <= 180.0 + 0.001; lon += step) {
        bool drawing = false;
        for (double lat = -85.0; lat <= 85.0 + 0.001; lat += 1.0) {
            QPointF screen;
            if (projectLatLon(lat, lon, &screen)) {
                if (!drawing) {
                    glBegin(GL_LINE_STRIP);
                    drawing = true;
                }
                glVertex2f(screen.x(), screen.y());
            }
            else if (drawing) {
                glEnd();
                drawing = false;
            }
        }
        if (drawing) {
            glEnd();
        }
    }

    for (double lat = startLat; lat <= 80.0 + 0.001; lat += step) {
        bool drawing = false;
        for (double lon = -180.0; lon <= 180.0 + 0.001; lon += 1.0) {
            QPointF screen;
            if (projectLatLon(lat, lon, &screen)) {
                if (!drawing) {
                    glBegin(GL_LINE_STRIP);
                    drawing = true;
                }
                glVertex2f(screen.x(), screen.y());
            }
            else if (drawing) {
                glEnd();
                drawing = false;
            }
        }
        if (drawing) {
            glEnd();
        }
    }

    glEnable(GL_DEPTH_TEST);
}

void GridRenderer::renderLabels(QPainter& painter)
{
    if (!m_camera)
        return;

    const double step = gridStepDegrees();
    const QPointF centerLatLon = MercatorProjection::mercatorToLatLon(
        m_camera->getCenterMercator().x(),
        m_camera->getCenterMercator().y());
    const double centerLat = qBound(-80.0, centerLatLon.x(), 80.0);
    const double centerLon = centerLatLon.y();

    QFont font = painter.font();
    font.setPointSize(9);
    font.setBold(true);
    painter.setFont(font);

    QFontMetrics metrics(font);

    auto drawLabel = [&](const QPointF& anchor, const QString& text) {
        QRect rect = metrics.boundingRect(text).adjusted(-5, -3, 5, 3);
        rect.moveCenter(anchor.toPoint());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 145));
        painter.drawRoundedRect(rect, 3, 3);
        painter.setPen(QColor(230, 245, 255));
        painter.drawText(rect, Qt::AlignCenter, text);
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    for (double lat = std::ceil(-80.0 / step) * step; lat <= 80.0 + 0.001; lat += step) {
        QPointF screen;
        const double labelLon = m_camera->isOrthographic()
            ? centerLon + 65.0
            : centerLon;

        if (projectLatLon(lat, labelLon, &screen)) {
            const double x = m_camera->isOrthographic()
                ? screen.x() + 18.0
                : 44.0;
            if (screen.y() >= 16.0 && screen.y() <= m_camera->getViewportHeight() - 16.0) {
                drawLabel(QPointF(x, screen.y()), latitudeLabel(lat));
            }
        }
    }

    for (double lon = std::ceil(-180.0 / step) * step; lon <= 180.0 + 0.001; lon += step) {
        QPointF screen;
        const double labelLat = m_camera->isOrthographic() ? 0.0 : centerLat;
        if (projectLatLon(labelLat, lon, &screen)) {
            const double y = m_camera->isOrthographic()
                ? screen.y() + 18.0
                : m_camera->getViewportHeight() - 18.0;
            if (screen.x() >= 24.0 && screen.x() <= m_camera->getViewportWidth() - 24.0) {
                drawLabel(QPointF(screen.x(), y), longitudeLabel(lon));
            }
        }
    }

    painter.restore();
}

double GridRenderer::gridStepDegrees() const
{
    const double zoom = m_camera ? m_camera->getZoomLevel() : 2.0;

    if (zoom < 2.0)
        return 45.0;
    if (zoom < 4.0)
        return 30.0;
    if (zoom < 6.0)
        return 15.0;
    if (zoom < 8.0)
        return 5.0;
    return 1.0;
}

QString GridRenderer::latitudeLabel(double latitude) const
{
    if (qFuzzyIsNull(latitude))
        return "0 deg";

    return QString("%1 deg%2")
        .arg(std::abs(latitude), 0, 'f', latitude == std::round(latitude) ? 0 : 1)
        .arg(latitude > 0.0 ? "N" : "S");
}

QString GridRenderer::longitudeLabel(double longitude) const
{
    double wrapped = MercatorProjection::wrapMercatorX(longitude * M_PI / 180.0) * 180.0 / M_PI;
    if (std::abs(wrapped) < 0.0001)
        return "0 deg";
    if (std::abs(std::abs(wrapped) - 180.0) < 0.0001)
        return "180 deg";

    return QString("%1 deg%2")
        .arg(std::abs(wrapped), 0, 'f', wrapped == std::round(wrapped) ? 0 : 1)
        .arg(wrapped > 0.0 ? "E" : "W");
}

bool GridRenderer::projectLatLon(double latitude, double longitude, QPointF* screenPos) const
{
    if (!m_camera || !screenPos)
        return false;

    QPointF mercator = MercatorProjection::latLonToMercator(latitude, longitude);
    return m_camera->projectMercatorToScreen(mercator, screenPos);
}
