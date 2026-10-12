#include "earthview3d/CameraControl.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace earthview3d {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kSemiMajorAxisM = 6378137.0;
constexpr double kEccentricitySquared = 6.69437999014e-3;

// Meridian (north) and prime-vertical (east) radii of curvature at a latitude.
void radiiOfCurvature(double latDeg, double* northM, double* eastM)
{
    const double s = std::sin(latDeg * kDegToRad);
    const double w = std::sqrt(1.0 - kEccentricitySquared * s * s);
    *northM = kSemiMajorAxisM * (1.0 - kEccentricitySquared) / (w * w * w);
    *eastM = kSemiMajorAxisM / w;
}
} // namespace

QString cameraModeName(CameraMode mode)
{
    switch (mode) {
    case CameraMode::Orbit: return QStringLiteral("orbit");
    case CameraMode::Chase: return QStringLiteral("chase");
    case CameraMode::Stealth: return QStringLiteral("stealth");
    case CameraMode::FirstPerson: return QStringLiteral("fp");
    case CameraMode::FreeFly: return QStringLiteral("free");
    }
    return QString();
}

bool parseCameraMode(const QString& name, CameraMode* mode)
{
    const QString n = name.trimmed().toLower();
    CameraMode m;
    if (n == QLatin1String("orbit") || n == QLatin1String("home"))
        m = CameraMode::Orbit;
    else if (n == QLatin1String("chase"))
        m = CameraMode::Chase;
    else if (n == QLatin1String("stealth"))
        m = CameraMode::Stealth;
    else if (n == QLatin1String("fp") || n == QLatin1String("first-person"))
        m = CameraMode::FirstPerson;
    else if (n == QLatin1String("free") || n == QLatin1String("free-fly"))
        m = CameraMode::FreeFly;
    else
        return false;
    if (mode)
        *mode = m;
    return true;
}

double smoothStep(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double normalizeHeading(double deg)
{
    double h = std::fmod(deg, 360.0);
    if (h < 0.0)
        h += 360.0;
    return h;
}

CameraPose blendPose(const CameraPose& from, const CameraPose& to, double t)
{
    const double s = smoothStep(t);
    auto lerp = [s](double a, double b) { return a + (b - a) * s; };
    double dHeading = normalizeHeading(to.headingDeg - from.headingDeg);
    if (dHeading > 180.0)
        dHeading -= 360.0;
    double dLon = to.lonDeg - from.lonDeg;   // short way across the antimeridian
    if (dLon > 180.0)
        dLon -= 360.0;
    else if (dLon < -180.0)
        dLon += 360.0;

    CameraPose p;
    p.latDeg = lerp(from.latDeg, to.latDeg);
    p.lonDeg = from.lonDeg + dLon * s;
    if (p.lonDeg > 180.0)
        p.lonDeg -= 360.0;
    else if (p.lonDeg < -180.0)
        p.lonDeg += 360.0;
    p.altM = lerp(from.altM, to.altM);
    p.headingDeg = normalizeHeading(from.headingDeg + dHeading * s);
    p.pitchDeg = lerp(from.pitchDeg, to.pitchDeg);
    p.rollDeg = lerp(from.rollDeg, to.rollDeg);
    return p;
}

double clampEyeAltitude(double eyeAltM, double groundAltM, double clearanceM)
{
    return std::max(eyeAltM, groundAltM + clearanceM);
}

void TerrainClearance::update(bool ok, double groundAltM)
{
    if (!ok)
        return;
    m_groundM = groundAltM;
    m_have = true;
}

double TerrainClearance::clamp(double eyeAltM, double clearanceM) const
{
    return m_have ? clampEyeAltitude(eyeAltM, m_groundM, clearanceM) : eyeAltM;
}

double freeFlySpeed(double aglM)
{
    // About half the height per second: 5 m/s at the ground, 2.5 km/s at 5 km and above.
    return std::clamp(0.5 * std::max(aglM, 0.0), 5.0, 2500.0);
}

void offsetLatLon(double latDeg, double lonDeg, double bearingDeg, double distanceM,
                  double* outLatDeg, double* outLonDeg)
{
    double rn = 0.0;
    double re = 0.0;
    radiiOfCurvature(latDeg, &rn, &re);
    const double b = bearingDeg * kDegToRad;
    const double north = distanceM * std::cos(b);
    const double east = distanceM * std::sin(b);
    *outLatDeg = latDeg + (north / rn) / kDegToRad;
    *outLonDeg = lonDeg + (east / (re * std::max(std::cos(latDeg * kDegToRad), 1e-9))) / kDegToRad;
}

void lookAtAngles(const CameraPose& from, double toLatDeg, double toLonDeg, double toAltM,
                  double* headingDeg, double* pitchDeg)
{
    double rn = 0.0;
    double re = 0.0;
    radiiOfCurvature(from.latDeg, &rn, &re);
    double dLon = toLonDeg - from.lonDeg;
    if (dLon > 180.0)
        dLon -= 360.0;
    else if (dLon < -180.0)
        dLon += 360.0;
    const double north = (toLatDeg - from.latDeg) * kDegToRad * rn;
    const double east = dLon * kDegToRad * re * std::cos(from.latDeg * kDegToRad);
    const double up = toAltM - from.altM;
    *headingDeg = normalizeHeading(std::atan2(east, north) / kDegToRad);
    *pitchDeg = std::atan2(up, std::hypot(east, north)) / kDegToRad;
}

QString formatHud(const HudData& hud)
{
    QString mode = cameraModeName(hud.mode).toUpper();
    if (hud.mode == CameraMode::Chase && hud.chaseFreeLook)
        mode += QStringLiteral(" (free look)");
    QStringList lines;
    lines << QStringLiteral("MODE  %1").arg(mode);
    lines << QStringLiteral("HDG   %1°").arg(normalizeHeading(hud.headingDeg), 5, 'f', 1, QLatin1Char('0'));
    lines << QStringLiteral("PITCH %1°").arg(hud.pitchDeg, 0, 'f', 1);
    lines << QStringLiteral("ALT   %1 m").arg(hud.altM, 0, 'f', 0);
    lines << (hud.haveAgl ? QStringLiteral("AGL   %1 m").arg(hud.aglM, 0, 'f', 0) : QStringLiteral("AGL   --"));
    lines << QStringLiteral("SPD   %1 m/s").arg(hud.speedMps, 0, 'f', 1);
    lines << QStringLiteral("TGT   %1").arg(hud.target.isEmpty() ? QStringLiteral("--") : hud.target);
    return lines.join(QLatin1Char('\n'));
}

} // namespace earthview3d
