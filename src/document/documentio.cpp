#include "document/documentio.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringConverter>
#include <QTextDocument>
#include <QUrl>

#include "markdown/markdown.h"

namespace writero::documentio {

namespace {

QString escaped(const QString &value)
{
    QString result = value;
    result.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    result.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    result.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    result.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return result;
}

QString markdownFragment(const QString &markdown)
{
    QTextDocument document;
    document.setMarkdown(markdown);
    const QString html = document.toHtml();
    const int bodyStart = html.indexOf(QStringLiteral("<body"));
    const int bodyOpenEnd = html.indexOf(QLatin1Char('>'), bodyStart);
    const int bodyEnd = html.lastIndexOf(QStringLiteral("</body>"));
    if (bodyStart < 0 || bodyOpenEnd < 0 || bodyEnd < 0)
        return escaped(markdown);
    return html.mid(bodyOpenEnd + 1, bodyEnd - bodyOpenEnd - 1);
}

QString mediaDataUri(const Block &block, const MediaAccess &access)
{
    if (!access.bytes)
        return {};
    const QByteArray bytes = access.bytes(block);
    if (bytes.isEmpty())
        return {};
    const QString mime = access.mimeType ? access.mimeType(block)
                                         : QStringLiteral("application/octet-stream");
    return QStringLiteral("data:%1;base64,%2")
        .arg(mime, QString::fromLatin1(bytes.toBase64()));
}

QString blockToHtml(const Block &block, const MediaAccess &access)
{
    switch (block.type) {
    case BlockType::Heading: {
        const QString level = blocktype::normalizeHeadingLevel(block.headingLevel()).mid(1);
        return QStringLiteral("<h%1>%2</h%1>").arg(level, escaped(block.content));
    }
    case BlockType::Text:
        return markdownFragment(block.content);
    case BlockType::Quote:
        return QStringLiteral("<blockquote>%1</blockquote>").arg(markdownFragment(block.content));
    case BlockType::Ul:
    case BlockType::Ol:
        return markdownFragment(block.content);
    case BlockType::Code:
        return QStringLiteral("<pre><code>%1</code></pre>").arg(escaped(block.content));
    case BlockType::Divider:
        return QStringLiteral("<hr>");
    case BlockType::Media: {
        QString source = block.mediaSource();
        if (source.isEmpty() && access.source)
            source = access.source(block);
        const QString dataUri = mediaDataUri(block, access);
        if (!dataUri.isEmpty())
            source = dataUri;
        if (source.isEmpty())
            return QStringLiteral("<p><em>[Image]</em></p>");
        const QString alt = block.mediaAlt().isEmpty() ? QStringLiteral("image")
                                                       : escaped(block.mediaAlt());
        return QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(escaped(source), alt);
    }
    }
    return {};
}

QString htmlDocument(const Document &document, const MediaAccess &access)
{
    QStringList parts;
    parts << QStringLiteral("<!DOCTYPE html>")
          << QStringLiteral("<html><head><meta charset=\"utf-8\">")
          << QStringLiteral("<title>%1</title>").arg(escaped(document.title))
          << QStringLiteral(
                 "<style>"
                 "body{max-width:800px;margin:2rem auto;padding:0 1rem;"
                 "font-family:system-ui,sans-serif;line-height:1.6;color:#222}"
                 "pre{background:#f4f4f4;padding:1rem;overflow-x:auto;border-radius:4px}"
                 "blockquote{border-left:3px solid #ccc;margin-left:0;padding-left:1rem;color:#555}"
                 "img{max-width:100%}"
                 "</style>")
          << QStringLiteral("</head><body>")
          << QStringLiteral("<h1>%1</h1>").arg(escaped(document.title));

    for (const Block &block : document.blocks)
        parts << blockToHtml(block, access);

    parts << QStringLiteral("</body></html>");
    return parts.join(QLatin1Char('\n'));
}

bool writeTextFile(const QString &path, const QString &content, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(content.toUtf8()) < 0 || !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QJsonObject blockToJson(const Block &block, const QString &mediaSha)
{
    QJsonObject object = QJsonObject::fromVariantMap(block.toJson());
    object.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(block.metadata));
    if (!mediaSha.isEmpty())
        object.insert(QStringLiteral("media_sha"), mediaSha);
    return object;
}

constexpr qint64 MaxManifestBytes = 32 * 1024 * 1024;
constexpr qint64 MaxMediaBytes = 100 * 1024 * 1024;

} // namespace

bool exportMarkdown(const Document &document, const QString &path, const MediaAccess &access,
                    QString *error)
{
    return writeTextFile(path, markdown::serialize(document.blocks, access.source), error);
}

QString toHtml(const Document &document, const MediaAccess &access)
{
    return htmlDocument(document, access);
}

bool exportHtml(const Document &document, const QString &path, const MediaAccess &access,
                QString *error)
{
    return writeTextFile(path, htmlDocument(document, access), error);
}

bool exportPdf(const Document &document, const QString &path, const MediaAccess &access,
               QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("PDF export failed: %1").arg(file.errorString());
        return false;
    }
    {
        QPdfWriter writer(&file);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
        writer.setTitle(document.title);

        QTextDocument textDocument;
        textDocument.setHtml(htmlDocument(document, access));
        textDocument.print(&writer);
    }
    if (file.error() != QFileDevice::NoError || !file.commit()) {
        if (error)
            *error = QStringLiteral("PDF export failed: %1").arg(file.errorString());
        return false;
    }
    return true;
}

