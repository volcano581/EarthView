#include "FeatureDetails.h"
#include "MapFeatures.h"

#include <QStringList>
#include <QVariantList>
#include <algorithm>
#include <cmath>

namespace {
QString text(const QHash<QString, QVariant>& tags, const QString& key)
{
    return tags.value(key).toString().trimmed();
}

QString valueText(const QVariant& value)
{
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    if (value.typeId() == QMetaType::Float || value.typeId() == QMetaType::Double)
        return QString::number(value.toDouble(), 'g', 8);
    return value.toString();
}

QString coordinate(double value, QChar positive, QChar negative)
{
    return QStringLiteral("%1%2 %3").arg(std::abs(value), 0, 'f', 5).arg(QChar(0x00B0)).arg(value < 0 ? negative : positive);
}

/// Name variants other than the English one only repeat the title in other scripts.
bool isOtherNameVariant(const QString& key)
{
    if (key == QLatin1String("name") || key == QLatin1String("name:en") || key == QLatin1String("name_en"))
        return false;
    return key.startsWith(QLatin1String("name"));
}
}

QString FeatureDetails::humanize(const QString& key)
{
    QString base = key;
    QString suffix;
    const int colon = key.indexOf(':');
    if (colon > 0) {
        base = key.left(colon);
        suffix = key.mid(colon + 1);
    }
    base.replace('_', ' ');
    if (!base.isEmpty())
        base[0] = base.at(0).toUpper();
    return suffix.isEmpty() ? base : QStringLiteral("%1 (%2)").arg(base, suffix);
}

QVariantMap FeatureDetails::describe(const QString& layerName, int geometryType,
                                     const QHash<QString, QVariant>& tags, const QPointF& latLon)
{
    const QString name = text(tags, QStringLiteral("name"));
    const QString englishName = !text(tags, QStringLiteral("name:en")).isEmpty()
        ? text(tags, QStringLiteral("name:en"))
        : text(tags, QStringLiteral("name_en"));
    const QString klass = text(tags, QStringLiteral("class"));
    const QString subclass = text(tags, QStringLiteral("subclass"));
    const QString kind = humanize(!subclass.isEmpty() ? subclass : (!klass.isEmpty() ? klass : layerName));

    QString title = !name.isEmpty() ? name : englishName;
    if (title.isEmpty())
        title = text(tags, QStringLiteral("ref"));
    if (title.isEmpty())
        title = text(tags, QStringLiteral("housenumber"));
    if (title.isEmpty())
        title = kind;

    QVariantList rows;
    auto addRow = [&rows](const QString& rowName, const QString& value) {
        if (!value.isEmpty())
            rows.append(QVariantMap{ { "name", rowName }, { "value", value } });
    };
    if (!englishName.isEmpty() && englishName != title)
        addRow(QStringLiteral("English name"), englishName);

    // Classification first, then the remaining attributes alphabetically.
    static const QStringList kLeading = { QStringLiteral("class"), QStringLiteral("subclass"),
                                          QStringLiteral("ref"), QStringLiteral("admin_level") };
    for (const QString& key : kLeading) {
        if (tags.contains(key))
            addRow(humanize(key), humanize(valueText(tags.value(key))));
    }
    QStringList keys = tags.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        if (kLeading.contains(key) || key == QLatin1String("name") || key == QLatin1String("name:en")
            || key == QLatin1String("name_en") || isOtherNameVariant(key))
            continue;
        addRow(humanize(key), valueText(tags.value(key)));
    }

    static const char* kGeometry[] = { "", "Point", "Line", "Area" };
    addRow(QStringLiteral("Geometry"), QString::fromLatin1(kGeometry[qBound(0, geometryType, 3)]));
    addRow(QStringLiteral("Map layer"), layerName);
    addRow(QStringLiteral("Location"), coordinate(latLon.x(), 'N', 'S') + QStringLiteral(", ")
                                           + coordinate(latLon.y(), 'E', 'W'));

    const QString layerLabel = MapFeatures::layerLabel(layerName);
    return QVariantMap{
        { "title", title },
        { "subtitle", kind == title || kind == layerLabel
                          ? layerLabel
                          : QStringLiteral("%1 %2 %3").arg(kind, QString(QChar(0x00B7)), layerLabel) },
        { "layer", layerName },
        { "rows", rows },
    };
}
