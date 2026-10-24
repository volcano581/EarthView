#include "earthview3d/DtedMosaic.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace earthview3d {

namespace {

constexpr int kUhlSize = 80;          // User Header Label, the first record of every DTED file
constexpr int kVoidValue = -32767;    // MIL-PRF-89020B: void post
constexpr double kTenthsOfArcSecondPerDegree = 36000.0;
// A mosaic wider than this is a mistake (a stray cell on the far side of the world), not data:
// worldwide DTED2 is 1,296,000 x 648,000 pixels, so this leaves plenty of room.
constexpr long long kMaxMosaicPixels = 4000000;

const char* kWgs84Wkt =
    "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563,"
    "AUTHORITY[\"EPSG\",\"7030\"]],AUTHORITY[\"EPSG\",\"6326\"]],"
    "PRIMEM[\"Greenwich\",0,AUTHORITY[\"EPSG\",\"8901\"]],"
    "UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],AUTHORITY[\"EPSG\",\"4326\"]]";

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

void addWarning(QStringList* warnings, const QString& message)
{
    if (warnings)
        warnings->append(message);
}

bool isDtedFile(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return suffix == QLatin1String("dt0") || suffix == QLatin1String("dt1")
        || suffix == QLatin1String("dt2");
}

/// "0660000E" / "0320000N" → signed degrees. Fractional seconds are accepted (the DSI record
/// uses them), although the UHL itself is whole seconds.
bool parseAngle(const QByteArray& field, double* degrees)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d{1,3})(\\d{2})(\\d{2}(?:\\.\\d+)?)([NSEWnsew])$"));
    const QRegularExpressionMatch match = pattern.match(QString::fromLatin1(field).trimmed());
    if (!match.hasMatch())
        return false;

    const double value = match.captured(1).toDouble() + match.captured(2).toDouble() / 60.0
        + match.captured(3).toDouble() / 3600.0;
    const QChar hemisphere = match.captured(4).at(0).toUpper();
    *degrees = (hemisphere == QLatin1Char('S') || hemisphere == QLatin1Char('W')) ? -value : value;
    return true;
}

bool parseCount(const QByteArray& field, int* value)
{
    bool ok = false;
    const int parsed = QString::fromLatin1(field).trimmed().toInt(&ok);
    if (!ok || parsed <= 0)
        return false;
    *value = parsed;
    return true;
}

/// Degrees per post from a field in tenths of an arc-second.
bool parseInterval(const QByteArray& field, double* degrees)
{
    int tenths = 0;
    if (!parseCount(field, &tenths))
        return false;
    *degrees = tenths / kTenthsOfArcSecondPerDegree;
    return true;
}

/// Path to write into the mosaic, relative to it when that survives the folder being copied.
/// @p isRelative says which it is (GDAL needs to be told).
QString sourcePathFor(const QDir& vrtDir, const QString& filePath, bool* isRelative)
{
    const QString relative = vrtDir.relativeFilePath(filePath);
    // Only stay relative while the file is under the mosaic's own folder: a "../.." chain to
    // another drive does not survive the folder being copied, which is the point of relative.
    *isRelative = !relative.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(relative);
    return *isRelative ? relative : filePath;   // QFileInfo paths already use '/'
}

QString xmlEscaped(const QString& text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        if (c == QLatin1Char('&'))
            out += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<'))
            out += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>'))
            out += QStringLiteral("&gt;");
        else if (c == QLatin1Char('"'))
            out += QStringLiteral("&quot;");
        else
            out += c;
    }
    return out;
}

QString number(double value)
{
    return QString::number(value, 'g', 17);
}

} // namespace

const char* DtedMosaic::defaultFileName()
{
    return "dted_mosaic.vrt";
}

int DtedMosaic::voidValue()
{
    return kVoidValue;
}

bool DtedMosaic::readHeader(const QString& file, DtedTile* tile, QString* error)
{
    if (!tile) {
        setError(error, QStringLiteral("No tile to fill"));
        return false;
    }
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot read %1: %2").arg(file, source.errorString()));
        return false;
    }
    const QByteArray uhl = source.read(kUhlSize);
    source.close();
    if (uhl.size() < kUhlSize) {
        setError(error, QStringLiteral("%1 is too short to be DTED").arg(file));
        return false;
    }
    if (!uhl.startsWith("UHL")) {
        setError(error, QStringLiteral("%1 does not start with a DTED user header label").arg(file));
        return false;
    }

    DtedTile parsed;
    parsed.path = QFileInfo(file).absoluteFilePath();
    //  4..11 longitude of origin   20..23 longitude interval   47..50 longitude lines
    // 12..19 latitude of origin    24..27 latitude interval    51..54 latitude posts per line
    if (!parseAngle(uhl.mid(4, 8), &parsed.westLonDeg) || !parseAngle(uhl.mid(12, 8), &parsed.southLatDeg)
        || !parseInterval(uhl.mid(20, 4), &parsed.lonIntervalDeg)
        || !parseInterval(uhl.mid(24, 4), &parsed.latIntervalDeg)
        || !parseCount(uhl.mid(47, 4), &parsed.columns) || !parseCount(uhl.mid(51, 4), &parsed.rows)) {
        setError(error, QStringLiteral("%1 has an unreadable DTED user header label").arg(file));
        return false;
    }

    *tile = parsed;
    return true;
}

