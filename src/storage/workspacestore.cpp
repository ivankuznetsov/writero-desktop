#include "storage/workspacestore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

namespace writero {

namespace {

QString toJson(const QVariantMap &map)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
}

QVariantMap fromJson(const QString &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    return document.object().toVariantMap();
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

/// A default-constructed QString binds as SQL NULL; NOT NULL columns need an
/// explicit empty string instead.
QVariant nonNull(const QString &value)
{
    return value.isNull() ? QVariant(QString::fromLatin1("")) : QVariant(value);
}

} // namespace

WorkspaceStore::WorkspaceStore()
    : m_connectionName(QStringLiteral("writero-workspace-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

WorkspaceStore::~WorkspaceStore()
{
    close();
}

bool WorkspaceStore::open(const QString &databasePath, QString *error)
{
    close();

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        if (error)
            *error = m_lastError;
        return false;
    }

    {
        QSqlQuery pragma(m_database);
        pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
        pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
        pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    }

    if (!migrate()) {
        if (error)
            *error = m_lastError;
        close();
        return false;
    }
    return true;
}

void WorkspaceStore::close()
{
    if (m_database.isOpen())
        m_database.close();
    m_database = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

bool WorkspaceStore::isOpen() const
{
    return m_database.isOpen();
}

bool WorkspaceStore::migrate()
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA user_version"))) {
        m_lastError = query.lastError().text();
        return false;
    }
    query.next();
    int version = query.value(0).toInt();

    if (version >= 5)
        return true;

    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }

    if (version < 1) {
        const QStringList statements = {
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS documents ("
                " id TEXT PRIMARY KEY,"
                " title TEXT NOT NULL DEFAULT '',"
                " revision INTEGER NOT NULL DEFAULT 0,"
                " created_at TEXT NOT NULL,"
                " updated_at TEXT NOT NULL,"
                " cloud_id TEXT,"
                " cloud_state TEXT NOT NULL DEFAULT 'local',"
                " trashed_at TEXT)"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS media ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " sha256 TEXT NOT NULL UNIQUE,"
                " filename TEXT NOT NULL,"
                " mime_type TEXT NOT NULL,"
                " byte_size INTEGER NOT NULL,"
                " created_at TEXT NOT NULL)"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS blocks ("
                " id TEXT PRIMARY KEY,"
                " document_id TEXT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                " position INTEGER NOT NULL,"
                " block_type TEXT NOT NULL,"
                " content TEXT NOT NULL DEFAULT '',"
                " metadata TEXT NOT NULL DEFAULT '{}',"
                " media_id INTEGER REFERENCES media(id) ON DELETE SET NULL,"
                " revision INTEGER NOT NULL DEFAULT 1)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_blocks_document"
                           " ON blocks(document_id, position)"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS revisions ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " document_id TEXT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                " block_id TEXT NOT NULL,"
                " event TEXT NOT NULL,"
                " source TEXT NOT NULL DEFAULT 'local',"
                " content TEXT,"
                " block_type TEXT,"
                " metadata TEXT,"
                " created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_revisions_document"
                           " ON revisions(document_id, block_id, created_at)"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS media_versions ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " block_id TEXT NOT NULL,"
                " media_id INTEGER NOT NULL REFERENCES media(id) ON DELETE CASCADE,"
                " created_at TEXT NOT NULL)"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS pending_operations ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " document_id TEXT NOT NULL,"
                " operation_id TEXT NOT NULL UNIQUE,"
                " kind TEXT NOT NULL,"
                " payload TEXT NOT NULL,"
                " created_at TEXT NOT NULL,"
                " state TEXT NOT NULL DEFAULT 'pending')"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS settings ("
                " key TEXT PRIMARY KEY,"
                " value TEXT NOT NULL)"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                m_lastError = query.lastError().text();
                m_database.rollback();
                return false;
            }
        }
        if (!query.exec(QStringLiteral("PRAGMA user_version = 1"))) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return false;
        }
        version = 1;
    }

    if (version < 2) {
        const QStringList statements = {
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS ai_results ("
                " id TEXT PRIMARY KEY,"
                " document_id TEXT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                " block_id TEXT NOT NULL,"
                " kind TEXT NOT NULL,"
                " provider_id TEXT NOT NULL DEFAULT '',"
                " model TEXT NOT NULL DEFAULT '',"
                " prompt TEXT NOT NULL DEFAULT '',"
                " status TEXT NOT NULL DEFAULT 'pending',"
                " content TEXT NOT NULL DEFAULT '',"
                " error TEXT NOT NULL DEFAULT '',"
                " base_revision INTEGER NOT NULL DEFAULT 0,"
                " batch_id TEXT NOT NULL DEFAULT '',"
                " created_at TEXT NOT NULL)"),
            QStringLiteral("CREATE INDEX IF NOT EXISTS idx_ai_results_block"
                           " ON ai_results(document_id, block_id, created_at)"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                m_lastError = query.lastError().text();
                m_database.rollback();
                return false;
            }
        }
        if (!query.exec(QStringLiteral("PRAGMA user_version = 2"))) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return false;
        }
        version = 2;
    }

