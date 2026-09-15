#include "storage/mediastore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QUuid>

namespace writero {

MediaStore::MediaStore() = default;
MediaStore::~MediaStore() = default;

bool MediaStore::open(const QString &workspaceRoot, QString *error)
{
    close();
    QDir root(workspaceRoot);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        if (error)
            *error = QStringLiteral("Cannot create workspace directory %1").arg(workspaceRoot);
        return false;
    }

    m_mediaRoot = root.filePath(QStringLiteral("media"));
    m_stagingRoot = QDir(m_mediaRoot).filePath(QStringLiteral(".staging"));

    if (!QDir().mkpath(m_stagingRoot)) {
        if (error)
            *error = QStringLiteral("Cannot create media staging directory");
        close();
        return false;
    }

    cleanupStaging();
    return true;
}

void MediaStore::close()
{
    m_mediaRoot.clear();
    m_stagingRoot.clear();
}

QString MediaStore::importFile(const QString &sourcePath, QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read %1: %2").arg(sourcePath, source.errorString());
        return {};
    }

    const QByteArray data = source.readAll();
    return importData(data, QFileInfo(sourcePath).fileName(), mimeTypeForFile(sourcePath), error);
}

QString MediaStore::importData(const QByteArray &data, const QString &filename,
                               const QString &mimeType, QString *error)
{
    if (!isOpen()) {
        if (error)
            *error = QStringLiteral("Media store is not open");
        return {};
    }

    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    const QString finalPath = absolutePathForSha(sha, mimeType);

    if (QFile::exists(finalPath))
        return sha; // Content-addressed: identical bytes are already stored.

    const QString stagingFile =
        QDir(m_stagingRoot).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));

    {
        QFile staged(stagingFile);
        if (!staged.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("Cannot write staging file: %1").arg(staged.errorString());
            return {};
        }
        if (staged.write(data) != data.size() || !staged.flush()) {
            if (error)
                *error = QStringLiteral("Short write to staging file");
            staged.close();
            QFile::remove(stagingFile);
            return {};
        }
        staged.close();
    }

    if (!QDir().mkpath(QFileInfo(finalPath).absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create media directory");
        QFile::remove(stagingFile);
        return {};
    }

    if (!QFile::rename(stagingFile, finalPath)) {
        if (error)
            *error = QStringLiteral("Cannot move media into place");
        QFile::remove(stagingFile);
        return {};
    }

    Q_UNUSED(filename);
    return sha;
}

QString MediaStore::relativePathForSha(const QString &sha256, const QString &mimeType) const
{
    Q_UNUSED(mimeType);
    if (sha256.size() != 64)
        return {};
    for (const QChar ch : sha256) {
        if (!(ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            && !(ch >= QLatin1Char('a') && ch <= QLatin1Char('f')))
            return {};
    }
    return QStringLiteral("%1/%2").arg(sha256.left(2), sha256);
}

QString MediaStore::absolutePathForSha(const QString &sha256, const QString &mimeType) const
{
    const QString relative = relativePathForSha(sha256, mimeType);
    if (!isOpen() || relative.isEmpty())
        return {};
    return QDir(m_mediaRoot).absoluteFilePath(relative);
}

bool MediaStore::contains(const QString &sha256) const
{
    const QString path = absolutePathForSha(sha256);
    return !path.isEmpty() && QFileInfo(path).isFile();
}

QString MediaStore::stagingPath() const
{
    return m_stagingRoot;
}

int MediaStore::cleanupStaging()
{
    if (!isOpen())
        return 0;
    int removed = 0;
    QDir staging(m_stagingRoot);
    const QFileInfoList entries =
        staging.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &entry : entries) {
        // Give in-flight imports a minute before treating a file as abandoned.
        if (entry.lastModified().secsTo(QDateTime::currentDateTime()) < 60)
            continue;
        if (QFile::remove(entry.absoluteFilePath()))
            ++removed;
    }
    return removed;
}

int MediaStore::removeUnreferenced(const QSet<QString> &referencedShas)
{
    if (!isOpen())
        return 0;
    int removed = 0;
    QDir media(m_mediaRoot);
    const QFileInfoList buckets =
        media.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &bucket : buckets) {
        if (bucket.fileName() == QLatin1String(".staging"))
            continue;
        QDir bucketDir(bucket.absoluteFilePath());
        const QFileInfoList files =
            bucketDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &file : files) {
            const QString sha = file.completeBaseName();
            if (referencedShas.contains(sha))
                continue;
            if (QFile::remove(file.absoluteFilePath()))
                ++removed;
        }
    }
    return removed;
}

QString MediaStore::mimeTypeForFile(const QString &path)
{
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent);
    if (mime.isValid())
        return mime.name();
    return QStringLiteral("application/octet-stream");
}

} // namespace writero