QList<DtedTile> DtedMosaic::scan(const QString& folder, QStringList* warnings)
{
    QList<DtedTile> tiles;
    QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!isDtedFile(path))
            continue;
        DtedTile tile;
        QString error;
        if (!readHeader(path, &tile, &error)) {
            addWarning(warnings, error);
            continue;
        }
        tiles.append(tile);
    }

    std::sort(tiles.begin(), tiles.end(), [](const DtedTile& a, const DtedTile& b) {
        if (a.southLatDeg != b.southLatDeg)
            return a.southLatDeg < b.southLatDeg;
        if (a.westLonDeg != b.westLonDeg)
            return a.westLonDeg < b.westLonDeg;
        return a.path < b.path;
    });
    return tiles;
}

QByteArray DtedMosaic::buildVrt(const QList<DtedTile>& tiles, const QString& vrtPath, QString* error)
{
    if (tiles.isEmpty()) {
        setError(error, QStringLiteral("No DTED cells to index"));
        return {};
    }

    // The mosaic grid: the finest posting present, anchored at the north-west corner of the
    // whole set, so every cell lands on it at a whole number of pixels.
    double pixelX = tiles.first().lonIntervalDeg;
    double pixelY = tiles.first().latIntervalDeg;
    double originX = tiles.first().originLonDeg();
    double originY = tiles.first().originLatDeg();
    double eastX = originX;
    double southY = originY;
    for (const DtedTile& tile : tiles) {
        if (!tile.isValid()) {
            setError(error, QStringLiteral("%1 has an empty DTED grid").arg(tile.path));
            return {};
        }
        pixelX = std::min(pixelX, tile.lonIntervalDeg);
        pixelY = std::min(pixelY, tile.latIntervalDeg);
        originX = std::min(originX, tile.originLonDeg());
        originY = std::max(originY, tile.originLatDeg());
        eastX = std::max(eastX, tile.originLonDeg() + tile.columns * tile.lonIntervalDeg);
        southY = std::min(southY, tile.originLatDeg() - tile.rows * tile.latIntervalDeg);
    }

    const long long width = std::llround((eastX - originX) / pixelX);
    const long long height = std::llround((originY - southY) / pixelY);
    if (width <= 0 || height <= 0) {
        setError(error, QStringLiteral("DTED cells produce an empty mosaic"));
        return {};
    }
    if (width > kMaxMosaicPixels || height > kMaxMosaicPixels) {
        setError(error, QStringLiteral("DTED cells span %1 x %2 pixels, which is not one area; "
                                       "check for a stray file far from the rest")
                            .arg(width).arg(height));
        return {};
    }

    const QDir vrtDir(QFileInfo(vrtPath).absolutePath());
    QString xml;
    xml += QStringLiteral("<!-- DTED mosaic written by earthview3d from %1 cell(s). Regenerated\n"
                          "     whenever a cell is newer than this file; safe to delete. -->\n")
               .arg(tiles.size());
    xml += QStringLiteral("<VRTDataset rasterXSize=\"%1\" rasterYSize=\"%2\">\n").arg(width).arg(height);
    xml += QStringLiteral("  <SRS>%1</SRS>\n").arg(QString::fromLatin1(kWgs84Wkt));
    xml += QStringLiteral("  <GeoTransform>%1, %2, 0, %3, 0, %4</GeoTransform>\n")
               .arg(number(originX), number(pixelX), number(originY), number(-pixelY));
    xml += QStringLiteral("  <VRTRasterBand dataType=\"Int16\" band=\"1\">\n");
    xml += QStringLiteral("    <NoDataValue>%1</NoDataValue>\n").arg(kVoidValue);
    xml += QStringLiteral("    <ColorInterp>Gray</ColorInterp>\n");

    for (const DtedTile& tile : tiles) {
        const long long xOff = std::llround((tile.originLonDeg() - originX) / pixelX);
        const long long yOff = std::llround((originY - tile.originLatDeg()) / pixelY);
        const long long xSize = std::llround(tile.columns * tile.lonIntervalDeg / pixelX);
        const long long ySize = std::llround(tile.rows * tile.latIntervalDeg / pixelY);
        bool relative = false;
        const QString sourcePath = sourcePathFor(vrtDir, tile.path, &relative);

        xml += QStringLiteral("    <ComplexSource>\n");
        xml += QStringLiteral("      <SourceFilename relativeToVRT=\"%1\">%2</SourceFilename>\n")
                   .arg(relative ? 1 : 0)
                   .arg(xmlEscaped(sourcePath));
        xml += QStringLiteral("      <SourceBand>1</SourceBand>\n");
        xml += QStringLiteral("      <SourceProperties RasterXSize=\"%1\" RasterYSize=\"%2\" DataType=\"Int16\"/>\n")
                   .arg(tile.columns).arg(tile.rows);
        xml += QStringLiteral("      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"%1\" ySize=\"%2\"/>\n")
                   .arg(tile.columns).arg(tile.rows);
        xml += QStringLiteral("      <DstRect xOff=\"%1\" yOff=\"%2\" xSize=\"%3\" ySize=\"%4\"/>\n")
                   .arg(xOff).arg(yOff).arg(xSize).arg(ySize);
        xml += QStringLiteral("      <NODATA>%1</NODATA>\n").arg(kVoidValue);
        xml += QStringLiteral("    </ComplexSource>\n");
    }

    xml += QStringLiteral("  </VRTRasterBand>\n");
    xml += QStringLiteral("</VRTDataset>\n");
    return xml.toUtf8();
}

