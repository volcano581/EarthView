#include "SmoothNavigation.h"

#include <cmath>

double SmoothNavigation::easeStep(double current, double target, double dtSeconds, double tau)
{
    if (dtSeconds <= 0.0 || tau <= 0.0)
        return target - current;
    return (target - current) * (1.0 - std::exp(-dtSeconds / tau));
}

QPointF SmoothNavigation::decayVelocity(const QPointF& velocity, double dtSeconds, double tau)
{
    if (dtSeconds <= 0.0 || tau <= 0.0)
        return velocity;
    return velocity * std::exp(-dtSeconds / tau);
}

QPointF SmoothNavigation::releaseVelocity(const QVector<DragSample>& samples, qint64 releaseTimeMs)
{
    const DragSample* first = nullptr;
    const DragSample* last = nullptr;
    for (const DragSample& sample : samples) {
        if (releaseTimeMs - sample.timeMs > kVelocityWindowMs)
            continue;
        if (!first)
            first = &sample;
        last = &sample;
    }
    if (!first || !last || first == last)
        return QPointF();
    const double seconds = (last->timeMs - first->timeMs) / 1000.0;
    if (seconds <= 0.0)
        return QPointF();
    return (last->position - first->position) / seconds;
}
