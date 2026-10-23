#pragma once
#ifndef SMOOTHNAVIGATION_H
#define SMOOTHNAVIGATION_H

#include <QPointF>
#include <QVector>

/**
 * @brief Frame-rate independent easing for smooth zoom and inertial pan. GL-free and pure,
 * so it is unit-tested; SmoothNavigator applies it to the Camera each frame.
 */
namespace SmoothNavigation {

/// Zoom levels added per wheel notch (120 eighths of a degree); trackpads send fractions.
constexpr double kZoomPerNotch = 0.5;
/// Time constant of the zoom ease (s): ~95 % of the way after 3 * tau.
constexpr double kZoomTau = 0.07;
/// Time constant of the pan glide's speed decay (s).
constexpr double kGlideTau = 0.32;
/// Below this release speed (px/s) a drag stops dead; above it the map glides.
constexpr double kMinGlideSpeed = 150.0;
/// The glide ends once the speed falls below this (px/s).
constexpr double kStopSpeed = 12.0;
/// Release velocity is averaged over the drag's last this-many milliseconds.
constexpr qint64 kVelocityWindowMs = 80;

/// The part of (target - current) to move this frame: 1 - exp(-dt / tau) of it.
double easeStep(double current, double target, double dtSeconds, double tau);

/// Glide velocity after @p dtSeconds of exponential decay.
QPointF decayVelocity(const QPointF& velocity, double dtSeconds, double tau);

struct DragSample
{
    qint64 timeMs;
    QPointF position;
};

/// Drag velocity (px/s) at release from the samples of the last kVelocityWindowMs; zero when
/// the pointer had stopped before release (no sample in the window) or there are too few.
QPointF releaseVelocity(const QVector<DragSample>& samples, qint64 releaseTimeMs);

} // namespace SmoothNavigation

#endif // SMOOTHNAVIGATION_H
