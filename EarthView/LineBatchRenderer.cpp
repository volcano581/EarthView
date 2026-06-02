#include "LineBatchRenderer.h"
#include "Camera.h"
#include "Constants.h"
#include "FrameProfiler.h"
#include "ShaderUtils.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QVector2D>
#include <QtMath>
#include <cstddef>

LineBatchRenderer::LineBatchRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_vao(0)
    , m_vertexCount(0)
    , m_initialized(false)
{
    initializeOpenGLFunctions();
}

LineBatchRenderer::~LineBatchRenderer()
{
    if (!m_initialized)
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (f) {
        m_vertexBuffer.destroy();
        if (m_vao) {
            f->glDeleteVertexArrays(1, &m_vao);
        }
    }
}

LineBatchRenderer::LineVertex LineBatchRenderer::vertex(double x, double y, const QColor& color)
{
    return {
        { static_cast<float>(x), static_cast<float>(y) },
        {
            static_cast<float>(color.redF()),
            static_cast<float>(color.greenF()),
            static_cast<float>(color.blueF()),
            static_cast<float>(color.alphaF())
        }
    };
}

void LineBatchRenderer::appendSegment(
    QVector<LineVertex>& vertices,
    const QPointF& a,
    const QPointF& b,
    const QColor& color)
{
    vertices.append(vertex(a.x(), a.y(), color));
    vertices.append(vertex(b.x(), b.y(), color));
}

void LineBatchRenderer::initializeGpuResources()
{
    if (m_initialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            &m_screenProgram,
            QStringLiteral("colored_2d.vert"),
            QStringLiteral("colored_line.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    if (!ShaderUtils::loadProgram(
            &m_mercatorProgram,
            QStringLiteral("colored_mercator.vert"),
            QStringLiteral("colored_line.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    if (!ShaderUtils::loadProgram(
            &m_terrainProgram,
            QStringLiteral("colored_terrain.vert"),
            QStringLiteral("colored_line.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    f->glGenVertexArrays(1, &m_vao);
    if (!m_vertexBuffer.initialize(GL_ARRAY_BUFFER))
        return;

    setupVertexArray();
    m_initialized = true;
}

void LineBatchRenderer::setupVertexArray()
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
        sizeof(LineVertex),
        reinterpret_cast<void*>(offsetof(LineVertex, position)));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(LineVertex),
        reinterpret_cast<void*>(offsetof(LineVertex, color)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);
}

void LineBatchRenderer::setVertices(const QVector<LineVertex>& vertices)
{
    initializeGpuResources();
    if (!m_initialized)
        return;

    FrameProfiler::Scope uploadScope(QStringLiteral("upload.lineBatch"));
    FrameProfiler::recordCount(QStringLiteral("upload.lineBatch.vertices"), vertices.size());
    FrameProfiler::recordCount(
        QStringLiteral("upload.lineBatch.bytes"),
        vertices.size() * static_cast<qsizetype>(sizeof(LineVertex)));

    const StreamingBuffer::UploadResult upload = m_vertexBuffer.upload(
        vertices.constData(),
        vertices.size() * static_cast<qsizetype>(sizeof(LineVertex)),
        GL_DYNAMIC_DRAW);
    if (!upload.ok)
        return;
    if (upload.bufferRecreated) {
        setupVertexArray();
    }

    m_vertexCount = vertices.size();
}

void LineBatchRenderer::render(CoordinateMode mode, float lineWidth)
{
    if (!m_camera || m_vertexCount <= 0)
        return;

    initializeGpuResources();
    if (!m_initialized)
        return;

    QOpenGLShaderProgram* program = &m_screenProgram;
    if (mode == CoordinateMode::Mercator) {
        program = &m_mercatorProgram;
    }
    else if (mode == CoordinateMode::Terrain3D) {
        program = &m_terrainProgram;
    }
    if (!program->isLinked())
        return;

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(lineWidth);

    program->bind();
    program->setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));

    if (mode == CoordinateMode::Mercator) {
        program->setUniformValue(
            "u_centerMercator",
            QVector2D(
                static_cast<float>(m_camera->getCenterMercator().x()),
                static_cast<float>(m_camera->getCenterMercator().y())));
        program->setUniformValue(
            "u_pixelsPerMercator",
            static_cast<float>(GIS::EARTH_RADIUS / m_camera->getResolution()));
    }
    else if (mode == CoordinateMode::Terrain3D) {
        program->setUniformValue(
            "u_centerMercator",
            QVector2D(
                static_cast<float>(m_camera->getCenterMercator().x()),
                static_cast<float>(m_camera->getCenterMercator().y())));
        program->setUniformValue("u_pixelsPerMeter", static_cast<float>(1.0 / m_camera->getResolution()));
        program->setUniformValue("u_earthRadius", static_cast<float>(GIS::EARTH_RADIUS));
        program->setUniformValue("u_pitchRadians", qDegreesToRadians(m_camera->terrainPitchDegrees()));
        program->setUniformValue(
            "u_yawRadians",
            m_camera->isStealthViewEnabled() ? qDegreesToRadians(m_camera->cameraYawDegrees()) : 0.0f);
        program->setUniformValue(
            "u_screenAnchor",
            QVector2D(
                static_cast<float>(m_camera->terrainScreenAnchor().x()),
                static_cast<float>(m_camera->terrainScreenAnchor().y())));
        program->setUniformValue("u_focalPixels", static_cast<float>(m_camera->terrainFocalPixels()));
        program->setUniformValue("u_viewDistanceMeters", static_cast<float>(m_camera->terrainViewDistanceMeters()));
    }

    f->glBindVertexArray(m_vao);
    f->glDrawArrays(GL_LINES, 0, m_vertexCount);
    FrameProfiler::recordCount(QStringLiteral("draw.calls"));
    FrameProfiler::recordCount(QStringLiteral("draw.lineBatch.vertices"), m_vertexCount);
    f->glBindVertexArray(0);
    program->release();
}
