// deck.gl-in-QWebEngine throughput spike: pushes N simulated entities from C++ to a
// deck.gl ScatterplotLayer and reports fps / latency / drops per transport.
// Usage: deckgl_bench [N=5000] [Hz=30] [mode=scheme|b64|json]

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>
#include <QtMath>

#include <cmath>
#include <cstring>

// Frame: [double t_ms][uint32 count][uint32 seq] + count * (float lon, lat, alt, heading)
class Sim : public QObject
{
    Q_OBJECT
public:
    explicit Sim(int n, QObject *parent = nullptr)
        : QObject(parent), m_n(n), m_state(n * 4)
    {
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < n; ++i) {
            m_state[i * 4 + 0] = float(rng->bounded(360.0) - 180.0);
            m_state[i * 4 + 1] = float(rng->bounded(140.0) - 70.0);
            m_state[i * 4 + 2] = 0.f;
            m_state[i * 4 + 3] = float(rng->bounded(360.0));
        }
    }

    void step()
    {
        for (int i = 0; i < m_n; ++i) {
            const float h = float(qDegreesToRadians(double(m_state[i * 4 + 3])));
            m_state[i * 4 + 0] += 0.01f * std::sin(h);
            m_state[i * 4 + 1] += 0.01f * std::cos(h);
            m_state[i * 4 + 3] += 0.5f;
        }
        QByteArray f(16 + m_n * 16, Qt::Uninitialized);
        const double t = double(QDateTime::currentMSecsSinceEpoch());
        const quint32 cnt = quint32(m_n);
        const quint32 seq = ++m_seq;
        std::memcpy(f.data(), &t, 8);
        std::memcpy(f.data() + 8, &cnt, 4);
        std::memcpy(f.data() + 12, &seq, 4);
        std::memcpy(f.data() + 16, m_state.constData(), size_t(m_n) * 16);
        m_latest = f;
    }

    QByteArray latest() const { return m_latest; }

private:
    int m_n;
    QVector<float> m_state;
    quint32 m_seq = 0;
    QByteArray m_latest;
};

// Modes "json" and "b64": frames pushed through QWebChannel signals.
class Bridge : public QObject
{
    Q_OBJECT
public:
    explicit Bridge(Sim *sim, QObject *parent = nullptr) : QObject(parent), m_sim(sim) {}

    Q_INVOKABLE void report(const QString &stats) { qInfo().noquote() << stats; }

    void push(const QString &mode)
    {
        const QByteArray f = m_sim->latest();
        if (mode == QLatin1String("json")) {
            double t;
            std::memcpy(&t, f.constData(), 8);
            const auto *v = reinterpret_cast<const float *>(f.constData() + 16);
            const int n = int((f.size() - 16) / 4);
            QVariantList list;
            list.reserve(n);
            for (int i = 0; i < n; ++i)
                list.append(v[i]);
            emit frameJson(t, list);
        } else {
            emit frameB64(QString::fromLatin1(f.toBase64()));
        }
    }

signals:
    void frameJson(double t, const QVariantList &data);
    void frameB64(const QString &b64);

private:
    Sim *m_sim;
};

// Mode "scheme": JS pulls raw bytes with fetch('sim://frame'), no JSON/base64.
class SimScheme : public QWebEngineUrlSchemeHandler
{
public:
    explicit SimScheme(Sim *sim) : m_sim(sim) {}

    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        auto *buf = new QBuffer(job);
        buf->setData(m_sim->latest());
        buf->open(QIODevice::ReadOnly);
        job->reply("application/octet-stream", buf);
    }

private:
    Sim *m_sim;
};

int main(int argc, char **argv)
{
    QWebEngineUrlScheme scheme("sim");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::CorsEnabled | QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme); // must precede QApplication

    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int n = qMax(1, args.value(1, QStringLiteral("5000")).toInt());
    const int hz = qBound(1, args.value(2, QStringLiteral("30")).toInt(), 240);
    const QString mode = args.value(3, QStringLiteral("scheme"));
    if (mode != QLatin1String("scheme") && mode != QLatin1String("b64")
        && mode != QLatin1String("json")) {
        qCritical("mode must be scheme, b64 or json");
        return 1;
    }

    Sim sim(n);
    sim.step();
    Bridge bridge(&sim);
    SimScheme handler(&sim);
    QWebEngineProfile::defaultProfile()->installUrlSchemeHandler("sim", &handler);

    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, [&] {
        sim.step();
        if (mode != QLatin1String("scheme"))
            bridge.push(mode);
    });
    tick.start(1000 / hz);

    QWebEngineView view;
    QWebChannel channel;
    channel.registerObject(QStringLiteral("bridge"), &bridge);
    view.page()->setWebChannel(&channel);
    view.setUrl(QUrl(QStringLiteral("qrc:/web/index.html?mode=%1&n=%2&hz=%3")
                         .arg(mode).arg(n).arg(hz)));
    view.resize(1600, 900);
    view.setWindowTitle(QStringLiteral("deckgl_bench %1 N=%2 %3Hz").arg(mode).arg(n).arg(hz));
    view.show();
    return app.exec();
}

#include "main.moc"
