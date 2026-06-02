#include "DemLoader.h"
#include "Camera.h"
#include "MercatorProjection.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int kMetadataProbeBytes = 65536;
constexpr int kMetadataWindowBytes = 131072;
constexpr int kMaxMetadataRequestsPerUpdate = 8;
constexpr int kMaxImageRequestsPerUpdate = 8;
constexpr const char* kIndexCatalogPaths[] = {
    "7th/alldem90.vrt.index.bin",
    "10th/alldem90.vrt.index.bin"
};

struct GeoTiffMetadata {
    bool valid = false;
    int width = 0;
    int height = 0;
    QRectF geographicBounds;
};

struct GeoTiffRaster {
    bool valid = false;
    int width = 0;
    int height = 0;
    QVector<float> samples;
    float minElevation = 0.0f;
    float maxElevation = 1.0f;
};

struct TiffByteReader {
    const QByteArray& data;
    qint64 baseOffset = 0;
    const QByteArray* header = nullptr;

    bool canRead(qint64 offset, qint64 size) const
    {
        if (offset >= baseOffset && offset + size <= baseOffset + data.size())
            return true;
        return header && offset >= 0 && offset + size <= header->size();
    }

    unsigned char byteAt(qint64 offset) const
    {
        if (offset >= baseOffset && offset < baseOffset + data.size()) {
            return static_cast<unsigned char>(data.at(static_cast<qsizetype>(offset - baseOffset)));
        }
        if (header && offset >= 0 && offset < header->size()) {
            return static_cast<unsigned char>(header->at(static_cast<qsizetype>(offset)));
        }
        return 0;
    }
};

quint16 readU16(const TiffByteReader& reader, qint64 offset, bool littleEndian)
{
    if (!reader.canRead(offset, 2))
        return 0;

    if (littleEndian)
        return static_cast<quint16>(reader.byteAt(offset) | (reader.byteAt(offset + 1) << 8));
    return static_cast<quint16>((reader.byteAt(offset) << 8) | reader.byteAt(offset + 1));
}

quint32 readU32(const TiffByteReader& reader, qint64 offset, bool littleEndian)
{
    if (!reader.canRead(offset, 4))
        return 0;

    if (littleEndian) {
        return static_cast<quint32>(reader.byteAt(offset))
            | (static_cast<quint32>(reader.byteAt(offset + 1)) << 8)
            | (static_cast<quint32>(reader.byteAt(offset + 2)) << 16)
            | (static_cast<quint32>(reader.byteAt(offset + 3)) << 24);
    }
    return (static_cast<quint32>(reader.byteAt(offset)) << 24)
        | (static_cast<quint32>(reader.byteAt(offset + 1)) << 16)
        | (static_cast<quint32>(reader.byteAt(offset + 2)) << 8)
        | static_cast<quint32>(reader.byteAt(offset + 3));
}

quint64 readBigEndianU64(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 8 > data.size())
        return 0;

    quint64 value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<unsigned char>(data.at(offset + i));
    }
    return value;
}

double readF64(const TiffByteReader& reader, qint64 offset, bool littleEndian)
{
    if (!reader.canRead(offset, 8))
        return 0.0;

    unsigned char bytes[8] = {};
    if (littleEndian) {
        for (int i = 0; i < 8; ++i) {
            bytes[i] = reader.byteAt(offset + i);
        }
    }
    else {
        for (int i = 0; i < 8; ++i) {
            bytes[i] = reader.byteAt(offset + 7 - i);
        }
    }

    double value = 0.0;
    std::memcpy(&value, bytes, sizeof(double));
    return value;
}

float readF32(const TiffByteReader& reader, qint64 offset, bool littleEndian)
{
    if (!reader.canRead(offset, 4))
        return 0.0f;

    unsigned char bytes[4] = {};
    if (littleEndian) {
        for (int i = 0; i < 4; ++i) {
            bytes[i] = reader.byteAt(offset + i);
        }
    }
    else {
        for (int i = 0; i < 4; ++i) {
            bytes[i] = reader.byteAt(offset + 3 - i);
        }
    }

    float value = 0.0f;
    std::memcpy(&value, bytes, sizeof(float));
    return value;
}

int typeByteSize(quint16 type)
{
    switch (type) {
    case 1:
    case 2:
    case 6:
    case 7:
        return 1;
    case 3:
    case 8:
        return 2;
    case 4:
    case 9:
    case 11:
        return 4;
    case 5:
    case 10:
    case 12:
        return 8;
    default:
        return 0;
    }
}

QVector<double> readDoubleTag(
    const TiffByteReader& reader,
    qint64 valueOrOffset,
    quint32 count,
    bool littleEndian,
    bool inlineValue)
{
    QVector<double> values;
    values.reserve(static_cast<int>(count));

    const qint64 start = inlineValue ? valueOrOffset : static_cast<qint64>(valueOrOffset);
    for (quint32 i = 0; i < count; ++i) {
        values.append(readF64(reader, start + static_cast<qint64>(i) * 8, littleEndian));
    }
    return values;
}

