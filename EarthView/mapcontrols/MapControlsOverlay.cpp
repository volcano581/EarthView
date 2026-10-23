#include "MapControlsOverlay.h"
#include "MapControlsModel.h"
#include "MapWidget.h"

#include <QEvent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QToolTip>
#include <QUrl>

namespace {
constexpr int kMargin = 12;
constexpr int kSideBarBottomMargin = 24;
// Clears MapWidget's scale bar, which is drawn at the bottom-left.
constexpr int kLayersBottomMargin = 92;
}

MapControlsOverlay::MapControlsOverlay(MapWidget* mapWidget)
    : QObject(mapWidget)
    , m_mapWidget(mapWidget)
    , m_model(new MapControlsModel(this))
    , m_sideBarView(nullptr)
    , m_layersView(nullptr)
{
    m_model->bindCamera(mapWidget->camera());
    m_model->setHillshadeVisible(mapWidget->isTerrainVisible());
    m_model->setCitiesVisible(mapWidget->areCitiesVisible());
    m_model->setBordersVisible(mapWidget->areBordersVisible());
    m_model->setGridVisible(mapWidget->isGridVisible());

    connect(m_model, &MapControlsModel::hillshadeVisibleChanged, mapWidget, &MapWidget::setTerrainVisible);
    connect(m_model, &MapControlsModel::citiesVisibleChanged, mapWidget, &MapWidget::setCitiesVisible);
    connect(m_model, &MapControlsModel::bordersVisibleChanged, mapWidget, &MapWidget::setBordersVisible);
    connect(m_model, &MapControlsModel::gridVisibleChanged, mapWidget, &MapWidget::setGridVisible);

    connect(m_model, &MapControlsModel::toolTipRequested, this,
        [this](const QString& text, const QPointF& globalPos) {
            QToolTip::showText(globalPos.toPoint(), text, m_mapWidget);
        });
    connect(m_model, &MapControlsModel::toolTipHideRequested, this, []() {
        QToolTip::hideText();
    });

    // The map is a QOpenGLWidget, so its window composites with OpenGL; Qt Quick defaults to
    // Direct3D 11 on Windows and then renders nothing ("no QRhi"). Process-wide, and must run
    // before the first QQuickWidget exists.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    m_sideBarView = createView(QStringLiteral("MapSideBar.qml"));
    m_layersView = createView(QStringLiteral("LayersButton.qml"));

    mapWidget->installEventFilter(this);
    updateGeometry();
}

MapControlsOverlay::~MapControlsOverlay() = default;

QQuickWidget* MapControlsOverlay::createView(const QString& qmlFile)
{
    // Transparent, composited over the GL map; keyboard focus stays on the map.
    auto* view = new QQuickWidget(m_mapWidget);
    view->setAttribute(Qt::WA_AlwaysStackOnTop);
    view->setAttribute(Qt::WA_TranslucentBackground);
    view->setClearColor(Qt::transparent);
    view->setFocusPolicy(Qt::NoFocus);
    view->setResizeMode(QQuickWidget::SizeViewToRootObject);
    view->rootContext()->setContextProperty(QStringLiteral("mapControls"), m_model);
    view->setSource(QUrl(QStringLiteral("qrc:/earthview/mapcontrols/") + qmlFile));
    if (view->status() == QQuickWidget::Error) {
        for (const QQmlError& error : view->errors())
            qWarning("MapControlsOverlay: %s", qPrintable(error.toString()));
    }
    view->installEventFilter(this);
    view->show();
    return view;
}

bool MapControlsOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_mapWidget) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
            updateGeometry();
            break;
        case QEvent::MouseButtonPress:
            // Click on the map closes an open panel; the map still gets the press.
            m_model->closePanels();
            break;
        default:
            break;
        }
    }
    else if ((watched == m_sideBarView || watched == m_layersView) && event->type() == QEvent::Resize) {
        updateGeometry();
    }
    return QObject::eventFilter(watched, event);
}

void MapControlsOverlay::updateGeometry()
{
    if (!m_mapWidget)
        return;
    if (m_sideBarView) {
        m_sideBarView->move(
            qMax(0, m_mapWidget->width() - m_sideBarView->width() - kMargin),
            qMax(0, m_mapWidget->height() - m_sideBarView->height() - kSideBarBottomMargin));
        m_sideBarView->raise();
    }
    if (m_layersView) {
        m_layersView->move(
            kMargin,
            qMax(0, m_mapWidget->height() - m_layersView->height() - kLayersBottomMargin));
        m_layersView->raise();
    }
}
