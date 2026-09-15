#include <QtTest/QtTest>

#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "ai/aicontroller.h"
#include "storage/workspace.h"

using namespace writero;

namespace {

class StubServer : public QTcpServer
{
public:
    explicit StubServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                m_request += socket->readAll();
                if (!m_request.contains("\r\n\r\n"))
                    return;
                m_request.clear();
                socket->write(m_response);
                socket->flush();
                socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return QTcpServer::listen(QHostAddress::LocalHost, 0); }

    void setSse(const QString &text)
    {
        const QByteArray payload = QByteArrayLiteral(
            "data: {\"choices\":[{\"delta\":{\"content\":\"")
            + text.toUtf8().replace("\"", "\\\"")
            + QByteArrayLiteral("\"}}],\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1}}"
                                "\n\ndata: [DONE]\n\n");
        m_response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                                       "Content-Length: ")
            + QByteArray::number(payload.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + payload;
    }

private:
    QByteArray m_request;
    QByteArray m_response;
};

} // namespace

struct AiFixture
{
    QTemporaryDir dir;
    Workspace workspace;
    ProviderRegistry providers;
    DocumentController document;
    AiController ai;
    AiFixture()
    {
        workspace.open(dir.path());
        providers.setWorkspace(&workspace);
        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("Synthetic QA"));
        document.setBlockContent(0, QStringLiteral("original"));
        ai.setWorkspace(&workspace);
        ai.setProviders(&providers);
        ai.setDocument(&document);
    }
    QString result(const QString &kind = QStringLiteral("rewrite"), bool target = true)
    {
        WorkspaceStore::AiResultRecord record;
        record.id = newId();
        record.documentId = document.documentId();
        if (target)
            record.blockId = document.session().document().blocks.first().id;
        record.kind = kind;
        record.content = QStringLiteral("replacement");
        record.status = QStringLiteral("completed");
        record.baseRevision = document.session().document().blocks.first().revision;
        record.createdAt = QDateTime::currentDateTimeUtc();
        workspace.store()->saveAiResult(record);
        ai.refreshResults();
        return record.id;
    }
    QString provider(StubServer &server)
    {
        return providers.addProvider(QStringLiteral("Stub"), QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});
    }
};

class TestAiController : public QObject
{
    Q_OBJECT

private slots:
    void rewriteWithoutSetupReturnsEmpty()
    {
        AiController ai;
        QVERIFY(ai.runRewrite(0, {}, QStringLiteral("model"), {}).isEmpty());
    }

    void insertedAlternativeHasFreshIdentity()
    {
        AiFixture f;
        const QString originalId = f.document.session().document().blocks.first().id;
        QVERIFY(f.ai.applyResult(f.result(), true));
        const auto blocks = f.document.session().document().blocks;
        QCOMPARE(blocks.at(0).content, QStringLiteral("original"));
        QCOMPARE(blocks.at(1).content, QStringLiteral("replacement"));
        QVERIFY(blocks.at(1).id != originalId);
    }

    void deletedTargetCannotCrashInsertion()
    {
        AiFixture f;
        const QString id = f.result();
        f.document.session().removeBlock(0);
        QVERIFY(!f.ai.applyResult(id, true));
    }

    void bulkSummaryCannotBeInsertedAsBlock()
    {
        AiFixture f;
        const QString id = f.result(QStringLiteral("polish"), false);
        QVERIFY(!f.ai.applyResult(id, true));
    }

    void explanationProducesTextBlock()
    {
        AiFixture f;
        Block media = f.document.session().document().blocks.first();
        media.type = BlockType::Media;
        media.mediaId = 42;
        f.document.session().updateBlock(0, media);
        QVERIFY(f.ai.applyResult(f.result(QStringLiteral("image_explanation")), true));
        const Block inserted = f.document.session().document().blocks.at(1);
        QCOMPARE(inserted.type, BlockType::Text);
        QCOMPARE(inserted.mediaId, 0);
    }

