#include "Sim.h"

#include "Dted.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

#include <cmath>
#include <cstring>

namespace {

constexpr double kEarthRadius = 6371000.0;
const char *const kTypeCodes[] = { "INF", "ARM", "MECH", "RECCE", "ARTY" };
const double kTypeSpeeds[] = { 1.5, 9.0, 7.0, 12.0, 5.0 }; // m/s
const int kTypeWeights[] = { 40, 15, 20, 15, 10 };          // percent

double distanceMetres(double lon1, double lat1, double lon2, double lat2)
{
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1) * std::cos(qDegreesToRadians((lat1 + lat2) * 0.5));
    return kEarthRadius * std::sqrt(dLat * dLat + dLon * dLon);
}

double bearingDegrees(double lon1, double lat1, double lon2, double lat2)
{
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1) * std::cos(qDegreesToRadians((lat1 + lat2) * 0.5));
    return std::fmod(qRadiansToDegrees(std::atan2(dLon, dLat)) + 360.0, 360.0);
}

} // namespace

Sim::Sim(const DtedStore *dted, double lon, double lat, double radiusKm, int count, double timeScale)
    : m_dted(dted)
    , m_lon(lon)
    , m_lat(lat)
    , m_radiusM(radiusKm * 1000.0)
    , m_timeScale(timeScale)
    , m_rng(42)
{
    int serial[2][5] = {};
    for (int i = 0; i < count; ++i) {
        Unit u;
        u.side = m_rng.bounded(100) < 60 ? 0 : 1;
        int roll = m_rng.bounded(100);
        while (roll >= kTypeWeights[u.type]) {
            roll -= kTypeWeights[u.type];
            ++u.type;
        }
        u.name = QStringLiteral("%1 %2 %3")
                     .arg(u.side == 0 ? QStringLiteral("B") : QStringLiteral("R"))
                     .arg(QLatin1String(kTypeCodes[u.type]))
                     .arg(++serial[u.side][u.type], 2, 10, QLatin1Char('0'));

        // Friendly units start in the western half of the area, hostile ones in the east.
        const double r = m_radiusM * std::sqrt(m_rng.generateDouble()) * 0.9;
        const double a = M_PI * (m_rng.generateDouble() - 0.5) + (u.side == 0 ? M_PI : 0.0);
        u.lat = m_lat + qRadiansToDegrees(r * std::cos(a) / kEarthRadius);
        u.lon = m_lon + qRadiansToDegrees(r * std::sin(a) / (kEarthRadius * std::cos(qDegreesToRadians(m_lat))));
        u.heading = m_rng.generateDouble() * 360.0;
        u.baseSpeed = kTypeSpeeds[u.type] * (0.7 + 0.6 * m_rng.generateDouble());
        u.speed = u.baseSpeed;
        u.height = groundHeight(u.lon, u.lat);
        m_units.push_back(u);
    }
    buildFrame();
}

double Sim::groundHeight(double lon, double lat) const
{
    double h = 0.0;
    return m_dted && m_dted->height(lon, lat, &h) ? h : 0.0;
}

void Sim::step(double wallSeconds)
{
    const double dt = wallSeconds * m_timeScale;
    for (Unit &u : m_units) {
        if (distanceMetres(u.lon, u.lat, m_lon, m_lat) > m_radiusM) {
            const double home = bearingDegrees(u.lon, u.lat, m_lon, m_lat);
            const double turn = std::remainder(home - u.heading, 360.0);
            u.heading += qBound(-90.0 * dt, turn, 90.0 * dt);
        } else {
            u.heading += (m_rng.generateDouble() - 0.5) * 40.0 * std::sqrt(dt);
        }
        u.heading = std::fmod(u.heading + 360.0, 360.0);

        // Vehicles lose speed on uphill grades (a placeholder for the CGF's mobility model).
        const double h = qDegreesToRadians(u.heading);
        const double probe = 20.0;
        const double aheadLat = u.lat + qRadiansToDegrees(probe * std::cos(h) / kEarthRadius);
        const double aheadLon = u.lon + qRadiansToDegrees(probe * std::sin(h) / (kEarthRadius * std::cos(qDegreesToRadians(u.lat))));
        const double grade = (groundHeight(aheadLon, aheadLat) - u.height) / probe;
        const double factor = u.type == 0 ? 1.0 : qBound(0.25, 1.0 - 3.0 * grade, 1.15);
        u.speed = u.baseSpeed * factor;

        const double d = u.speed * dt;
        u.lat += qRadiansToDegrees(d * std::cos(h) / kEarthRadius);
        u.lon += qRadiansToDegrees(d * std::sin(h) / (kEarthRadius * std::cos(qDegreesToRadians(u.lat))));
        u.height = groundHeight(u.lon, u.lat);
    }
    buildFrame();
}

void Sim::buildFrame()
{
    const quint32 count = quint32(m_units.size());
    m_frame = QByteArray(16 + int(count) * 28, Qt::Uninitialized);
    char *p = m_frame.data();
    const double t = double(QDateTime::currentMSecsSinceEpoch());
    const quint32 seq = ++m_seq;
    std::memcpy(p, &t, 8);
    std::memcpy(p + 8, &count, 4);
    std::memcpy(p + 12, &seq, 4);
    auto *lonLat = reinterpret_cast<double *>(p + 16);
    auto *dynamics = reinterpret_cast<float *>(p + 16 + count * 16);
    for (quint32 i = 0; i < count; ++i) {
        const Unit &u = m_units[i];
        lonLat[i * 2] = u.lon;
        lonLat[i * 2 + 1] = u.lat;
        dynamics[i * 3] = float(u.height);
        dynamics[i * 3 + 1] = float(u.heading);
        dynamics[i * 3 + 2] = float(u.speed * m_timeScale);
    }
}

QByteArray Sim::metaJson() const
{
    QJsonArray units;
    for (const Unit &u : m_units)
        units.append(QJsonArray{ u.name, u.type, u.side });
    const QJsonObject meta{
        { "count", int(m_units.size()) },
        { "timeScale", m_timeScale },
        { "center", QJsonArray{ m_lon, m_lat } },
        { "radiusKm", m_radiusM / 1000.0 },
        { "units", units },
    };
    return QJsonDocument(meta).toJson(QJsonDocument::Compact);
}
