#include <QtTest/QtTest>

#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "ai/providerregistry.h"
#include "security/credentialstore.h"
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
                socket->write(
                    QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                      "Content-Length: ")
                    + QByteArray::number(m_body.size())
                    + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + m_body);
                socket->flush();
                socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return QTcpServer::listen(QHostAddress::LocalHost, 0); }
    void setBody(const QByteArray &body) { m_body = body; }

private:
    QByteArray m_request;
    QByteArray m_body;
};

} // namespace

class TestProviderRegistry : public QObject
{
    Q_OBJECT

private slots:
    void credentialsFallBackToSession()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");
        CredentialStore store;
        QVERIFY(!store.isPersistent());

        QVERIFY(store.store(QStringLiteral("provider.test"), QStringLiteral("secret-value")));
        QCOMPARE(store.load(QStringLiteral("provider.test")), QStringLiteral("secret-value"));
        QVERIFY(store.remove(QStringLiteral("provider.test")));
        QCOMPARE(store.load(QStringLiteral("provider.test")), QString());
    }

    void profilesPersistWithoutSecrets()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        ProviderRegistry registry;
        registry.setWorkspace(&workspace);
        QCOMPARE(registry.profileList().size(), 0);

        const QString id = registry.addProvider(
            QStringLiteral("My Ollama"), QStringLiteral("ollama"),
            QStringLiteral("http://localhost:11434"), QStringLiteral("llama3"),
            QStringLiteral("top-secret-key"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(registry.profileList().size(), 1);
        QVERIFY(registry.hasCredential(id));

        // The key must never appear in workspace settings.
        QVERIFY(!workspace.setting(QStringLiteral("ai.providers"))
                     .contains(QStringLiteral("top-secret-key")));

        ProviderRegistry reopened;
        reopened.setWorkspace(&workspace);
        QCOMPARE(reopened.profileList().size(), 1);
        QCOMPARE(reopened.profile(id).defaultModel, QStringLiteral("llama3"));
        QCOMPARE(reopened.profile(id).type, ProviderType::Ollama);
        QVERIFY(reopened.hasCredential(id));

        registry.updateProvider(id, {}, QStringLiteral("http://example.test"), QString());
        ProviderRegistry updated;
        updated.setWorkspace(&workspace);
        QCOMPARE(updated.profile(id).baseUrl, QStringLiteral("http://example.test"));
        QCOMPARE(updated.profile(id).defaultModel, QStringLiteral("llama3"));

        registry.removeProvider(id);
        QCOMPARE(registry.profileList().size(), 0);
        QVERIFY(!registry.hasCredential(id));
        ProviderRegistry afterRemoval;
        afterRemoval.setWorkspace(&workspace);
        QCOMPARE(afterRemoval.profileList().size(), 0);
    }

    void modelCatalogFiltersOperationsAndCaches()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        StubServer server;
        QVERIFY(server.listen());
        server.setBody(R"({"data":[
            {"id":"text-only","architecture":{"input_modalities":["text"],
                                              "output_modalities":["text"]}},
            {"id":"img","architecture":{"input_modalities":["image","text"],
                                        "output_modalities":["image","text"]}},
            {"id":"vision","architecture":{"input_modalities":["text","image"],
                                           "output_modalities":["text"]}}
        ]})");

        ProviderRegistry registry;
        registry.setWorkspace(&workspace);
        const QString id = registry.addProvider(
            QStringLiteral("OR"), QStringLiteral("openrouter"),
            QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort()), {}, QString());

        QSignalSpy changed(&registry, &ProviderRegistry::modelsChanged);
        registry.refreshModels(id);
        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);

        QCOMPARE(registry.modelsFor(id, QStringLiteral("text")).size(), 3);
        QCOMPARE(registry.modelsFor(id, QStringLiteral("generate")).size(), 1);
        QCOMPARE(registry.modelsFor(id, QStringLiteral("explain")).size(), 2);
        QCOMPARE(registry.modelsFor(id, QStringLiteral("generate"))
                     .first()
                     .toMap()
                     .value(QStringLiteral("id"))
                     .toString(),
                 QStringLiteral("img"));

        QVERIFY(registry.modelSupportsReference(id, QStringLiteral("img")));
        QVERIFY(!registry.modelSupportsReference(id, QStringLiteral("text-only")));
        QVERIFY(!registry.modelSupportsImageGeneration(id, QStringLiteral("vision")));
        QVERIFY(registry.modelSupportsImageInput(id, QStringLiteral("vision")));

        // The catalog is cached in workspace settings and reloads with it.
        ProviderRegistry reopened;
        reopened.setWorkspace(&workspace);
        QVERIFY(reopened.hasModels(id));
        QCOMPARE(reopened.modelsFor(id, QStringLiteral("generate")).size(), 1);
    }

    void removedProviderClearsCachedModels()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));
        ProviderRegistry registry;
        registry.setWorkspace(&workspace);
        StubServer server;
        QVERIFY(server.listen());
        server.setBody(R"({"data":[{"id":"gpt-4o"}]})");
        const QString id = registry.addProvider({}, QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});
        registry.refreshModels(id);
        QTRY_VERIFY(registry.hasModels(id));
        registry.removeProvider(id);
        QVERIFY(!registry.hasModels(id));
        QVERIFY(workspace.setting(QStringLiteral("ai.models.") + id).isEmpty());
    }

    void endpointEditInvalidatesCachedModels()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));
        ProviderRegistry registry;
        registry.setWorkspace(&workspace);
        StubServer server;
        QVERIFY(server.listen());
        server.setBody(R"({"data":[{"id":"old-model"}]})");
        const QString id = registry.addProvider({}, QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});
        registry.refreshModels(id);
        QTRY_VERIFY(registry.hasModels(id));
        registry.updateProvider(id, {}, QStringLiteral("http://localhost:45108/v1"), {});
        QVERIFY(!registry.hasModels(id));
        QVERIFY(workspace.setting(QStringLiteral("ai.models.") + id).isEmpty());
    }

    void removedProviderCannotReceiveInFlightCatalog()
    {
        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));
        ProviderRegistry registry;
        registry.setWorkspace(&workspace);
        StubServer server;
        QVERIFY(server.listen());
        server.setBody(R"({"data":[{"id":"old-model"}]})");
        const QString id = registry.addProvider({}, QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});
        registry.refreshModels(id);
        registry.removeProvider(id);
        QTest::qWait(100);
        QVERIFY(!registry.hasModels(id));
        QVERIFY(!registry.modelsLoading(id));
    }

    void workspaceSwitchCannotReceivePreviousCatalog()
    {
        QTemporaryDir dir, otherDir;
        Workspace workspace, other;
        QVERIFY(workspace.open(dir.path()));
        QVERIFY(other.open(otherDir.path()));
        ProviderRegistry registry;
        registry.setWorkspace(&workspace);
        StubServer server;
        QVERIFY(server.listen());
        server.setBody(R"({"data":[{"id":"old-model"}]})");
        const QString id = registry.addProvider({}, QStringLiteral("openai-compatible"),
            QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()), {}, {});
        registry.refreshModels(id);
        registry.setWorkspace(&other);
        QTest::qWait(100);
        QVERIFY(!registry.hasModels(id));
        QVERIFY(other.setting(QStringLiteral("ai.models.") + id).isEmpty());
        QVERIFY(!registry.modelsLoading(id));
    }

    void createClientCarriesProfile()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        ProviderRegistry registry;
        registry.setWorkspace(&workspace);
        const QString id = registry.addProvider(QStringLiteral("OpenRouter"),
                                                QStringLiteral("openrouter"), {}, {}, QString());
        QVERIFY(!id.isEmpty());

        AiClient *client = registry.createClient(id);
        QVERIFY(client != nullptr);
        QVERIFY(client->supportsImageGeneration());
        delete client;

        QVERIFY(registry.createClient(QStringLiteral("missing")) == nullptr);
    }
};

QTEST_MAIN(TestProviderRegistry)
#include "tst_providerregistry.moc"
