#include "MainWindow.h"
#include "MapWidget.h"
#include "Camera.h"
#include "MercatorProjection.h"
#include <QAction>
#include <QBuffer>
#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace {
struct EarthSource {
    QString label;
    QString filePath;
    QList<TmsLoader::TileSourceLayer> layers;
};

struct TileMapResourceMetadata {
    bool valid = false;
    QString title;
    QString extension = "png";
    int minTileZoom = -1;
    int maxTileZoom = -1;
    bool tmsYOrigin = true;
};

bool hasTilePlaceholders(const QString& value)
{
    return value.contains("{z}") && value.contains("{x}") && value.contains("{y}");
}

QString normalizedText(QString value)
{
    value = value.toLower();
    QString normalized;
    normalized.reserve(value.size());
    for (const QChar& ch : value) {
        if (ch.isLetterOrNumber()) {
            normalized.append(ch);
        }
    }
    return normalized;
}

QString normalizedExtension(QString extension)
{
    extension = extension.trimmed();
    if (extension.startsWith('.')) {
        extension.remove(0, 1);
    }
    return extension.isEmpty() ? QString("png") : extension;
}

QString replaceLiteralTileSegments(QString value)
{
    const QStringList suffixes = {
        "/z/x/y.png",
        "/z/x/y.jpg",
        "/z/x/y.jpeg"
    };

    for (const QString& suffix : suffixes) {
        if (value.endsWith(suffix, Qt::CaseInsensitive)) {
            value.chop(suffix.size());
            const int extensionIndex = suffix.lastIndexOf('.');
            const QString extension = extensionIndex >= 0 ? suffix.mid(extensionIndex) : QString(".png");
            return value + "/{z}/{x}/{y}" + extension;
        }
    }

    return value;
}

QString tileTemplateFromUrl(QString value, const QString& extension = "png")
{
    const QString outputExtension = normalizedExtension(extension);
    value = replaceLiteralTileSegments(value.trimmed());
    if (hasTilePlaceholders(value)) {
        return value;
    }

    if (value.endsWith("/tilemapresource.xml", Qt::CaseInsensitive)) {
        value.chop(QString("/tilemapresource.xml").size());
    }

    while (value.endsWith('/')) {
        value.chop(1);
    }
    return value + "/{z}/{x}/{y}." + outputExtension;
}

int readZoomValue(const QXmlStreamAttributes& attributes, const QStringList& names)
{
    for (const QString& name : names) {
        const QString value = attributes.value(name).toString();
        if (value.isEmpty())
            continue;

        bool ok = false;
        const int zoom = value.toInt(&ok);
        if (ok)
            return zoom;
    }

    return -1;
}

TileMapResourceMetadata readTileMapResourceMetadata(QIODevice* device)
{
    if (!device)
        return {};

    QXmlStreamReader xml(device);
    TileMapResourceMetadata metadata;

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        const QString elementName = xml.name().toString();
        if (elementName == "Title") {
            metadata.title = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        }
        else if (elementName == "TileFormat") {
            const QXmlStreamAttributes attributes = xml.attributes();
            const QString extension = attributes.value("extension").toString();
            if (!extension.isEmpty()) {
                metadata.extension = normalizedExtension(extension);
            }
        }
        else if (elementName == "TileSets") {
            const QString profile = xml.attributes().value("profile").toString();
            metadata.tmsYOrigin = profile.compare("mercator", Qt::CaseInsensitive) == 0
                || profile.compare("global-mercator", Qt::CaseInsensitive) == 0;
        }
        else if (elementName == "TileSet") {
            const QXmlStreamAttributes attributes = xml.attributes();
            bool ok = false;
            int order = attributes.value("order").toInt(&ok);
            if (!ok) {
                order = attributes.value("href").toInt(&ok);
            }
            if (!ok)
                continue;

            metadata.minTileZoom = metadata.minTileZoom < 0
                ? order
                : qMin(metadata.minTileZoom, order);
            metadata.maxTileZoom = metadata.maxTileZoom < 0
                ? order
                : qMax(metadata.maxTileZoom, order);
            metadata.valid = true;
        }
    }

    return metadata;
}