bool DtedMosaic::write(const QList<DtedTile>& tiles, const QString& vrtPath, QString* error)
{
    const QByteArray xml = buildVrt(tiles, vrtPath, error);
    if (xml.isEmpty())
        return false;

    const QDir dir(QFileInfo(vrtPath).absolutePath());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        setError(error, QStringLiteral("Cannot create %1").arg(dir.absolutePath()));
        return false;
    }
    // QSaveFile: a half-written mosaic beside live data would be worse than none.
    QSaveFile file(vrtPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(vrtPath, file.errorString()));
        return false;
    }
    if (file.write(xml) != xml.size() || !file.commit()) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(vrtPath, file.errorString()));
        return false;
    }
    return true;
}

QString DtedMosaic::ensureMosaic(const QString& path, const QString& fallbackDir, QString* error,
                                 QStringList* warnings)
{
    QString trimmed = path.trimmed();
    while (trimmed.size() > 1
           && (trimmed.endsWith(QLatin1Char('/')) || trimmed.endsWith(QLatin1Char('\\'))))
        trimmed.chop(1);
    if (trimmed.isEmpty()) {
        setError(error, QStringLiteral("No DTED path given"));
        return {};
    }

    const QFileInfo info(trimmed);
    if (info.isFile())
        return info.absoluteFilePath();     // already a dataset GDAL can open
    if (!info.isDir()) {
        setError(error, QStringLiteral("DTED path %1 is neither a file nor a folder").arg(trimmed));
        return {};
    }

    const QList<DtedTile> tiles = scan(info.absoluteFilePath(), warnings);
    if (tiles.isEmpty()) {
        setError(error, QStringLiteral("No .dt0/.dt1/.dt2 cells under %1").arg(info.absoluteFilePath()));
        return {};
    }

    QString vrtPath = QDir(info.absoluteFilePath()).absoluteFilePath(QLatin1String(defaultFileName()));
    const QFileInfo vrtInfo(vrtPath);
    if (vrtInfo.exists()) {
        // Rebuild only when a cell changed: a theatre holds thousands of files.
        QDateTime newest;
        for (const DtedTile& tile : tiles) {
            const QDateTime modified = QFileInfo(tile.path).lastModified();
            if (!newest.isValid() || modified > newest)
                newest = modified;
        }
        if (newest.isValid() && vrtInfo.lastModified() >= newest)
            return vrtInfo.absoluteFilePath();
    }

    QString writeError;
    if (write(tiles, vrtPath, &writeError))
        return QFileInfo(vrtPath).absoluteFilePath();

    if (fallbackDir.isEmpty()) {
        setError(error, writeError);
        return {};
    }
    // Read-only folder (a share from the elevation server): keep the mosaic elsewhere, named
    // after the folder it indexes so two folders never collide.
    addWarning(warnings, QStringLiteral("%1; writing the mosaic to %2 instead").arg(writeError, fallbackDir));
    const QByteArray key = QCryptographicHash::hash(info.absoluteFilePath().toUtf8(),
                                                    QCryptographicHash::Sha1).toHex().left(12);
    vrtPath = QDir(fallbackDir).absoluteFilePath(QStringLiteral("dted_%1.vrt").arg(QString::fromLatin1(key)));
    if (!write(tiles, vrtPath, error))
        return {};
    return QFileInfo(vrtPath).absoluteFilePath();
}

} // namespace earthview3d
