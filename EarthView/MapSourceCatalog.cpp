#include "MapSourceCatalog.h"

#include "MbTilesReader.h"
#include "RemoteMbTiles.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

struct TileMapResourceMetadata {
    bool valid = false;
    QString title;
    QString extension = "png";
    int minTileZoom = -1;
    int maxTileZoom = -1;
    bool tmsYOrigin = true;
};

bool hasTilePlaceholders(const QString& value)
{
    return value.contains("{z}") && value.contains("{x}") && value.contains("{y}");
}

QString normalizedText(QString value)
{
    value = value.toLower();
    QString normalized;
    normalized.reserve(value.size());
    for (const QChar& ch : value) {
        if (ch.isLetterOrNumber()) {
            normalized.append(ch);
        }
    }
    return normalized;
}

QString normalizedExtension(QString extension)
{
    extension = extension.trimmed();
    if (extension.startsWith('.')) {
        extension.remove(0, 1);
    }
    return extension.isEmpty() ? QString("png") : extension;
}

QString replaceLiteralTileSegments(QString value)
{
    const QStringList suffixes = {
        "/z/x/y.png",
        "/z/x/y.jpg",
        "/z/x/y.jpeg"
    };

    for (const QString& suffix : suffixes) {
        if (value.endsWith(suffix, Qt::CaseInsensitive)) {
            value.chop(suffix.size());
            const int extensionIndex = suffix.lastIndexOf('.');
            const QString extension = extensionIndex >= 0 ? suffix.mid(extensionIndex) : QString(".png");
            return value + "/{z}/{x}/{y}" + extension;
        }
    }

    return value;
}

QString tileTemplateFromUrl(QString value, const QString& extension = "png")
{
    const QString outputExtension = normalizedExtension(extension);
    value = replaceLiteralTileSegments(value.trimmed());
    if (hasTilePlaceholders(value)) {
        return value;
    }

    if (value.endsWith("/tilemapresource.xml", Qt::CaseInsensitive)) {
        value.chop(QString("/tilemapresource.xml").size());
    }

    while (value.endsWith('/')) {
        value.chop(1);
    }
    return value + "/{z}/{x}/{y}." + outputExtension;
}

int readZoomValue(const QXmlStreamAttributes& attributes, const QStringList& names)
{
    for (const QString& name : names) {
        const QString value = attributes.value(name).toString();
        if (value.isEmpty())
            continue;

        bool ok = false;
        const int zoom = value.toInt(&ok);
        if (ok)
            return zoom;
    }

    return -1;
}

TileMapResourceMetadata readTileMapResourceMetadata(QIODevice* device)
{
    if (!device)
        return {};

    QXmlStreamReader xml(device);
    TileMapResourceMetadata metadata;

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        const QString elementName = xml.name().toString();
        if (elementName == "Title") {
            metadata.title = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        }
        else if (elementName == "TileFormat") {
            const QXmlStreamAttributes attributes = xml.attributes();
            const QString extension = attributes.value("extension").toString();
            if (!extension.isEmpty()) {
                metadata.extension = normalizedExtension(extension);
            }
        }
        else if (elementName == "TileSets") {
            const QString profile = xml.attributes().value("profile").toString();
            metadata.tmsYOrigin = profile.compare("mercator", Qt::CaseInsensitive) == 0
                || profile.compare("global-mercator", Qt::CaseInsensitive) == 0;
        }
        else if (elementName == "TileSet") {
            const QXmlStreamAttributes attributes = xml.attributes();
            bool ok = false;
            int order = attributes.value("order").toInt(&ok);
            if (!ok) {
                order = attributes.value("href").toInt(&ok);
            }
            if (!ok)
                continue;

            metadata.minTileZoom = metadata.minTileZoom < 0
                ? order
                : qMin(metadata.minTileZoom, order);
            metadata.maxTileZoom = metadata.maxTileZoom < 0
                ? order
                : qMax(metadata.maxTileZoom, order);
            metadata.valid = true;
        }
    }

    return metadata;
}

