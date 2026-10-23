#include "BaseMapSources.h"

#include <QFileInfo>
#include <QVariantMap>

namespace {
bool nameOrLabelContains(const MapSourceCatalog::Source& source, std::initializer_list<const char*> words)
{
    const QString name = QFileInfo(source.filePath).completeBaseName().toLower();
    const QString label = source.label.toLower();
    for (const char* word : words) {
        const QLatin1String w(word);
        if (name.contains(w) || label.contains(w))
            return true;
    }
    return false;
}
}

bool BaseMapSources::isMbTiles(const MapSourceCatalog::Source& source)
{
    return source.filePath.endsWith(QLatin1String(".mbtiles"), Qt::CaseInsensitive);
}

bool BaseMapSources::isImagery(const MapSourceCatalog::Source& source)
{
    return nameOrLabelContains(source, { "satellite", "imagery" });
}

bool BaseMapSources::isTopographic(const MapSourceCatalog::Source& source)
{
    return nameOrLabelContains(source, { "cardg", "topo" });
}

QString BaseMapSources::iconFor(const MapSourceCatalog::Source& source)
{
    if (isMbTiles(source))
        return QStringLiteral("thumb_osm");
    if (isImagery(source))
        return QStringLiteral("thumb_imagery");
    if (isTopographic(source))
        return QStringLiteral("thumb_topo");
    return QStringLiteral("thumb_map");
}

QString BaseMapSources::labelFor(const MapSourceCatalog::Source& source,
                                 const QList<MapSourceCatalog::Source>& sources)
{
    if (!isMbTiles(source))
        return source.label;
    int mbTilesCount = 0;
    for (const MapSourceCatalog::Source& other : sources)
        mbTilesCount += isMbTiles(other) ? 1 : 0;
    return mbTilesCount > 1 ? QStringLiteral("OSM Map %1").arg(source.label) : QStringLiteral("OSM Map");
}

QVariantList BaseMapSources::entries(const QList<MapSourceCatalog::Source>& sources)
{
    QVariantList list;
    if (sources.isEmpty()) {
        list.append(QVariantMap{ { "label", QStringLiteral("OSM (online)") },
                                 { "icon", QStringLiteral("thumb_osm") } });
        return list;
    }
    for (const MapSourceCatalog::Source& source : sources) {
        list.append(QVariantMap{ { "label", labelFor(source, sources) },
                                 { "icon", iconFor(source) } });
    }
    return list;
}
