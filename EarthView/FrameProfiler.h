#pragma once
#ifndef FRAMEPROFILER_H
#define FRAMEPROFILER_H

#include <QElapsedTimer>
#include <QString>

class FrameProfiler
{
public:
    class Scope
    {
    public:
        explicit Scope(const QString& name);
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        QString m_name;
        QElapsedTimer m_timer;
        bool m_enabled;
    };

    static bool isEnabled();
    static void beginFrame();
    static void endFrame();
    static void recordDuration(const QString& name, qint64 elapsedNs);
    static void recordCount(const QString& name, qint64 count = 1);
};

#endif // FRAMEPROFILER_H
