#include <QtTest/QtTest>

#include <memory>

#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QTemporaryFile>

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
                const int headerEnd = m_request.indexOf("\r\n\r\n") + 4;
                int length = 0;
                for (const QByteArray &line : m_request.left(headerEnd).split('\n')) {
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toInt();
                }
                if (m_request.size() < headerEnd + length)
                    return;
                m_lastBody = m_request.mid(headerEnd, length);
                m_request.clear();

                const QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + m_status
                    + QByteArrayLiteral("\r\nContent-Type: ") + m_contentType
                    + QByteArrayLiteral("\r\nContent-Length: ")
                    + QByteArray::number(m_body.size())
                    + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + m_body;
                if (m_chunkSize <= 0) {
                    socket->write(response);
                    socket->disconnectFromHost();
                } else {
                    auto remaining = std::make_shared<QByteArray>(response);
                    auto timer = new QTimer(socket);
                    connect(timer, &QTimer::timeout, socket, [this, socket, timer, remaining] {
                        if (socket->state() != QAbstractSocket::ConnectedState) {
                            timer->stop();
                            return;
                        }
                        socket->write(remaining->left(m_chunkSize));
                        remaining->remove(0, m_chunkSize);
                        if (remaining->isEmpty()) {
                            timer->stop();
                            socket->disconnectFromHost();
                        }
                    });
                    timer->start(1);
                }
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

    void setChunkSize(int size) { m_chunkSize = size; }
    QString lastPath() const { return m_path; }
    QJsonObject lastBody() const { return QJsonDocument::fromJson(m_lastBody).object(); }

