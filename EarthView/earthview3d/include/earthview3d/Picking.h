#pragma once
#ifndef EARTHVIEW3D_PICKING_H
#define EARTHVIEW3D_PICKING_H

#include <cstdint>
#include <vector>

namespace earthview3d {

/**
 * @brief An entity projected to the screen, as a picking candidate.
 */
struct PickCandidate
{
    std::uint64_t id = 0;
    double x = 0.0;          ///< Window pixels, origin top-left
    double y = 0.0;
    double distanceM = 0.0;  ///< Eye distance; nearer wins a tie
    bool inFront = true;     ///< False when behind the camera
    bool visible = true;
};

/**
 * @brief Entity nearest to the click within @p radiusPx, or 0.
 *
 * Screen-space picking keeps small far-away icons easy to hit and costs O(n) per click.
 * Hidden and behind-camera candidates are ignored; among equally close candidates (within
 * 1 px) the one nearer the eye wins.
 */
std::uint64_t pickNearest(const std::vector<PickCandidate>& candidates, double x, double y, double radiusPx);

} // namespace earthview3d

#endif // EARTHVIEW3D_PICKING_H