TileMapResourceMetadata readTileMapResourceMetadata(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    return readTileMapResourceMetadata(&file);
}

TileMapResourceMetadata readTileMapResourceMetadataFromUrl(const QString& urlString)
{
    const QUrl url(urlString);
    if (!url.isValid() || url.scheme().isEmpty() || url.isLocalFile())
        return {};

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(750);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return {};
    }

    TileMapResourceMetadata metadata;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QBuffer buffer(&data);
        if (buffer.open(QIODevice::ReadOnly)) {
            metadata = readTileMapResourceMetadata(&buffer);
        }
    }

    reply->deleteLater();
    return metadata;
}

bool tileMapMetadataMatchesUrl(const TileMapResourceMetadata& metadata, const QString& tileMapUrl)
{
    if (metadata.title.isEmpty())
        return false;

    const QString titleKey = normalizedText(QFileInfo(metadata.title).completeBaseName());
    const QString urlKey = normalizedText(QUrl(tileMapUrl).path());
    return titleKey.size() >= 4 && urlKey.contains(titleKey);
}

QStringList tileMapSearchDirectories(const QString& earthFilePath)
{
    QStringList directories;
    const QString appPath = QCoreApplication::applicationDirPath();
    directories << QFileInfo(earthFilePath).absolutePath()
                << QDir::currentPath()
                << appPath
                << QDir(appPath).absoluteFilePath("..");
    directories.removeDuplicates();
    return directories;
}

void applyTileMapResourceMetadata(
    const QString& tileMapUrl,
    const QString& earthFilePath,
    TmsLoader::TileSourceLayer& layer)
{
    if (!tileMapUrl.endsWith("tilemapresource.xml", Qt::CaseInsensitive))
        return;

    const QString urlPath = QUrl(tileMapUrl).path();
    const QString fileName = QFileInfo(urlPath).fileName();
    QString relativeUrlPath = urlPath;
    while (relativeUrlPath.startsWith('/')) {
        relativeUrlPath.remove(0, 1);
    }

    struct Candidate {
        QString filePath;
        bool requireTitleMatch;
    };

    QList<Candidate> candidates;
    for (const QString& directoryPath : tileMapSearchDirectories(earthFilePath)) {
        if (!relativeUrlPath.isEmpty()) {
            candidates.append({ QDir(directoryPath).filePath(relativeUrlPath), false });
        }
        if (!fileName.isEmpty()) {
            candidates.append({ QDir(directoryPath).filePath(fileName), true });
        }
    }

    QSet<QString> seenCandidates;
    for (const Candidate& candidate : candidates) {
        const QFileInfo candidateInfo(candidate.filePath);
        const QString absolutePath = candidateInfo.absoluteFilePath();
        if (seenCandidates.contains(absolutePath) || !candidateInfo.exists())
            continue;
        seenCandidates.insert(absolutePath);

        const TileMapResourceMetadata metadata = readTileMapResourceMetadata(absolutePath);
        if (!metadata.valid)
            continue;
        if (candidate.requireTitleMatch && !tileMapMetadataMatchesUrl(metadata, tileMapUrl))
            continue;

        layer.urlTemplate = tileTemplateFromUrl(tileMapUrl, metadata.extension);
        layer.minTileZoom = metadata.minTileZoom;
        layer.maxTileZoom = metadata.maxTileZoom;
        layer.tmsYOrigin = metadata.tmsYOrigin;
        return;
    }

    const TileMapResourceMetadata metadata = readTileMapResourceMetadataFromUrl(tileMapUrl);
    if (!metadata.valid)
        return;

    layer.urlTemplate = tileTemplateFromUrl(tileMapUrl, metadata.extension);
    layer.minTileZoom = metadata.minTileZoom;
    layer.maxTileZoom = metadata.maxTileZoom;
    layer.tmsYOrigin = metadata.tmsYOrigin;
}

