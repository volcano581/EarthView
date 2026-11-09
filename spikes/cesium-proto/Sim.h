#pragma once
#ifndef CESIUM_PROTO_SIM_H
#define CESIUM_PROTO_SIM_H

#include <QByteArray>
#include <QRandomGenerator>
#include <QString>

#include <vector>

class DtedStore;

/// @brief Ground units wandering around a centre point on DTED terrain (a stand-in for the
///        CGF engine). Vehicles slow down uphill; units turn back at the edge of the area.
///
/// Frame: [double t_ms][uint32 count][uint32 seq], then count x (double lon, double lat),
/// then count x (float height MSL, float heading deg, float speed m/s of wall time).
class Sim
{
public:
    Sim(const DtedStore *dted, double lon, double lat, double radiusKm, int count, double timeScale);

    void step(double wallSeconds);
    QByteArray frame() const { return m_frame; }
    quint32 seq() const { return m_seq; }
    QByteArray metaJson() const;

private:
    struct Unit
    {
        QString name;
        int type = 0; // 0 infantry, 1 armour, 2 mech infantry, 3 recce, 4 artillery
        int side = 0; // 0 friendly, 1 hostile
        double lon = 0.0, lat = 0.0;
        double heading = 0.0; // degrees clockwise from north
        double baseSpeed = 0.0, speed = 0.0, height = 0.0;
    };

    double groundHeight(double lon, double lat) const;
    void buildFrame();

    const DtedStore *m_dted;
    double m_lon, m_lat, m_radiusM, m_timeScale;
    std::vector<Unit> m_units;
    QRandomGenerator m_rng;
    quint32 m_seq = 0;
    QByteArray m_frame;
};

#endif // CESIUM_PROTO_SIM_H
