#include "Dted.h"

#include <QBuffer>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QtMath>

#include <cmath>
#include <cstring>

namespace {

constexpr qint64 kDataOffset = 80 + 648 + 2700; // UHL + DSI + ACC records
constexpr double kMetresPerDegree = 111320.0;

qint32 cellKey(int lonDegree, int latDegree)
{
    return (latDegree + 90) * 360 + (lonDegree + 180);
}

// DDDMMSSH, H = N/S/E/W.
double parseAngle(const char *field)
{
    const QByteArray f(field, 8);
    const double value = f.left(3).toDouble() + f.mid(3, 2).toDouble() / 60.0
        + f.mid(5, 2).toDouble() / 3600.0;
    return (f.at(7) == 'W' || f.at(7) == 'S') ? -value : value;
}

double tileLon(int z, double fx)
{
    return fx / double(1 << z) * 360.0 - 180.0;
}

double tileLat(int z, double fy)
{
    return qRadiansToDegrees(std::atan(std::sinh(M_PI * (1.0 - 2.0 * fy / double(1 << z)))));
}

QByteArray encodePng(const QImage &image)
{
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

} // namespace

struct DtedStore::Cell
{
    std::unique_ptr<QFile> file;
    const uchar *data = nullptr;
    double lon0 = 0.0, lat0 = 0.0; // south-west post
    double dLon = 0.0, dLat = 0.0; // post spacing, degrees
    int nLon = 0, nLat = 0;        // longitude lines (records), latitude points per record
    qint64 recordLength = 0;

