#pragma once

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QString>
#include <QTimer>

#include <osg/MatrixTransform>
#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>

#include <osgEarth/Viewpoint>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace osgEarth {
class GeoTransform;
class MapNode;
namespace Util { class EarthManipulator; }
}

// ─────────────────────────────────────────────────────────────────────────────
// GlobeView3D — osgEarth globe hosted in a QOpenGLWidget.
//
// Deliberately free of host-application types (OSGEARTH_INTEGRATION.md §3.1):
// the host's adapter converts its simulation snapshot into EntityState3D and
// calls setEntities() from the pre-frame callback (see DOCTRINE_WIRING.md).
//
// Embedded-osgEarth requirements learned in the EarthView spike (§3.3):
//   - per-widget compatibility-profile surface format (the host's default may be core)
//   - vertex-attribute aliasing + matrix uniforms set by hand (no realize operation)
//   - Qt's FBO id handed to OSG every frame
//   - MapNode::getTerrain() is null until the first frame
//   - home viewpoint registered with EarthManipulator, applied after the first frame
// GUI thread only.
// ─────────────────────────────────────────────────────────────────────────────
class GlobeView3D : public QOpenGLWidget
{
    Q_OBJECT

public:
    /// One entity as the 3D view needs it. Heights are above the WGS-84 ellipsoid.
    struct EntityState3D
    {
        std::uint64_t id = 0;
        double   latDeg = 0.0;
        double   lonDeg = 0.0;
        double   altM = 0.0;
        double   headingDeg = 0.0;   // clockwise from north
        int      shape = 0;          // 0 infantry, 1 air, 2 armour, 3 artillery
        std::uint32_t rgba = 0xFFFFFFFFu;
        QString  label;
        /// Place on the 3D terrain instead of using altM (ground units while the
        /// sim's ground does not match the DTED surface; integration design §5.4).
        bool     clampToTerrain = false;
    };

    GlobeView3D(const QString& earthFile, double homeLatDeg, double homeLonDeg, QWidget* parent = nullptr);
    ~GlobeView3D() override;

    /// Called on the GUI thread right before each frame; the owner pushes entities here.
    void setPreFrameCallback(std::function<void()> callback) { m_preFrame = std::move(callback); }

    /// Add/update/remove entity visuals so the scene matches `entities` (diff by id).
    void setEntities(const std::vector<EntityState3D>& entities);

    void goHome();
    void selectNextEntity();
    void chaseSelected();     // tethered camera behind the selected entity
    void stealthSelected();   // close, near eye level
    void untether();

    QString selectedLabel() const;
    QString viewpointDescription() const;
    bool saveSnapshot(const QString& pngPath);

signals:
    void selectionChanged(QString label);
    void loadFailed(QString reason);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

private:
    struct EntityVisual
    {
        osg::ref_ptr<osgEarth::GeoTransform> geo;
        osg::ref_ptr<osg::MatrixTransform> attitude;
        int shape = -1;
        std::uint32_t rgba = 0;
        QString label;
        bool seen = false;
        double groundAltM = 0.0;    // last terrain height found (tiles may still be loading)
        bool haveGround = false;
    };

    void buildScene();
    osg::Node* createShape(int shape, std::uint32_t rgba) const;
    EntityVisual& createEntity(const EntityState3D& state);
    void updateEntity(EntityVisual& visual, const EntityState3D& state);
    void tether(double rangeMeters, double pitchDeg);
    osgEarth::Viewpoint homeViewpoint() const;
    osgGA::EventQueue* eventQueue() const;
    float scaledX(double x) const { return float(x * devicePixelRatioF()); }
    float scaledY(double y) const { return float(y * devicePixelRatioF()); }

private:
    QString m_earthFile;
    double m_homeLat;
    double m_homeLon;

    osg::ref_ptr<osgViewer::Viewer> m_viewer;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> m_window;
    osg::ref_ptr<osgEarth::MapNode> m_mapNode;
    osg::ref_ptr<osgEarth::Util::EarthManipulator> m_manip;
    osg::ref_ptr<osg::Group> m_entityRoot;

    std::unordered_map<std::uint64_t, EntityVisual> m_entities;
    std::uint64_t m_selectedId = 0;
    bool m_homeApplied = false;

    std::function<void()> m_preFrame;
    QTimer m_frameTimer;
};