quint32 readIntegerTagValue(
    const TiffByteReader& reader,
    quint16 type,
    qint64 valueOffset,
    bool littleEndian)
{
    if (type == 3)
        return readU16(reader, valueOffset, littleEndian);
    if (type == 4)
        return readU32(reader, valueOffset, littleEndian);
    return 0;
}

quint32 readUnsignedTagValueAt(
    const TiffByteReader& reader,
    quint16 type,
    qint64 valueOffset,
    quint32 index,
    bool littleEndian)
{
    const int byteSize = typeByteSize(type);
    if (byteSize <= 0)
        return 0;

    const qint64 offset = valueOffset + static_cast<qint64>(index) * byteSize;
    if (type == 3)
        return readU16(reader, offset, littleEndian);
    if (type == 4)
        return readU32(reader, offset, littleEndian);
    return 0;
}

float readSampleValue(
    const TiffByteReader& reader,
    qint64 offset,
    int bitsPerSample,
    int sampleFormat,
    bool littleEndian)
{
    if (sampleFormat == 3 && bitsPerSample == 32)
        return readF32(reader, offset, littleEndian);
    if (sampleFormat == 2 && bitsPerSample == 16)
        return static_cast<float>(static_cast<qint16>(readU16(reader, offset, littleEndian)));
    if (sampleFormat == 2 && bitsPerSample == 32)
        return static_cast<float>(static_cast<qint32>(readU32(reader, offset, littleEndian)));
    if ((sampleFormat == 1 || sampleFormat == 0) && bitsPerSample == 16)
        return static_cast<float>(readU16(reader, offset, littleEndian));
    if ((sampleFormat == 1 || sampleFormat == 0) && bitsPerSample == 8)
        return static_cast<float>(reader.byteAt(offset));
    return 0.0f;
}

bool readTiffIfdOffset(const QByteArray& data, qint64* ifdOffset)
{
    if (!ifdOffset || data.size() < 8)
        return false;

    const TiffByteReader reader{ data, 0, nullptr };
    const bool littleEndian = reader.byteAt(0) == 'I' && reader.byteAt(1) == 'I';
    const bool bigEndian = reader.byteAt(0) == 'M' && reader.byteAt(1) == 'M';
    if (!littleEndian && !bigEndian)
        return false;

    if (readU16(reader, 2, littleEndian) != 42)
        return false;

    *ifdOffset = static_cast<qint64>(readU32(reader, 4, littleEndian));
    return *ifdOffset > 0;
}

GeoTiffMetadata parseGeoTiffMetadata(
    const QByteArray& data,
    qint64 baseOffset = 0,
    const QByteArray& header = QByteArray())
{
    GeoTiffMetadata metadata;
    const QByteArray* headerPtr = header.isEmpty() ? nullptr : &header;
    const TiffByteReader reader{ data, baseOffset, headerPtr };
    if (!reader.canRead(0, 8))
        return metadata;

    const bool littleEndian = reader.byteAt(0) == 'I' && reader.byteAt(1) == 'I';
    const bool bigEndian = reader.byteAt(0) == 'M' && reader.byteAt(1) == 'M';
    if (!littleEndian && !bigEndian)
        return metadata;

    const quint16 magic = readU16(reader, 2, littleEndian);
    if (magic != 42)
        return metadata;

    const qint64 ifdOffset = static_cast<qint64>(readU32(reader, 4, littleEndian));
    if (!reader.canRead(ifdOffset, 2))
        return metadata;

    const quint16 entryCount = readU16(reader, ifdOffset, littleEndian);
    const qint64 entriesStart = ifdOffset + 2;
    QVector<double> pixelScale;
    QVector<double> tiePoints;

    for (quint16 i = 0; i < entryCount; ++i) {
        const qint64 entryOffset = entriesStart + static_cast<qint64>(i) * 12;
        if (!reader.canRead(entryOffset, 12))
            break;

        const quint16 tag = readU16(reader, entryOffset, littleEndian);
        const quint16 type = readU16(reader, entryOffset + 2, littleEndian);
        const quint32 count = readU32(reader, entryOffset + 4, littleEndian);
        const quint32 valueOrOffset = readU32(reader, entryOffset + 8, littleEndian);
        const int byteSize = typeByteSize(type);
        if (byteSize <= 0)
            continue;

        const qint64 totalBytes = static_cast<qint64>(byteSize) * count;
        const bool inlineValue = totalBytes <= 4;
        const qint64 valueOffset = inlineValue ? entryOffset + 8 : static_cast<qint64>(valueOrOffset);
        if (!reader.canRead(valueOffset, totalBytes))
            continue;

        if (tag == 256) {
            metadata.width = static_cast<int>(readIntegerTagValue(reader, type, valueOffset, littleEndian));
        }
        else if (tag == 257) {
            metadata.height = static_cast<int>(readIntegerTagValue(reader, type, valueOffset, littleEndian));
        }
        else if (tag == 33550 && type == 12) {
            pixelScale = readDoubleTag(reader, valueOffset, count, littleEndian, true);
        }
        else if (tag == 33922 && type == 12) {
            tiePoints = readDoubleTag(reader, valueOffset, count, littleEndian, true);
        }
    }

    if (metadata.width <= 0 || metadata.height <= 0 || pixelScale.size() < 2 || tiePoints.size() < 6)
        return metadata;

    const double rasterX = tiePoints.at(0);
    const double rasterY = tiePoints.at(1);
    const double modelX = tiePoints.at(3);
    const double modelY = tiePoints.at(4);
    const double scaleX = pixelScale.at(0);
    const double scaleY = pixelScale.at(1);

    const double west = modelX - rasterX * scaleX;
    const double north = modelY + rasterY * scaleY;
    const double east = west + metadata.width * scaleX;
    const double south = north - metadata.height * scaleY;

    metadata.geographicBounds = QRectF(
        QPointF(qMin(west, east), qMax(north, south)),
        QPointF(qMax(west, east), qMin(north, south)));
    metadata.valid = std::isfinite(west)
        && std::isfinite(east)
        && std::isfinite(north)
        && std::isfinite(south)
        && !qFuzzyCompare(west, east)
        && !qFuzzyCompare(north, south);
    return metadata;
}

