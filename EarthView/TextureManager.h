#pragma once
#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include <QString>
#include <QMap>
#include <QImage>
#include <QOpenGLFunctions>

/**
 * @brief TextureManager handles OpenGL texture creation, caching, and lifecycle
 *
 * Responsibilities:
 * - Create OpenGL textures from images
 * - Cache textures with size limits
 * - Delete textures and cleanup resources
 * - Texture parameter configuration
 */
class TextureManager
{
public:
    TextureManager(int maxCacheSizeMB = 200);
    ~TextureManager();

    // Texture creation and caching
    GLuint createTexture(const QImage& image, const QString& cacheKey);
    GLuint getTexture(const QString& cacheKey) const;
    bool hasTexture(const QString& cacheKey) const;

    // Cache management
    void deleteTexture(const QString& cacheKey);
    void clearCache();

    // Configuration
    void setMaxCacheSize(int sizeInMB);
    int getCacheSize() const;
    int getTextureCount() const { return m_textureCache.size(); }

private:
    void setupTextureParameters();
    void evictOldest();

private:
    struct TextureEntry {
        GLuint id;
        int sizeBytes;
    };

    QMap<QString, TextureEntry> m_textureCache;
    QMap<QString, int> m_textureUsageOrder;  // Track insertion order for LRU
    int m_maxCacheSize;
    int m_currentCacheSize;
    int m_nextUsageIndex;
};

#endif // TEXTUREMANAGER_H