#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include "document/documentio.h"
#include "editor/documentlibrarymodel.h"
#include "storage/mediastore.h"
#include "storage/workspacestore.h"

class QLockFile;

namespace writero {

/// One local workspace: SQLite database plus content-addressed media.
///
/// A workspace is single-instance. Opening it from a second application
/// process fails with a clear error instead of risking concurrent writers.
class Workspace : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(DocumentLibraryModel *documents READ documents CONSTANT)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY opened)
    Q_PROPERTY(bool ready READ isReady NOTIFY opened)
    Q_PROPERTY(QString lastError READ lastError NOTIFY opened)

public:
    explicit Workspace(QObject *parent = nullptr);
    ~Workspace() override;

    static QString defaultRootPath();

    Q_INVOKABLE bool openDefault();
    Q_INVOKABLE bool open(const QString &rootPath);
    Q_INVOKABLE void close();

    bool isReady() const;
    QString rootPath() const { return m_rootPath; }
    QString lastError() const { return m_lastError; }

    DocumentLibraryModel *documents() { return &m_documents; }
    WorkspaceStore *store() { return &m_store; }
    MediaStore *media() { return &m_media; }

    Q_INVOKABLE QString createDocument(const QString &title = QString());
    Q_INVOKABLE bool trashDocument(const QString &documentId);
    Q_INVOKABLE bool restoreDocument(const QString &documentId);
    Q_INVOKABLE bool deleteDocument(const QString &documentId);
    Q_INVOKABLE bool documentExists(const QString &documentId);

    /// Copies a file into the workspace and returns its media id, or 0.
    Q_INVOKABLE qint64 importMedia(const QString &sourcePath);
    Q_INVOKABLE QString mediaPath(qint64 mediaId) const;
    Q_INVOKABLE QString mediaUrl(qint64 mediaId) const;

    /// Media access for document export (Markdown references and embedded bytes).
    documentio::MediaAccess mediaAccess() const;

    /// Writes/reads a portable directory bundle. Import returns the new
    /// document id, or an empty string on failure.
    QString exportBundleTo(const QString &directory, const QString &documentId,
                           QString *error = nullptr);
    QString importBundleFrom(const QString &directory, QString *error = nullptr);

    /// Persists a revision read from a bundle (used by bundle import).
    bool importRevision(const QString &documentId, const Revision &revision);

    Q_INVOKABLE QString setting(const QString &key, const QString &fallback = QString()) const;
    Q_INVOKABLE bool setSetting(const QString &key, const QString &value);

signals:
    void opened();

private:
    void setError(const QString &error);

    WorkspaceStore m_store;
    MediaStore m_media;
    DocumentLibraryModel m_documents;
    QLockFile *m_lock = nullptr;
    QString m_rootPath;
    QString m_lastError;
};

} // namespace writero
