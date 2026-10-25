#pragma once
#ifndef SCENE3D_CAMERA3D_H
#define SCENE3D_CAMERA3D_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>

namespace scene3d {

/**
 * @brief World-space ray (ECEF, double precision).
 */
struct Ray
{
    glm::dvec3 origin{0.0};
    glm::dvec3 direction{0.0, 0.0, -1.0}; ///< Unit length
};

/**
 * @brief Plane n.p + d = 0, with n pointing into the frustum.
 */
struct Plane
{
    glm::dvec3 normal{0.0, 0.0, 1.0};
    double d = 0.0;

    double signedDistance(const glm::dvec3& point) const { return glm::dot(normal, point) + d; }
};

/**
 * @brief Globe camera in ECEF double precision.
 *
 * Orientation maps camera space to ECEF. Camera space follows OpenGL: +X right, +Y up,
 * looking down -Z. GPU matrices are relative-to-eye (RTE): the view matrix holds rotation
 * only, and vertex positions are uploaded as float(worldPosition - position()).
 *
 * The projection uses reversed Z with an infinite far plane and a [0, 1] depth range
 * (glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE), depth cleared to 0, GL_GREATER).
 * Because the far plane is at infinity, frustumPlanes() returns the near plane and the
 * four side planes only.
 */
class Camera3D
{
public:
    enum FrustumPlane { Left = 0, Right, Bottom, Top, Near, PlaneCount };

    Camera3D();

    void setPosition(const glm::dvec3& ecef) { m_position = ecef; }
    const glm::dvec3& position() const { return m_position; }

    void setOrientation(const glm::dquat& cameraToWorld);
    const glm::dquat& orientation() const { return m_orientation; }

    void setFovYDegrees(double fovYDeg);
    double fovYDegrees() const;

    void setNearPlane(double nearMeters);
    double nearPlane() const { return m_near; }

    void setViewportSize(int width, int height);
    int viewportWidth() const { return m_viewportWidth; }
    int viewportHeight() const { return m_viewportHeight; }
    double aspectRatio() const;

    /// Camera axes in ECEF.
    glm::dvec3 right() const;
    glm::dvec3 up() const;
    glm::dvec3 forward() const;

    /**
     * @brief Place the camera at a geodetic location with aircraft-style angles (degrees).
     * @param headingDeg Clockwise from north, looking down on the local horizon
     * @param pitchDeg 0 = horizontal, negative = looking down, -90 = straight down
     * @param rollDeg Positive banks to the right
     */
    void setFromGeodetic(double latitudeDeg, double longitudeDeg, double heightMeters,
                         double headingDeg, double pitchDeg, double rollDeg);

    /// Rotate to look at an ECEF target, keeping the local geodetic up as close to "up" as possible.
    void lookAt(const glm::dvec3& targetEcef);

    /// Rotation-only world-to-camera matrix for RTE rendering.
    glm::mat4 viewMatrixRTE() const;

    /// Full world-to-camera matrix in double (CPU use only; never upload).
    glm::dmat4 viewMatrix() const;

    /// Reversed-Z, infinite-far perspective projection (depth 1 at near, 0 at infinity).
    glm::mat4 projectionReversedZ() const;

    /// Near + side planes in ECEF; a point is inside when every signedDistance() >= 0.
    std::array<Plane, PlaneCount> frustumPlanes() const;
    bool frustumContains(const glm::dvec3& pointEcef) const;

    /// World ray through a pixel; (0, 0) is the top-left corner, pixel centres at +0.5.
    Ray screenRay(double pixelX, double pixelY) const;

    /// Relative-to-eye position for GPU upload.
    glm::vec3 toRelativeToEye(const glm::dvec3& pointEcef) const;

private:
    glm::dvec3 m_position;
    glm::dquat m_orientation;
    double m_fovY;   ///< radians
    double m_near;   ///< metres
    int m_viewportWidth;
    int m_viewportHeight;
};

} // namespace scene3d

#endif // SCENE3D_CAMERA3D_H
