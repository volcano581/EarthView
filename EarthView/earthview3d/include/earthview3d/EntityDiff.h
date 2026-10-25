#pragma once
#ifndef EARTHVIEW3D_ENTITYDIFF_H
#define EARTHVIEW3D_ENTITYDIFF_H

#include "earthview3d/EntityState3D.h"

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace earthview3d {

/**
 * @brief Tracks which entity ids are on screen and diffs each new host snapshot against them.
 *
 * GL-free so the add/update/remove logic is unit tested without osgEarth. GlobeView3D feeds
 * every setEntities() call through apply() and creates, updates or destroys visuals from the
 * result.
 *
 * Rules: entries with id 0 are ignored; if an id appears more than once in one snapshot, the
 * first entry wins and later ones are ignored.
 */
class EntityDiff
{
public:
    struct Changes
    {
        std::vector<std::size_t> added;     ///< Indices into the snapshot, in snapshot order
        std::vector<std::size_t> updated;   ///< Indices into the snapshot, in snapshot order
        std::vector<std::uint64_t> removed; ///< Ids no longer present, ascending
    };

    /// Diff @p snapshot against the previous one and remember it as the current set.
    Changes apply(const std::vector<EntityState3D>& snapshot);

    bool contains(std::uint64_t id) const { return m_known.count(id) != 0; }
    std::size_t size() const { return m_known.size(); }
    void clear() { m_known.clear(); }

private:
    std::unordered_set<std::uint64_t> m_known;
};

} // namespace earthview3d

#endif // EARTHVIEW3D_ENTITYDIFF_H
