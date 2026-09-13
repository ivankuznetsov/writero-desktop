#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>

#include "ai/aicontroller.h"
#include "cloud/syncengine.h"
#include "editor/documentcontroller.h"
#include "storage/workspace.h"

using namespace writero;

namespace {

/// In-memory desktop API stub: documents, blocks, change feed, mutations
/// with idempotency receipts, and a switch to force lock conflicts.
class StubDesktopApi : public QTcpServer
{
public:
    struct ServerBlock
    {
        QString id;
        QString content;
        QString type;
        int position = 1;
        int lockVersion = 1;
    };
    struct ServerChange
    {
        qint64 sequence = 0;
        QString event;
        QJsonObject payload;
    };
    struct Document
    {
        QString id;
        QString title;
        qint64 generation = 1;
        qint64 sequence = 0;
        qint64 titleVersion = 1;
        QVector<ServerBlock> blocks;
        QVector<ServerChange> changes;
    };

    explicit StubDesktopApi(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = nextPendingConnection();
            auto buffer = QSharedPointer<QByteArray>::create();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                buffer->append(socket->readAll());
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
                const QByteArray method = requestLine.value(0);
                const QString path = QString::fromUtf8(requestLine.value(1));

                int contentLength = 0;
                for (const QByteArray &line : lines) {
                    const QByteArray trimmed = line.trimmed();
                    if (trimmed.toLower().startsWith("content-length:"))
                        contentLength = trimmed.mid(15).trimmed().toInt();
                }
                if (buffer->size() < headerEnd + 4 + contentLength)
                    return;

                const QJsonObject body = QJsonDocument::fromJson(
                                             buffer->mid(headerEnd + 4, contentLength))
                                             .object();
                socket->write(route(method, path, body));
                socket->flush();
                socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return QTcpServer::listen(QHostAddress::LocalHost, 0); }
    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }

    Document *document(const QString &id) { return m_documents.contains(id) ? &m_documents[id] : nullptr; }
    void forceConflict(bool force) { m_forceConflict = force; }
    void setAccountEmail(const QString &email) { m_accountEmail = email; }
    void setDocumentMissing(bool missing) { m_documentMissing = missing; }

    void appendResultChange(const QString &documentId, const QString &remoteBlockId,
                            const QString &resultId, const QString &content)
    {
        Document *doc = document(documentId);
        if (!doc)
            return;
        QJsonObject result{
            {QStringLiteral("id"), resultId},
            {QStringLiteral("block_id"), remoteBlockId},
            {QStringLiteral("ai_model"), QStringLiteral("google/gemini-3.6-flash")},
            {QStringLiteral("status"), QStringLiteral("completed")},
            {QStringLiteral("result_content"), content},
            {QStringLiteral("error_message"), QString()},
        };
        ++doc->sequence;
        doc->changes.append(ServerChange{
            doc->sequence, QStringLiteral("result_rewrite_result"),
            QJsonObject{{QStringLiteral("result"), result}}});
    }

