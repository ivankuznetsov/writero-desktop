#include "storage/workspace.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <QUrl>

namespace writero {

Workspace::Workspace(QObject *parent)
    : QObject(parent)
{
}

Workspace::~Workspace()
{
    close();
}

QString Workspace::defaultRootPath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("workspace"));
}

bool Workspace::openDefault()
{
    return open(defaultRootPath());
}

bool Workspace::open(const QString &rootPath)
{
    close();

    if (!QDir().mkpath(rootPath)) {
        setError(QStringLiteral("Cannot create workspace at %1").arg(rootPath));
        emit opened();
        return false;
    }

    m_lock = new QLockFile(QDir(rootPath).filePath(QStringLiteral("workspace.lock")));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(100)) {
        setError(QStringLiteral("This workspace is already open in another instance."));
        delete m_lock;
        m_lock = nullptr;
        emit opened();
        return false;
    }

    QString error;
    if (!m_store.open(QDir(rootPath).filePath(QStringLiteral("workspace.db")), &error)) {
        setError(QStringLiteral("Cannot open workspace database: %1").arg(error));
        close();
        emit opened();
        return false;
    }
    if (!m_media.open(rootPath, &error)) {
        setError(QStringLiteral("Cannot open media store: %1").arg(error));
        close();
        emit opened();
        return false;
    }

    m_rootPath = rootPath;
    m_lastError.clear();
    m_documents.setStore(&m_store);
    m_documents.refresh();
    emit opened();
    return true;
}

void Workspace::close()
{
    if (!m_rootPath.isEmpty())
        m_documents.setStore(nullptr);
    m_store.close();
    m_media.close();
    m_rootPath.clear();
    if (m_lock) {
        m_lock->unlock();
        delete m_lock;
        m_lock = nullptr;
    }
}

bool Workspace::isReady() const
{
    return m_store.isOpen() && m_media.isOpen();
}

QString Workspace::createDocument(const QString &title)
{
    if (!isReady())
        return {};

    Document document;
    document.id = newId();
    document.title = title.isEmpty() ? QStringLiteral("Untitled") : title;
    document.blocks = {Block::create(BlockType::Text)};
    document.createdAt = QDateTime::currentDateTimeUtc();
    document.updatedAt = document.createdAt;

    QString error;
    if (!m_store.createDocument(document, &error)) {
        setError(error);
        return {};
    }
    if (!m_store.saveDocument(document, {}, &error)) {
        setError(error);
        return {};
    }
    m_documents.refresh();
    return document.id;
}

bool Workspace::trashDocument(const QString &documentId)
{
    if (!m_store.setDocumentTrashed(documentId, true))
        return false;
    m_documents.refresh();
    return true;
}

bool Workspace::restoreDocument(const QString &documentId)
{
    if (!m_store.setDocumentTrashed(documentId, false))
        return false;
    m_documents.refresh();
    return true;
}

bool Workspace::deleteDocument(const QString &documentId)
{
    if (!m_store.deleteDocument(documentId))
        return false;
    m_documents.refresh();
    return true;
}

bool Workspace::documentExists(const QString &documentId)
{
    return m_store.documentExists(documentId);
}

qint64 Workspace::importMedia(const QString &sourcePath)
{
    if (!isReady())
        return 0;

    QString error;
    const QString sha = m_media.importFile(sourcePath, &error);
    if (sha.isEmpty()) {
        setError(error);
        return 0;
    }

    const QFileInfo info(sourcePath);
    const qint64 mediaId = m_store.ensureMedia(sha, info.fileName(),
                                               MediaStore::mimeTypeForFile(sourcePath),
                                               info.size());
    if (mediaId == 0)
        setError(m_store.lastError());
    return mediaId;
}

QString Workspace::mediaPath(qint64 mediaId) const
{
    const WorkspaceStore::MediaRecord record = m_store.mediaRecord(mediaId);
    if (!record.isValid())
        return {};
    return m_media.absolutePathForSha(record.sha256, record.mimeType);
}

QString Workspace::mediaUrl(qint64 mediaId) const
{
    const QString path = mediaPath(mediaId);
    return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
}

QString Workspace::setting(const QString &key, const QString &fallback) const
{
    return m_store.setting(key, fallback);
}

bool Workspace::setSetting(const QString &key, const QString &value)
{
    return m_store.setSetting(key, value);
}

void Workspace::setError(const QString &error)
{
    m_lastError = error;
    qWarning().noquote() << "writero workspace:" << error;
}

} // namespace writero
