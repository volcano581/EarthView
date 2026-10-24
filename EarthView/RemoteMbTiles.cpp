#include "RemoteMbTiles.h"

#include <QCache>
#include <QDateTime>
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QWaitCondition>
#include <cstring>
#include <memory>
#include <utility>

namespace {
// Range requests are made in blocks of this size: a page walk then costs one request for the
// upper B-tree levels (cached after the first tile) and one or two for the leaf and the blob.
constexpr qint64 kBlockSize = 64 * 1024;
constexpr qint64 kBlockCacheBytes = 64LL * 1024 * 1024;
// A server that ignores Range is accepted only for a file this small (read once).
constexpr qint64 kWholeFileLimit = 64LL * 1024 * 1024;
constexpr int kRequestTimeoutMs = 15000;
// Opening (header, schema, metadata) runs when the earth files are read at start-up, on the UI
// thread: an unreachable server must not hold the application for long.
constexpr int kOpenTimeoutMs = 5000;
// After a failed open, tile reads do not retry the server before this (ms).
constexpr qint64 kOpenRetryMs = 5000;

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

// ── HTTP ─────────────────────────────────────────────────────────────────────

/// One manager per thread, kept for the thread's lifetime so its keep-alive connections are
/// reused. Deliberately never deleted: reader threads live until the process exits.
QNetworkAccessManager* threadNetworkManager()
{
    thread_local QNetworkAccessManager* manager = nullptr;
    if (!manager)
        manager = new QNetworkAccessManager();
    return manager;
}

struct HttpResult {
    QByteArray body;
    int status = 0;
    QByteArray contentRange;
    QString error;
    bool ignoredRange = false;
};

/// Blocking GET on the calling thread. With @p range, a 200 answer (the server ignores Range)
/// is aborted as soon as its headers arrive, unless its body is no larger than @p wholeLimit.
HttpResult blockingGet(const QUrl& url, const QByteArray& range, int timeoutMs, qint64 wholeLimit = 0)
{
    HttpResult result;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("EarthView/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!range.isEmpty()) {
        request.setRawHeader("Range", range);
        // Byte offsets must refer to the file itself, not to a compressed transfer.
        request.setRawHeader("Accept-Encoding", "identity");
    }

    QNetworkReply* reply = threadNetworkManager()->get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (!range.isEmpty()) {
        QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&result, reply, wholeLimit, &loop]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200)
                return;
            const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            if (length <= 0 || length > wholeLimit) {
                result.ignoredRange = true;
                loop.quit();
            }
        });
    }
    timeout.start(timeoutMs);
    if (!reply->isFinished())
        loop.exec();

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (result.ignoredRange) {
        reply->abort();
        result.error = QStringLiteral("%1 does not support HTTP range requests (it answered with the whole file). "
                                      "Serve the file from a web server that does (IIS, nginx, Apache).")
                           .arg(url.toString());
    }
    else if (!reply->isFinished()) {
        reply->abort();
        result.error = QStringLiteral("Timed out reading %1").arg(url.toString());
    }
    else if (reply->error() != QNetworkReply::NoError && result.status == 0) {
        result.error = QStringLiteral("Could not read %1: %2").arg(url.toString(), reply->errorString());
    }
    else {
        result.body = reply->readAll();
        result.contentRange = reply->rawHeader("Content-Range");
    }
    reply->deleteLater();
    return result;
}

// ── Remote file in cached blocks ─────────────────────────────────────────────

class RemoteFile
{
public:
    explicit RemoteFile(const QUrl& url)
        : m_url(url)
    {
        m_blocks.setMaxCost(static_cast<int>(kBlockCacheBytes / 1024));  // cost in KB
    }

    QUrl url() const { return m_url; }

    bool read(qint64 offset, qint64 length, QByteArray* out, QString* errorMessage,
              int timeoutMs = kRequestTimeoutMs)
    {
        out->resize(length);
        qint64 done = 0;
        while (done < length) {
            const qint64 position = offset + done;
            const qint64 blockIndex = position / kBlockSize;
            QByteArray block;
            if (!block_(blockIndex, &block, errorMessage, timeoutMs))
                return false;
            const qint64 inBlock = position - blockIndex * kBlockSize;
            const qint64 available = block.size() - inBlock;
            if (available <= 0) {
                setError(errorMessage, QStringLiteral("Read past the end of %1").arg(m_url.toString()));
                return false;
            }
            const qint64 take = qMin(available, length - done);
            std::memcpy(out->data() + done, block.constData() + inBlock, static_cast<size_t>(take));
            done += take;
        }
        return true;
    }

private:
    bool block_(qint64 index, QByteArray* block, QString* errorMessage, int timeoutMs)
    {
        {
            QMutexLocker lock(&m_mutex);
            for (;;) {
                if (!m_wholeFile.isEmpty()) {
                    *block = m_wholeFile.mid(index * kBlockSize, kBlockSize);
                    return true;
                }
                if (const QByteArray* cached = m_blocks.object(index)) {
                    *block = *cached;
                    return true;
                }
                // Another reader is fetching it (the upper B-tree pages are wanted by every
                // tile): wait for that instead of asking the server again.
                if (!m_inFlight.contains(index))
                    break;
                if (!m_blockArrived.wait(&m_mutex, timeoutMs))
                    break;
            }
            m_inFlight.insert(index);
        }

        const qint64 first = index * kBlockSize;
        const QByteArray range = "bytes=" + QByteArray::number(first) + '-' + QByteArray::number(first + kBlockSize - 1);
        const HttpResult result = blockingGet(m_url, range, timeoutMs, kWholeFileLimit);

        QMutexLocker lock(&m_mutex);
        m_inFlight.remove(index);
        m_blockArrived.wakeAll();
        if (!result.error.isEmpty()) {
            setError(errorMessage, result.error);
            return false;
        }
        if (result.status == 200) {
            // No range support, but the whole (small) file came back: keep it.
            m_wholeFile = result.body;
            *block = m_wholeFile.mid(first, kBlockSize);
            return true;
        }
        if (result.status != 206) {
            setError(errorMessage, QStringLiteral("HTTP %1 reading %2").arg(result.status).arg(m_url.toString()));
            return false;
        }

        m_blocks.insert(index, new QByteArray(result.body), qMax<int>(1, static_cast<int>(result.body.size() / 1024)));
        *block = result.body;
        return true;
    }

