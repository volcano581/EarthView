#include "MapViewWindow.h"
#include "Camera.h"
#include "MapWidget.h"
#include "MercatorProjection.h"
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
#include "MapControlsOverlay.h"
#endif

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QStatusBar>
#include <QToolBar>

MapViewWindow::MapViewWindow(const QString& title, QWidget* parent)
    : QMainWindow(parent)
    , m_mapWidget(new MapWidget(this))
    , m_mapControls(nullptr)
    , m_coordLabel(nullptr)
    , m_zoomLabel(nullptr)
{
    setWindowFlag(Qt::Window, true);
    setWindowTitle(title);
    resize(800, 520);

    setCentralWidget(m_mapWidget);
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
    m_mapControls = (new MapControlsOverlay(m_mapWidget))->model();
#endif
    setupToolbar();
    createStatusBar();
    setFocusProxy(m_mapWidget);
    m_mapWidget->setFocus(Qt::OtherFocusReason);

    connect(m_mapWidget->camera(), &Camera::cameraChanged, this, &MapViewWindow::updateStatus);
}

void MapViewWindow::closeEvent(QCloseEvent* event)
{
    emit windowVisibilityChanged(false);
    QMainWindow::closeEvent(event);
}

void MapViewWindow::showEvent(QShowEvent* event)
{
    emit windowVisibilityChanged(true);
    m_mapWidget->setFocus(Qt::OtherFocusReason);
    QMainWindow::showEvent(event);
}

void MapViewWindow::setupToolbar()
{
    QToolBar* toolbar = addToolBar("Navigation");
    toolbar->addAction("Zoom In", [this]() {
        m_mapWidget->camera()->zoom(0.5f);
    });
    toolbar->addAction("Zoom Out", [this]() {
        m_mapWidget->camera()->zoom(-0.5f);
    });
    toolbar->addAction("Reset View", [this]() {
        m_mapWidget->camera()->setCenter(QPointF(0, 0));
        m_mapWidget->camera()->setZoomLevel(2.0);
    });

    QAction* loadBordersAction = new QAction("Load Borders...", this);
    toolbar->addSeparator();
    toolbar->addAction(loadBordersAction);

    QAction* texturesVisibleAction = new QAction("Textures", this);
    texturesVisibleAction->setCheckable(true);
    texturesVisibleAction->setChecked(true);
#ifndef EARTHVIEW_HAS_MAP_CONTROLS  // In the layers panel instead.
    toolbar->addAction(texturesVisibleAction);
#endif

    QAction* bordersVisibleAction = new QAction("Borders", this);
    bordersVisibleAction->setCheckable(true);
    bordersVisibleAction->setChecked(true);
#ifndef EARTHVIEW_HAS_MAP_CONTROLS  // In the layers panel instead.
    toolbar->addAction(bordersVisibleAction);
#endif

    QAction* gridVisibleAction = new QAction("Grid", this);
    gridVisibleAction->setCheckable(true);
    gridVisibleAction->setChecked(true);
#ifndef EARTHVIEW_HAS_MAP_CONTROLS  // In the layers panel instead.
    toolbar->addAction(gridVisibleAction);
#endif

    QAction* citiesVisibleAction = new QAction("Cities", this);
    citiesVisibleAction->setCheckable(true);
    citiesVisibleAction->setChecked(true);
#ifndef EARTHVIEW_HAS_MAP_CONTROLS  // In the layers panel instead.
    toolbar->addAction(citiesVisibleAction);
#endif

    QAction* terrainVisibleAction = new QAction("Terrain", this);
    terrainVisibleAction->setCheckable(true);
    terrainVisibleAction->setChecked(true);
#ifndef EARTHVIEW_HAS_MAP_CONTROLS  // In the layers panel instead.
    toolbar->addAction(terrainVisibleAction);
#endif

    QAction* wrapLongitudeAction = new QAction("Wrap Longitude", this);
    wrapLongitudeAction->setCheckable(true);
    toolbar->addSeparator();
    toolbar->addAction(wrapLongitudeAction);


    connect(loadBordersAction, &QAction::triggered, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(
            this,
            "Load Border Shapefile",
            QDir::currentPath(),
            "ESRI Shapefile (*.shp)");
        if (filePath.isEmpty())
            return;

        QString message;
        if (m_mapWidget->loadBorderShapefile(filePath, &message)) {
            statusBar()->showMessage(message, 4000);
        }
        else {
            statusBar()->showMessage(
                QString("Could not load %1: %2").arg(QFileInfo(filePath).fileName(), message),
                6000);
        }
    });

    connect(texturesVisibleAction, &QAction::toggled, m_mapWidget, &MapWidget::setTexturesVisible);
    connect(bordersVisibleAction, &QAction::toggled, m_mapWidget, &MapWidget::setBordersVisible);
    connect(gridVisibleAction, &QAction::toggled, m_mapWidget, &MapWidget::setGridVisible);
    connect(citiesVisibleAction, &QAction::toggled, m_mapWidget, &MapWidget::setCitiesVisible);
    connect(terrainVisibleAction, &QAction::toggled, m_mapWidget, &MapWidget::setTerrainVisible);

    connect(wrapLongitudeAction, &QAction::toggled, this, [this](bool enabled) {
        m_mapWidget->camera()->setHorizontalWrapEnabled(enabled);
    });
    // Projection is switched from the on-map Settings panel (MapControlsOverlay).
    connect(m_mapWidget->camera(), &Camera::projectionModeChanged, this, [this](Camera::ProjectionMode mode) {
        statusBar()->showMessage(
            mode == Camera::ProjectionMode::Orthographic
                ? "Orthographic globe view enabled"
                : "Mercator map view enabled",
            2000);
    });
}

void MapViewWindow::createStatusBar()
{
    statusBar()->show();
    m_coordLabel = new QLabel(QString("Center: 0.00%1N, 0.00%1E").arg(QChar(0x00B0)), this);
    m_zoomLabel = new QLabel("Zoom: 2.0", this);

    statusBar()->addWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
}

void MapViewWindow::updateStatus()
{
    QPointF center = m_mapWidget->camera()->getCenterMercator();
    QPointF latLon = MercatorProjection::mercatorToLatLon(center.x(), center.y());
    m_coordLabel->setText(QString("Center: %1%3N, %2%3E")
        .arg(latLon.x(), 0, 'f', 2)
        .arg(latLon.y(), 0, 'f', 2)
        .arg(QChar(0x00B0)));
    m_zoomLabel->setText(QString("Zoom: %1")
        .arg(m_mapWidget->camera()->getZoomLevel(), 0, 'f', 1));
}
