#include "TextRenderer.h"
#include "Camera.h"
#include "FrameProfiler.h"
#include "ShaderUtils.h"

#include <QDebug>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QVector2D>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
constexpr int kAtlasMargin = 2;
constexpr int kPreferredAtlasSize = 2048;

struct TextVertex {
    float position[2];
    float texCoord[2];
};

TextVertex textVertex(const QPointF& position, float u, float v)
{
    return {
        { static_cast<float>(position.x()), static_cast<float>(position.y()) },
        { u, v }
    };
}

void appendQuad(
    QVector<TextVertex>& vertices,
    const QRectF& rect,
    const QRect& textureRect,
    int atlasSize,
    qreal angleDegrees)
{
    if (atlasSize <= 0)
        return;

    const float u0 = static_cast<float>(textureRect.left()) / atlasSize;
    const float v0 = static_cast<float>(textureRect.top()) / atlasSize;
    const float u1 = static_cast<float>(textureRect.right() + 1) / atlasSize;
    const float v1 = static_cast<float>(textureRect.bottom() + 1) / atlasSize;

    QPointF p00(rect.left(), rect.top());
    QPointF p10(rect.right(), rect.top());
    QPointF p11(rect.right(), rect.bottom());
    QPointF p01(rect.left(), rect.bottom());
    if (angleDegrees != 0.0) {
        const QPointF center = rect.center();
        const double radians = qDegreesToRadians(angleDegrees);
        const double cosA = std::cos(radians);
        const double sinA = std::sin(radians);
        auto rotate = [&](const QPointF& p) {
            const QPointF d = p - center;
            return center + QPointF(d.x() * cosA - d.y() * sinA, d.x() * sinA + d.y() * cosA);
        };
        p00 = rotate(p00);
        p10 = rotate(p10);
        p11 = rotate(p11);
        p01 = rotate(p01);
    }

    vertices.append(textVertex(p00, u0, v0));
    vertices.append(textVertex(p10, u1, v0));
    vertices.append(textVertex(p11, u1, v1));
    vertices.append(textVertex(p00, u0, v0));
    vertices.append(textVertex(p11, u1, v1));
    vertices.append(textVertex(p01, u0, v1));
}

QRect marginsRemovedOrOriginal(const QRect& rect, const QMargins& margins)
{
    const QRect adjusted = rect.marginsRemoved(margins);
    if (adjusted.width() <= 0 || adjusted.height() <= 0)
        return rect;

    return adjusted;
}

void mixHash(quint64* seed, quint64 value)
{
    if (!seed)
        return;

    *seed ^= value + 0x9e3779b97f4a7c15ULL + (*seed << 6) + (*seed >> 2);
}

quint64 quantizedCoordinate(double value)
{
    return static_cast<quint64>(qRound64(value * 64.0));
}

QSize labelSize(const TextRenderer::Label& label)
{
    return QSize(static_cast<int>(std::ceil(label.rect.width())), static_cast<int>(std::ceil(label.rect.height())));
}
}

TextRenderer::TextRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_vao(0)
    , m_textureId(0)
    , m_atlasSize(kPreferredAtlasSize)
    , m_shelfX(kAtlasMargin)
    , m_shelfY(kAtlasMargin)
    , m_shelfHeight(0)
    , m_frame(0)
    , m_cachedGeometryHash(0)
    , m_cachedVertexCount(0)
    , m_gpuResourcesInitialized(false)
{
    initializeOpenGLFunctions();
}

TextRenderer::~TextRenderer()
{
    if (!m_gpuResourcesInitialized)
        return;

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (f) {
        if (m_textureId) {
            f->glDeleteTextures(1, &m_textureId);
        }
        m_vertexBuffer.destroy();
        if (m_vao) {
            f->glDeleteVertexArrays(1, &m_vao);
        }
    }
}

void TextRenderer::initializeGpuResources()
{
    if (m_gpuResourcesInitialized)
        return;

    QString errorMessage;
    if (!ShaderUtils::loadProgram(
            &m_program,
            QStringLiteral("textured_quad.vert"),
            QStringLiteral("textured_quad.frag"),
            &errorMessage)) {
        qWarning() << errorMessage;
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->initializeOpenGLFunctions();
    GLint maxTextureSize = 4096;
    f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    m_atlasSize = qBound(256, static_cast<int>(maxTextureSize), kPreferredAtlasSize);

    f->glGenVertexArrays(1, &m_vao);
    if (!m_vertexBuffer.initialize(GL_ARRAY_BUFFER))
        return;
    f->glGenTextures(1, &m_textureId);

    setupVertexArray();

    f->glBindTexture(GL_TEXTURE_2D, m_textureId);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_atlasSize, m_atlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    m_gpuResourcesInitialized = true;
}

void TextRenderer::setupVertexArray()
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
        sizeof(TextVertex),
        reinterpret_cast<void*>(offsetof(TextVertex, position)));
    f->glEnableVertexAttribArray(1);
    f->glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TextVertex),
        reinterpret_cast<void*>(offsetof(TextVertex, texCoord)));
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);
}