    QUrl m_url;
    QMutex m_mutex;
    QWaitCondition m_blockArrived;
    QSet<qint64> m_inFlight;
    QCache<qint64, QByteArray> m_blocks;
    QByteArray m_wholeFile;
};

// ── SQLite file format (read-only) ───────────────────────────────────────────

enum PageType : quint8 {
    InteriorIndex = 2,
    InteriorTable = 5,
    LeafIndex = 10,
    LeafTable = 13
};

quint16 be16(const char* p)
{
    return static_cast<quint16>((static_cast<quint8>(p[0]) << 8) | static_cast<quint8>(p[1]));
}

quint32 be32(const char* p)
{
    return (static_cast<quint32>(static_cast<quint8>(p[0])) << 24)
        | (static_cast<quint32>(static_cast<quint8>(p[1])) << 16)
        | (static_cast<quint32>(static_cast<quint8>(p[2])) << 8)
        | static_cast<quint32>(static_cast<quint8>(p[3]));
}

/// SQLite varint (big-endian groups of 7 bits, 9th byte carries 8). Returns bytes used, 0 on
/// overrun.
int sqliteVarint(const char* p, const char* end, quint64* value)
{
    quint64 result = 0;
    for (int i = 0; i < 9; ++i) {
        if (p + i >= end)
            return 0;
        const quint8 byte = static_cast<quint8>(p[i]);
        if (i == 8) {
            *value = (result << 8) | byte;
            return 9;
        }
        result = (result << 7) | (byte & 0x7f);
        if ((byte & 0x80) == 0) {
            *value = result;
            return i + 1;
        }
    }
    return 0;
}

struct SqlValue {
    enum Type { Null, Integer, Real, Text, Blob } type = Null;
    qint64 integer = 0;
    double real = 0.0;
    QByteArray bytes;  // Text in the database encoding, or Blob

    static SqlValue fromInteger(qint64 value)
    {
        SqlValue v;
        v.type = Integer;
        v.integer = value;
        return v;
    }
};

using Record = QVector<SqlValue>;

bool decodeRecord(const QByteArray& payload, Record* record)
{
    record->clear();
    const char* begin = payload.constData();
    const char* end = begin + payload.size();
    quint64 headerSize = 0;
    const int headerBytes = sqliteVarint(begin, end, &headerSize);
    if (headerBytes == 0 || headerSize > static_cast<quint64>(payload.size()))
        return false;

    const char* type = begin + headerBytes;
    const char* headerEnd = begin + headerSize;
    const char* body = headerEnd;
    while (type < headerEnd) {
        quint64 serial = 0;
        const int used = sqliteVarint(type, headerEnd, &serial);
        if (used == 0)
            return false;
        type += used;

        SqlValue value;
        qint64 size = 0;
        switch (serial) {
        case 0: value.type = SqlValue::Null; break;
        case 1: size = 1; break;
        case 2: size = 2; break;
        case 3: size = 3; break;
        case 4: size = 4; break;
        case 5: size = 6; break;
        case 6: size = 8; break;
        case 7: size = 8; break;
        case 8: value = SqlValue::fromInteger(0); break;
        case 9: value = SqlValue::fromInteger(1); break;
        case 10:
        case 11: return false;
        default:
            size = static_cast<qint64>((serial - (serial % 2 == 0 ? 12 : 13)) / 2);
            value.type = serial % 2 == 0 ? SqlValue::Blob : SqlValue::Text;
            break;
        }
        if (body + size > end)
            return false;
        if (serial >= 1 && serial <= 6) {
            qint64 v = static_cast<qint8>(body[0]);  // sign-extend the first byte
            for (qint64 i = 1; i < size; ++i)
                v = (v << 8) | static_cast<quint8>(body[i]);
            value = SqlValue::fromInteger(v);
        }
        else if (serial == 7) {
            quint64 bits = 0;
            for (int i = 0; i < 8; ++i)
                bits = (bits << 8) | static_cast<quint8>(body[i]);
            value.type = SqlValue::Real;
            std::memcpy(&value.real, &bits, sizeof(double));
        }
        else if (serial >= 12) {
            value.bytes = QByteArray(body, static_cast<qsizetype>(size));
        }
        body += size;
        record->append(value);
    }
    return true;
}

