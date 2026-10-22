#include "HttpServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpSocket>
#include <QUuid>

namespace {

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default: return "Internal Server Error";
    }
}

QByteArray contentTypeFor(const QString &suffix)
{
    static const QHash<QString, QByteArray> types = {
        { "html", "text/html; charset=utf-8" },  { "js", "text/javascript; charset=utf-8" },
        { "mjs", "text/javascript; charset=utf-8" }, { "css", "text/css; charset=utf-8" },
        { "json", "application/json" },          { "png", "image/png" },
        { "jpg", "image/jpeg" },                 { "jpeg", "image/jpeg" },
        { "gif", "image/gif" },                  { "svg", "image/svg+xml" },
        { "wasm", "application/wasm" },          { "glb", "model/gltf-binary" },
        { "gltf", "model/gltf+json" },           { "xml", "application/xml" },
        { "ktx2", "image/ktx2" },                { "woff", "font/woff" },
        { "woff2", "font/woff2" },               { "txt", "text/plain; charset=utf-8" },
    };
    return types.value(suffix.toLower(), "application/octet-stream");
}

} // namespace

HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
    , m_token(QUuid::createUuid().toString(QUuid::Id128).left(16))
{
    connect(&m_server, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
}

bool HttpServer::listen(quint16 port)
{
    return m_server.listen(QHostAddress::LocalHost, port);
}

QUrl HttpServer::baseUrl() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/%2/").arg(m_server.serverPort()).arg(m_token));
}

void HttpServer::setStaticRoot(const QString &dir)
{
    m_root = QFileInfo(dir).canonicalFilePath();
}

void HttpServer::setHandler(Handler handler)
{
    m_handler = std::move(handler);
}

void HttpServer::reply(QTcpSocket *socket, int status, const QByteArray &contentType,
                       const QByteArray &body, bool cacheable, const QByteArray &extraHeaders)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasonPhrase(status) + "\r\n";
    head += "Content-Type: " + contentType + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += cacheable ? "Cache-Control: max-age=86400\r\n" : "Cache-Control: no-store\r\n";
    head += extraHeaders;
    head += "Connection: keep-alive\r\n\r\n";
    socket->write(head);
    socket->write(body);
}

void HttpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void HttpServer::onReadyRead(QTcpSocket *socket)
{
    m_buffers[socket] += socket->readAll();
    while (socket->state() == QAbstractSocket::ConnectedState) {
        QByteArray &buffer = m_buffers[socket];
        const int end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (buffer.size() > 64 * 1024)
                socket->abort();
            return;
        }
        const QByteArray head = buffer.left(end);
        buffer.remove(0, end + 4);
        handleRequest(socket, head);
    }
}

void HttpServer::handleRequest(QTcpSocket *socket, const QByteArray &head)
{
    const int lineEnd = head.indexOf("\r\n");
    const QList<QByteArray> requestLine = (lineEnd < 0 ? head : head.left(lineEnd)).split(' ');
    if (requestLine.size() < 3) {
        reply(socket, 400, "text/plain", "bad request");
        return;
    }
    if (requestLine[0] != "GET") {
        reply(socket, 405, "text/plain", "GET only");
        return;
    }

    const QByteArray target = requestLine[1];
    const int q = target.indexOf('?');
    const QString path = QUrl::fromPercentEncoding(q < 0 ? target : target.left(q));
    const QUrlQuery query(q < 0 ? QString() : QString::fromUtf8(target.mid(q + 1)));

    const QString prefix = QLatin1Char('/') + m_token + QLatin1Char('/');
    if (!path.startsWith(prefix)) {
        reply(socket, 404, "text/plain", "not found");
        return;
    }
    const QString relative = path.mid(prefix.size());
    const QStringList segments = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.contains(QStringLiteral("..")) || relative.contains(QLatin1Char('\\'))) {
        reply(socket, 403, "text/plain", "forbidden");
        return;
    }
    if (m_handler && m_handler(socket, segments, query))
        return;
    serveStatic(socket, relative);
}

void HttpServer::serveStatic(QTcpSocket *socket, const QString &relativePath)
{
    const QString file = relativePath.isEmpty() ? QStringLiteral("index.html") : relativePath;
    const QFileInfo info(QDir(m_root).filePath(file));
    const QString canonical = info.canonicalFilePath();
    if (m_root.isEmpty() || canonical.isEmpty() || !info.isFile()
        || !canonical.startsWith(m_root + QLatin1Char('/'))) {
        reply(socket, 404, "text/plain", "not found");
        return;
    }
    QFile f(canonical);
    if (!f.open(QIODevice::ReadOnly)) {
        reply(socket, 404, "text/plain", "not found");
        return;
    }
    reply(socket, 200, contentTypeFor(info.suffix()), f.readAll(),
          relativePath.startsWith(QStringLiteral("cesium/")));
}
