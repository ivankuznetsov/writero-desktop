#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>

namespace writero {

/// Content-addressed media file store.
///
/// Files are copied into a staging directory first, hashed, then moved into
/// their final location before any database row references them. A crash
/// between the copy and the move leaves only an abandoned staging file, which
/// `cleanupStaging` removes; no document ever references missing bytes.
class MediaStore
{
public:
    MediaStore();
    ~MediaStore();

    bool open(const QString &workspaceRoot, QString *error = nullptr);
    void close();
    bool isOpen() const { return !m_mediaRoot.isEmpty(); }

    /// Copies a file into the store. Returns the content hash, or an empty
    /// string on failure (`error` receives the reason).
    QString importFile(const QString &sourcePath, QString *error = nullptr);

    QString importData(const QByteArray &data, const QString &filename, const QString &mimeType,
                       QString *error = nullptr);

    /// Relative path of the blob for a content hash, e.g. `ab/<sha>`.
    QString relativePathForSha(const QString &sha256, const QString &mimeType = QString()) const;

    /// Absolute path of the blob for a content hash.
    QString absolutePathForSha(const QString &sha256, const QString &mimeType = QString()) const;

    bool contains(const QString &sha256) const;

    QString stagingPath() const;
    QString mediaRoot() const { return m_mediaRoot; }

    /// Removes abandoned files from staging. Returns the number removed.
    int cleanupStaging();

    /// Removes blobs not referenced by any media row in the given database.
    /// `referencedShas` is provided by the workspace store.
    int removeUnreferenced(const QSet<QString> &referencedShas);

    static QString mimeTypeForFile(const QString &path);

private:
    QString m_mediaRoot;
    QString m_stagingRoot;
};

} // namespace writero
