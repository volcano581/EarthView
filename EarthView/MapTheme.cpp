#include "MapTheme.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace {

struct Palette {
    QColor background, water, waterOutline;
    QColor farmland, ice, wood, rock, grass, wetland, sand, scrub;
    QColor residential, commercial, industrial, retail, cemetery, military, institution, sport, railwayArea, park;
    QColor building, buildingOutline;
    QColor motorway, trunk, primary, secondary, tertiary, minor, service, path, track, rail, railDash;
    QColor motorwayCasing, trunkCasing, primaryCasing, secondaryCasing, tertiaryCasing, minorCasing;
    float casingPx; // per side at z14+; 0 = no casings
    QColor waterway, runway, apron;
    QColor boundaryCountry, boundaryState, boundaryOther;
    QColor placeMajor, placeMinor, country, state, roadLabel, waterLabel, poiDefault, peak, airport;
    QColor halo, waterHalo;
    bool poiColors; // POI markers and names coloured by kind
};

QColor c(const char* hex, int alpha = 255)
{
    QColor color = QColor::fromString(QLatin1String(hex));
    color.setAlpha(alpha);
    return color;
}

// OSM Carto (openstreetmap.org "Standard").
const Palette& standardPalette()
{
    static const Palette p{
        c("#f2efe9"), c("#aad3df"), c("#aad3df"),
        c("#eef0d5"), c("#ddecec"), c("#add19e"), c("#eee5dc"), c("#cdebb0"), c("#dde8d3"), c("#f5e9c6"), c("#c8d7ab"),
        c("#e0dfdf"), c("#f2dad9"), c("#ebdbe8"), c("#ffd6d1"), c("#aacbaf"), c("#f2e1dc"), c("#ffffe5"), c("#dffce2"), c("#ebdbe8"), c("#c8facc", 160),
        c("#d9d0c9"), c("#c4b6ab"),
        c("#e892a2"), c("#f9b29c"), c("#fcd6a4"), c("#f7fabf"), c("#ffffff"), c("#ffffff"), c("#ffffff"), c("#fa8072"), c("#996600"), c("#707070"), c("#ffffff"),
        c("#dc2a67"), c("#c84e2f"), c("#a06b00"), c("#707d05"), c("#8f8f8f"), c("#bbbbbb"),
        1.0f,
        c("#aad3df"), c("#bbbbcc"), c("#dadae0"),
        c("#8d618b", 200), c("#8d618b", 140), c("#8d618b", 90),
        c("#000000"), c("#333333"), c("#8d618b"), c("#8d618b", 200), c("#222222"), c("#4d80b3"), c("#734a08"), c("#5f4b3a"), c("#6a5a8e"),
        c("#ffffff", 230), c("#ffffff", 200),
        true,
    };
    return p;
}

// CARTO Positron.
const Palette& positronPalette()
{
    static const Palette p{
        c("#fafaf8"), c("#d4dadc"), c("#c4cdd0"),
        c("#f6f6f2"), c("#f5f8f9"), c("#e8ede3"), c("#f1f0ee"), c("#eef2e9"), c("#ebefea"), c("#f6f3ee"), c("#ecf0e6"),
        c("#f4f4f2"), c("#f4f2f2"), c("#f2f1f2"), c("#f5f1f1"), c("#e8ebe4"), c("#f4f1ef"), c("#f5f4ec"), c("#ebf0e8"), c("#f1f0f1"), c("#e6ebde"),
        c("#e9e8e5"), c("#dedcd8"),
        c("#ffffff"), c("#ffffff"), c("#ffffff"), c("#ffffff"), c("#ffffff"), c("#ffffff"), c("#ffffff"), c("#e3e3e3"), c("#e3e3e3"), c("#dddddd"), c("#fafaf8"),
        c("#d6d6d6"), c("#d9d9d9"), c("#dcdcdc"), c("#e0e0e0"), c("#e2e2e2"), c("#e6e6e6"),
        0.5f,
        c("#cbd3d6"), c("#e8e8ea"), c("#efeff0"),
        c("#a8a8b5"), c("#c8c8d0"), c("#d8d8de"),
        c("#444444"), c("#777777"), c("#888888"), c("#9a9a9a"), c("#999999"), c("#8fa5ae"), c("#8a8a8a"), c("#8a8a8a"), c("#8a8a8a"),
        c("#fafaf8", 230), c("#f3f6f7", 220),
        false,
    };
    return p;
}

