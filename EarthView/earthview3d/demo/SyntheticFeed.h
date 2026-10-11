#pragma once
#ifndef EARTHVIEW3D_DEMO_SYNTHETICFEED_H
#define EARTHVIEW3D_DEMO_SYNTHETICFEED_H

#include <earthview3d/EntityState3D.h>

#include <QMutex>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace earthview3d::demo {

/**
 * @brief Scripted entities moving around an origin: a stand-in for a host simulation.
 *
 * Deterministic: stateAt(t) depends only on the settings and t, so tests and snapshots are
 * reproducible. Entity i has kind i % 4 (infantry, armour, artillery, air) and drives a
 * circular route whose radius, speed and direction depend on i. Ground kinds are clamped to
 * terrain; air flies at airAltitudeM above the ellipsoid.
 */
class SyntheticFeed
{
public:
    enum Kind { Infantry = 0, Armour = 1, Artillery = 2, Air = 3 };

    struct Settings
    {
        int entityCount = 40;
        double originLatDeg = 33.6844;
        double originLonDeg = 73.0479;
        double groundAltitudeM = 540.0;  ///< Used until terrain clamping finds the ground
        double airAltitudeM = 1500.0;
    };

    explicit SyntheticFeed(const Settings& settings);

    const Settings& settings() const { return m_settings; }

    /// All entities at simulation time @p seconds.
    std::vector<EntityState3D> stateAt(double seconds) const;

    static Kind kindOf(int index) { return Kind(index % 4); }
    /// EntityState3D::shape for a kind (0 infantry, 1 air, 2 armour, 3 artillery).
    static int shapeOf(Kind kind);
    /// Route radius (m) and ground speed (m/s) of entity @p index.
    static double routeRadiusM(int index);
    static double speedMps(int index);

private:
    Settings m_settings;
};

/**
 * @brief Latest-snapshot double buffer between a producer thread and the GUI thread.
 *
 * The producer publishes whole snapshots; the consumer copies only when the tick counter
 * changed (the same "copy on new tick" rule as the host contract, DOCTRINE_WIRING.md §2).
 */
class SnapshotBuffer
{
public:
    void publish(std::vector<EntityState3D> snapshot);

    /// Copy the latest snapshot into @p out if it is newer than @p lastTick; returns true
    /// and updates @p lastTick when a copy was made.
    bool consumeIfNew(std::uint64_t* lastTick, std::vector<EntityState3D>* out) const;

    std::uint64_t tick() const;

private:
    mutable QMutex m_mutex;
    std::vector<EntityState3D> m_latest;
    std::uint64_t m_tick = 0;
};

/**
 * @brief Runs a SyntheticFeed on a worker thread, publishing into a SnapshotBuffer at a fixed rate.
 */
class FeedPublisher
{
public:
    FeedPublisher(const SyntheticFeed& feed, SnapshotBuffer* buffer, double rateHz = 100.0);
    ~FeedPublisher();

    void start();
    void stop();
    bool isRunning() const { return m_running.load(); }

private:
    const SyntheticFeed& m_feed;
    SnapshotBuffer* m_buffer;
    double m_rateHz;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace earthview3d::demo

#endif // EARTHVIEW3D_DEMO_SYNTHETICFEED_H
