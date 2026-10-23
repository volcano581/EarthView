#include "MainWindow.h"
#include "MapWidget.h"
#include "MapViewWindow.h"
#include "Camera.h"
#include "MapSourceCatalog.h"
#include "MercatorProjection.h"
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
#include "MapControlsOverlay.h"
#endif
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_mapWidget(nullptr)
    , m_secondaryWindow(nullptr)
    , m_coordLabel(nullptr)
    , m_zoomLabel(nullptr)
{
    setupUI();
    createStatusBar();
    setWindowTitle("Military GIS Map Application");
    resize(1280, 800);
}

void MainWindow::setupUI()
{
    // Create toolbar
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

    // Create map widgets
    m_mapWidget = new MapWidget(this);
    m_secondaryWindow = new MapViewWindow("EarthView Secondary View", this);

    const QList<MapSourceCatalog::Source> mapSources = MapSourceCatalog::discoverMapSources();
    if (!mapSources.isEmpty()) {
        m_mapWidget->setTileSourceLayers(mapSources.first().layers);
        m_secondaryWindow->mapWidget()->setTileSourceLayers(mapSources.first().layers);
        m_mapWidget->setDtedDirectory(mapSources.first().dtedDirectory);
        m_secondaryWindow->mapWidget()->setDtedDirectory(mapSources.first().dtedDirectory);
    }
    else {
        m_mapWidget->setTileServerUrl("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
        m_secondaryWindow->mapWidget()->setTileServerUrl("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    }
    m_mapWidget->loadWorldBorders();
    m_mapWidget->loadCities();
    m_secondaryWindow->mapWidget()->loadWorldBorders();
    m_secondaryWindow->mapWidget()->loadCities();

    setCentralWidget(m_mapWidget);
    m_mapWidget->setFocus(Qt::OtherFocusReason);
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
    new MapControlsOverlay(m_mapWidget);
#endif

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Map Source:", this));
    QComboBox* sourceCombo = new QComboBox(this);
    sourceCombo->setMinimumContentsLength(20);
    sourceCombo->setToolTip("Select an offline MBTiles source or a map source loaded from a .earth file.");

    for (int i = 0; i < mapSources.size(); ++i) {
        const MapSourceCatalog::Source& source = mapSources.at(i);
        sourceCombo->addItem(
            QString("%1 (%2 layers)").arg(source.label).arg(source.layers.size()),
            i);
    }
    if (mapSources.isEmpty()) {
        sourceCombo->addItem("OpenStreetMap fallback", -1);
    }

    toolbar->addWidget(sourceCombo);

    auto applySourceSelection = [this, mapSources](int sourceIndex) {
        if (sourceIndex >= 0 && sourceIndex < mapSources.size()) {
            const MapSourceCatalog::Source& source = mapSources.at(sourceIndex);
            m_mapWidget->setTileSourceLayers(source.layers);
            m_secondaryWindow->mapWidget()->setTileSourceLayers(source.layers);
            m_mapWidget->setDtedDirectory(source.dtedDirectory);
            m_secondaryWindow->mapWidget()->setDtedDirectory(source.dtedDirectory);
            statusBar()->showMessage(
                QString("Map source set to %1 from %2")
                    .arg(source.label, QFileInfo(source.filePath).fileName()),
                3000);
            return;
        }

        m_mapWidget->setTileServerUrl("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
        m_secondaryWindow->mapWidget()->setTileServerUrl("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
        statusBar()->showMessage("Map source set to OpenStreetMap fallback", 3000);
    };

    connect(sourceCombo, &QComboBox::activated, this, [sourceCombo, applySourceSelection](int index) {
        applySourceSelection(sourceCombo->itemData(index).toInt());
        });

    QAction* loadBordersAction = new QAction("Load Borders...", this);
    toolbar->addSeparator();
    toolbar->addAction(loadBordersAction);

    QAction* texturesVisibleAction = new QAction("Textures", this);
    texturesVisibleAction->setCheckable(true);
    texturesVisibleAction->setChecked(true);
    texturesVisibleAction->setToolTip("Show or hide map imagery.");
    toolbar->addAction(texturesVisibleAction);

    QAction* bordersVisibleAction = new QAction("Borders", this);
    bordersVisibleAction->setCheckable(true);
    bordersVisibleAction->setChecked(true);
    bordersVisibleAction->setToolTip("Show or hide shapefile borders.");
    toolbar->addAction(bordersVisibleAction);

    QAction* gridVisibleAction = new QAction("Grid", this);
    gridVisibleAction->setCheckable(true);
    gridVisibleAction->setChecked(true);
    gridVisibleAction->setToolTip("Show or hide latitude and longitude grid.");
    toolbar->addAction(gridVisibleAction);

    QAction* citiesVisibleAction = new QAction("Cities", this);
    citiesVisibleAction->setCheckable(true);
    citiesVisibleAction->setChecked(true);
    citiesVisibleAction->setToolTip("Show or hide city and settlement labels.");
    toolbar->addAction(citiesVisibleAction);

    QAction* terrainVisibleAction = new QAction("Terrain", this);
    terrainVisibleAction->setCheckable(true);
    terrainVisibleAction->setChecked(true);
    terrainVisibleAction->setToolTip("Show or hide DEM terrain overlay.");
    toolbar->addAction(terrainVisibleAction);

    QAction* secondaryWindowAction = new QAction("Secondary Window", this);
    secondaryWindowAction->setCheckable(true);
    secondaryWindowAction->setChecked(true);
    secondaryWindowAction->setToolTip("Show or hide the independent secondary map window.");
    toolbar->addAction(secondaryWindowAction);

    QAction* wrapLongitudeAction = new QAction("Wrap Longitude", this);
    wrapLongitudeAction->setCheckable(true);
    wrapLongitudeAction->setToolTip("Repeat imagery horizontally instead of constraining longitude to +/-180 degrees.");
    toolbar->addSeparator();
    toolbar->addAction(wrapLongitudeAction);

    // Projection lives in the on-map Settings panel; kept in the View menu only.
    QAction* globeViewAction = new QAction("Globe View", this);
    globeViewAction->setCheckable(true);
    globeViewAction->setToolTip("Switch between flat Mercator map and orthographic globe view.");

    QMenu* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(loadBordersAction);

    QMenu* viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(texturesVisibleAction);
    viewMenu->addAction(bordersVisibleAction);
    viewMenu->addAction(gridVisibleAction);
    viewMenu->addAction(citiesVisibleAction);
    viewMenu->addAction(terrainVisibleAction);
    viewMenu->addAction(secondaryWindowAction);
    viewMenu->addSeparator();
    viewMenu->addAction(wrapLongitudeAction);
    viewMenu->addAction(globeViewAction);

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
            m_secondaryWindow->mapWidget()->loadBorderShapefile(filePath);
            statusBar()->showMessage(message, 4000);
        }
        else {
            statusBar()->showMessage(
                QString("Could not load %1: %2").arg(QFileInfo(filePath).fileName(), message),
                6000);
        }
        });

    connect(texturesVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setTexturesVisible(visible);
        statusBar()->showMessage(visible ? "Map imagery shown" : "Map imagery hidden", 2000);
        });

    connect(bordersVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setBordersVisible(visible);
        statusBar()->showMessage(visible ? "Borders shown" : "Borders hidden", 2000);
        });

    connect(gridVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setGridVisible(visible);
        statusBar()->showMessage(visible ? "Grid shown" : "Grid hidden", 2000);
        });

    connect(citiesVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setCitiesVisible(visible);
        statusBar()->showMessage(visible ? "Cities shown" : "Cities hidden", 2000);
        });

    connect(terrainVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setTerrainVisible(visible);
        statusBar()->showMessage(visible ? "Terrain shown" : "Terrain hidden", 2000);
        });

    connect(secondaryWindowAction, &QAction::toggled, this, [this](bool visible) {
        m_secondaryWindow->setVisible(visible);
        statusBar()->showMessage(visible ? "Secondary map window shown" : "Secondary map window hidden", 2000);
        });

    connect(wrapLongitudeAction, &QAction::toggled, this, [this](bool enabled) {
        m_mapWidget->camera()->setHorizontalWrapEnabled(enabled);
        statusBar()->showMessage(
            enabled ? "Longitude wrapping enabled" : "Longitude constrained to +/-180 degrees",
            2000);
        });

    connect(globeViewAction, &QAction::toggled, this, [this](bool enabled) {
        m_mapWidget->camera()->setProjectionMode(
            enabled ? Camera::ProjectionMode::Orthographic : Camera::ProjectionMode::Mercator);
        statusBar()->showMessage(enabled ? "Orthographic globe view enabled" : "Mercator map view enabled", 2000);
        });

    auto syncGlobeAction = [this, globeViewAction]() {
        QSignalBlocker blocker(globeViewAction);
        globeViewAction->setChecked(m_mapWidget->camera()->isOrthographic());
    };
    connect(m_mapWidget->camera(), &Camera::projectionModeChanged, this, syncGlobeAction);
    connect(m_secondaryWindow, &MapViewWindow::windowVisibilityChanged, this, [secondaryWindowAction](bool visible) {
        QSignalBlocker blocker(secondaryWindowAction);
        secondaryWindowAction->setChecked(visible);
    });

    // Connect camera for status updates
    connect(m_mapWidget->camera(), &Camera::cameraChanged, [this]() {
        QPointF center = m_mapWidget->camera()->getCenterMercator();
        QPointF latLon = MercatorProjection::mercatorToLatLon(center.x(), center.y());
        m_coordLabel->setText(QString("Center: %1%3N, %2%3E")
            .arg(latLon.x(), 0, 'f', 2)
            .arg(latLon.y(), 0, 'f', 2)
            .arg(QChar(0x00B0)));
        m_zoomLabel->setText(QString("Zoom: %1")
            .arg(m_mapWidget->camera()->getZoomLevel(), 0, 'f', 1));
        });
    m_secondaryWindow->show();
}

void MainWindow::createStatusBar()
{
    statusBar()->show();
    m_coordLabel = new QLabel(QString("Center: 0.00%1N, 0.00%1E").arg(QChar(0x00B0)), this);
    m_zoomLabel = new QLabel("Zoom: 2.0", this);

    statusBar()->addWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
}
