#include "earthview3d/EarthFileCatalog.h"

#include "earthview3d/DtedMosaic.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QUrl>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

#include <algorithm>

namespace earthview3d {

namespace {
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

/// XML text/attribute escaping (names and URLs come from a file, so never trust them).
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
        else if (c == QLatin1Char('\''))
            out += QStringLiteral("&apos;");
        else
            out += c;
    }
    return out;
}

int readLevel(const QXmlStreamAttributes& attributes, const QStringList& names)
{
    for (const QString& name : names) {
        if (!attributes.hasAttribute(name))
            continue;
        bool ok = false;
        const int value = attributes.value(name).toString().toInt(&ok);
        if (ok)
            return value;
    }
    return -1;
}

/// Layer name when the file does not give one: the folder above tilemapresource.xml.
QString nameFromUrl(const QString& url)
{
    const QString path = QUrl(url).path();
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = parts.size() - 1; i >= 0; --i) {
        if (!parts.at(i).endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive))
            return parts.at(i);
    }
    return QStringLiteral("TMS layer");
}

bool isTmsDriver(const QString& driver)
{
    // As in the 2D reader: no driver means the file's own convention (TMS endpoints).
    return driver.isEmpty() || driver.compare(QStringLiteral("tms"), Qt::CaseInsensitive) == 0;
}

bool isDtedDriver(const QString& driver)
{
    return driver.compare(QStringLiteral("dted"), Qt::CaseInsensitive) == 0;
}

/// true when the content's first element is <map>, i.e. already a standard osgEarth file.
bool looksLikeStandardEarthFile(const QByteArray& content)
{
    QXmlStreamReader xml(content);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement())
            return xml.name().compare(QStringLiteral("map"), Qt::CaseInsensitive) == 0;
    }
    return false;
}

void makeNamesUnique(QList<TmsLayerSpec>* layers)
{
    QSet<QString> used;
    for (TmsLayerSpec& layer : *layers) {
        QString name = layer.name;
        int suffix = 2;
        while (used.contains(name))
            name = QStringLiteral("%1 (%2)").arg(layer.name).arg(suffix++);
        layer.name = name;
        used.insert(name);
    }
}
} // namespace

const char* EarthFileCatalog::layerPlaceholder()
{
    return "<!--@EARTHVIEW3D_TMS_LAYERS@-->";
}

const char* EarthFileCatalog::dtedMosaicFileName()
{
    return "dted_mosaic.vrt";
}

QString EarthFileCatalog::dtedMosaicUrl(const QString& path)
{
    QString trimmed = path.trimmed();
    while (trimmed.size() > 1
           && (trimmed.endsWith(QLatin1Char('/')) || trimmed.endsWith(QLatin1Char('\\'))))
        trimmed.chop(1);
    if (trimmed.isEmpty())
        return trimmed;

    const int slash = std::max(trimmed.lastIndexOf(QLatin1Char('/')), trimmed.lastIndexOf(QLatin1Char('\\')));
    const QString lastSegment = trimmed.mid(slash + 1);
    if (lastSegment.contains(QLatin1Char('.')))   // already a file (.vrt, .dt2, …)
        return trimmed;
    return trimmed + QLatin1Char('/') + QLatin1String(dtedMosaicFileName());
}

QStringList EarthFileCatalog::defaultSearchDirectories(const QString& dataRoot)
{
    const QDir root(dataRoot.isEmpty() ? QDir::currentPath() : dataRoot);
    QStringList directories;
    directories << root.absoluteFilePath(QStringLiteral("EarthFiles"))
                << root.absoluteFilePath(QStringLiteral("Data/EarthFiles"))
                << root.absolutePath();

    // The same folders the 2D viewer reads (MapSourceCatalog::earthSearchDirectories). A
    // deployment puts the executable beside Data/, so the data root usually covers these —
    // but not always, and a layer list the 2D map was happily using must not be invisible
    // here just because it sits next to the .exe instead (I-046).
    const QDir cwd(QDir::currentPath());
    const QDir appDir(QCoreApplication::applicationDirPath());
    directories << cwd.absoluteFilePath(QStringLiteral("Data/EarthFiles"))
                << cwd.absolutePath()
                << appDir.absoluteFilePath(QStringLiteral("Data/EarthFiles"))
                << appDir.absolutePath()
                << appDir.absoluteFilePath(QStringLiteral("../Data/EarthFiles"))
                << appDir.absoluteFilePath(QStringLiteral(".."));

    for (QString& directory : directories)
        directory = QDir::cleanPath(directory);
    directories.removeDuplicates();
    return directories;
}

