#pragma once
#ifndef MBTILESREADER_H
#define MBTILESREADER_H

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVector>

class QThreadPool;

struct MbTilesMetadata {
    bool valid = false;
    QString name;
    QString format;
    QString scheme = "tms";
    int minZoom = 0;
    int maxZoom = 0;
};

struct MbTilesVectorFeature {
    int type = 0;
    QHash<QString, QVariant> tags;
    QVector<QVector<QPointF>> paths;
};

struct MbTilesVectorLayer {
    QString name;
    int extent = 4096;
    QVector<MbTilesVectorFeature> features;
};

struct MbTilesVectorTile {
    QVector<MbTilesVectorLayer> layers;
    bool isEmpty() const { return layers.isEmpty(); }
};

/**
 * @brief Tile keys still wanted by a loader, shared with its reader jobs (thread-safe).
 *
 * A job checks it before reading, so tiles that scrolled out of view or belong to a zoom level
 * already left are skipped instead of read from a (possibly remote) file.
 */
class MbTilesRequestSet
{
public:
    void insert(const QString& key) { QMutexLocker lock(&m_mutex); m_keys.insert(key); }
    void remove(const QString& key) { QMutexLocker lock(&m_mutex); m_keys.remove(key); }
    bool contains(const QString& key) const { QMutexLocker lock(&m_mutex); return m_keys.contains(key); }
    void clear() { QMutexLocker lock(&m_mutex); m_keys.clear(); }

private:
    mutable QMutex m_mutex;
    QSet<QString> m_keys;
};

/**
 * @brief Reads MBTiles (SQLite) files, local or on a network share.
 *
 * Tile reads keep one read-only connection per thread and file open for the thread's
 * lifetime, so a large file (tens of GB, e.g. on a share served by the imagery server) is not
 * re-opened per tile: SQLite reads only the index pages and the tile asked for, and keeps
 * them in its page cache. The file is opened `immutable` (no locking round trips over SMB).
 * Run reads on threadPool(), whose threads live as long as the process, so the connections
 * are reused.
 */
class MbTilesReader
{
public:
    /// Shared pool for MBTiles reads: a few long-lived threads (each holds its connections).
    static QThreadPool* threadPool();
    /// SQLite URI that opens @p filePath read-only and immutable (drive, UNC or local path).
    static QString immutableUri(const QString& filePath);

    static bool isVectorFormat(const QString& format);
    static MbTilesMetadata readMetadata(const QString& filePath, QString* errorMessage = nullptr);
    static MbTilesVectorTile readVectorTile(
        const QString& filePath,
        int z,
        int x,
        int y,
        bool tmsYOrigin,
        QString* errorMessage = nullptr);
    static QImage readTileImage(
        const QString& filePath,
        int z,
        int x,
        int y,
        bool tmsYOrigin,
        const QString& format,
        QString* errorMessage = nullptr);
};

#endif // MBTILESREADER_H
