#include "FrameProfiler.h"

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>

namespace {
constexpr qint64 kReportIntervalMs = 1000;

struct Metric {
    qint64 totalNs = 0;
    qint64 maxNs = 0;
    qint64 samples = 0;
};

struct ProfilerState {
    bool enabled = false;
    bool initialized = false;
    bool frameActive = false;
    qint64 frameCount = 0;
    QElapsedTimer reportTimer;
    QElapsedTimer frameTimer;
    QHash<QString, Metric> durations;
    QHash<QString, qint64> counts;
};

ProfilerState& state()
{
    static ProfilerState profilerState;
    if (!profilerState.initialized) {
        const QByteArray value = qgetenv("EARTHVIEW_PROFILE").trimmed().toLower();
        profilerState.enabled = value == "1"
            || value == "true"
            || value == "yes"
            || value == "on";
        profilerState.initialized = true;
        profilerState.reportTimer.start();
    }
    return profilerState;
}

double nsToMs(qint64 ns)
{
    return static_cast<double>(ns) / 1000000.0;
}

void appendMetric(QStringList* parts, const QString& name, const Metric& metric)
{
    if (!parts || metric.samples <= 0)
        return;

    parts->append(QString("%1 avg=%2ms max=%3ms n=%4")
        .arg(name)
        .arg(nsToMs(metric.totalNs / metric.samples), 0, 'f', 3)
        .arg(nsToMs(metric.maxNs), 0, 'f', 3)
        .arg(metric.samples));
}
}

FrameProfiler::Scope::Scope(const QString& name)
    : m_name(name)
    , m_enabled(FrameProfiler::isEnabled())
{
    if (m_enabled) {
        m_timer.start();
    }
}

FrameProfiler::Scope::~Scope()
{
    if (m_enabled) {
        FrameProfiler::recordDuration(m_name, m_timer.nsecsElapsed());
    }
}

bool FrameProfiler::isEnabled()
{
    return state().enabled;
}

void FrameProfiler::beginFrame()
{
    ProfilerState& profilerState = state();
    if (!profilerState.enabled)
        return;

    profilerState.frameActive = true;
    profilerState.frameTimer.start();
}

void FrameProfiler::endFrame()
{
    ProfilerState& profilerState = state();
    if (!profilerState.enabled || !profilerState.frameActive)
        return;

    recordDuration(QStringLiteral("frame"), profilerState.frameTimer.nsecsElapsed());
    ++profilerState.frameCount;
    profilerState.frameActive = false;

    if (profilerState.reportTimer.elapsed() < kReportIntervalMs)
        return;

    QStringList parts;
    parts.reserve(profilerState.durations.size() + profilerState.counts.size() + 2);
    parts.append(QString("frames=%1").arg(profilerState.frameCount));

    QList<QString> durationNames = profilerState.durations.keys();
    std::sort(durationNames.begin(), durationNames.end());
    for (const QString& name : durationNames) {
        appendMetric(&parts, name, profilerState.durations.value(name));
    }

    QList<QString> countNames = profilerState.counts.keys();
    std::sort(countNames.begin(), countNames.end());
    for (const QString& name : countNames) {
        parts.append(QString("%1=%2").arg(name).arg(profilerState.counts.value(name)));
    }

    qDebug().noquote() << "[EarthView profile]" << parts.join(" | ");

    profilerState.frameCount = 0;
    profilerState.durations.clear();
    profilerState.counts.clear();
    profilerState.reportTimer.restart();
}

void FrameProfiler::recordDuration(const QString& name, qint64 elapsedNs)
{
    ProfilerState& profilerState = state();
    if (!profilerState.enabled || name.isEmpty() || elapsedNs < 0)
        return;

    Metric& metric = profilerState.durations[name];
    metric.totalNs += elapsedNs;
    metric.maxNs = qMax(metric.maxNs, elapsedNs);
    ++metric.samples;
}

void FrameProfiler::recordCount(const QString& name, qint64 count)
{
    ProfilerState& profilerState = state();
    if (!profilerState.enabled || name.isEmpty() || count == 0)
        return;

    profilerState.counts[name] += count;
}
