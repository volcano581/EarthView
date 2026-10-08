#pragma once

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>

#include <osg/MatrixTransform>
#include <osg/ref_ptr>
#include <osgViewer/GraphicsWindow>
#include <osgViewer/Viewer>

#include <osgEarth/Viewpoint>

namespace osgEarth {
class GeoTransform;
class MapNode;
namespace Util { class EarthManipulator; }
}

/**
 * @brief Hosts an osgEarth scene inside a Qt6 QOpenGLWidget.
 *
 * OSG renders into Qt's FBO through an embedded graphics window. Spike-only:
 * single-threaded viewer, one moving test entity, tether/free camera keys.
 */
class OsgWidget : public QOpenGLWidget
{
    Q_OBJECT

public:
    OsgWidget(const QString& earthFile, double lat, double lon, QWidget* parent = nullptr);
    ~OsgWidget() override;

    /// Current camera viewpoint as text (diagnostics).
    QString viewpointDescription() const;

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
    void buildScene();
    osg::Node* createEntityModel() const;
    void updateEntity();
    void tether(double rangeMeters, double pitchDeg);
    osgEarth::Viewpoint homeViewpoint() const;
    void goHome();
    osgGA::EventQueue* eventQueue() const;
    float scaledX(double x) const { return float(x * devicePixelRatioF()); }
    float scaledY(double y) const { return float(y * devicePixelRatioF()); }

private:
    QString m_earthFile;
    double m_lat;
    double m_lon;
    double m_groundMsl = 0.0;
    bool m_homeApplied = false;

    osg::ref_ptr<osgViewer::Viewer> m_viewer;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> m_window;
    osg::ref_ptr<osgEarth::MapNode> m_mapNode;
    osg::ref_ptr<osgEarth::Util::EarthManipulator> m_manip;
    osg::ref_ptr<osgEarth::GeoTransform> m_entity;
    osg::ref_ptr<osg::MatrixTransform> m_entityAttitude;

    QTimer m_frameTimer;
    QElapsedTimer m_clock;
};