BlockList importMarkdownFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    const QByteArray bytes = file.readAll();
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder.decode(bytes);
    if (file.error() != QFileDevice::NoError || decoder.hasError()) {
        if (error)
            *error = QStringLiteral("Cannot read %1 as UTF-8 Markdown").arg(path);
        return {};
    }
    BlockList blocks = markdown::parse(text);
    const QUrl base = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    for (Block &block : blocks) {
        if (block.type == BlockType::Media && !block.mediaSource().isEmpty())
            block.setMediaSource(base.resolved(QUrl(block.mediaSource())).toString());
    }
    return blocks;
}

bool exportBundle(const QString &directory, const BundleContents &contents, QString *error)
{
    static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
    for (const auto &media : contents.media) {
        if (!shaPattern.match(media.sha256).hasMatch()
            || media.byteSize != media.data.size()
            || QString::fromLatin1(QCryptographicHash::hash(media.data, QCryptographicHash::Sha256)
                                      .toHex()) != media.sha256) {
            if (error)
                *error = QStringLiteral("Bundle media has invalid content or hash");
            return false;
        }
    }

    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral(".")) || !dir.mkpath(QStringLiteral("media"))) {
        if (error)
            *error = QStringLiteral("Cannot create bundle directory %1").arg(directory);
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("writero-bundle"));
    root.insert(QStringLiteral("version"), 1);

    QJsonObject documentJson;
    documentJson.insert(QStringLiteral("id"), contents.document.id);
    documentJson.insert(QStringLiteral("title"), contents.document.title);
    documentJson.insert(QStringLiteral("created_at"),
                        contents.document.createdAt.toString(Qt::ISODateWithMs));
    documentJson.insert(QStringLiteral("updated_at"),
                        contents.document.updatedAt.toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("document"), documentJson);

    QJsonArray blocksJson;
    for (const Block &block : contents.document.blocks)
        blocksJson.append(blockToJson(block, contents.mediaShaByBlock.value(block.id)));
    root.insert(QStringLiteral("blocks"), blocksJson);

    QJsonArray revisionsJson;
    for (const Revision &revision : contents.revisions) {
        QJsonObject object;
        object.insert(QStringLiteral("block_id"), revision.blockId);
        object.insert(QStringLiteral("event"), revision.event);
        object.insert(QStringLiteral("source"), revision.source);
        object.insert(QStringLiteral("content"), revision.content);
        object.insert(QStringLiteral("type"), blocktype::toKey(revision.type));
        object.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(revision.metadata));
        object.insert(QStringLiteral("created_at"), revision.createdAt.toString(Qt::ISODateWithMs));
        revisionsJson.append(object);
    }
    root.insert(QStringLiteral("revisions"), revisionsJson);

    QJsonArray mediaJson;
    for (const BundleContents::MediaFile &media : contents.media) {
        QJsonObject object;
        object.insert(QStringLiteral("sha256"), media.sha256);
        object.insert(QStringLiteral("filename"), media.filename);
        object.insert(QStringLiteral("mime_type"), media.mimeType);
        object.insert(QStringLiteral("byte_size"), media.byteSize);
        mediaJson.append(object);

        const QString mediaPath = dir.filePath(QStringLiteral("media/%1").arg(media.sha256));
        QSaveFile file(mediaPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(media.data) != media.data.size()
            || !file.commit()) {
            if (error)
                *error = QStringLiteral("Cannot write bundle media %1").arg(media.sha256);
            return false;
        }
    }
    root.insert(QStringLiteral("media"), mediaJson);

    return writeTextFile(dir.filePath(QStringLiteral("manifest.json")),
                         QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)),
                         error);
}