// Vector tiles of OpenMapTiles-style OSM maps stop at z14; deeper views over-zoom them.
constexpr int kDefaultVectorMaxTileZoom = 14;
constexpr int kProbeTimeoutMs = 5000;

bool isVectorExtension(const QString& extension)
{
    const QString e = extension.toLower();
    return e == QLatin1String("pbf") || e == QLatin1String("mvt") || e == QLatin1String("vector");
}

bool isRasterExtension(const QString& extension)
{
    const QString e = extension.toLower();
    return e == QLatin1String("png") || e == QLatin1String("jpg") || e == QLatin1String("jpeg")
        || e == QLatin1String("webp");
}

QString urlPathExtension(const QString& url)
{
    return QFileInfo(QUrl(url).path()).suffix().toLower();
}

bool looksLikeRasterImage(const QByteArray& data)
{
    return data.startsWith("\x89PNG") || data.startsWith("\xFF\xD8") || data.startsWith("RIFF")
        || data.startsWith("GIF8");
}

/// Fills @p layer as a tile-server layer: vector (VectorMbTiles without a file, tiles fetched by
/// URL) or raster (an ordinary URL template layer).
void setTileServerLayer(TmsLoader::TileSourceLayer* layer, const QString& urlTemplate, bool vector,
                        bool tmsYOrigin, int minTileZoom, int maxTileZoom)
{
    layer->urlTemplate = urlTemplate;
    layer->mbTilesPath.clear();
    layer->tmsYOrigin = tmsYOrigin;
    if (vector) {
        layer->sourceType = TmsLoader::TileSourceLayer::SourceType::VectorMbTiles;
        layer->mbTilesFormat = QStringLiteral("pbf");
        layer->minTileZoom = minTileZoom >= 0 ? minTileZoom : 0;
        layer->maxTileZoom = maxTileZoom >= 0 ? maxTileZoom : kDefaultVectorMaxTileZoom;
    }
    else {
        layer->sourceType = TmsLoader::TileSourceLayer::SourceType::UrlTemplate;
        layer->minTileZoom = minTileZoom >= 0 ? minTileZoom : 0;
        layer->maxTileZoom = maxTileZoom >= 0 ? maxTileZoom : GIS::MAX_TILE_ZOOM;
    }
}

bool tileJsonLayer(const QByteArray& json, const QString& jsonUrl, TmsLoader::TileSourceLayer* layer,
                   QString* errorMessage)
{
    const QJsonObject tileJson = QJsonDocument::fromJson(json).object();
    const QJsonArray tiles = tileJson.value(QStringLiteral("tiles")).toArray();
    if (tiles.isEmpty() || !tiles.first().isString()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("%1 is not a TileJSON document (no \"tiles\")").arg(jsonUrl);
        return false;
    }
    // Resolve a relative template against the document; QUrl would percent-encode the braces.
    QString tileUrl = tiles.first().toString();
    if (!RemoteMbTiles::isRemoteUrl(tileUrl)) {
        tileUrl.replace(QLatin1String("{z}"), QLatin1String("__z__"))
            .replace(QLatin1String("{x}"), QLatin1String("__x__"))
            .replace(QLatin1String("{y}"), QLatin1String("__y__"));
        tileUrl = QUrl(jsonUrl).resolved(QUrl(tileUrl)).toString();
        tileUrl.replace(QLatin1String("__z__"), QLatin1String("{z}"))
            .replace(QLatin1String("__x__"), QLatin1String("{x}"))
            .replace(QLatin1String("__y__"), QLatin1String("{y}"));
    }
    QString format = tileJson.value(QStringLiteral("format")).toString();
    if (format.isEmpty())
        format = urlPathExtension(QString(tileUrl).replace(QLatin1String("{z}"), QLatin1String("0")));
    const bool vector = isVectorExtension(format) || tileJson.contains(QStringLiteral("vector_layers"));
    const bool tms = tileJson.value(QStringLiteral("scheme")).toString().compare(QLatin1String("tms"), Qt::CaseInsensitive) == 0;
    setTileServerLayer(layer, tileUrl, vector, tms,
                       tileJson.value(QStringLiteral("minzoom")).toInt(-1),
                       tileJson.value(QStringLiteral("maxzoom")).toInt(-1));
    if (layer->name.isEmpty())
        layer->name = tileJson.value(QStringLiteral("name")).toString();
    layer->vectorLayers = MbTilesReader::vectorLayerIds(json);
    return true;
}

