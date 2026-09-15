#include <QtTest/QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QUrl>
#include <QImage>
#include <QBuffer>

#include "document/documentio.h"
#include "markdown/markdown.h"

using namespace writero;

namespace {

Document makeDocument()
{
    Document document;
    document.id = newId();
    document.title = QStringLiteral("Export me");
    document.blocks = {
        Block::create(BlockType::Heading, QStringLiteral("Title")),
        Block::create(BlockType::Text, QStringLiteral("Some **bold** text")),
        Block::create(BlockType::Code, QStringLiteral("print('hi')")),
    };
    document.blocks[0].setHeadingLevel(QStringLiteral("h1"));
    return document;
}

documentio::BundleContents makeBundle()
{
    documentio::BundleContents contents;
    contents.document = makeDocument();

    Block media = Block::create(BlockType::Media);
    media.id = QStringLiteral("media-block");
    media.setMediaAlt(QStringLiteral("pixel"));
    contents.document.blocks.append(media);

    const QByteArray bytes("fake-png-bytes");
    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    contents.mediaShaByBlock.insert(media.id, sha);

    documentio::BundleContents::MediaFile file;
    file.sha256 = sha;
    file.filename = QStringLiteral("pixel.png");
    file.mimeType = QStringLiteral("image/png");
    file.byteSize = bytes.size();
    file.data = bytes;
    contents.media.append(file);

    Revision revision;
    revision.blockId = contents.document.blocks.first().id;
    revision.event = QStringLiteral("update");
    revision.source = QStringLiteral("ai");
    revision.content = QStringLiteral("Title");
    revision.createdAt = QDateTime::currentDateTimeUtc();
    contents.revisions.append(revision);

    return contents;
}

} // namespace

