#include "MapControlsOverlay.h"
#include "Camera.h"
#include "FeatureDetails.h"
#include "GeoMeasure.h"
#include "LayerImages.h"
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
#include <QQmlEngine>
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
// The feature card sits under MapWidget's FPS box at the top-left.
constexpr int kFeatureCardTop = 48;
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
    , m_featureCardView(nullptr)
    , m_imageStore(std::make_shared<MapImageStore>())
    , m_thumbnailer(new MapThumbnailer(mapWidget->camera(), m_imageStore, this))
    , m_pressTimeMs(0)
    , m_leftDown(false)
    , m_panelsOpenAtPress(false)
    , m_hoverValid(false)
    , m_selectionGeometryType(0)
{
    m_clock.start();
    m_navigator->setFrameSource(mapWidget);
    m_model->bindCamera(mapWidget->camera());
    // Web-map behaviour: never zoom out past a map that fills the window's height.
    mapWidget->camera()->setFitHeightMinimumZoom(true);
    // Place names and boundaries come from the OSM map's details: the world cities (Data/Cities)
    // and the border shapefile are not offered here and not loaded. The host's View menu can
    // still switch them on.
    mapWidget->setCitiesVisible(false);
    mapWidget->setBordersVisible(false);
    m_model->setMeasureAvailable(true);
    m_model->setHillshadeVisible(mapWidget->isTerrainVisible());
    m_model->setCitiesVisible(mapWidget->areCitiesVisible());
    m_model->setBordersVisible(mapWidget->areBordersVisible());
    m_model->setGridVisible(mapWidget->isGridVisible());
    m_model->setFeatureLayerIds(mapWidget->featureLayerNames());
    m_model->setHiddenFeatureLayers(mapWidget->hiddenFeatureLayers());
    m_model->setCurrentTheme(mapWidget->mapTheme());

    connect(m_model, &MapControlsModel::hillshadeVisibleChanged, mapWidget, &MapWidget::setTerrainVisible);
    connect(m_model, &MapControlsModel::citiesVisibleChanged, mapWidget, &MapWidget::setCitiesVisible);
    connect(m_model, &MapControlsModel::bordersVisibleChanged, mapWidget, &MapWidget::setBordersVisible);
    connect(m_model, &MapControlsModel::gridVisibleChanged, mapWidget, &MapWidget::setGridVisible);
    connect(m_model, &MapControlsModel::hiddenFeatureLayersChanged, mapWidget, &MapWidget::setHiddenFeatureLayers);
    connect(m_model, &MapControlsModel::hiddenFeatureLayersChanged, this, [this](const QStringList& hidden) {
        // A detail switched off takes its selection with it.
        if (hidden.contains(m_model->selectedFeature().value(QStringLiteral("layer")).toString()))
            m_model->clearSelectedFeature();
    });
    connect(m_model, &MapControlsModel::currentThemeChanged, this, [this](const QString& id) {
        m_mapWidget->setMapTheme(id);
        m_thumbnailer->setTheme(id);
    });
    connect(mapWidget, &MapWidget::featureSourceChanged, this, [this]() {
        m_model->setFeatureLayerIds(m_mapWidget->featureLayerNames());
    });
    connect(mapWidget, &MapWidget::featureSourceChanged, this, [this]() {
        // Only a change of map source drops the selection (not layers met in new tiles).
        const bool hasSource = m_mapWidget->hasFeatureSource();
        if (!hasSource)
            m_model->clearSelectedFeature();
    });

    // Live thumbnails: reloaded together once a refresh has delivered (not once per picture).
    m_thumbnailRevisionTimer.setSingleShot(true);
    m_thumbnailRevisionTimer.setInterval(150);
    connect(&m_thumbnailRevisionTimer, &QTimer::timeout, m_model, &MapControlsModel::bumpThumbnailRevision);
    connect(m_thumbnailer, &MapThumbnailer::thumbnailsChanged, this, [this]() {
        if (!m_thumbnailRevisionTimer.isActive())
            m_thumbnailRevisionTimer.start();
    });
    connect(m_model, &MapControlsModel::layersOpenChanged, this, [this](bool open) {
        if (open)
            m_thumbnailer->refresh(false);
    });
    connect(m_model, &MapControlsModel::selectedFeatureChanged, this, [this]() {
        if (!m_model->isFeatureSelected()) {
            m_selectionPaths.clear();
            m_selectionGeometryType = 0;
        }
        if (m_featureCardView) {
            m_featureCardView->setVisible(m_model->isFeatureSelected());
            updateGeometry();
        }
        m_mapWidget->update();
    });

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
    mapWidget->setOverlayPainter([this](QPainter& painter) { paintOverlay(painter); });

    // The map is a QOpenGLWidget, so its window composites with OpenGL; Qt Quick defaults to
    // Direct3D 11 on Windows and then renders nothing ("no QRhi"). Process-wide, and must run
    // before the first QQuickWidget exists.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    m_sideBarView = createView(QStringLiteral("MapSideBar.qml"));
    m_layersView = createView(QStringLiteral("LayersButton.qml"));
    m_featureCardView = createView(QStringLiteral("FeatureCard.qml"));
    m_featureCardView->hide();  // Shown while a feature is selected.

    mapWidget->installEventFilter(this);
    updateGeometry();
}