quint64 TextRenderer::labelKey(const Label& label)
{
    const QSize size = labelSize(label);
    quint64 seed = qHash(label.text);
    mixHash(&seed, qHash(label.font));
    mixHash(&seed, label.textColor.rgba());
    mixHash(&seed, label.backgroundColor.rgba());
    mixHash(&seed, static_cast<quint64>(size.width()) << 32 | static_cast<quint32>(size.height()));
    mixHash(&seed, static_cast<quint64>(label.textMargins.left()) << 48
                       | static_cast<quint64>(label.textMargins.top() & 0xffff) << 32
                       | static_cast<quint64>(label.textMargins.right() & 0xffff) << 16
                       | static_cast<quint64>(label.textMargins.bottom() & 0xffff));
    mixHash(&seed, static_cast<quint64>(label.radius));
    mixHash(&seed, static_cast<quint64>(label.alignment.toInt()));
    mixHash(&seed, label.haloColor.rgba());
    mixHash(&seed, quantizedCoordinate(label.haloWidth));
    mixHash(&seed, static_cast<quint64>(label.marker));
    mixHash(&seed, label.markerColor.rgba());
    mixHash(&seed, quantizedCoordinate(label.markerSize));
    return seed;
}

void TextRenderer::paintLabel(QPainter& painter, const QRect& rect, const Label& label)
{
    if (label.backgroundColor.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(label.backgroundColor);
        painter.drawRoundedRect(rect, label.radius, label.radius);
    }

    QRectF textRect = marginsRemovedOrOriginal(rect, label.textMargins);
    if (label.marker != Marker::None && label.markerSize > 0.0) {
        const qreal size = label.markerSize;
        const QPointF center(rect.left() + label.haloWidth + size * 0.5 + 0.5, rect.center().y() + 0.5);
        painter.setPen(QPen(label.haloColor.alpha() > 0 ? label.haloColor : QColor(255, 255, 255, 200), 1.2));
        painter.setBrush(label.markerColor);
        if (label.marker == Marker::Circle) {
            painter.drawEllipse(center, size * 0.5, size * 0.5);
        }
        else if (label.marker == Marker::Square) {
            painter.drawRoundedRect(QRectF(center.x() - size * 0.5, center.y() - size * 0.5, size, size), 2, 2);
        }
        else {
            QPolygonF triangle;
            triangle << QPointF(center.x(), center.y() - size * 0.55) << QPointF(center.x() + size * 0.55, center.y() + size * 0.45)
                     << QPointF(center.x() - size * 0.55, center.y() + size * 0.45);
            painter.drawPolygon(triangle);
        }
        textRect.setLeft(rect.left() + label.haloWidth + size + 4.0);
    }

    if (label.haloColor.alpha() <= 0 || label.haloWidth <= 0.0) {
        painter.setFont(label.font);
        painter.setPen(label.textColor);
        painter.drawText(textRect, label.alignment, label.text);
        return;
    }

    // Outlined glyphs: the halo is a stroke of the text path, the text filled over it.
    const QFontMetricsF metrics(label.font);
    const qreal textWidth = metrics.horizontalAdvance(label.text);
    qreal x = textRect.left();
    if (label.alignment & Qt::AlignHCenter)
        x = textRect.center().x() - textWidth * 0.5;
    else if (label.alignment & Qt::AlignRight)
        x = textRect.right() - textWidth;
    const qreal baseline = textRect.center().y() + (metrics.ascent() - metrics.descent()) * 0.5;
    QPainterPath path;
    path.addText(QPointF(x, baseline), label.font, label.text);
    painter.strokePath(path, QPen(label.haloColor, label.haloWidth * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.fillPath(path, label.textColor);
}

void TextRenderer::resetAtlas()
{
    m_entries.clear();
    m_shelfX = kAtlasMargin;
    m_shelfY = kAtlasMargin;
    m_shelfHeight = 0;
}

bool TextRenderer::allocate(const QSize& size, QRect* rect)
{
    // Room for a transparent border on every side, written with the label, so linear filtering
    // never samples a neighbour.
    const int width = size.width() + 2 * kAtlasMargin;
    const int height = size.height() + 2 * kAtlasMargin;
    if (width > m_atlasSize || height > m_atlasSize)
        return false;
    if (m_shelfX + width > m_atlasSize) {
        m_shelfX = 0;
        m_shelfY += m_shelfHeight;
        m_shelfHeight = 0;
    }
    if (m_shelfY + height > m_atlasSize)
        return false;
    *rect = QRect(m_shelfX, m_shelfY, width, height);
    m_shelfX += width;
    m_shelfHeight = qMax(m_shelfHeight, height);
    return true;
}

bool TextRenderer::addToAtlas(const Label& label, quint64 key, AtlasEntry* entry)
{
    const QSize size = labelSize(label);
    if (label.text.isEmpty() || size.width() <= 0 || size.height() <= 0)
        return false;

    QRect slot;
    if (!allocate(size, &slot))
        return false;

    QImage image(slot.size(), QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintLabel(painter, QRect(QPoint(kAtlasMargin, kAtlasMargin), size), label);
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->glBindTexture(GL_TEXTURE_2D, m_textureId);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    f->glTexSubImage2D(GL_TEXTURE_2D, 0, slot.x(), slot.y(), slot.width(), slot.height(),
                       GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());

    entry->textureRect = QRect(slot.topLeft() + QPoint(kAtlasMargin, kAtlasMargin), size);
    entry->lastUsedFrame = m_frame;
    m_entries.insert(key, *entry);
    return true;
}

void TextRenderer::render(const QVector<Label>& labels)
{
    if (!m_camera || labels.isEmpty())
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized || !m_program.isLinked())
        return;

    ++m_frame;
    QVector<quint64> keys;
    keys.reserve(labels.size());
    int newLabels = 0;
    for (int pass = 0; pass < 2; ++pass) {
        keys.clear();
        bool full = false;
        quint64 geometryHash = static_cast<quint64>(labels.size());
        for (const Label& label : labels) {
            const quint64 key = labelKey(label);
            keys.append(key);
            mixHash(&geometryHash, key);
            mixHash(&geometryHash, quantizedCoordinate(label.rect.left()));
            mixHash(&geometryHash, quantizedCoordinate(label.rect.top()));
            mixHash(&geometryHash, quantizedCoordinate(label.angle));

            auto it = m_entries.find(key);
            if (it != m_entries.end()) {
                it->lastUsedFrame = m_frame;
                continue;
            }
            AtlasEntry entry;
            if (addToAtlas(label, key, &entry)) {
                ++newLabels;
                continue;
            }
            if (!label.text.isEmpty())
                full = true;
        }
        if (!full || pass == 1) {
            if (newLabels > 0) {
                FrameProfiler::recordCount(QStringLiteral("textAtlas.newLabels"), newLabels);
                m_cachedGeometryHash = 0;  // texture coordinates of reused keys may have moved
            }
            if (geometryHash != m_cachedGeometryHash || m_cachedVertexCount == 0) {
                FrameProfiler::Scope uploadScope(QStringLiteral("upload.textGeometry"));
                QVector<TextVertex> vertices;
                vertices.reserve(labels.size() * 6);
                for (int i = 0; i < labels.size(); ++i) {
                    const auto it = m_entries.constFind(keys.at(i));
                    if (it != m_entries.cend())
                        appendQuad(vertices, labels.at(i).rect, it->textureRect, m_atlasSize, labels.at(i).angle);
                }
                m_cachedVertexCount = 0;
                if (!vertices.isEmpty()) {
                    const StreamingBuffer::UploadResult upload = m_vertexBuffer.upload(
                        vertices.constData(),
                        vertices.size() * static_cast<qsizetype>(sizeof(TextVertex)),
                        GL_STREAM_DRAW);
                    if (upload.ok) {
                        if (upload.bufferRecreated)
                            setupVertexArray();
                        m_cachedVertexCount = vertices.size();
                    }
                }
                m_cachedGeometryHash = geometryHash;
            }
            break;
        }
        // Full: start over with only what this frame shows.
        FrameProfiler::recordCount(QStringLiteral("textAtlas.resets"));
        resetAtlas();
        newLabels = 0;
    }

    if (m_cachedVertexCount <= 0)
        return;

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, m_textureId);

    m_program.bind();
    m_program.setUniformValue(
        "u_viewportSize",
        QVector2D(
            static_cast<float>(m_camera->getViewportWidth()),
            static_cast<float>(m_camera->getViewportHeight())));
    m_program.setUniformValue("u_texture", 0);

    f->glBindVertexArray(m_vao);
    f->glDrawArrays(GL_TRIANGLES, 0, m_cachedVertexCount);
    FrameProfiler::recordCount(QStringLiteral("draw.calls"));
    FrameProfiler::recordCount(QStringLiteral("draw.text.vertices"), m_cachedVertexCount);

    f->glBindVertexArray(0);
    m_program.release();
    f->glBindTexture(GL_TEXTURE_2D, 0);
}
