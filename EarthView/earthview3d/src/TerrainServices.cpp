#include "earthview3d/TerrainServices.h"

#include <algorithm>
#include <cmath>

namespace earthview3d {

HeightCache::HeightCache(double cellDeg, std::size_t maxEntries)
    : m_cellDeg(cellDeg > 0.0 ? cellDeg : 1.0e-5)
    , m_maxEntries(std::max<std::size_t>(maxEntries, 1))
{
}

std::uint64_t HeightCache::key(double latDeg, double lonDeg) const
{
    // Row/column of the cell; 32 bits each is plenty down to ~1e-7° cells.
    const auto row = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor((latDeg + 90.0) / m_cellDeg)));
    double lon = std::fmod(lonDeg + 180.0, 360.0);
    if (lon < 0.0)
        lon += 360.0;
    const auto col = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(lon / m_cellDeg)));
    return (std::uint64_t(row) << 32) | col;
}

bool HeightCache::lookup(double latDeg, double lonDeg, double* heightM) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cells.find(key(latDeg, lonDeg));
    if (it == m_cells.end()) {
        ++m_misses;
        return false;
    }
    ++m_hits;
    if (heightM)
        *heightM = it->second.heightM;
    return true;
}

void HeightCache::store(double latDeg, double lonDeg, double heightM)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cells.size() >= m_maxEntries)
        m_cells.clear();   // simple bound: start over rather than track recency
    m_cells[key(latDeg, lonDeg)] = Entry{latDeg, lonDeg, heightM};
}

void HeightCache::invalidate(double southDeg, double westDeg, double northDeg, double eastDeg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool wraps = westDeg > eastDeg;
    for (auto it = m_cells.begin(); it != m_cells.end();) {
        const Entry& e = it->second;
        const bool inLat = e.latDeg >= southDeg && e.latDeg <= northDeg;
        const bool inLon = wraps ? (e.lonDeg >= westDeg || e.lonDeg <= eastDeg)
                                 : (e.lonDeg >= westDeg && e.lonDeg <= eastDeg);
        it = (inLat && inLon) ? m_cells.erase(it) : std::next(it);
    }
}

void HeightCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cells.clear();
}

void HeightCache::request(double latDeg, double lonDeg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::uint64_t k = key(latDeg, lonDeg);
    if (m_requested.emplace(k, true).second)
        m_requests.emplace_back(latDeg, lonDeg);
}

std::vector<std::pair<double, double>> HeightCache::takeRequests(std::size_t maxCount)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t n = std::min(maxCount, m_requests.size());
    std::vector<std::pair<double, double>> out(m_requests.begin(), m_requests.begin() + std::ptrdiff_t(n));
    m_requests.erase(m_requests.begin(), m_requests.begin() + std::ptrdiff_t(n));
    for (const auto& p : out)
        m_requested.erase(key(p.first, p.second));
    return out;
}

std::size_t HeightCache::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cells.size();
}

std::size_t HeightCache::pendingCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_requests.size();
}

std::uint64_t HeightCache::hits() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hits;
}

std::uint64_t HeightCache::misses() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_misses;
}

void GroundOffsetStats::add(double offsetM)
{
    if (count == 0) {
        minM = offsetM;
        maxM = offsetM;
    }
    else {
        minM = std::min(minM, offsetM);
        maxM = std::max(maxM, offsetM);
    }
    sumM += offsetM;
    ++count;
}

} // namespace earthview3d
