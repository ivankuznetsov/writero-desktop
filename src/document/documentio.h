#pragma once

#include <functional>

#include "document/document.h"
#include "storage/revision.h"

namespace writero::documentio {

/// How import/export reaches block media. Local workspaces and bundles
/// provide different implementations.
struct MediaAccess
{
    /// Reference used in Markdown links (relative path or URL).
    std::function<QString(const Block &)> source;
    /// Raw bytes for HTML/PDF embedding.
    std::function<QByteArray(const Block &)> bytes;
    /// MIME type for embedded data URIs.
    std::function<QString(const Block &)> mimeType;
};

struct BundleContents
{
    Document document;
    QVector<Revision> revisions;
    /// Block id → content hash for media referenced by that block.
    QHash<QString, QString> mediaShaByBlock;
    /// Media entries referenced by `media_sha` metadata on blocks.
    struct MediaFile
    {
        QString sha256;
        QString filename;
        QString mimeType;
        qint64 byteSize = 0;
        QByteArray data;
    };
    QVector<MediaFile> media;
};

/// Writes Markdown, self-contained HTML, or PDF.
bool exportMarkdown(const Document &document, const QString &path, const MediaAccess &access,
                    QString *error = nullptr);
bool exportHtml(const Document &document, const QString &path, const MediaAccess &access,
                QString *error = nullptr);
bool exportPdf(const Document &document, const QString &path, const MediaAccess &access,
               QString *error = nullptr);

/// Renders a document to a self-contained HTML string (images embedded).
QString toHtml(const Document &document, const MediaAccess &access);

/// A bundle is a directory: `manifest.json` plus `media/<sha256>` blobs.
/// Import rejects path traversal, absolute paths, and oversized manifests.
bool exportBundle(const QString &directory, const BundleContents &contents, QString *error = nullptr);
bool importBundle(const QString &directory, BundleContents *contents, QString *error = nullptr);

/// Reads a Markdown file into blocks.
BlockList importMarkdownFile(const QString &path, QString *error = nullptr);

} // namespace writero::documentio
