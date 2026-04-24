#pragma once
#ifndef FPSCOUNTER_H
#define FPSCOUNTER_H

#include <QElapsedTimer>

class FpsCounter
{
public:
    void frameRendered()
    {
        if (!m_timer.isValid()) {
            m_timer.start();
            m_frameCount = 0;
            return;
        }

        ++m_frameCount;

        const qint64 elapsedMs = m_timer.elapsed();
        if (elapsedMs < 500)
            return;

        m_fps = (m_frameCount * 1000.0) / static_cast<double>(elapsedMs);
        m_frameCount = 0;
        m_timer.restart();
    }

    double fps() const { return m_fps; }

private:
    QElapsedTimer m_timer;
    int m_frameCount = 0;
    double m_fps = 0.0;
};

#endif // FPSCOUNTER_H
