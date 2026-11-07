#pragma once
#ifndef CESIUM_PROTO_HTTPSERVER_H
#define CESIUM_PROTO_HTTPSERVER_H

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

class QTcpSocket;

/// @brief Minimal HTTP/1.1 GET server on 127.0.0.1 for the prototype page, CesiumJS and data.
///
/// Every URL lives under a random per-run token (`http://127.0.0.1:<port>/<token>/...`), so
/// other local processes cannot guess it. Requests the handler does not claim are served as
/// static files from the web root, never outside it.
class HttpServer : public QObject
{
    Q_OBJECT
public:
    /// @brief Returns true when it answered the request, now or later (long poll).
    using Handler = std::function<bool(QTcpSocket *socket, const QStringList &path,
                                       const QUrlQuery &query)>;

    explicit HttpServer(QObject *parent = nullptr);

    bool listen(quint16 port = 0);
    QUrl baseUrl() const;
    void setStaticRoot(const QString &dir);
    void setHandler(Handler handler);

    static void reply(QTcpSocket *socket, int status, const QByteArray &contentType,
                      const QByteArray &body, bool cacheable = false,
                      const QByteArray &extraHeaders = QByteArray());

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void handleRequest(QTcpSocket *socket, const QByteArray &head);
    void serveStatic(QTcpSocket *socket, const QString &relativePath);

    QTcpServer m_server;
    QString m_token;
    QString m_root;
    Handler m_handler;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

#endif // CESIUM_PROTO_HTTPSERVER_H