    int mutationCount = 0;
    int replayCount = 0;

private:
    static QByteArray respond(int status, const QByteArray &reason, const QJsonObject &json)
    {
        const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
        return QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason
            + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(body.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
    }

    QJsonObject serialize(const ServerBlock &block) const
    {
        return QJsonObject{
            {QStringLiteral("id"), block.id},
            {QStringLiteral("position"), block.position},
            {QStringLiteral("block_type"), block.type},
            {QStringLiteral("content"), block.content},
            {QStringLiteral("metadata"), QJsonObject{}},
            {QStringLiteral("lock_version"), block.lockVersion},
        };
    }

    void appendChange(Document &document, const QString &event, const QJsonObject &payload)
    {
        ++document.sequence;
        document.changes.append(ServerChange{document.sequence, event, payload});
    }

    QByteArray route(const QByteArray &method, const QString &path, const QJsonObject &body)
    {
        const QString routePath = path.section(QLatin1Char('?'), 0, 0);
        if (method == "GET" && routePath == QLatin1String("/api/desktop/v1/capabilities")) {
            return respond(200, "OK",
                           QJsonObject{
                               {QStringLiteral("protocol"),
                                QJsonObject{{QStringLiteral("name"), QStringLiteral("writero-desktop")},
                                            {QStringLiteral("version"), 1}}},
                               {QStringLiteral("account"),
                                QJsonObject{{QStringLiteral("id"), 1},
                                            {QStringLiteral("email"), m_accountEmail}}},
                               {QStringLiteral("subscription"),
                                QJsonObject{{QStringLiteral("plan"), QStringLiteral("starter")},
                                            {QStringLiteral("active"), true}}},
                               {QStringLiteral("hosted_ai"),
                                QJsonObject{{QStringLiteral("enabled"), true},
                                            {QStringLiteral("remaining_credit_usd"), 10.0}}},
                               {QStringLiteral("entitlements"), QJsonArray{QStringLiteral("cloud_sync")}},
                           });
        }

        if (method == "POST" && routePath == QLatin1String("/api/desktop/v1/documents")) {
            Document document;
            document.id = QStringLiteral("cloud-1");
            document.title = body.value(QStringLiteral("title")).toString();
            m_documents.insert(document.id, document);
            return respond(201, "Created",
                           QJsonObject{
                               {QStringLiteral("document"),
                                QJsonObject{{QStringLiteral("id"), document.id},
                                            {QStringLiteral("title"), document.title},
                                            {QStringLiteral("title_version"), 1}}},
                               {QStringLiteral("watermark"),
                                QJsonObject{{QStringLiteral("generation"), 1},
                                            {QStringLiteral("sequence"), 0}}},
                           });
        }

        const QString prefix = QStringLiteral("/api/desktop/v1/documents/");
        if (!routePath.startsWith(prefix))
            return respond(404, "Not Found", QJsonObject{{QStringLiteral("error"), QStringLiteral("not found")}});

        const QString remainder = routePath.mid(prefix.size());
        const QString documentId = remainder.section(QLatin1Char('/'), 0, 0);
        Document *document = this->document(documentId);
        if (!document)
            return respond(404, "Not Found", QJsonObject{{QStringLiteral("error"), QStringLiteral("not found")}});
        if (m_documentMissing
            && (remainder.endsWith(QLatin1String("/snapshot"))
                || remainder.endsWith(QLatin1String("/changes")))) {
            return respond(404, "Not Found", QJsonObject{{QStringLiteral("error"), QStringLiteral("not found")}});
        }

        if (remainder.endsWith(QLatin1String("/snapshot")) && method == "GET") {
            QJsonArray blocks;
            for (const ServerBlock &block : document->blocks)
                blocks.append(serialize(block));
            return respond(200, "OK",
                           QJsonObject{
                               {QStringLiteral("document"),
                                QJsonObject{{QStringLiteral("id"), document->id},
                                            {QStringLiteral("title"), document->title},
                                            {QStringLiteral("title_version"), document->titleVersion}}},
                               {QStringLiteral("generation"), document->generation},
                               {QStringLiteral("watermark"),
                                QJsonObject{{QStringLiteral("generation"), document->generation},
                                            {QStringLiteral("sequence"), document->sequence}}},
                               {QStringLiteral("lease"),
                                QJsonObject{{QStringLiteral("id"), QStringLiteral("lease-1")},
                                            {QStringLiteral("expires_at"), QStringLiteral("2099-01-01T00:00:00Z")}}},
                               {QStringLiteral("page"),
                                QJsonObject{{QStringLiteral("number"), 1},
                                            {QStringLiteral("total_pages"), 1}}},
                               {QStringLiteral("blocks"), blocks},
                           });
        }

        if (remainder.endsWith(QLatin1String("/changes")) && method == "GET") {
            const qint64 cursor = QUrlQuery(QUrl(path)).queryItemValue(QStringLiteral("cursor"))
                                      .toLongLong();
            QJsonArray changes;
            for (const ServerChange &change : document->changes) {
                if (change.sequence <= cursor)
                    continue;
                changes.append(QJsonObject{
                    {QStringLiteral("sequence"), change.sequence},
                    {QStringLiteral("event"), change.event},
                    {QStringLiteral("payload"), change.payload},
                });
            }
            return respond(200, "OK",
                           QJsonObject{
                               {QStringLiteral("generation"), document->generation},
                               {QStringLiteral("cursor"), cursor},
                               {QStringLiteral("next_cursor"), document->sequence},
                               {QStringLiteral("has_more"), false},
                               {QStringLiteral("changes"), changes},
                           });
        }

        if (remainder.endsWith(QLatin1String("/mutations")) && method == "POST") {
            return handleMutations(*document, body);
        }

        return respond(404, "Not Found", QJsonObject{{QStringLiteral("error"), QStringLiteral("not found")}});
    }

    QByteArray handleMutations(Document &document, const QJsonObject &body)
    {
        QJsonArray results;
        const QJsonArray mutations = body.value(QStringLiteral("mutations")).toArray();
        for (const QJsonValue &value : mutations) {
            const QJsonObject mutation = value.toObject();
            const QString operationId = mutation.value(QStringLiteral("operation_id")).toString();
            if (m_receipts.contains(operationId)) {
                ++replayCount;
                results.append(m_receipts.value(operationId));
                continue;
            }
            ++mutationCount;

            const QString kind = mutation.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("create_block")) {
                ServerBlock block;
                block.id = QStringLiteral("r%1").arg(++m_remoteIds);
                block.content = mutation.value(QStringLiteral("attributes"))
                                    .toObject()
                                    .value(QStringLiteral("content"))
                                    .toString();
                block.type = mutation.value(QStringLiteral("attributes"))
                                 .toObject()
                                 .value(QStringLiteral("block_type"))
                                 .toString();
                block.position = document.blocks.size() + 1;
                document.blocks.append(block);
                appendChange(document, QStringLiteral("block_create"),
                             QJsonObject{{QStringLiteral("block"), serialize(block)}});
                const QJsonObject result{
                    {QStringLiteral("operation_id"), operationId},
                    {QStringLiteral("status"), QStringLiteral("applied")},
                    {QStringLiteral("block"), serialize(block)},
                };
                m_receipts.insert(operationId, result);
                results.append(result);
                continue;
            }

            if (kind == QLatin1String("update_block")) {
                const QString blockId = mutation.value(QStringLiteral("block_id")).toString();
                ServerBlock *block = nullptr;
                for (ServerBlock &candidate : document.blocks) {
                    if (candidate.id == blockId) {
                        block = &candidate;
                        break;
                    }
                }
                if (!block)
                    return respond(404, "Not Found", QJsonObject{{QStringLiteral("error"), QStringLiteral("no block")}});
                const int lockVersion = mutation.value(QStringLiteral("lock_version")).toInt();
                if (m_forceConflict || lockVersion != block->lockVersion) {
                    return respond(409, "Conflict",
                                   QJsonObject{
                                       {QStringLiteral("error"), QStringLiteral("conflict")},
                                       {QStringLiteral("index"), results.size()},
                                       {QStringLiteral("current_block"), serialize(*block)},
                                   });
                }
                block->content = mutation.value(QStringLiteral("attributes"))
                                     .toObject()
                                     .value(QStringLiteral("content"))
                                     .toString();
                ++block->lockVersion;
                appendChange(document, QStringLiteral("block_update"),
                             QJsonObject{{QStringLiteral("block"), serialize(*block)}});
                const QJsonObject result{
                    {QStringLiteral("operation_id"), operationId},
                    {QStringLiteral("status"), QStringLiteral("applied")},
                    {QStringLiteral("block"), serialize(*block)},
                };
                m_receipts.insert(operationId, result);
                results.append(result);
                continue;
            }

            if (kind == QLatin1String("update_title")) {
                document.title = mutation.value(QStringLiteral("title")).toString();
                ++document.titleVersion;
                const QJsonObject result{
                    {QStringLiteral("operation_id"), operationId},
                    {QStringLiteral("status"), QStringLiteral("applied")},
                    {QStringLiteral("title"), document.title},
                    {QStringLiteral("title_version"), document.titleVersion},
                };
                m_receipts.insert(operationId, result);
                results.append(result);
                continue;
            }

            const QJsonObject result{
                {QStringLiteral("operation_id"), operationId},
                {QStringLiteral("status"), QStringLiteral("applied")},
            };
            m_receipts.insert(operationId, result);
            results.append(result);
        }

        return respond(200, "OK",
                       QJsonObject{
                           {QStringLiteral("generation"), document.generation},
                           {QStringLiteral("cursor"), document.sequence},
                           {QStringLiteral("results"), results},
                       });
    }

