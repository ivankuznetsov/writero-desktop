#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include "document/documentsession.h"
#include "storage/mediastore.h"
#include "storage/workspacestore.h"

using namespace writero;

class TestWorkspaceStore : public QObject
{
    Q_OBJECT

private slots:
    void saveAndLoadRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        WorkspaceStore store;
        QString error;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("workspace.db")), &error),
                 qPrintable(error));

        Document document;
        document.id = newId();
        document.title = QStringLiteral("Round trip");
        document.blocks = {
            Block::create(BlockType::Text, QStringLiteral("first")),
            Block::create(BlockType::Heading, QStringLiteral("Second")),
            Block::create(BlockType::Code, QStringLiteral("print('x')")),
        };
        document.blocks[1].setHeadingLevel(QStringLiteral("h3"));
        document.blocks[2].setLanguage(QStringLiteral("python"));
        QVERIFY(store.createDocument(document, &error));

        QVERIFY(store.saveDocument(document, {}, &error));

        const Document loaded = store.loadDocument(document.id, &error);
        QCOMPARE(loaded.title, document.title);
        QCOMPARE(loaded.blocks.size(), 3);
        QCOMPARE(loaded.blocks.at(1).headingLevel(), QStringLiteral("h3"));
        QCOMPARE(loaded.blocks.at(2).language(), QStringLiteral("python"));
        QCOMPARE(loaded.blocks.at(0).content, QStringLiteral("first"));
    }

    void changeJournalBecomesRevisions()
    {
        QTemporaryDir dir;
        WorkspaceStore store;
        QVERIFY(store.open(dir.filePath(QStringLiteral("workspace.db"))));

        DocumentSession session;
        Document document;
        document.id = newId();
        document.title = QStringLiteral("History");
        document.blocks = {Block::create(BlockType::Text, QStringLiteral("original")),
                           Block::create(BlockType::Text)};
        session.load(document);

        session.updateContent(0, QStringLiteral("edited"));
        session.updateType(1, BlockType::Heading, QStringLiteral("h2"));
        const auto changes = session.drainJournal();
        // Two updates plus the trailing placeholder the heading update creates.
        QCOMPARE(changes.size(), 3);

        QVERIFY(store.saveDocument(session.document(), changes));

        const auto revisions = store.revisions(document.id, session.document().blocks.at(0).id);
        QCOMPARE(revisions.size(), 1);
        QCOMPARE(revisions.first().event, QStringLiteral("update"));
        QCOMPARE(revisions.first().content, QStringLiteral("edited"));
        QCOMPARE(revisions.first().source, QStringLiteral("local"));
    }

    void crashBeforeSaveLosesNothingAlreadyCommitted()
    {
        QTemporaryDir dir;
        const QString databasePath = dir.filePath(QStringLiteral("workspace.db"));

        {
            WorkspaceStore store;
            QVERIFY(store.open(databasePath));
            Document document;
            document.id = newId();
            document.title = QStringLiteral("Durable");
            document.blocks = {Block::create(BlockType::Text, QStringLiteral("committed"))};
            QVERIFY(store.createDocument(document));
            QVERIFY(store.saveDocument(document, {}));
        } // store destroyed without clean shutdown

        WorkspaceStore reopened;
        QVERIFY(reopened.open(databasePath));
        const QVector<DocumentSummary> documents = reopened.listDocuments();
        QCOMPARE(documents.size(), 1);
        const Document loaded = reopened.loadDocument(documents.first().id);
        QCOMPARE(loaded.blocks.first().content, QStringLiteral("committed"));
    }

    void listDocumentsFiltersTrashAndQuery()
    {
        QTemporaryDir dir;
        WorkspaceStore store;
        QVERIFY(store.open(dir.filePath(QStringLiteral("workspace.db"))));

        Document alpha;
        alpha.id = newId();
        alpha.title = QStringLiteral("Alpha notes");
        alpha.blocks = {Block::create(BlockType::Text, QStringLiteral("about rivers"))};
        QVERIFY(store.createDocument(alpha));
        QVERIFY(store.saveDocument(alpha, {}));

        Document beta;
        beta.id = newId();
        beta.title = QStringLiteral("Beta draft");
        beta.blocks = {Block::create(BlockType::Text, QStringLiteral("about mountains"))};
        QVERIFY(store.createDocument(beta));
        QVERIFY(store.saveDocument(beta, {}));

        QCOMPARE(store.listDocuments(false).size(), 2);
        QCOMPARE(store.listDocuments(false, QStringLiteral("rivers")).size(), 1);
        QCOMPARE(store.listDocuments(false, QStringLiteral("Beta")).size(), 1);

        QVERIFY(store.setDocumentTrashed(beta.id, true));
        QCOMPARE(store.listDocuments(false).size(), 1);
        QCOMPARE(store.listDocuments(true).size(), 1);
        QVERIFY(store.setDocumentTrashed(beta.id, false));
        QCOMPARE(store.listDocuments(false).size(), 2);
    }

    void deletingDocumentRemovesBlocksAndRevisions()
    {
        QTemporaryDir dir;
        WorkspaceStore store;
        QVERIFY(store.open(dir.filePath(QStringLiteral("workspace.db"))));

        Document document;
        document.id = newId();
        document.title = QStringLiteral("Disposable");
        document.blocks = {Block::create(BlockType::Text, QStringLiteral("bye"))};
        QVERIFY(store.createDocument(document));
        QVERIFY(store.saveDocument(document, {}));

        QVERIFY(store.deleteDocument(document.id));
        QVERIFY(!store.documentExists(document.id));
        QVERIFY(store.revisions(document.id).isEmpty());
        QVERIFY(store.loadDocument(document.id).id.isEmpty());
    }

    void unknownMetadataSurvivesRoundTrip()
    {
        QTemporaryDir dir;
        WorkspaceStore store;
        QVERIFY(store.open(dir.filePath(QStringLiteral("workspace.db"))));

        Document document;
        document.id = newId();
        document.title = QStringLiteral("Future");
        Block block = Block::create(BlockType::Text, QStringLiteral("body"));
        block.metadata.insert(QStringLiteral("future_field"), QVariantMap{{QStringLiteral("a"), 1}});
        document.blocks = {block};
        QVERIFY(store.createDocument(document));
        QVERIFY(store.saveDocument(document, {}));

        const Document loaded = store.loadDocument(document.id);
        const QVariant value = loaded.blocks.first().metadata.value(QStringLiteral("future_field"));
        QVERIFY(value.isValid());
        QCOMPARE(value.toMap().value(QStringLiteral("a")).toInt(), 1);
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        WorkspaceStore store;
        QVERIFY(store.open(dir.filePath(QStringLiteral("workspace.db"))));
        QCOMPARE(store.setting(QStringLiteral("theme"), QStringLiteral("dark")),
                 QStringLiteral("dark"));
        QVERIFY(store.setSetting(QStringLiteral("theme"), QStringLiteral("light")));
        QCOMPARE(store.setting(QStringLiteral("theme")), QStringLiteral("light"));
    }

    void mediaStoreIsContentAddressedAndDeduplicates()
    {
        QTemporaryDir dir;
        MediaStore media;
        QVERIFY(media.open(dir.path()));

        const QByteArray data("fake-image-bytes");
        const QString sha = media.importData(data, QStringLiteral("cat.png"),
                                             QStringLiteral("image/png"));
        QVERIFY(!sha.isEmpty());
        QVERIFY(media.contains(sha));

        const QString shaAgain = media.importData(data, QStringLiteral("other.png"),
                                                  QStringLiteral("image/png"));
        QCOMPARE(shaAgain, sha);

        const QString path = media.absolutePathForSha(sha, QStringLiteral("image/png"));
        QVERIFY(QFile::exists(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), data);
    }

    void mediaStagingCleanupRemovesAbandonedFiles()
    {
        QTemporaryDir dir;
        MediaStore media;
        QVERIFY(media.open(dir.path()));

        const QString abandoned =
            QDir(media.stagingPath()).filePath(QStringLiteral("interrupted-upload"));
        QFile staging(abandoned);
        QVERIFY(staging.open(QIODevice::WriteOnly));
        staging.write("partial");
        staging.close();

        // Fresh staging files may belong to an in-flight import and are kept.
        QCOMPARE(media.cleanupStaging(), 0);
        QVERIFY(QFile::exists(abandoned));

        QFile::remove(abandoned);

        // Simulate an abandoned import: recreate with an old modification time.
        QFile aged(abandoned);
        QVERIFY(aged.open(QIODevice::WriteOnly));
        aged.write("partial");
        aged.close();
        QProcess::execute(QStringLiteral("touch"),
                          {QStringLiteral("-d"), QStringLiteral("1 hour ago"), abandoned});

        QCOMPARE(media.cleanupStaging(), 1);
        QVERIFY(!QFile::exists(abandoned));
    }
};

QTEST_MAIN(TestWorkspaceStore)
#include "tst_workspacestore.moc"
