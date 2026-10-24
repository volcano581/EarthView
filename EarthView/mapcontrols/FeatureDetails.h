#pragma once
#ifndef FEATUREDETAILS_H
#define FEATUREDETAILS_H

#include <QHash>
#include <QPointF>
#include <QString>
#include <QVariant>
#include <QVariantMap>

/**
 * @brief Turns a picked vector-tile feature into the details card's content.
 *
 * Pure (Qt Core only), so it is unit-tested. The result is what MapControlsModel exposes as
 * `selectedFeature`: "title", "subtitle", "layer" (the layer id, for its icon) and "rows" (a
 * list of {name, value}).
 */
namespace FeatureDetails {

/// @p layerName is the vector-tile layer ("transportation", "place", ...), @p geometryType the
/// MVT type (1 point, 2 line, 3 polygon), @p latLon (degrees) where the user clicked.
QVariantMap describe(const QString& layerName, int geometryType, const QHash<QString, QVariant>& tags,
                     const QPointF& latLon);

/// "admin_level" -> "Admin level", "name:en" -> "Name (en)".
QString humanize(const QString& key);

} // namespace FeatureDetails

#endif // FEATUREDETAILS_H
