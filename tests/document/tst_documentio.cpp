#include <QtTest/QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

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
