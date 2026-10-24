#pragma once
#ifndef EARTHVIEW3D_TERRAINSERVICES_H
#define EARTHVIEW3D_TERRAINSERVICES_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace earthview3d {

/**
 * @brief Thread-safe cache of terrain heights on a fixed lat/lon grid (roadmap E5).
 *
 * Keys are cells of @p cellDeg degrees (default 1e-5°, about 1 m); a hit returns the height
 * stored for that cell. Heights refine as finer terrain tiles load, so the owner invalidates
 * the extent of every updated tile. Misses from threads that may not query the terrain are
 * recorded with request() and resolved later on the GUI thread (takeRequests()).
 */
class HeightCache
{
public:
    explicit HeightCache(double cellDeg = 1.0e-5, std::size_t maxEntries = 200000);

    /// Cached height of the cell containing (@p latDeg, @p lonDeg); false on a miss.
    bool lookup(double latDeg, double lonDeg, double* heightM) const;
    void store(double latDeg, double lonDeg, double heightM);

    /// Drop cached heights inside the extent (degrees; west may exceed east across 180°).
    void invalidate(double southDeg, double westDeg, double northDeg, double eastDeg);
    void clear();

    /// Queue a point to be resolved later (duplicates in the same cell are merged).
    void request(double latDeg, double lonDeg);
    /// Take up to @p maxCount queued points (lat, lon), oldest first.
    std::vector<std::pair<double, double>> takeRequests(std::size_t maxCount);

    std::size_t size() const;
    std::size_t pendingCount() const;
    std::uint64_t hits() const;
    std::uint64_t misses() const;
    double cellDeg() const { return m_cellDeg; }

private:
    struct Entry
    {
        double latDeg;
        double lonDeg;
        double heightM;
    };
    std::uint64_t key(double latDeg, double lonDeg) const;

    double m_cellDeg;
    std::size_t m_maxEntries;
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, Entry> m_cells;
    std::vector<std::pair<double, double>> m_requests;
    std::unordered_map<std::uint64_t, bool> m_requested;
    mutable std::uint64_t m_hits = 0;
    mutable std::uint64_t m_misses = 0;
};

/**
 * @brief Host ground plane vs. 3D terrain for terrain-clamped entities (I-015).
 *
 * offset = clamped (terrain) altitude − altitude the host sent. A host that models the
 * ground as a flat plane shows a spread of offsets that follows the real terrain.
 */
struct GroundOffsetStats
{
    int count = 0;
    double minM = 0.0;
    double maxM = 0.0;
    double sumM = 0.0;

    void add(double offsetM);
    double meanM() const { return count > 0 ? sumM / count : 0.0; }
};

} // namespace earthview3d

#endif // EARTHVIEW3D_TERRAINSERVICES_H