QString EarthFileCatalog::labelForFile(const QFileInfo& fileInfo)
{
    // Same labels as the 2D source dropdown (MainWindow::sourceLabelForFile).
    const QString lowerName = fileInfo.completeBaseName().toLower();
    if (lowerName.contains(QStringLiteral("satellite")) || lowerName.contains(QStringLiteral("imagery")))
        return QStringLiteral("Satellite Imagery");
    if (lowerName.contains(QStringLiteral("cardg")) || lowerName.contains(QStringLiteral("topographic"))
        || lowerName.contains(QStringLiteral("topo")))
        return QStringLiteral("CardG Maps");

    QString label = fileInfo.completeBaseName();
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    return label;
}

bool EarthFileCatalog::parse(const QByteArray& content, MapCatalogEntry* entry, QString* error)
{
    if (!entry) {
        setError(error, QStringLiteral("No entry to fill"));
        return false;
    }
    entry->tmsLayers.clear();
    entry->standardEarthFile = looksLikeStandardEarthFile(content);
    if (entry->standardEarthFile)
        return true;

    // The 2D layer-list files have no root element; wrap them, exactly as the 2D reader does.
    QByteArray wrapped;
    wrapped.reserve(content.size() + 16);
    wrapped.append("<earth>\n").append(content).append("\n</earth>");

    QXmlStreamReader xml(wrapped);
    TmsLayerSpec current;
    bool insideLayer = false;
    bool skipCurrent = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString elementName = xml.name().toString().toLower();
            if (elementName == QStringLiteral("image") || elementName == QStringLiteral("elevation")
                || elementName == QStringLiteral("heightfield")) {
                const QXmlStreamAttributes attributes = xml.attributes();
                const QString driver = attributes.value(QStringLiteral("driver")).toString();
                const bool image = elementName == QStringLiteral("image");
                // DTED is an elevation-only driver: the offline server's .dt1/.dt2 folder.
                const bool dted = !image && isDtedDriver(driver);
                insideLayer = true;
                skipCurrent = !dted && !isTmsDriver(driver);
                current = TmsLayerSpec();
                current.kind = image ? TmsLayerSpec::Kind::Image
                                     : (dted ? TmsLayerSpec::Kind::DtedElevation : TmsLayerSpec::Kind::Elevation);
                current.name = attributes.value(QStringLiteral("name")).toString().trimmed();
                current.minLevel = readLevel(attributes, { QStringLiteral("min_level"), QStringLiteral("minLevel"),
                                                           QStringLiteral("min_zoom"), QStringLiteral("minZoom") });
                current.maxLevel = readLevel(attributes, { QStringLiteral("max_level"), QStringLiteral("maxLevel"),
                                                           QStringLiteral("max_zoom"), QStringLiteral("maxZoom") });
                current.vdatum = attributes.value(QStringLiteral("vdatum")).toString().trimmed();
            }
            else if (insideLayer) {
                const QString text = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                bool ok = false;
                const int value = text.toInt(&ok);
                if (elementName == QStringLiteral("url"))
                    current.url = text;
                else if (elementName == QStringLiteral("vdatum"))
                    current.vdatum = text;
                else if (ok && (elementName == QStringLiteral("min_level") || elementName == QStringLiteral("minlevel")
                                || elementName == QStringLiteral("min_zoom") || elementName == QStringLiteral("minzoom")))
                    current.minLevel = value;
                else if (ok && (elementName == QStringLiteral("max_level") || elementName == QStringLiteral("maxlevel")
                                || elementName == QStringLiteral("max_zoom") || elementName == QStringLiteral("maxzoom")))
                    current.maxLevel = value;
            }
        }
        else if (xml.isEndElement() && insideLayer) {
            const QString elementName = xml.name().toString().toLower();
            if (elementName == QStringLiteral("image") || elementName == QStringLiteral("elevation")
                || elementName == QStringLiteral("heightfield")) {
                if (!skipCurrent && !current.url.isEmpty()) {
                    if (current.name.isEmpty())
                        current.name = nameFromUrl(current.url);
                    entry->tmsLayers.append(current);
                }
                insideLayer = false;
                skipCurrent = false;
            }
        }
    }

    if (xml.hasError()) {
        setError(error, QStringLiteral("XML error at line %1: %2").arg(xml.lineNumber()).arg(xml.errorString()));
        return false;
    }
    if (entry->tmsLayers.isEmpty()) {
        setError(error, QStringLiteral("No TMS layer with a URL found"));
        return false;
    }
    makeNamesUnique(&entry->tmsLayers);
    return true;
}

