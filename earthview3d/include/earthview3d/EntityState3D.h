#pragma once
#ifndef EARTHVIEW3D_ENTITYSTATE3D_H
#define EARTHVIEW3D_ENTITYSTATE3D_H

#include <QString>

#include <cstdint>

namespace earthview3d {

/// Force / affiliation; drives the standard colours when set (see entityColor()).
enum class Force : std::uint8_t { Unspecified = 0, Friendly, Hostile, Neutral, Unknown };

/**
 * @brief One entity as the 3D view needs it, pushed by the host every frame.
 *
 * Positions are geodetic WGS-84 in double precision; heights are above the ellipsoid.
 * Attitude follows D-005: heading clockwise from north, pitch positive nose-up, roll positive
 * right wing down. The host fills these from its own simulation snapshot (DOCTRINE_WIRING.md §2).
 */
struct EntityState3D
{
    std::uint64_t id = 0;        ///< Stable per entity; 0 is reserved (entries with id 0 are ignored)
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double altM = 0.0;           ///< Height above the WGS-84 ellipsoid
    double headingDeg = 0.0;     ///< Clockwise from north
    double pitchDeg = 0.0;       ///< Nose up positive
    double rollDeg = 0.0;        ///< Right wing down positive
    /// Model-table key (e.g. a SISO kind "1.1.225.1.1.3.0" or "ground/armour"); empty or
    /// unmatched keys fall back to the placeholder `shape`.
    QString kindKey;
    int shape = 0;               ///< Placeholder: 0 infantry, 1 air, 2 armour, 3 artillery
    std::uint32_t rgba = 0xFFFFFFFFu; ///< 0xRRGGBBAA; used when force is Unspecified
    Force force = Force::Unspecified;
    QString label;
    /// Place on the 3D terrain instead of using altM (ground units while the host's ground
    /// does not match the DTED surface; I-015). The model table can also request clamping.
    bool clampToTerrain = false;
    bool visible = true;         ///< Hidden entities keep their visual (and selection) but are not drawn or pickable
};

/// Display colour (0xRRGGBBAA): standard affiliation colours for a set force, else rgba.
std::uint32_t entityColor(const EntityState3D& state);

} // namespace earthview3d

#endif // EARTHVIEW3D_ENTITYSTATE3D_H
