#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "editor/documentcontroller.h"
#include "editor/documentlibrarymodel.h"
#include "storage/workspace.h"

using namespace writero;

class TestWorkspace : public QObject
{
    Q_OBJECT

private slots:
    void opensAndRejectsASecondInstance()
    {
        QTemporaryDir dir;
        Workspace first;
        QString error;
        QVERIFY2(first.open(dir.path()), qPrintable(first.lastError()));
        QVERIFY(first.isReady());

        Workspace second;
        QVERIFY(!second.open(dir.path()));
        QVERIFY(second.lastError().contains(QStringLiteral("already open")));

        first.close();
        QVERIFY(second.open(dir.path()));
        QVERIFY(second.isReady());
    }

    void createDocumentAppearsInLibrary()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        const QString id = workspace.createDocument(QStringLiteral("Notes"));
        QVERIFY(!id.isEmpty());

        DocumentLibraryModel *model = workspace.documents();
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Notes"));
        QCOMPARE(model->get(0).value(QStringLiteral("documentId")).toString(), id);

        QVERIFY(workspace.trashDocument(id));
        QCOMPARE(model->rowCount(), 0);
        model->setShowTrashed(true);
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(model->get(0).value(QStringLiteral("trashed")).toBool());
        QVERIFY(workspace.restoreDocument(id));
        QCOMPARE(model->rowCount(), 0);
        model->setShowTrashed(false);
        QCOMPARE(model->rowCount(), 1);

