#include "MapControlsOverlay.h"
#include "Camera.h"
#include "GeoMeasure.h"
#include "MapControlsModel.h"
#include "MapWidget.h"
#include "MeasureModel.h"
#include "MercatorProjection.h"
#include "SmoothNavigator.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QWheelEvent>

namespace {
constexpr int kMargin = 12;
// Clears MapWidget's scale bar, which the overlay puts at the bottom-right.
constexpr int kLayersBottomMargin = 84;
// A press and release closer than this (px) is a click, not a drag.
constexpr int kClickSlop = 4;
// Legs are drawn along the great circle, this many pieces each.
constexpr int kLegSegments = 48;
const QColor kLineColor(26, 115, 232);
}

MapControlsOverlay::MapControlsOverlay(MapWidget* mapWidget)
    : QObject(mapWidget)
    , m_mapWidget(mapWidget)
    , m_model(new MapControlsModel(this))
    , m_measure(new MeasureModel(this))
    , m_navigator(new SmoothNavigator(mapWidget->camera(), this))
    , m_sideBarView(nullptr)
    , m_layersView(nullptr)
    , m_pressTimeMs(0)
    , m_leftDown(false)
    , m_hoverValid(false)
{
    m_clock.start();
    m_model->bindCamera(mapWidget->camera());
    // Web-map behaviour: never zoom out past a map that fills the window's height.
    mapWidget->camera()->setFitHeightMinimumZoom(true);
    m_model->setMeasureAvailable(true);
    m_model->setHillshadeVisible(mapWidget->isTerrainVisible());
    m_model->setCitiesVisible(mapWidget->areCitiesVisible());
    m_model->setBordersVisible(mapWidget->areBordersVisible());
    m_model->setGridVisible(mapWidget->isGridVisible());

    connect(m_model, &MapControlsModel::hillshadeVisibleChanged, mapWidget, &MapWidget::setTerrainVisible);
    connect(m_model, &MapControlsModel::citiesVisibleChanged, mapWidget, &MapWidget::setCitiesVisible);
    connect(m_model, &MapControlsModel::bordersVisibleChanged, mapWidget, &MapWidget::setBordersVisible);
    connect(m_model, &MapControlsModel::gridVisibleChanged, mapWidget, &MapWidget::setGridVisible);

    connect(m_model, &MapControlsModel::measureOpenChanged, this, [this](bool open) {
        if (!open)
            m_measure->clear();
        m_hoverValid = false;
        updateCursor();
    });

    connect(m_model, &MapControlsModel::toolTipRequested, this,
        [this](const QString& text, const QPointF& globalPos) {
            QToolTip::showText(globalPos.toPoint(), text, m_mapWidget);
        });
    connect(m_model, &MapControlsModel::toolTipHideRequested, this, []() {
        QToolTip::hideText();
    });

    mapWidget->setScaleBarCorner(Qt::BottomRightCorner);
    mapWidget->setOverlayPainter([this](QPainter& painter) { paintMeasure(painter); });

    // The map is a QOpenGLWidget, so its window composites with OpenGL; Qt Quick defaults to
    // Direct3D 11 on Windows and then renders nothing ("no QRhi"). Process-wide, and must run
    // before the first QQuickWidget exists.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    m_sideBarView = createView(QStringLiteral("MapSideBar.qml"));
    m_layersView = createView(QStringLiteral("LayersButton.qml"));

    mapWidget->installEventFilter(this);
    updateGeometry();
}

MapControlsOverlay::~MapControlsOverlay()
{
    if (m_mapWidget)
        m_mapWidget->setOverlayPainter({});
}

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
    view->rootContext()->setContextProperty(QStringLiteral("measure"), m_measure);
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
        if (handleMapEvent(event))
            return true;
    }
    else if ((watched == m_sideBarView || watched == m_layersView) && event->type() == QEvent::Resize) {
        updateGeometry();
    }
    return QObject::eventFilter(watched, event);
}

bool MapControlsOverlay::handleMapEvent(QEvent* event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Show:
        updateGeometry();
        return false;

    case QEvent::Wheel: {
        // Replaces the map's stepped wheel zoom with an eased one.
        auto* wheel = static_cast<QWheelEvent*>(event);
        m_mapWidget->setFocus(Qt::MouseFocusReason);
        m_navigator->wheel(wheel->angleDelta().y(), wheel->position());
        wheel->accept();
        return true;
    }

    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        m_model->closePanels();
        if (mouse->button() == Qt::RightButton && m_model->isMeasureOpen()) {
            m_measure->removeLastPoint();
            return true;
        }
        if (mouse->button() == Qt::LeftButton) {
            m_leftDown = true;
            m_pressPos = mouse->position().toPoint();
            m_pressTimeMs = m_clock.elapsed();
            m_navigator->beginDrag(m_pressTimeMs, mouse->position());
        }
        return false;  // The map still pans.
    }

    case QEvent::MouseMove: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (m_leftDown)
            m_navigator->dragTo(m_clock.elapsed(), mouse->position());
        if (m_model->isMeasureOpen()) {
            m_hoverPos = mouse->position();
            m_hoverValid = true;
        }
        return false;
    }

    case QEvent::MouseButtonRelease: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton || !m_leftDown)
            return false;
        m_leftDown = false;
        const bool click = (mouse->position().toPoint() - m_pressPos).manhattanLength() <= kClickSlop;
        if (click && m_model->isMeasureOpen())
            addMeasurePointAt(mouse->position());
        else if (!click)
            m_navigator->endDrag(m_clock.elapsed());
        // MapWidget resets the cursor to an arrow on release; put the crosshair back.
        QTimer::singleShot(0, this, &MapControlsOverlay::updateCursor);
        return false;
    }

    case QEvent::Leave:
        m_hoverValid = false;
        return false;

    default:
        return false;
    }
}

