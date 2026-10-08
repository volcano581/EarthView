#include "earthview3d/EntityDiff.h"

#include <algorithm>

namespace earthview3d {

EntityDiff::Changes EntityDiff::apply(const std::vector<EntityState3D>& snapshot)
{
    Changes changes;
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(snapshot.size());

    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        const std::uint64_t id = snapshot[i].id;
        if (id == 0 || !seen.insert(id).second)
            continue;
        if (m_known.count(id))
            changes.updated.push_back(i);
        else
            changes.added.push_back(i);
    }

    for (std::uint64_t id : m_known) {
        if (!seen.count(id))
            changes.removed.push_back(id);
    }
    std::sort(changes.removed.begin(), changes.removed.end());

    m_known = std::move(seen);
    return changes;
}

} // namespace earthview3d