int typeRank(const SqlValue& v)
{
    switch (v.type) {
    case SqlValue::Null: return 0;
    case SqlValue::Integer:
    case SqlValue::Real: return 1;
    case SqlValue::Text: return 2;
    case SqlValue::Blob: return 3;
    }
    return 0;
}

/// SQLite's record order with BINARY collation.
int compareValues(const SqlValue& a, const SqlValue& b)
{
    const int rankA = typeRank(a);
    const int rankB = typeRank(b);
    if (rankA != rankB)
        return rankA < rankB ? -1 : 1;
    if (rankA == 0)
        return 0;
    if (rankA == 1) {
        if (a.type == SqlValue::Integer && b.type == SqlValue::Integer)
            return a.integer < b.integer ? -1 : (a.integer > b.integer ? 1 : 0);
        const double x = a.type == SqlValue::Integer ? double(a.integer) : a.real;
        const double y = b.type == SqlValue::Integer ? double(b.integer) : b.real;
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    const int common = static_cast<int>(qMin(a.bytes.size(), b.bytes.size()));
    const int c = std::memcmp(a.bytes.constData(), b.bytes.constData(), static_cast<size_t>(common));
    if (c != 0)
        return c < 0 ? -1 : 1;
    return a.bytes.size() < b.bytes.size() ? -1 : (a.bytes.size() > b.bytes.size() ? 1 : 0);
}

/// Compares @p key with the first key.size() fields of @p record.
int comparePrefix(const Record& key, const Record& record)
{
    for (int i = 0; i < key.size(); ++i) {
        if (i >= record.size())
            return 1;
        const int c = compareValues(key.at(i), record.at(i));
        if (c != 0)
            return c;
    }
    return 0;
}

// ── Schema (from the CREATE statements) ──────────────────────────────────────

QString unquoted(QString name)
{
    name = name.trimmed();
    if (name.size() >= 2) {
        const QChar first = name.front();
        const QChar last = name.back();
        if ((first == '"' && last == '"') || (first == '`' && last == '`')
            || (first == '[' && last == ']') || (first == '\'' && last == '\''))
            name = name.mid(1, name.size() - 2);
    }
    return name.toLower();
}

/// Splits the text between the outermost parentheses at top-level commas.
QStringList splitTopLevel(const QString& text)
{
    QStringList parts;
    int depth = 0;
    QString current;
    QChar quote;
    for (const QChar ch : text) {
        if (!quote.isNull()) {
            current += ch;
            if (ch == quote)
                quote = QChar();
            continue;
        }
        if (ch == '"' || ch == '\'' || ch == '`') {
            quote = ch;
        }
        else if (ch == '[') {
            quote = ']';
        }
        else if (ch == '(') {
            ++depth;
        }
        else if (ch == ')') {
            --depth;
        }
        else if (ch == ',' && depth == 0) {
            parts.append(current.trimmed());
            current.clear();
            continue;
        }
        current += ch;
    }
    if (!current.trimmed().isEmpty())
        parts.append(current.trimmed());
    return parts;
}

QString parenthesised(const QString& sql, int from = 0)
{
    const int open = sql.indexOf('(', from);
    if (open < 0)
        return QString();
    int depth = 0;
    for (int i = open; i < sql.size(); ++i) {
        if (sql.at(i) == '(')
            ++depth;
        else if (sql.at(i) == ')' && --depth == 0)
            return sql.mid(open + 1, i - open - 1);
    }
    return QString();
}

QStringList columnList(const QString& inner)
{
    QStringList columns;
    for (const QString& part : splitTopLevel(inner)) {
        const QString name = part.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
        columns.append(unquoted(name));
    }
    return columns;
}

struct TableInfo {
    QString name;
    quint32 rootPage = 0;
    QStringList columns;          ///< declaration order
    QString rowidAlias;           ///< INTEGER PRIMARY KEY column, if any
    QStringList primaryKey;       ///< declared PRIMARY KEY columns
    QList<QStringList> uniqueKeys;///< column / table UNIQUE constraints, in order
    bool withoutRowid = false;

    /// Field order of a stored row.
    QStringList storedColumns() const
    {
        if (!withoutRowid)
            return columns;
        QStringList order = primaryKey;
        for (const QString& column : columns) {
            if (!order.contains(column))
                order.append(column);
        }
        return order;
    }
};

struct IndexInfo {
    QString name;
    QString table;
    quint32 rootPage = 0;
    QStringList columns;
};

TableInfo parseCreateTable(const QString& name, quint32 rootPage, const QString& sql)
{
    TableInfo table;
    table.name = name.toLower();
    table.rootPage = rootPage;
    const QString inner = parenthesised(sql);
    const int close = sql.lastIndexOf(')');
    table.withoutRowid = close >= 0
        && sql.mid(close).contains(QRegularExpression(QStringLiteral("without\\s+rowid"),
                                                      QRegularExpression::CaseInsensitiveOption));

    QList<QStringList> constraintKeys;
    for (const QString& part : splitTopLevel(inner)) {
        const QString lower = part.toLower();
        const QString first = lower.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
        if (first == QLatin1String("constraint") || first == QLatin1String("primary")
            || first == QLatin1String("unique") || first == QLatin1String("check")
            || first == QLatin1String("foreign")) {
            const QStringList columns = columnList(parenthesised(part));
            if (lower.contains(QLatin1String("primary key")))
                table.primaryKey = columns;
            else if (lower.contains(QLatin1String("unique")))
                table.uniqueKeys.append(columns);
            continue;
        }

        const QString column = unquoted(part.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0));
        table.columns.append(column);
        if (lower.contains(QLatin1String("primary key"))) {
            const QString type = lower.section(QRegularExpression(QStringLiteral("\\s+")), 1, 1);
            if (type == QLatin1String("integer") && !table.withoutRowid)
                table.rowidAlias = column;
            table.primaryKey = { column };
        }
        else if (lower.contains(QRegularExpression(QStringLiteral("\\bunique\\b")))) {
            table.uniqueKeys.append({ column });
        }
    }
    return table;
}