bool EarthFileCatalog::parseFile(const QString& path, MapCatalogEntry* entry, QString* error)
{
    if (!entry) {
        setError(error, QStringLiteral("No entry to fill"));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(error, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray content = file.readAll();
    file.close();

    const QFileInfo info(path);
    entry->sourcePath = info.absoluteFilePath();
    entry->label = labelForFile(info);
    entry->earthFile.clear();
    if (!parse(content, entry, error))
        return false;
    if (entry->standardEarthFile)
        entry->earthFile = entry->sourcePath;
    return true;
}

QByteArray EarthFileCatalog::layersXml(const QList<TmsLayerSpec>& layers, int indent)
{
    const QString pad = QString(std::max(0, indent), QLatin1Char(' '));
    QString xml;
    for (const TmsLayerSpec& layer : layers) {
        const bool dted = layer.kind == TmsLayerSpec::Kind::DtedElevation;
        const bool elevation = dted || layer.kind == TmsLayerSpec::Kind::Elevation;
        const QString element = dted ? QStringLiteral("GDALElevation")
                                     : (elevation ? QStringLiteral("TMSElevation") : QStringLiteral("TMSImage"));
        const QString url = dted ? EarthFileCatalog::dtedMosaicUrl(layer.url) : layer.url;
        xml += QStringLiteral("%1<%2 name=\"%3\">\n").arg(pad, element, xmlEscaped(layer.name));
        xml += QStringLiteral("%1    <url>%2</url>\n").arg(pad, xmlEscaped(url));
        if (layer.minLevel >= 0)
            xml += QStringLiteral("%1    <min_level>%2</min_level>\n").arg(pad).arg(layer.minLevel);
        if (layer.maxLevel >= 0)
            xml += QStringLiteral("%1    <max_level>%2</max_level>\n").arg(pad).arg(layer.maxLevel);
        // DTED heights are orthometric (EGM96 mean sea level); osgEarth needs that said so it
        // converts them to the ellipsoid heights every other 3D position uses (D-021).
        const QString vdatum = (dted && layer.vdatum.isEmpty()) ? QStringLiteral("egm96") : layer.vdatum;
        if (elevation && !vdatum.isEmpty())
            xml += QStringLiteral("%1    <vdatum>%2</vdatum>\n").arg(pad, xmlEscaped(vdatum));
        xml += QStringLiteral("%1</%2>\n").arg(pad, element);
    }
    if (!xml.isEmpty())
        xml.chop(1);   // the placeholder's own line ends the block
    return xml.toUtf8();
}

QList<MapCatalogEntry> EarthFileCatalog::discover(const QStringList& directories, QStringList* warnings)
{
    QList<MapCatalogEntry> entries;
    QSet<QString> seen;

    for (const QString& directoryPath : directories) {
        const QDir directory(directoryPath);
        if (!directory.exists())
            continue;
        const QFileInfoList files =
            directory.entryInfoList({ QStringLiteral("*.earth") }, QDir::Files | QDir::Readable, QDir::Name);
        // The distribution ships the layer lists as *.earth.sample, so that a machine without a
        // tile server gets no half-configured map. A folder holding only those is a machine
        // where they were never copied to *.earth, which looks exactly like "no maps found"
        // unless it is said out loud (I-046).
        if (files.isEmpty()
            && !directory.entryInfoList({ QStringLiteral("*.earth.sample") }, QDir::Files).isEmpty()) {
            addWarning(warnings,
                       QStringLiteral("%1 holds only *.earth.sample templates; copy one to *.earth "
                                      "(and put your tile server's URL in it) for it to be offered as a map")
                           .arg(directory.absolutePath()));
        }
        for (const QFileInfo& file : files) {
            const QString absolutePath = file.absoluteFilePath();
            if (seen.contains(absolutePath))
                continue;
            seen.insert(absolutePath);

            MapCatalogEntry entry;
            QString error;
            if (!parseFile(absolutePath, &entry, &error)) {
                addWarning(warnings, QStringLiteral("Skipped %1: %2").arg(absolutePath, error));
                continue;
            }
            entries.append(entry);
        }
    }

    std::stable_sort(entries.begin(), entries.end(), [](const MapCatalogEntry& a, const MapCatalogEntry& b) {
        return a.label.localeAwareCompare(b.label) < 0;
    });
    return entries;
}

QString EarthFileCatalog::generatedFileName(const MapCatalogEntry& entry)
{
    QString base = QFileInfo(entry.sourcePath).completeBaseName();
    // Keep the name a safe file name on every platform.
    for (QChar& c : base) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    }
    if (base.isEmpty())
        base = QStringLiteral("map");
    return QStringLiteral("tms_%1.earth").arg(base);
}

bool EarthFileCatalog::generate(MapCatalogEntry* entry, const QString& templatePath, const QString& outputDir,
                                QString* error, QStringList* warnings)
{
    if (!entry) {
        setError(error, QStringLiteral("No entry to generate"));
        return false;
    }
    if (entry->standardEarthFile) {
        entry->earthFile = entry->sourcePath;
        return true;
    }
    if (entry->tmsLayers.isEmpty()) {
        setError(error, QStringLiteral("Entry %1 has no TMS layer").arg(entry->label));
        return false;
    }

    QFile templateFile(templatePath);
    if (!templateFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(error, QStringLiteral("Cannot read template %1: %2").arg(templatePath, templateFile.errorString()));
        return false;
    }
    const QByteArray templateContent = templateFile.readAll();
    templateFile.close();

    const QByteArray placeholder(layerPlaceholder());
    if (!templateContent.contains(placeholder)) {
        setError(error, QStringLiteral("Template %1 has no %2 placeholder")
                            .arg(templatePath, QString::fromLatin1(placeholder)));
        return false;
    }

    const QDir targetDir(outputDir.isEmpty() ? QFileInfo(templatePath).absolutePath() : outputDir);
    if (!targetDir.exists() && !QDir().mkpath(targetDir.absolutePath())) {
        setError(error, QStringLiteral("Cannot create %1").arg(targetDir.absolutePath()));
        return false;
    }

    // A DTED entry names the folder the elevation server holds (DTED2/e066/n32.dt2, …), but
    // GDAL opens a dataset, not a directory, so index the folder here and write the mosaic's
    // path into the generated map (I-042). The mosaic lands in the DTED folder, or beside this
    // file when that folder is read-only. If it cannot be built the folder path stays, and
    // layersXml() appends the default mosaic name — the file a run of
    // `build_offline_data.py dted-vrt` would have left there.
    QList<TmsLayerSpec> layers = entry->tmsLayers;
    for (TmsLayerSpec& layer : layers) {
        if (layer.kind != TmsLayerSpec::Kind::DtedElevation)
            continue;
        QString mosaicError;
        const QString mosaic = DtedMosaic::ensureMosaic(layer.url, targetDir.absolutePath(),
                                                        &mosaicError, warnings);
        if (mosaic.isEmpty())
            addWarning(warnings, QStringLiteral("DTED layer \"%1\": %2").arg(layer.name, mosaicError));
        else
            layer.url = mosaic;
    }

    QByteArray generated = templateContent;
    const QByteArray header = QStringLiteral("<!-- Generated by earthview3d from %1 — edit that file, not this one. -->\n")
                                  .arg(QFileInfo(entry->sourcePath).fileName())
                                  .toUtf8();
    generated.replace(placeholder, header + layersXml(layers));
    const QString outPath = targetDir.absoluteFilePath(generatedFileName(*entry));
    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(outPath, outFile.errorString()));
        return false;
    }
    if (outFile.write(generated) != generated.size()) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(outPath, outFile.errorString()));
        return false;
    }
    outFile.close();

    entry->earthFile = QDir::cleanPath(outPath);
    return true;
}

QList<MapCatalogEntry> EarthFileCatalog::build(const QStringList& directories, const QString& templatePath,
                                               const QString& outputDir, QStringList* warnings)
{
    QList<MapCatalogEntry> entries = discover(directories, warnings);
    for (int i = entries.size() - 1; i >= 0; --i) {
        QString error;
        if (!generate(&entries[i], templatePath, outputDir, &error, warnings)) {
            addWarning(warnings, QStringLiteral("Skipped %1: %2").arg(entries.at(i).sourcePath, error));
            entries.removeAt(i);
        }
    }
    return entries;
}

} // namespace earthview3d