// CARTO Dark Matter.
const Palette& darkPalette()
{
    static const Palette p{
        c("#1b1c1e"), c("#0f1114"), c("#23272c"),
        c("#1d1e1f"), c("#26292b"), c("#1f241f"), c("#222222"), c("#202420"), c("#1e2321"), c("#24231f"), c("#1f231f"),
        c("#1f1f21"), c("#222024"), c("#212023"), c("#241f22"), c("#1f2420"), c("#231f1f"), c("#232320"), c("#1f2320"), c("#212023"), c("#1e2620"),
        c("#262729"), c("#303134"),
        c("#4b4c4f"), c("#46474a"), c("#404144"), c("#3a3b3e"), c("#353639"), c("#2e2f32"), c("#2a2b2e"), c("#2c2c2e"), c("#2c2c2e"), c("#3a3a3c"), c("#1b1c1e"),
        c("#000000", 0), c("#000000", 0), c("#000000", 0), c("#000000", 0), c("#000000", 0), c("#000000", 0),
        0.0f,
        c("#14181c"), c("#2c2c30"), c("#242427"),
        c("#6b6b75"), c("#4b4b55"), c("#3c3c44"),
        c("#d0d0d0"), c("#9a9a9a"), c("#a0a0a0"), c("#7a7a7a"), c("#8c8c8c"), c("#4f6378"), c("#8a8a8a"), c("#8a8a8a"), c("#8a8a8a"),
        c("#111111", 230), c("#0d0d0f", 220),
        false,
    };
    return p;
}