    if (version < 3) {
        const QStringList statements = {
            QStringLiteral("ALTER TABLE documents ADD COLUMN sync_cursor INTEGER NOT NULL DEFAULT 0"),
            QStringLiteral("ALTER TABLE documents ADD COLUMN feed_generation INTEGER NOT NULL DEFAULT 0"),
            QStringLiteral("ALTER TABLE documents ADD COLUMN sync_title_version INTEGER NOT NULL DEFAULT 0"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS sync_block_map ("
                " document_id TEXT NOT NULL,"
                " local_id TEXT NOT NULL,"
                " remote_id TEXT NOT NULL,"
                " remote_version INTEGER NOT NULL DEFAULT 0,"
                " PRIMARY KEY (document_id, local_id),"
                " UNIQUE (document_id, remote_id))"),
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS sync_conflicts ("
                " id TEXT PRIMARY KEY,"
                " document_id TEXT NOT NULL,"
                " block_id TEXT,"
                " kind TEXT NOT NULL,"
                " base_content TEXT NOT NULL DEFAULT '',"
                " local_content TEXT NOT NULL DEFAULT '',"
                " remote_content TEXT NOT NULL DEFAULT '',"
                " remote_sequence INTEGER NOT NULL DEFAULT 0,"
                " remote_lock_version INTEGER NOT NULL DEFAULT 0,"
                " created_at TEXT NOT NULL,"
                " resolved_at TEXT)"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                m_lastError = query.lastError().text();
                m_database.rollback();
                return false;
            }
        }
        if (!query.exec(QStringLiteral("PRAGMA user_version = 3"))) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return false;
        }
        version = 3;
    }

    if (version < 4) {
        const QStringList statements = {
            QStringLiteral("ALTER TABLE ai_results ADD COLUMN operation_id TEXT NOT NULL DEFAULT ''"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                m_lastError = query.lastError().text();
                m_database.rollback();
                return false;
            }
        }
        if (!query.exec(QStringLiteral("PRAGMA user_version = 4"))) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return false;
        }
        version = 4;
    }

    if (version < 5) {
        const QStringList statements = {
            QStringLiteral("ALTER TABLE ai_results ADD COLUMN remote_id TEXT NOT NULL DEFAULT ''"),
            QStringLiteral("ALTER TABLE documents ADD COLUMN sync_account_email TEXT NOT NULL DEFAULT ''"),
        };
        for (const QString &statement : statements) {
            if (!query.exec(statement)) {
                m_lastError = query.lastError().text();
                m_database.rollback();
                return false;
            }
        }
        if (!query.exec(QStringLiteral("PRAGMA user_version = 5"))) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return false;
        }
    }

    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool WorkspaceStore::createDocument(const Document &document, QString *error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO documents (id, title, revision, created_at, updated_at, cloud_id, cloud_state)"
        " VALUES (:id, :title, :revision, :created_at, :updated_at, :cloud_id, :cloud_state)"));
    query.bindValue(QStringLiteral(":id"), document.id);
    query.bindValue(QStringLiteral(":title"), document.title);
    query.bindValue(QStringLiteral(":revision"), document.revision);
    query.bindValue(QStringLiteral(":created_at"), nowIso());
    query.bindValue(QStringLiteral(":updated_at"), nowIso());
    query.bindValue(QStringLiteral(":cloud_id"), document.cloudId);
    query.bindValue(QStringLiteral(":cloud_state"), document.cloudState);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        if (error)
            *error = m_lastError;
        return false;
    }
    return true;
}