GeoTiffRaster parseGeoTiffRaster(const QByteArray& data)
{
    GeoTiffRaster raster;
    const TiffByteReader reader{ data, 0, nullptr };
    if (!reader.canRead(0, 8))
        return raster;

    const bool littleEndian = reader.byteAt(0) == 'I' && reader.byteAt(1) == 'I';
    const bool bigEndian = reader.byteAt(0) == 'M' && reader.byteAt(1) == 'M';
    if (!littleEndian && !bigEndian)
        return raster;
    if (readU16(reader, 2, littleEndian) != 42)
        return raster;

    const qint64 ifdOffset = static_cast<qint64>(readU32(reader, 4, littleEndian));
    if (!reader.canRead(ifdOffset, 2))
        return raster;

    int width = 0;
    int height = 0;
    int bitsPerSample = 0;
    int sampleFormat = 1;
    int compression = 1;
    int samplesPerPixel = 1;
    int rowsPerStrip = 0;
    qint64 stripOffsetsValueOffset = -1;
    qint64 stripByteCountsValueOffset = -1;
    quint32 stripCount = 0;
    quint16 stripOffsetsType = 0;
    quint16 stripByteCountsType = 0;
    QString noDataText;

    const quint16 entryCount = readU16(reader, ifdOffset, littleEndian);
    const qint64 entriesStart = ifdOffset + 2;
    for (quint16 i = 0; i < entryCount; ++i) {
        const qint64 entryOffset = entriesStart + static_cast<qint64>(i) * 12;
        if (!reader.canRead(entryOffset, 12))
            break;

        const quint16 tag = readU16(reader, entryOffset, littleEndian);
        const quint16 type = readU16(reader, entryOffset + 2, littleEndian);
        const quint32 count = readU32(reader, entryOffset + 4, littleEndian);
        const quint32 valueOrOffset = readU32(reader, entryOffset + 8, littleEndian);
        const int byteSize = typeByteSize(type);
        if (byteSize <= 0)
            continue;

        const qint64 totalBytes = static_cast<qint64>(byteSize) * count;
        const bool inlineValue = totalBytes <= 4;
        const qint64 valueOffset = inlineValue ? entryOffset + 8 : static_cast<qint64>(valueOrOffset);
        if (!reader.canRead(valueOffset, totalBytes))
            continue;

        switch (tag) {
        case 256:
            width = static_cast<int>(readUnsignedTagValueAt(reader, type, valueOffset, 0, littleEndian));
            break;
        case 257:
            height = static_cast<int>(readUnsignedTagValueAt(reader, type, valueOffset, 0, littleEndian));
            break;
        case 258:
            bitsPerSample = static_cast<int>(readUnsignedTagValueAt(reader, type, valueOffset, 0, littleEndian));
            break;
        case 259:
            compression = static_cast<int>(readUnsignedTagValueAt(reader, type, valueOffset, 0, littleEndian));
            break;
        case 273:
            stripOffsetsType = type;
            stripOffsetsValueOffset = valueOffset;
            stripCount = count;
            break;
        case 277:
            samplesPerPixel = static_cast<int>(readUnsignedTagValueAt(reader, type, valueOffset, 0, littleEndian));
            break;
        case 278:
            rowsPerStrip = static_cast<int>(readUnsignedTagValueAt(reader, type, valueOffset, 0, littleEndian));
            break;
        case 279:
            stripByteCountsType = type;
            stripByteCountsValueOffset = valueOffset;
            break;
        case 339:
            sampleFormat = static_cast<int>(readUnsignedTagValueAt(reader, type, valueOffset, 0, littleEndian));
            break;
        case 42113:
            if (type == 2 && count > 0) {
                QByteArray text;
                text.reserve(static_cast<int>(count));
                for (quint32 j = 0; j < count; ++j) {
                    const char ch = static_cast<char>(reader.byteAt(valueOffset + j));
                    if (ch == '\0')
                        break;
                    text.append(ch);
                }
                noDataText = QString::fromLatin1(text).trimmed();
            }
            break;
        default:
            break;
        }
    }

    if (width <= 0 || height <= 0 || compression != 1 || samplesPerPixel != 1
        || rowsPerStrip <= 0 || stripCount == 0
        || stripOffsetsValueOffset < 0 || stripByteCountsValueOffset < 0) {
        return raster;
    }

    const bool supportedSample =
        (sampleFormat == 3 && bitsPerSample == 32)
        || (sampleFormat == 2 && (bitsPerSample == 16 || bitsPerSample == 32))
        || ((sampleFormat == 1 || sampleFormat == 0) && (bitsPerSample == 8 || bitsPerSample == 16));
    if (!supportedSample)
        return raster;

    raster.samples.resize(width * height);
    float minValue = std::numeric_limits<float>::max();
    float maxValue = std::numeric_limits<float>::lowest();
    bool okNoData = false;
    const float noDataValue = noDataText.toFloat(&okNoData);
    const int bytesPerSample = bitsPerSample / 8;
    int row = 0;

    for (quint32 strip = 0; strip < stripCount && row < height; ++strip) {
        const quint32 stripOffset = readUnsignedTagValueAt(
            reader,
            stripOffsetsType,
            stripOffsetsValueOffset,
            strip,
            littleEndian);
        const quint32 stripByteCount = readUnsignedTagValueAt(
            reader,
            stripByteCountsType,
            stripByteCountsValueOffset,
            strip,
            littleEndian);
        if (stripOffset == 0 || stripByteCount == 0)
            continue;

        const int rowsInStrip = qMin(rowsPerStrip, height - row);
        const int expectedSamples = rowsInStrip * width;
        const int availableSamples = qMin(expectedSamples, static_cast<int>(stripByteCount / bytesPerSample));
        for (int i = 0; i < availableSamples; ++i) {
            const float value = readSampleValue(
                reader,
                static_cast<qint64>(stripOffset) + static_cast<qint64>(i) * bytesPerSample,
                bitsPerSample,
                sampleFormat,
                littleEndian);
            const int sampleIndex = row * width + i;
            const bool isNoData = !std::isfinite(value)
                || (okNoData && std::abs(value - noDataValue) <= 0.001f);
            if (isNoData) {
                raster.samples[sampleIndex] = std::numeric_limits<float>::quiet_NaN();
            }
            else {
                raster.samples[sampleIndex] = value;
                minValue = std::min(minValue, value);
                maxValue = std::max(maxValue, value);
            }
        }

        for (int i = availableSamples; i < expectedSamples; ++i) {
            raster.samples[row * width + i] = std::numeric_limits<float>::quiet_NaN();
        }
        row += rowsInStrip;
    }

    if (!std::isfinite(minValue) || !std::isfinite(maxValue) || minValue >= maxValue)
        return raster;

    for (float& value : raster.samples) {
        if (!std::isfinite(value)) {
            value = minValue;
        }
    }

    raster.valid = true;
    raster.width = width;
    raster.height = height;
    raster.minElevation = minValue;
    raster.maxElevation = maxValue;
    return raster;
}

