#pragma once
#ifndef BASEMAPSOURCES_H
#define BASEMAPSOURCES_H

#include "MapSourceCatalog.h"

#include <QList>

/**
 * @brief Maps the 2D map sources onto the layers panel's two base maps (OSM, Imagery).
 *
 * Imagery = the first source whose file name or label says satellite/imagery. OSM = the first
 * other source, MBTiles files first. With no sources at all, OSM is the online OpenStreetMap
 * fallback (index -1), as the viewer did before. A base map with no source is unavailable.
 */
namespace BaseMapSources {

/// Source index for each base map; -1 = none (OSM: -1 with noSources = online fallback).
struct Choice
{
    int osmIndex = -1;
    int imageryIndex = -1;
    bool noSources = true;

    bool osmAvailable() const { return osmIndex >= 0 || noSources; }
    bool imageryAvailable() const { return imageryIndex >= 0; }
};

bool isImagery(const MapSourceCatalog::Source& source);

Choice choose(const QList<MapSourceCatalog::Source>& sources);

/// Base map (MapControlsModel::BaseMap) shown for source @p sourceIndex (-1 = OSM fallback).
int baseMapForSource(const QList<MapSourceCatalog::Source>& sources, int sourceIndex);

/// Source index to load for @p baseMap, or -1 (OSM fallback / nothing).
int sourceForBaseMap(const Choice& choice, int baseMap);

} // namespace BaseMapSources

#endif // BASEMAPSOURCES_H
