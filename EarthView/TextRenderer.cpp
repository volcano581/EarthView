#include "TextRenderer.h"
#include "Camera.h"
#include "ShaderUtils.h"

#include <QDebug>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QPainter>
#include <QVector2D>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
constexpr int kAtlasMargin = 2;

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
    const QSize& atlasSize)
{
    if (atlasSize.width() <= 0 || atlasSize.height() <= 0)
        return;

    const float u0 = static_cast<float>(textureRect.left()) / atlasSize.width();
    const float v0 = static_cast<float>(textureRect.top()) / atlasSize.height();
    const float u1 = static_cast<float>(textureRect.right() + 1) / atlasSize.width();
    const float v1 = static_cast<float>(textureRect.bottom() + 1) / atlasSize.height();

    const QPointF p00(rect.left(), rect.top());
    const QPointF p10(rect.right(), rect.top());
    const QPointF p11(rect.right(), rect.bottom());
    const QPointF p01(rect.left(), rect.bottom());

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
}

struct TextRenderer::PackedLabel {
    Label label;
    QRect textureRect;
};

TextRenderer::TextRenderer(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_vbo(0)
    , m_vao(0)
    , m_textureId(0)
    , m_maxTextureSize(4096)
    , m_cachedLabelHash(0)
    , m_cachedVertexCount(0)
    , m_gpuResourcesInitialized(false)
    , m_cacheValid(false)
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
        if (m_vbo) {
            f->glDeleteBuffers(1, &m_vbo);
        }
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
    f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
    m_maxTextureSize = qMax(256, m_maxTextureSize);

    f->glGenVertexArrays(1, &m_vao);
    f->glGenBuffers(1, &m_vbo);
    f->glGenTextures(1, &m_textureId);

    f->glBindVertexArray(m_vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
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

    f->glBindTexture(GL_TEXTURE_2D, m_textureId);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    m_gpuResourcesInitialized = true;
}

QImage TextRenderer::buildAtlas(const QVector<Label>& labels, QVector<PackedLabel>* packedLabels) const
{
    if (!packedLabels)
        return QImage();

    packedLabels->clear();
    if (labels.isEmpty())
        return QImage();

    struct SizedLabel {
        Label label;
        QSize size;
    };

    QVector<SizedLabel> sizedLabels;
    sizedLabels.reserve(labels.size());
    int widestLabel = 0;

    for (const Label& label : labels) {
        const int width = static_cast<int>(std::ceil(label.rect.width()));
        const int height = static_cast<int>(std::ceil(label.rect.height()));
        if (label.text.isEmpty() || width <= 0 || height <= 0)
            continue;

        const QSize size(
            qMin(width, m_maxTextureSize - kAtlasMargin * 2),
            qMin(height, m_maxTextureSize - kAtlasMargin * 2));
        if (size.width() <= 0 || size.height() <= 0)
            continue;

        sizedLabels.append({ label, size });
        widestLabel = qMax(widestLabel, size.width());
    }

    if (sizedLabels.isEmpty())
        return QImage();

    int atlasWidth = qMin(m_maxTextureSize, 4096);
    atlasWidth = qMin(m_maxTextureSize, qMax(512, atlasWidth));
    if (widestLabel + kAtlasMargin * 2 > atlasWidth) {
        atlasWidth = qMin(m_maxTextureSize, widestLabel + kAtlasMargin * 2);
    }

    int x = kAtlasMargin;
    int y = kAtlasMargin;
    int rowHeight = 0;

    for (const SizedLabel& sized : sizedLabels) {
        if (x + sized.size.width() + kAtlasMargin > atlasWidth) {
            x = kAtlasMargin;
            y += rowHeight + kAtlasMargin;
            rowHeight = 0;
        }

        if (y + sized.size.height() + kAtlasMargin > m_maxTextureSize)
            break;

        PackedLabel packed;
        packed.label = sized.label;
        packed.textureRect = QRect(QPoint(x, y), sized.size);
        packedLabels->append(packed);

        x += sized.size.width() + kAtlasMargin;
        rowHeight = qMax(rowHeight, sized.size.height());
    }

    if (packedLabels->isEmpty())
        return QImage();

    const int atlasHeight = y + rowHeight + kAtlasMargin;
    QImage atlas(atlasWidth, atlasHeight, QImage::Format_RGBA8888);
    atlas.fill(Qt::transparent);

    QPainter painter(&atlas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (const PackedLabel& packed : *packedLabels) {
        const QRect rect = packed.textureRect;
        if (packed.label.backgroundColor.alpha() > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(packed.label.backgroundColor);
            painter.drawRoundedRect(rect, packed.label.radius, packed.label.radius);
        }

        painter.setFont(packed.label.font);
        painter.setPen(packed.label.textColor);
        painter.drawText(
            marginsRemovedOrOriginal(rect, packed.label.textMargins),
            packed.label.alignment,
            packed.label.text);
    }

    return atlas;
}

quint64 TextRenderer::hashLabels(const QVector<Label>& labels) const
{
    quint64 seed = static_cast<quint64>(labels.size());
    for (const Label& label : labels) {
        mixHash(&seed, qHash(label.text));
        mixHash(&seed, qHash(label.font.toString()));
        mixHash(&seed, label.textColor.rgba());
        mixHash(&seed, label.backgroundColor.rgba());
        mixHash(&seed, quantizedCoordinate(label.rect.left()));
        mixHash(&seed, quantizedCoordinate(label.rect.top()));
        mixHash(&seed, quantizedCoordinate(label.rect.width()));
        mixHash(&seed, quantizedCoordinate(label.rect.height()));
        mixHash(&seed, static_cast<quint64>(label.textMargins.left()));
        mixHash(&seed, static_cast<quint64>(label.textMargins.top()));
        mixHash(&seed, static_cast<quint64>(label.textMargins.right()));
        mixHash(&seed, static_cast<quint64>(label.textMargins.bottom()));
        mixHash(&seed, static_cast<quint64>(label.radius));
        mixHash(&seed, static_cast<quint64>(label.alignment.toInt()));
    }
    return seed;
}

void TextRenderer::uploadBatch(const QVector<Label>& labels, quint64 labelsHash)
{
    QVector<PackedLabel> packedLabels;
    const QImage atlas = buildAtlas(labels, &packedLabels);
    if (atlas.isNull() || packedLabels.isEmpty()) {
        m_cachedLabelHash = labelsHash;
        m_cachedVertexCount = 0;
        m_cacheValid = true;
        return;
    }

    QVector<TextVertex> vertices;
    vertices.reserve(packedLabels.size() * 6);
    for (const PackedLabel& packed : packedLabels) {
        appendQuad(vertices, packed.label.rect, packed.textureRect, atlas.size());
    }
    if (vertices.isEmpty()) {
        m_cachedLabelHash = labelsHash;
        m_cachedVertexCount = 0;
        m_cacheValid = true;
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, m_textureId);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    f->glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        atlas.width(),
        atlas.height(),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        atlas.constBits());

    f->glBindVertexArray(m_vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    f->glBufferData(
        GL_ARRAY_BUFFER,
        vertices.size() * static_cast<qsizetype>(sizeof(TextVertex)),
        vertices.constData(),
        GL_STREAM_DRAW);
    f->glBindBuffer(GL_ARRAY_BUFFER, 0);
    f->glBindVertexArray(0);

    m_cachedLabelHash = labelsHash;
    m_cachedVertexCount = vertices.size();
    m_cacheValid = true;
}

void TextRenderer::render(const QVector<Label>& labels)
{
    if (!m_camera || labels.isEmpty())
        return;

    initializeGpuResources();
    if (!m_gpuResourcesInitialized || !m_program.isLinked())
        return;

    const quint64 labelsHash = hashLabels(labels);
    if (!m_cacheValid || labelsHash != m_cachedLabelHash) {
        uploadBatch(labels, labelsHash);
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

    f->glBindVertexArray(0);
    m_program.release();
    f->glBindTexture(GL_TEXTURE_2D, 0);
}