QRectF geographicToMercatorBounds(const QRectF& geographicBounds)
{
    const QPointF topLeft = MercatorProjection::latLonToMercator(
        geographicBounds.top(),
        geographicBounds.left());
    const QPointF bottomRight = MercatorProjection::latLonToMercator(
        geographicBounds.bottom(),
        geographicBounds.right());
    return QRectF(topLeft, bottomRight);
}

bool mercatorBoundsIntersects(const QRectF& a, const QRectF& b)
{
    const double aLeft = qMin(a.left(), a.right());
    const double aRight = qMax(a.left(), a.right());
    const double aBottom = qMin(a.top(), a.bottom());
    const double aTop = qMax(a.top(), a.bottom());
    const double bLeft = qMin(b.left(), b.right());
    const double bRight = qMax(b.left(), b.right());
    const double bBottom = qMin(b.top(), b.bottom());
    const double bTop = qMax(b.top(), b.bottom());

    if (!std::isfinite(aLeft) || !std::isfinite(aRight) || !std::isfinite(aBottom) || !std::isfinite(aTop)
        || !std::isfinite(bLeft) || !std::isfinite(bRight) || !std::isfinite(bBottom) || !std::isfinite(bTop)) {
        return false;
    }

    return aLeft <= bRight
        && aRight >= bLeft
        && aBottom <= bTop
        && aTop >= bBottom;
}

QUrl normalizedBaseUrl(const QString& baseUrl)
{
    QString value = baseUrl.trimmed();
    if (value.isEmpty())
        return QUrl();
    if (!value.contains("://")) {
        value.prepend(QStringLiteral("http://"));
    }
    if (!value.endsWith('/')) {
        value.append('/');
    }
    return QUrl(value);
}

bool isPrintablePathByte(unsigned char value)
{
    return value >= 32 && value <= 126;
}

