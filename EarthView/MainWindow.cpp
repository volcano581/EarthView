#include "MainWindow.h"
#include "MapWidget.h"
#include "MapViewWindow.h"
#include "Camera.h"
#include "MapSourceCatalog.h"

#ifdef EARTHVIEW_HAVE_3D
#include <earthview3d/GlobeView3D.h>
#include <earthview3d/MapConfig.h>
#endif
#include "MercatorProjection.h"
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
#include "BaseMapSources.h"
#include "MapControlsModel.h"
#include "MapControlsOverlay.h"
#include "SmoothNavigator.h"
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
    // With the on-map controls the buttons zoom one level, eased like the wheel.
    toolbar->addAction("Zoom In", [this]() {
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
        if (m_mapControlsOverlay) {
            m_mapControlsOverlay->navigator()->zoomBy(1.0);
            return;
        }
#endif
        m_mapWidget->camera()->zoom(0.5f);
        });
    toolbar->addAction("Zoom Out", [this]() {
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
        if (m_mapControlsOverlay) {
            m_mapControlsOverlay->navigator()->zoomBy(-1.0);
            return;
        }
#endif
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
    // The OSM (vector MBTiles) map's details can be drawn over every other base map.
    const QList<TmsLoader::TileSourceLayer> featureLayers = MapSourceCatalog::featureLayers(mapSources);
    m_mapWidget->setFeatureLayers(featureLayers);
    m_secondaryWindow->mapWidget()->setFeatureLayers(featureLayers);
#ifndef EARTHVIEW_HAS_MAP_CONTROLS
    // With the on-map controls the boundaries come from the OSM map: no border shapefile.
    m_mapWidget->loadWorldBorders();
    m_secondaryWindow->mapWidget()->loadWorldBorders();
#endif
    m_mapWidget->loadCities();
    m_secondaryWindow->mapWidget()->loadCities();

    setCentralWidget(m_mapWidget);
    m_mapWidget->setFocus(Qt::OtherFocusReason);
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
    m_mapControlsOverlay = new MapControlsOverlay(m_mapWidget);
    MapControlsModel* mapControls = m_mapControlsOverlay->model();
    // Live thumbnails of the map types come from the sources themselves.
    m_mapControlsOverlay->setMapSources(mapSources);
    if (m_secondaryWindow->mapControlsOverlay())
        m_secondaryWindow->mapControlsOverlay()->setMapSources(mapSources);
    // Base map choice is shared by both windows, as the Map Source dropdown is.
    QList<MapControlsModel*> baseMapModels{ mapControls };
    if (m_secondaryWindow->mapControls())
        baseMapModels.append(m_secondaryWindow->mapControls());
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

#ifdef EARTHVIEW_HAS_MAP_CONTROLS
    // Layers panel base map tiles <-> Map Source dropdown: same discovery, same order, so
    // tile i is dropdown item i (BaseMapSources picks each tile's label and icon).
    const QVariantList baseMapEntries = BaseMapSources::entries(mapSources);
    auto syncBaseMapModels = [baseMapModels](int comboIndex) {
        for (MapControlsModel* model : baseMapModels)
            model->setCurrentBaseMap(comboIndex);
    };
    for (MapControlsModel* model : baseMapModels)
        model->setBaseMaps(baseMapEntries);
    syncBaseMapModels(sourceCombo->currentIndex());

    for (MapControlsModel* model : baseMapModels) {
        connect(model, &MapControlsModel::currentBaseMapChanged, this,
            [sourceCombo, applySourceSelection, syncBaseMapModels](int index) {
                if (index < 0 || index >= sourceCombo->count() || index == sourceCombo->currentIndex())
                    return;
                sourceCombo->setCurrentIndex(index);
                applySourceSelection(sourceCombo->itemData(index).toInt());
                syncBaseMapModels(index);
            });
    }
    connect(sourceCombo, &QComboBox::activated, this,
        [sourceCombo, applySourceSelection, syncBaseMapModels](int index) {
            applySourceSelection(sourceCombo->itemData(index).toInt());
            syncBaseMapModels(index);
        });
#else
    connect(sourceCombo, &QComboBox::activated, this, [sourceCombo, applySourceSelection](int index) {
        applySourceSelection(sourceCombo->itemData(index).toInt());
        });
#endif

    QAction* loadBordersAction = new QAction("Load Borders...", this);
    toolbar->addSeparator();
#ifndef EARTHVIEW_HAS_MAP_CONTROLS  // Boundaries come from the OSM map there.
    toolbar->addAction(loadBordersAction);
#endif

    QAction* texturesVisibleAction = new QAction("Textures", this);
    texturesVisibleAction->setCheckable(true);
    texturesVisibleAction->setChecked(true);
    texturesVisibleAction->setToolTip("Show or hide map imagery.");
#ifndef EARTHVIEW_HAS_MAP_CONTROLS  // In the layers panel (and the View menu) instead.
    toolbar->addAction(texturesVisibleAction);
#endif

    QAction* bordersVisibleAction = new QAction("Borders", this);
    bordersVisibleAction->setCheckable(true);
    bordersVisibleAction->setChecked(true);
    bordersVisibleAction->setToolTip("Show or hide shapefile borders.");
#ifndef EARTHVIEW_HAS_MAP_CONTROLS
    toolbar->addAction(bordersVisibleAction);
#endif

    QAction* gridVisibleAction = new QAction("Grid", this);
    gridVisibleAction->setCheckable(true);
    gridVisibleAction->setChecked(true);
    gridVisibleAction->setToolTip("Show or hide latitude and longitude grid.");
#ifndef EARTHVIEW_HAS_MAP_CONTROLS
    toolbar->addAction(gridVisibleAction);
#endif

    QAction* citiesVisibleAction = new QAction("Cities", this);
    citiesVisibleAction->setCheckable(true);
#ifdef EARTHVIEW_HAS_MAP_CONTROLS
    // Off with the on-map controls: place names come from the OSM map's Places detail.
    citiesVisibleAction->setChecked(mapControls->areCitiesVisible());
#else
    citiesVisibleAction->setChecked(true);
#endif
    citiesVisibleAction->setToolTip("Show or hide city and settlement labels (world cities data).");
#ifndef EARTHVIEW_HAS_MAP_CONTROLS
    toolbar->addAction(citiesVisibleAction);
#endif

    QAction* terrainVisibleAction = new QAction("Terrain", this);
    terrainVisibleAction->setCheckable(true);
    terrainVisibleAction->setChecked(true);
    terrainVisibleAction->setToolTip("Show or hide DEM terrain overlay.");
#ifndef EARTHVIEW_HAS_MAP_CONTROLS
    toolbar->addAction(terrainVisibleAction);
#endif

    QAction* secondaryWindowAction = new QAction("Secondary Window", this);
    secondaryWindowAction->setCheckable(true);
    secondaryWindowAction->setChecked(false);  // Opened only when the user asks for it.
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

#ifndef EARTHVIEW_HAS_MAP_CONTROLS
    QMenu* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(loadBordersAction);
#endif

    QMenu* viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(texturesVisibleAction);
#ifndef EARTHVIEW_HAS_MAP_CONTROLS
    viewMenu->addAction(bordersVisibleAction);
#endif
    viewMenu->addAction(gridVisibleAction);
    viewMenu->addAction(citiesVisibleAction);
    viewMenu->addAction(terrainVisibleAction);
    viewMenu->addAction(secondaryWindowAction);

    // osgEarth 3D view (E9). Built only when earthview3d is: a 2D-only build has no action
    // rather than one that explains it cannot work.
#ifdef EARTHVIEW_HAVE_3D
    QAction* open3DAction = new QAction("Open 3D View", this);
    open3DAction->setShortcut(QKeySequence("Ctrl+3"));
    open3DAction->setToolTip("Open the osgEarth 3D view centred on the current 2D view.");
    connect(open3DAction, &QAction::triggered, this, &MainWindow::open3DView);
    toolbar->addSeparator();
    toolbar->addAction(open3DAction);
    viewMenu->addSeparator();
    viewMenu->addAction(open3DAction);
#endif
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

#ifdef EARTHVIEW_HAS_MAP_CONTROLS
    // Layers panel <-> View menu. The action's own handler applies the change to the map.
    auto bindLayerAction = [](QAction* action, MapControlsModel* model,
                              void (MapControlsModel::*setter)(bool),
                              void (MapControlsModel::*changed)(bool)) {
        QObject::connect(action, &QAction::toggled, model, setter);
        QObject::connect(model, changed, action, &QAction::setChecked);
    };
    bindLayerAction(terrainVisibleAction, mapControls,
        &MapControlsModel::setHillshadeVisible, &MapControlsModel::hillshadeVisibleChanged);
    bindLayerAction(citiesVisibleAction, mapControls,
        &MapControlsModel::setCitiesVisible, &MapControlsModel::citiesVisibleChanged);
    bindLayerAction(gridVisibleAction, mapControls,
        &MapControlsModel::setGridVisible, &MapControlsModel::gridVisibleChanged);
#endif

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
}

void MainWindow::createStatusBar()
{
    statusBar()->show();
    m_coordLabel = new QLabel(QString("Center: 0.00%1N, 0.00%1E").arg(QChar(0x00B0)), this);
    m_zoomLabel = new QLabel("Zoom: 2.0", this);

    statusBar()->addWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
}

#ifdef EARTHVIEW_HAVE_3D
namespace {
/// Data root the 3D view resolves its map and models against: the same folder the 2D viewer
/// finds its own data in.
QString dataRootForView3D()
{
    const QStringList candidates = {
        QDir::current().absoluteFilePath(QStringLiteral("Data")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("Data")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../Data"))
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir())
            return QFileInfo(candidate).absoluteFilePath();
    }
    return QDir::current().absoluteFilePath(QStringLiteral("Data"));
}
} // namespace

void MainWindow::open3DView()
{
    if (m_window3D) {
        m_window3D->raise();
        m_window3D->activateWindow();
        return;
    }

    earthview3d::MapConfig config;
    config.dataRoot = dataRootForView3D();
    config.earthFile = QStringLiteral("maps/earthview.earth");
    config.modelTable = QStringLiteral("models/models.json");
    config.cacheDir = QStringLiteral("cache");
    QString error;
    if (!config.validate(&error)) {
        statusBar()->showMessage(QStringLiteral("3D view: %1").arg(error), 8000);
        return;
    }

    // Open on what the 2D map is centred on, so the two views agree at the moment of opening.
    const QPointF latLon = MercatorProjection::mercatorToLatLon(
        m_mapWidget->camera()->getCenterMercator().x(),
        m_mapWidget->camera()->getCenterMercator().y());

    auto* window = new QMainWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QStringLiteral("EarthView — 3D view"));
    auto* view = new earthview3d::GlobeView3D(config, latLon.x(), latLon.y(), window);
    connect(view, &earthview3d::GlobeView3D::loadFailed, this, [this](const QString& reason) {
        statusBar()->showMessage(QStringLiteral("3D view: %1").arg(reason), 8000);
    });
    window->setCentralWidget(view);
    window->resize(1280, 800);
    window->show();
    m_window3D = window;
}
#endif // EARTHVIEW_HAVE_3D
