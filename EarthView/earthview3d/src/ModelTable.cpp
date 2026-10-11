#include "earthview3d/ModelTable.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace earthview3d {

namespace {
void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

bool globMatch(const QString& pattern, const QString& text)
{
    QString rx;
    rx.reserve(pattern.size() * 2 + 4);
    for (const QChar c : pattern) {
        if (c == QLatin1Char('*'))
            rx += QStringLiteral(".*");
        else if (c == QLatin1Char('?'))
            rx += QLatin1Char('.');
        else
            rx += QRegularExpression::escape(QString(c));
    }
    return QRegularExpression(QRegularExpression::anchoredPattern(rx)).match(text).hasMatch();
}
} // namespace

std::uint32_t entityColor(const EntityState3D& state)
{
    // Affiliation colours after MIL-STD-2525 (light fill variants), fully opaque.
    switch (state.force) {
    case Force::Friendly: return 0x80E0FFFFu;
    case Force::Hostile: return 0xFF8080FFu;
    case Force::Neutral: return 0xAAFFAAFFu;
    case Force::Unknown: return 0xFFFF80FFu;
    case Force::Unspecified: break;
    }
    return state.rgba;
}

int ModelTable::specificity(const QString& pattern)
{
    int literal = 0;
    for (const QChar c : pattern) {
        if (c != QLatin1Char('*') && c != QLatin1Char('?'))
            ++literal;
    }
    return literal;
}

const ModelEntry* ModelTable::match(const QString& kindKey) const
{
    if (kindKey.isEmpty())
        return nullptr;
    const ModelEntry* best = nullptr;
    int bestSpecificity = -1;
    for (const ModelEntry& entry : m_entries) {
        const int s = specificity(entry.pattern);
        if (s > bestSpecificity && globMatch(entry.pattern, kindKey)) {
            best = &entry;
            bestSpecificity = s;
        }
    }
    return best;
}

QString ModelTable::resolvedFile(const ModelEntry& entry) const
{
    if (entry.file.isEmpty())
        return QString();
    if (QDir::isAbsolutePath(entry.file))
        return QDir::cleanPath(entry.file);
    return QDir::cleanPath(QDir(m_baseDir.isEmpty() ? QDir::currentPath() : m_baseDir).absoluteFilePath(entry.file));
}

bool ModelTable::fromJson(const QByteArray& json, const QString& baseDir, ModelTable* table, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(error, parseError.error != QJsonParseError::NoError
                            ? QStringLiteral("Invalid JSON at offset %1: %2").arg(parseError.offset).arg(parseError.errorString())
                            : QStringLiteral("Model table must be a JSON object"));
        return false;
    }

    const QJsonObject root = doc.object();
    ModelTable result;
    result.m_baseDir = baseDir;
    if (root.contains(QStringLiteral("iconBelowPixels"))) {
        const QJsonValue v = root.value(QStringLiteral("iconBelowPixels"));
        if (!v.isDouble() || v.toDouble() < 0.0) {
            setError(error, QStringLiteral("\"iconBelowPixels\" must be a non-negative number"));
            return false;
        }
        result.m_iconBelowPixels = v.toDouble();
    }

    const QJsonValue models = root.value(QStringLiteral("models"));
    if (!models.isArray()) {
        setError(error, QStringLiteral("\"models\" must be an array"));
        return false;
    }

    static const QStringList kKeys = {QStringLiteral("kind"), QStringLiteral("file"), QStringLiteral("scale"),
                                      QStringLiteral("hpr"), QStringLiteral("clampToTerrain"), QStringLiteral("shape")};
    const QJsonArray array = models.toArray();
    for (int i = 0; i < array.size(); ++i) {
        const QString where = QStringLiteral("models[%1]").arg(i);
        if (!array[i].isObject()) {
            setError(error, where + QStringLiteral(" must be an object"));
            return false;
        }
        const QJsonObject o = array[i].toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!kKeys.contains(it.key())) {
                setError(error, QStringLiteral("%1: unknown key \"%2\"").arg(where, it.key()));
                return false;
            }
        }

        ModelEntry entry;
        entry.pattern = o.value(QStringLiteral("kind")).toString();
        if (entry.pattern.isEmpty()) {
            setError(error, where + QStringLiteral(": \"kind\" is required"));
            return false;
        }
        entry.file = o.value(QStringLiteral("file")).toString();
        entry.scale = o.value(QStringLiteral("scale")).toDouble(1.0);
        if (entry.scale <= 0.0) {
            setError(error, where + QStringLiteral(": \"scale\" must be positive"));
            return false;
        }
        if (o.contains(QStringLiteral("hpr"))) {
            const QJsonArray hpr = o.value(QStringLiteral("hpr")).toArray();
            if (hpr.size() != 3) {
                setError(error, where + QStringLiteral(": \"hpr\" must be [heading, pitch, roll] in degrees"));
                return false;
            }
            entry.headingOffsetDeg = hpr[0].toDouble();
            entry.pitchOffsetDeg = hpr[1].toDouble();
            entry.rollOffsetDeg = hpr[2].toDouble();
        }
        entry.clampToTerrain = o.value(QStringLiteral("clampToTerrain")).toBool(false);
        entry.shape = o.value(QStringLiteral("shape")).toInt(-1);
        result.m_entries.push_back(entry);
    }

    *table = result;
    return true;
}

bool ModelTable::fromJsonFile(const QString& path, ModelTable* table, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot read model table %1: %2").arg(path, file.errorString()));
        return false;
    }
    return fromJson(file.readAll(), QFileInfo(path).absolutePath(), table, error);
}

ResolvedStyle resolveStyle(const EntityState3D& state, const ModelTable* table)
{
    ResolvedStyle style;
    style.shape = state.shape;
    style.clampToTerrain = state.clampToTerrain;
    style.rgba = entityColor(state);
    style.label = state.label.isEmpty() ? QStringLiteral("#%1").arg(state.id) : state.label;
    style.visible = state.visible;

    if (const ModelEntry* entry = table ? table->match(state.kindKey) : nullptr) {
        style.modelFile = table->resolvedFile(*entry);
        style.scale = entry->scale;
        style.headingOffsetDeg = entry->headingOffsetDeg;
        style.pitchOffsetDeg = entry->pitchOffsetDeg;
        style.rollOffsetDeg = entry->rollOffsetDeg;
        style.clampToTerrain = state.clampToTerrain || entry->clampToTerrain;
        if (entry->shape >= 0)
            style.shape = entry->shape;
    }
    return style;
}

} // namespace earthview3d
