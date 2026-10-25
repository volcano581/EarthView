#include "Camera3D.h"
#include "Geodesy.h"

#include <algorithm>
#include <cmath>

namespace scene3d {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

glm::dquat orientationFromBasis(const glm::dvec3& right, const glm::dvec3& up, const glm::dvec3& forward)
{
    // Columns are the camera axes (+X right, +Y up, +Z backward) expressed in world space.
    return glm::normalize(glm::quat_cast(glm::dmat3(right, up, -forward)));
}
} // namespace

Camera3D::Camera3D()
    : m_position(Geodesy::kSemiMajorAxis * 3.0, 0.0, 0.0)
    , m_orientation(1.0, 0.0, 0.0, 0.0)
    , m_fovY(60.0 * kDegToRad)
    , m_near(1.0)
    , m_viewportWidth(1)
    , m_viewportHeight(1)
{
    lookAt(glm::dvec3(0.0));
}

void Camera3D::setOrientation(const glm::dquat& cameraToWorld)
{
    m_orientation = glm::normalize(cameraToWorld);
}

void Camera3D::setFovYDegrees(double fovYDeg)
{
    m_fovY = std::clamp(fovYDeg, 1e-3, 179.0) * kDegToRad;
}

double Camera3D::fovYDegrees() const
{
    return m_fovY / kDegToRad;
}

void Camera3D::setNearPlane(double nearMeters)
{
    m_near = std::max(nearMeters, 1e-4);
}

void Camera3D::setViewportSize(int width, int height)
{
    m_viewportWidth = std::max(width, 1);
    m_viewportHeight = std::max(height, 1);
}

double Camera3D::aspectRatio() const
{
    return static_cast<double>(m_viewportWidth) / static_cast<double>(m_viewportHeight);
}

glm::dvec3 Camera3D::right() const
{
    return m_orientation * glm::dvec3(1.0, 0.0, 0.0);
}

glm::dvec3 Camera3D::up() const
{
    return m_orientation * glm::dvec3(0.0, 1.0, 0.0);
}

glm::dvec3 Camera3D::forward() const
{
    return m_orientation * glm::dvec3(0.0, 0.0, -1.0);
}

void Camera3D::setFromGeodetic(double latitudeDeg, double longitudeDeg, double heightMeters,
                               double headingDeg, double pitchDeg, double rollDeg)
{
    m_position = Geodesy::geodeticToEcef(latitudeDeg, longitudeDeg, heightMeters);

    const double h = headingDeg * kDegToRad;
    const double p = pitchDeg * kDegToRad;
    const double r = rollDeg * kDegToRad;

    // Local frame X = east, Y = north, Z = up. At zero angles the camera looks north,
    // right = east, up = up. Apply roll (about forward), pitch (about right), then
    // heading (clockwise about up).
    const glm::dmat3 rollM(glm::dvec3(std::cos(r), 0.0, -std::sin(r)),
                           glm::dvec3(0.0, 1.0, 0.0),
                           glm::dvec3(std::sin(r), 0.0, std::cos(r)));
    const glm::dmat3 pitchM(glm::dvec3(1.0, 0.0, 0.0),
                            glm::dvec3(0.0, std::cos(p), std::sin(p)),
                            glm::dvec3(0.0, -std::sin(p), std::cos(p)));
    const glm::dmat3 headingM(glm::dvec3(std::cos(h), -std::sin(h), 0.0),
                              glm::dvec3(std::sin(h), std::cos(h), 0.0),
                              glm::dvec3(0.0, 0.0, 1.0));
    const glm::dmat3 local = headingM * pitchM * rollM;
    const glm::dmat3 enu = Geodesy::enuFrame(latitudeDeg, longitudeDeg);

    const glm::dvec3 rightW = enu * (local * glm::dvec3(1.0, 0.0, 0.0));
    const glm::dvec3 forwardW = enu * (local * glm::dvec3(0.0, 1.0, 0.0));
    const glm::dvec3 upW = enu * (local * glm::dvec3(0.0, 0.0, 1.0));
    m_orientation = orientationFromBasis(rightW, upW, forwardW);
}

