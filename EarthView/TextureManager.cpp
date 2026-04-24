#include "TextureManager.h"
#include <QDebug>
#include <QOpenGLFunctions>

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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureImage.width(), textureImage.height(),
        0, GL_RGBA, GL_UNSIGNED_BYTE, textureImage.bits());

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
