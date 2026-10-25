#include "Camera3D.h"
#include "Geodesy.h"

#include <QtTest>
#include <cmath>

using scene3d::Camera3D;
using scene3d::Geodesy;

namespace {
bool near3(const glm::dvec3& a, const glm::dvec3& b, double tolerance)
{
    return glm::length(a - b) < tolerance;
}
} // namespace

/**
 * @brief Unit tests for Camera3D orientation, matrices, frustum and picking rays.
 */
class TestCamera3D : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void geodeticOrientation();
    void orientationOrthonormal();
    void lookAtTarget();
    void rteViewMatchesDoubleView();
    void reversedZDepth();
    void frustumContainsAndExcludes();
    void frustumMatchesScreenCorners();
    void screenRayCentreHitsNadir();
    void rteKeepsPrecisionNearSurface();

private:
    Camera3D m_camera;
};

void TestCamera3D::init()
{
    m_camera = Camera3D();
    m_camera.setViewportSize(1600, 900);
    m_camera.setFovYDegrees(60.0);
    m_camera.setNearPlane(1.0);
}

void TestCamera3D::geodeticOrientation()
{
    const double lat = 33.7;
    const double lon = 73.0;
    const glm::dmat3 enu = Geodesy::enuFrame(lat, lon);

    m_camera.setFromGeodetic(lat, lon, 1000.0, 0.0, 0.0, 0.0);
    QVERIFY(near3(m_camera.forward(), enu[1], 1e-12)); // north
    QVERIFY(near3(m_camera.right(), enu[0], 1e-12));   // east
    QVERIFY(near3(m_camera.up(), enu[2], 1e-12));      // up

    m_camera.setFromGeodetic(lat, lon, 1000.0, 90.0, 0.0, 0.0);
    QVERIFY(near3(m_camera.forward(), enu[0], 1e-12)); // heading 90 = east
    QVERIFY(near3(m_camera.right(), -enu[1], 1e-12));

    m_camera.setFromGeodetic(lat, lon, 1000.0, 0.0, -90.0, 0.0);
    QVERIFY(near3(m_camera.forward(), -enu[2], 1e-12)); // straight down
    QVERIFY(near3(m_camera.up(), enu[1], 1e-12));       // north at top of screen

    m_camera.setFromGeodetic(lat, lon, 1000.0, 0.0, 0.0, 90.0);
    QVERIFY(near3(m_camera.up(), enu[0], 1e-12));       // banked right: up tilts east
    QVERIFY(near3(m_camera.forward(), enu[1], 1e-12));

    QVERIFY(near3(m_camera.position(), Geodesy::geodeticToEcef(lat, lon, 1000.0), 1e-6));
}

void TestCamera3D::orientationOrthonormal()
{
    m_camera.setFromGeodetic(-20.0, 140.0, 5000.0, 37.0, -25.0, 12.0);
    const glm::dvec3 r = m_camera.right();
    const glm::dvec3 u = m_camera.up();
    const glm::dvec3 f = m_camera.forward();
    QVERIFY(std::abs(glm::length(r) - 1.0) < 1e-12);
    QVERIFY(std::abs(glm::length(u) - 1.0) < 1e-12);
    QVERIFY(std::abs(glm::dot(r, u)) < 1e-12);
    QVERIFY(std::abs(glm::dot(r, f)) < 1e-12);
    QVERIFY(near3(glm::cross(r, u), -f, 1e-12)); // right-handed, looking down -Z
}

void TestCamera3D::lookAtTarget()
{
    const glm::dvec3 target = Geodesy::geodeticToEcef(30.0, 70.0, 0.0);
    m_camera.setPosition(Geodesy::geodeticToEcef(29.0, 69.0, 200000.0));
    m_camera.lookAt(target);
    QVERIFY(near3(m_camera.forward(), glm::normalize(target - m_camera.position()), 1e-12));
    QVERIFY(glm::dot(m_camera.up(), Geodesy::surfaceNormal(29.0, 69.0)) > 0.0);
    QVERIFY(std::abs(glm::dot(m_camera.right(), Geodesy::surfaceNormal(29.0, 69.0))) < 1e-9);

    // Straight down falls back to north-up instead of a degenerate basis.
    m_camera.setPosition(Geodesy::geodeticToEcef(10.0, 20.0, 1000.0));
    m_camera.lookAt(Geodesy::geodeticToEcef(10.0, 20.0, 0.0));
    QVERIFY(near3(m_camera.forward(), -Geodesy::surfaceNormal(10.0, 20.0), 1e-9));
    QVERIFY(std::abs(glm::length(m_camera.right()) - 1.0) < 1e-12);
}

void TestCamera3D::rteViewMatchesDoubleView()
{
    m_camera.setFromGeodetic(45.0, 7.0, 3000.0, 120.0, -30.0, 0.0);
    const glm::dvec3 point = Geodesy::geodeticToEcef(45.01, 7.02, 500.0);

    const glm::dvec4 viewDouble = m_camera.viewMatrix() * glm::dvec4(point, 1.0);
    const glm::vec4 viewRte = m_camera.viewMatrixRTE() * glm::vec4(m_camera.toRelativeToEye(point), 1.0f);
    QVERIFY(near3(glm::dvec3(viewDouble), glm::dvec3(viewRte), 1e-2));
    QVERIFY(std::abs(m_camera.viewMatrixRTE()[3][0]) == 0.0f); // rotation only
}

