#pragma once
#ifndef MAPTHEME_H
#define MAPTHEME_H

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>

/**
 * @brief How a vector (OSM, OpenMapTiles schema) map is drawn: one of a few themes.
 *
 * - **standard**: the colours of the OpenStreetMap website (OSM Carto): pink motorways, orange
 *   primaries, yellow secondaries, white streets with grey casings, green parks, blue water,
 *   purple dashed boundaries, black names with a white halo.
 * - **positron**: CARTO Positron, a pale grey map that lets data on top stand out.
 * - **dark**: CARTO Dark Matter, its dark counterpart.
 *
 * Details drawn over satellite imagery (overlay) use a "hybrid" look whatever the theme: light
 * roads without casings and white names with a dark halo, no area fills.
 *
 * Widths are in screen pixels at the zoom asked for; `growth` is the factor per zoom level the
 * renderer applies between levels, so widths change smoothly while zooming. Pure (no GL), so it
 * is unit-tested.
 */
class MapTheme
{
public:
    struct Line {
        QColor color = QColor(0, 0, 0, 0);
        float width = 0.0f;   ///< px
        float growth = 1.0f;  ///< width factor per zoom level
        float dash = 0.0f;    ///< px; 0 = solid
        float gap = 0.0f;
        int order = 0;        ///< draw order over all tiles (casings below fills)
        bool isVisible() const { return color.alpha() > 0 && width > 0.0f; }
    };

    struct FeatureStyle {
        QColor fill = QColor(0, 0, 0, 0);
        int fillOrder = 0;
        QColor outline = QColor(0, 0, 0, 0);  ///< 1 px hairline around areas
        Line casing;
        Line line;
        Line overline;                          ///< e.g. the white dashes on a railway
    };

    enum class Marker { None, Circle, Triangle, Square };

    struct LabelStyle {
        bool visible = false;
        QColor color;
        QColor halo;
        float haloWidth = 1.5f;
        float size = 11.0f;   ///< px
        int weight = 400;
        bool italic = false;
        bool uppercase = false;
        float letterSpacing = 0.0f;  ///< px
        bool alongLine = false;      ///< road names follow the road
        Marker marker = Marker::None;
        QColor markerColor;
        int priority = 0;            ///< lower is placed first
    };

    /// "standard", "positron", "dark".
    static QStringList ids();
    static QString displayName(const QString& id);
    /// Unknown ids give the standard theme.
    static const MapTheme& theme(const QString& id);

    QString id() const { return m_id; }
    QColor background() const;
    QColor water() const;

    /// Style of one feature of @p layer at @p zoom (integer display zoom). @p overlay: drawn over
    /// satellite imagery.
    FeatureStyle featureStyle(const QString& layer, const QHash<QString, QVariant>& tags, int geometryType,
                              int zoom, bool overlay) const;
    /// Label of one feature at @p zoom; `visible` false when it has none at this zoom.
    LabelStyle labelStyle(const QString& layer, const QHash<QString, QVariant>& tags, int zoom, bool overlay) const;
    /// The same from the two attributes it depends on (`class`, `rank`; rank < 0 = none).
    LabelStyle labelStyle(const QString& layer, const QString& klass, int rank, int zoom, bool overlay) const;
    /// True for the layers whose features carry names to show.
    static bool isLabelLayer(const QString& layer);

    /// Road width (px) of an OpenMapTiles transportation class at @p zoom (fractional zooms
    /// interpolate). Exposed for tests and thumbnails.
    static float roadWidth(const QString& klass, double zoom);

private:
    explicit MapTheme(const QString& id);
    QString m_id;
    int m_palette;
};

#endif // MAPTHEME_H