        QVERIFY(workspace.deleteDocument(id));
        QCOMPARE(model->rowCount(), 0);
    }

    void controllerSavesAndReloadsThroughWorkspace()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController controller;
        controller.setWorkspace(&workspace);

        const QString id = controller.createDocument(QStringLiteral("Persisted"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(controller.title(), QStringLiteral("Persisted"));
        QVERIFY2(controller.blocks()->rowCount() == 1,
                 qPrintable(QStringLiteral("blocks=%1 error=%2")
                                .arg(controller.blocks()->rowCount())
                                .arg(controller.saveError())));

        controller.setBlockContent(0, QStringLiteral("hello disk"));
        QVERIFY(controller.isDirty());
        QVERIFY2(controller.saveIfDirty(), qPrintable(controller.saveError()));
        QVERIFY(!controller.isDirty());

        DocumentController reopened;
        reopened.setWorkspace(&workspace);
        QVERIFY(reopened.openDocument(id));
        QCOMPARE(reopened.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("hello disk"));
    }

    void trashCurrentDocumentClearsTheEditor()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController controller;
        controller.setWorkspace(&workspace);
        const QString id = controller.createDocument(QStringLiteral("Temp"));
        controller.setBlockContent(0, QStringLiteral("content"));
        QVERIFY(controller.saveIfDirty());

        QVERIFY(controller.trashCurrentDocument());
        QVERIFY(controller.documentId() != id);
        QCOMPARE(workspace.documents()->rowCount(), 0);
    }

    void autosavePersistsWithoutExplicitSave()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController controller;
        controller.setWorkspace(&workspace);
        const QString id = controller.createDocument(QStringLiteral("Auto"));
        controller.setBlockContent(0, QStringLiteral("autosaved text"));

        QTRY_VERIFY_WITH_TIMEOUT(!controller.isDirty(), 4000);

        DocumentController reopened;
        reopened.setWorkspace(&workspace);
        QVERIFY(reopened.openDocument(id));
        QCOMPARE(reopened.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("autosaved text"));
    }

    void bundleExportImportKeepsMediaAndHistory()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController controller;
        controller.setWorkspace(&workspace);
        const QString id = controller.createDocument(QStringLiteral("Portable"));
        controller.setBlockContent(0, QStringLiteral("body text"));

        const QString imagePath = dir.filePath(QStringLiteral("pixel.png"));
        QFile image(imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write(QByteArray("\x89PNG\r\n\x1a\nfake", 12));
        image.close();
        QVERIFY(controller.attachMedia(0, imagePath));
        QVERIFY(controller.saveIfDirty());

        const QString bundlePath = dir.filePath(QStringLiteral("portable.writero"));
        QCOMPARE(controller.exportBundle(bundlePath), id);

        const QString importedId = controller.importBundle(bundlePath);
        QVERIFY2(!importedId.isEmpty(), qPrintable(controller.saveError()));
        QVERIFY(importedId != id);
        QCOMPARE(controller.title(), QStringLiteral("Portable"));
        QCOMPARE(controller.blocks()->rowCount(), 2);
        const QVariantMap mediaBlock = controller.blocks()->get(0);
        QCOMPARE(mediaBlock.value(QStringLiteral("blockType")).toString(),
                 QStringLiteral("media"));
        QVERIFY(mediaBlock.value(QStringLiteral("mediaId")).toLongLong() > 0);
        QVERIFY(!workspace.mediaPath(mediaBlock.value(QStringLiteral("mediaId")).toLongLong())
                     .isEmpty());
        QCOMPARE(workspace.documents()->rowCount(), 2);
    }

    void bundleImportFailsWhenMediaCannotBeStored()
    {
        QTemporaryDir sourceDir;
        Workspace source;
        QVERIFY(source.open(sourceDir.path()));
        const QString id = source.createDocument("Media");
        const QString imagePath = sourceDir.filePath("pixel.png");
        QFile image(imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("image-bytes");
        image.close();
        Document document = source.store()->loadDocument(id);
        document.blocks[0].mediaId = source.importMedia(imagePath);
        QVERIFY(document.blocks[0].mediaId > 0);
        QVERIFY(source.store()->saveDocument(document, {}));
        const QString bundle = sourceDir.filePath("bundle");
        QVERIFY(!source.exportBundleTo(bundle, id).isEmpty());

        QTemporaryDir targetDir;
        Workspace target;
        QVERIFY(target.open(targetDir.path()));
        QVERIFY(QDir(targetDir.filePath("media/.staging")).removeRecursively());
        QFile blocker(targetDir.filePath("media/.staging"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        QString error;
        QVERIFY(target.importBundleFrom(bundle, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(target.store()->listDocuments().isEmpty());
    }

    void bundleExportFailsWhenAttachedBlobIsMissing()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));
        const QString id = workspace.createDocument("Missing media");
        Document document = workspace.store()->loadDocument(id);
        document.blocks[0].mediaId = workspace.store()->ensureMedia(
            QString(64, 'a'), "missing.png", "image/png", 10);
        QVERIFY(workspace.store()->saveDocument(document, {}));
        QString error;
        QVERIFY(workspace.exportBundleTo(dir.filePath("bundle"), id, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void markdownImportReplacesBlocks()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController controller;
        controller.setWorkspace(&workspace);
        controller.createDocument(QStringLiteral("Import target"));

        const QString path = dir.filePath(QStringLiteral("article.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Imported\n\nHello world.\n\n- one\n- two\n");
        file.close();

        QVERIFY2(controller.importMarkdownFile(path), qPrintable(controller.saveError()));
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("blockType")).toString(),
                 QStringLiteral("heading"));
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("Imported"));
        QVERIFY(controller.blocks()->rowCount() >= 4); // heading, text, list, placeholder
        QVERIFY(controller.saveIfDirty());
    }

    void historyRestoresPreviousContent()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController controller;
        controller.setWorkspace(&workspace);
        controller.createDocument(QStringLiteral("History"));
        controller.setBlockContent(0, QStringLiteral("first"));
        QVERIFY(controller.saveIfDirty());
        controller.setBlockContent(0, QStringLiteral("second"));
        QVERIFY(controller.saveIfDirty());

        const QVariantList revisions = controller.blockRevisions(0);
        QVERIFY(revisions.size() >= 2);
        const qint64 firstId = revisions.last().toMap().value(QStringLiteral("id")).toLongLong();
        QVERIFY(controller.restoreRevision(0, firstId));
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("first"));
    }

    void importedMediaResolvesToAFilePath()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        const QString source = dir.filePath(QStringLiteral("pixel.png"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("\x89PNG\r\n\x1a\nfake", 12));
        file.close();

        const qint64 mediaId = workspace.importMedia(source);
        QVERIFY(mediaId > 0);

        const QString path = workspace.mediaPath(mediaId);
        QVERIFY(QFile::exists(path));
        QVERIFY(path.startsWith(workspace.rootPath()));
        QVERIFY(workspace.mediaUrl(mediaId).startsWith(QStringLiteral("file://")));
    }
};

QTEST_MAIN(TestWorkspace)
#include "tst_workspace.moc"
