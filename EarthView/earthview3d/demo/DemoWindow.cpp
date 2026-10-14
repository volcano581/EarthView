#include "DemoWindow.h"

#include "Camera.h"
#include "MapSourceCatalog.h"
#include "MapWidget.h"
#include "MercatorProjection.h"

#include <earthview3d/GlobeView3D.h>

#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QGridLayout>
#include <QMenuBar>
#include <QPainter>
#include <QSignalBlocker>
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
    // The 2D pane needs a map source of its own: it is EarthView's MapWidget, but the demo is
    // built without MainWindow, which is where the 2D viewer discovers its sources. Without
    // this it keeps TMSLoader's built-in default, online OpenStreetMap, and stays blank on an
    // offline machine (I-041). Same reader as the 2D application, so the same files work.
    m_sources2D = MapSourceCatalog::discoverMapSources();
    if (!m_sources2D.isEmpty())
        applySource2D(m_sources2D.first());

    auto* open3D = new QAction(QStringLiteral("Open 3D View"), this);
    open3D->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    connect(open3D, &QAction::triggered, this, [this] { open3DView(); });
    menuBar()->addMenu(QStringLiteral("&View"))->addAction(open3D);
    QToolBar* toolBar = addToolBar(QStringLiteral("View"));
    toolBar->addAction(open3D);

    // Map source dropdown: one entry per earth file found (filled by setMapCatalog).
    toolBar->addSeparator();
    toolBar->addWidget(new QLabel(QStringLiteral(" Map: "), toolBar));
    m_mapCombo = new QComboBox(toolBar);
    m_mapCombo->setToolTip(QStringLiteral("Map source for the 3D view (one entry per earth file found)."));
    m_mapCombo->setEnabled(false);
    toolBar->addWidget(m_mapCombo);
    connect(m_mapCombo, &QComboBox::currentIndexChanged, this, &DemoWindow::selectMap);
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

void DemoWindow::setMapCatalog(const QList<MapCatalogEntry>& entries, int currentIndex)
{
    m_catalog = entries;
    m_mapIndex = (currentIndex >= 0 && currentIndex < entries.size()) ? currentIndex : 0;

    QSignalBlocker blocker(m_mapCombo);
    m_mapCombo->clear();
    for (const MapCatalogEntry& entry : entries)
        m_mapCombo->addItem(entry.label, entry.earthFile);
    m_mapCombo->setCurrentIndex(m_mapIndex);
    m_mapCombo->setEnabled(entries.size() > 1);
    if (!entries.isEmpty()) {
        m_mapConfig.earthFile = entries.at(m_mapIndex).earthFile;
        applySource2D(source2DFor(entries.at(m_mapIndex)));   // 2D pane follows the dropdown
    }
}

void DemoWindow::applySource2D(const MapSourceCatalog::Source& source)
{
    if (source.layers.isEmpty())
        return;   // nothing 2D can read (a standard osgEarth map): leave the pane as it is
    m_map->setTileSourceLayers(source.layers);
    m_map->setDtedDirectory(source.dtedDirectory);
}

MapSourceCatalog::Source DemoWindow::source2DFor(const MapCatalogEntry& entry) const
{
    // The 3D catalog and the 2D sources are discovered separately but come from the same
    // files, so match on the file the entry was built from.
    const QString wanted = QFileInfo(entry.sourcePath).absoluteFilePath();
    for (const MapSourceCatalog::Source& source : m_sources2D) {
        if (QFileInfo(source.filePath).absoluteFilePath() == wanted)
            return source;
    }
    return {};
}

void DemoWindow::selectMap(int index)
{
    if (index < 0 || index >= m_catalog.size() || index == m_mapIndex)
        return;
    m_mapIndex = index;
    const MapCatalogEntry& entry = m_catalog.at(index);
    m_mapConfig.earthFile = entry.earthFile;
    applySource2D(source2DFor(entry));
    statusBar()->showMessage(QStringLiteral("Map: %1 (%2)").arg(entry.label, QFileInfo(entry.earthFile).fileName()));
    if (!m_window3D)
        return;
    // osgEarth loads the map when the view is built, so switching rebuilds the 3D window.
    m_window3D->close();        // WA_DeleteOnClose; m_window3D/m_view3D are QPointers
    m_window3D = nullptr;
    m_view3D = nullptr;
    open3DView();
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