void appendTiffReferencesFromText(const QString& text, QSet<QString>* references)
{
    if (!references || !text.contains(QStringLiteral(".tif"), Qt::CaseInsensitive))
        return;

    const QRegularExpression tiffPattern(
        QStringLiteral("([A-Za-z0-9_./\\\\:%+\\-]+\\.tif(?:f)?)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = tiffPattern.globalMatch(text);
    while (it.hasNext()) {
        const QString reference = it.next().captured(1);
        references->insert(reference);
    }
}

QSet<QString> extractTiffReferences(const QByteArray& data)
{
    QSet<QString> references;
    QByteArray run;
    run.reserve(256);

    auto flushRun = [&]() {
        if (!run.isEmpty()) {
            appendTiffReferencesFromText(QString::fromLatin1(run), &references);
            run.clear();
        }
    };

    for (unsigned char value : data) {
        if (isPrintablePathByte(value)) {
            run.append(static_cast<char>(value));
        }
        else {
            flushRun();
        }
    }
    flushRun();

    for (int offset = 0; offset < 2; ++offset) {
        run.clear();
        for (qsizetype i = offset; i + 1 < data.size(); i += 2) {
            const unsigned char value = static_cast<unsigned char>(data.at(i));
            const unsigned char high = static_cast<unsigned char>(data.at(i + 1));
            if (high == 0 && isPrintablePathByte(value)) {
                run.append(static_cast<char>(value));
            }
            else {
                flushRun();
            }
        }
        flushRun();
    }

    return references;
}

bool parseGdalVrtIndexHeader(const QByteArray& data, quint64* recordCount)
{
    if (recordCount)
        *recordCount = 0;
    if (data.size() < 16)
        return false;

    const quint64 version = readBigEndianU64(data, 0);
    const quint64 count = readBigEndianU64(data, 8);
    constexpr qsizetype recordSize = 29;
    if (version != 1 || count == 0)
        return false;
    if ((data.size() - 16) % recordSize != 0)
        return false;
    if (static_cast<quint64>((data.size() - 16) / recordSize) != count)
        return false;

    if (recordCount)
        *recordCount = count;
    return true;
}

QUrl localDirectoryUrl(const QString& path)
{
    QDir dir(path);
    if (!dir.exists())
        return QUrl();
    return QUrl::fromLocalFile(dir.absolutePath() + QLatin1Char('/'));
}
}

DemLoader::DemLoader(Camera* camera, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_pendingCatalogRequests(0)
    , m_catalogGeneration(0)
    , m_enabled(true)
    , m_catalogRequested(false)
    , m_catalogReady(false)
    , m_directoryCatalogRequested(false)
{
}

void DemLoader::setBaseUrl(const QString& baseUrl)
{
    const QUrl url = normalizedBaseUrl(baseUrl);
    if (m_baseUrl == url)
        return;

    clear();
    m_baseUrl = url;
    if (m_enabled) {
        fetchCatalog();
    }
}

void DemLoader::setIndexBaseUrl(const QString& indexBaseUrl)
{
    const QUrl url = normalizedBaseUrl(indexBaseUrl);
    if (m_indexBaseUrl == url)
        return;

    clear();
    m_indexBaseUrl = url;
    if (m_enabled && m_baseUrl.isValid()) {
        fetchCatalog();
    }
}

void DemLoader::setLocalMirrorPath(const QString& demRootPath)
{
    QDir root(demRootPath);
    if (!root.exists())
        return;

    const QUrl tileBaseUrl = localDirectoryUrl(root.absoluteFilePath(QStringLiteral("DEM90TIF")));
    const QUrl indexBaseUrl = localDirectoryUrl(root.absoluteFilePath(QStringLiteral("indexes")));
    if (m_localTileBaseUrl == tileBaseUrl && m_localIndexBaseUrl == indexBaseUrl)
        return;

    clear();
    m_localTileBaseUrl = tileBaseUrl;
    m_localIndexBaseUrl = indexBaseUrl;
    if (m_enabled && m_baseUrl.isValid()) {
        fetchCatalog();
    }
}

void DemLoader::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    if (m_enabled) {
        fetchCatalog();
        updateVisibleTiles();
    }
}

void DemLoader::clear()
{
    ++m_catalogGeneration;
    m_tiles.clear();
    m_pendingMetadata.clear();
    m_pendingImages.clear();
    m_pendingCatalogRequests = 0;
    m_catalogRequested = false;
    m_catalogReady = false;
    m_directoryCatalogRequested = false;
}

void DemLoader::fetchCatalog()
{
    if (!m_enabled || !m_baseUrl.isValid() || m_catalogRequested || m_catalogReady)
        return;

    m_catalogRequested = true;
    m_directoryCatalogRequested = false;
    m_pendingCatalogRequests = 0;
    const int generation = m_catalogGeneration;

    const QUrl catalogIndexBaseUrl = m_localIndexBaseUrl.isValid() ? m_localIndexBaseUrl : m_indexBaseUrl;
    if (catalogIndexBaseUrl.isValid()) {
        for (const char* path : kIndexCatalogPaths) {
            requestCatalogUrl(catalogIndexBaseUrl.resolved(QUrl(QString::fromLatin1(path))), true, generation);
        }
    }

    if (m_pendingCatalogRequests == 0) {
        fetchDirectoryCatalog(generation);
    }
}

void DemLoader::fetchDirectoryCatalog(int generation)
{
    if (m_directoryCatalogRequested || !m_baseUrl.isValid())
        return;

    m_directoryCatalogRequested = true;
    requestCatalogUrl(m_baseUrl, false, generation);
}

void DemLoader::requestCatalogUrl(const QUrl& url, bool isIndexCatalog, int generation)
{
    if (!url.isValid())
        return;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "EarthView/1.0");
    QNetworkReply* reply = m_networkManager->get(request);
    ++m_pendingCatalogRequests;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QUrl catalogUrl = reply->url();
        const bool isIndexCatalog = reply->property("earthviewDemIndexCatalog").toBool();
        const int generation = reply->property("earthviewDemCatalogGeneration").toInt();
        onCatalogDownloaded(reply, catalogUrl, isIndexCatalog, generation);
    });
    reply->setProperty("earthviewDemIndexCatalog", isIndexCatalog);
    reply->setProperty("earthviewDemCatalogGeneration", generation);
}

