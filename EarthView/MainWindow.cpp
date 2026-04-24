#include "MainWindow.h"
#include "MapWidget.h"
#include "Camera.h"
#include "MercatorProjection.h"
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
