#include <QtTest/QtTest>

#include <QTcpServer>
#include <QTcpSocket>

#include "ai/aiclient.h"

using namespace writero;

namespace {

/// Minimal HTTP stub: answers each request with a canned response.
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
                m_path = QString::fromUtf8(m_request).section(QLatin1Char(' '), 1, 1);
                m_request.clear();

                const QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + m_status
                    + QByteArrayLiteral("\r\nContent-Type: ") + m_contentType
                    + QByteArrayLiteral("\r\nContent-Length: ")
                    + QByteArray::number(m_body.size())
                    + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + m_body;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
        });
    }

    bool listen()
    {
        return QTcpServer::listen(QHostAddress::LocalHost, 0);
    }

    void setResponse(const QByteArray &status, const QByteArray &contentType,
                     const QByteArray &body)
    {
        m_status = status;
        m_contentType = contentType;
        m_body = body;
    }

    QString lastPath() const { return m_path; }

private:
    QByteArray m_status = QByteArrayLiteral("200 OK");
    QByteArray m_contentType = QByteArrayLiteral("application/json");
    QByteArray m_body;
    QByteArray m_request;
    QString m_path;
};

ProviderProfile openAiProfile(int port)
{
    ProviderProfile profile;
    profile.id = QStringLiteral("test-provider");
    profile.name = QStringLiteral("Test");
    profile.type = ProviderType::OpenAiCompatible;
    profile.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(port);
    profile.defaultModel = QStringLiteral("test-model");
    return profile;
}

} // namespace