bool WorkspaceStore::saveDocument(const Document &document, const QVector<DocumentChange> &changes,
                                  const QVector<PendingOperation> &pendingOperations,
                                  QString *error, const QVector<Revision> &importedRevisions)
{
    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        if (error)
            *error = m_lastError;
        return false;
    }

    QSqlQuery query(m_database);

    const QString timestamp = nowIso();
    query.prepare(QStringLiteral(
        "UPDATE documents SET title = :title, revision = :revision, updated_at = :updated_at,"
        " cloud_id = :cloud_id, cloud_state = :cloud_state, sync_cursor = :sync_cursor,"
        " feed_generation = :feed_generation, sync_title_version = :sync_title_version,"
        " sync_account_email = :sync_account_email WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), document.id);
    query.bindValue(QStringLiteral(":title"), document.title);
    query.bindValue(QStringLiteral(":revision"), document.revision);
    query.bindValue(QStringLiteral(":updated_at"), timestamp);
    query.bindValue(QStringLiteral(":cloud_id"), document.cloudId);
    query.bindValue(QStringLiteral(":cloud_state"), document.cloudState);
    query.bindValue(QStringLiteral(":sync_cursor"), document.syncCursor);
    query.bindValue(QStringLiteral(":feed_generation"), document.feedGeneration);
    query.bindValue(QStringLiteral(":sync_title_version"), document.syncTitleVersion);
    query.bindValue(QStringLiteral(":sync_account_email"), nonNull(document.syncAccountEmail));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        m_database.rollback();
        if (error)
            *error = m_lastError;
        return false;
    }

    if (query.numRowsAffected() == 0) {
        query.prepare(QStringLiteral(
            "INSERT INTO documents (id, title, revision, created_at, updated_at, cloud_id,"
            " cloud_state, sync_cursor, feed_generation, sync_title_version,"
            " sync_account_email) VALUES (:id, :title, :revision, :created_at, :updated_at,"
            " :cloud_id, :cloud_state, :sync_cursor, :feed_generation, :sync_title_version,"
            " :sync_account_email)"));
        query.bindValue(QStringLiteral(":id"), document.id);
        query.bindValue(QStringLiteral(":title"), document.title);
        query.bindValue(QStringLiteral(":revision"), document.revision);
        query.bindValue(QStringLiteral(":created_at"), timestamp);
        query.bindValue(QStringLiteral(":updated_at"), timestamp);
        query.bindValue(QStringLiteral(":cloud_id"), document.cloudId);
        query.bindValue(QStringLiteral(":cloud_state"), document.cloudState);
        query.bindValue(QStringLiteral(":sync_cursor"), document.syncCursor);
        query.bindValue(QStringLiteral(":feed_generation"), document.feedGeneration);
        query.bindValue(QStringLiteral(":sync_title_version"), document.syncTitleVersion);
    query.bindValue(QStringLiteral(":sync_account_email"), nonNull(document.syncAccountEmail));
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            if (error)
                *error = m_lastError;
            return false;
        }
    }

    query.prepare(QStringLiteral("DELETE FROM blocks WHERE document_id = :document_id"));
    query.bindValue(QStringLiteral(":document_id"), document.id);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        m_database.rollback();
        if (error)
            *error = m_lastError;
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT INTO blocks (id, document_id, position, block_type, content, metadata, media_id,"
        " revision) VALUES (:id, :document_id, :position, :block_type, :content, :metadata,"
        " :media_id, :revision)"));
    for (int position = 0; position < document.blocks.size(); ++position) {
        const Block &block = document.blocks.at(position);
        query.bindValue(QStringLiteral(":id"), block.id);
        query.bindValue(QStringLiteral(":document_id"), document.id);
        query.bindValue(QStringLiteral(":position"), position);
        query.bindValue(QStringLiteral(":block_type"), blocktype::toKey(block.type));
        // A default-constructed QString binds as NULL, which violates the
        // NOT NULL constraint; empty content must be stored as an empty string.
        query.bindValue(QStringLiteral(":content"),
                        block.content.isNull() ? QString::fromLatin1("") : block.content);
        query.bindValue(QStringLiteral(":metadata"), toJson(block.metadata));
        query.bindValue(QStringLiteral(":media_id"), block.mediaId > 0 ? block.mediaId
                                                                       : QVariant());
        query.bindValue(QStringLiteral(":revision"), block.revision);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            if (error)
                *error = m_lastError;
            return false;
        }
    }

    if (!pendingOperations.isEmpty()) {
        QSqlQuery pending(m_database);
        pending.prepare(QStringLiteral(
            "INSERT INTO pending_operations (document_id, operation_id, kind, payload,"
            " created_at, state) VALUES (:document_id, :operation_id, :kind, :payload,"
            " :created_at, 'pending') ON CONFLICT(operation_id) DO NOTHING"));
        for (const PendingOperation &operation : pendingOperations) {
            pending.bindValue(QStringLiteral(":document_id"), document.id);
            pending.bindValue(QStringLiteral(":operation_id"), operation.operationId);
            pending.bindValue(QStringLiteral(":kind"), operation.kind);
            pending.bindValue(QStringLiteral(":payload"),
                              QString::fromUtf8(QJsonDocument(operation.payload)
                                                    .toJson(QJsonDocument::Compact)));
            pending.bindValue(QStringLiteral(":created_at"), nowIso());
            if (!pending.exec()) {
                m_lastError = pending.lastError().text();
                m_database.rollback();
                if (error)
                    *error = m_lastError;
                return false;
            }
        }
    }

    for (const DocumentChange &change : changes) {
        if (!insertRevision(change, document.id, error)) {
            m_database.rollback();
            return false;
        }
    }

    for (const Revision &revision : importedRevisions) {
        if (!insertRevision(document.id, revision)) {
            m_database.rollback();
            if (error)
                *error = m_lastError;
            return false;
        }
    }

    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        m_database.rollback();
        if (error)
            *error = m_lastError;
        return false;
    }
    return true;
}