// Over satellite imagery (any theme): light roads, white names with a dark halo.
const Palette& hybridPalette()
{
    static const Palette p{
        c("#000000", 0), c("#000000", 0), c("#8ecbff", 200),
        {}, {}, {}, {}, {}, {}, {}, {},
        {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
        c("#000000", 0), c("#ffffff", 115),
        c("#ffd27a", 235), c("#ffe08a", 230), c("#fff1b0", 220), c("#ffffff", 200), c("#ffffff", 190), c("#ffffff", 150), c("#ffffff", 120), c("#ffffff", 120), c("#ffffff", 110), c("#d0d0d0", 180), c("#000000", 0),
        c("#000000", 0), c("#000000", 0), c("#000000", 0), c("#000000", 0), c("#000000", 0), c("#000000", 0),
        0.0f,
        c("#8ecbff", 215), c("#d0d0d8", 180), c("#d0d0d8", 120),
        c("#ffcaff", 220), c("#ffd6ff", 160), c("#ffd6ff", 110),
        c("#ffffff"), c("#f2f2f2"), c("#ffe6ff"), c("#ffe6ff"), c("#ffffff"), c("#bfe3ff"), c("#ffffff"), c("#ffffff"), c("#ffffff"),
        c("#000000", 200), c("#000000", 200),
        true,
    };
    return p;
}

const Palette& palette(int index)
{
    switch (index) {
    case 1: return positronPalette();
    case 2: return darkPalette();
    default: return standardPalette();
    }
}

QString tag(const QHash<QString, QVariant>& tags, const char* key)
{
    return tags.value(QLatin1String(key)).toString();
}

int tagInt(const QHash<QString, QVariant>& tags, const char* key, int fallback)
{
    bool ok = false;
    const int value = tags.value(QLatin1String(key)).toInt(&ok);
    return ok ? value : fallback;
}

/// Mapbox Streets layer names onto the OpenMapTiles ones the rules use.
QString normalizedLayer(const QString& layer)
{
    static const struct { const char* from; const char* to; } kAliases[] = {
        { "road", "transportation" }, { "road_label", "transportation_name" }, { "admin", "boundary" },
        { "place_label", "place" }, { "poi_label", "poi" }, { "airport_label", "aerodrome_label" },
        { "waterway_label", "waterway" }, { "landuse_overlay", "park" }, { "natural_label", "mountain_peak" },
    };
    for (const auto& alias : kAliases) {
        if (layer == QLatin1String(alias.from))
            return QString::fromLatin1(alias.to);
    }
    return layer;
}

/// Road class onto the width/colour rank: path 0 to motorway 8, rail 9; -1 = not a road line.
int roadRank(QString klass)
{
    klass.remove(QLatin1String("_construction"));
    if (klass == QLatin1String("motorway")) return 8;
    if (klass == QLatin1String("trunk")) return 7;
    if (klass == QLatin1String("primary")) return 6;
    if (klass == QLatin1String("secondary")) return 5;
    if (klass == QLatin1String("tertiary")) return 4;
    if (klass == QLatin1String("minor") || klass == QLatin1String("street") || klass == QLatin1String("street_limited")
        || klass == QLatin1String("residential") || klass == QLatin1String("unclassified") || klass == QLatin1String("busway")
        || klass == QLatin1String("raceway"))
        return 3;
    if (klass == QLatin1String("service")) return 2;
    if (klass == QLatin1String("track")) return 1;
    if (klass == QLatin1String("path") || klass == QLatin1String("pedestrian")) return 0;
    if (klass == QLatin1String("rail") || klass == QLatin1String("transit") || klass == QLatin1String("major_rail")
        || klass == QLatin1String("minor_rail"))
        return 9;
    return -1;
}

struct Stop { float zoom; float value; };

float interpolate(const Stop* stops, int count, double zoom)
{
    if (zoom <= stops[0].zoom)
        return stops[0].value * float(std::pow(0.6, stops[0].zoom - zoom));  // fades out below the first stop
    for (int i = 1; i < count; ++i) {
        if (zoom <= stops[i].zoom) {
            const double t = (zoom - stops[i - 1].zoom) / (stops[i].zoom - stops[i - 1].zoom);
            // Exponential between stops, like the web styles.
            return float(stops[i - 1].value * std::pow(double(stops[i].value / stops[i - 1].value), t));
        }
    }
    const Stop& last = stops[count - 1];
    return last.value * float(std::pow(2.0, (zoom - last.zoom) * 0.5));
}

float tableWidth(int rank, double zoom)
{
    static const Stop motorway[] = { { 5, 0.8f }, { 7, 1.2f }, { 9, 2.0f }, { 11, 3.0f }, { 13, 4.5f }, { 15, 8.0f }, { 17, 15.0f }, { 18, 20.0f } };
    static const Stop primary[] = { { 7, 0.7f }, { 9, 1.4f }, { 11, 2.5f }, { 13, 4.0f }, { 15, 7.5f }, { 17, 14.0f }, { 18, 18.0f } };
    static const Stop secondary[] = { { 8, 0.6f }, { 10, 1.2f }, { 12, 2.4f }, { 14, 4.5f }, { 16, 9.0f }, { 18, 16.0f } };
    static const Stop tertiary[] = { { 10, 0.7f }, { 12, 1.8f }, { 14, 4.0f }, { 16, 8.0f }, { 18, 14.0f } };
    static const Stop minor[] = { { 11, 0.5f }, { 13, 1.2f }, { 14, 2.6f }, { 16, 6.0f }, { 18, 11.0f } };
    static const Stop service[] = { { 13, 0.5f }, { 14, 1.2f }, { 16, 3.0f }, { 18, 6.0f } };
    static const Stop track[] = { { 12, 0.5f }, { 14, 1.0f }, { 16, 1.6f }, { 18, 2.4f } };
    static const Stop path[] = { { 13, 0.5f }, { 15, 1.0f }, { 17, 1.6f }, { 18, 2.0f } };
    static const Stop rail[] = { { 9, 0.5f }, { 11, 0.9f }, { 13, 1.5f }, { 15, 2.5f }, { 18, 3.5f } };
#define WIDTH(table) interpolate(table, int(sizeof(table) / sizeof(table[0])), zoom)
    switch (rank) {
    case 8: return WIDTH(motorway);
    case 7: return WIDTH(motorway) * 0.95f;
    case 6: return WIDTH(primary);
    case 5: return WIDTH(secondary);
    case 4: return WIDTH(tertiary);
    case 3: return WIDTH(minor);
    case 2: return WIDTH(service);
    case 1: return WIDTH(track);
    case 0: return WIDTH(path);
    case 9: return WIDTH(rail);
    default: return 0.0f;
    }
}

int roadMinZoom(int rank)
{
    static const int kMinZoom[] = { 14, 13, 13, 12, 10, 8, 7, 5, 4, 9 };
    return rank >= 0 && rank <= 9 ? kMinZoom[rank] : 99;
}

MapTheme::Line widthLine(const QColor& color, float width, float growth, int order)
{
    MapTheme::Line line;
    line.color = color;
    line.width = width;
    line.growth = growth;
    line.order = order;
    return line;
}

float growthOf(float atZoom, float atNextZoom)
{
    return atZoom > 0.0f ? qBound(1.0f, atNextZoom / atZoom, 2.2f) : 1.0f;
}

QColor landColor(const Palette& p, const QString& layer, const QString& klass, const QString& subclass)
{
    const QString k = klass;
    if (layer == QLatin1String("park"))
        return p.park;
    if (k == QLatin1String("wood") || k == QLatin1String("forest") || subclass == QLatin1String("forest"))
        return p.wood;
    if (k == QLatin1String("grass") || k == QLatin1String("meadow") || k == QLatin1String("grassland")
        || k == QLatin1String("garden") || k == QLatin1String("village_green") || k == QLatin1String("recreation_ground"))
        return p.grass;
    if (k == QLatin1String("farmland") || k == QLatin1String("farm") || k == QLatin1String("orchard")
        || k == QLatin1String("vineyard") || k == QLatin1String("allotments"))
        return p.farmland;
    if (k == QLatin1String("ice") || k == QLatin1String("glacier"))
        return p.ice;
    if (k == QLatin1String("rock") || k == QLatin1String("bare_rock") || k == QLatin1String("scree") || k == QLatin1String("quarry"))
        return p.rock;
    if (k == QLatin1String("wetland") || k == QLatin1String("swamp") || k == QLatin1String("bog") || k == QLatin1String("marsh"))
        return p.wetland;
    if (k == QLatin1String("sand") || k == QLatin1String("beach") || k == QLatin1String("dune"))
        return p.sand;
    if (k == QLatin1String("scrub") || k == QLatin1String("heath"))
        return p.scrub;
    if (k == QLatin1String("residential") || k == QLatin1String("suburb") || k == QLatin1String("quarter")
        || k == QLatin1String("neighbourhood"))
        return p.residential;
    if (k == QLatin1String("commercial"))
        return p.commercial;
    if (k == QLatin1String("industrial") || k == QLatin1String("garages") || k == QLatin1String("dam"))
        return p.industrial;
    if (k == QLatin1String("retail"))
        return p.retail;
    if (k == QLatin1String("cemetery") || k == QLatin1String("grave_yard"))
        return p.cemetery;
    if (k == QLatin1String("military"))
        return p.military;
    if (k == QLatin1String("school") || k == QLatin1String("university") || k == QLatin1String("college")
        || k == QLatin1String("kindergarten") || k == QLatin1String("hospital") || k == QLatin1String("library"))
        return p.institution;
    if (k == QLatin1String("stadium") || k == QLatin1String("pitch") || k == QLatin1String("playground")
        || k == QLatin1String("track") || k == QLatin1String("theme_park") || k == QLatin1String("zoo"))
        return p.sport;
    if (k == QLatin1String("railway") || k == QLatin1String("bus_station"))
        return p.railwayArea;
    return QColor(0, 0, 0, 0);
}

/// POI colour by kind (OSM Carto's grouping).
QColor poiColor(const Palette& p, const QString& klass)
{
    if (!p.poiColors)
        return p.poiDefault;
    static const struct { const char* klass; const char* color; } kGroups[] = {
        { "restaurant", "#c77400" }, { "cafe", "#c77400" }, { "fast_food", "#c77400" }, { "bar", "#c77400" },
        { "beer", "#c77400" }, { "ice_cream", "#c77400" },
        { "shop", "#ac39ac" }, { "grocery", "#ac39ac" }, { "clothing_store", "#ac39ac" }, { "bakery", "#ac39ac" },
        { "hospital", "#bf0000" }, { "pharmacy", "#bf0000" }, { "doctors", "#bf0000" }, { "dentist", "#bf0000" },
        { "school", "#7f5a2c" }, { "college", "#7f5a2c" }, { "library", "#7f5a2c" }, { "kindergarten", "#7f5a2c" },
        { "bus", "#0092da" }, { "railway", "#0092da" }, { "fuel", "#0092da" }, { "parking", "#0092da" },
        { "car", "#0092da" }, { "bicycle", "#0092da" }, { "airport", "#0092da" },
        { "park", "#0e8a3a" }, { "playground", "#0e8a3a" }, { "pitch", "#0e8a3a" }, { "swimming", "#0e8a3a" },
        { "stadium", "#0e8a3a" }, { "garden", "#0e8a3a" },
        { "place_of_worship", "#333333" },
        { "attraction", "#2f6aa6" }, { "museum", "#2f6aa6" }, { "lodging", "#2f6aa6" }, { "viewpoint", "#2f6aa6" },
        { "monument", "#2f6aa6" }, { "castle", "#2f6aa6" }, { "information", "#2f6aa6" },
    };
    for (const auto& group : kGroups) {
        if (klass == QLatin1String(group.klass))
            return QColor::fromString(QLatin1String(group.color));
    }
    return p.poiDefault;
}

int placeRank(const QString& klass)
{
    static const char* kClasses[] = { "country", "state", "province", "city", "town", "village", "suburb", "hamlet" };
    for (int i = 0; i < int(sizeof(kClasses) / sizeof(kClasses[0])); ++i) {
        if (klass == QLatin1String(kClasses[i]))
            return i;
    }
    return 9;
}

} // namespace

