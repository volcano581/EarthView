#include "TextureManager.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <algorithm>
#include <cmath>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace {
int mipLevelCount(int width, int height)
{
    const int maxDimension = std::max(width, height);
    if (maxDimension <= 0)
        return 1;

    return static_cast<int>(std::floor(std::log2(maxDimension))) + 1;
}
}

TextureManager::TextureManager(int maxCacheSizeMB)
    : m_maxCacheSize(maxCacheSizeMB * 1024 * 1024)
    , m_currentCacheSize(0)
    , m_nextUsageIndex(0)
{
}

TextureManager::~TextureManager()
{
    clearCache();
}

GLuint TextureManager::createTexture(const QImage& image, const QString& cacheKey)
{
    // Check if already cached
    if (m_textureCache.contains(cacheKey)) {
        return m_textureCache[cacheKey].id;
    }

    // Convert image to RGBA format
    QImage textureImage = image.convertToFormat(QImage::Format_RGBA8888);

    // Calculate texture size
    int textureSize = textureImage.width() * textureImage.height() * 4; // RGBA = 4 bytes per pixel

    // Evict old textures if cache is full
    while (m_currentCacheSize + textureSize > m_maxCacheSize && !m_textureCache.isEmpty()) {
        evictOldest();
    }

    // Create OpenGL texture
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* f = context ? context->extraFunctions() : nullptr;
    if (context && context->hasExtension(QByteArrayLiteral("GL_EXT_texture_filter_anisotropic"))) {
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAnisotropy));
    }

    // Upload texture data
    if (f) {
        f->glTexStorage2D(
            GL_TEXTURE_2D,
            mipLevelCount(textureImage.width(), textureImage.height()),
            GL_RGBA8,
            textureImage.width(),
            textureImage.height());
        f->glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            textureImage.width(),
            textureImage.height(),
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            textureImage.bits());
        f->glGenerateMipmap(GL_TEXTURE_2D);
    }
    else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, textureImage.width(), textureImage.height(),
            0, GL_RGBA, GL_UNSIGNED_BYTE, textureImage.bits());
        if (context && context->functions()) {
            context->functions()->glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    // Cache the texture
    TextureEntry entry;
    entry.id = textureId;
    entry.sizeBytes = textureSize;
    m_textureCache[cacheKey] = entry;
    m_textureUsageOrder[cacheKey] = m_nextUsageIndex++;
    m_currentCacheSize += textureSize;

    return textureId;
}

GLuint TextureManager::getTexture(const QString& cacheKey) const
{
    if (m_textureCache.contains(cacheKey)) {
        return m_textureCache[cacheKey].id;
    }
    return 0;
}

bool TextureManager::hasTexture(const QString& cacheKey) const
{
    return m_textureCache.contains(cacheKey);
}

void TextureManager::deleteTexture(const QString& cacheKey)
{
    if (!m_textureCache.contains(cacheKey))
        return;

    TextureEntry entry = m_textureCache[cacheKey];
    glDeleteTextures(1, &entry.id);
    m_currentCacheSize -= entry.sizeBytes;
    m_textureCache.remove(cacheKey);
    m_textureUsageOrder.remove(cacheKey);
}

void TextureManager::clearCache()
{
    for (const auto& entry : m_textureCache) {
        glDeleteTextures(1, (GLuint*)&entry.id);
    }
    m_textureCache.clear();
    m_textureUsageOrder.clear();
    m_currentCacheSize = 0;
}

void TextureManager::setMaxCacheSize(int sizeInMB)
{
    m_maxCacheSize = sizeInMB * 1024 * 1024;

    // Evict textures if current size exceeds new limit
    while (m_currentCacheSize > m_maxCacheSize && !m_textureCache.isEmpty()) {
        evictOldest();
    }
}

int TextureManager::getCacheSize() const
{
    return m_currentCacheSize / (1024 * 1024); // Return in MB
}

void TextureManager::evictOldest()
{
    if (m_textureCache.isEmpty())
        return;

    // Find the key with the lowest usage index
    QString oldestKey;
    int oldestIndex = INT_MAX;

    for (auto it = m_textureUsageOrder.begin(); it != m_textureUsageOrder.end(); ++it) {
        if (it.value() < oldestIndex) {
            oldestIndex = it.value();
            oldestKey = it.key();
        }
    }

    if (!oldestKey.isEmpty()) {
        deleteTexture(oldestKey);
    }
}
