#pragma once
#ifndef BASEMAPSOURCES_H
#define BASEMAPSOURCES_H

#include "MapSourceCatalog.h"

#include <QList>
#include <QString>
#include <QVariantList>

/**
 * @brief Base map tiles for the layers panel, built from the same discovery as the toolbar's
 * Map Source dropdown (MapSourceCatalog::discoverMapSources()), in the same order: tile i is
 * dropdown item i. Each discovered earth file gets a tile with an icon matching its kind;
 * MBTiles files get the "OSM Map" tile. No sources at all = one "OSM (online)" tile, the
 * dropdown's "OpenStreetMap fallback".
 */
namespace BaseMapSources {

/// Icon names under qrc:/earthview/mapcontrols/icons/ (without ".svg").
QString iconFor(const MapSourceCatalog::Source& source);
QString labelFor(const MapSourceCatalog::Source& source, const QList<MapSourceCatalog::Source>& sources);

bool isMbTiles(const MapSourceCatalog::Source& source);
bool isImagery(const MapSourceCatalog::Source& source);
bool isTopographic(const MapSourceCatalog::Source& source);

/// One {"label", "icon"} map per tile, for MapControlsModel::setBaseMaps().
QVariantList entries(const QList<MapSourceCatalog::Source>& sources);

} // namespace BaseMapSources

#endif // BASEMAPSOURCES_H