void DemLoader::finishCatalogRequests(int generation)
{
    if (generation != m_catalogGeneration || m_pendingCatalogRequests > 0)
        return;

    if (m_tiles.isEmpty() && m_localTileBaseUrl.isValid()) {
        const int added = scanLocalTileDirectory();
        if (added > 0) {
            qDebug() << "DEM local mirror added" << added << "sample TIFF references";
        }
    }

    if (m_tiles.isEmpty() && !m_directoryCatalogRequested) {
        qWarning() << "DEM index catalogs did not expose TIFF references; falling back to directory listing";
        fetchDirectoryCatalog(generation);
        return;
    }

    m_catalogRequested = false;
    m_catalogReady = true;
    qDebug() << "DEM catalog ready with" << m_tiles.size() << "TIFF files";
    emit catalogUpdated();
    updateVisibleTiles();
}

void DemLoader::onCatalogDownloaded(QNetworkReply* reply, const QUrl& catalogUrl, bool isIndexCatalog, int generation)
{
    if (!reply)
        return;

    if (generation != m_catalogGeneration) {
        reply->deleteLater();
        return;
    }

    m_pendingCatalogRequests = qMax(0, m_pendingCatalogRequests - 1);

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        const int added = isIndexCatalog
            ? parseIndexCatalog(data, catalogUrl)
            : parseCatalog(data);
        qDebug() << "DEM catalog source" << catalogUrl << "added" << added << "TIFF references";
    }
    else {
        qWarning() << "DEM catalog request failed for" << catalogUrl << reply->errorString();
    }
    reply->deleteLater();
    finishCatalogRequests(generation);
}

int DemLoader::parseCatalog(const QByteArray& html)
{
    const QString text = QString::fromUtf8(html);
    const QRegularExpression linkPattern(
        QStringLiteral("href\\s*=\\s*[\"']([^\"']+\\.tif(?:f)?)[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    int added = 0;
    QRegularExpressionMatchIterator it = linkPattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (addTileReference(match.captured(1), m_baseUrl, false)) {
            ++added;
        }
    }
    return added;
}

int DemLoader::parseIndexCatalog(const QByteArray& data, const QUrl& indexUrl)
{
    quint64 indexRecordCount = 0;
    if (parseGdalVrtIndexHeader(data, &indexRecordCount)) {
        qDebug() << "DEM VRT index" << indexUrl << "contains" << indexRecordCount << "spatial records";
    }

    const QSet<QString> references = extractTiffReferences(data);
    int added = 0;
    for (const QString& reference : references) {
        if (addTileReference(reference, indexUrl, true)) {
            ++added;
        }
    }
    return added;
}

bool DemLoader::addTileReference(const QString& reference, const QUrl& catalogUrl, bool forceTileBaseUrl)
{
    QString cleaned = reference.trimmed();
    if (cleaned.isEmpty())
        return false;

    cleaned.replace('\\', '/');
    if (cleaned.startsWith(QStringLiteral("/vsicurl/"), Qt::CaseInsensitive)) {
        cleaned.remove(0, QStringLiteral("/vsicurl/").size());
    }

    QUrl url;
    QUrl localUrl;
    const QUrl directUrl(cleaned);
    if (directUrl.isValid() && !directUrl.scheme().isEmpty()
        && (directUrl.scheme() == QStringLiteral("http") || directUrl.scheme() == QStringLiteral("https"))) {
        url = directUrl;
    }
    else if (!forceTileBaseUrl) {
        url = catalogUrl.resolved(QUrl(cleaned));
    }
    else {
        const int tileDirIndex = cleaned.lastIndexOf(QStringLiteral("/dem90tif/"), -1, Qt::CaseInsensitive);
        if (tileDirIndex >= 0) {
            cleaned = cleaned.mid(tileDirIndex + QStringLiteral("/dem90tif/").size());
        }

        const QString fileName = QFileInfo(cleaned).fileName();
        if (fileName.isEmpty())
            return false;

        url = m_baseUrl.resolved(QUrl(fileName));
    }

    const QString key = url.fileName();
    if (key.isEmpty() || m_tiles.contains(key))
        return false;

    if (m_localTileBaseUrl.isValid()) {
        const QUrl candidateLocalUrl = m_localTileBaseUrl.resolved(QUrl(key));
        if (QFileInfo::exists(candidateLocalUrl.toLocalFile())) {
            localUrl = candidateLocalUrl;
        }
    }

    DemTile tile;
    tile.key = key;
    tile.url = url;
    tile.localUrl = localUrl;
    m_tiles.insert(key, tile);
    return true;
}

int DemLoader::scanLocalTileDirectory()
{
    if (!m_localTileBaseUrl.isValid() || !m_localTileBaseUrl.isLocalFile())
        return 0;

    QDir dir(m_localTileBaseUrl.toLocalFile());
    const QStringList filters = { QStringLiteral("*.tif"), QStringLiteral("*.tiff") };
    const QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name);
    int added = 0;
    for (const QFileInfo& file : files) {
        if (addTileReference(file.fileName(), m_localTileBaseUrl, true)) {
            ++added;
        }
    }
    return added;
}

