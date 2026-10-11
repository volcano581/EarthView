#pragma once
#ifndef EARTHVIEW3D_ENTITYSTATE3D_H
#define EARTHVIEW3D_ENTITYSTATE3D_H

#include <QString>

#include <cstdint>

namespace earthview3d {

/**
 * @brief One entity as the 3D view needs it, pushed by the host every frame.
 *
 * Positions are geodetic WGS-84 in double precision; heights are above the ellipsoid.
 * The host fills these from its own simulation snapshot (see DOCTRINE_WIRING.md §2).
 */
struct EntityState3D
{
    std::uint64_t id = 0;        ///< Stable per entity; 0 is reserved (entries with id 0 are ignored)
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double altM = 0.0;           ///< Height above the WGS-84 ellipsoid
    double headingDeg = 0.0;     ///< Clockwise from north
    int shape = 0;               ///< 0 infantry, 1 air, 2 armour, 3 artillery (model table in E3)
    std::uint32_t rgba = 0xFFFFFFFFu; ///< 0xRRGGBBAA
    QString label;
    /// Place on the 3D terrain instead of using altM (ground units while the host's ground
    /// does not match the DTED surface; I-015).
    bool clampToTerrain = false;
    bool visible = true;         ///< Hidden entities keep their visual (and selection) but are not drawn
};

} // namespace earthview3d

#endif // EARTHVIEW3D_ENTITYSTATE3D_H
