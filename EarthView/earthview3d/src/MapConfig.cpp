#include "earthview3d/MapConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace earthview3d {

namespace {
void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QString resolveAgainst(const QString& path, const QString& root)
{
    if (path.isEmpty())
        return QString();
    if (QDir::isAbsolutePath(path))
        return QDir::cleanPath(path);
    const QDir base(root.isEmpty() ? QDir::currentPath() : root);
    return QDir::cleanPath(base.absoluteFilePath(path));
}
} // namespace

QString MapConfig::resolvedEarthFile() const
{
    return resolveAgainst(earthFile, dataRoot);
}

QString MapConfig::resolvedCacheDir() const
{
    return resolveAgainst(cacheDir, dataRoot);
}

QDateTime MapConfig::effectiveSkyTime() const
{
    return skyTime.isValid() ? skyTime.toUTC() : QDateTime::currentDateTimeUtc();
}

bool MapConfig::validate(QString* error) const
{
    if (earthFile.isEmpty()) {
        setError(error, QStringLiteral("No earth file configured"));
        return false;
    }
    if (!dataRoot.isEmpty() && !QFileInfo(dataRoot).isDir()) {
        setError(error, QStringLiteral("Data root %1 does not exist").arg(dataRoot));
        return false;
    }
    const QString path = resolvedEarthFile();
    if (!QFileInfo(path).isFile()) {
        setError(error, QStringLiteral("Earth file %1 does not exist").arg(path));
        return false;
    }
    return true;
}

bool MapConfig::fromJson(const QByteArray& json, const QString& baseDir, MapConfig* config,
                         QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("Invalid JSON at offset %1: %2")
                            .arg(parseError.offset).arg(parseError.errorString()));
        return false;
    }
    if (!doc.isObject()) {
        setError(error, QStringLiteral("Map config must be a JSON object"));
        return false;
    }

    const QJsonObject obj = doc.object();
    static const QStringList kKnownKeys = {
        QStringLiteral("earthFile"), QStringLiteral("dataRoot"), QStringLiteral("cacheDir"),
        QStringLiteral("sky"), QStringLiteral("skyTime")};
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!kKnownKeys.contains(it.key())) {
            setError(error, QStringLiteral("Unknown map config key \"%1\"").arg(it.key()));
            return false;
        }
    }

    MapConfig result;
    auto readString = [&](const QString& key, QString* out) {
        const QJsonValue value = obj.value(key);
        if (value.isUndefined() || value.isNull())
            return true;
        if (!value.isString()) {
            setError(error, QStringLiteral("\"%1\" must be a string").arg(key));
            return false;
        }
        *out = value.toString();
        return true;
    };
    if (!readString(QStringLiteral("earthFile"), &result.earthFile)
        || !readString(QStringLiteral("dataRoot"), &result.dataRoot)
        || !readString(QStringLiteral("cacheDir"), &result.cacheDir))
        return false;

    if (result.earthFile.isEmpty()) {
        setError(error, QStringLiteral("\"earthFile\" is required"));
        return false;
    }

    const QJsonValue sky = obj.value(QStringLiteral("sky"));
    if (!sky.isUndefined() && !sky.isNull()) {
        if (!sky.isBool()) {
            setError(error, QStringLiteral("\"sky\" must be true or false"));
            return false;
        }
        result.sky = sky.toBool();
    }

    QString skyTime;
    if (!readString(QStringLiteral("skyTime"), &skyTime))
        return false;
    if (!skyTime.isEmpty()) {
        result.skyTime = QDateTime::fromString(skyTime, Qt::ISODate);
        if (!result.skyTime.isValid()) {
            setError(error, QStringLiteral("\"skyTime\" is not an ISO 8601 date-time: %1").arg(skyTime));
            return false;
        }
        result.skyTime = result.skyTime.toUTC();
    }

    // A relative data root is relative to the config file, not the working directory.
    if (!result.dataRoot.isEmpty())
        result.dataRoot = resolveAgainst(result.dataRoot, baseDir.isEmpty() ? QDir::currentPath() : baseDir);
    else if (!baseDir.isEmpty())
        result.dataRoot = QDir::cleanPath(QDir(baseDir).absolutePath());

    *config = result;
    return true;
}

bool MapConfig::fromJsonFile(const QString& path, MapConfig* config, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot read map config %1: %2").arg(path, file.errorString()));
        return false;
    }
    return fromJson(file.readAll(), QFileInfo(path).absolutePath(), config, error);
}

} // namespace earthview3d