bool MapControlsOverlay::addMeasurePointAt(const QPointF& screenPos)
{
    const Camera* camera = m_mapWidget->camera();
    const QPointF mercator = camera->screenToMercator(screenPos);
    // Off the globe (orthographic) screenToMercator falls back to the centre: reject clicks
    // that do not project back to where they were made.
    QPointF check;
    if (!camera->projectMercatorToScreen(mercator, &check)
        || (check - screenPos).manhattanLength() > 2.0)
        return false;
    m_measure->addPoint(MercatorProjection::mercatorToLatLon(mercator.x(), mercator.y()));
    return true;
}

void MapControlsOverlay::updateCursor()
{
    if (!m_mapWidget)
        return;
    if (m_model->isMeasureOpen())
        m_mapWidget->setCursor(Qt::CrossCursor);
    else
        m_mapWidget->unsetCursor();
}

void MapControlsOverlay::paintMeasure(QPainter& painter)
{
    if (!m_model->isMeasureOpen() || !m_mapWidget)
        return;
    const QVector<QPointF>& points = m_measure->points();
    const Camera* camera = m_mapWidget->camera();
    const double jumpLimit = m_mapWidget->width() * 0.5;

    auto project = [camera](const QPointF& latLon, QPointF* screen) {
        return camera->projectMercatorToScreen(
            MercatorProjection::latLonToMercator(latLon.x(), latLon.y()), screen);
    };

    // Great-circle legs, split where a piece leaves the globe or wraps across the map.
    QPainterPath path;
    for (int leg = 1; leg < points.size(); ++leg) {
        bool penDown = false;
        QPointF previous;
        for (const QPointF& latLon : GeoMeasure::greatCirclePoints(points.at(leg - 1), points.at(leg), kLegSegments)) {
            QPointF screen;
            if (!project(latLon, &screen)) {
                penDown = false;
                continue;
            }
            if (penDown && std::abs(screen.x() - previous.x()) < jumpLimit)
                path.lineTo(screen);
            else
                path.moveTo(screen);
            penDown = true;
            previous = screen;
        }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 220), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setPen(QPen(kLineColor, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);

    // Rubber band from the last point to the cursor.
    QPointF lastScreen;
    const bool lastVisible = !points.isEmpty() && project(points.last(), &lastScreen);
    const bool lineComplete = m_measure->mode() == MeasureModel::LineMode && points.size() >= 2;
    if (lastVisible && m_hoverValid && !lineComplete) {
        painter.setPen(QPen(kLineColor, 2, Qt::DashLine, Qt::RoundCap));
        painter.drawLine(lastScreen, m_hoverPos);
    }

    painter.setPen(QPen(kLineColor, 2));
    painter.setBrush(Qt::white);
    for (const QPointF& latLon : points) {
        QPointF screen;
        if (project(latLon, &screen))
            painter.drawEllipse(screen, 5.0, 5.0);
    }

    // Total at the last point.
    const QString total = m_measure->totalText();
    if (lastVisible && !total.isEmpty()) {
        QFont font = painter.font();
        font.setPixelSize(13);
        font.setBold(true);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        QRectF box = metrics.boundingRect(total).adjusted(-8, -5, 8, 5);
        box.moveTopLeft(lastScreen + QPointF(12, -box.height() - 8));
        painter.setPen(QPen(QColor(218, 220, 224), 1));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(box, 6, 6);
        painter.setPen(QColor(32, 33, 36));
        painter.drawText(box, Qt::AlignCenter, total);
    }
}

void MapControlsOverlay::updateGeometry()
{
    if (!m_mapWidget)
        return;
    if (m_sideBarView) {
        m_sideBarView->move(
            qMax(0, m_mapWidget->width() - m_sideBarView->width() - kMargin),
            qMax(0, (m_mapWidget->height() - m_sideBarView->height()) / 2));
        m_sideBarView->raise();
    }
    if (m_layersView) {
        m_layersView->move(
            qMax(0, m_mapWidget->width() - m_layersView->width() - kMargin),
            qMax(0, m_mapWidget->height() - m_layersView->height() - kLayersBottomMargin));
        m_layersView->raise();
    }
}
