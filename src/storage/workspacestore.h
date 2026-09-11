#pragma once

#include <QDateTime>
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

    /// Saves document metadata, all blocks, and the given changes in one
    /// transaction. Changes are journaled as revisions.
    bool saveDocument(const Document &document, const QVector<DocumentChange> &changes,
                      QString *error = nullptr);

    Document loadDocument(const QString &documentId, QString *error = nullptr);

    QVector<DocumentSummary> listDocuments(bool trashed = false, const QString &query = QString());
    bool setDocumentTrashed(const QString &documentId, bool trashed);
    bool deleteDocument(const QString &documentId);
    bool documentExists(const QString &documentId);

    QVector<Revision> revisions(const QString &documentId, const QString &blockId = QString(),
                                int limit = 200);
    bool pruneRevisions(const QString &documentId, const QString &blockId, int keep);

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