    void invalidBulkEntryCannotEraseText()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        const QString id = f.document.session().document().blocks.first().id;
        server.setSse(QStringLiteral("[{\"id\":\"%1\",\"content\":null}]").arg(id));
        f.ai.runPolish(f.provider(server), QStringLiteral("model"));
        QTRY_VERIFY(!f.ai.busy());
        QCOMPARE(f.document.session().document().blocks.first().content, QStringLiteral("original"));
    }

    void staleBulkResultsRemainReviewable()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        const QString id = f.document.session().document().blocks.first().id;
        server.setSse(QStringLiteral("[{\"id\":\"%1\",\"content\":\"corrected\"}]").arg(id));
        f.ai.runPolish(f.provider(server), QStringLiteral("model"));
        f.document.setBlockContent(0, QStringLiteral("edited while running"));
        QTRY_VERIFY(!f.ai.busy());
        QCOMPARE(f.document.session().document().blocks.first().content, QStringLiteral("edited while running"));
        bool found = false;
        for (const auto &result : f.ai.results()) {
            const QVariantMap row = result.toMap();
            if (row.value(QStringLiteral("content")) == QStringLiteral("corrected")) {
                found = true;
                QVERIFY(row.value(QStringLiteral("stale")).toBool());
            }
        }
        QVERIFY(found);
    }

    void documentReplacementClearsOldResults()
    {
        AiFixture f;
        f.result();
        QCOMPARE(f.ai.results().size(), 1);
        DocumentController other;
        other.setWorkspace(&f.workspace);
        other.createDocument(QStringLiteral("Other"));
        f.ai.setDocument(&other);
        QVERIFY(f.ai.results().isEmpty());
    }

    void whitespaceModelsDoNotCreateOperations()
    {
        AiFixture f;
        QVERIFY(f.ai.runRewrite(0, {}, QStringLiteral(" ,  , "), {}).isEmpty());
        QVERIFY(f.ai.results().isEmpty());
    }

    void workspaceChangeCancelsPendingRequests()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("late result"));
        const QString id = f.ai.runRewrite(0, f.provider(server), QStringLiteral("model"), {});
        QVERIFY(f.ai.busy());
        f.ai.setWorkspace(nullptr);
        QVERIFY(!f.ai.busy());
        const auto records = f.workspace.store()->aiResults(f.document.documentId(), {}, 50);
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().id, id);
        QCOMPARE(records.first().status, QStringLiteral("failed"));
    }

    void codeRewritePreservesIndentation()
    {
        AiFixture f;
        Block code = f.document.session().document().blocks.first();
        code.type = BlockType::Code;
        f.document.session().updateBlock(0, code);
        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("    indented_code()"));
        const QString id = f.ai.runRewrite(0, f.provider(server), QStringLiteral("model"), {});
        QTRY_VERIFY(!f.ai.busy());
        QVERIFY(f.ai.applyResult(id, false));
        QCOMPARE(f.document.session().document().blocks.first().content,
                 QStringLiteral("    indented_code()"));
    }

    void explanationWithoutImageDoesNotDispatch()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("hallucinated description"));
        f.ai.runImageExplanation(0, f.provider(server), QStringLiteral("gpt-4o"), {});
        QVERIFY(!f.ai.busy());
        for (const auto &row : f.ai.results())
            QVERIFY(row.toMap().value(QStringLiteral("status")) != QStringLiteral("completed"));
    }

    void resultPersistenceFailureDoesNotDispatch()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("result"));
        const QString provider = f.provider(server);
        f.workspace.store()->close();
        QVERIFY(f.ai.runRewrite(0, provider, QStringLiteral("model"), {}).isEmpty());
        QVERIFY(!f.ai.busy());
    }

    void failedResultWriteDoesNotDispatch()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("untracked result"));
        const QString provider = f.provider(server);
        const QString connection = newId();
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(f.dir.path() + QStringLiteral("/workspace.db"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("CREATE TRIGGER qa_reject_result BEFORE INSERT ON ai_results "
                "BEGIN SELECT RAISE(FAIL, 'synthetic disk failure'); END")));
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(f.workspace.isReady());
        QVERIFY(f.ai.runRewrite(0, provider, QStringLiteral("model"), {}).isEmpty());
        QVERIFY(!f.ai.busy());
    }

    void requestedReferenceMustExist()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        f.ai.runImageGeneration(0, f.provider(server), QStringLiteral("dall-e-3"),
                                QStringLiteral("synthetic image"), true);
        QVERIFY(!f.ai.busy());
    }

    void modelRefreshRetainsSelectedProvider()
    {
        AiFixture f;
        const QString first = f.providers.addProvider(QStringLiteral("First"),
            QStringLiteral("ollama"), {}, {}, {});
        const QString second = f.providers.addProvider(QStringLiteral("Second"),
            QStringLiteral("ollama"), {}, {}, {});
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import Writero\nAiPanel {}", QUrl());
        QScopedPointer<QObject> panel(component.createWithInitialProperties(
            {{QStringLiteral("ai"), QVariant::fromValue(&f.ai)}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        QObject *box = nullptr;
        for (QObject *child : panel->findChildren<QObject *>()) {
            if (child->property("displayText").toString() == QStringLiteral("First")) {
                box = child;
                break;
            }
        }
        QVERIFY(box);
        box->setProperty("currentIndex", 1);
        QCOMPARE(panel->property("providerId").toString(), second);
        f.providers.setCredential(first, QStringLiteral("synthetic"));
        QCOMPARE(panel->property("providerId").toString(), second);
    }

    void concurrentAlternativesSettleIndependently()
    {
        AiFixture f;
        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("stress result"));
        QStringList models;
        for (int i = 0; i < 40; ++i)
            models.append(QStringLiteral("synthetic-%1").arg(i));
        f.ai.runRewrite(0, f.provider(server), models.join(QLatin1Char(',')), {});
        QTRY_VERIFY_WITH_TIMEOUT(!f.ai.busy(), 10000);
        QCOMPARE(f.ai.results().size(), 40);
        QSet<QString> ids;
        for (const auto &row : f.ai.results()) {
            const auto result = row.toMap();
            QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
            ids.insert(result.value(QStringLiteral("id")).toString());
        }
        QCOMPARE(ids.size(), 40);
    }

    void rewriteAppliesToBlock()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("REWRITTEN TEXT"));

        ProviderRegistry providers;
        providers.setWorkspace(&workspace);
        const QString providerId = providers.addProvider(
            QStringLiteral("Stub"), QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()),
            QStringLiteral("test-model"), QString());

        DocumentController document;
        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("AI test"));
        document.setBlockContent(0, QStringLiteral("original text"));

        AiController ai;
        ai.setWorkspace(&workspace);
        ai.setProviders(&providers);
        ai.setDocument(&document);

        ai.runRewrite(0, providerId, QStringLiteral("test-model"), QStringLiteral("shorten"));
        QTRY_VERIFY_WITH_TIMEOUT(ai.results().size() == 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ai.results().first().toMap()
                                         .value(QStringLiteral("status")).toString()
                                     == QLatin1String("completed"),
                                 5000);

        const QVariantMap result = ai.results().first().toMap();
        QCOMPARE(result.value(QStringLiteral("content")).toString(),
                 QStringLiteral("REWRITTEN TEXT"));
        QVERIFY(ai.applyResult(result.value(QStringLiteral("id")).toString(), false));
        QCOMPARE(document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("REWRITTEN TEXT"));
    }

    void multiModelProducesSeparateResults()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("ALT"));

        ProviderRegistry providers;
        providers.setWorkspace(&workspace);
        const QString providerId = providers.addProvider(
            QStringLiteral("Stub"), QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});

        DocumentController document;
        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("Multi"));
        document.setBlockContent(0, QStringLiteral("text"));

        AiController ai;
        ai.setWorkspace(&workspace);
        ai.setProviders(&providers);
        ai.setDocument(&document);

        ai.runRewrite(0, providerId, QStringLiteral("model-a,model-b"), QStringLiteral("tweak"));
        QTRY_VERIFY_WITH_TIMEOUT(ai.results().size() == 2
                                     && ai.results().at(0).toMap()
                                            .value(QStringLiteral("status")).toString()
                                         == QLatin1String("completed")
                                     && ai.results().at(1).toMap()
                                            .value(QStringLiteral("status")).toString()
                                         == QLatin1String("completed"),
                                 5000);
        for (const QVariant &value : ai.results())
            QCOMPARE(value.toMap().value(QStringLiteral("status")).toString(),
                     QStringLiteral("completed"));
        QVERIFY(ai.results().first().toMap().value(QStringLiteral("model")).toString()
                != ai.results().last().toMap().value(QStringLiteral("model")).toString());
    }

    void bulkPolishAppliesAndReportsStaleBlocks()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        StubServer server;
        QVERIFY(server.listen());

        ProviderRegistry providers;
        providers.setWorkspace(&workspace);
        const QString providerId = providers.addProvider(
            QStringLiteral("Stub"), QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});

        DocumentController document;
        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("Bulk"));
        document.setBlockContent(0, QStringLiteral("hello wrold"));

        const QString blockId =
            document.blocks()->get(0).value(QStringLiteral("blockId")).toString();
        server.setSse(QStringLiteral("[{\"id\":\"%1\",\"content\":\"hello world\"}]")
                          .arg(blockId));

        AiController ai;
        ai.setWorkspace(&workspace);
        ai.setProviders(&providers);
        ai.setDocument(&document);

        QSignalSpy notices(&ai, &AiController::notice);
        ai.runPolish(providerId, QStringLiteral("test-model"));

        QTRY_COMPARE_WITH_TIMEOUT(document.blocks()->get(0)
                                      .value(QStringLiteral("content"))
                                      .toString(),
                                  QStringLiteral("hello world"), 5000);
        QVERIFY(notices.count() > 0);
        QVERIFY(notices.last().at(0).toString().contains(QStringLiteral("Updated 1")));
    }

    void reportPlainTextBulkResultFails()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        StubServer server;
        QVERIFY(server.listen());
        server.setSse(QStringLiteral("I could not produce JSON."));

        ProviderRegistry providers;
        providers.setWorkspace(&workspace);
        const QString providerId = providers.addProvider(
            QStringLiteral("Stub"), QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});

        DocumentController document;
        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("Bad bulk"));
        document.setBlockContent(0, QStringLiteral("text"));

        AiController ai;
        ai.setWorkspace(&workspace);
        ai.setProviders(&providers);
        ai.setDocument(&document);

        QSignalSpy notices(&ai, &AiController::notice);
        ai.runPolish(providerId, QStringLiteral("test-model"));
        QTRY_VERIFY_WITH_TIMEOUT(notices.count() > 0, 5000);
        QVERIFY(notices.last().at(0).toString().contains(QStringLiteral("no block changes")));
        QCOMPARE(document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("text"));
    }
};

QTEST_MAIN(TestAiController)
#include "tst_aicontroller.moc"
