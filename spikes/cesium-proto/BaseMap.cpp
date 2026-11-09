#include "BaseMap.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSqlError>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace {

constexpr double kHalfWorld = 20037508.342789244; // Web Mercator, metres

template <typename T>
T readLE(const QByteArray &data, qsizetype offset)
{
    T v;
    std::memcpy(&v, data.constData() + offset, sizeof(T));
    return qFromLittleEndian(v);
}

qint32 readBE32(const QByteArray &data, qsizetype offset)
{
    qint32 v;
    std::memcpy(&v, data.constData() + offset, sizeof(v));
    return qFromBigEndian(v);
}

} // namespace

bool BordersMap::open(const QString &shapefile, QString *error)
{
    QFile f(shapefile);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(shapefile);
        return false;
    }
    const QByteArray data = f.readAll();
    if (data.size() < 100 || readBE32(data, 0) != 9994) {
        *error = QStringLiteral("%1 is not a shapefile").arg(shapefile);
        return false;
    }

    qsizetype pos = 100;
    while (pos + 8 <= data.size()) {
        const qsizetype length = qsizetype(readBE32(data, pos + 4)) * 2; // 16-bit words
        const qsizetype content = pos + 8;
        pos = content + length;
        if (pos > data.size() || length < 44)
            break;
        const qint32 type = readLE<qint32>(data, content);
        if (type != 5 && type != 15 && type != 25) // Polygon, PolygonZ, PolygonM
            continue;
        const double xmin = readLE<double>(data, content + 4), ymin = readLE<double>(data, content + 12);
        const double xmax = readLE<double>(data, content + 20), ymax = readLE<double>(data, content + 28);
        const qint32 parts = readLE<qint32>(data, content + 36);
        const qint32 points = readLE<qint32>(data, content + 40);
        const qsizetype partsAt = content + 44, pointsAt = partsAt + qsizetype(parts) * 4;
        if (parts <= 0 || points <= 0 || pointsAt + qsizetype(points) * 16 > pos)
            continue;

        Shape shape;
        shape.bounds = QRectF(xmin, ymin, xmax - xmin, ymax - ymin);
        for (qint32 p = 0; p < parts; ++p) {
            const qint32 first = readLE<qint32>(data, partsAt + p * 4);
            const qint32 last = p + 1 < parts ? readLE<qint32>(data, partsAt + (p + 1) * 4) : points;
            QPolygonF ring;
            ring.reserve(qMax(0, last - first));
            for (qint32 i = first; i < last; ++i)
                ring.append(QPointF(readLE<double>(data, pointsAt + qsizetype(i) * 16),
                                    readLE<double>(data, pointsAt + qsizetype(i) * 16 + 8)));
            shape.rings.append(ring);
        }
        m_shapes.push_back(std::move(shape));
    }
    if (m_shapes.empty()) {
        *error = QStringLiteral("%1 has no polygons").arg(shapefile);
        return false;
    }
    return true;
}

QByteArray BordersMap::tile(int z, int x, int y) const
{
    constexpr int kSize = 256;
    const double extent = 2.0 * kHalfWorld / double(1 << z);
    const double minX = -kHalfWorld + x * extent;
    const double maxY = kHalfWorld - y * extent;
    const double scale = kSize / extent;
    const double margin = extent * 0.02;
    const QRectF tileRect(minX - margin, maxY - extent - margin, extent + 2 * margin, extent + 2 * margin);

    QImage image(kSize, kSize, QImage::Format_RGB32);
    image.fill(QColor(0xa9, 0xd3, 0xf2)); // sea
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setTransform(QTransform(scale, 0, 0, -scale, -minX * scale, maxY * scale));
    QPen border(QColor(0xa8, 0x9b, 0xbd), z < 5 ? 0.8 : 1.3);
    border.setCosmetic(true);
    painter.setPen(border);
    painter.setBrush(QColor(0xf4, 0xf1, 0xea)); // land
    for (const Shape &shape : m_shapes) {
        if (!shape.bounds.intersects(tileRect))
            continue;
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (const QPolygonF &ring : shape.rings)
            path.addPolygon(ring);
        path.closeSubpath();
        painter.drawPath(path);
    }
    painter.end();

    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

MbTiles::~MbTiles()
{
    const QString name = m_db.connectionName();
    m_query = QSqlQuery();
    m_db = QSqlDatabase();
    if (!name.isEmpty())
        QSqlDatabase::removeDatabase(name);
}

bool MbTiles::open(const QString &file, QString *error)
{
    if (!QFile::exists(file)) {
        *error = QStringLiteral("%1 does not exist").arg(file);
        return false;
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        *error = QStringLiteral("Qt SQLite driver (qsqlite) not found; run windeployqt");
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("cesium_proto_mbtiles"));
    m_db.setDatabaseName(file);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!m_db.open()) {
        *error = m_db.lastError().text();
        return false;
    }
    m_meta = QJsonObject{ { "format", "png" }, { "minzoom", 0 }, { "maxzoom", 14 } };
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("select name, value from metadata"))) {
        while (q.next()) {
            const QString name = q.value(0).toString();
            const QString value = q.value(1).toString();
            if (name == QLatin1String("minzoom") || name == QLatin1String("maxzoom"))
                m_meta[name] = value.toInt();
            else if (name == QLatin1String("format") || name == QLatin1String("bounds")
                     || name == QLatin1String("center") || name == QLatin1String("name"))
                m_meta[name] = value;
        }
    }
    m_query = QSqlQuery(m_db);
    m_query.prepare(QStringLiteral("select tile_data from tiles where zoom_level = ? and tile_column = ? and tile_row = ?"));
    return true;
}

QByteArray MbTiles::tile(int z, int x, int y)
{
    m_query.bindValue(0, z);
    m_query.bindValue(1, x);
    m_query.bindValue(2, (1 << z) - 1 - y); // MBTiles rows are TMS (south up)
    if (m_query.exec() && m_query.next())
        return m_query.value(0).toByteArray();
    return {};
}