QUrl DemLoader::sourceUrlForTile(const DemTile& tile) const
{
    return tile.localUrl.isValid() ? tile.localUrl : tile.url;
}

void DemLoader::requestMetadata(const QString& key)
{
    auto it = m_tiles.find(key);
    if (it == m_tiles.end() || it->metadataReady || it->metadataLoading)
        return;

    const QUrl sourceUrl = sourceUrlForTile(*it);
    if (sourceUrl.isLocalFile()) {
        it->metadataLoading = true;
        m_pendingMetadata.insert(key);

        QFile file(sourceUrl.toLocalFile());
        if (file.open(QIODevice::ReadOnly)) {
            const GeoTiffMetadata metadata = parseGeoTiffMetadata(file.readAll());
            if (metadata.valid) {
                it->geographicBounds = metadata.geographicBounds;
                it->mercatorBounds = geographicToMercatorBounds(metadata.geographicBounds);
                it->metadataReady = true;
                qDebug() << "DEM metadata loaded from local mirror" << sourceUrl.toLocalFile()
                         << metadata.geographicBounds;
                updateVisibleTiles();
            }
            else {
                qWarning() << "Could not read GeoTIFF bounds from local DEM" << sourceUrl.toLocalFile();
                emit demTileFailed(key);
            }
        }
        else {
            qWarning() << "Could not open local DEM metadata" << sourceUrl.toLocalFile() << file.errorString();
            emit demTileFailed(key);
        }

        it->metadataLoading = false;
        m_pendingMetadata.remove(key);
        return;
    }

    QNetworkRequest request(sourceUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "EarthView/1.0");
    request.setRawHeader("Range", QByteArray("bytes=0-") + QByteArray::number(kMetadataProbeBytes - 1));
    QNetworkReply* reply = m_networkManager->get(request);
    reply->setProperty("earthviewDemMetadataBaseOffset", 0);
    it->metadataLoading = true;
    m_pendingMetadata.insert(key);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        onMetadataDownloaded(reply, key);
    });
}

void DemLoader::requestMetadataWindow(const QString& key, const QByteArray& header, qint64 ifdOffset)
{
    auto it = m_tiles.find(key);
    if (it == m_tiles.end())
        return;

    const QUrl sourceUrl = sourceUrlForTile(*it);
    QNetworkRequest request(sourceUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "EarthView/1.0");
    request.setRawHeader(
        "Range",
        QByteArray("bytes=")
            + QByteArray::number(ifdOffset)
            + QByteArray("-")
            + QByteArray::number(ifdOffset + kMetadataWindowBytes - 1));

    QNetworkReply* reply = m_networkManager->get(request);
    reply->setProperty("earthviewDemMetadataBaseOffset", ifdOffset);
    reply->setProperty("earthviewDemMetadataHeader", header);
    it->metadataLoading = true;
    m_pendingMetadata.insert(key);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        onMetadataDownloaded(reply, key);
    });
}

