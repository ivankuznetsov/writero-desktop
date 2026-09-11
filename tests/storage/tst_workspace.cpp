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