QStringList MapTheme::ids()
{
    return { QStringLiteral("standard"), QStringLiteral("positron"), QStringLiteral("dark") };
}

QString MapTheme::displayName(const QString& id)
{
    if (id == QLatin1String("positron"))
        return QStringLiteral("Positron");
    if (id == QLatin1String("dark"))
        return QStringLiteral("Dark");
    return QStringLiteral("Standard");
}

const MapTheme& MapTheme::theme(const QString& id)
{
    static const MapTheme standard(QStringLiteral("standard"));
    static const MapTheme positron(QStringLiteral("positron"));
    static const MapTheme dark(QStringLiteral("dark"));
    if (id == QLatin1String("positron"))
        return positron;
    if (id == QLatin1String("dark"))
        return dark;
    return standard;
}

MapTheme::MapTheme(const QString& id)
    : m_id(id)
    , m_palette(id == QLatin1String("positron") ? 1 : (id == QLatin1String("dark") ? 2 : 0))
{
}

QColor MapTheme::background() const
{
    return palette(m_palette).background;
}

QColor MapTheme::water() const
{
    return palette(m_palette).water;
}

float MapTheme::roadWidth(const QString& klass, double zoom)
{
    return tableWidth(roadRank(klass), zoom);
}

MapTheme::FeatureStyle MapTheme::featureStyle(const QString& layerName, const QHash<QString, QVariant>& tags,
                                              int geometryType, int zoom, bool overlay) const
{
    FeatureStyle style;
    const Palette& p = overlay ? hybridPalette() : palette(m_palette);
    const QString layer = normalizedLayer(layerName);
    const QString klass = tag(tags, "class");

    if (layer == QLatin1String("water")) {
        if (geometryType == 3) {
            style.fill = p.water;
            style.fillOrder = 20;
            if (overlay)
                style.outline = p.waterOutline;
        }
        return style;
    }

    if (layer == QLatin1String("landcover") || layer == QLatin1String("landuse") || layer == QLatin1String("park")) {
        if (geometryType == 3 && !overlay) {
            style.fill = landColor(p, layer, klass, tag(tags, "subclass"));
            style.fillOrder = layer == QLatin1String("landcover") ? 10 : (layer == QLatin1String("landuse") ? 12 : 14);
        }
        return style;
    }

    if (layer == QLatin1String("building")) {
        if (geometryType == 3) {
            if (!overlay && zoom >= 13) {
                style.fill = p.building;
                style.fillOrder = 30;
            }
            if (zoom >= (overlay ? 15 : 14))
                style.outline = p.buildingOutline;
        }
        return style;
    }

    if (layer == QLatin1String("aeroway")) {
        if (geometryType == 3 && !overlay) {
            style.fill = klass == QLatin1String("apron") ? p.apron : p.runway;
            style.fillOrder = 16;
        }
        else if (geometryType == 2 && zoom >= 10) {
            static const Stop runway[] = { { 10, 1.0f }, { 12, 2.5f }, { 14, 8.0f }, { 16, 24.0f }, { 18, 60.0f } };
            static const Stop taxiway[] = { { 12, 0.5f }, { 14, 1.5f }, { 16, 4.0f }, { 18, 8.0f } };
            const bool isRunway = klass == QLatin1String("runway");
            const Stop* stops = isRunway ? runway : taxiway;
            const int count = isRunway ? 5 : 4;
            if (!isRunway && zoom < 12)
                return style;
            const float width = interpolate(stops, count, zoom);
            style.line = widthLine(p.runway, width, growthOf(width, interpolate(stops, count, zoom + 1)), 150);
        }
        return style;
    }

    if (layer == QLatin1String("waterway")) {
        if (geometryType != 2)
            return style;
        static const Stop river[] = { { 8, 0.5f }, { 10, 1.0f }, { 12, 2.0f }, { 14, 4.0f }, { 16, 7.0f }, { 18, 12.0f } };
        static const Stop stream[] = { { 12, 0.5f }, { 14, 1.0f }, { 16, 1.8f }, { 18, 2.5f } };
        static const Stop ditch[] = { { 14, 0.5f }, { 16, 1.0f }, { 18, 1.5f } };
        const bool major = klass == QLatin1String("river") || klass == QLatin1String("canal");
        const bool minorStream = klass == QLatin1String("stream");
        const Stop* stops = major ? river : (minorStream ? stream : ditch);
        const int count = major ? 6 : (minorStream ? 4 : 3);
        if (zoom < int(stops[0].zoom) - (major ? 1 : 0))
            return style;
        const float width = interpolate(stops, count, zoom) * (klass == QLatin1String("canal") ? 0.8f : 1.0f);
        style.line = widthLine(p.waterway, width, growthOf(width, interpolate(stops, count, zoom + 1)), 100);
        if (tagInt(tags, "intermittent", 0) == 1 || tag(tags, "brunnel") == QLatin1String("tunnel")) {
            style.line.dash = 4.0f;
            style.line.gap = 3.0f;
        }
        return style;
    }

    if (layer == QLatin1String("boundary")) {
        if (geometryType != 2 && geometryType != 3)
            return style;
        if (tagInt(tags, "maritime", 0) == 1)
            return style;
        const int level = tagInt(tags, "admin_level", 2);
        static const Stop country[] = { { 2, 0.5f }, { 4, 0.8f }, { 6, 1.2f }, { 8, 1.8f }, { 10, 2.5f }, { 14, 3.0f } };
        static const Stop state[] = { { 4, 0.4f }, { 6, 0.6f }, { 8, 0.9f }, { 12, 1.4f }, { 16, 2.0f } };
        if (level <= 2) {
            const float width = interpolate(country, 6, zoom);
            style.line = widthLine(p.boundaryCountry, width, growthOf(width, interpolate(country, 6, zoom + 1)), 600);
            style.line.dash = 7.0f;
            style.line.gap = 3.0f;
        }
        else if (level <= 4) {
            if (zoom < 4)
                return style;
            const float width = interpolate(state, 5, zoom);
            style.line = widthLine(p.boundaryState, width, growthOf(width, interpolate(state, 5, zoom + 1)), 599);
            style.line.dash = 4.0f;
            style.line.gap = 2.5f;
        }
        else if (zoom >= 11) {
            style.line = widthLine(p.boundaryOther, 0.8f, 1.1f, 598);
            style.line.dash = 2.0f;
            style.line.gap = 2.0f;
        }
        if (tagInt(tags, "disputed", 0) == 1)
            style.line.gap *= 2.0f;
        return style;
    }

    if (layer == QLatin1String("transportation")) {
        if (geometryType != 2)
            return style;
        const int rank = roadRank(klass);
        if (rank < 0 || zoom < roadMinZoom(rank))
            return style;
        const QString brunnel = tag(tags, "brunnel");
        const bool bridge = brunnel == QLatin1String("bridge");
        const bool tunnel = brunnel == QLatin1String("tunnel");
        const bool ramp = tagInt(tags, "ramp", 0) == 1;
        float width = tableWidth(rank, zoom);
        float next = tableWidth(rank, zoom + 1);
        if (ramp) {
            width *= 0.7f;
            next *= 0.7f;
        }
        const float growth = growthOf(width, next);
        const int tier = bridge ? 400 : 200;

        if (rank == 9) {  // railway: grey with white dashes
            style.line = widthLine(p.rail, width + (p.railDash.alpha() ? 1.0f : 0.0f), growth, tier + 109);
            if (p.railDash.alpha() > 0 && zoom >= 13) {
                style.overline = widthLine(p.railDash, width * 0.5f, growth, tier + 110);
                style.overline.dash = 6.0f;
                style.overline.gap = 6.0f;
            }
            return style;
        }

        static const QColor Palette::*kFill[] = { &Palette::path, &Palette::track, &Palette::service, &Palette::minor,
                                                  &Palette::tertiary, &Palette::secondary, &Palette::primary,
                                                  &Palette::trunk, &Palette::motorway };
        static const QColor Palette::*kCasing[] = { nullptr, nullptr, &Palette::minorCasing, &Palette::minorCasing,
                                                    &Palette::tertiaryCasing, &Palette::secondaryCasing,
                                                    &Palette::primaryCasing, &Palette::trunkCasing, &Palette::motorwayCasing };
        QColor fill = p.*kFill[rank];
        if (klass.endsWith(QLatin1String("_construction")))
            fill.setAlphaF(fill.alphaF() * 0.6);
        if (tunnel)
            fill.setAlphaF(fill.alphaF() * 0.65);
        style.line = widthLine(fill, width, growth, tier + 100 + rank);
        if (rank <= 1) {  // paths and tracks: dashed, no casing
            style.line.dash = rank == 0 ? 2.5f : 4.0f;
            style.line.gap = 2.0f;
            return style;
        }

        static const int kCasingZoom[] = { 99, 99, 14, 13, 11, 10, 8, 8, 8 };
        if (kCasing[rank] && p.casingPx > 0.0f && zoom >= kCasingZoom[rank]) {
            const float side = p.casingPx * (zoom >= 14 ? 1.0f : (zoom >= 12 ? 0.75f : 0.5f));
            QColor casing = p.*kCasing[rank];
            if (tunnel)
                casing.setAlphaF(casing.alphaF() * 0.5);
            style.casing = widthLine(casing, width + 2.0f * side, growthOf(width + 2.0f * side, next + 2.0f * side), tier + rank);
            if (tunnel) {
                style.casing.dash = 4.0f;
                style.casing.gap = 2.0f;
            }
        }
        return style;
    }

    // Layers without a rule: a thin line for lines, an outline for areas.
    if (layer == QLatin1String("place") || layer == QLatin1String("poi") || layer == QLatin1String("water_name")
        || layer == QLatin1String("transportation_name") || layer == QLatin1String("housenumber")
        || layer == QLatin1String("aerodrome_label") || layer == QLatin1String("mountain_peak"))
        return style;  // labels only
    if (geometryType == 2)
        style.line = widthLine(QColor(150, 150, 140, 170), 0.8f, 1.1f, 590);
    else if (geometryType == 3)
        style.outline = QColor(150, 150, 140, 120);
    return style;
}