void TestCamera3D::reversedZDepth()
{
    const glm::mat4 proj = m_camera.projectionReversedZ();
    auto depthAt = [&proj](float distance) {
        const glm::vec4 clip = proj * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
        return clip.z / clip.w;
    };
    QVERIFY(std::abs(depthAt(1.0f) - 1.0f) < 1e-6f);   // near plane -> 1
    QVERIFY(depthAt(1.0e7f) > 0.0f);                    // finite -> > 0
    QVERIFY(depthAt(1.0e7f) < 1e-6f);                   // far -> approaches 0
    QVERIFY(depthAt(10.0f) > depthAt(100.0f));          // closer = greater (GL_GREATER)

    // Top edge of the field of view maps to NDC y = 1.
    const float tanHalf = std::tan(glm::radians(30.0f));
    const glm::vec4 top = proj * glm::vec4(0.0f, tanHalf * 50.0f, -50.0f, 1.0f);
    QVERIFY(std::abs(top.y / top.w - 1.0f) < 1e-5f);
}

void TestCamera3D::frustumContainsAndExcludes()
{
    m_camera.setFromGeodetic(0.0, 0.0, 10000.0, 0.0, 0.0, 0.0); // looking north
    const glm::dvec3 eye = m_camera.position();
    const glm::dvec3 f = m_camera.forward();
    const glm::dvec3 r = m_camera.right();
    const glm::dvec3 u = m_camera.up();

    QVERIFY(m_camera.frustumContains(eye + f * 1000.0));
    QVERIFY(m_camera.frustumContains(eye + f * 1.0e9));            // infinite far
    QVERIFY(!m_camera.frustumContains(eye - f * 1000.0));          // behind
    QVERIFY(!m_camera.frustumContains(eye + f * 0.5));             // before near plane
    QVERIFY(!m_camera.frustumContains(eye + f * 100.0 + r * 1000.0)); // far right
    QVERIFY(!m_camera.frustumContains(eye + f * 100.0 - r * 1000.0)); // far left
    QVERIFY(!m_camera.frustumContains(eye + f * 100.0 + u * 100.0));  // above (tan 30 = 0.577)
    QVERIFY(m_camera.frustumContains(eye + f * 100.0 + u * 50.0));
    QVERIFY(m_camera.frustumContains(eye + f * 100.0 + r * 100.0)); // aspect widens X

    // Looking straight down from 1000 km, the nadir point is visible.
    m_camera.setFromGeodetic(30.0, 70.0, 1.0e6, 0.0, -90.0, 0.0);
    QVERIFY(m_camera.frustumContains(Geodesy::geodeticToEcef(30.0, 70.0, 0.0)));
    QVERIFY(!m_camera.frustumContains(Geodesy::geodeticToEcef(30.0, -110.0, 2.0e7)));
}

void TestCamera3D::frustumMatchesScreenCorners()
{
    m_camera.setFromGeodetic(-10.0, 100.0, 2000.0, 45.0, -20.0, 5.0);
    const auto planes = m_camera.frustumPlanes();
    const double corners[4][2] = {{0, 0}, {1600, 0}, {0, 900}, {1600, 900}};
    for (const auto& c : corners) {
        const scene3d::Ray ray = m_camera.screenRay(c[0], c[1]);
        const glm::dvec3 p = ray.origin + ray.direction * 5000.0;
        // On two side planes (distance ~0), inside the rest.
        int onBoundary = 0;
        for (int i = 0; i < Camera3D::Near; ++i) {
            const double d = planes[i].signedDistance(p);
            QVERIFY2(d > -1e-6, qPrintable(QStringLiteral("plane %1 d=%2").arg(i).arg(d)));
            if (std::abs(d) < 1e-6)
                ++onBoundary;
        }
        QCOMPARE(onBoundary, 2);
    }
    const scene3d::Ray centre = m_camera.screenRay(800.0, 450.0);
    QVERIFY(near3(centre.direction, m_camera.forward(), 1e-12));
}

void TestCamera3D::screenRayCentreHitsNadir()
{
    m_camera.setFromGeodetic(51.5, -0.12, 20000.0, 0.0, -90.0, 0.0);
    const scene3d::Ray ray = m_camera.screenRay(800.0, 450.0);
    double t = 0.0;
    QVERIFY(Geodesy::intersectRayEllipsoid(ray.origin, ray.direction, &t));
    QVERIFY(std::abs(t - 20000.0) < 1e-3);
    const glm::dvec3 hit = ray.origin + t * ray.direction;
    QVERIFY(near3(hit, Geodesy::geodeticToEcef(51.5, -0.12, 0.0), 1e-3));

    // A pixel at the top of the screen looks north of nadir.
    const scene3d::Ray upper = m_camera.screenRay(800.0, 0.0);
    QVERIFY(Geodesy::intersectRayEllipsoid(upper.origin, upper.direction, &t));
    const scene3d::GeodeticPosition geo = Geodesy::ecefToGeodetic(upper.origin + t * upper.direction);
    QVERIFY(geo.latitudeDeg > 51.5);
}

void TestCamera3D::rteKeepsPrecisionNearSurface()
{
    // Camera 1 m above the ground; a vertex 10 m away must stay stable to well under 1 mm.
    m_camera.setFromGeodetic(33.7, 73.0, 1.0, 0.0, 0.0, 0.0);
    const glm::dvec3 vertex = Geodesy::geodeticToEcef(33.70009, 73.0, 0.0);
    const glm::dvec3 exact = vertex - m_camera.position();

    const glm::vec3 rte = m_camera.toRelativeToEye(vertex);
    QVERIFY(glm::length(glm::dvec3(rte) - exact) < 1e-4);

    // Uploading absolute ECEF as float would be off by decimetres (why CLAUDE.md forbids it).
    const glm::vec3 naive = glm::vec3(vertex) - glm::vec3(m_camera.position());
    QVERIFY(glm::length(glm::dvec3(naive) - exact) > 1e-2);
}

QTEST_APPLESS_MAIN(TestCamera3D)
#include "tst_camera3d.moc"