private:
    int m_chunkSize = 0;
    QByteArray m_status = QByteArrayLiteral("200 OK");
    QByteArray m_contentType = QByteArrayLiteral("application/json");
    QByteArray m_body;
    QByteArray m_request;
    QByteArray m_lastBody;
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
    void sseLineEndings_data()
    {
        QTest::addColumn<QByteArray>("separator");
        QTest::newRow("lf") << QByteArray("\n");
        QTest::newRow("crlf") << QByteArray("\r\n");
        QTest::newRow("cr") << QByteArray("\r");
    }

    void sseLineEndings()
    {
        QFETCH(QByteArray, separator);
        StubServer server;
        QVERIFY(server.listen());
        server.setChunkSize(7);
        const QByteArray frame = "data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}";
        server.setResponse("200 OK", "text/event-stream", frame + separator + separator + "data: [DONE]");
        AiClient client(openAiProfile(server.serverPort()), {});
        QSignalSpy finished(&client, &AiClient::chatFinished);
        QSignalSpy failed(&client, &AiClient::failed);
        client.chat("test", {});
        QTRY_VERIFY(!client.isRunning());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toString(), QString("hello"));
    }

    void fragmentedUnicode_data()
    {
        QTest::addColumn<int>("chunkSize");
        QTest::addColumn<bool>("ollama");
        for (int size : {1, 2, 3, 7, 17, 61}) {
            QTest::newRow(qPrintable(QString("sse-%1").arg(size))) << size << false;
            QTest::newRow(qPrintable(QString("ndjson-%1").arg(size))) << size << true;
        }
    }

    void fragmentedUnicode()
    {
        QFETCH(int, chunkSize);
        QFETCH(bool, ollama);
        StubServer server;
        QVERIFY(server.listen());
        server.setChunkSize(chunkSize);
        const QString text = QString::fromUtf8("你好 🦊 café");
        const QJsonObject message{{"content", text}};
        const QByteArray body = ollama
            ? QJsonDocument(QJsonObject{{"message", message}, {"done", true}}).toJson(QJsonDocument::Compact) + "\n"
            : "data: " + QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"delta", message}}}}}).toJson(QJsonDocument::Compact) + "\n\ndata: [DONE]\n\n";
        server.setResponse("200 OK", ollama ? "application/x-ndjson" : "text/event-stream", body);
        auto profile = openAiProfile(server.serverPort());
        if (ollama)
            profile.type = ProviderType::Ollama;
        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::chatFinished);
        client.chat("test", {});
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toString(), text);
    }

    void abortInsideFinalTokenDoesNotFinish()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("200 OK", "text/event-stream", "data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}");
        AiClient client(openAiProfile(server.serverPort()), {});
        QSignalSpy tokens(&client, &AiClient::tokenReceived);
        QSignalSpy finished(&client, &AiClient::chatFinished);
        connect(&client, &AiClient::tokenReceived, &client, &AiClient::abort);
        client.chat("test", {});
        QTRY_COMPARE(tokens.count(), 1);
        QCOMPARE(finished.count(), 0);
    }

    void ollamaFinalFrameWithoutNewline()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("200 OK", "application/x-ndjson",
                           R"({"message":{"content":"final"},"done":true,"prompt_eval_count":7,"eval_count":2})");
        auto profile = openAiProfile(server.serverPort());
        profile.type = ProviderType::Ollama;
        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::chatFinished);
        client.chat("test", {});
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toString(), QString("final"));
        QCOMPARE(finished.first().at(1).toInt(), 7);
    }

    void ollamaUsesNativeOptionsAndImages()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("200 OK", "application/x-ndjson", "{\"done\":true}\n");
        auto profile = openAiProfile(server.serverPort());
        profile.type = ProviderType::Ollama;
        ModelCapabilities capabilities;
        capabilities.imageInput = true;
        AiClient client(profile, {}, capabilities);
        QSignalSpy finished(&client, &AiClient::chatFinished);
        client.chat("vision", {{"user", "Describe", "image", "image/png"}}, 0.2, 31);
        QTRY_COMPARE(finished.count(), 1);
        const auto body = server.lastBody();
        const auto message = body.value("messages").toArray().first().toObject();
        QCOMPARE(message.value("content").toString(), QString("Describe"));
        QCOMPARE(message.value("images").toArray().first().toString(), QString("aW1hZ2U="));
        QCOMPARE(body.value("options").toObject().value("temperature").toDouble(), 0.2);
        QCOMPARE(body.value("options").toObject().value("num_predict").toInt(), 31);
    }

    void streamErrorsDoNotCompleteSuccessfully_data()
    {
        QTest::addColumn<bool>("ollama");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("openai") << false << QByteArray("data: {\"error\":{\"message\":\"quota exhausted\"}}\n\n");
        QTest::newRow("ollama") << true << QByteArray("{\"error\":\"quota exhausted\"}\n");
        QTest::newRow("malformed-sse") << false << QByteArray("data: {oops}\n\n");
        QTest::newRow("malformed-ndjson") << true << QByteArray("{oops}\n");
    }

    void streamErrorsDoNotCompleteSuccessfully()
    {
        QFETCH(bool, ollama);
        QFETCH(QByteArray, body);
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("200 OK", ollama ? "application/x-ndjson" : "text/event-stream", body);
        auto profile = openAiProfile(server.serverPort());
        if (ollama)
            profile.type = ProviderType::Ollama;
        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::chatFinished);
        QSignalSpy failures(&client, &AiClient::failed);
        client.chat("test", {});
        QTRY_VERIFY(!client.isRunning());
        QCOMPARE(failures.count(), 1);
        QCOMPARE(finished.count(), 0);
    }

    void httpErrorRetainsDiagnostic()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("429 Too Many Requests", "application/json", "{\"error\":{\"message\":\"quota exhausted\"}}\n");
        AiClient client(openAiProfile(server.serverPort()), {});
        QSignalSpy failures(&client, &AiClient::failed);
        client.chat("test", {});
        QTRY_COMPARE(failures.count(), 1);
        QVERIFY(failures.first().first().toString().contains("quota exhausted"));
    }

    void sseMultilineData()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("200 OK", "text/event-stream",
                           "data: {\n" // JSON whitespace belongs to the event, not a separate object.
                           "data: \"choices\":[{\"delta\":{\"content\":\"hello\"}}]}\n\n"
                           "data: [DONE]\n\n");
        AiClient client(openAiProfile(server.serverPort()), {});
        QSignalSpy finished(&client, &AiClient::chatFinished);
        client.chat("test", {});
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().first().toString(), QString("hello"));
    }

    void rejectsProviderLocalFileImageUrl()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write("private local bytes");
        file.flush();
        StubServer server;
        QVERIFY(server.listen());
        const QJsonObject image{{"image_url", QJsonObject{{"url", QUrl::fromLocalFile(file.fileName()).toString()}}}};
        const QJsonObject body{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"images", QJsonArray{image}}}}}}}};
        server.setResponse("200 OK", "application/json", QJsonDocument(body).toJson());
        auto profile = openAiProfile(server.serverPort());
        profile.type = ProviderType::OpenRouter;
        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::imageFinished);
        QSignalSpy failed(&client, &AiClient::failed);
        client.generateImage("image", "test");
        QTRY_VERIFY(!client.isRunning());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(failed.count(), 1);
    }

    void rejectsInvalidOpenRouterDataUri_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::newRow("invalid-base64") << QString("data:image/png;base64,!!!");
        QTest::newRow("missing-comma") << QString("data:image/png;base64");
        QTest::newRow("wrong-media") << QString("data:text/plain;base64,aGVsbG8=");
        QTest::newRow("not-base64") << QString("data:image/png,hello");
    }

    void rejectsInvalidOpenRouterDataUri()
    {
        QFETCH(QString, uri);
        StubServer server;
        QVERIFY(server.listen());
        const QJsonObject image{{"image_url", QJsonObject{{"url", uri}}}};
        const QJsonObject body{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"images", QJsonArray{image}}}}}}}};
        server.setResponse("200 OK", "application/json", QJsonDocument(body).toJson());
        auto profile = openAiProfile(server.serverPort());
        profile.type = ProviderType::OpenRouter;
        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::imageFinished);
        QSignalSpy failed(&client, &AiClient::failed);
        client.generateImage("image", "test");
        QTRY_VERIFY(!client.isRunning());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(failed.count(), 1);
    }

    void generatedImageDownloadCompletes()
    {
        StubServer imageServer;
        QVERIFY(imageServer.listen());
        imageServer.setResponse("200 OK", "image/webp", "image bytes");
        StubServer server;
        QVERIFY(server.listen());
        const QString url = QString("http://127.0.0.1:%1/image").arg(imageServer.serverPort());
        const QJsonObject image{{"image_url", QJsonObject{{"url", url}}}};
        const QJsonObject body{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"images", QJsonArray{image}}}}}}}};
        server.setResponse("200 OK", "application/json", QJsonDocument(body).toJson());
        auto profile = openAiProfile(server.serverPort());
        profile.type = ProviderType::OpenRouter;
        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::imageFinished);
        client.generateImage("image", "test");
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toByteArray(), QByteArray("image bytes"));
        QCOMPARE(finished.first().at(1).toString(), QString("image/webp"));
        QVERIFY(!client.isRunning());
    }

    void generatedImageDownloadCanBeCanceled()
    {
        StubServer imageServer;
        QVERIFY(imageServer.listen());
        imageServer.setChunkSize(1);
        imageServer.setResponse("200 OK", "image/png", QByteArray(1000, 'x'));
        StubServer server;
        QVERIFY(server.listen());
        const QString url = QString("http://127.0.0.1:%1/image").arg(imageServer.serverPort());
        const QJsonObject image{{"image_url", QJsonObject{{"url", url}}}};
        const QJsonObject body{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"images", QJsonArray{image}}}}}}}};
        server.setResponse("200 OK", "application/json", QJsonDocument(body).toJson());
        auto profile = openAiProfile(server.serverPort());
        profile.type = ProviderType::OpenRouter;
        AiClient client(profile, {});
        QSignalSpy finished(&client, &AiClient::imageFinished);
        client.generateImage("image", "test");
        QTRY_COMPARE(imageServer.lastPath(), QString("/image"));
        QVERIFY(client.isRunning());
        const auto sockets = imageServer.findChildren<QTcpSocket *>();
        QCOMPARE(sockets.size(), 1);
        client.abort();
        QTRY_COMPARE_WITH_TIMEOUT(sockets.first()->state(), QAbstractSocket::UnconnectedState, 1000);
        QVERIFY(!client.isRunning());
        QCOMPARE(finished.count(), 0);
    }

    void rejectsInvalidBase64Image()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setResponse("200 OK", "application/json", R"({"data":[{"b64_json":"!!!"}]})");
        AiClient client(openAiProfile(server.serverPort()), {});
        QSignalSpy finished(&client, &AiClient::imageFinished);
        QSignalSpy failures(&client, &AiClient::failed);
        client.generateImage("image", "test");
        QTRY_VERIFY(!client.isRunning());
        QCOMPARE(failures.count(), 1);
        QCOMPARE(finished.count(), 0);
    }

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