bool WorkspaceStore::insertRevision(const DocumentChange &change, const QString &documentId,
                                    QString *error)
{
    QSqlQuery query(m_database);

    const auto insertOne = [&](const QString &blockId, const QString &event, const QString &source,
                               const Block &block) -> bool {
        query.prepare(QStringLiteral(
            "INSERT INTO revisions (document_id, block_id, event, source, content, block_type,"
            " metadata, created_at) VALUES (:document_id, :block_id, :event, :source, :content,"
            " :block_type, :metadata, :created_at)"));
        query.bindValue(QStringLiteral(":document_id"), documentId);
        query.bindValue(QStringLiteral(":block_id"), blockId);
        query.bindValue(QStringLiteral(":event"), event);
        query.bindValue(QStringLiteral(":source"), source);
        query.bindValue(QStringLiteral(":content"), block.content);
        query.bindValue(QStringLiteral(":block_type"), blocktype::toKey(block.type));
        query.bindValue(QStringLiteral(":metadata"), toJson(block.metadata));
        query.bindValue(QStringLiteral(":created_at"), nowIso());
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            if (error)
                *error = m_lastError;
            return false;
        }
        return true;
    };

    switch (change.kind) {
    case DocumentChange::Kind::Title:
        return true; // Titles are not block history.
    case DocumentChange::Kind::InsertBlock:
        return insertOne(change.blockId, QStringLiteral("create"), change.source, change.afterBlock);
    case DocumentChange::Kind::RemoveBlock:
        return insertOne(change.blockId, QStringLiteral("destroy"), change.source, change.beforeBlock);
    case DocumentChange::Kind::UpdateBlock:
        return insertOne(change.blockId, QStringLiteral("update"), change.source, change.afterBlock);
    case DocumentChange::Kind::MoveBlock:
        return true; // Position changes are metadata, not content history.
    case DocumentChange::Kind::ReplaceAll: {
        QSet<QString> afterIds;
        for (const Block &block : change.afterBlocks) {
            afterIds.insert(block.id);
            if (!insertOne(block.id, QStringLiteral("update"), change.source, block))
                return false;
        }
        for (const Block &block : change.beforeBlocks) {
            if (afterIds.contains(block.id))
                continue;
            if (!insertOne(block.id, QStringLiteral("destroy"), change.source, block))
                return false;
        }
        return true;
    }
    }
    return true;
}

Document WorkspaceStore::loadDocument(const QString &documentId, QString *error)
{
    Document document;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id, title, revision, created_at, updated_at, cloud_id,"
                                 " cloud_state, sync_cursor, feed_generation, sync_title_version,"
                                 " sync_account_email FROM documents WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), documentId);
    if (!query.exec() || !query.next()) {
        m_lastError = query.lastError().text().isEmpty() ? QStringLiteral("Document not found")
                                                         : query.lastError().text();
        if (error)
            *error = m_lastError;
        return document;
    }

    document.id = query.value(0).toString();
    document.title = query.value(1).toString();
    document.revision = query.value(2).toLongLong();
    document.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
    document.updatedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
    document.cloudId = query.value(5).toString();
    document.cloudState = query.value(6).toString();
    document.syncCursor = query.value(7).toLongLong();
    document.feedGeneration = query.value(8).toLongLong();
    document.syncTitleVersion = query.value(9).toLongLong();
    document.syncAccountEmail = query.value(10).toString();

    QSqlQuery blocks(m_database);
    blocks.prepare(QStringLiteral(
        "SELECT id, block_type, content, metadata, media_id, revision FROM blocks"
        " WHERE document_id = :document_id ORDER BY position"));
    blocks.bindValue(QStringLiteral(":document_id"), documentId);
    if (!blocks.exec()) {
        m_lastError = blocks.lastError().text();
        if (error)
            *error = m_lastError;
        return document;
    }

    while (blocks.next()) {
        Block block;
        block.id = blocks.value(0).toString();
        block.type = blocktype::fromKey(blocks.value(1).toString());
        block.content = blocks.value(2).toString();
        block.metadata = fromJson(blocks.value(3).toString());
        block.mediaId = blocks.value(4).toLongLong();
        block.revision = blocks.value(5).toInt();
        document.blocks.append(block);
    }

    return document;
}

