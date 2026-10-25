#include "MbTilesReader.h"

#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QtGlobal>
#include <cstring>

#if __has_include(<QtZlib/zlib.h>)
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

namespace {
constexpr int kTileImageSize = 256;

using MvtFeature = MbTilesVectorFeature;
using MvtLayer = MbTilesVectorLayer;

QString connectionName()
{
    return QStringLiteral("earthview_mbtiles_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool openDatabase(const QString& filePath, const QString& name, QSqlDatabase* database, QString* errorMessage)
{
    if (!database)
        return false;

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setError(errorMessage, QStringLiteral("Qt SQLite driver is not available."));
        return false;
    }

    *database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    database->setDatabaseName(filePath);
    if (!database->open()) {
        setError(
            errorMessage,
            QStringLiteral("Could not open MBTiles database %1: %2")
                .arg(filePath, database->lastError().text()));
        return false;
    }

    return true;
}

QHash<QString, QString> readMetadataMap(const QString& filePath, QString* errorMessage)
{
    const QString name = connectionName();
    QHash<QString, QString> metadata;

    {
        QSqlDatabase database;
        if (!openDatabase(filePath, name, &database, errorMessage)) {
            QSqlDatabase::removeDatabase(name);
            return metadata;
        }

        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("select name, value from metadata"))) {
            setError(errorMessage, QStringLiteral("Could not read MBTiles metadata: %1").arg(query.lastError().text()));
        }
        else {
            while (query.next()) {
                metadata.insert(query.value(0).toString(), query.value(1).toString());
            }
        }

        database.close();
    }

    QSqlDatabase::removeDatabase(name);
    return metadata;
}

QByteArray readTileData(
    const QString& filePath,
    int z,
    int x,
    int y,
    bool tmsYOrigin,
    QString* errorMessage)
{
    const QString name = connectionName();
    QByteArray tileData;

    {
        QSqlDatabase database;
        if (!openDatabase(filePath, name, &database, errorMessage)) {
            QSqlDatabase::removeDatabase(name);
            return tileData;
        }

        const int tileRow = tmsYOrigin ? ((1 << z) - 1 - y) : y;
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "select tile_data from tiles "
            "where zoom_level = ? and tile_column = ? and tile_row = ? "
            "limit 1"));
        query.addBindValue(z);
        query.addBindValue(x);
        query.addBindValue(tileRow);

        if (!query.exec()) {
            setError(errorMessage, QStringLiteral("Could not read MBTiles tile: %1").arg(query.lastError().text()));
        }
        else if (query.next()) {
            tileData = query.value(0).toByteArray();
        }

        database.close();
    }

    QSqlDatabase::removeDatabase(name);
    return tileData;
}

QByteArray gunzip(const QByteArray& data)
{
    if (data.size() < 2
        || static_cast<unsigned char>(data.at(0)) != 0x1f
        || static_cast<unsigned char>(data.at(1)) != 0x8b) {
        return data;
    }

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());

    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return QByteArray();
    }

    QByteArray output;
    char buffer[16384];
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        status = inflate(&stream, Z_NO_FLUSH);
        output.append(buffer, sizeof(buffer) - stream.avail_out);
    }

    inflateEnd(&stream);
    return status == Z_STREAM_END ? output : QByteArray();
}

bool readVarint(const QByteArray& data, qsizetype* offset, quint64* value)
{
    if (!offset || !value)
        return false;

    quint64 result = 0;
    int shift = 0;
    while (*offset < data.size() && shift <= 63) {
        const quint8 byte = static_cast<quint8>(data.at((*offset)++));
        result |= static_cast<quint64>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return true;
        }
        shift += 7;
    }

    return false;
}

qint64 zigZagDecode(quint64 value)
{
    return static_cast<qint64>((value >> 1) ^ (~(value & 1) + 1));
}

bool readField(
    const QByteArray& data,
    qsizetype* offset,
    int* fieldNumber,
    int* wireType,
    quint64* varintValue,
    QByteArray* bytesValue)
{
    quint64 key = 0;
    if (!readVarint(data, offset, &key))
        return false;

    *fieldNumber = static_cast<int>(key >> 3);
    *wireType = static_cast<int>(key & 0x7);
    if (varintValue) {
        *varintValue = 0;
    }
    if (bytesValue) {
        bytesValue->clear();
    }

    switch (*wireType) {
    case 0:
        return readVarint(data, offset, varintValue);
    case 1:
        if (*offset + 8 > data.size())
            return false;
        if (bytesValue) {
            *bytesValue = data.mid(*offset, 8);
        }
        *offset += 8;
        return true;
    case 2: {
        quint64 length = 0;
        if (!readVarint(data, offset, &length) || *offset + static_cast<qsizetype>(length) > data.size())
            return false;
        if (bytesValue) {
            *bytesValue = data.mid(*offset, static_cast<qsizetype>(length));
        }
        *offset += static_cast<qsizetype>(length);
        return true;
    }
    case 5:
        if (*offset + 4 > data.size())
            return false;
        if (bytesValue) {
            *bytesValue = data.mid(*offset, 4);
        }
        *offset += 4;
        return true;
    default:
        return false;
    }
}

