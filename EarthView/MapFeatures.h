#pragma once
#ifndef MAPFEATURES_H
#define MAPFEATURES_H

#include <QString>
#include <QStringList>
#include <algorithm>

/**
 * @brief The layers of a vector (OSM) map, as the user sees and switches them.
 *
 * Every layer of the MBTiles file is a map detail of its own (water, rivers, roads, road names,
 * buildings, places, ...); the Layers panel lists the ones the file has. This catalog gives the
 * layers of the OpenMapTiles and Mapbox Streets schemas a display name and a place in the list;
 * any other layer is shown under its own name, after them.
 */
namespace MapFeatures {

struct LayerInfo {
    const char* id;
    const char* label;
    bool area;  ///< land / water areas: not filled over satellite imagery
};

inline const LayerInfo* knownLayers(int* count)
{
    // Panel order.
    static const LayerInfo kLayers[] = {
        { "place", "Places", false },
        { "transportation", "Roads", false },
        { "transportation_name", "Road names", false },
        { "water", "Water", true },
        { "waterway", "Rivers", false },
        { "water_name", "Water names", false },
        { "building", "Buildings", true },
        { "housenumber", "House numbers", false },
        { "boundary", "Boundaries", false },
        { "park", "Parks", true },
        { "landcover", "Land cover", true },
        { "landuse", "Land use", true },
        { "aeroway", "Airports", true },
        { "aerodrome_label", "Airport names", false },
        { "mountain_peak", "Peaks", false },
        { "poi", "Points of interest", false },
        // Mapbox Streets
        { "place_label", "Places", false },
        { "road", "Roads", false },
        { "road_label", "Road names", false },
        { "waterway_label", "River names", false },
        { "admin", "Boundaries", false },
        { "landuse_overlay", "Land use", true },
        { "airport_label", "Airport names", false },
        { "poi_label", "Points of interest", false },
        { "natural_label", "Natural features", false },
    };
    *count = int(sizeof(kLayers) / sizeof(kLayers[0]));
    return kLayers;
}

inline const LayerInfo* layerInfo(const QString& id)
{
    int count = 0;
    const LayerInfo* layers = knownLayers(&count);
    for (int i = 0; i < count; ++i) {
        if (id == QLatin1String(layers[i].id))
            return &layers[i];
    }
    return nullptr;
}

/// "Rivers" for waterway; unknown layers: their id with spaces and a capital ("ski_area" ->
/// "Ski area").
inline QString layerLabel(const QString& id)
{
    if (const LayerInfo* info = layerInfo(id))
        return QString::fromLatin1(info->label);
    QString label = id;
    label.replace('_', ' ');
    if (!label.isEmpty())
        label[0] = label.at(0).toUpper();
    return label;
}

/// Land cover, land use, parks, water areas, buildings, airport areas: the map's ground.
inline bool isAreaLayer(const QString& id)
{
    const LayerInfo* info = layerInfo(id);
    return info && info->area;
}

/// Position in the Layers panel: known layers in catalog order, then the others by name.
inline int layerOrder(const QString& id)
{
    int count = 0;
    const LayerInfo* layers = knownLayers(&count);
    for (int i = 0; i < count; ++i) {
        if (id == QLatin1String(layers[i].id))
            return i;
    }
    return count;
}

/// @p ids in panel order, without duplicates.
inline QStringList sortedLayers(QStringList ids)
{
    ids.removeDuplicates();
    std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) {
        const int orderA = layerOrder(a);
        const int orderB = layerOrder(b);
        return orderA != orderB ? orderA < orderB : a < b;
    });
    return ids;
}

} // namespace MapFeatures

#endif // MAPFEATURES_H
