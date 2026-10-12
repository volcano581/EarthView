#pragma once
#ifndef EARTHVIEW3D_CAMERACONTROL_H
#define EARTHVIEW3D_CAMERACONTROL_H

#include "earthview3d/TerrainServices.h"

#include <QString>

namespace earthview3d {

/**
 * @brief Camera modes of GlobeView3D (roadmap E4).
 *
 * Orbit is osgEarth's EarthManipulator (drag to rotate/pan, wheel to zoom). The other modes
 * are driven by GlobeView3D itself so the eye can be kept above the terrain every frame.
 */
enum class CameraMode
{
    Orbit,        ///< Free orbit around the globe (osgEarth EarthManipulator)
    Chase,        ///< Behind and above the selected entity; heading-locked or free-look
    Stealth,      ///< Near ground level behind the selected entity, eye height above terrain
    FirstPerson,  ///< From the selected entity, following its attitude; drag to look around
    FreeFly       ///< WASD + mouse; speed scales with height above ground
};

/// Short lower-case name ("orbit", "chase", "stealth", "fp", "free").
QString cameraModeName(CameraMode mode);
/// Parse a name from cameraModeName() (case-insensitive; also "home" = orbit). False if unknown.
bool parseCameraMode(const QString& name, CameraMode* mode);

/**
 * @brief Eye position and orientation. Positions are WGS-84 geodetic doubles (rule 4).
 */
struct CameraPose
{
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double altM = 0.0;        ///< Height above the WGS-84 ellipsoid
    double headingDeg = 0.0;  ///< Clockwise from north
    double pitchDeg = 0.0;    ///< Positive looks up
    double rollDeg = 0.0;     ///< Positive rolls right
};

/// 0..1 ease-in/ease-out (smoothstep) of @p t, clamped.
double smoothStep(double t);
/// Heading in [0, 360).
double normalizeHeading(double deg);

/**
 * @brief Pose between @p from and @p to at eased fraction @p t (0..1).
 *
 * Position is interpolated linearly in lat/lon/alt (smooth enough for camera transitions;
 * the eye is clamped above terrain afterwards). Heading takes the short way round.
 */
CameraPose blendPose(const CameraPose& from, const CameraPose& to, double t);

/// Minimum eye height above terrain in every mode except orbit (E4 acceptance).
constexpr double kMinTerrainClearanceM = 2.0;

/// @p eyeAltM raised so it is at least @p clearanceM above @p groundAltM.
double clampEyeAltitude(double eyeAltM, double groundAltM, double clearanceM = kMinTerrainClearanceM);

/**
 * @brief Last good terrain height under a moving point.
 *
 * Terrain queries fail while tiles load; the last successful height is kept so the camera
 * never drops to the ellipsoid in that time.
 */
class TerrainClearance
{
public:
    /// Record a query result; a failed query (@p ok false) keeps the previous height.
    void update(bool ok, double groundAltM);
    bool hasGround() const { return m_have; }
    double groundAltM() const { return m_groundM; }
    /// @p eyeAltM clamped above the known ground (unchanged when no ground is known yet).
    double clamp(double eyeAltM, double clearanceM = kMinTerrainClearanceM) const;

private:
    double m_groundM = 0.0;
    bool m_have = false;
};

/// Free-fly speed (m/s) for an eye @p aglM above ground: slow near the ground, fast high up.
double freeFlySpeed(double aglM);

/// Point @p distanceM from (@p latDeg, @p lonDeg) along @p bearingDeg (local flat-earth
/// approximation on the WGS-84 ellipsoid; accurate to well under 1 % below ~10 km).
void offsetLatLon(double latDeg, double lonDeg, double bearingDeg, double distanceM,
                  double* outLatDeg, double* outLonDeg);

/// Heading/pitch (degrees) to look from @p from to @p to (local tangent-plane approximation).
void lookAtAngles(const CameraPose& from, double toLatDeg, double toLonDeg, double toAltM,
                  double* headingDeg, double* pitchDeg);

/**
 * @brief Values shown on the HUD overlay.
 */
struct HudData
{
    CameraMode mode = CameraMode::Orbit;
    double headingDeg = 0.0;
    double pitchDeg = 0.0;
    double altM = 0.0;        ///< Eye height above the ellipsoid (HAE)
    bool haveMsl = false;
    double mslM = 0.0;        ///< Eye height above mean sea level (EGM96), when the geoid is known
    bool haveAgl = false;
    double aglM = 0.0;        ///< Eye height above terrain
    double speedMps = 0.0;    ///< Eye speed
    QString target;           ///< Tracked entity label (empty: none)
    bool chaseFreeLook = false;
    // Ground-offset debug overlay (E5, I-015): terrain minus host altitude, clamped entities.
    bool showGroundOffset = false;
    bool haveTargetOffset = false;
    double targetOffsetM = 0.0;
    GroundOffsetStats groundOffsets;
};

/// HUD text, one item per line.
QString formatHud(const HudData& hud);

} // namespace earthview3d

#endif // EARTHVIEW3D_CAMERACONTROL_H
