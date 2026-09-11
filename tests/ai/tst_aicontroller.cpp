#include <QtTest/QtTest>

#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

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

class TestAiController : public QObject
{
    Q_OBJECT

private slots:
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