bool tileMapResourceLayer(const QByteArray& xml, const QString& tileMapUrl, TmsLoader::TileSourceLayer* layer,
                          QString* errorMessage)
{
    QByteArray data = xml;
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    const TileMapResourceMetadata metadata = readTileMapResourceMetadata(&buffer);
    if (!metadata.valid) {
        if (errorMessage)
            *errorMessage = QStringLiteral("%1 is not a TMS tilemapresource.xml").arg(tileMapUrl);
        return false;
    }
    const bool vector = isVectorExtension(metadata.extension);
    setTileServerLayer(layer, tileTemplateFromUrl(tileMapUrl, metadata.extension), vector, metadata.tmsYOrigin,
                       metadata.minTileZoom, metadata.maxTileZoom);
    return true;
}
} // namespace

bool MapSourceCatalog::urlMbTilesLayer(const QString& url, TmsLoader::TileSourceLayer* layer, QString* errorMessage)
{
    if (!layer)
        return false;
    QString location = url.trimmed();

    // A tile server's {z}/{x}/{y} template ({-y} = TMS row order).
    if (hasTilePlaceholders(location) || location.contains(QLatin1String("{-y}"))) {
        const bool tms = location.contains(QLatin1String("{-y}"));
        location.replace(QLatin1String("{-y}"), QLatin1String("{y}"));
        const QString extension = urlPathExtension(QString(location).replace(QLatin1String("{z}"), QLatin1String("0")));
        bool vector = isVectorExtension(extension);
        if (!vector && !isRasterExtension(extension)) {
            // No telling extension: look at the world tile.
            QString probeUrl = location;
            probeUrl.replace(QLatin1String("{z}"), QLatin1String("0"))
                .replace(QLatin1String("{x}"), QLatin1String("0"))
                .replace(QLatin1String("{y}"), QLatin1String("0"));
            vector = !looksLikeRasterImage(RemoteMbTiles::httpGet(probeUrl, kProbeTimeoutMs));
        }
        setTileServerLayer(layer, location, vector, tms, -1, -1);
        return true;
    }

    const QString extension = urlPathExtension(location);
    if (extension == QLatin1String("mbtiles") || extension == QLatin1String("mbtile"))
        return mbTilesLayer(location, layer, errorMessage);

    // A document: TMS tilemapresource.xml, TileJSON, or the .mbtiles file under another name.
    int status = 0;
    QString requestError;
    const bool small = location.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)
        || extension == QLatin1String("json");
    const QByteArray head = RemoteMbTiles::httpGet(location, kProbeTimeoutMs, &status, &requestError,
                                                   small ? QByteArray() : QByteArrayLiteral("bytes=0-15"));
    if (!requestError.isEmpty() || (status != 200 && status != 206)) {
        if (errorMessage) {
            *errorMessage = requestError.isEmpty()
                ? QStringLiteral("HTTP %1 for %2").arg(status).arg(location)
                : requestError;
        }
        return false;
    }
    if (head.startsWith(QByteArrayLiteral("SQLite format 3")))
        return mbTilesLayer(location, layer, errorMessage);

    QByteArray document = head;
    if (status == 206) {
        // Only the first bytes came back: fetch the (small) document whole.
        document = RemoteMbTiles::httpGet(location, kProbeTimeoutMs, &status, &requestError);
    }
    const QByteArray trimmed = document.trimmed();
    if (trimmed.startsWith('{'))
        return tileJsonLayer(trimmed, location, layer, errorMessage);
    if (trimmed.startsWith('<'))
        return tileMapResourceLayer(trimmed, location, layer, errorMessage);

    if (errorMessage) {
        *errorMessage = QStringLiteral("%1 is neither an MBTiles file nor a tile service "
                                       "(TileJSON, tilemapresource.xml or a {z}/{x}/{y} URL)").arg(location);
    }
    return false;
}

