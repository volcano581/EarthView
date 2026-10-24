#pragma once
#ifndef REMOTEMBTILES_H
#define REMOTEMBTILES_H

#include <QByteArray>
#include <QHash>
#include <QString>

/**
 * @brief Reads an MBTiles file that the imagery server publishes over HTTP, by URL.
 *
 * The earth files name TMS layers by URL (`http://host:port/.../tilemapresource.xml`); an
 * MBTiles file on the same web server is named the same way:
 * @code
 * <image name="OSM" driver="mbtiles"> <url> http://host:port/maps/planet.mbtiles </url></image>
 * @endcode
 * SQLite cannot open a URL, so this is a small read-only reader of the SQLite file format that
 * fetches only the B-tree pages a lookup walks, with HTTP range requests in 64 KB blocks kept
 * in a shared 64 MB cache. A tile costs a few small requests; the file (tens of GB) is never
 * downloaded. The three MBTiles layouts in use are understood: a `tiles` table with an index,
 * the deduplicated `map` + `images` tables behind a `tiles` view (OpenMapTiles, tippecanoe,
 * mbutil) and planetiler's `tiles_shallow` + `tiles_data`.
 *
 * The web server must answer range requests (206 Partial Content), as IIS, nginx and Apache
 * do for static files. A server that ignores them is refused unless the file is small
 * (<= 64 MB), which is then read once.
 *
 * Thread-safe: call it from MbTilesReader::threadPool() threads. Each thread keeps its own
 * HTTP connection (keep-alive); the parsed schema and the block cache are shared per URL.
 */
namespace RemoteMbTiles {

/// True for an http:// or https:// location.
bool isRemoteUrl(const QString& location);

/// Rows of the `metadata` table. Empty, with @p errorMessage set, when the file cannot be read.
QHash<QString, QString> readMetadata(const QString& url, QString* errorMessage = nullptr);

/// tile_data of one tile (@p tileRow in the file's own row order). Empty when the file has no
/// such tile; @p errorMessage is set only on a real failure (network, corrupt file).
QByteArray readTile(const QString& url, int z, int x, int tileRow, QString* errorMessage = nullptr);

/// Plain blocking GET, safe on any thread (used for tile URLs and probes). @p range is a raw
/// Range header value ("bytes=0-99") or empty. @p httpStatus receives the status code (0 when
/// there was no HTTP answer). Returns the body; on failure, an empty array with
/// @p errorMessage set. A 404 is not an error for tile reads: check @p httpStatus.
QByteArray httpGet(const QString& url,
                   int timeoutMs,
                   int* httpStatus = nullptr,
                   QString* errorMessage = nullptr,
                   const QByteArray& range = QByteArray());

/// Drops every cached connection, schema and block (tests).
void clearCaches();

} // namespace RemoteMbTiles

#endif // REMOTEMBTILES_H
