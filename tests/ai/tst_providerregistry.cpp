#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "ai/providerregistry.h"
#include "security/credentialstore.h"
#include "storage/workspace.h"

using namespace writero;

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