MapControlsOverlay::~MapControlsOverlay()
{
    if (m_mapWidget)
        m_mapWidget->setOverlayPainter({});
    // The views are the map's children and would outlive the model their bindings read
    // (QML "Cannot read property ... of null" at exit): they go first.
    delete m_sideBarView;
    delete m_layersView;
    delete m_featureCardView;
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
    // image://maplayers/...: live thumbnails and drawn layer pictures (the engine owns it).
    view->engine()->addImageProvider(QStringLiteral("maplayers"), new MapImageProvider(m_imageStore));
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
    else if ((watched == m_sideBarView || watched == m_layersView || watched == m_featureCardView)
             && event->type() == QEvent::Resize) {
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
        // A click that only closes an open panel does not also select something.
        m_panelsOpenAtPress = m_model->isSettingsOpen() || m_model->isLayersOpen();
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
        else if (click && !m_panelsOpenAtPress)
            selectFeatureAt(mouse->position());
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

void MapControlsOverlay::setMapSources(const QList<MapSourceCatalog::Source>& sources)
{
    m_thumbnailer->setSources(sources);
}

bool MapControlsOverlay::selectFeatureAt(const QPointF& screenPos)
{
    if (!m_mapWidget)
        return false;
    const VectorTileRenderer::PickedFeature picked = m_mapWidget->pickFeature(screenPos);
    if (!picked.valid) {
        m_model->clearSelectedFeature();
        return false;
    }
    const QPointF latLon = MercatorProjection::mercatorToLatLon(picked.mercatorAnchor.x(), picked.mercatorAnchor.y());
    // Geometry first: the model's change handler repaints the map.
    m_selectionPaths = picked.mercatorPaths;
    m_selectionGeometryType = picked.geometryType;
    m_model->setSelectedFeature(FeatureDetails::describe(picked.layerName, picked.geometryType, picked.tags, latLon));
    m_mapWidget->update();
    return true;
}

void MapControlsOverlay::paintOverlay(QPainter& painter)
{
    paintSelection(painter);
    paintMeasure(painter);
}

void MapControlsOverlay::paintSelection(QPainter& painter)
{
    if (m_selectionPaths.isEmpty() || !m_mapWidget)
        return;
    const Camera* camera = m_mapWidget->camera();

    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const QVector<QPointF>& piece : std::as_const(m_selectionPaths)) {
        bool penDown = false;
        QPointF previous;
        for (const QPointF& mercator : piece) {
            QPointF screen;
            if (!camera->projectMercatorToScreen(mercator, &screen)) {
                penDown = false;
                continue;
            }
            if (m_selectionGeometryType == 1) {
                path.addEllipse(screen, 7.0, 7.0);
                continue;
            }
            // Long segments are normal when zoomed in; only a jump across the antimeridian
            // (more than half the world) breaks the line.
            if (penDown && std::abs(mercator.x() - previous.x()) < M_PI)
                path.lineTo(screen);
            else
                path.moveTo(screen);
            penDown = true;
            previous = mercator;
        }
        if (m_selectionGeometryType == 3 && penDown)
            path.closeSubpath();
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_selectionGeometryType == 3) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(26, 115, 232, 45));
        painter.drawPath(path);
    }
    painter.setBrush(m_selectionGeometryType == 1 ? QBrush(kLineColor) : QBrush(Qt::NoBrush));
    painter.setPen(QPen(QColor(255, 255, 255, 230), m_selectionGeometryType == 2 ? 7 : 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setPen(QPen(kLineColor, m_selectionGeometryType == 2 ? 4 : 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.restore();
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
        // Text as outlines, not glyph textures: the GL map leaves texture-upload state that can
        // garble QPainter's cached glyphs on a QOpenGLWidget.
        QPainterPath text;
        text.addText(0, 0, font, total);
        const QRectF textBounds = text.boundingRect();
        text.translate(box.center() - textBounds.center());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(32, 33, 36));
        painter.drawPath(text);
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
        // The panel scrolls rather than run off the top of a short map.
        m_model->setPanelMaxHeight(m_mapWidget->height() - kLayersBottomMargin - 2 * kMargin);
        m_layersView->move(
            qMax(0, m_mapWidget->width() - m_layersView->width() - kMargin),
            qMax(0, m_mapWidget->height() - m_layersView->height() - kLayersBottomMargin));
        m_layersView->raise();
    }
    if (m_featureCardView && m_featureCardView->isVisible()) {
        m_featureCardView->move(kMargin, kFeatureCardTop);
        m_featureCardView->raise();
    }
}