IndexInfo parseCreateIndex(const QString& name, const QString& tableName, quint32 rootPage, const QString& sql)
{
    IndexInfo index;
    index.name = name.toLower();
    index.table = tableName.toLower();
    index.rootPage = rootPage;
    const int on = sql.indexOf(QRegularExpression(QStringLiteral("\\bon\\b"), QRegularExpression::CaseInsensitiveOption));
    for (const QString& column : columnList(parenthesised(sql, qMax(0, on))))
        index.columns.append(column);
    return index;
}

/// The parts of `CREATE VIEW tiles AS SELECT a.x AS x, ... FROM A [AS] a JOIN B [AS] b ON a.k = b.k`
/// that a tile lookup needs.
struct ViewJoin {
    bool valid = false;
    QHash<QString, QPair<QString, QString>> outputs;  ///< output column -> (table, column)
    QString leftTable;
    QString leftColumn;
    QString rightTable;
    QString rightColumn;
};

ViewJoin parseJoinView(const QString& sql)
{
    ViewJoin view;
    QString text = sql;
    text.replace(QRegularExpression(QStringLiteral("[\"`\\[\\]]")), QString());
    text = text.simplified();

    const QRegularExpression selectPattern(QStringLiteral("\\bselect\\s+(.*)\\s+from\\s+(.*)$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch select = selectPattern.match(text);
    if (!select.hasMatch())
        return view;

    // FROM A [AS] a [INNER|LEFT] JOIN B [AS] b ON x.c = y.d   |   FROM A a, B b WHERE x.c = y.d
    const QString from = select.captured(2);
    const QRegularExpression joinPattern(
        QStringLiteral("^(\\w+)(?:\\s+(?:as\\s+)?(?!inner\\b|left\\b|cross\\b|join\\b)(\\w+))?\\s*"
                       "(?:(?:inner\\s+|left\\s+(?:outer\\s+)?|cross\\s+)?join|,)\\s*"
                       "(\\w+)(?:\\s+(?:as\\s+)?(?!on\\b|where\\b)(\\w+))?\\s+(?:on|where)\\s+"
                       "(\\w+)\\.(\\w+)\\s*=\\s*(\\w+)\\.(\\w+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch join = joinPattern.match(from);
    if (!join.hasMatch())
        return view;

    QHash<QString, QString> aliases;
    const QString tableA = join.captured(1).toLower();
    const QString tableB = join.captured(3).toLower();
    aliases.insert(tableA, tableA);
    aliases.insert(tableB, tableB);
    if (!join.captured(2).isEmpty())
        aliases.insert(join.captured(2).toLower(), tableA);
    if (!join.captured(4).isEmpty())
        aliases.insert(join.captured(4).toLower(), tableB);

    const QString leftAlias = join.captured(5).toLower();
    const QString rightAlias = join.captured(7).toLower();
    if (!aliases.contains(leftAlias) || !aliases.contains(rightAlias))
        return view;
    view.leftTable = aliases.value(leftAlias);
    view.leftColumn = join.captured(6).toLower();
    view.rightTable = aliases.value(rightAlias);
    view.rightColumn = join.captured(8).toLower();

    const QRegularExpression columnPattern(QStringLiteral("^(\\w+)\\.(\\w+)(?:\\s+(?:as\\s+)?(\\w+))?$"),
                                           QRegularExpression::CaseInsensitiveOption);
    for (const QString& item : splitTopLevel(select.captured(1))) {
        const QRegularExpressionMatch column = columnPattern.match(item.trimmed());
        if (!column.hasMatch() || !aliases.contains(column.captured(1).toLower()))
            continue;
        const QString output = (column.captured(3).isEmpty() ? column.captured(2) : column.captured(3)).toLower();
        view.outputs.insert(output, { aliases.value(column.captured(1).toLower()), column.captured(2).toLower() });
    }
    view.valid = view.outputs.contains(QStringLiteral("tile_data"))
        && view.outputs.contains(QStringLiteral("zoom_level"));
    return view;
}

// ── Database ────────────────────────────────────────────────────────────────

class RemoteDatabase
{
public:
    explicit RemoteDatabase(const QUrl& url)
        : m_file(url)
    {
    }

    bool open(QString* errorMessage)
    {
        QMutexLocker lock(&m_openMutex);
        if (m_opened)
            return true;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastFailureMs > 0 && now - m_lastFailureMs < kOpenRetryMs) {
            setError(errorMessage, m_lastError);
            return false;
        }
        QString error;
        if (openNow(&error)) {
            m_opened = true;
            return true;
        }
        m_lastFailureMs = now;
        m_lastError = error;
        setError(errorMessage, error);
        return false;
    }

    QHash<QString, QString> metadata(QString* errorMessage)
    {
        QHash<QString, QString> result;
        const auto it = m_tables.constFind(QStringLiteral("metadata"));
        if (it == m_tables.cend()) {
            setError(errorMessage, QStringLiteral("%1 has no metadata table").arg(m_file.url().toString()));
            return result;
        }
        const QStringList stored = it->storedColumns();
        const int nameField = stored.indexOf(QStringLiteral("name"));
        const int valueField = stored.indexOf(QStringLiteral("value"));
        QVector<Record> rows;
        if (!scanTable(it->rootPage, it->withoutRowid, &rows, errorMessage))
            return result;
        for (const Record& row : rows) {
            if (nameField < 0 || valueField < 0 || nameField >= row.size() || valueField >= row.size())
                continue;
            result.insert(textOf(row.at(nameField)), textOf(row.at(valueField)));
        }
        return result;
    }

    QByteArray tile(int z, int x, int tileRow, QString* errorMessage)
    {
        const Record key{ SqlValue::fromInteger(z), SqlValue::fromInteger(x), SqlValue::fromInteger(tileRow) };
        Record row;
        if (!m_tilesTable.isEmpty()) {
            if (!findRow(m_tables.value(m_tilesTable), m_tileKeyColumns, key, &row, errorMessage))
                return QByteArray();
            return fieldOf(m_tables.value(m_tilesTable), row, m_tileDataColumn).bytes;
        }

        // Join: the tile's row in the map table, then its data by the join column.
        const TableInfo& mapTable = m_tables.value(m_join.leftTable);
        if (!findRow(mapTable, m_tileKeyColumns, key, &row, errorMessage))
            return QByteArray();
        const SqlValue joinValue = fieldOf(mapTable, row, m_join.leftColumn);
        const TableInfo& dataTable = m_tables.value(m_join.rightTable);
        Record dataRow;
        if (!findRow(dataTable, { m_join.rightColumn }, { joinValue }, &dataRow, errorMessage))
            return QByteArray();
        return fieldOf(dataTable, dataRow, m_tileDataColumn).bytes;
    }

private:
    bool openNow(QString* errorMessage)
    {
        QByteArray header;
        if (!m_file.read(0, 100, &header, errorMessage, kOpenTimeoutMs))
            return false;
        if (!header.startsWith(QByteArrayLiteral("SQLite format 3\0"))) {
            setError(errorMessage, QStringLiteral("%1 is not an SQLite (MBTiles) file").arg(m_file.url().toString()));
            return false;
        }
        const quint16 pageSize = be16(header.constData() + 16);
        m_pageSize = pageSize == 1 ? 65536 : pageSize;
        m_usableSize = m_pageSize - static_cast<quint8>(header.at(20));
        m_textEncoding = be32(header.constData() + 56);
        if (m_pageSize < 512 || m_usableSize < 480) {
            setError(errorMessage, QStringLiteral("%1 has an invalid SQLite header").arg(m_file.url().toString()));
            return false;
        }

        QVector<Record> schema;
        if (!scanTable(1, false, &schema, errorMessage))
            return false;

        QHash<QString, QString> views;
        QList<QPair<QString, quint32>> autoIndexes;  // (name, root) of sqlite_autoindex_*
        QHash<QString, QString> autoIndexTables;
        for (const Record& row : schema) {
            if (row.size() < 5)
                continue;
            const QString type = textOf(row.at(0));
            const QString name = textOf(row.at(1));
            const QString tableName = textOf(row.at(2));
            const quint32 root = row.at(3).type == SqlValue::Integer ? static_cast<quint32>(row.at(3).integer) : 0;
            const QString sql = textOf(row.at(4));
            if (type == QLatin1String("table")) {
                m_tables.insert(name.toLower(), parseCreateTable(name, root, sql));
            }
            else if (type == QLatin1String("index")) {
                if (sql.isEmpty()) {
                    autoIndexes.append({ name.toLower(), root });
                    autoIndexTables.insert(name.toLower(), tableName.toLower());
                }
                else {
                    m_indexes.append(parseCreateIndex(name, tableName, root, sql));
                }
            }
            else if (type == QLatin1String("view")) {
                views.insert(name.toLower(), sql);
            }
        }

        // Constraint indexes have no SQL: sqlite_autoindex_<table>_<n> covers the table's n-th
        // PRIMARY KEY / UNIQUE constraint, in declaration order.
        for (const auto& autoIndex : autoIndexes) {
            const TableInfo table = m_tables.value(autoIndexTables.value(autoIndex.first));
            QList<QStringList> keys;
            if (!table.primaryKey.isEmpty() && table.rowidAlias.isEmpty() && !table.withoutRowid)
                keys.append(table.primaryKey);
            keys.append(table.uniqueKeys);
            const int n = autoIndex.first.section('_', -1).toInt();
            if (n >= 1 && n <= keys.size())
                m_indexes.append({ autoIndex.first, table.name, autoIndex.second, keys.at(n - 1) });
        }

        m_tileKeyColumns = { QStringLiteral("zoom_level"), QStringLiteral("tile_column"), QStringLiteral("tile_row") };
        m_tileDataColumn = QStringLiteral("tile_data");
        if (m_tables.contains(QStringLiteral("tiles"))) {
            m_tilesTable = QStringLiteral("tiles");
            return true;
        }
        if (!views.contains(QStringLiteral("tiles"))) {
            setError(errorMessage, QStringLiteral("%1 has no tiles table or view").arg(m_file.url().toString()));
            return false;
        }

        m_join = parseJoinView(views.value(QStringLiteral("tiles")));
        if (!m_join.valid || !m_tables.contains(m_join.leftTable) || !m_tables.contains(m_join.rightTable)) {
            setError(errorMessage, QStringLiteral("%1: unsupported tiles view").arg(m_file.url().toString()));
            return false;
        }
        // Map the view's columns onto the joined tables; the map side is the one with zoom_level.
        if (m_join.outputs.value(QStringLiteral("zoom_level")).first != m_join.leftTable) {
            std::swap(m_join.leftTable, m_join.rightTable);
            std::swap(m_join.leftColumn, m_join.rightColumn);
        }
        m_tileKeyColumns = { m_join.outputs.value(QStringLiteral("zoom_level")).second,
                             m_join.outputs.value(QStringLiteral("tile_column"), { QString(), QStringLiteral("tile_column") }).second,
                             m_join.outputs.value(QStringLiteral("tile_row"), { QString(), QStringLiteral("tile_row") }).second };
        m_tileDataColumn = m_join.outputs.value(QStringLiteral("tile_data")).second;
        return true;
    }

    QString textOf(const SqlValue& value) const
    {
        if (value.type == SqlValue::Integer)
            return QString::number(value.integer);
        if (value.type == SqlValue::Real)
            return QString::number(value.real);
        if (m_textEncoding == 2)
            return QString::fromUtf16(reinterpret_cast<const char16_t*>(value.bytes.constData()), value.bytes.size() / 2);
        if (m_textEncoding == 3) {
            QString text;
            for (int i = 0; i + 1 < value.bytes.size(); i += 2)
                text += QChar(static_cast<char16_t>((static_cast<quint8>(value.bytes.at(i)) << 8) | static_cast<quint8>(value.bytes.at(i + 1))));
            return text;
        }
        return QString::fromUtf8(value.bytes);
    }

    SqlValue fieldOf(const TableInfo& table, const Record& row, const QString& column) const
    {
        if (!table.withoutRowid && column == table.rowidAlias)
            return row.value(row.size() - 1);  // the rowid, appended by findRow
        const int field = table.storedColumns().indexOf(column);
        return field >= 0 && field < row.size() ? row.at(field) : SqlValue();
    }

    /// The row of @p table whose @p columns equal @p key. For rowid tables the rowid is appended
    /// to the returned row.
    bool findRow(const TableInfo& table, const QStringList& columns, const Record& key, Record* row,
                 QString* errorMessage)
    {
        // WITHOUT ROWID: the table is an index B-tree on its primary key.
        if (table.withoutRowid && table.primaryKey.mid(0, columns.size()) == columns)
            return seekIndex(table.rootPage, key, row, errorMessage);

        if (!table.withoutRowid && columns.size() == 1 && columns.first() == table.rowidAlias) {
            if (key.first().type != SqlValue::Integer)
                return false;
            if (!seekRowid(table.rootPage, key.first().integer, row, errorMessage))
                return false;
            row->append(key.first());
            return true;
        }

        for (const IndexInfo& index : std::as_const(m_indexes)) {
            if (index.table != table.name || index.columns.mid(0, columns.size()) != columns)
                continue;
            Record entry;
            if (!seekIndex(index.rootPage, key, &entry, errorMessage))
                return false;
            if (table.withoutRowid) {
                const Record primaryKey = entry.mid(index.columns.size());
                return seekIndex(table.rootPage, primaryKey, row, errorMessage);
            }
            if (entry.isEmpty() || entry.last().type != SqlValue::Integer)
                return false;
            const SqlValue rowid = entry.last();
            if (!seekRowid(table.rootPage, rowid.integer, row, errorMessage))
                return false;
            row->append(rowid);
            return true;
        }
        setError(errorMessage, QStringLiteral("%1: no index on %2(%3)")
                                   .arg(m_file.url().toString(), table.name, columns.join(',')));
        return false;
    }

    bool page(quint32 number, QByteArray* data, QString* errorMessage)
    {
        if (number == 0) {
            setError(errorMessage, QStringLiteral("%1: corrupt page reference").arg(m_file.url().toString()));
            return false;
        }
        return m_file.read(static_cast<qint64>(number - 1) * m_pageSize, m_pageSize, data, errorMessage);
    }

    /// Full payload of the cell at @p cell (pointing at the payload-size varint, or at the
    /// rowid for table leaves via @p skipRowid).
    bool cellPayload(const QByteArray& pageData, int cell, bool tableLeaf, QByteArray* payload,
                     qint64* rowid, QString* errorMessage)
    {
        const char* end = pageData.constData() + pageData.size();
        const char* p = pageData.constData() + cell;
        quint64 payloadSize = 0;
        int used = sqliteVarint(p, end, &payloadSize);
        if (used == 0)
            return false;
        p += used;
        if (tableLeaf) {
            quint64 id = 0;
            used = sqliteVarint(p, end, &id);
            if (used == 0)
                return false;
            p += used;
            if (rowid)
                *rowid = static_cast<qint64>(id);
        }

        const qint64 u = m_usableSize;
        const qint64 maxLocal = tableLeaf ? u - 35 : ((u - 12) * 64 / 255) - 23;
        const qint64 minLocal = ((u - 12) * 32 / 255) - 23;
        const qint64 total = static_cast<qint64>(payloadSize);
        qint64 local = total;
        if (total > maxLocal) {
            const qint64 k = minLocal + ((total - minLocal) % (u - 4));
            local = k <= maxLocal ? k : minLocal;
        }
        if (p + local > end)
            return false;
        payload->resize(total);
        std::memcpy(payload->data(), p, static_cast<size_t>(local));
        if (local == total)
            return true;

        quint32 overflow = be32(p + local);
        qint64 done = local;
        QByteArray overflowPage;
        while (done < total) {
            if (!page(overflow, &overflowPage, errorMessage))
                return false;
            const qint64 take = qMin(total - done, u - 4);
            std::memcpy(payload->data() + done, overflowPage.constData() + 4, static_cast<size_t>(take));
            done += take;
            overflow = be32(overflowPage.constData());
        }
        return true;
    }

    struct PageHeader {
        quint8 type = 0;
        int cellCount = 0;
        int cellPointers = 0;
        quint32 rightMost = 0;
    };

    bool pageHeader(quint32 number, const QByteArray& data, PageHeader* header)
    {
        const int offset = number == 1 ? 100 : 0;
        if (data.size() < offset + 12)
            return false;
        const char* p = data.constData() + offset;
        header->type = static_cast<quint8>(p[0]);
        header->cellCount = be16(p + 3);
        const bool interior = header->type == InteriorIndex || header->type == InteriorTable;
        header->cellPointers = offset + (interior ? 12 : 8);
        header->rightMost = interior ? be32(p + 8) : 0;
        return header->type == InteriorIndex || header->type == InteriorTable
            || header->type == LeafIndex || header->type == LeafTable;
    }

    int cellOffset(const QByteArray& data, const PageHeader& header, int i) const
    {
        return be16(data.constData() + header.cellPointers + 2 * i);
    }

    bool seekRowid(quint32 root, qint64 rowid, Record* row, QString* errorMessage)
    {
        quint32 number = root;
        QByteArray data;
        for (int depth = 0; depth < 40; ++depth) {
            PageHeader header;
            if (!page(number, &data, errorMessage) || !pageHeader(number, data, &header))
                return false;
            if (header.type == LeafTable) {
                int lo = 0;
                int hi = header.cellCount - 1;
                while (lo <= hi) {
                    const int mid = (lo + hi) / 2;
                    const char* p = data.constData() + cellOffset(data, header, mid);
                    const char* end = data.constData() + data.size();
                    quint64 skip = 0;
                    quint64 id = 0;
                    const int used = sqliteVarint(p, end, &skip);
                    if (used == 0 || sqliteVarint(p + used, end, &id) == 0)
                        return false;
                    const qint64 cellRowid = static_cast<qint64>(id);
                    if (cellRowid == rowid) {
                        QByteArray payload;
                        if (!cellPayload(data, cellOffset(data, header, mid), true, &payload, nullptr, errorMessage))
                            return false;
                        return decodeRecord(payload, row);
                    }
                    if (cellRowid < rowid)
                        lo = mid + 1;
                    else
                        hi = mid - 1;
                }
                return false;
            }
            if (header.type != InteriorTable)
                return false;

            // First cell whose key >= rowid holds it in its left child; else the right-most.
            int lo = 0;
            int hi = header.cellCount;
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                const char* p = data.constData() + cellOffset(data, header, mid);
                quint64 key = 0;
                if (sqliteVarint(p + 4, data.constData() + data.size(), &key) == 0)
                    return false;
                if (static_cast<qint64>(key) < rowid)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            number = lo < header.cellCount ? be32(data.constData() + cellOffset(data, header, lo))
                                           : header.rightMost;
        }
        return false;
    }

    bool seekIndex(quint32 root, const Record& key, Record* entry, QString* errorMessage)
    {
        quint32 number = root;
        QByteArray data;
        for (int depth = 0; depth < 40; ++depth) {
            PageHeader header;
            if (!page(number, &data, errorMessage) || !pageHeader(number, data, &header))
                return false;
            const bool interior = header.type == InteriorIndex;
            if (!interior && header.type != LeafIndex)
                return false;

            int lo = 0;
            int hi = header.cellCount;
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                const int cell = cellOffset(data, header, mid) + (interior ? 4 : 0);
                QByteArray payload;
                Record record;
                if (!cellPayload(data, cell, false, &payload, nullptr, errorMessage) || !decodeRecord(payload, &record))
                    return false;
                const int c = comparePrefix(key, record);
                if (c == 0) {
                    *entry = record;
                    return true;
                }
                if (c > 0)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (!interior)
                return false;
            number = lo < header.cellCount ? be32(data.constData() + cellOffset(data, header, lo))
                                           : header.rightMost;
        }
        return false;
    }

    /// Every row of a (small) table, in key order.
    bool scanTable(quint32 root, bool indexTree, QVector<Record>* rows, QString* errorMessage, int depth = 0)
    {
        if (depth > 40)
            return false;
        QByteArray data;
        PageHeader header;
        if (!page(root, &data, errorMessage) || !pageHeader(root, data, &header))
            return false;
        for (int i = 0; i < header.cellCount; ++i) {
            const int cell = cellOffset(data, header, i);
            if (header.type == InteriorTable || header.type == InteriorIndex) {
                if (!scanTable(be32(data.constData() + cell), indexTree, rows, errorMessage, depth + 1))
                    return false;
                if (header.type == InteriorTable)
                    continue;
            }
            QByteArray payload;
            Record record;
            const bool tableLeaf = header.type == LeafTable;
            const int payloadAt = header.type == InteriorIndex ? cell + 4 : cell;
            if (!cellPayload(data, payloadAt, tableLeaf, &payload, nullptr, errorMessage) || !decodeRecord(payload, &record))
                return false;
            rows->append(record);
        }
        if (header.rightMost != 0)
            return scanTable(header.rightMost, indexTree, rows, errorMessage, depth + 1);
        return true;
    }

    RemoteFile m_file;
    QMutex m_openMutex;
    bool m_opened = false;
    qint64 m_lastFailureMs = 0;
    QString m_lastError;
    qint64 m_pageSize = 4096;
    qint64 m_usableSize = 4096;
    quint32 m_textEncoding = 1;
    // Schema; written once by open(), read-only afterwards.
    QHash<QString, TableInfo> m_tables;
    QList<IndexInfo> m_indexes;
    QString m_tilesTable;
    ViewJoin m_join;
    QStringList m_tileKeyColumns;
    QString m_tileDataColumn;
};

QMutex& registryMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, std::shared_ptr<RemoteDatabase>>& registry()
{
    static QHash<QString, std::shared_ptr<RemoteDatabase>> databases;
    return databases;
}

std::shared_ptr<RemoteDatabase> database(const QString& url)
{
    QMutexLocker lock(&registryMutex());
    std::shared_ptr<RemoteDatabase>& entry = registry()[url];
    if (!entry)
        entry = std::make_shared<RemoteDatabase>(QUrl(url));
    return entry;
}

} // namespace

bool RemoteMbTiles::isRemoteUrl(const QString& location)
{
    const QString trimmed = location.trimmed();
    return trimmed.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
}

QHash<QString, QString> RemoteMbTiles::readMetadata(const QString& url, QString* errorMessage)
{
    const std::shared_ptr<RemoteDatabase> db = database(url.trimmed());
    if (!db->open(errorMessage))
        return {};
    return db->metadata(errorMessage);
}

QByteArray RemoteMbTiles::readTile(const QString& url, int z, int x, int tileRow, QString* errorMessage)
{
    const std::shared_ptr<RemoteDatabase> db = database(url.trimmed());
    if (!db->open(errorMessage))
        return QByteArray();
    return db->tile(z, x, tileRow, errorMessage);
}

QByteArray RemoteMbTiles::httpGet(const QString& url, int timeoutMs, int* httpStatus, QString* errorMessage,
                                  const QByteArray& range)
{
    const HttpResult result = blockingGet(QUrl(url.trimmed()), range, timeoutMs, range.isEmpty() ? 0 : kWholeFileLimit);
    if (httpStatus)
        *httpStatus = result.status;
    if (!result.error.isEmpty()) {
        setError(errorMessage, result.error);
        return QByteArray();
    }
    return result.body;
}

void RemoteMbTiles::clearCaches()
{
    QMutexLocker lock(&registryMutex());
    registry().clear();
}