QVector<DocumentSummary> WorkspaceStore::listDocuments(bool trashed, const QString &queryText)
{
    QVector<DocumentSummary> result;

    QString sql = QStringLiteral(
        "SELECT d.id, d.title, d.created_at, d.updated_at, d.trashed_at, d.revision,"
        " (SELECT content FROM blocks b WHERE b.document_id = d.id AND b.content != ''"
        "  ORDER BY b.position LIMIT 1)"
        " FROM documents d");
    QStringList conditions;
    if (trashed)
        conditions << QStringLiteral("d.trashed_at IS NOT NULL");
    else
        conditions << QStringLiteral("d.trashed_at IS NULL");
    if (!queryText.isEmpty()) {
        conditions << QStringLiteral(
            "(d.title LIKE :query OR EXISTS (SELECT 1 FROM blocks b2 WHERE b2.document_id = d.id"
            " AND b2.content LIKE :query))");
    }
    sql += QStringLiteral(" WHERE ") + conditions.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY d.updated_at DESC");

    QSqlQuery query(m_database);
    query.prepare(sql);
    if (!queryText.isEmpty())
        query.bindValue(QStringLiteral(":query"), QStringLiteral("%") + queryText + QStringLiteral("%"));

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return result;
    }

    while (query.next()) {
        DocumentSummary summary;
        summary.id = query.value(0).toString();
        summary.title = query.value(1).toString();
        summary.createdAt = QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs);
        summary.updatedAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        summary.trashed = !query.value(4).isNull();
        summary.revision = query.value(5).toInt();
        const QString preview = query.value(6).toString().simplified();
        summary.preview = preview.size() > 100 ? preview.left(99) + QChar(0x2026) : preview;
        result.append(summary);
    }
    return result;
}

bool WorkspaceStore::setDocumentTrashed(const QString &documentId, bool trashed)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE documents SET trashed_at = :trashed WHERE id = :id"));
    query.bindValue(QStringLiteral(":trashed"), trashed ? QVariant(nowIso()) : QVariant());
    query.bindValue(QStringLiteral(":id"), documentId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool WorkspaceStore::deleteDocument(const QString &documentId)
{
    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        return false;
    }
    // Removed blocks remain in revisions so their retained media history must
    // also be collected before the document's cascading revision deletion.
    const QStringList statements = {
        QStringLiteral("DELETE FROM media_versions WHERE block_id IN ("
                       "SELECT id FROM blocks WHERE document_id = :id UNION "
                       "SELECT block_id FROM revisions WHERE document_id = :id) "
                       "AND block_id NOT IN (SELECT id FROM blocks WHERE document_id != :id "
                       "UNION SELECT block_id FROM revisions WHERE document_id != :id)"),
        QStringLiteral("DELETE FROM pending_operations WHERE document_id = :id"),
        QStringLiteral("DELETE FROM sync_block_map WHERE document_id = :id"),
        QStringLiteral("DELETE FROM sync_conflicts WHERE document_id = :id"),
        QStringLiteral("DELETE FROM documents WHERE id = :id"),
    };
    QSqlQuery query(m_database);
    for (const QString &statement : statements) {
        query.prepare(statement);
        query.bindValue(QStringLiteral(":id"), documentId);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_database.rollback();
            return false;
        }
    }
    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        m_database.rollback();
        return false;
    }
    return true;
}

bool WorkspaceStore::documentExists(const QString &documentId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT 1 FROM documents WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), documentId);
    if (!query.exec())
        return false;
    return query.next();
}

QVector<Revision> WorkspaceStore::revisions(const QString &documentId, const QString &blockId,
                                            int limit)
{
    QVector<Revision> result;

    QSqlQuery query(m_database);
    QString sql = QStringLiteral(
        "SELECT id, block_id, event, source, content, block_type, metadata, created_at"
        " FROM revisions WHERE document_id = :document_id");
    if (!blockId.isEmpty())
        sql += QStringLiteral(" AND block_id = :block_id");
    sql += QStringLiteral(" ORDER BY created_at DESC, id DESC LIMIT :limit");

    query.prepare(sql);
    query.bindValue(QStringLiteral(":document_id"), documentId);
    if (!blockId.isEmpty())
        query.bindValue(QStringLiteral(":block_id"), blockId);
    query.bindValue(QStringLiteral(":limit"), limit);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return result;
    }

    while (query.next()) {
        Revision revision;
        revision.id = query.value(0).toLongLong();
        revision.blockId = query.value(1).toString();
        revision.event = query.value(2).toString();
        revision.source = query.value(3).toString();
        revision.content = query.value(4).toString();
        revision.type = blocktype::fromKey(query.value(5).toString());
        revision.metadata = fromJson(query.value(6).toString());
        revision.createdAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs);
        result.append(revision);
    }
    return result;
}

