#include "GridRenderer.h"
#include "Camera.h"
#include "Constants.h"
#include "MercatorProjection.h"
#include "ShaderUtils.h"
#include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QPainter>
#include <QVector2D>
#include <QVector4D>
#include <QtMath>
#include <cmath>
#include <cstddef>

namespace {
struct ScreenVertex {
    float position[2];
};

ScreenVertex screenVertex(const QPointF& point)
{
    return { { static_cast<float>(point.x()), static_cast<float>(point.y()) } };
}

void appendSegment(QVector<ScreenVertex>& vertices, const QPointF& a, const QPointF& b)
{
    vertices.append(screenVertex(a));
    vertices.append(screenVertex(b));
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

GridRenderer::GridRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_vbo(0)
    , m_vao(0)
    , m_gpuResourcesInitialized(false)
{
    initializeOpenGLFunctions();
}

GridRenderer::~GridRenderer()
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

void GridRenderer::initializeGpuResources()
{
    if (m_gpuResourcesInitialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            &m_lineProgram,
            QStringLiteral("solid_2d.vert"),
            QStringLiteral("solid_2d.frag"),
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
        sizeof(ScreenVertex),
        reinterpret_cast<void*>(offsetof(ScreenVertex, position)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);

    m_gpuResourcesInitialized = true;
}

void GridRenderer::render()
{
    if (!m_camera)
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized || !m_lineProgram.isLinked())
        return;

    const double step = gridStepDegrees();
    const double startLon = std::ceil(-180.0 / step) * step;
    const double startLat = std::ceil(-80.0 / step) * step;
    int firstCopy = 0;
    int lastCopy = 0;
    visibleWorldCopyRange(m_camera, &firstCopy, &lastCopy);

    QVector<ScreenVertex> vertices;

    for (int copy = firstCopy; copy <= lastCopy; ++copy) {
        const double lonOffset = copy * 360.0;
        const double lonEnd = copy == lastCopy ? 180.0 + 0.001 : 180.0 - 0.001;

        for (double lon = startLon; lon <= lonEnd; lon += step) {
            bool previousVisible = false;
            QPointF previousScreen;
            const double renderLon = lon + lonOffset;
            for (double lat = -85.0; lat <= 85.0 + 0.001; lat += 1.0) {
                QPointF screen;
                if (projectLatLon(lat, renderLon, &screen)) {
                    if (previousVisible) {
                        appendSegment(vertices, previousScreen, screen);
                    }
                    previousScreen = screen;
                    previousVisible = true;
                }
                else {
                    previousVisible = false;
                }
            }
        }
    }

    for (int copy = firstCopy; copy <= lastCopy; ++copy) {
        const double lonOffset = copy * 360.0;
        const double lonStart = -180.0 + lonOffset;
        const double lonEnd = 180.0 + lonOffset;

        for (double lat = startLat; lat <= 80.0 + 0.001; lat += step) {
            bool previousVisible = false;
            QPointF previousScreen;
            for (double lon = lonStart; lon <= lonEnd + 0.001; lon += 1.0) {
                QPointF screen;
                if (projectLatLon(lat, lon, &screen)) {
                    if (previousVisible) {
                        appendSegment(vertices, previousScreen, screen);
                    }
                    previousScreen = screen;
                    previousVisible = true;
                }
                else {
                    previousVisible = false;
                }
            }
        }
    }

    if (vertices.isEmpty())
        return;

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    m_lineProgram.bind();
    m_lineProgram.setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));
    m_lineProgram.setUniformValue("u_color", QVector4D(0.9f, 0.95f, 1.0f, 0.35f));

    f->glBindVertexArray(m_vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    f->glBufferData(
        GL_ARRAY_BUFFER,
        vertices.size() * static_cast<qsizetype>(sizeof(ScreenVertex)),
        vertices.constData(),
        GL_STREAM_DRAW);
    f->glDrawArrays(GL_LINES, 0, vertices.size());
    f->glBindVertexArray(0);
    m_lineProgram.release();
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
    int firstCopy = 0;
    int lastCopy = 0;
    visibleWorldCopyRange(m_camera, &firstCopy, &lastCopy);

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
        if (projectLatLon(lat, centerLon, &screen)) {
            if (screen.x() >= 24.0 && screen.x() <= m_camera->getViewportWidth() - 24.0
                && screen.y() >= 16.0 && screen.y() <= m_camera->getViewportHeight() - 16.0) {
                drawLabel(screen, latitudeLabel(lat));
            }
        }
    }

    for (int copy = firstCopy; copy <= lastCopy; ++copy) {
        const double lonOffset = copy * 360.0;
        const double lonEnd = copy == lastCopy ? 180.0 + 0.001 : 180.0 - 0.001;

        for (double lon = std::ceil(-180.0 / step) * step; lon <= lonEnd; lon += step) {
            QPointF screen;
            const double renderLon = lon + lonOffset;
            if (projectLatLon(centerLat, renderLon, &screen)) {
                if (screen.x() >= 24.0 && screen.x() <= m_camera->getViewportWidth() - 24.0
                    && screen.y() >= 16.0 && screen.y() <= m_camera->getViewportHeight() - 16.0) {
                    drawLabel(screen, longitudeLabel(renderLon));
                }
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
        return "0";

    return QString("%1%2")
        .arg(std::abs(latitude), 0, 'f', latitude == std::round(latitude) ? 0 : 1)
        .arg(latitude > 0.0 ? "N" : "S");
}

QString GridRenderer::longitudeLabel(double longitude) const
{
    double wrapped = MercatorProjection::wrapMercatorX(longitude * M_PI / 180.0) * 180.0 / M_PI;
    if (std::abs(wrapped) < 0.0001)
        return "0";
    if (std::abs(std::abs(wrapped) - 180.0) < 0.0001)
        return "180";

    return QString("%1%2")
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