/**
 * @brief Read the layer list of a 2D .earth file.
 *
 * @p dtedDirectory, when given, receives the folder of an `<elevation driver="dted">` entry:
 * the offline machine's DTED tiles (.dt1/.dt2), e.g.
 * @code
 * <elevation name="DTED" driver="dted"> <url> D:/EarthViewData/DTED </url></elevation>
 * @endcode
 * Only the path is read here (one place to edit per machine); the 2D DEM pipeline still uses
 * its GeoTIFF mirror. earthview3d turns the same entry into an osgEarth GDALElevation layer.
 */
bool MapSourceCatalog::mbTilesLayer(const QString& filePath, TmsLoader::TileSourceLayer* layer, QString* errorMessage)
{
    const MbTilesMetadata metadata = MbTilesReader::readMetadata(filePath, errorMessage);
    if (!metadata.valid || !layer)
        return false;

    layer->sourceType = MbTilesReader::isVectorFormat(metadata.format)
        ? TmsLoader::TileSourceLayer::SourceType::VectorMbTiles
        : TmsLoader::TileSourceLayer::SourceType::MbTiles;
    if (layer->name.isEmpty())
        layer->name = metadata.name.isEmpty() ? QFileInfo(filePath).completeBaseName() : metadata.name;
    layer->mbTilesPath = filePath;
    layer->mbTilesFormat = metadata.format;
    layer->minTileZoom = metadata.minZoom;
    layer->maxTileZoom = metadata.maxZoom;
    layer->tmsYOrigin = metadata.scheme.compare("xyz", Qt::CaseInsensitive) != 0;
    layer->vectorLayers = metadata.vectorLayers;
    return true;
}

bool MapSourceCatalog::isMbTilesSource(const Source& source)
{
    for (const TmsLoader::TileSourceLayer& layer : source.layers) {
        if (layer.sourceType == TmsLoader::TileSourceLayer::SourceType::MbTiles
            || layer.sourceType == TmsLoader::TileSourceLayer::SourceType::VectorMbTiles)
            return true;
    }
    return source.filePath.endsWith(".mbtiles", Qt::CaseInsensitive);
}

