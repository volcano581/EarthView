#include "DemoWindow.h"

#include "Camera.h"
#include "MapWidget.h"
#include "MercatorProjection.h"

#include <earthview3d/GlobeView3D.h>

#include <QAction>
#include <QMenu>
#include <QGridLayout>
#include <QMenuBar>
#include <QPainter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <cmath>
#include <memory>

namespace earthview3d::demo {

namespace {
QColor toQColor(std::uint32_t rgba)
{
    return QColor(int((rgba >> 24) & 0xFF), int((rgba >> 16) & 0xFF), int((rgba >> 8) & 0xFF), int(rgba & 0xFF));
}
} // namespace

EntityOverlay2D::EntityOverlay2D(MapWidget* map, const SnapshotBuffer* buffer, QWidget* parent)
    : QWidget(parent)
    , m_map(map)
    , m_buffer(buffer)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (m_buffer->consumeIfNew(&m_lastTick, &m_entities))
            update();
    });
    timer->start(33);
}

void EntityOverlay2D::paintEvent(QPaintEvent*)
{
    const Camera* camera = m_map->camera();
    if (!camera)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPointSizeF(8.0);
    painter.setFont(font);

    for (const EntityState3D& e : m_entities) {
        if (!e.visible)
            continue;
        QPointF screen;
        if (!camera->projectMercatorToScreen(MercatorProjection::latLonToMercator(e.latDeg, e.lonDeg), &screen))
            continue;
        const QColor color = toQColor(e.rgba);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.setBrush(color);
        painter.drawEllipse(screen, 5.0, 5.0);
        // Heading tick (clockwise from north; screen y grows downwards).
        const double h = e.headingDeg * 3.14159265358979323846 / 180.0;
        painter.setPen(QPen(color, 2.0));
        painter.drawLine(screen, screen + QPointF(12.0 * std::sin(h), -12.0 * std::cos(h)));
        painter.setPen(Qt::white);
        painter.drawText(screen + QPointF(8.0, -6.0), e.label);
    }
}

DemoWindow::DemoWindow(const MapConfig& mapConfig, const SyntheticFeed::Settings& feedSettings,
                       const SnapshotBuffer* buffer, QWidget* parent)
    : QMainWindow(parent)
    , m_mapConfig(mapConfig)
    , m_feedSettings(feedSettings)
    , m_buffer(buffer)
{
    setWindowTitle(QStringLiteral("earthview3d demo — 2D map"));

    // Map and overlay share one grid cell, so the overlay always covers the map exactly.
    m_mapContainer = new QWidget(this);
    auto* layout = new QGridLayout(m_mapContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    m_map = new MapWidget(m_mapContainer);
    m_overlay = new EntityOverlay2D(m_map, buffer, m_mapContainer);
    layout->addWidget(m_map, 0, 0);
    layout->addWidget(m_overlay, 0, 0);
    setCentralWidget(m_mapContainer);
    m_map->loadWorldBorders();

    auto* open3D = new QAction(QStringLiteral("Open 3D View"), this);
    open3D->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    connect(open3D, &QAction::triggered, this, [this] { open3DView(); });
    menuBar()->addMenu(QStringLiteral("&View"))->addAction(open3D);
    addToolBar(QStringLiteral("View"))->addAction(open3D);
    statusBar()->showMessage(QStringLiteral("%1 synthetic entities around %2, %3 — Ctrl+3 opens the 3D view")
                                 .arg(feedSettings.entityCount)
                                 .arg(feedSettings.originLatDeg, 0, 'f', 4)
                                 .arg(feedSettings.originLonDeg, 0, 'f', 4));

    // Centre the 2D map on the origin once the GL widget exists (zoom first: it clamps the centre).
    QTimer::singleShot(0, this, [this] {
        m_map->camera()->setZoomLevel(11.0);
        m_map->camera()->setCenter(MercatorProjection::latLonToMercator(m_feedSettings.originLatDeg,
                                                                        m_feedSettings.originLonDeg));
    });
}

GlobeView3D* DemoWindow::view3D() const
{
    return m_view3D.data();
}

GlobeView3D* DemoWindow::open3DView()
{
    if (m_window3D) {
        m_window3D->raise();
        m_window3D->activateWindow();
        return m_view3D;
    }

    auto* window = new QMainWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QStringLiteral("earthview3d demo — 3D view"));
    auto* view = new GlobeView3D(m_mapConfig, m_feedSettings.originLatDeg, m_feedSettings.originLonDeg, window);
    window->setCentralWidget(view);
    window->statusBar()->showMessage(QStringLiteral(
        "Click: select  N: next  O: orbit  T: chase  F: stealth  P: first person  G: free-fly (WASD QE, Shift)"
        "  L: chase free-look  J: HUD  H: home  Drag: look  Wheel: range/height/speed"));
    connect(view, &GlobeView3D::cameraModeChanged, window, [window, view](earthview3d::CameraMode mode) {
        window->statusBar()->showMessage(QStringLiteral("Camera: %1 %2").arg(earthview3d::cameraModeName(mode),
                                                                            view->selectedLabel()));
    });
    connect(view, &GlobeView3D::selectionChanged, window, [window](const QString& label) {
        window->statusBar()->showMessage(label.isEmpty() ? QStringLiteral("No selection")
                                                         : QStringLiteral("Selected %1 — click another entity or press N for the next").arg(label));
    });
    connect(view, &GlobeView3D::entityPicked, window, [window, view](quint64 id) {
        window->statusBar()->showMessage(QStringLiteral("Picked entity %1 (%2)").arg(id).arg(view->selectedLabel()));
    });
    connect(view, &GlobeView3D::loadFailed, window, [window](const QString& reason) {
        window->statusBar()->showMessage(QStringLiteral("Map load failed: %1").arg(reason));
    });

    // Host-contract pattern: per frame, copy the snapshot only when a new tick arrived.
    auto lastTick = std::make_shared<std::uint64_t>(0);
    auto entities = std::make_shared<std::vector<EntityState3D>>();
    const SnapshotBuffer* buffer = m_buffer;
    view->setPreFrameCallback([view, buffer, lastTick, entities] {
        if (buffer->consumeIfNew(lastTick.get(), entities.get()))
            view->setEntities(*entities);
    });

    window->resize(1280, 800);
    window->show();
    m_window3D = window;
    m_view3D = view;
    return view;
}

AgreementReport DemoWindow::checkAgreement() const
{
    const GlobeView3D* view = m_view3D.data();
    return compareHorizontal(m_overlay->entities(), [view](std::uint64_t id, double* lat, double* lon) {
        return view && view->entityPosition(id, lat, lon, nullptr);
    });
}

QImage DemoWindow::grab2D() const
{
    // Compose the GL map with the overlay on top (QWidget::grab includes the GL content).
    return m_mapContainer->grab().toImage();
}

} // namespace earthview3d::demo