void DemLoader::onMetadataDownloaded(QNetworkReply* reply, const QString& key)
{
    m_pendingMetadata.remove(key);
    auto it = m_tiles.find(key);
    if (it == m_tiles.end()) {
        reply->deleteLater();
        return;
    }

    it->metadataLoading = false;
    if (reply->error() == QNetworkReply::NoError) {
        const qint64 metadataBaseOffset = reply->property("earthviewDemMetadataBaseOffset").toLongLong();
        const QByteArray metadataHeader = reply->property("earthviewDemMetadataHeader").toByteArray();
        const QByteArray metadataBytes = reply->readAll();
        const bool serverReturnedFullFile = metadataBaseOffset > 0
            && (metadataBytes.startsWith("II") || metadataBytes.startsWith("MM"));
        const GeoTiffMetadata metadata = serverReturnedFullFile
            ? parseGeoTiffMetadata(metadataBytes)
            : parseGeoTiffMetadata(metadataBytes, metadataBaseOffset, metadataHeader);
        if (metadata.valid) {
            it->geographicBounds = metadata.geographicBounds;
            it->mercatorBounds = geographicToMercatorBounds(metadata.geographicBounds);
            it->metadataReady = true;
            updateVisibleTiles();
        }
        else {
            qint64 ifdOffset = 0;
            if (metadataBaseOffset == 0
                && readTiffIfdOffset(metadataBytes, &ifdOffset)
                && ifdOffset >= metadataBytes.size()) {
                requestMetadataWindow(key, metadataBytes.left(16), ifdOffset);
                reply->deleteLater();
                return;
            }
            qWarning() << "Could not read GeoTIFF bounds from" << key;
        }
    }
    else {
        qWarning() << "DEM metadata request failed for" << key << reply->errorString();
        emit demTileFailed(key);
    }
    reply->deleteLater();
}

void DemLoader::requestImage(const QString& key)
{
    auto it = m_tiles.find(key);
    if (it == m_tiles.end() || it->imageReady || it->imageLoading)
        return;

    const QUrl sourceUrl = sourceUrlForTile(*it);
    if (sourceUrl.isLocalFile()) {
        it->imageLoading = true;
        m_pendingImages.insert(key);

        QFile file(sourceUrl.toLocalFile());
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            const GeoTiffRaster raster = parseGeoTiffRaster(bytes);
            if (raster.valid) {
                it->elevationSamples = raster.samples;
                it->elevationWidth = raster.width;
                it->elevationHeight = raster.height;
                it->minElevation = raster.minElevation;
                it->maxElevation = raster.maxElevation;
            }
            else {
                QImage image;
                if (image.loadFromData(bytes)) {
                    it->image = image;
                }
                else {
                    qWarning() << "Could not decode local DEM TIFF image" << sourceUrl.toLocalFile();
                    emit demTileFailed(key);
                    it->imageLoading = false;
                    m_pendingImages.remove(key);
                    return;
                }
            }

            it->imageReady = true;
            qDebug() << "DEM image loaded from local mirror" << sourceUrl.toLocalFile()
                     << QSize(it->elevationWidth, it->elevationHeight)
                     << "elevation range" << it->minElevation << it->maxElevation;
            emit demTileLoaded(key);
        }
        else {
            qWarning() << "Could not open local DEM TIFF image" << sourceUrl.toLocalFile() << file.errorString();
            emit demTileFailed(key);
        }

        it->imageLoading = false;
        m_pendingImages.remove(key);
        return;
    }

    QNetworkRequest request(sourceUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "EarthView/1.0");
    QNetworkReply* reply = m_networkManager->get(request);
    it->imageLoading = true;
    m_pendingImages.insert(key);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        onImageDownloaded(reply, key);
    });
}

void DemLoader::onImageDownloaded(QNetworkReply* reply, const QString& key)
{
    m_pendingImages.remove(key);
    auto it = m_tiles.find(key);
    if (it == m_tiles.end()) {
        reply->deleteLater();
        return;
    }

    it->imageLoading = false;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        const GeoTiffRaster raster = parseGeoTiffRaster(bytes);
        if (raster.valid) {
            it->elevationSamples = raster.samples;
            it->elevationWidth = raster.width;
            it->elevationHeight = raster.height;
            it->minElevation = raster.minElevation;
            it->maxElevation = raster.maxElevation;
            it->imageReady = true;
            emit demTileLoaded(key);
        }
        else {
        QImage image;
        if (image.loadFromData(bytes)) {
            it->image = image;
            it->imageReady = true;
            emit demTileLoaded(key);
        }
        else {
            qWarning() << "Could not decode DEM TIFF image" << key;
            emit demTileFailed(key);
        }
        }
    }
    else {
        qWarning() << "DEM image request failed for" << key << reply->errorString();
        emit demTileFailed(key);
    }
    reply->deleteLater();
}

bool DemLoader::visibleIntersects(const QRectF& mercatorBounds) const
{
    if (!m_camera || mercatorBounds.isNull())
        return false;

    const QRectF visible = m_camera->getVisibleMercatorExtent();
    return mercatorBoundsIntersects(visible, mercatorBounds);
}

void DemLoader::updateVisibleTiles()
{
    if (!m_enabled || !m_camera)
        return;

    if (!m_catalogReady) {
        fetchCatalog();
        return;
    }

    int metadataRequests = 0;
    int imageRequests = 0;
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (!it->metadataReady && !it->metadataLoading && metadataRequests < kMaxMetadataRequestsPerUpdate) {
            requestMetadata(it.key());
            ++metadataRequests;
            continue;
        }

        if (it->metadataReady
            && visibleIntersects(it->mercatorBounds)
            && !it->imageReady
            && !it->imageLoading
            && imageRequests < kMaxImageRequestsPerUpdate) {
            requestImage(it.key());
            ++imageRequests;
        }
    }
}
