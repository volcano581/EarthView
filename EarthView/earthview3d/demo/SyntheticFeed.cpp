#include "SyntheticFeed.h"

#include "Geodesy.h"

#include <QMutexLocker>
#include <QString>

#include <chrono>
#include <cmath>

namespace earthview3d::demo {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kGoldenAngle = 2.39996322972865332;   // rad, spreads route centres evenly

const char* kindPrefix(SyntheticFeed::Kind kind)
{
    switch (kind) {
    case SyntheticFeed::Infantry: return "INF";
    case SyntheticFeed::Armour: return "ARM";
    case SyntheticFeed::Artillery: return "ART";
    case SyntheticFeed::Air: return "AIR";
    }
    return "UNK";
}

std::uint32_t kindColor(SyntheticFeed::Kind kind)
{
    switch (kind) {
    case SyntheticFeed::Infantry: return 0x3A7BD5FFu;
    case SyntheticFeed::Armour: return 0x2E9E4FFFu;
    case SyntheticFeed::Artillery: return 0xE08A1EFFu;
    case SyntheticFeed::Air: return 0xD03030FFu;
    }
    return 0xFFFFFFFFu;
}
} // namespace

SyntheticFeed::SyntheticFeed(const Settings& settings)
    : m_settings(settings)
{
    if (m_settings.entityCount < 0)
        m_settings.entityCount = 0;
}

int SyntheticFeed::shapeOf(Kind kind)
{
    switch (kind) {
    case Infantry: return 0;
    case Air: return 1;
    case Armour: return 2;
    case Artillery: return 3;
    }
    return 0;
}

double SyntheticFeed::routeRadiusM(int index)
{
    static const double kBase[] = {300.0, 800.0, 500.0, 4000.0};
    return kBase[kindOf(index)] + 50.0 * ((index / 4) % 5);
}

double SyntheticFeed::speedMps(int index)
{
    static const double kSpeed[] = {1.5, 10.0, 4.0, 120.0};
    return kSpeed[kindOf(index)];
}

std::vector<EntityState3D> SyntheticFeed::stateAt(double seconds) const
{
    const glm::dvec3 originEcef = scene3d::Geodesy::geodeticToEcef(
        m_settings.originLatDeg, m_settings.originLonDeg, 0.0);
    const glm::dmat3 enu = scene3d::Geodesy::enuFrame(m_settings.originLatDeg, m_settings.originLonDeg);

    std::vector<EntityState3D> entities;
    entities.reserve(std::size_t(m_settings.entityCount));
    for (int i = 0; i < m_settings.entityCount; ++i) {
        const Kind kind = kindOf(i);
        const double radius = routeRadiusM(i);
        const double direction = (i % 2) ? -1.0 : 1.0;   // odd entities drive clockwise

        // Ground routes are spread on rings 2-8 km from the origin; air circles the origin.
        double centreEast = 0.0;
        double centreNorth = 0.0;
        if (kind != Air) {
            const double ring = 2000.0 + 600.0 * (i % 11);
            centreEast = ring * std::sin(i * kGoldenAngle);
            centreNorth = ring * std::cos(i * kGoldenAngle);
        }

        const double angle = i * 0.7 + direction * speedMps(i) / radius * seconds;
        const double east = centreEast + radius * std::cos(angle);
        const double north = centreNorth + radius * std::sin(angle);

        // Direction of travel: derivative of the circle, sign by direction.
        const double velEast = -std::sin(angle) * direction;
        const double velNorth = std::cos(angle) * direction;
        double heading = std::atan2(velEast, velNorth) * 180.0 / kPi;
        if (heading < 0.0)
            heading += 360.0;

        // Local east/north offset on the origin's tangent plane, converted exactly (double).
        const glm::dvec3 ecef = originEcef + enu * glm::dvec3(east, north, 0.0);
        const scene3d::GeodeticPosition geo = scene3d::Geodesy::ecefToGeodetic(ecef);

        EntityState3D e;
        e.id = std::uint64_t(i) + 1;
        e.latDeg = geo.latitudeDeg;
        e.lonDeg = geo.longitudeDeg;
        e.altM = kind == Air ? m_settings.airAltitudeM : m_settings.groundAltitudeM;
        e.headingDeg = heading;
        e.shape = shapeOf(kind);
        e.rgba = kindColor(kind);
        e.label = QStringLiteral("%1-%2").arg(QLatin1String(kindPrefix(kind))).arg(i + 1, 2, 10, QLatin1Char('0'));
        e.clampToTerrain = kind != Air;
        entities.push_back(std::move(e));
    }
    return entities;
}

double horizontalDistanceM(double lat1, double lon1, double lat2, double lon2)
{
    const glm::dvec3 a = scene3d::Geodesy::geodeticToEcef(lat1, lon1, 0.0);
    const glm::dvec3 b = scene3d::Geodesy::geodeticToEcef(lat2, lon2, 0.0);
    return glm::length(a - b);
}

void SnapshotBuffer::publish(std::vector<EntityState3D> snapshot)
{
    QMutexLocker lock(&m_mutex);
    m_latest = std::move(snapshot);
    ++m_tick;
}

bool SnapshotBuffer::consumeIfNew(std::uint64_t* lastTick, std::vector<EntityState3D>* out) const
{
    QMutexLocker lock(&m_mutex);
    if (m_tick == *lastTick)
        return false;
    *out = m_latest;
    *lastTick = m_tick;
    return true;
}

std::uint64_t SnapshotBuffer::tick() const
{
    QMutexLocker lock(&m_mutex);
    return m_tick;
}

FeedPublisher::FeedPublisher(const SyntheticFeed& feed, SnapshotBuffer* buffer, double rateHz)
    : m_feed(feed)
    , m_buffer(buffer)
    , m_rateHz(rateHz > 0.0 ? rateHz : 100.0)
{
}

FeedPublisher::~FeedPublisher()
{
    stop();
}

void FeedPublisher::start()
{
    if (m_running.exchange(true))
        return;
    m_thread = std::thread([this] {
        using Clock = std::chrono::steady_clock;
        const auto period = std::chrono::duration<double>(1.0 / m_rateHz);
        const auto start = Clock::now();
        auto next = start;
        while (m_running.load()) {
            const double t = std::chrono::duration<double>(Clock::now() - start).count();
            m_buffer->publish(m_feed.stateAt(t));
            next += std::chrono::duration_cast<Clock::duration>(period);
            std::this_thread::sleep_until(next);
        }
    });
}

void FeedPublisher::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_thread.joinable())
        m_thread.join();
}

} // namespace earthview3d::demo