QList<TmsLoader::TileSourceLayer> MapSourceCatalog::readEarthLayers(const QString& filePath, QString* dtedDirectory)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString wrappedXml = "<earth>\n" + QString::fromUtf8(file.readAll()) + "\n</earth>";
    QXmlStreamReader xml(wrappedXml);
    QList<TmsLoader::TileSourceLayer> layers;

    TmsLoader::TileSourceLayer currentLayer;
    bool insideImage = false;
    bool skipCurrentImage = false;
    bool insideDted = false;
    bool currentIsMbTiles = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString elementName = xml.name().toString();
            if (elementName == "elevation" || elementName == "heightfield") {
                insideDted = xml.attributes().value("driver").toString()
                                 .compare("dted", Qt::CaseInsensitive) == 0;
            }
            else if (insideDted && elementName == "url") {
                const QString path = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                if (dtedDirectory && !path.isEmpty())
                    *dtedDirectory = path;
            }
            else if (elementName == "image") {
                const QXmlStreamAttributes attributes = xml.attributes();
                const QString driver = attributes.value("driver").toString();
                insideImage = true;
                currentIsMbTiles = driver.compare("mbtiles", Qt::CaseInsensitive) == 0;
                skipCurrentImage = !driver.isEmpty() && !currentIsMbTiles
                    && driver.compare("tms", Qt::CaseInsensitive) != 0;
                currentLayer = TmsLoader::TileSourceLayer();
                currentLayer.name = attributes.value("name").toString();
                // Without the attributes the layer shows at every zoom. (TMS layers get their
                // display range from their tile range in TmsLoader anyway; vector layers use
                // this one as it is, and -1 hid them at every zoom.)
                const int minLevel = readZoomValue(attributes, { "min_level", "minLevel", "min_zoom", "minZoom" });
                const int maxLevel = readZoomValue(attributes, { "max_level", "maxLevel", "max_zoom", "maxZoom" });
                if (minLevel >= 0)
                    currentLayer.minZoom = minLevel;
                if (maxLevel >= 0)
                    currentLayer.maxZoom = maxLevel;
                currentLayer.tmsYOrigin = driver.compare("tms", Qt::CaseInsensitive) == 0;
            }
            else if (insideImage && currentIsMbTiles && elementName == "url") {
                // An MBTiles file on the imagery server's share (UNC / mapped drive), relative to
                // this earth file, or on its web server by URL (the file itself, or a tile service
                // in front of it). Its metadata is read once here; tiles are read on demand.
                QString path = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                QString errorMessage;
                bool loaded = false;
                if (RemoteMbTiles::isRemoteUrl(path)) {
                    loaded = urlMbTilesLayer(path, &currentLayer, &errorMessage);
                    if (loaded && currentLayer.urlTemplate.isEmpty())
                        currentLayer.urlTemplate = path;  // The .mbtiles file itself
                }
                else {
                    if (path.startsWith("file:", Qt::CaseInsensitive))
                        path = QUrl(path).toLocalFile();
                    if (QFileInfo(path).isRelative())
                        path = QFileInfo(filePath).absoluteDir().absoluteFilePath(path);
                    loaded = mbTilesLayer(path, &currentLayer, &errorMessage);
                    if (loaded) {
                        // Marks the layer as complete for the end-of-image check below.
                        currentLayer.urlTemplate = path;
                    }
                }
                if (!loaded) {
                    qWarning() << "Skipping MBTiles layer" << path << "in" << filePath << errorMessage;
                    skipCurrentImage = true;
                }
            }
            else if (insideImage && elementName == "url") {
                const QString rawUrl = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                currentLayer.urlTemplate = tileTemplateFromUrl(rawUrl);
                applyTileMapResourceMetadata(rawUrl, filePath, currentLayer);
            }
            else if (insideImage && (elementName == "min_level" || elementName == "minZoom" || elementName == "min_zoom")) {
                bool ok = false;
                const int zoom = xml.readElementText(QXmlStreamReader::SkipChildElements).toInt(&ok);
                if (ok)
                    currentLayer.minZoom = zoom;
            }
            else if (insideImage && (elementName == "max_level" || elementName == "maxZoom" || elementName == "max_zoom")) {
                bool ok = false;
                const int zoom = xml.readElementText(QXmlStreamReader::SkipChildElements).toInt(&ok);
                if (ok)
                    currentLayer.maxZoom = zoom;
            }
        }
        else if (xml.isEndElement()
                 && (xml.name() == QLatin1String("elevation") || xml.name() == QLatin1String("heightfield"))) {
            insideDted = false;
        }
        else if (xml.isEndElement() && xml.name() == QLatin1String("image")) {
            if (insideImage && !skipCurrentImage && !currentLayer.urlTemplate.isEmpty()) {
                if (currentLayer.name.isEmpty()) {
                    currentLayer.name = QFileInfo(currentLayer.urlTemplate).baseName();
                }
                layers.append(currentLayer);
            }
            insideImage = false;
            skipCurrentImage = false;
            currentIsMbTiles = false;
        }
    }

    return layers;
}

QString MapSourceCatalog::labelForFile(const QFileInfo& fileInfo)
{
    const QString lowerName = fileInfo.completeBaseName().toLower();
    if (lowerName.contains("satellite") || lowerName.contains("imagery"))
        return "Satellite Imagery";
    if (lowerName.contains("cardg") || lowerName.contains("topographic") || lowerName.contains("topo"))
        return "CardG Maps";

    QString label = fileInfo.completeBaseName();
    label.replace('_', ' ');
    return label;
}

