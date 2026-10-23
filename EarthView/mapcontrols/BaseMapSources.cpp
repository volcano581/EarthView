#include "BaseMapSources.h"
#include "MapControlsModel.h"

#include <QFileInfo>

bool BaseMapSources::isImagery(const MapSourceCatalog::Source& source)
{
    const QString name = QFileInfo(source.filePath).completeBaseName().toLower();
    const QString label = source.label.toLower();
    return name.contains(QLatin1String("satellite")) || name.contains(QLatin1String("imagery"))
        || label.contains(QLatin1String("satellite")) || label.contains(QLatin1String("imagery"));
}

BaseMapSources::Choice BaseMapSources::choose(const QList<MapSourceCatalog::Source>& sources)
{
    Choice choice;
    choice.noSources = sources.isEmpty();
    int firstOther = -1;
    for (int i = 0; i < sources.size(); ++i) {
        const MapSourceCatalog::Source& source = sources.at(i);
        if (isImagery(source)) {
            if (choice.imageryIndex < 0)
                choice.imageryIndex = i;
            continue;
        }
        if (firstOther < 0)
            firstOther = i;
        if (choice.osmIndex < 0
            && source.filePath.endsWith(QLatin1String(".mbtiles"), Qt::CaseInsensitive))
            choice.osmIndex = i;
    }
    if (choice.osmIndex < 0)
        choice.osmIndex = firstOther;
    return choice;
}

int BaseMapSources::baseMapForSource(const QList<MapSourceCatalog::Source>& sources, int sourceIndex)
{
    if (sourceIndex >= 0 && sourceIndex < sources.size() && isImagery(sources.at(sourceIndex)))
        return MapControlsModel::ImageryBaseMap;
    return MapControlsModel::OsmBaseMap;
}

int BaseMapSources::sourceForBaseMap(const Choice& choice, int baseMap)
{
    return baseMap == MapControlsModel::ImageryBaseMap ? choice.imageryIndex : choice.osmIndex;
}
