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
constexpr int kRightMargin = 12;
constexpr int kBottomMargin = 24;
}

namespace {
// The map is a QOpenGLWidget, so its window composites with OpenGL; Qt Quick defaults to
// Direct3D 11 on Windows and then renders nothing ("no QRhi"). Process-wide, and must run
// before the first QQuickWidget exists.
QQuickWidget* createOpenGLQuickWidget(QWidget* parent)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    return new QQuickWidget(parent);
}
}

MapControlsOverlay::MapControlsOverlay(MapWidget* mapWidget)
    : QObject(mapWidget)
    , m_mapWidget(mapWidget)
    , m_model(new MapControlsModel(this))
    , m_view(createOpenGLQuickWidget(mapWidget))
{
    m_model->bindCamera(mapWidget->camera());

    // Transparent, composited over the GL map; keyboard focus stays on the map.
    m_view->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_view->setAttribute(Qt::WA_TranslucentBackground);
    m_view->setClearColor(Qt::transparent);
    m_view->setFocusPolicy(Qt::NoFocus);
    m_view->setResizeMode(QQuickWidget::SizeViewToRootObject);
    m_view->rootContext()->setContextProperty(QStringLiteral("mapControls"), m_model);
    m_view->setSource(QUrl(QStringLiteral("qrc:/earthview/mapcontrols/MapSideBar.qml")));
    if (m_view->status() == QQuickWidget::Error) {
        for (const QQmlError& error : m_view->errors())
            qWarning("MapControlsOverlay: %s", qPrintable(error.toString()));
    }

    connect(m_model, &MapControlsModel::toolTipRequested, this,
        [this](const QString& text, const QPointF& globalPos) {
            QToolTip::showText(globalPos.toPoint(), text, m_view);
        });
    connect(m_model, &MapControlsModel::toolTipHideRequested, this, []() {
        QToolTip::hideText();
    });

    mapWidget->installEventFilter(this);
    m_view->installEventFilter(this);
    updateGeometry();
    m_view->show();
}

MapControlsOverlay::~MapControlsOverlay() = default;

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
            m_model->setSettingsOpen(false);
            break;
        default:
            break;
        }
    }
    else if (watched == m_view && event->type() == QEvent::Resize) {
        updateGeometry();
    }
    return QObject::eventFilter(watched, event);
}

void MapControlsOverlay::updateGeometry()
{
    if (!m_mapWidget || !m_view)
        return;
    m_view->move(
        qMax(0, m_mapWidget->width() - m_view->width() - kRightMargin),
        qMax(0, m_mapWidget->height() - m_view->height() - kBottomMargin));
    m_view->raise();
}
