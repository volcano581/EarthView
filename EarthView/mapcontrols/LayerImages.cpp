#include "LayerImages.h"

#include "Camera.h"
#include "Constants.h"
#include "MapTheme.h"
#include "MbTilesReader.h"
#include "MercatorProjection.h"
#include "RemoteMbTiles.h"

#include <QFont>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kThumbnailSize = 144;

QColor hex(const char* value)
{
    return QColor::fromString(QLatin1String(value));
}

// Illustrations are drawn on a 64 x 64 grid, scaled to the image.
QPainter* beginCanvas(QImage* image, int size, const QColor& background)
{
    *image = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
    image->fill(background);
    auto* painter = new QPainter(image);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->scale(size / 64.0, size / 64.0);
    return painter;
}

void stroke(QPainter& p, const QPainterPath& path, const QColor& color, qreal width, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, width, style, Qt::RoundCap, Qt::RoundJoin);
    p.strokePath(path, pen);
}

void road(QPainter& p, const QPainterPath& path, const QColor& fill, const QColor& casing, qreal width)
{
    if (casing.alpha() > 0)
        stroke(p, path, casing, width + 1.6);
    stroke(p, path, fill, width);
}

void haloText(QPainter& p, const QPointF& at, const QString& text, const QFont& font, const QColor& color,
              const QColor& halo = QColor(255, 255, 255, 235))
{
    QPainterPath path;
    path.addText(at, font, text);
    p.strokePath(path, QPen(halo, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.fillPath(path, color);
}

QFont iconFont(qreal size, bool bold = false, bool italic = false)
{
    QFont font;
    font.setPixelSize(qMax(1, qRound(size)));
    font.setBold(bold);
    font.setItalic(italic);
    return font;
}

/// The light street map every illustration starts from (OSM standard colours).
void paintStreets(QPainter& p)
{
    p.setPen(Qt::NoPen);
    p.setBrush(hex("#e0dfdf"));
    p.drawRect(QRectF(4, 6, 22, 18));
    p.drawRect(QRectF(36, 40, 24, 20));
    QPainterPath streets;
    streets.moveTo(-2, 30);
    streets.lineTo(66, 26);
    streets.moveTo(30, -2);
    streets.lineTo(34, 66);
    streets.moveTo(-2, 52);
    streets.lineTo(30, 50);
    road(p, streets, Qt::white, hex("#cfcfcf"), 2.6);
}

QPainterPath curve(std::initializer_list<QPointF> points)
{
    QPainterPath path;
    const QVector<QPointF> v(points);
    path.moveTo(v.first());
    for (int i = 1; i + 1 < v.size(); i += 2)
        path.quadTo(v.at(i), v.at(i + 1));
    if (v.size() % 2 == 0)
        path.lineTo(v.last());
    return path;
}

} // namespace

QImage LayerImages::layerIcon(const QString& id, int size)
{
    QImage image;
    QPainter* painter = beginCanvas(&image, size, hex("#f2efe9"));
    QPainter& p = *painter;
    const QColor water = hex("#aad3df");

    if (id == QLatin1String("water") || id == QLatin1String("water_name") || id == QLatin1String("waterway_label")) {
        paintStreets(p);
        p.setPen(Qt::NoPen);
        p.setBrush(water);
        p.drawPath(curve({ { 64, 10 }, { 30, 14 }, { 26, 38 }, { 24, 60 }, { 64, 64 } }));
        if (id != QLatin1String("water"))
            haloText(p, QPointF(30, 46), QStringLiteral("Lake"), iconFont(11, false, true), hex("#4d80b3"));
    }
    else if (id == QLatin1String("waterway")) {
        paintStreets(p);
        stroke(p, curve({ { -4, 12 }, { 22, 18 }, { 30, 34 }, { 40, 52 }, { 68, 56 } }), water, 7);
        stroke(p, curve({ { 30, 34 }, { 48, 30 }, { 68, 16 } }), water, 3.5);
    }
    else if (id == QLatin1String("transportation") || id == QLatin1String("road")) {
        p.setPen(Qt::NoPen);
        p.setBrush(hex("#e0dfdf"));
        p.drawRect(QRectF(4, 4, 18, 16));
        road(p, curve({ { -4, 56 }, { 26, 40 }, { 68, 6 } }), hex("#e892a2"), hex("#dc2a67"), 6);
        road(p, curve({ { -4, 22 }, { 36, 28 }, { 68, 40 } }), hex("#fcd6a4"), hex("#a06b00"), 4.2);
        QPainterPath minor;
        minor.moveTo(40, -2);
        minor.lineTo(46, 66);
        minor.moveTo(12, -2);
        minor.lineTo(8, 66);
        road(p, minor, Qt::white, hex("#bbbbbb"), 2.4);
    }
    else if (id == QLatin1String("transportation_name") || id == QLatin1String("road_label")) {
        paintStreets(p);
        QPainterPath main;
        main.moveTo(-2, 46);
        main.lineTo(66, 34);
        road(p, main, hex("#f7fabf"), hex("#707d05"), 6.5);
        p.save();
        p.translate(32, 41);
        p.rotate(-10);
        haloText(p, QPointF(-19, 3), QStringLiteral("Main St"), iconFont(8.5, true), hex("#222222"));
        p.restore();
    }
    else if (id == QLatin1String("building") || id == QLatin1String("housenumber")) {
        paintStreets(p);
        p.setPen(QPen(hex("#c4b6ab"), 0.8));
        p.setBrush(hex("#d9d0c9"));
        const QRectF blocks[] = { { 6, 8, 10, 8 }, { 18, 8, 8, 12 }, { 38, 6, 12, 9 }, { 52, 8, 8, 12 },
                                  { 6, 36, 8, 10 }, { 16, 36, 10, 8 }, { 38, 34, 9, 12 }, { 50, 36, 10, 9 } };
        for (const QRectF& block : blocks)
            p.drawRect(block);
        if (id == QLatin1String("housenumber")) {
            const QFont font = iconFont(7.5, true);
            haloText(p, QPointF(7, 14.5), QStringLiteral("12"), font, hex("#666666"));
            haloText(p, QPointF(39, 13), QStringLiteral("14"), font, hex("#666666"));
            haloText(p, QPointF(39, 43), QStringLiteral("7"), font, hex("#666666"));
        }
    }
    else if (id == QLatin1String("boundary") || id == QLatin1String("admin")) {
        p.setPen(Qt::NoPen);
        p.setBrush(hex("#eef0d5"));
        p.drawPath(curve({ { 0, 0 }, { 30, 6 }, { 34, 30 }, { 40, 64 }, { 0, 64 } }));
        stroke(p, curve({ { 30, -2 }, { 28, 22 }, { 36, 34 }, { 44, 48 }, { 40, 66 } }), hex("#8d618b"), 3.2, Qt::DashLine);
        stroke(p, curve({ { 36, 34 }, { 52, 30 }, { 66, 18 } }), QColor(141, 97, 139, 150), 1.8, Qt::DashLine);
    }
    else if (id == QLatin1String("park") || id == QLatin1String("landuse_overlay")) {
        paintStreets(p);
        p.setPen(QPen(hex("#6fc18e"), 0.8));
        p.setBrush(hex("#c8facc"));
        p.drawRoundedRect(QRectF(8, 12, 40, 30), 4, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(hex("#7bbf7a"));
        for (const QPointF& tree : { QPointF(16, 20), QPointF(26, 30), QPointF(38, 20), QPointF(20, 34), QPointF(40, 34) })
            p.drawEllipse(tree, 2.6, 2.6);
    }
    else if (id == QLatin1String("landcover")) {
        p.setPen(Qt::NoPen);
        p.setBrush(hex("#add19e"));
        p.drawPath(curve({ { 0, 0 }, { 40, 0 }, { 36, 26 }, { 20, 40 }, { 0, 34 } }));
        p.setBrush(hex("#cdebb0"));
        p.drawPath(curve({ { 64, 0 }, { 40, 10 }, { 44, 34 }, { 48, 50 }, { 64, 46 } }));
        p.setBrush(hex("#f5e9c6"));
        p.drawPath(curve({ { 0, 64 }, { 8, 44 }, { 30, 50 }, { 52, 60 }, { 64, 64 } }));
    }
    else if (id == QLatin1String("landuse")) {
        p.setPen(Qt::NoPen);
        p.setBrush(hex("#e0dfdf"));
        p.drawRect(QRectF(0, 0, 30, 30));
        p.setBrush(hex("#f2dad9"));
        p.drawRect(QRectF(34, 0, 30, 26));
        p.setBrush(hex("#ebdbe8"));
        p.drawRect(QRectF(0, 34, 40, 30));
        p.setBrush(hex("#ffffe5"));
        p.drawRect(QRectF(44, 30, 20, 34));
        QPainterPath streets;
        streets.moveTo(32, -2);
        streets.lineTo(32, 66);
        streets.moveTo(-2, 32);
        streets.lineTo(66, 28);
        road(p, streets, Qt::white, hex("#cfcfcf"), 2.6);
    }
    else if (id == QLatin1String("aeroway") || id == QLatin1String("aerodrome_label") || id == QLatin1String("airport_label")) {
        p.setPen(Qt::NoPen);
        p.setBrush(hex("#e9e7e2"));
        p.drawRect(QRectF(0, 0, 64, 64));
        p.save();
        p.translate(32, 32);
        p.rotate(-30);
        p.setBrush(hex("#bbbbcc"));
        p.drawRect(QRectF(-40, -6, 80, 12));
        stroke(p, [] { QPainterPath centre; centre.moveTo(-36, 0); centre.lineTo(36, 0); return centre; }(),
               Qt::white, 1.2, Qt::DashLine);
        p.restore();
        if (id != QLatin1String("aeroway")) {
            p.setPen(QPen(Qt::white, 1));
            p.setBrush(hex("#6a5a8e"));
            p.drawRoundedRect(QRectF(8, 44, 9, 9), 2, 2);
            haloText(p, QPointF(20, 52), QStringLiteral("Airport"), iconFont(8.5, true), hex("#6a5a8e"));
        }
    }
    else if (id == QLatin1String("mountain_peak") || id == QLatin1String("natural_label")) {
        p.setPen(Qt::NoPen);
        p.setBrush(hex("#eee5dc"));
        p.drawPath(curve({ { 0, 64 }, { 18, 20 }, { 30, 18 }, { 44, 30 }, { 64, 64 } }));
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(hex("#d08f55"));
        QPolygonF peak;
        peak << QPointF(24, 18) << QPointF(30, 28) << QPointF(18, 28);
        p.drawPolygon(peak);
        haloText(p, QPointF(18, 42), QStringLiteral("Peak"), iconFont(9, true), hex("#5f4b3a"));
    }
    else if (id == QLatin1String("poi") || id == QLatin1String("poi_label")) {
        paintStreets(p);
        const struct { QPointF at; const char* color; } pins[] = {
            { { 16, 16 }, "#c77400" }, { { 46, 14 }, "#ac39ac" }, { { 18, 44 }, "#0e8a3a" }, { { 48, 46 }, "#2f6aa6" },
        };
        for (const auto& pin : pins) {
            p.setPen(QPen(Qt::white, 1.4));
            p.setBrush(hex(pin.color));
            p.drawEllipse(pin.at, 5.5, 5.5);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(pin.at, 1.8, 1.8);
        }
    }
    else if (id == QLatin1String("place") || id == QLatin1String("place_label")) {
        paintStreets(p);
        haloText(p, QPointF(8, 24), QStringLiteral("City"), iconFont(14, true), hex("#000000"));
        haloText(p, QPointF(30, 46), QStringLiteral("Town"), iconFont(10.5, true), hex("#333333"));
        haloText(p, QPointF(6, 58), QStringLiteral("village"), iconFont(8.5), hex("#333333"));
    }
    else {
        paintStreets(p);
        stroke(p, curve({ { -2, 40 }, { 30, 20 }, { 66, 30 } }), hex("#969690"), 2.5);
        haloText(p, QPointF(22, 40), id.left(1).toUpper(), iconFont(18, true), hex("#5f6368"));
    }

    painter->end();
    delete painter;
    return image;
}

QImage LayerImages::themePreview(const QString& themeId, int size)
{
    const MapTheme& theme = MapTheme::theme(themeId);
    QImage image;
    QPainter* painter = beginCanvas(&image, size, theme.background());
    QPainter& p = *painter;
    const int zoom = 15;
    auto styleOf = [&](const char* layer, const char* klass, int type) {
        return theme.featureStyle(QString::fromLatin1(layer), { { QStringLiteral("class"), QString::fromLatin1(klass) } },
                                  type, zoom, false);
    };

    p.setPen(Qt::NoPen);
    p.setBrush(styleOf("landuse", "residential", 3).fill);
    p.drawRect(QRectF(0, 0, 64, 64));
    p.setBrush(styleOf("park", "park", 3).fill);
    p.drawRoundedRect(QRectF(36, 4, 24, 20), 3, 3);
    p.setBrush(theme.water());
    p.drawPath(curve({ { 0, 40 }, { 14, 44 }, { 18, 64 }, { 0, 64 } }));
    const MapTheme::FeatureStyle building = styleOf("building", "", 3);
    p.setPen(QPen(building.outline, 0.6));
    p.setBrush(building.fill);
    for (const QRectF& block : { QRectF(6, 6, 9, 7), QRectF(18, 6, 8, 9), QRectF(40, 36, 9, 8), QRectF(52, 38, 7, 9) })
        p.drawRect(block);

    auto drawRoad = [&](const QPainterPath& path, const char* klass, double scale) {
        const MapTheme::FeatureStyle style = styleOf("transportation", klass, 2);
        if (style.casing.isVisible())
            stroke(p, path, style.casing.color, style.casing.width * scale);
        stroke(p, path, style.line.color, style.line.width * scale);
    };
    QPainterPath minor;
    minor.moveTo(30, -2);
    minor.lineTo(34, 66);
    minor.moveTo(-2, 30);
    minor.lineTo(66, 32);
    drawRoad(minor, "minor", 0.45);
    drawRoad(curve({ { -4, 58 }, { 30, 46 }, { 68, 14 } }), "primary", 0.45);
    drawRoad(curve({ { -4, 18 }, { 24, 22 }, { 68, 52 } }), "motorway", 0.4);

    painter->end();
    delete painter;
    return image;
}

QImage LayerImages::renderVector(const QVector<MbTilesVectorTile>& tiles, int columns, int zoom, const QString& themeId,
                                 const QPointF& center, double span, int size)
{
    const MapTheme& theme = MapTheme::theme(themeId);
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(theme.background());
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const double scale = size / span;
    p.translate(size * 0.5, size * 0.5);
    p.scale(scale, scale);
    p.translate(-center);
    // A tile pixel is two screen pixels at the style zoom (512 px vector tiles).
    const double widthScale = 0.5 / scale;

    struct Fill { int order; QColor color; QPainterPath path; };
    struct Line { int order; MapTheme::Line style; QPainterPath path; };
    QVector<Fill> fills;
    QVector<Line> lines;
    for (int index = 0; index < tiles.size(); ++index) {
        const QPointF offset((index % columns) * GIS::TILE_SIZE, (index / columns) * GIS::TILE_SIZE);
        for (const MbTilesVectorLayer& layer : tiles.at(index).layers) {
            for (const MbTilesVectorFeature& feature : layer.features) {
                const MapTheme::FeatureStyle style = theme.featureStyle(layer.name, feature.tags, feature.type, zoom, false);
                QPainterPath path;
                path.setFillRule(Qt::WindingFill);
                for (const QVector<QPointF>& ring : feature.paths) {
                    if (ring.isEmpty())
                        continue;
                    path.moveTo(ring.first() + offset);
                    for (int i = 1; i < ring.size(); ++i)
                        path.lineTo(ring.at(i) + offset);
                }
                if (style.fill.alpha() > 0 && feature.type == 3)
                    fills.append({ style.fillOrder, style.fill, path });
                if (feature.type == 2) {
                    for (const MapTheme::Line* line : { &style.casing, &style.line, &style.overline }) {
                        if (line->isVisible())
                            lines.append({ line->order, *line, path });
                    }
                }
            }
        }
    }
    std::stable_sort(fills.begin(), fills.end(), [](const Fill& a, const Fill& b) { return a.order < b.order; });
    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.order < b.order; });
    p.setPen(Qt::NoPen);
    for (const Fill& fill : std::as_const(fills)) {
        p.setBrush(fill.color);
        p.drawPath(fill.path);
    }
    p.setBrush(Qt::NoBrush);
    for (const Line& line : std::as_const(lines)) {
        const double width = qMax(0.35 / scale, line.style.width * widthScale);
        QPen pen(line.style.color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (line.style.dash > 0.0f)
            pen.setDashPattern({ qMax(0.5, line.style.dash / (line.style.width + 0.01)), qMax(0.5, line.style.gap / (line.style.width + 0.01)) });
        p.setPen(pen);
        p.drawPath(line.path);
    }
    return image;
}

MapImageProvider::MapImageProvider(std::shared_ptr<MapImageStore> store)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_store(std::move(store))
{
}

QImage MapImageProvider::requestImage(const QString& requestId, QSize* size, const QSize& requestedSize)
{
    const QString id = requestId.section(QLatin1Char('?'), 0, 0);
    const int edge = requestedSize.isValid() && requestedSize.width() > 0 ? requestedSize.width() : kThumbnailSize;
    QImage image = m_store ? m_store->image(id) : QImage();
    if (image.isNull()) {
        if (id.startsWith(QLatin1String("layer/"))) {
            image = LayerImages::layerIcon(id.mid(6), edge);
        }
        else if (id.startsWith(QLatin1String("theme/"))) {
            image = LayerImages::themePreview(id.mid(6), edge);
        }
        else if (id.startsWith(QLatin1String("icon/"))) {
            // A drawn tile picture (qml/icons/<name>.svg).
            QSvgRenderer svg(QStringLiteral(":/earthview/mapcontrols/icons/%1.svg").arg(id.mid(5)));
            image = QImage(edge, edge, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            svg.render(&painter, QRectF(0, 0, edge, edge));
        }
        else {
            image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);  // not there yet: QML shows the drawn picture under it
            if (size)
                *size = image.size();
            return image;
        }
    }
    // Rounded corners, as the tiles of Google Maps' layer chooser have.
    const int side = image.width();
    QImage rounded(side, side, QImage::Format_ARGB32_Premultiplied);
    rounded.fill(Qt::transparent);
    {
        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, side, side), side * 0.16, side * 0.16);
        painter.setClipPath(clip);
        painter.drawImage(QRectF(0, 0, side, side), image);
    }
    if (size)
        *size = rounded.size();
    return rounded;
}