float readFloatLE(const QByteArray& bytes)
{
    float value = 0.0f;
    if (bytes.size() >= 4) {
        std::memcpy(&value, bytes.constData(), sizeof(value));
    }
    return value;
}

double readDoubleLE(const QByteArray& bytes)
{
    double value = 0.0;
    if (bytes.size() >= 8) {
        std::memcpy(&value, bytes.constData(), sizeof(value));
    }
    return value;
}

QVariant parseValue(const QByteArray& data)
{
    qsizetype offset = 0;
    while (offset < data.size()) {
        int field = 0;
        int wire = 0;
        quint64 varint = 0;
        QByteArray bytes;
        if (!readField(data, &offset, &field, &wire, &varint, &bytes))
            break;

        switch (field) {
        case 1:
            return QString::fromUtf8(bytes);
        case 2:
            return readFloatLE(bytes);
        case 3:
            return readDoubleLE(bytes);
        case 4:
            return static_cast<qlonglong>(varint);
        case 5:
            return static_cast<qulonglong>(varint);
        case 6:
            return zigZagDecode(varint);
        case 7:
            return varint != 0;
        default:
            break;
        }
    }

    return QVariant();
}

QVector<quint64> readPackedVarints(const QByteArray& data)
{
    QVector<quint64> values;
    qsizetype offset = 0;
    while (offset < data.size()) {
        quint64 value = 0;
        if (!readVarint(data, &offset, &value))
            break;
        values.append(value);
    }
    return values;
}

QVector<QVector<QPointF>> decodeGeometry(const QByteArray& data, int extent)
{
    QVector<QVector<QPointF>> paths;
    const double scale = extent > 0 ? static_cast<double>(kTileImageSize) / extent : 1.0;
    const QVector<quint64> values = readPackedVarints(data);

    qint64 x = 0;
    qint64 y = 0;
    int index = 0;
    QVector<QPointF> path;

    auto flushPath = [&]() {
        if (!path.isEmpty()) {
            paths.append(path);
            path.clear();
        }
    };

    while (index < values.size()) {
        const quint64 commandInteger = values.at(index++);
        const int command = static_cast<int>(commandInteger & 0x7);
        const int count = static_cast<int>(commandInteger >> 3);

        if (command == 1) {
            flushPath();
            for (int i = 0; i < count && index + 1 < values.size(); ++i) {
                x += zigZagDecode(values.at(index++));
                y += zigZagDecode(values.at(index++));
                path.append(QPointF(x * scale, y * scale));
            }
        }
        else if (command == 2) {
            for (int i = 0; i < count && index + 1 < values.size(); ++i) {
                x += zigZagDecode(values.at(index++));
                y += zigZagDecode(values.at(index++));
                path.append(QPointF(x * scale, y * scale));
            }
        }
        else if (command == 7) {
            if (!path.isEmpty() && path.first() != path.last()) {
                path.append(path.first());
            }
        }
        else {
            break;
        }
    }

    flushPath();
    return paths;
}

MvtFeature parseFeature(
    const QByteArray& data,
    const QVector<QString>& keys,
    const QVector<QVariant>& values,
    int extent)
{
    MvtFeature feature;
    QVector<quint64> tagIndices;
    QByteArray geometry;

    qsizetype offset = 0;
    while (offset < data.size()) {
        int field = 0;
        int wire = 0;
        quint64 varint = 0;
        QByteArray bytes;
        if (!readField(data, &offset, &field, &wire, &varint, &bytes))
            break;

        if (field == 2 && wire == 2) {
            tagIndices = readPackedVarints(bytes);
        }
        else if (field == 3 && wire == 0) {
            feature.type = static_cast<int>(varint);
        }
        else if (field == 4 && wire == 2) {
            geometry = bytes;
        }
    }

    for (int i = 0; i + 1 < tagIndices.size(); i += 2) {
        const int keyIndex = static_cast<int>(tagIndices.at(i));
        const int valueIndex = static_cast<int>(tagIndices.at(i + 1));
        if (keyIndex >= 0 && keyIndex < keys.size()
            && valueIndex >= 0 && valueIndex < values.size()) {
            feature.tags.insert(keys.at(keyIndex), values.at(valueIndex));
        }
    }

    feature.paths = decodeGeometry(geometry, extent);
    return feature;
}