TileMapResourceMetadata readTileMapResourceMetadata(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    return readTileMapResourceMetadata(&file);
}

TileMapResourceMetadata readTileMapResourceMetadataFromUrl(const QString& urlString)
{
    const QUrl url(urlString);
    if (!url.isValid() || url.scheme().isEmpty() || url.isLocalFile())
        return {};

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    QNetworkReply* reply = manager.get(request);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(750);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return {};
    }

    TileMapResourceMetadata metadata;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QBuffer buffer(&data);
        if (buffer.open(QIODevice::ReadOnly)) {
            metadata = readTileMapResourceMetadata(&buffer);
        }
    }

    reply->deleteLater();
    return metadata;
}

bool tileMapMetadataMatchesUrl(const TileMapResourceMetadata& metadata, const QString& tileMapUrl)
{
    if (metadata.title.isEmpty())
        return false;

    const QString titleKey = normalizedText(QFileInfo(metadata.title).completeBaseName());
    const QString urlKey = normalizedText(QUrl(tileMapUrl).path());
    return titleKey.size() >= 4 && urlKey.contains(titleKey);
}

QStringList tileMapSearchDirectories(const QString& earthFilePath)
{
    QStringList directories;
    const QString appPath = QCoreApplication::applicationDirPath();
    directories << QFileInfo(earthFilePath).absolutePath()
                << QDir::currentPath()
                << appPath
                << QDir(appPath).absoluteFilePath("..");
    directories.removeDuplicates();
    return directories;
}

void applyTileMapResourceMetadata(
    const QString& tileMapUrl,
    const QString& earthFilePath,
    TmsLoader::TileSourceLayer& layer)
{
    if (!tileMapUrl.endsWith("tilemapresource.xml", Qt::CaseInsensitive))
        return;

    const QString urlPath = QUrl(tileMapUrl).path();
    const QString fileName = QFileInfo(urlPath).fileName();
    QString relativeUrlPath = urlPath;
    while (relativeUrlPath.startsWith('/')) {
        relativeUrlPath.remove(0, 1);
    }

    struct Candidate {
        QString filePath;
        bool requireTitleMatch;
    };

    QList<Candidate> candidates;
    for (const QString& directoryPath : tileMapSearchDirectories(earthFilePath)) {
        if (!relativeUrlPath.isEmpty()) {
            candidates.append({ QDir(directoryPath).filePath(relativeUrlPath), false });
        }
        if (!fileName.isEmpty()) {
            candidates.append({ QDir(directoryPath).filePath(fileName), true });
        }
    }

    QSet<QString> seenCandidates;
    for (const Candidate& candidate : candidates) {
        const QFileInfo candidateInfo(candidate.filePath);
        const QString absolutePath = candidateInfo.absoluteFilePath();
        if (seenCandidates.contains(absolutePath) || !candidateInfo.exists())
            continue;
        seenCandidates.insert(absolutePath);

        const TileMapResourceMetadata metadata = readTileMapResourceMetadata(absolutePath);
        if (!metadata.valid)
            continue;
        if (candidate.requireTitleMatch && !tileMapMetadataMatchesUrl(metadata, tileMapUrl))
            continue;

        layer.urlTemplate = tileTemplateFromUrl(tileMapUrl, metadata.extension);
        layer.minTileZoom = metadata.minTileZoom;
        layer.maxTileZoom = metadata.maxTileZoom;
        layer.tmsYOrigin = metadata.tmsYOrigin;
        return;
    }

    const TileMapResourceMetadata metadata = readTileMapResourceMetadataFromUrl(tileMapUrl);
    if (!metadata.valid)
        return;

    layer.urlTemplate = tileTemplateFromUrl(tileMapUrl, metadata.extension);
    layer.minTileZoom = metadata.minTileZoom;
    layer.maxTileZoom = metadata.maxTileZoom;
    layer.tmsYOrigin = metadata.tmsYOrigin;
}