void Camera3D::lookAt(const glm::dvec3& targetEcef)
{
    const glm::dvec3 toTarget = targetEcef - m_position;
    if (glm::dot(toTarget, toTarget) <= 0.0)
        return;
    const glm::dvec3 forwardW = glm::normalize(toTarget);

    const GeodeticPosition geo = Geodesy::ecefToGeodetic(m_position);
    const glm::dmat3 enu = Geodesy::enuFrame(geo.latitudeDeg, geo.longitudeDeg);
    glm::dvec3 upHint = enu[2];
    if (std::abs(glm::dot(upHint, forwardW)) > 0.999999)
        upHint = enu[1]; // looking straight up/down: keep north at the top of the screen

    const glm::dvec3 rightW = glm::normalize(glm::cross(forwardW, upHint));
    const glm::dvec3 upW = glm::cross(rightW, forwardW);
    m_orientation = orientationFromBasis(rightW, upW, forwardW);
}

glm::mat4 Camera3D::viewMatrixRTE() const
{
    return glm::mat4(glm::mat3(glm::transpose(glm::mat3_cast(m_orientation))));
}

glm::dmat4 Camera3D::viewMatrix() const
{
    glm::dmat4 view(glm::transpose(glm::mat3_cast(m_orientation)));
    view[3] = glm::dvec4(-(glm::dmat3(view) * m_position), 1.0);
    return view;
}

glm::mat4 Camera3D::projectionReversedZ() const
{
    const double f = 1.0 / std::tan(m_fovY * 0.5);
    glm::dmat4 proj(0.0);
    proj[0][0] = f / aspectRatio();
    proj[1][1] = f;
    proj[2][3] = -1.0;   // w_clip = -z_eye
    proj[3][2] = m_near; // z_clip = near  ->  depth = near / -z_eye
    return glm::mat4(proj);
}

std::array<Plane, Camera3D::PlaneCount> Camera3D::frustumPlanes() const
{
    const glm::dvec3 r = right();
    const glm::dvec3 u = up();
    const glm::dvec3 fwd = forward();
    const double tanY = std::tan(m_fovY * 0.5);
    const double tanX = tanY * aspectRatio();

    auto throughEye = [this](const glm::dvec3& inwardNormal) {
        const glm::dvec3 n = glm::normalize(inwardNormal);
        return Plane{n, -glm::dot(n, m_position)};
    };

    std::array<Plane, PlaneCount> planes;
    // A side plane contains the eye and the edge direction (fwd +/- tan * axis).
    planes[Left] = throughEye(glm::cross(fwd - tanX * r, u));
    planes[Right] = throughEye(glm::cross(u, fwd + tanX * r));
    planes[Bottom] = throughEye(glm::cross(r, fwd - tanY * u));
    planes[Top] = throughEye(glm::cross(fwd + tanY * u, r));
    planes[Near] = Plane{fwd, -glm::dot(fwd, m_position + fwd * m_near)};
    return planes;
}

bool Camera3D::frustumContains(const glm::dvec3& pointEcef) const
{
    for (const Plane& plane : frustumPlanes()) {
        if (plane.signedDistance(pointEcef) < 0.0)
            return false;
    }
    return true;
}

Ray Camera3D::screenRay(double pixelX, double pixelY) const
{
    const double ndcX = 2.0 * pixelX / m_viewportWidth - 1.0;
    const double ndcY = 1.0 - 2.0 * pixelY / m_viewportHeight;
    const double tanY = std::tan(m_fovY * 0.5);

    const glm::dvec3 dirCamera(ndcX * tanY * aspectRatio(), ndcY * tanY, -1.0);
    return Ray{m_position, glm::normalize(m_orientation * dirCamera)};
}

glm::vec3 Camera3D::toRelativeToEye(const glm::dvec3& pointEcef) const
{
    return glm::vec3(pointEcef - m_position);
}

} // namespace scene3d