MapThumbnailer::MapThumbnailer(Camera* camera, std::shared_ptr<MapImageStore> store, QObject* parent)
    : QObject(parent)
    , m_camera(camera)
    , m_store(std::move(store))
    , m_theme(QStringLiteral("standard"))
    , m_lastZoom(-100.0)
    , m_hasRefreshed(false)
    , m_generation(0)
{
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(900);
    connect(&m_idleTimer, &QTimer::timeout, this, [this]() { refresh(false); });
    if (m_camera)
        connect(m_camera, &Camera::cameraChanged, this, &MapThumbnailer::scheduleIdleRefresh);
}

void MapThumbnailer::setSources(const QList<MapSourceCatalog::Source>& sources)
{
    m_sources = sources;
    refresh(true);
}

void MapThumbnailer::setTheme(const QString& themeId)
{
    if (m_theme == themeId)
        return;
    m_theme = themeId;
    refresh(true);  // the OSM map's own thumbnail changes look
}

void MapThumbnailer::scheduleIdleRefresh()
{
    m_idleTimer.start();
}

void MapThumbnailer::storeThumbnail(const QString& id, const QImage& image, quint64 generation)
{
    if (generation != m_generation || image.isNull())
        return;
    m_store->setImage(id, image);
    emit thumbnailsChanged();
}

