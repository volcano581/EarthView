#include "MeasureModel.h"

MeasureModel::MeasureModel(QObject* parent)
    : QObject(parent)
    , m_mode(LineMode)
    , m_unit(static_cast<int>(GeoMeasure::Unit::Kilometers))
{
}

void MeasureModel::setMode(int mode)
{
    if (mode != LineMode && mode != PathMode)
        return;
    if (m_mode == mode)
        return;
    m_mode = mode;
    // A path cut down to a line would silently drop legs: start over instead.
    if (m_mode == LineMode && m_points.size() > 2)
        clear();
    emit modeChanged(m_mode);
}

void MeasureModel::setUnit(int unit)
{
    if (unit < static_cast<int>(GeoMeasure::Unit::Kilometers) || unit > static_cast<int>(GeoMeasure::Unit::Feet))
        return;
    if (m_unit == unit)
        return;
    m_unit = unit;
    emit unitChanged(m_unit);
    emit resultChanged();
}

void MeasureModel::addPoint(const QPointF& latLon)
{
    if (m_mode == LineMode && m_points.size() >= 2)
        m_points.clear();
    m_points.append(latLon);
    emit pointsChanged();
    emit resultChanged();
}

void MeasureModel::removeLastPoint()
{
    if (m_points.isEmpty())
        return;
    m_points.removeLast();
    emit pointsChanged();
    emit resultChanged();
}

void MeasureModel::clear()
{
    if (m_points.isEmpty())
        return;
    m_points.clear();
    emit pointsChanged();
    emit resultChanged();
}

double MeasureModel::totalMeters() const
{
    return GeoMeasure::pathLengthMeters(m_points);
}

QString MeasureModel::format(double meters) const
{
    return GeoMeasure::format(meters, static_cast<GeoMeasure::Unit>(m_unit));
}

QString MeasureModel::totalText() const
{
    return m_points.size() < 2 ? QString() : format(totalMeters());
}

QString MeasureModel::lastLegText() const
{
    if (m_points.size() < 3)
        return QString();
    return format(GeoMeasure::distanceMeters(m_points.at(m_points.size() - 2), m_points.last()));
}
