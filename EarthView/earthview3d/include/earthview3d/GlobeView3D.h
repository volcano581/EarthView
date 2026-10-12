#pragma once
#ifndef EARTHVIEW3D_GLOBEVIEW3D_H
#define EARTHVIEW3D_GLOBEVIEW3D_H

#include "earthview3d/CameraControl.h"
#include "earthview3d/EntityState3D.h"
#include "earthview3d/MapConfig.h"

#include <QOpenGLWidget>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace earthview3d {

/**
 * @brief osgEarth globe hosted in a QOpenGLWidget: terrain, imagery, sky, entities, cameras.
 *
 * Host-independent (OSGEARTH_INTEGRATION.md §3.1): the host converts its simulation snapshot
 * into EntityState3D records and calls setEntities() from the pre-frame callback (see
 * DOCTRINE_WIRING.md). This header includes no osgEarth/OSG headers (D-011); everything
 * osgEarth lives in the private implementation.
 *
 * The widget sets its own compatibility-profile surface format, so hosts may keep a core
 * profile as their default. GUI thread only.
 *
 * Entities: MapConfig::modelTable (models.json, see ModelTable.h) maps kindKey to a model
 * file; unmatched kinds, missing files and far-away models (below the table's
 * iconBelowPixels) are drawn as placeholder markers that never get smaller than a few pixels.
 * Labels are decluttered by osgEarth's screen-space layout. Left-click picks an entity.
 *
 * Cameras (CameraControl.h): orbit, chase, stealth, first person and free-fly, with a HUD
 * overlay. Every mode except orbit keeps the eye at least 2 m above the terrain each frame.
 * Keys: O orbit, T chase, F stealth, P first person, G free-fly (WASD, Q/E down/up, Shift
 * fast), L chase free-look, J HUD, K ground-offset overlay, N next entity, H home. Left-drag
 * looks around.
 *
 * Diagnostics (environment): EARTHVIEW3D_NO_SKY=1 disables the sky,
 * EARTHVIEW3D_LOGDEPTH=1 installs osgEarth's logarithmic depth buffer (off by default, D-010).
 */
class GlobeView3D : public QOpenGLWidget
{
    Q_OBJECT

public:
    /// @param homeLatDeg, homeLonDeg Home viewpoint focal point (e.g. the scenario origin).
    GlobeView3D(const MapConfig& config, double homeLatDeg, double homeLonDeg, QWidget* parent = nullptr);
    ~GlobeView3D() override;

    const MapConfig& mapConfig() const;

    /// Called on the GUI thread right before each frame; the owner pushes entities here.
    void setPreFrameCallback(std::function<void()> callback);

    /// Add/update/remove entity visuals so the scene matches @p entities (diff by id).
    void setEntities(const std::vector<EntityState3D>& entities);
    /// Number of entity visuals currently in the scene.
    int entityCount() const;

    void goHome();            ///< Orbit mode, fly to the home viewpoint
    void selectNextEntity();

    /// Switch camera mode with a smooth transition. Chase, stealth and first person follow the
    /// selected entity (the first entity if none is selected). A mode requested before the
    /// first frame (or before any entity exists) is applied as soon as possible.
    void setCameraMode(CameraMode mode);
    CameraMode cameraMode() const;
    void chaseSelected();       ///< setCameraMode(Chase)
    void stealthSelected();     ///< setCameraMode(Stealth)
    void firstPersonSelected(); ///< setCameraMode(FirstPerson)
    void freeFly();             ///< setCameraMode(FreeFly)
    void untether();            ///< setCameraMode(Orbit) from the current eye

    /// Stealth eye height above the terrain (metres, default 10, at least 2).
    void setStealthEyeHeight(double metres);
    double stealthEyeHeight() const;
    /// Chase: false = camera stays behind the target heading; true = drag to orbit the target.
    void setChaseFreeLook(bool freeLook);
    bool chaseFreeLook() const;
    /// HUD overlay: mode, heading, pitch, altitude, AGL, speed, target (on by default).
    void setHudVisible(bool visible);
    bool hudVisible() const;

    // ── Terrain services (E5) ── heights are metres above the WGS-84 ellipsoid (HAE).
    /// Height of the drawn terrain at a point, cached (~1 m cells, refreshed as tiles load).
    /// Safe from any thread: on the GUI thread a miss is queried at once; on other threads a
    /// miss returns false and is resolved on the next frame (ask again later).
    bool heightAt(double latDeg, double lonDeg, double* altM) const;
    /// Height from the elevation source at full resolution (independent of the tiles drawn;
    /// slower, may read files). For spot checks and tools, not per-frame use.
    bool sourceHeightAt(double latDeg, double lonDeg, double* altM, double* resolutionM = nullptr) const;
    /// EGM96 geoid height N (m) at a point: MSL = HAE − N. 0 when the geoid is unavailable.
    double geoidHeight(double latDeg, double lonDeg) const;
    bool haveGeoid() const;
    /// Debug overlay (HUD) of terrain − host altitude for clamped entities (I-015); key K.
    void setGroundOffsetOverlay(bool show);
    bool groundOffsetOverlay() const;
    /// Terrain − host altitude over the clamped entities of the last setEntities() call.
    GroundOffsetStats groundOffsetStats() const;

    std::uint64_t selectedId() const;
    /// Select entity @p id (0 clears): highlight ring + label emphasis, emits selectionChanged.
    /// Does not move the camera; call chaseSelected()/stealthSelected() for that.
    void setSelected(quint64 id);
    /// Show or hide entity labels (decluttered; on by default).
    void setLabelsVisible(bool visible);
    /// Where entity @p id is placed in the scene (after terrain clamping): WGS-84 degrees and
    /// metres above the ellipsoid. Returns false if the entity is unknown. For checks/tests.
    bool entityPosition(std::uint64_t id, double* latDeg, double* lonDeg, double* altM) const;
    QString selectedLabel() const;
    /// Camera state for logs/checks: orbit → osgEarth viewpoint; other modes → eye position,
    /// AGL, heading/pitch and range to the target. Ends with the smoothed frame time.
    QString viewpointDescription() const;
    bool saveSnapshot(const QString& pngPath);

signals:
    void selectionChanged(QString label);
    /// The user clicked entity @p id in the view (it is selected as well).
    void entityPicked(quint64 id);
    /// The camera mode changed (also when a pending mode is applied).
    void cameraModeChanged(earthview3d::CameraMode mode);
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
    void focusOutEvent(QFocusEvent* e) override;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace earthview3d

#endif // EARTHVIEW3D_GLOBEVIEW3D_H