bool WorkspaceStore::pruneRevisions(const QString &documentId, const QString &blockId, int keep)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "DELETE FROM revisions WHERE document_id = :document_id AND block_id = :block_id"
        " AND id NOT IN (SELECT id FROM revisions WHERE document_id = :document_id"
        " AND block_id = :block_id ORDER BY id DESC LIMIT :keep)"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":block_id"), blockId);
    query.bindValue(QStringLiteral(":keep"), keep);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool WorkspaceStore::insertRevision(const QString &documentId, const Revision &revision)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO revisions (document_id, block_id, event, source, content, block_type,"
        " metadata, created_at) VALUES (:document_id, :block_id, :event, :source, :content,"
        " :block_type, :metadata, :created_at)"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":block_id"), revision.blockId);
    query.bindValue(QStringLiteral(":event"), revision.event);
    query.bindValue(QStringLiteral(":source"), revision.source);
    query.bindValue(QStringLiteral(":content"), revision.content);
    query.bindValue(QStringLiteral(":block_type"), blocktype::toKey(revision.type));
    query.bindValue(QStringLiteral(":metadata"), toJson(revision.metadata));
    query.bindValue(QStringLiteral(":created_at"),
                    revision.createdAt.isValid()
                        ? revision.createdAt.toString(Qt::ISODateWithMs)
                        : nowIso());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

qint64 WorkspaceStore::ensureMedia(const QString &sha256, const QString &filename,
                                   const QString &mimeType, qint64 byteSize)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM media WHERE sha256 = :sha256"));
    query.bindValue(QStringLiteral(":sha256"), sha256);
    if (query.exec() && query.next())
        return query.value(0).toLongLong();

    query.prepare(QStringLiteral(
        "INSERT INTO media (sha256, filename, mime_type, byte_size, created_at)"
        " VALUES (:sha256, :filename, :mime_type, :byte_size, :created_at)"));
    query.bindValue(QStringLiteral(":sha256"), sha256);
    query.bindValue(QStringLiteral(":filename"), filename);
    query.bindValue(QStringLiteral(":mime_type"), mimeType);
    query.bindValue(QStringLiteral(":byte_size"), byteSize);
    query.bindValue(QStringLiteral(":created_at"), nowIso());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

WorkspaceStore::MediaRecord WorkspaceStore::mediaRecord(qint64 mediaId) const
{
    MediaRecord record;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, sha256, filename, mime_type, byte_size FROM media WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), mediaId);
    if (query.exec() && query.next()) {
        record.id = query.value(0).toLongLong();
        record.sha256 = query.value(1).toString();
        record.filename = query.value(2).toString();
        record.mimeType = query.value(3).toString();
        record.byteSize = query.value(4).toLongLong();
    }
    return record;
}

QSet<QString> WorkspaceStore::referencedMediaShas() const
{
    QSet<QString> shas;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral(
        "SELECT DISTINCT m.sha256 FROM media m WHERE EXISTS ("
        " SELECT 1 FROM blocks b WHERE b.media_id = m.id) OR EXISTS ("
        " SELECT 1 FROM media_versions v WHERE v.media_id = m.id)"));
    while (query.next())
        shas.insert(query.value(0).toString());
    return shas;
}

bool WorkspaceStore::addMediaVersion(const QString &blockId, qint64 mediaId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO media_versions (block_id, media_id, created_at)"
        " VALUES (:block_id, :media_id, :created_at)"));
    query.bindValue(QStringLiteral(":block_id"), blockId);
    query.bindValue(QStringLiteral(":media_id"), mediaId);
    query.bindValue(QStringLiteral(":created_at"), nowIso());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QVector<qint64> WorkspaceStore::mediaVersions(const QString &blockId) const
{
    QVector<qint64> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT media_id FROM media_versions WHERE block_id = :block_id"
        " ORDER BY created_at DESC, id DESC"));
    query.bindValue(QStringLiteral(":block_id"), blockId);
    if (!query.exec())
        return result;
    while (query.next())
        result.append(query.value(0).toLongLong());
    return result;
}