bool MapTheme::isLabelLayer(const QString& layerName)
{
    const QString layer = normalizedLayer(layerName);
    return layer == QLatin1String("place") || layer == QLatin1String("water_name")
        || layer == QLatin1String("transportation_name") || layer == QLatin1String("poi")
        || layer == QLatin1String("aerodrome_label") || layer == QLatin1String("mountain_peak")
        || layer == QLatin1String("housenumber") || layer == QLatin1String("waterway");
}

MapTheme::LabelStyle MapTheme::labelStyle(const QString& layerName, const QHash<QString, QVariant>& tags,
                                          int zoom, bool overlay) const
{
    return labelStyle(layerName, tag(tags, "class"), tagInt(tags, "rank", -1), zoom, overlay);
}

MapTheme::LabelStyle MapTheme::labelStyle(const QString& layerName, const QString& klass, int tagRank,
                                          int zoom, bool overlay) const
{
    LabelStyle style;
    const Palette& p = overlay ? hybridPalette() : palette(m_palette);
    const QString layer = normalizedLayer(layerName);
    const int rank = tagRank < 0 ? 30 : qBound(0, tagRank, 999);
    style.halo = p.halo;
    style.haloWidth = 1.6f;

    auto show = [&style](const QColor& color, float size, int priority) {
        style.visible = true;
        style.color = color;
        style.size = size;
        style.priority = priority;
    };

    if (layer == QLatin1String("place")) {
        const int classRank = placeRank(klass);
        if (klass == QLatin1String("country")) {
            if (zoom < 2 || zoom > 7)
                return style;
            show(p.country, zoom >= 5 ? 14.0f : 12.0f, rank);
            style.weight = 600;
        }
        else if (klass == QLatin1String("state") || klass == QLatin1String("province")) {
            if (zoom < 5 || zoom > 9)
                return style;
            show(p.state, zoom >= 7 ? 12.0f : 11.0f, 100 + rank);
            style.italic = true;
        }
        else if (klass == QLatin1String("city")) {
            const int maxRank = zoom >= 8 ? 999 : (zoom >= 6 ? 6 : 3);
            if (zoom < 4 || rank > maxRank)
                return style;
            show(p.placeMajor, zoom >= 11 ? 15.5f : (zoom >= 8 ? 14.0f : 12.5f), 200 + rank);
            style.weight = 600;
        }
        else if (klass == QLatin1String("town")) {
            if (zoom < 8 || (zoom < 10 && rank > 12))
                return style;
            show(p.placeMajor, zoom >= 12 ? 13.5f : 12.0f, 300 + rank);
            style.weight = 500;
        }
        else if (klass == QLatin1String("village")) {
            if (zoom < 11)
                return style;
            show(p.placeMinor, zoom >= 14 ? 12.5f : 11.5f, 400 + rank);
        }
        else if (klass == QLatin1String("suburb") || klass == QLatin1String("quarter")
                 || klass == QLatin1String("neighbourhood")) {
            if (zoom < (klass == QLatin1String("suburb") ? 12 : 14))
                return style;
            show(p.placeMinor, 10.5f, 450 + rank);
            style.uppercase = true;
            style.letterSpacing = 1.0f;
        }
        else if (klass == QLatin1String("island") || klass == QLatin1String("islet")) {
            if (zoom < 10)
                return style;
            show(p.placeMinor, 11.0f, 470 + rank);
            style.italic = true;
        }
        else if (classRank == 9 && klass != QLatin1String("continent")) {  // hamlet, locality, ...
            if (zoom < 13)
                return style;
            show(p.placeMinor, 10.5f, 500 + rank);
        }
        return style;
    }

    if (layer == QLatin1String("water_name")) {
        const bool ocean = klass == QLatin1String("ocean");
        const bool sea = klass == QLatin1String("sea");
        if (zoom < (ocean ? 1 : (sea ? 4 : 8)))
            return style;
        show(p.waterLabel, ocean ? 13.0f : (sea ? 12.0f : 11.0f), 550 + rank);
        style.italic = true;
        style.halo = p.waterHalo;
        style.letterSpacing = ocean || sea ? 1.5f : 0.0f;
        return style;
    }

    if (layer == QLatin1String("waterway")) {  // river names along the river
        if (zoom < 12 || (klass != QLatin1String("river") && klass != QLatin1String("canal") && zoom < 15))
            return style;
        show(p.waterLabel, 11.0f, 650);
        style.italic = true;
        style.halo = p.waterHalo;
        style.alongLine = true;
        return style;
    }

    if (layer == QLatin1String("transportation_name")) {
        const int roadClass = roadRank(klass);
        static const int kMinZoom[] = { 16, 15, 15, 15, 13, 13, 12, 12, 11, 14 };
        if (roadClass < 0 || zoom < kMinZoom[roadClass])
            return style;
        show(p.roadLabel, zoom >= 15 ? 11.5f : 10.5f, 700 + (9 - roadClass));
        style.alongLine = true;
        return style;
    }

    if (layer == QLatin1String("poi")) {
        const int maxRank = zoom >= 17 ? 999 : (zoom >= 16 ? 30 : 10);
        if (zoom < 15 || rank > maxRank)
            return style;
        const QColor color = poiColor(p, klass);
        show(overlay ? p.poiDefault : color, 10.5f, 900 + rank);
        style.marker = Marker::Circle;
        style.markerColor = overlay ? color : (p.poiColors ? color : p.poiDefault.lighter(130));
        return style;
    }

    if (layer == QLatin1String("aerodrome_label")) {
        if (zoom < 10)
            return style;
        show(p.airport, 11.0f, 600);
        style.marker = Marker::Square;
        style.markerColor = p.airport;
        return style;
    }

    if (layer == QLatin1String("mountain_peak")) {
        if (zoom < 12 || (zoom < 14 && rank > 5))
            return style;
        show(p.peak, 10.5f, 800 + rank);
        style.marker = Marker::Triangle;
        style.markerColor = p.poiColors ? QColor(208, 143, 85) : p.peak;
        return style;
    }

    if (layer == QLatin1String("housenumber")) {
        if (zoom < 17)
            return style;
        show(p.placeMinor, 9.0f, 1000);
        return style;
    }
    return style;
}