    float post(int column, int row) const
    {
        const uchar *p = data + kDataOffset + column * recordLength + 8 + row * 2;
        int v = (p[0] << 8) | p[1];
        if (v & 0x8000)
            v = -(v & 0x7FFF); // signed magnitude
        return v <= -32767 ? 0.f : float(v);
    }
};

DtedStore::DtedStore() = default;
DtedStore::~DtedStore() = default;

int DtedStore::open(const QString &dir)
{
    QDirIterator it(dir, { "*.dt0", "*.dt1", "*.dt2" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        addFile(it.next());
    return cellCount();
}

bool DtedStore::addFile(const QString &path)
{
    auto cell = std::make_unique<Cell>();
    cell->file = std::make_unique<QFile>(path);
    if (!cell->file->open(QIODevice::ReadOnly))
        return false;
    const qint64 size = cell->file->size();
    if (size < kDataOffset + 1)
        return false;
    cell->data = cell->file->map(0, size);
    if (!cell->data)
        return false;

    const char *uhl = reinterpret_cast<const char *>(cell->data);
    if (std::memcmp(uhl, "UHL", 3) != 0)
        return false;
    cell->lon0 = parseAngle(uhl + 4);
    cell->lat0 = parseAngle(uhl + 12);
    cell->dLon = QByteArray(uhl + 20, 4).toDouble() / 36000.0; // tenths of arc seconds
    cell->dLat = QByteArray(uhl + 24, 4).toDouble() / 36000.0;
    cell->nLon = QByteArray(uhl + 47, 4).toInt();
    cell->nLat = QByteArray(uhl + 51, 4).toInt();
    cell->recordLength = 12 + 2 * qint64(cell->nLat);
    if (cell->nLon < 2 || cell->nLat < 2 || cell->dLon <= 0.0 || cell->dLat <= 0.0
        || size < kDataOffset + cell->nLon * cell->recordLength || cell->data[kDataOffset] != 0xAA)
        return false;

    const qint32 key = cellKey(int(std::floor(cell->lon0 + 1e-9)), int(std::floor(cell->lat0 + 1e-9)));
    if (const Cell *existing = m_index.value(key); existing && existing->nLat >= cell->nLat)
        return false; // keep the finer level of duplicate cells

    const QRectF extent(cell->lon0, cell->lat0, (cell->nLon - 1) * cell->dLon, (cell->nLat - 1) * cell->dLat);
    m_bounds = m_bounds.isNull() ? extent : m_bounds.united(extent);
    m_index.insert(key, cell.get());
    m_cells.push_back(std::move(cell));
    return true;
}

const DtedStore::Cell *DtedStore::cellAt(int lonDegree, int latDegree) const
{
    return m_index.value(cellKey(lonDegree, latDegree), nullptr);
}

bool DtedStore::height(double lon, double lat, double *metres) const
{
    const Cell *c = cellAt(int(std::floor(lon)), int(std::floor(lat)));
    if (!c)
        return false;
    const double col = qBound(0.0, (lon - c->lon0) / c->dLon, double(c->nLon - 1));
    const double row = qBound(0.0, (lat - c->lat0) / c->dLat, double(c->nLat - 1));
    const int c0 = qMin(int(col), c->nLon - 2);
    const int r0 = qMin(int(row), c->nLat - 2);
    const double fx = col - c0, fy = row - r0;
    const double south = c->post(c0, r0) * (1.0 - fx) + c->post(c0 + 1, r0) * fx;
    const double north = c->post(c0, r0 + 1) * (1.0 - fx) + c->post(c0 + 1, r0 + 1) * fx;
    *metres = south * (1.0 - fy) + north * fy;
    return true;
}

QByteArray DtedStore::heightmapTile(int level, int x, int y, int size) const
{
    QByteArray out(size * size * int(sizeof(float)), '\0');
    const double span = 180.0 / double(1 << level);
    const double west = -180.0 + x * span;
    const double north = 90.0 - y * span;
    if (!m_bounds.intersects(QRectF(west, north - span, span, span)))
        return out;

    auto *heights = reinterpret_cast<float *>(out.data());
    const double step = span / double(size - 1);
    for (int row = 0; row < size; ++row) {
        const double lat = north - row * step;
        for (int col = 0; col < size; ++col) {
            double h = 0.0;
            if (height(west + col * step, lat, &h))
                heights[row * size + col] = float(h);
        }
    }
    return out;
}

QByteArray DtedStore::hillshadeTile(int z, int x, int y) const
{
    static const QByteArray empty = [] {
        QImage blank(1, 1, QImage::Format_ARGB32);
        blank.fill(Qt::transparent);
        return encodePng(blank);
    }();

    constexpr int kSize = 256;
    const QRectF tileRect(tileLon(z, x), tileLat(z, y + 1), tileLon(z, x + 1) - tileLon(z, x),
                          tileLat(z, y) - tileLat(z, y + 1));
    if (!m_bounds.intersects(tileRect))
        return empty;

    // Sun from the north-west, 45 degrees up; x east, y north, z up.
    const double lightX = -0.5, lightY = 0.5, lightZ = M_SQRT1_2;
    const double flat = lightZ;
    const double exaggeration = z < 10 ? 2.0 : 1.4;

    QImage image(kSize, kSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    bool any = false;
    for (int py = 0; py < kSize; ++py) {
        const double lat = tileLat(z, y + (py + 0.5) / kSize);
        const double metresPerPixel = 2.0 * M_PI * 6378137.0 * std::cos(qDegreesToRadians(lat)) / (kSize * double(1 << z));
        const double s = qMax(metresPerPixel, 30.0);
        const double dLat = s / kMetresPerDegree;
        const double dLon = s / (kMetresPerDegree * std::cos(qDegreesToRadians(lat)));
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(py));
        for (int px = 0; px < kSize; ++px) {
            const double lon = tileLon(z, x + (px + 0.5) / kSize);
            double hE, hW, hN, hS;
            if (!height(lon + dLon, lat, &hE) || !height(lon - dLon, lat, &hW)
                || !height(lon, lat + dLat, &hN) || !height(lon, lat - dLat, &hS))
                continue;
            const double gx = (hE - hW) / (2.0 * s) * exaggeration;
            const double gy = (hN - hS) / (2.0 * s) * exaggeration;
            const double shade = qMax(0.0, (-gx * lightX - gy * lightY + lightZ) / std::sqrt(gx * gx + gy * gy + 1.0));
            if (shade < flat)
                line[px] = qRgba(20, 24, 28, int(qMin(1.0, (flat - shade) / flat * 1.3) * 190));
            else
                line[px] = qRgba(255, 255, 255, int((shade - flat) / (1.0 - flat) * 110));
            any = true;
        }
    }
    return any ? encodePng(image) : empty;
}
