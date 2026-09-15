#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "cloud/changereconciler.h"
#include "editor/documentcontroller.h"
#include "storage/workspace.h"

using namespace writero;

namespace {

struct Fixture
{
    QTemporaryDir dir;
    Workspace workspace;
    DocumentController document;
    QString documentId;
    QString firstBlockId;
    QVector<SyncConflict> conflicts;

    bool setUp()
    {
        if (!workspace.open(dir.path()))
            return false;
        document.setWorkspace(&workspace);
        documentId = document.createDocument(QStringLiteral("Reconcile"));
        if (documentId.isEmpty())
            return false;
        document.setBlockContent(0, QStringLiteral("local content"));
        document.saveIfDirty();
        firstBlockId = document.blocks()->get(0).value(QStringLiteral("blockId")).toString();
        return true;
    }

    ReconcileContext context()
    {
        ReconcileContext ctx;
        ctx.session = &document.session();
        ctx.store = workspace.store();
        ctx.documentId = document.documentId();
        ctx.pendingBlockIds = pending;
        ctx.pendingTitle = titlePending;
        ctx.recordConflict = [this](const SyncConflict &conflict) { conflicts.append(conflict); };
        ctx.wantMedia = [](const QString &, const QJsonObject &) {};
        return ctx;
    }

    QSet<QString> pending;
    bool titlePending = false;
};

QJsonObject blockJson(const QString &remoteId, const QString &content, int position = 1,
                      const QString &type = QStringLiteral("text"), int lockVersion = 1)
{
    return QJsonObject{
        {QStringLiteral("id"), remoteId},
        {QStringLiteral("position"), position},
        {QStringLiteral("block_type"), type},
        {QStringLiteral("content"), content},
        {QStringLiteral("metadata"), QJsonObject{}},
        {QStringLiteral("lock_version"), lockVersion},
    };
}

QJsonObject changeJson(const QString &event, const QJsonObject &block, qint64 sequence,
                       const QString &remoteId)
{
    Q_UNUSED(remoteId);
    return QJsonObject{
        {QStringLiteral("sequence"), sequence},
        {QStringLiteral("event"), event},
        {QStringLiteral("payload"), QJsonObject{{QStringLiteral("block"), block}}},
    };
}

} // namespace