    QHash<QString, Document> m_documents;
    QHash<QString, QJsonObject> m_receipts;
    bool m_forceConflict = false;
    bool m_documentMissing = false;
    QString m_accountEmail = QStringLiteral("stub@example.com");
    int m_remoteIds = 0;
};

struct Fixture
{
    QTemporaryDir dir;
    Workspace workspace;
    DocumentController document;
    AccountSession account;
    SyncEngine engine;
    StubDesktopApi api;

    bool setUp()
    {
        if (!api.listen() || !workspace.open(dir.path()))
            return false;

        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("Local doc"));
        document.setBlockContent(0, QStringLiteral("local paragraph"));
        if (!document.saveIfDirty())
            return false;

        account.setBaseUrl(api.baseUrl());
        if (!account.importToken(api.baseUrl(), QStringLiteral("stub-token")))
            return false;

        engine.setWorkspace(&workspace);
        engine.setAccount(&account);
        engine.setDocument(&document);
        return true;
    }

    QString localBlockId(int index = 0)
    {
        return document.blocks()->get(index).value(QStringLiteral("blockId")).toString();
    }
};

} // namespace

class TestSyncEngine : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qputenv("WRITERO_DISABLE_KEYRING", "1"); }

    void init()
    {
        AccountSession cleanup;
        cleanup.clearLocalSession();
    }

    void connectUploadsLocalDocument()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());

        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();

        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        QCOMPARE(fixture.engine.cloudId(), QStringLiteral("cloud-1"));
        QCOMPARE(fixture.engine.pendingCount(), 0);

        const QString remoteId =
            fixture.workspace.store()->remoteIdForLocal(fixture.document.documentId(),
                                                        fixture.localBlockId());
        QVERIFY(!remoteId.isEmpty());

        StubDesktopApi::Document *cloud = fixture.api.document(QStringLiteral("cloud-1"));
        QVERIFY(cloud != nullptr);
        // The local document has its paragraph plus the trailing empty block.
        QCOMPARE(cloud->blocks.size(), 2);
        QCOMPARE(cloud->blocks.first().content, QStringLiteral("local paragraph"));
    }

    void localEditsPushAndClearPending()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);

        fixture.document.setBlockContent(0, QStringLiteral("desktop edit"), false);
        QVERIFY(fixture.document.saveIfDirty());
        QCOMPARE(fixture.engine.pendingCount(), 1);

        fixture.engine.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        QCOMPARE(fixture.engine.pendingCount(), 0);

        StubDesktopApi::Document *cloud = fixture.api.document(QStringLiteral("cloud-1"));
        QCOMPARE(cloud->blocks.first().content, QStringLiteral("desktop edit"));
        QCOMPARE(cloud->blocks.first().lockVersion, 2);
    }

    void remoteChangesApplyWithoutDirtying()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);

        // Simulate a browser edit: mutate the stub and append a change.
        StubDesktopApi::Document *cloud = fixture.api.document(QStringLiteral("cloud-1"));
        cloud->blocks.first().content = QStringLiteral("browser edit");
        ++cloud->blocks.first().lockVersion;
        QJsonObject blockJson{
            {QStringLiteral("id"), cloud->blocks.first().id},
            {QStringLiteral("position"), 1},
            {QStringLiteral("block_type"), QStringLiteral("text")},
            {QStringLiteral("content"), QStringLiteral("browser edit")},
            {QStringLiteral("lock_version"), cloud->blocks.first().lockVersion},
        };
        ++cloud->sequence;
        cloud->changes.append(StubDesktopApi::ServerChange{
            cloud->sequence, QStringLiteral("block_update"),
            QJsonObject{{QStringLiteral("block"), blockJson}}});

        fixture.engine.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        QCOMPARE(fixture.document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("browser edit"));
        QVERIFY(!fixture.document.isDirty());
    }

    void lockConflictsAreRetainedAndResolvable()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);

        // Remote edit plus a local edit; both sides changed the same block.
        StubDesktopApi::Document *cloud = fixture.api.document(QStringLiteral("cloud-1"));
        cloud->blocks.first().content = QStringLiteral("remote version");
        ++cloud->blocks.first().lockVersion;

        fixture.document.setBlockContent(0, QStringLiteral("local version"), false);
        QVERIFY(fixture.document.saveIfDirty());

        fixture.api.forceConflict(true);
        fixture.engine.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("conflict"), 5000);
        QCOMPARE(fixture.engine.conflictCount(), 1);
        QVERIFY(fixture.engine.pendingCount() >= 1);
        QCOMPARE(fixture.document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("local version"));

        // Keep the local version: the engine retries with the remote lock.
        fixture.api.forceConflict(false);
        const QVariantMap conflict = fixture.engine.conflictList().first().toMap();
        fixture.engine.resolveConflict(conflict.value(QStringLiteral("id")).toString(), true);

        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        QCOMPARE(fixture.engine.conflictCount(), 0);
        QCOMPARE(cloud->blocks.first().content, QStringLiteral("local version"));
    }

    void remoteEditorialResultsAppearLocally()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);

        const QString remoteBlockId = fixture.workspace.store()->remoteIdForLocal(
            fixture.document.documentId(), fixture.localBlockId());
        fixture.api.appendResultChange(QStringLiteral("cloud-1"), remoteBlockId,
                                       QStringLiteral("9001"), QStringLiteral("Browser rewrite"));

        QSignalSpy results(&fixture.engine, &SyncEngine::cloudResultsChanged);
        fixture.engine.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        QVERIFY(results.count() >= 1);

        AiController ai;
        ai.setWorkspace(&fixture.workspace);
        ai.setDocument(&fixture.document);
        ai.setCurrentBlock(0);

        QTRY_VERIFY_WITH_TIMEOUT(!ai.results().isEmpty(), 5000);
        const QVariantMap result = ai.results().first().toMap();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(result.value(QStringLiteral("content")).toString(),
                 QStringLiteral("Browser rewrite"));
        QVERIFY(ai.applyResult(result.value(QStringLiteral("id")).toString(), false));
        QCOMPARE(fixture.document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("Browser rewrite"));

        // Re-applying the same remote change must not duplicate the result.
        const int before = ai.results().size();
        fixture.api.appendResultChange(QStringLiteral("cloud-1"), remoteBlockId,
                                       QStringLiteral("9001"), QStringLiteral("Browser rewrite"));
        fixture.engine.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        ai.refreshResults();
        QCOMPARE(ai.results().size(), before);
    }

    void documentsLinkedToAnotherAccountAreNotPushed()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        const int mutationsAfterConnect = fixture.api.mutationCount;

        // A different account signs in on this machine.
        fixture.api.setAccountEmail(QStringLiteral("someone@example.com"));
        AccountSession other;
        QVERIFY(other.importToken(fixture.api.baseUrl(), QStringLiteral("other-token")));
        QTRY_VERIFY_WITH_TIMEOUT(other.isConnected(), 5000);
        fixture.engine.setAccount(&other);

        fixture.document.setBlockContent(0, QStringLiteral("private local edit"), false);
        QVERIFY(fixture.document.saveIfDirty());
        QVERIFY(fixture.engine.pendingCount() >= 1);

        fixture.engine.syncNow();
        QCOMPARE(fixture.engine.state(), QStringLiteral("account_mismatch"));
        QCOMPARE(fixture.api.mutationCount, mutationsAfterConnect);
        QCOMPARE(fixture.document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("private local edit"));
    }

    void deletedCloudDocumentsKeepLocalWork()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);

        fixture.api.setDocumentMissing(true);
        fixture.document.setBlockContent(0, QStringLiteral("edit after deletion"), false);
        QVERIFY(fixture.document.saveIfDirty());

        fixture.engine.syncNow();
        // The push may succeed; the pull then reports the document as missing.
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("deleted"), 5000);
        QVERIFY(fixture.engine.lastError().contains(QStringLiteral("deleted")));
        QCOMPARE(fixture.document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("edit after deletion"));
        QVERIFY(fixture.document.isDirty() == false);
        QVERIFY(fixture.engine.pendingCount() >= 0);
    }

    void signedOutAccountsStopSyncingButKeepLocalWork()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);

        fixture.account.clearLocalSession();
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.account.isConnected(), 5000);

        fixture.document.setBlockContent(0, QStringLiteral("offline local edit"), false);
        QVERIFY(fixture.document.saveIfDirty());
        fixture.engine.syncNow();

        QCOMPARE(fixture.engine.state(), QStringLiteral("auth"));
        QCOMPARE(fixture.document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("offline local edit"));
        QVERIFY(fixture.engine.pendingCount() >= 1);
    }

    void duplicateAcknowledgementsDoNotDoubleApply()
    {
        Fixture fixture;
        QVERIFY(fixture.setUp());
        QTRY_VERIFY_WITH_TIMEOUT(fixture.account.isConnected(), 5000);
        fixture.engine.connectDocument();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);

        fixture.document.setBlockContent(0, QStringLiteral("retry me"), false);
        QVERIFY(fixture.document.saveIfDirty());

        const QVector<PendingOperation> pending =
            fixture.workspace.store()->pendingOperations(fixture.document.documentId());
        QCOMPARE(pending.size(), 1);

        fixture.engine.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        QCOMPARE(fixture.engine.pendingCount(), 0);
        const int mutationsAfterEdit = fixture.api.mutationCount;

        // Simulate a lost acknowledgement: the client retries the same
        // operation id after the server already committed it.
        QVERIFY(fixture.workspace.store()->saveDocument(
            fixture.document.session().document(), {}, pending));
        fixture.engine.refreshCounts();
        QCOMPARE(fixture.engine.pendingCount(), 1);

        fixture.engine.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(fixture.engine.state(), QStringLiteral("synced"), 5000);
        QCOMPARE(fixture.engine.pendingCount(), 0);
        QVERIFY(fixture.api.replayCount >= 1);
        QCOMPARE(fixture.api.mutationCount, mutationsAfterEdit);
        QCOMPARE(fixture.api.document(QStringLiteral("cloud-1"))->blocks.size(), 2);
    }
};

QTEST_MAIN(TestSyncEngine)
#include "tst_sync.moc"