MvtLayer parseLayer(const QByteArray& data)
{
    MvtLayer layer;
    QVector<QByteArray> featureBlobs;
    QVector<QString> keys;
    QVector<QVariant> values;

    qsizetype offset = 0;
    while (offset < data.size()) {
        int field = 0;
        int wire = 0;
        quint64 varint = 0;
        QByteArray bytes;
        if (!readField(data, &offset, &field, &wire, &varint, &bytes))
            break;

        if (field == 1 && wire == 2) {
            layer.name = QString::fromUtf8(bytes);
        }
        else if (field == 2 && wire == 2) {
            featureBlobs.append(bytes);
        }
        else if (field == 3 && wire == 2) {
            keys.append(QString::fromUtf8(bytes));
        }
        else if (field == 4 && wire == 2) {
            values.append(parseValue(bytes));
        }
        else if (field == 5 && wire == 0) {
            layer.extent = static_cast<int>(varint);
        }
    }

    layer.features.reserve(featureBlobs.size());
    for (const QByteArray& featureBlob : featureBlobs) {
        layer.features.append(parseFeature(featureBlob, keys, values, layer.extent));
    }

    return layer;
}

QVector<MvtLayer> parseTile(const QByteArray& data)
{
    QVector<MvtLayer> layers;
    qsizetype offset = 0;
    while (offset < data.size()) {
        int field = 0;
        int wire = 0;
        quint64 varint = 0;
        QByteArray bytes;
        if (!readField(data, &offset, &field, &wire, &varint, &bytes))
            break;

        if (field == 3 && wire == 2) {
            MvtLayer layer = parseLayer(bytes);
            if (!layer.name.isEmpty()) {
                layers.append(layer);
            }
        }
    }
    return layers;
}

}

bool MbTilesReader::isVectorFormat(const QString& format)
{
    const QString normalizedFormat = format.trimmed().toLower();
    return normalizedFormat == QStringLiteral("pbf")
        || normalizedFormat == QStringLiteral("mvt")
        || normalizedFormat == QStringLiteral("vector");
}

MbTilesMetadata MbTilesReader::readMetadata(const QString& filePath, QString* errorMessage)
{
    const QHash<QString, QString> metadataMap = readMetadataMap(filePath, errorMessage);
    MbTilesMetadata metadata;
    metadata.valid = !metadataMap.isEmpty();
    metadata.name = metadataMap.value(QStringLiteral("name"), QFileInfo(filePath).completeBaseName());
    metadata.format = metadataMap.value(QStringLiteral("format")).toLower();
    metadata.scheme = metadataMap.value(QStringLiteral("scheme"), QStringLiteral("tms")).toLower();
    metadata.minZoom = metadataMap.value(QStringLiteral("minzoom"), QStringLiteral("0")).toInt();
    metadata.maxZoom = metadataMap.value(QStringLiteral("maxzoom"), QStringLiteral("0")).toInt();
    return metadata;
}

MbTilesVectorTile MbTilesReader::readVectorTile(
    const QString& filePath,
    int z,
    int x,
    int y,
    bool tmsYOrigin,
    QString* errorMessage)
{
    const QByteArray tileData = readTileData(filePath, z, x, y, tmsYOrigin, errorMessage);
    if (tileData.isEmpty())
        return {};

    const QByteArray protobuf = gunzip(tileData);
    if (protobuf.isEmpty()) {
        setError(errorMessage, QStringLiteral("Could not decode vector tile from %1.").arg(QFileInfo(filePath).fileName()));
        return {};
    }

    MbTilesVectorTile tile;
    tile.layers = parseTile(protobuf);
    return tile;
}

QImage MbTilesReader::readTileImage(
    const QString& filePath,
    int z,
    int x,
    int y,
    bool tmsYOrigin,
    const QString& format,
    QString* errorMessage)
{
    if (isVectorFormat(format)) {
        setError(
            errorMessage,
            QStringLiteral("Vector MBTiles source %1 must be rendered as vector geometry, not as a raster tile.")
                .arg(QFileInfo(filePath).fileName()));
        return QImage();
    }

    const QByteArray tileData = readTileData(filePath, z, x, y, tmsYOrigin, errorMessage);
    if (tileData.isEmpty())
        return QImage();

    QImage image;
    if (!image.loadFromData(tileData)) {
        setError(errorMessage, QStringLiteral("Could not decode raster tile from %1.").arg(QFileInfo(filePath).fileName()));
    }
    return image;
}
