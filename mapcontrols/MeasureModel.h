#pragma once
#ifndef MEASUREMODEL_H
#define MEASUREMODEL_H

#include "GeoMeasure.h"

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

/**
 * @brief Points and result of the measure tool, exposed to QML as `measure`. GL-free.
 *
 * Line mode measures one leg: a third click starts a new line. Path mode adds legs until the
 * user clears it. Points are (lat, lon) degrees, double precision.
 */
class MeasureModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY pointsChanged)
    Q_PROPERTY(QString totalText READ totalText NOTIFY resultChanged)
    Q_PROPERTY(QString lastLegText READ lastLegText NOTIFY resultChanged)

public:
    enum Mode {
        LineMode = 0,
        PathMode = 1
    };
    Q_ENUM(Mode)

    explicit MeasureModel(QObject* parent = nullptr);

    int mode() const { return m_mode; }
    void setMode(int mode);
    /// GeoMeasure::Unit as int (0 km, 1 mi, 2 nmi, 3 m, 4 ft).
    int unit() const { return m_unit; }
    void setUnit(int unit);

    const QVector<QPointF>& points() const { return m_points; }
    int pointCount() const { return m_points.size(); }
    void addPoint(const QPointF& latLon);
    Q_INVOKABLE void removeLastPoint();
    Q_INVOKABLE void clear();

    double totalMeters() const;
    QString totalText() const;
    QString lastLegText() const;
    QString format(double meters) const;

signals:
    void modeChanged(int mode);
    void unitChanged(int unit);
    void pointsChanged();
    void resultChanged();

private:
    int m_mode;
    int m_unit;
    QVector<QPointF> m_points;
};

#endif // MEASUREMODEL_H