QList<TmsLoader::TileSourceLayer> readEarthLayers(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString wrappedXml = "<earth>\n" + QString::fromUtf8(file.readAll()) + "\n</earth>";
    QXmlStreamReader xml(wrappedXml);
    QList<TmsLoader::TileSourceLayer> layers;

    TmsLoader::TileSourceLayer currentLayer;
    bool insideImage = false;
    bool skipCurrentImage = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString elementName = xml.name().toString();
            if (elementName == "image") {
                const QXmlStreamAttributes attributes = xml.attributes();
                const QString driver = attributes.value("driver").toString();
                insideImage = true;
                skipCurrentImage = !driver.isEmpty() && driver.compare("tms", Qt::CaseInsensitive) != 0;
                currentLayer = TmsLoader::TileSourceLayer();
                currentLayer.name = attributes.value("name").toString();
                currentLayer.minZoom = readZoomValue(attributes, { "min_level", "minLevel", "min_zoom", "minZoom" });
                currentLayer.maxZoom = readZoomValue(attributes, { "max_level", "maxLevel", "max_zoom", "maxZoom" });
                currentLayer.tmsYOrigin = driver.compare("tms", Qt::CaseInsensitive) == 0;
            }
            else if (insideImage && elementName == "url") {
                const QString rawUrl = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                currentLayer.urlTemplate = tileTemplateFromUrl(rawUrl);
                applyTileMapResourceMetadata(rawUrl, filePath, currentLayer);
            }
            else if (insideImage && (elementName == "min_level" || elementName == "minZoom" || elementName == "min_zoom")) {
                bool ok = false;
                const int zoom = xml.readElementText(QXmlStreamReader::SkipChildElements).toInt(&ok);
                if (ok)
                    currentLayer.minZoom = zoom;
            }
            else if (insideImage && (elementName == "max_level" || elementName == "maxZoom" || elementName == "max_zoom")) {
                bool ok = false;
                const int zoom = xml.readElementText(QXmlStreamReader::SkipChildElements).toInt(&ok);
                if (ok)
                    currentLayer.maxZoom = zoom;
            }
        }
        else if (xml.isEndElement() && xml.name() == "image") {
            if (insideImage && !skipCurrentImage && !currentLayer.urlTemplate.isEmpty()) {
                if (currentLayer.name.isEmpty()) {
                    currentLayer.name = QFileInfo(currentLayer.urlTemplate).baseName();
                }
                layers.append(currentLayer);
            }
            insideImage = false;
            skipCurrentImage = false;
        }
    }

    return layers;
}

QString sourceLabelForFile(const QFileInfo& fileInfo)
{
    const QString lowerName = fileInfo.completeBaseName().toLower();
    if (lowerName.contains("satellite") || lowerName.contains("imagery"))
        return "Satellite Imagery";
    if (lowerName.contains("cardg") || lowerName.contains("topographic") || lowerName.contains("topo"))
        return "CardG Maps";

    QString label = fileInfo.completeBaseName();
    label.replace('_', ' ');
    return label;
}

QStringList earthSearchDirectories()
{
    QStringList directories;
    const QString currentPath = QDir::currentPath();
    const QString appPath = QCoreApplication::applicationDirPath();
    directories << QDir(currentPath).absoluteFilePath("Data/EarthFiles")
                << currentPath
                << QDir(appPath).absoluteFilePath("Data/EarthFiles")
                << appPath
                << QDir(appPath).absoluteFilePath("../Data/EarthFiles")
                << QDir(appPath).absoluteFilePath("..");
    directories.removeDuplicates();
    return directories;
}

QList<EarthSource> discoverEarthSources()
{
    QList<EarthSource> sources;
    QSet<QString> seenFiles;

    for (const QString& directoryPath : earthSearchDirectories()) {
        const QDir directory(directoryPath);
        const QFileInfoList earthFiles = directory.entryInfoList({ "*.earth" }, QDir::Files, QDir::Name);
        for (const QFileInfo& earthFile : earthFiles) {
            const QString absolutePath = earthFile.absoluteFilePath();
            if (seenFiles.contains(absolutePath))
                continue;
            seenFiles.insert(absolutePath);

            EarthSource source;
            source.label = sourceLabelForFile(earthFile);
            source.filePath = absolutePath;
            source.layers = readEarthLayers(absolutePath);
            if (!source.layers.isEmpty()) {
                sources.append(source);
            }
        }
    }

    return sources;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUI();
    createStatusBar();
    setWindowTitle("Military GIS Map Application");
    resize(1280, 800);
}