bool importBundle(const QString &directory, BundleContents *contents, QString *error)
{
    if (!contents)
        return false;

    QDir dir(directory);
    QFile manifest(dir.filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Bundle has no manifest.json");
        return false;
    }
    if (manifest.size() > MaxManifestBytes) {
        if (error)
            *error = QStringLiteral("Bundle manifest is too large");
        return false;
    }

    const QJsonDocument json = QJsonDocument::fromJson(manifest.readAll());
    if (!json.isObject() || json.object().value(QStringLiteral("format")).toString()
            != QLatin1String("writero-bundle")) {
        if (error)
            *error = QStringLiteral("Not a Writero bundle");
        return false;
    }

    const QJsonObject root = json.object();
    const auto reject = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (root.value(QStringLiteral("version")).toDouble(-1) != 1)
        return reject(QStringLiteral("Unsupported bundle version"));
    if (!root.value(QStringLiteral("document")).isObject()
        || !root.value(QStringLiteral("blocks")).isArray()
        || !root.value(QStringLiteral("revisions")).isArray()
        || !root.value(QStringLiteral("media")).isArray())
        return reject(QStringLiteral("Bundle manifest has invalid structure"));

    // Publish parsed data only after every referenced file has been validated.
    BundleContents parsed;
    const QJsonObject documentJson = root.value(QStringLiteral("document")).toObject();
    parsed.document.id = newId();
    parsed.document.title = documentJson.value(QStringLiteral("title")).toString();
    parsed.document.createdAt =
        QDateTime::fromString(documentJson.value(QStringLiteral("created_at")).toString(),
                              Qt::ISODateWithMs);
    parsed.document.updatedAt =
        QDateTime::fromString(documentJson.value(QStringLiteral("updated_at")).toString(),
                              Qt::ISODateWithMs);

    QSet<QString> blockIds;
    const auto blocks = root.value(QStringLiteral("blocks")).toArray();
    for (const QJsonValue &value : blocks) {
        const QJsonObject object = value.toObject();
        bool knownType = false;
        blocktype::fromKey(object.value(QStringLiteral("type")).toString(), &knownType);
        const QString id = object.value(QStringLiteral("id")).toString();
        if (!value.isObject() || id.isEmpty() || blockIds.contains(id) || !knownType
            || !object.value(QStringLiteral("content")).isString()
            || !object.value(QStringLiteral("metadata")).isObject())
            return reject(QStringLiteral("Bundle has an invalid or duplicate block"));
        blockIds.insert(id);
        QVariantMap map = object.toVariantMap();
        QJsonObject metadata = object.value(QStringLiteral("metadata")).toObject();
        map.insert(QStringLiteral("metadata"), metadata.toVariantMap());
        const QString mediaSha = object.value(QStringLiteral("media_sha")).toString();
        Block block = Block::fromJson(map);
        if (!mediaSha.isEmpty())
            parsed.mediaShaByBlock.insert(block.id, mediaSha);
        parsed.document.blocks.append(block);
    }

    const auto revisions = root.value(QStringLiteral("revisions")).toArray();
    for (const QJsonValue &value : revisions) {
        const QJsonObject object = value.toObject();
        bool knownType = false;
        blocktype::fromKey(object.value(QStringLiteral("type")).toString(), &knownType);
        if (!value.isObject() || !knownType
            || !object.value(QStringLiteral("metadata")).isObject()
            || !object.value(QStringLiteral("content")).isString())
            return reject(QStringLiteral("Bundle has an invalid revision"));
        Revision revision;
        revision.blockId = object.value(QStringLiteral("block_id")).toString();
        revision.event = object.value(QStringLiteral("event")).toString();
        revision.source = object.value(QStringLiteral("source")).toString();
        revision.content = object.value(QStringLiteral("content")).toString();
        revision.type = blocktype::fromKey(object.value(QStringLiteral("type")).toString());
        revision.metadata = object.value(QStringLiteral("metadata")).toObject().toVariantMap();
        revision.createdAt =
            QDateTime::fromString(object.value(QStringLiteral("created_at")).toString(),
                                  Qt::ISODateWithMs);
        parsed.revisions.append(revision);
    }

    static const QRegularExpression shaPattern(QStringLiteral("^[0-9a-f]{64}$"));
    QSet<QString> mediaHashes;
    const auto media = root.value(QStringLiteral("media")).toArray();
    for (const QJsonValue &value : media) {
        const QJsonObject object = value.toObject();
        BundleContents::MediaFile file;
        file.sha256 = object.value(QStringLiteral("sha256")).toString();
        if (!shaPattern.match(file.sha256).hasMatch()) {
            if (error)
                *error = QStringLiteral("Bundle media has an invalid hash");
            return false;
        }
        file.filename = object.value(QStringLiteral("filename")).toString();
        file.mimeType = object.value(QStringLiteral("mime_type")).toString();
        const double declaredSize = object.value(QStringLiteral("byte_size")).toDouble(-1);
        if (declaredSize < 0 || declaredSize > MaxMediaBytes)
            return reject(QStringLiteral("Bundle media has invalid size"));
        file.byteSize = qint64(declaredSize);

        const QString mediaPath = dir.filePath(QStringLiteral("media/%1").arg(file.sha256));
        QFile mediaFile(mediaPath);
        if (!mediaFile.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Bundle media %1 is missing").arg(file.sha256);
            return false;
        }
        if (mediaFile.size() > MaxMediaBytes) {
            if (error)
                *error = QStringLiteral("Bundle media %1 is too large").arg(file.sha256);
            return false;
        }
        file.data = mediaFile.readAll();
        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(file.data, QCryptographicHash::Sha256).toHex());
        if (digest != file.sha256) {
            if (error)
                *error = QStringLiteral("Bundle media %1 failed its integrity check")
                             .arg(file.sha256);
            return false;
        }
        if (object.value(QStringLiteral("byte_size")).toDouble(-1) != file.data.size()
            || mediaHashes.contains(file.sha256))
            return reject(QStringLiteral("Bundle media has invalid size or duplicate hash"));
        mediaHashes.insert(file.sha256);
        parsed.media.append(file);
    }

    for (const QString &sha : parsed.mediaShaByBlock) {
        if (!mediaHashes.contains(sha))
            return reject(QStringLiteral("Bundle references missing media"));
    }
    *contents = std::move(parsed);

    return true;
}

} // namespace writero::documentio