class TestReconciliation : public QObject
{
    Q_OBJECT

private slots:
    void replayRestoresMappedBlockMissingAfterFailedSave()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        const auto change = changeJson("block_create", blockJson("remote-new", "new from browser"),
                                       1, "remote-new");
        ChangeReconciler::applyChange(change, fixture.context());
        const QString localId = fixture.workspace.store()->localIdForRemote(
            fixture.documentId, QStringLiteral("remote-new"));
        QVERIFY(!localId.isEmpty());
        // Reopen the last durable document: reconciliation wrote the mapping,
        // but its document save did not commit before the process stopped.
        fixture.document.session().load(fixture.workspace.store()->loadDocument(fixture.documentId));
        QVERIFY(!fixture.document.session().document().blockById(localId));
        ChangeReconciler::applyChange(change, fixture.context());
        const Block *restored = fixture.document.session().document().blockById(localId);
        QVERIFY(restored);
        QCOMPARE(restored->content, QStringLiteral("new from browser"));
    }

    void snapshotPreservesPendingEditWhenRemoteBlockWasDeleted()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, "42");
        fixture.pending.insert(fixture.firstBlockId);
        ChangeReconciler::applySnapshot({{"document", QJsonObject{{"title", "Reconcile"}}},
                                         {"blocks", QJsonArray{}}}, fixture.context());
        QVERIFY(fixture.document.session().document().blockById(fixture.firstBlockId));
        QCOMPARE(fixture.conflicts.size(), 1);
        QCOMPARE(fixture.conflicts.first().kind, QStringLiteral("block_destroy"));
    }

    void snapshotImportsEditorialResults()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        auto block = blockJson("42", "remote");
        block.insert("id", 42);
        const QJsonObject result{{"id", 7}, {"block_id", 42}, {"kind", "rewrite"},
                                 {"status", "completed"},
                                 {"attributes", QJsonObject{{"id", 7}, {"block_id", 42},
                                     {"status", "completed"}, {"result_content", "rewritten"}}}};
        ChangeReconciler::applySnapshot({{"document", QJsonObject{{"title", "Reconcile"}}},
                                         {"blocks", QJsonArray{block}},
                                         {"results", QJsonArray{result}}}, fixture.context());
        const auto results = fixture.workspace.store()->aiResults(fixture.documentId);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().content, QStringLiteral("rewritten"));
    }

    void remoteMoveRefreshesLockVersionEvenIfPositionIsUnchanged()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, "42");
        fixture.workspace.store()->setRemoteVersion(fixture.documentId, fixture.firstBlockId, 0);
        const auto block = blockJson("42", "local content", 1, "text", 2);
        ChangeReconciler::applyChange(changeJson("block_move", block, 3, "42"), fixture.context());
        QCOMPARE(fixture.workspace.store()->remoteVersionForLocal(fixture.documentId,
                                                                  fixture.firstBlockId), 2);
    }

    void editorialResultIdsAreScopedByResultKind()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, "42");
        for (const QString &kind : {QStringLiteral("rewrite"), QStringLiteral("research")}) {
            const QJsonObject result{{"id", 7}, {"block_id", 42}, {"status", "completed"},
                                     {"result_content", kind}};
            ChangeReconciler::applyChange({{"event", "result_" + kind + "_result"},
                {"payload", QJsonObject{{"result", result}}}}, fixture.context());
        }
        QCOMPARE(fixture.workspace.store()->aiResults(fixture.documentId).size(), 2);
    }

    void remoteTitleRefreshesOptimisticVersion()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        ChangeReconciler::applyChange({{"event", "title_update"},
            {"payload", QJsonObject{{"title", "Browser title"}, {"title_version", 5}}}},
            fixture.context());
        QCOMPARE(fixture.document.session().document().syncTitleVersion, 5);
    }

    void numericRemoteBlockEventsApply()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, "42");
        auto block = blockJson("42", "numeric remote edit");
        block.insert("id", 42);
        ChangeReconciler::applyChange(changeJson("block_update", block, 3, "42"), fixture.context());
        QCOMPARE(fixture.document.session().document().blockById(fixture.firstBlockId)->content,
                 QStringLiteral("numeric remote edit"));
    }

    void remoteUpdateAppliesWithoutDirtyingOrJournaling()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());

        const QString remoteId = QStringLiteral("remote-1");
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, remoteId);

        // Start from a fresh session so undo history is empty.
        DocumentController reopened;
        reopened.setWorkspace(&fixture.workspace);
        QVERIFY(reopened.openDocument(fixture.documentId));
        QVERIFY(!reopened.canUndo());

        ReconcileContext context = fixture.context();
        context.session = &reopened.session();
        ChangeReconciler::applyChange(
            changeJson(QStringLiteral("block_update"),
                       blockJson(remoteId, QStringLiteral("remote content")), 5, remoteId),
            context);

        QCOMPARE(reopened.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("remote content"));
        QVERIFY(!reopened.isDirty());
        QVERIFY(!reopened.canUndo());
        QVERIFY(fixture.workspace.store()->pendingOperations(fixture.documentId).isEmpty());
    }

    void locallyEditedBlockBecomesAConflict()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());

        const QString remoteId = QStringLiteral("remote-1");
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, remoteId);

        // The block has an unsent local edit.
        fixture.document.setBlockContent(0, QStringLiteral("local edit"), false);
        fixture.pending.insert(fixture.firstBlockId);

        ChangeReconciler::applyChange(
            changeJson(QStringLiteral("block_update"),
                       blockJson(remoteId, QStringLiteral("remote wins")), 7, remoteId),
            fixture.context());

        QCOMPARE(fixture.document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("local edit"));
        QCOMPARE(fixture.conflicts.size(), 1);
        QCOMPARE(fixture.conflicts.first().localContent, QStringLiteral("local edit"));
        QCOMPARE(fixture.conflicts.first().remoteContent, QStringLiteral("remote wins"));
        QCOMPARE(fixture.conflicts.first().remoteLockVersion, 1);
    }

    void remoteCreateIsMappedAndInserted()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());

        ChangeReconciler::applyChange(
            changeJson(QStringLiteral("block_create"),
                       blockJson(QStringLiteral("remote-new"), QStringLiteral("from browser"), 1),
                       3, QStringLiteral("remote-new")),
            fixture.context());

        const QVariantMap created = fixture.document.blocks()->get(0);
        QCOMPARE(created.value(QStringLiteral("content")).toString(), QStringLiteral("from browser"));
        const QString localId = created.value(QStringLiteral("blockId")).toString();
        QCOMPARE(fixture.workspace.store()->remoteIdForLocal(fixture.documentId, localId),
                 QStringLiteral("remote-new"));
        QVERIFY(!fixture.document.isDirty());
    }

    void remoteDeleteOfLocallyEditedBlockBecomesAConflict()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());

        const QString remoteId = QStringLiteral("remote-1");
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, remoteId);
        fixture.pending.insert(fixture.firstBlockId);

        const int before = fixture.document.blocks()->rowCount();
        ChangeReconciler::applyChange(
            changeJson(QStringLiteral("block_destroy"),
                       blockJson(remoteId, QStringLiteral("local content")), 9, remoteId),
            fixture.context());

        QCOMPARE(fixture.document.blocks()->rowCount(), before);
        QCOMPARE(fixture.conflicts.size(), 1);
        QCOMPARE(fixture.conflicts.first().kind, QStringLiteral("block_destroy"));
    }

    void remoteTitleAppliesUnlessLocallyPending()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());

        fixture.titlePending = true;
        ChangeReconciler::applyChange(
            QJsonObject{
                {QStringLiteral("event"), QStringLiteral("title_update")},
                {QStringLiteral("sequence"), 4},
                {QStringLiteral("payload"),
                 QJsonObject{{QStringLiteral("title"), QStringLiteral("Remote title")}}},
            },
            fixture.context());

        QCOMPARE(fixture.conflicts.size(), 1);
        QCOMPARE(fixture.conflicts.first().kind, QStringLiteral("title"));
        QCOMPARE(fixture.conflicts.first().remoteContent, QStringLiteral("Remote title"));

        Fixture other;
        QVERIFY(other.setUp());
        ChangeReconciler::applyChange(
            QJsonObject{
                {QStringLiteral("event"), QStringLiteral("title_update")},
                {QStringLiteral("sequence"), 4},
                {QStringLiteral("payload"),
                 QJsonObject{{QStringLiteral("title"), QStringLiteral("Remote title")}}},
            },
            other.context());
        QCOMPARE(other.document.title(), QStringLiteral("Remote title"));
        QVERIFY(!other.document.isDirty());
    }

    void snapshotMergesMappedAndNewBlocksAndKeepsPendingCreates()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());

        const QString remoteId = QStringLiteral("remote-1");
        fixture.workspace.store()->mapBlock(fixture.documentId, fixture.firstBlockId, remoteId);
        fixture.workspace.store()->setRemoteVersion(fixture.documentId, fixture.firstBlockId, 4);

        // A local-only block with an unsent create; persisted so the test
        // isolates the remote application from local dirtiness.
        fixture.document.setBlockContent(0, QStringLiteral("kept local"), false);
        QVERIFY(fixture.document.saveIfDirty());
        QVERIFY(!fixture.document.isDirty());
        fixture.pending.insert(fixture.firstBlockId);

        QJsonObject snapshot{
            {QStringLiteral("document"),
             QJsonObject{{QStringLiteral("title"), QStringLiteral("Cloud doc")},
                         {QStringLiteral("title_version"), 2}}},
            {QStringLiteral("watermark"),
             QJsonObject{{QStringLiteral("generation"), 1}, {QStringLiteral("sequence"), 11}}},
            {QStringLiteral("blocks"),
             QJsonArray{blockJson(remoteId, QStringLiteral("remote content"), 1),
                        blockJson(QStringLiteral("remote-2"), QStringLiteral("second"), 2)}},
        };

        ChangeReconciler::applySnapshot(snapshot, fixture.context());

        QCOMPARE(fixture.document.title(), QStringLiteral("Cloud doc"));
        const QVariantMap first = fixture.document.blocks()->get(0);
        QCOMPARE(first.value(QStringLiteral("content")).toString(), QStringLiteral("kept local"));
        const QVariantMap second = fixture.document.blocks()->get(1);
        QCOMPARE(second.value(QStringLiteral("content")).toString(), QStringLiteral("second"));
        QCOMPARE(fixture.conflicts.size(), 1);
        QVERIFY(!fixture.document.isDirty());
    }
};

QTEST_MAIN(TestReconciliation)
#include "tst_reconciliation.moc"