class TestDocumentIo : public QObject
{
    Q_OBJECT

private slots:
    void markdownFileResolvesRelativeImages()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Unicode title\n\n![local](images/cat.png)\n");
        file.close();
        const auto blocks = documentio::importMarkdownFile(path);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.first().type, BlockType::Heading);
        QCOMPARE(blocks.last().mediaSource(),
                 QUrl::fromLocalFile(dir.filePath(QStringLiteral("images/cat.png"))).toString());
    }

    void markdownFileHandlesUtf8Bom()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("bom.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex("efbbbf") + "# Title\n");
        file.close();
        const auto blocks = documentio::importMarkdownFile(file.fileName());
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().type, BlockType::Heading);
    }

    void markdownFileRejectsInvalidUtf8_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid-sequence") << QByteArray::fromHex("6869ffc328");
        QTest::newRow("truncated-sequence") << QByteArray::fromHex("6869e282");
    }

    void markdownFileRejectsInvalidUtf8()
    {
        QFETCH(QByteArray, bytes);
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("bad.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();
        QString error;
        QVERIFY(documentio::importMarkdownFile(file.fileName(), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void pdfExportRejectsDirectoryDestination()
    {
        QTemporaryDir dir;
        QString error;
        QVERIFY(!documentio::exportPdf(makeDocument(), dir.path(), {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void bundleMalformedManifest_data()
    {
        QTest::addColumn<QString>("mutation");
        for (const char *name : {"version", "blocks-type", "duplicate-block", "unknown-type",
                                 "missing-media", "byte-size", "media-reference"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }

    void bundleMalformedManifest()
    {
        QFETCH(QString, mutation);
        QTemporaryDir dir;
        QVERIFY(documentio::exportBundle(dir.path(), makeBundle()));
        QFile manifest(dir.filePath(QStringLiteral("manifest.json")));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(manifest.readAll()).object();
        manifest.close();
        if (mutation == QLatin1String("version"))
            root["version"] = 999;
        if (mutation == QLatin1String("blocks-type"))
            root["blocks"] = QStringLiteral("corrupt");
        if (mutation == QLatin1String("duplicate-block")) {
            auto blocks = root["blocks"].toArray();
            blocks.append(blocks.first());
            root["blocks"] = blocks;
        }
        if (mutation == QLatin1String("unknown-type")) {
            auto blocks = root["blocks"].toArray();
            auto block = blocks[0].toObject();
            block["type"] = QStringLiteral("unknown");
            blocks[0] = block;
            root["blocks"] = blocks;
        }
        if (mutation == QLatin1String("missing-media"))
            root["media"] = QJsonArray();
        if (mutation == QLatin1String("byte-size")) {
            auto media = root["media"].toArray();
            auto item = media[0].toObject();
            item["byte_size"] = -1;
            media[0] = item;
            root["media"] = media;
        }
        if (mutation == QLatin1String("media-reference")) {
            auto blocks = root["blocks"].toArray();
            auto block = blocks.last().toObject();
            block["media_sha"] = QStringLiteral("bad");
            blocks[blocks.size() - 1] = block;
            root["blocks"] = blocks;
        }
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.write(QJsonDocument(root).toJson());
        manifest.close();
        documentio::BundleContents output;
        QString error;
        QVERIFY(!documentio::importBundle(dir.path(), &output, &error));
        QVERIFY(!error.isEmpty());
    }

    void failedBundleImportPreservesOutput()
    {
        QTemporaryDir dir;
        const auto original = makeBundle();
        QVERIFY(documentio::exportBundle(dir.path(), original));
        QFile media(dir.filePath(QStringLiteral("media/") + original.media.first().sha256));
        QVERIFY(media.remove());
        documentio::BundleContents output;
        output.document.title = QStringLiteral("Keep original");
        QVERIFY(!documentio::importBundle(dir.path(), &output));
        QCOMPARE(output.document.title, QStringLiteral("Keep original"));
    }

    void bundleExportRejectsInvalidHashWithoutEscapingDirectory()
    {
        QTemporaryDir dir;
        auto contents = makeBundle();
        contents.media.first().sha256 = QStringLiteral("../../escaped");
        QVERIFY(!documentio::exportBundle(dir.filePath(QStringLiteral("bundle")), contents));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("escaped"))));
    }

    void markdownExportWritesBlocks()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.md"));
        QString error;
        QVERIFY2(documentio::exportMarkdown(makeDocument(), path, {}, &error), qPrintable(error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString markdown = QString::fromUtf8(file.readAll());
        QVERIFY(markdown.contains(QStringLiteral("# Title")));
        QVERIFY(markdown.contains(QStringLiteral("Some **bold** text")));
        QVERIFY(markdown.contains(QStringLiteral("```\n") + QStringLiteral("print('hi')\n```")));
    }

    void htmlExportIsSelfContained()
    {
        const QString html = documentio::toHtml(makeDocument(), {});
        QVERIFY(html.contains(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(html.contains(QStringLiteral("<h1>Title</h1>")));
        QVERIFY(html.contains(QStringLiteral("<pre><code>print('hi')</code></pre>")));
        QVERIFY(html.contains(QStringLiteral("bold")));
    }

    void htmlEmbeddingEscapesContent()
    {
        Document document = makeDocument();
        document.blocks[1].content = QStringLiteral("<script>alert(1)</script>");
        const QString html = documentio::toHtml(document, {});
        QVERIFY(!html.contains(QStringLiteral("<script>")));
    }

    void pdfExportCreatesAFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.pdf"));
        QString error;
        QVERIFY2(documentio::exportPdf(makeDocument(), path, {}, &error), qPrintable(error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray header = file.read(5);
        QCOMPARE(header, QByteArray("%PDF-"));
    }

    void pdfEmbedsImageBytes()
    {
        QTemporaryDir dir;
        Document document;
        document.title = QStringLiteral("Image PDF");
        document.blocks = {Block::create(BlockType::Media)};
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        documentio::MediaAccess access;
        access.bytes = [png](const Block &) { return png; };
        access.mimeType = [](const Block &) { return QStringLiteral("image/png"); };
        const QString path = dir.filePath(QStringLiteral("image.pdf"));
        QVERIFY(documentio::exportPdf(document, path, access));
        QFile pdf(path);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.readAll().contains("/Subtype /Image"));
    }

    void bundleRoundTripsContentHistoryAndMedia()
    {
        QTemporaryDir dir;
        const QString bundlePath = dir.filePath(QStringLiteral("bundle"));

        const documentio::BundleContents original = makeBundle();
        QString error;
        QVERIFY2(documentio::exportBundle(bundlePath, original, &error), qPrintable(error));

        documentio::BundleContents imported;
        QVERIFY2(documentio::importBundle(bundlePath, &imported, &error), qPrintable(error));

        QCOMPARE(imported.document.title, original.document.title);
        QCOMPARE(imported.document.blocks.size(), original.document.blocks.size());
        QCOMPARE(imported.document.blocks.last().mediaAlt(), QStringLiteral("pixel"));
        QCOMPARE(imported.media.size(), 1);
        QCOMPARE(imported.media.first().sha256, original.media.first().sha256);
        QCOMPARE(imported.media.first().data, original.media.first().data);
        QCOMPARE(imported.revisions.size(), 1);
        QCOMPARE(imported.revisions.first().event, QStringLiteral("update"));
        QCOMPARE(imported.mediaShaByBlock.value(QStringLiteral("media-block")),
                 original.media.first().sha256);
    }

    void bundleRejectsTamperedMedia()
    {
        QTemporaryDir dir;
        const QString bundlePath = dir.filePath(QStringLiteral("bundle"));
        QVERIFY(documentio::exportBundle(bundlePath, makeBundle()));

        // Corrupt the media file; import must detect the hash mismatch.
        const QString mediaPath = QDir(bundlePath).filePath(
            QStringLiteral("media/") + makeBundle().media.first().sha256);
        QFile media(mediaPath);
        QVERIFY(media.open(QIODevice::WriteOnly));
        media.write("tampered");
        media.close();

        documentio::BundleContents imported;
        QString error;
        QVERIFY(!documentio::importBundle(bundlePath, &imported, &error));
        QVERIFY(error.contains(QStringLiteral("integrity")));
    }

    void bundleRejectsPathTraversalHash()
    {
        QTemporaryDir dir;
        const QString bundlePath = dir.filePath(QStringLiteral("bundle"));
        QDir().mkpath(bundlePath + QStringLiteral("/media"));

        const QString manifest = QStringLiteral(
            "{\"format\":\"writero-bundle\",\"version\":1,\"document\":{\"title\":\"x\"},"
            "\"blocks\":[],\"revisions\":[],"
            "\"media\":[{\"sha256\":\"../escape\",\"filename\":\"x\",\"mime_type\":\"image/png\","
            "\"byte_size\":1}]}");
        QFile file(bundlePath + QStringLiteral("/manifest.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(manifest.toUtf8());
        file.close();

        documentio::BundleContents imported;
        QString error;
        QVERIFY(!documentio::importBundle(bundlePath, &imported, &error));
        QVERIFY(error.contains(QStringLiteral("invalid hash")));
    }
};

QTEST_MAIN(TestDocumentIo)
#include "tst_documentio.moc"