void MapThumbnailer::refresh(bool force)
{
    if (!m_camera)
        return;
    const QPointF center = m_camera->getCenterMercator();
    const double zoom = m_camera->getZoomLevel();
    const double moved = std::hypot(center.x() - m_lastCenter.x(), center.y() - m_lastCenter.y())
        * GIS::EARTH_RADIUS / qMax(1e-9, m_camera->getResolution());
    if (!force && m_hasRefreshed && moved < 120.0 && std::abs(zoom - m_lastZoom) < 0.5)
        return;
    m_hasRefreshed = true;
    m_lastCenter = center;
    m_lastZoom = zoom;
    const quint64 generation = ++m_generation;

    QList<MapSourceCatalog::Source> sources = m_sources;
    if (sources.isEmpty()) {
        // The host's fallback when it found no map source.
        MapSourceCatalog::Source online;
        TmsLoader::TileSourceLayer layer;
        layer.urlTemplate = QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
        online.layers = { layer };
        sources.append(online);
    }

    bool themesDone = false;
    for (int index = 0; index < sources.size(); ++index) {
        const MapSourceCatalog::Source& source = sources.at(index);
        // The layer that shows at this zoom: the finest whose tiles cover it.
        const TmsLoader::TileSourceLayer* chosen = nullptr;
        bool vector = false;
        for (const TmsLoader::TileSourceLayer& layer : source.layers) {
            const bool isVector = layer.sourceType == TmsLoader::TileSourceLayer::SourceType::VectorMbTiles;
            const int tileZoom = qRound(zoom) - (isVector ? 2 : 1);
            if (!chosen || (tileZoom >= layer.minTileZoom && layer.maxTileZoom >= chosen->maxTileZoom))
                chosen = &layer;
            vector = vector || isVector;
        }
        if (!chosen)
            continue;
        const TmsLoader::TileSourceLayer layer = *chosen;
        const bool isVector = layer.sourceType == TmsLoader::TileSourceLayer::SourceType::VectorMbTiles;
        const int tileZoom = qBound(qMax(0, layer.minTileZoom), qRound(zoom) - (isVector ? 2 : 1),
                                    qMin(GIS::MAX_TILE_ZOOM, qMax(0, layer.maxTileZoom)));
        const double n = std::pow(2.0, tileZoom);
        const double fx = (center.x() + M_PI) / (2.0 * M_PI) * n;
        const double fy = (M_PI - center.y()) / (2.0 * M_PI) * n;
        const int c0 = int(std::floor(fx - 0.5));
        const int r0 = qBound(0, int(std::floor(fy - 0.5)), qMax(0, int(n) - 2));
        const QPointF blockCenter((fx - c0) * GIS::TILE_SIZE, (fy - r0) * GIS::TILE_SIZE);
        const QString baseId = QStringLiteral("base/%1").arg(index);
        const QString theme = m_theme;
        const bool renderThemes = isVector && !themesDone;
        themesDone = themesDone || renderThemes;
        QPointer<MapThumbnailer> self(this);

        MbTilesReader::threadPool()->start([self, layer, isVector, tileZoom, c0, r0, blockCenter, baseId, theme,
                                            renderThemes, generation]() {
            QHash<QString, QImage> results;
            const int tileCount = 1 << tileZoom;
            if (isVector) {
                QVector<MbTilesVectorTile> tiles;
                for (int r = 0; r < 2; ++r) {
                    for (int c = 0; c < 2; ++c) {
                        const int x = MercatorProjection::wrapTileX(c0 + c, tileZoom);
                        const int y = qBound(0, r0 + r, tileCount - 1);
                        tiles.append(layer.mbTilesPath.isEmpty()
                            ? MbTilesReader::readVectorTileFromUrl(layer.urlTemplate, tileZoom, x, y, layer.tmsYOrigin)
                            : MbTilesReader::readVectorTile(layer.mbTilesPath, tileZoom, x, y, layer.tmsYOrigin));
                    }
                }
                const int styleZoom = tileZoom + 1;
                results.insert(baseId, LayerImages::renderVector(tiles, 2, styleZoom, theme, blockCenter, 200.0, kThumbnailSize));
                if (renderThemes) {
                    for (const QString& id : MapTheme::ids())
                        results.insert(QStringLiteral("theme/") + id,
                                       LayerImages::renderVector(tiles, 2, styleZoom, id, blockCenter, 200.0, kThumbnailSize));
                }
            }
            else {
                QImage block(2 * GIS::TILE_SIZE, 2 * GIS::TILE_SIZE, QImage::Format_ARGB32_Premultiplied);
                block.fill(QColor(0xdd, 0xdd, 0xdd));
                QPainter painter(&block);
                bool any = false;
                for (int r = 0; r < 2; ++r) {
                    for (int c = 0; c < 2; ++c) {
                        const int x = MercatorProjection::wrapTileX(c0 + c, tileZoom);
                        const int y = qBound(0, r0 + r, tileCount - 1);
                        QImage tile;
                        if (layer.sourceType == TmsLoader::TileSourceLayer::SourceType::MbTiles) {
                            tile = MbTilesReader::readTileImage(layer.mbTilesPath, tileZoom, x, y, layer.tmsYOrigin, layer.mbTilesFormat);
                        }
                        else {
                            QString url = layer.urlTemplate;
                            url.replace(QLatin1String("{z}"), QString::number(tileZoom))
                                .replace(QLatin1String("{x}"), QString::number(x))
                                .replace(QLatin1String("{y}"), QString::number(layer.tmsYOrigin ? tileCount - 1 - y : y));
                            tile.loadFromData(RemoteMbTiles::httpGet(url, 8000));
                        }
                        if (!tile.isNull()) {
                            painter.drawImage(QRect(c * GIS::TILE_SIZE, r * GIS::TILE_SIZE, GIS::TILE_SIZE, GIS::TILE_SIZE), tile);
                            any = true;
                        }
                    }
                }
                painter.end();
                if (any) {
                    const QRectF crop(blockCenter.x() - 100.0, blockCenter.y() - 100.0, 200.0, 200.0);
                    results.insert(baseId, block.copy(crop.toAlignedRect()).scaled(kThumbnailSize, kThumbnailSize,
                                                                                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
                }
            }
            if (!self)
                return;
            QMetaObject::invokeMethod(self.data(), [self, results, generation]() {
                if (!self)
                    return;
                for (auto it = results.cbegin(); it != results.cend(); ++it)
                    self->storeThumbnail(it.key(), it.value(), generation);
            }, Qt::QueuedConnection);
        }, 0);
    }
}
