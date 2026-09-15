#include "storage/workspace.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSet>
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
    if (!m_store.saveDocument(document, {}, {}, &error)) {
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

documentio::MediaAccess Workspace::mediaAccess() const
{
    documentio::MediaAccess access;
    access.source = [this](const Block &block) {
        return block.mediaId > 0 ? mediaPath(block.mediaId) : QString();
    };
    access.bytes = [this](const Block &block) {
        if (block.mediaId <= 0)
            return QByteArray();
        QFile file(mediaPath(block.mediaId));
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    };
    access.mimeType = [this](const Block &block) {
        return m_store.mediaRecord(block.mediaId).mimeType;
    };
    return access;
}

QString Workspace::exportBundleTo(const QString &directory, const QString &documentId,
                                  QString *error)
{
    if (!isReady())
        return {};

    const Document document = m_store.loadDocument(documentId, error);
    if (document.id.isEmpty())
        return {};

    documentio::BundleContents contents;
    contents.document = document;
    contents.revisions = m_store.revisions(documentId, QString(), 10000);

    QSet<QString> seen;
    for (const Block &block : document.blocks) {
        if (block.mediaId <= 0)
            continue;
        const WorkspaceStore::MediaRecord record = m_store.mediaRecord(block.mediaId);
        if (!record.isValid())
            continue;
        contents.mediaShaByBlock.insert(block.id, record.sha256);
        if (seen.contains(record.sha256))
            continue;
        seen.insert(record.sha256);

        documentio::BundleContents::MediaFile media;
        media.sha256 = record.sha256;
        media.filename = record.filename;
        media.mimeType = record.mimeType;
        media.byteSize = record.byteSize;
        QFile file(m_media.absolutePathForSha(record.sha256, record.mimeType));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Cannot read bundle media %1: %2")
                             .arg(record.sha256, file.errorString());
            return {};
        }
        media.data = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            if (error)
                *error = QStringLiteral("Cannot read bundle media %1: %2")
                             .arg(record.sha256, file.errorString());
            return {};
        }
        contents.media.append(media);
    }

    if (!documentio::exportBundle(directory, contents, error))
        return {};
    return documentId;
}

QString Workspace::importBundleFrom(const QString &directory, QString *error)
{
    if (!isReady())
        return {};

    documentio::BundleContents contents;
    if (!documentio::importBundle(directory, &contents, error))
        return {};

    Document document = contents.document;
    document.id = newId();
    if (document.title.isEmpty())
        document.title = QStringLiteral("Imported document");

    // Importing creates a new document: allocate fresh block identities so the
    // bundle can coexist with its source, and remap media and history.
    QHash<QString, QString> idMap;
    for (Block &block : document.blocks) {
        const QString oldId = block.id;
        block.id = newId();
        idMap.insert(oldId, block.id);
    }

    QHash<QString, qint64> mediaIds;
    for (const documentio::BundleContents::MediaFile &media : contents.media) {
        if (!m_media.contains(media.sha256)) {
            if (m_media.importData(media.data, media.filename, media.mimeType, error).isEmpty())
                return {};
        }
        const qint64 mediaId =
            m_store.ensureMedia(media.sha256, media.filename, media.mimeType, media.byteSize);
        if (mediaId <= 0) {
            if (error)
                *error = m_store.lastError();
            return {};
        }
        mediaIds.insert(media.sha256, mediaId);
    }

    for (Block &block : document.blocks) {
        const QString oldId = idMap.key(block.id);
        const QString sha = contents.mediaShaByBlock.value(oldId);
        if (!sha.isEmpty())
            block.mediaId = mediaIds.value(sha);
        else
            block.mediaId = 0;
    }

    if (!m_store.saveDocument(document, {}, {}, error))
        return {};
    for (const Revision &revision : contents.revisions) {
        Revision remapped = revision;
        remapped.blockId = idMap.value(revision.blockId, revision.blockId);
        importRevision(document.id, remapped);
    }

    m_documents.refresh();
    return document.id;
}

bool Workspace::importRevision(const QString &documentId, const Revision &revision)
{
    return m_store.insertRevision(documentId, revision);
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