class TestAiClient : public QObject
{
    Q_OBJECT

private slots:
    void streamsOpenAiCompatibleChat()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse(
            "200 OK", "text/event-stream",
            "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}],"
            "\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":2}}\n\n"
            "data: [DONE]\n\n");

        AiClient client(openAiProfile(server.serverPort()), QStringLiteral("secret-key"));
        QSignalSpy tokens(&client, &AiClient::tokenReceived);
        QSignalSpy finished(&client, &AiClient::chatFinished);

        AiMessage message;
        message.role = QStringLiteral("user");
        message.content = QStringLiteral("Hi");
        client.chat(QStringLiteral("test-model"), {message});

        QTRY_COMPARE(finished.count(), 1);
        const QList<QVariant> arguments = finished.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QStringLiteral("Hello"));
        QCOMPARE(arguments.at(1).toInt(), 3);
        QCOMPARE(arguments.at(2).toInt(), 2);
        QCOMPARE(tokens.count(), 2);
        QCOMPARE(server.lastPath(), QStringLiteral("/v1/chat/completions"));
    }

    void streamsOllamaChat()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("200 OK", "application/x-ndjson",
                           "{\"message\":{\"content\":\"He\"},\"done\":false}\n"
                           "{\"message\":{\"content\":\"y\"},\"done\":true,"
                           "\"prompt_eval_count\":2,\"eval_count\":1}\n");

        ProviderProfile profile;
        profile.id = QStringLiteral("ollama");
        profile.type = ProviderType::Ollama;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());

        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::chatFinished);

        AiMessage message;
        message.role = QStringLiteral("user");
        message.content = QStringLiteral("Hi");
        client.chat(QStringLiteral("llama3"), {message});

        QTRY_COMPARE(finished.count(), 1);
        const QList<QVariant> arguments = finished.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QStringLiteral("Hey"));
        QCOMPARE(arguments.at(1).toInt(), 2);
        QCOMPARE(arguments.at(2).toInt(), 1);
        QCOMPARE(server.lastPath(), QStringLiteral("/api/chat"));
    }

    void reportsHttpErrors()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("401 Unauthorized", "application/json",
                           "{\"error\":{\"message\":\"bad key\"}}");

        AiClient client(openAiProfile(server.serverPort()), QStringLiteral("bad"));
        QSignalSpy failures(&client, &AiClient::failed);

        AiMessage message;
        message.role = QStringLiteral("user");
        message.content = QStringLiteral("Hi");
        client.chat(QStringLiteral("test-model"), {message});

        QTRY_COMPARE(failures.count(), 1);
        QVERIFY(failures.first().at(0).toString().contains(QStringLiteral("failed")));
    }

    void ollamaRejectsImageGeneration()
    {
        ProviderProfile profile;
        profile.type = ProviderType::Ollama;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:1");

        AiClient client(profile, {});
        QSignalSpy failures(&client, &AiClient::failed);
        client.generateImage(QStringLiteral("llava"), QStringLiteral("a cat"));

        QCOMPARE(failures.count(), 1);
        QVERIFY(failures.first().at(0).toString().contains(QStringLiteral("does not support")));
        QVERIFY(!client.supportsImageGeneration());
    }

    void rejectsUnsupportedReferenceImages()
    {
        ModelCapabilities capabilities;
        capabilities.id = QStringLiteral("text-to-image");
        capabilities.textOutput = true;
        capabilities.imageOutput = true;
        capabilities.imageInput = false;
        capabilities.modalitiesKnown = true;

        AiClient client(openAiProfile(1), QStringLiteral("key"), capabilities);
        QSignalSpy failures(&client, &AiClient::failed);
        client.generateImage(QStringLiteral("text-to-image"), QStringLiteral("a cat"),
                             QByteArray("reference-bytes"), QStringLiteral("image/png"));

        QCOMPARE(failures.count(), 1);
        QVERIFY(failures.first().at(0).toString().contains(QStringLiteral("reference")));
    }

    void rejectsVisionRequestsForTextOnlyModels()
    {
        ModelCapabilities capabilities;
        capabilities.id = QStringLiteral("text-only");
        capabilities.textOutput = true;
        capabilities.modalitiesKnown = true;

        AiClient client(openAiProfile(1), QStringLiteral("key"), capabilities);
        QSignalSpy failures(&client, &AiClient::failed);

        AiMessage message;
        message.role = QStringLiteral("user");
        message.content = QStringLiteral("what is this?");
        message.imageData = QByteArray("image-bytes");
        message.imageMime = QStringLiteral("image/png");
        client.chat(QStringLiteral("text-only"), {message});

        QCOMPARE(failures.count(), 1);
        QVERIFY(failures.first().at(0).toString().contains(QStringLiteral("image input")));
    }

    void openAiCompatibleImageGeneration()
    {
        StubServer server;
        QVERIFY(server.listen());
        const QByteArray pixels("png-bytes");
        const QByteArray body =
            QByteArrayLiteral("{\"data\":[{\"b64_json\":\"") + pixels.toBase64()
            + QByteArrayLiteral("\"}]}");
        server.setResponse("200 OK", "application/json", body);

        AiClient client(openAiProfile(server.serverPort()), QStringLiteral("key"));
        QSignalSpy finished(&client, &AiClient::imageFinished);
        client.generateImage(QStringLiteral("test-image-model"), QStringLiteral("a cat"));

        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toByteArray(), pixels);
        QCOMPARE(server.lastPath(), QStringLiteral("/v1/images/generations"));
    }

    void openRouterChatImageGeneration()
    {
        StubServer server;
        QVERIFY(server.listen());
        const QByteArray pixels("image-data");
        const QString dataUri = QStringLiteral("data:image/png;base64,")
            + QString::fromLatin1(pixels.toBase64());
        const QByteArray body =
            QByteArrayLiteral("{\"choices\":[{\"message\":{\"images\":[{\"type\":\"image_url\","
                              "\"image_url\":{\"url\":\"")
            + dataUri.toUtf8() + QByteArrayLiteral("\"}}]}}]}");
        server.setResponse("200 OK", "application/json", body);

        ProviderProfile profile;
        profile.type = ProviderType::OpenRouter;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:%1/api/v1").arg(server.serverPort());

        AiClient client(profile, QStringLiteral("key"));
        QSignalSpy finished(&client, &AiClient::imageFinished);
        client.generateImage(QStringLiteral("google/gemini-image"), QStringLiteral("a cat"));

        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toByteArray(), pixels);
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("image/png"));
        QCOMPARE(server.lastPath(), QStringLiteral("/api/v1/chat/completions"));
    }
};

QTEST_MAIN(TestAiClient)
#include "tst_aiclient.moc"
