#include "MainWindow.h"
#include "MapWidget.h"
#include "Camera.h"
#include "MercatorProjection.h"
#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
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

    // Create map widget
    m_mapWidget = new MapWidget(this);
    m_mapWidget->setTileServerUrl("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    m_mapWidget->loadWorldBorders();

    setCentralWidget(m_mapWidget);

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

    QAction* wrapLongitudeAction = new QAction("Wrap Longitude", this);
    wrapLongitudeAction->setCheckable(true);
    wrapLongitudeAction->setToolTip("Repeat imagery horizontally instead of constraining longitude to +/-180 degrees.");
    toolbar->addSeparator();
    toolbar->addAction(wrapLongitudeAction);

    QAction* globeViewAction = new QAction("Globe View", this);
    globeViewAction->setCheckable(true);
    globeViewAction->setToolTip("Switch between flat Mercator map and orthographic globe view.");
    toolbar->addAction(globeViewAction);

    QMenu* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(loadBordersAction);

    QMenu* viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(texturesVisibleAction);
    viewMenu->addAction(bordersVisibleAction);
    viewMenu->addAction(gridVisibleAction);
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