bool WorkspaceStore::saveAiResult(const AiResultRecord &result)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO ai_results (id, document_id, block_id, kind, provider_id, model, prompt,"
        " status, content, error, base_revision, batch_id, operation_id, remote_id, created_at)"
        " VALUES (:id, :document_id, :block_id, :kind, :provider_id, :model, :prompt, :status,"
        " :content, :error, :base_revision, :batch_id, :operation_id, :remote_id, :created_at)"
        " ON CONFLICT(id) DO UPDATE SET status = :status, content = :content, error = :error"));
    query.bindValue(QStringLiteral(":id"), result.id);
    query.bindValue(QStringLiteral(":document_id"), result.documentId);
    query.bindValue(QStringLiteral(":block_id"), nonNull(result.blockId));
    query.bindValue(QStringLiteral(":kind"), result.kind);
    query.bindValue(QStringLiteral(":provider_id"), nonNull(result.providerId));
    query.bindValue(QStringLiteral(":model"), nonNull(result.model));
    query.bindValue(QStringLiteral(":prompt"), nonNull(result.prompt));
    query.bindValue(QStringLiteral(":status"), result.status);
    query.bindValue(QStringLiteral(":content"), nonNull(result.content));
    query.bindValue(QStringLiteral(":error"), nonNull(result.error));
    query.bindValue(QStringLiteral(":base_revision"), result.baseRevision);
    query.bindValue(QStringLiteral(":batch_id"), nonNull(result.batchId));
    query.bindValue(QStringLiteral(":operation_id"), nonNull(result.operationId));
    query.bindValue(QStringLiteral(":remote_id"), nonNull(result.remoteId));
    query.bindValue(QStringLiteral(":created_at"),
                    result.createdAt.isValid() ? result.createdAt.toString(Qt::ISODateWithMs)
                                               : nowIso());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QVector<WorkspaceStore::AiResultRecord> WorkspaceStore::aiResults(const QString &documentId,
                                                                  const QString &blockId,
                                                                  int limit)
{
    QVector<AiResultRecord> result;
    QSqlQuery query(m_database);
    QString sql = QStringLiteral(
        "SELECT id, document_id, block_id, kind, provider_id, model, prompt, status, content,"
        " error, base_revision, batch_id, operation_id, remote_id, created_at FROM ai_results"
        " WHERE document_id = :document_id");
    if (!blockId.isEmpty())
        sql += QStringLiteral(" AND block_id = :block_id");
    sql += QStringLiteral(" ORDER BY created_at DESC, rowid DESC LIMIT :limit");

    query.prepare(sql);
    query.bindValue(QStringLiteral(":document_id"), documentId);
    if (!blockId.isEmpty())
        query.bindValue(QStringLiteral(":block_id"), blockId);
    query.bindValue(QStringLiteral(":limit"), limit);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return result;
    }

    while (query.next()) {
        AiResultRecord record;
        record.id = query.value(0).toString();
        record.documentId = query.value(1).toString();
        record.blockId = query.value(2).toString();
        record.kind = query.value(3).toString();
        record.providerId = query.value(4).toString();
        record.model = query.value(5).toString();
        record.prompt = query.value(6).toString();
        record.status = query.value(7).toString();
        record.content = query.value(8).toString();
        record.error = query.value(9).toString();
        record.baseRevision = query.value(10).toInt();
        record.batchId = query.value(11).toString();
        record.operationId = query.value(12).toString();
        record.remoteId = query.value(13).toString();
        record.createdAt = QDateTime::fromString(query.value(14).toString(), Qt::ISODateWithMs);
        result.append(record);
    }
    return result;
}

bool WorkspaceStore::updateAiResult(const QString &resultId, const QString &status,
                                    const QString &content, const QString &error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE ai_results SET status = :status, content = :content, error = :error"
        " WHERE id = :id"));
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":content"), nonNull(content));
    query.bindValue(QStringLiteral(":error"), nonNull(error));
    query.bindValue(QStringLiteral(":id"), resultId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QVector<PendingOperation> WorkspaceStore::pendingOperations(const QString &documentId)
{
    QVector<PendingOperation> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT operation_id, kind, payload, created_at FROM pending_operations"
        " WHERE document_id = :document_id AND state = 'pending' ORDER BY id"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return result;
    }
    while (query.next()) {
        PendingOperation operation;
        operation.operationId = query.value(0).toString();
        operation.kind = query.value(1).toString();
        operation.payload = QJsonDocument::fromJson(query.value(2).toString().toUtf8()).object();
        operation.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        result.append(operation);
    }
    return result;
}

bool WorkspaceStore::deletePendingOperation(const QString &documentId, const QString &operationId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "DELETE FROM pending_operations WHERE document_id = :document_id"
        " AND operation_id = :operation_id"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":operation_id"), operationId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool WorkspaceStore::updateDocumentSync(const QString &documentId, const QString &cloudId,
                                        const QString &cloudState, qint64 syncCursor,
                                        qint64 feedGeneration)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE documents SET cloud_id = :cloud_id, cloud_state = :cloud_state,"
        " sync_cursor = :sync_cursor, feed_generation = :feed_generation WHERE id = :id"));
    query.bindValue(QStringLiteral(":cloud_id"), cloudId);
    query.bindValue(QStringLiteral(":cloud_state"), cloudState);
    query.bindValue(QStringLiteral(":sync_cursor"), syncCursor);
    query.bindValue(QStringLiteral(":feed_generation"), feedGeneration);
    query.bindValue(QStringLiteral(":id"), documentId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool WorkspaceStore::mapBlock(const QString &documentId, const QString &localId,
                              const QString &remoteId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO sync_block_map (document_id, local_id, remote_id, remote_version)"
        " VALUES (:document_id, :local_id, :remote_id, 0)"
        " ON CONFLICT(document_id, local_id) DO UPDATE SET remote_id = :remote_id"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":local_id"), localId);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

