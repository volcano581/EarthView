#include "earthview3d/Picking.h"

#include <cmath>

namespace earthview3d {

std::uint64_t pickNearest(const std::vector<PickCandidate>& candidates, double x, double y, double radiusPx)
{
    std::uint64_t best = 0;
    double bestPx = radiusPx;
    double bestDistance = 0.0;
    for (const PickCandidate& c : candidates) {
        if (!c.visible || !c.inFront || c.id == 0)
            continue;
        const double px = std::hypot(c.x - x, c.y - y);
        if (px > radiusPx)
            continue;
        const bool clearlyCloser = px < bestPx - 1.0;
        const bool tie = std::abs(px - bestPx) <= 1.0;
        if (best == 0 || clearlyCloser || (tie && c.distanceM < bestDistance)) {
            best = c.id;
            bestPx = px;
            bestDistance = c.distanceM;
        }
    }
    return best;
}

} // namespace earthview3d