QStringList MapSourceCatalog::earthSearchDirectories()
{
    QStringList directories;
    const QString currentPath = QDir::currentPath();
    const QString appPath = QCoreApplication::applicationDirPath();
    directories << QDir(currentPath).absoluteFilePath("Data/EarthFiles")
                << currentPath
                << QDir(appPath).absoluteFilePath("Data/EarthFiles")
                << appPath
                << QDir(appPath).absoluteFilePath("../Data/EarthFiles")
                << QDir(appPath).absoluteFilePath("..");
    directories.removeDuplicates();
    return directories;
}

QList<MapSourceCatalog::Source> MapSourceCatalog::discoverEarthSources()
{
    QList<Source> sources;
    QSet<QString> seenFiles;

    for (const QString& directoryPath : earthSearchDirectories()) {
        const QDir directory(directoryPath);
        const QFileInfoList earthFiles = directory.entryInfoList({ "*.earth" }, QDir::Files, QDir::Name);
        for (const QFileInfo& earthFile : earthFiles) {
            const QString absolutePath = earthFile.absoluteFilePath();
            if (seenFiles.contains(absolutePath))
                continue;
            seenFiles.insert(absolutePath);

            Source source;
            source.label = labelForFile(earthFile);
            source.filePath = absolutePath;
            source.layers = readEarthLayers(absolutePath, &source.dtedDirectory);
            if (!source.layers.isEmpty()) {
                sources.append(source);
            }
        }
    }

    return sources;
}

QStringList MapSourceCatalog::mbTilesSearchDirectories()
{
    QStringList directories;
    const QString currentPath = QDir::currentPath();
    const QString appPath = QCoreApplication::applicationDirPath();
    directories << QDir(currentPath).absoluteFilePath("Data/OSM")
                << QDir(appPath).absoluteFilePath("Data/OSM")
                << QDir(appPath).absoluteFilePath("../Data/OSM");
    directories.removeDuplicates();
    return directories;
}

QList<MapSourceCatalog::Source> MapSourceCatalog::discoverMbTilesSources()
{
    QList<Source> sources;
    QSet<QString> seenFiles;

    for (const QString& directoryPath : mbTilesSearchDirectories()) {
        const QDir directory(directoryPath);
        const QFileInfoList files = directory.entryInfoList({ "*.mbtiles" }, QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            const QString absolutePath = file.absoluteFilePath();
            if (seenFiles.contains(absolutePath))
                continue;
            seenFiles.insert(absolutePath);

            QString errorMessage;
            TmsLoader::TileSourceLayer layer;
            if (!mbTilesLayer(absolutePath, &layer, &errorMessage)) {
                qWarning() << "Skipping MBTiles source" << absolutePath << errorMessage;
                continue;
            }

            Source source;
            source.label = QString("%1 - %2")
                .arg(layer.sourceType == TmsLoader::TileSourceLayer::SourceType::VectorMbTiles
                    ? QStringLiteral("Offline Vector MBTiles")
                    : QStringLiteral("Offline Raster MBTiles"))
                .arg(layer.name);
            source.filePath = absolutePath;
            source.layers = { layer };
            sources.append(source);
        }
    }

    return sources;
}

QList<MapSourceCatalog::Source> MapSourceCatalog::discoverMapSources()
{
    QList<Source> sources = discoverMbTilesSources();
    sources.append(discoverEarthSources());
    return sources;
}

QList<TmsLoader::TileSourceLayer> MapSourceCatalog::featureLayers(const QList<Source>& sources)
{
    for (const Source& source : sources) {
        QList<TmsLoader::TileSourceLayer> layers;
        for (const TmsLoader::TileSourceLayer& layer : source.layers) {
            if (layer.sourceType == TmsLoader::TileSourceLayer::SourceType::VectorMbTiles)
                layers.append(layer);
        }
        if (!layers.isEmpty())
            return layers;
    }
    return {};
}