int WorkspaceStore::remoteVersionForLocal(const QString &documentId, const QString &localId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT remote_version FROM sync_block_map WHERE document_id = :document_id"
        " AND local_id = :local_id"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":local_id"), localId);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

bool WorkspaceStore::setRemoteVersion(const QString &documentId, const QString &localId,
                                      int remoteVersion)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE sync_block_map SET remote_version = :remote_version WHERE document_id = :document_id"
        " AND local_id = :local_id"));
    query.bindValue(QStringLiteral(":remote_version"), remoteVersion);
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":local_id"), localId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QString WorkspaceStore::remoteIdForLocal(const QString &documentId, const QString &localId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT remote_id FROM sync_block_map WHERE document_id = :document_id"
        " AND local_id = :local_id"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":local_id"), localId);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return {};
}

QString WorkspaceStore::localIdForRemote(const QString &documentId, const QString &remoteId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT local_id FROM sync_block_map WHERE document_id = :document_id"
        " AND remote_id = :remote_id"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return {};
}

bool WorkspaceStore::clearBlockMap(const QString &documentId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM sync_block_map WHERE document_id = :document_id"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool WorkspaceStore::saveConflict(const SyncConflict &conflict)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO sync_conflicts (id, document_id, block_id, kind, base_content, local_content,"
        " remote_content, remote_sequence, remote_lock_version, created_at) VALUES (:id,"
        " :document_id, :block_id, :kind, :base_content, :local_content, :remote_content,"
        " :remote_sequence, :remote_lock_version, :created_at)"));
    query.bindValue(QStringLiteral(":id"), conflict.id);
    query.bindValue(QStringLiteral(":document_id"), conflict.documentId);
    query.bindValue(QStringLiteral(":block_id"), nonNull(conflict.blockId));
    query.bindValue(QStringLiteral(":kind"), nonNull(conflict.kind));
    query.bindValue(QStringLiteral(":base_content"), nonNull(conflict.baseContent));
    query.bindValue(QStringLiteral(":local_content"), nonNull(conflict.localContent));
    query.bindValue(QStringLiteral(":remote_content"), nonNull(conflict.remoteContent));
    query.bindValue(QStringLiteral(":remote_sequence"), conflict.remoteSequence);
    query.bindValue(QStringLiteral(":remote_lock_version"), conflict.remoteLockVersion);
    query.bindValue(QStringLiteral(":created_at"), nowIso());
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QVector<SyncConflict> WorkspaceStore::conflicts(const QString &documentId)
{
    QVector<SyncConflict> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, document_id, block_id, kind, base_content, local_content, remote_content,"
        " remote_sequence, remote_lock_version, created_at FROM sync_conflicts"
        " WHERE document_id = :document_id AND resolved_at IS NULL ORDER BY created_at"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    if (!query.exec())
        return result;
    while (query.next()) {
        SyncConflict conflict;
        conflict.id = query.value(0).toString();
        conflict.documentId = query.value(1).toString();
        conflict.blockId = query.value(2).toString();
        conflict.kind = query.value(3).toString();
        conflict.baseContent = query.value(4).toString();
        conflict.localContent = query.value(5).toString();
        conflict.remoteContent = query.value(6).toString();
        conflict.remoteSequence = query.value(7).toLongLong();
        conflict.remoteLockVersion = query.value(8).toInt();
        conflict.createdAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
        result.append(conflict);
    }
    return result;
}

int WorkspaceStore::conflictCount(const QString &documentId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM sync_conflicts WHERE document_id = :document_id"
        " AND resolved_at IS NULL"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

bool WorkspaceStore::resolveConflict(const QString &conflictId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE sync_conflicts SET resolved_at = :resolved_at WHERE id = :id"));
    query.bindValue(QStringLiteral(":resolved_at"), nowIso());
    query.bindValue(QStringLiteral(":id"), conflictId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QString WorkspaceStore::aiResultIdForRemote(const QString &documentId,
                                            const QString &remoteId) const
{
    if (remoteId.isEmpty())
        return {};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id FROM ai_results WHERE document_id = :document_id AND remote_id = :remote_id"
        " LIMIT 1"));
    query.bindValue(QStringLiteral(":document_id"), documentId);
    query.bindValue(QStringLiteral(":remote_id"), remoteId);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return {};
}

QString WorkspaceStore::setting(const QString &key, const QString &fallback) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = :key"));
    query.bindValue(QStringLiteral(":key"), key);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return fallback;
}

bool WorkspaceStore::setSetting(const QString &key, const QString &value)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO settings (key, value) VALUES (:key, :value)"
        " ON CONFLICT(key) DO UPDATE SET value = :value"));
    query.bindValue(QStringLiteral(":key"), key);
    query.bindValue(QStringLiteral(":value"), value);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

} // namespace writero
