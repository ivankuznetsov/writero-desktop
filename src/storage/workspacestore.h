#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "document/document.h"
#include "document/documentchange.h"
#include "storage/revision.h"

namespace writero {

/// Summary row for the document library.
struct DocumentSummary
{
    QString id;
    QString title;
    QString preview;
    QDateTime createdAt;
    QDateTime updatedAt;
    bool trashed = false;
    int revision = 0;
    int wordCount = 0;
};

/// A local mutation waiting to be pushed to the cloud.
struct PendingOperation
{
    QString operationId;
    QString kind;
    QJsonObject payload;
    QDateTime createdAt;
};

/// A retained local/remote divergence that needs an explicit resolution.
struct SyncConflict
{
    QString id;
    QString documentId;
    QString blockId;
    QString kind;
    QString baseContent;
    QString localContent;
    QString remoteContent;
    qint64 remoteSequence = 0;
    int remoteLockVersion = 0;
    QDateTime createdAt;
};

/// SQLite workspace database.
///
/// The store owns the on-disk schema and performs all writes in transactions:
/// a committed save includes the document, its blocks, and the revision
/// entries produced by the changes that led to it. A crash can therefore
/// never leave a half-written document behind.
class WorkspaceStore
{
public:
    WorkspaceStore();
    ~WorkspaceStore();

    WorkspaceStore(const WorkspaceStore &) = delete;
    WorkspaceStore &operator=(const WorkspaceStore &) = delete;

    bool open(const QString &databasePath, QString *error = nullptr);
    void close();
    bool isOpen() const;

    /// Creates the document. The caller keeps ownership of block rows; use
    /// `saveDocument` to persist blocks.
    bool createDocument(const Document &document, QString *error = nullptr);

    /// Saves document metadata, all blocks, the given changes, and any
    /// pending cloud operations in one transaction.
    bool saveDocument(const Document &document, const QVector<DocumentChange> &changes,
                      const QVector<PendingOperation> &pendingOperations = {},
                      QString *error = nullptr);

    Document loadDocument(const QString &documentId, QString *error = nullptr);

    QVector<DocumentSummary> listDocuments(bool trashed = false, const QString &query = QString());
    bool setDocumentTrashed(const QString &documentId, bool trashed);
    bool deleteDocument(const QString &documentId);
    bool documentExists(const QString &documentId);

    QVector<Revision> revisions(const QString &documentId, const QString &blockId = QString(),
                                int limit = 200);
    bool pruneRevisions(const QString &documentId, const QString &blockId, int keep);

    /// Appends an already-formed revision (bundle import, history restore).
    bool insertRevision(const QString &documentId, const Revision &revision);

    struct AiResultRecord
    {
        QString id;
        QString documentId;
        QString blockId;
        QString kind;
        QString providerId;
        QString model;
        QString prompt;
        QString status;
        QString content;
        QString error;
        int baseRevision = 0;
        QString batchId;
        QDateTime createdAt;
    };

    bool saveAiResult(const AiResultRecord &result);
    QVector<AiResultRecord> aiResults(const QString &documentId, const QString &blockId = QString(),
                                      int limit = 50);
    bool updateAiResult(const QString &resultId, const QString &status,
                        const QString &content = QString(), const QString &error = QString());

    // --- Cloud sync bookkeeping ---

    QVector<PendingOperation> pendingOperations(const QString &documentId);
    bool deletePendingOperation(const QString &documentId, const QString &operationId);
    bool updateDocumentSync(const QString &documentId, const QString &cloudId,
                            const QString &cloudState, qint64 syncCursor, qint64 feedGeneration);
    bool mapBlock(const QString &documentId, const QString &localId, const QString &remoteId);
    QString remoteIdForLocal(const QString &documentId, const QString &localId) const;
    int remoteVersionForLocal(const QString &documentId, const QString &localId) const;
    bool setRemoteVersion(const QString &documentId, const QString &localId, int remoteVersion);
    QString localIdForRemote(const QString &documentId, const QString &remoteId) const;
    bool clearBlockMap(const QString &documentId);
    bool saveConflict(const SyncConflict &conflict);
    QVector<SyncConflict> conflicts(const QString &documentId);
    int conflictCount(const QString &documentId);
    bool resolveConflict(const QString &conflictId);

    struct MediaRecord
    {
        qint64 id = 0;
        QString sha256;
        QString filename;
        QString mimeType;
        qint64 byteSize = 0;
        bool isValid() const { return id > 0; }
    };

    /// Inserts a media row if the hash is new and returns its id; reuses the
    /// existing row for identical content.
    qint64 ensureMedia(const QString &sha256, const QString &filename, const QString &mimeType,
                       qint64 byteSize);
    MediaRecord mediaRecord(qint64 mediaId) const;
    QSet<QString> referencedMediaShas() const;
    bool addMediaVersion(const QString &blockId, qint64 mediaId);
    QVector<qint64> mediaVersions(const QString &blockId) const;

    QString setting(const QString &key, const QString &fallback = QString()) const;
    bool setSetting(const QString &key, const QString &value);

    QString lastError() const { return m_lastError; }

private:
    bool migrate();
    bool insertRevision(const DocumentChange &change, const QString &documentId,
                        QString *error);

    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_lastError;
};

} // namespace writero