void MainWindow::setupUI()
{
    // Create toolbar
    QToolBar* toolbar = addToolBar("Navigation");
    toolbar->addAction("Zoom In", [this]() {
        m_mapWidget->camera()->zoom(0.5f);
        });
    toolbar->addAction("Zoom Out", [this]() {
        m_mapWidget->camera()->zoom(-0.5f);
        });
    toolbar->addAction("Reset View", [this]() {
        m_mapWidget->camera()->setCenter(QPointF(0, 0));
        m_mapWidget->camera()->setZoomLevel(2.0);
        });

    // Create map widget
    m_mapWidget = new MapWidget(this);
    const QList<EarthSource> earthSources = discoverEarthSources();
    if (!earthSources.isEmpty()) {
        m_mapWidget->setTileSourceLayers(earthSources.first().layers);
    }
    else {
        m_mapWidget->setTileServerUrl("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    }
    m_mapWidget->loadWorldBorders();
    m_mapWidget->loadCities();

    setCentralWidget(m_mapWidget);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Map Source:", this));
    QComboBox* sourceCombo = new QComboBox(this);
    sourceCombo->setMinimumContentsLength(20);
    sourceCombo->setToolTip("Select a map source loaded from a .earth file.");

    for (int i = 0; i < earthSources.size(); ++i) {
        const EarthSource& source = earthSources.at(i);
        sourceCombo->addItem(
            QString("%1 (%2 layers)").arg(source.label).arg(source.layers.size()),
            i);
    }
    if (earthSources.isEmpty()) {
        sourceCombo->addItem("OpenStreetMap fallback", -1);
    }

    toolbar->addWidget(sourceCombo);

    auto applySourceSelection = [this, earthSources](int sourceIndex) {
        if (sourceIndex >= 0 && sourceIndex < earthSources.size()) {
            const EarthSource& source = earthSources.at(sourceIndex);
            m_mapWidget->setTileSourceLayers(source.layers);
            statusBar()->showMessage(
                QString("Map source set to %1 from %2")
                    .arg(source.label, QFileInfo(source.filePath).fileName()),
                3000);
            return;
        }

        m_mapWidget->setTileServerUrl("https://tile.openstreetmap.org/{z}/{x}/{y}.png");
        statusBar()->showMessage("Map source set to OpenStreetMap fallback", 3000);
    };

    connect(sourceCombo, &QComboBox::activated, this, [sourceCombo, applySourceSelection](int index) {
        applySourceSelection(sourceCombo->itemData(index).toInt());
        });

    QAction* loadBordersAction = new QAction("Load Borders...", this);
    toolbar->addSeparator();
    toolbar->addAction(loadBordersAction);

    QAction* texturesVisibleAction = new QAction("Textures", this);
    texturesVisibleAction->setCheckable(true);
    texturesVisibleAction->setChecked(true);
    texturesVisibleAction->setToolTip("Show or hide map imagery.");
    toolbar->addAction(texturesVisibleAction);

    QAction* bordersVisibleAction = new QAction("Borders", this);
    bordersVisibleAction->setCheckable(true);
    bordersVisibleAction->setChecked(true);
    bordersVisibleAction->setToolTip("Show or hide shapefile borders.");
    toolbar->addAction(bordersVisibleAction);

    QAction* gridVisibleAction = new QAction("Grid", this);
    gridVisibleAction->setCheckable(true);
    gridVisibleAction->setChecked(true);
    gridVisibleAction->setToolTip("Show or hide latitude and longitude grid.");
    toolbar->addAction(gridVisibleAction);

    QAction* citiesVisibleAction = new QAction("Cities", this);
    citiesVisibleAction->setCheckable(true);
    citiesVisibleAction->setChecked(true);
    citiesVisibleAction->setToolTip("Show or hide city and settlement labels.");
    toolbar->addAction(citiesVisibleAction);

    QAction* wrapLongitudeAction = new QAction("Wrap Longitude", this);
    wrapLongitudeAction->setCheckable(true);
    wrapLongitudeAction->setToolTip("Repeat imagery horizontally instead of constraining longitude to +/-180 degrees.");
    toolbar->addSeparator();
    toolbar->addAction(wrapLongitudeAction);

    QAction* globeViewAction = new QAction("Globe View", this);
    globeViewAction->setCheckable(true);
    globeViewAction->setToolTip("Switch between flat Mercator map and orthographic globe view.");
    toolbar->addAction(globeViewAction);

    QMenu* fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction(loadBordersAction);

    QMenu* viewMenu = menuBar()->addMenu("View");
    viewMenu->addAction(texturesVisibleAction);
    viewMenu->addAction(bordersVisibleAction);
    viewMenu->addAction(gridVisibleAction);
    viewMenu->addAction(citiesVisibleAction);
    viewMenu->addSeparator();
    viewMenu->addAction(wrapLongitudeAction);
    viewMenu->addAction(globeViewAction);

    connect(loadBordersAction, &QAction::triggered, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(
            this,
            "Load Border Shapefile",
            QDir::currentPath(),
            "ESRI Shapefile (*.shp)");

        if (filePath.isEmpty())
            return;

        QString message;
        if (m_mapWidget->loadBorderShapefile(filePath, &message)) {
            statusBar()->showMessage(message, 4000);
        }
        else {
            statusBar()->showMessage(
                QString("Could not load %1: %2").arg(QFileInfo(filePath).fileName(), message),
                6000);
        }
        });

    connect(texturesVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setTexturesVisible(visible);
        statusBar()->showMessage(visible ? "Map imagery shown" : "Map imagery hidden", 2000);
        });

    connect(bordersVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setBordersVisible(visible);
        statusBar()->showMessage(visible ? "Borders shown" : "Borders hidden", 2000);
        });

    connect(gridVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setGridVisible(visible);
        statusBar()->showMessage(visible ? "Grid shown" : "Grid hidden", 2000);
        });

    connect(citiesVisibleAction, &QAction::toggled, this, [this](bool visible) {
        m_mapWidget->setCitiesVisible(visible);
        statusBar()->showMessage(visible ? "Cities shown" : "Cities hidden", 2000);
        });

    connect(wrapLongitudeAction, &QAction::toggled, this, [this](bool enabled) {
        m_mapWidget->camera()->setHorizontalWrapEnabled(enabled);
        statusBar()->showMessage(
            enabled ? "Longitude wrapping enabled" : "Longitude constrained to +/-180 degrees",
            2000);
        });

    connect(globeViewAction, &QAction::toggled, this, [this](bool enabled) {
        m_mapWidget->camera()->setProjectionMode(
            enabled ? Camera::ProjectionMode::Orthographic : Camera::ProjectionMode::Mercator);
        statusBar()->showMessage(enabled ? "Orthographic globe view enabled" : "Mercator map view enabled", 2000);
        });

    // Connect camera for status updates
    connect(m_mapWidget->camera(), &Camera::cameraChanged, [this]() {
        QPointF center = m_mapWidget->camera()->getCenterMercator();
        QPointF latLon = MercatorProjection::mercatorToLatLon(center.x(), center.y());
        m_coordLabel->setText(QString("Center: %1%3N, %2%3E")
            .arg(latLon.x(), 0, 'f', 2)
            .arg(latLon.y(), 0, 'f', 2)
            .arg(QChar(0x00B0)));
        m_zoomLabel->setText(QString("Zoom: %1")
            .arg(m_mapWidget->camera()->getZoomLevel(), 0, 'f', 1));
        });
}

void MainWindow::createStatusBar()
{
    statusBar()->show();
    m_coordLabel = new QLabel(QString("Center: 0.00%1N, 0.00%1E").arg(QChar(0x00B0)), this);
    m_zoomLabel = new QLabel("Zoom: 2.0", this);

    statusBar()->addWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
}
