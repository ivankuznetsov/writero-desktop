#include <QtTest/QtTest>

#include <QTcpServer>
#include <QTcpSocket>

#include "ai/modelcatalog.h"

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
                m_path = QString::fromUtf8(m_request).section(QLatin1Char(' '), 1, 1);
                m_request.clear();
                socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
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
    QString lastPath() const { return m_path; }

private:
    QByteArray m_request;
    QByteArray m_body;
    QString m_path;
};

ModelCapabilities find(const QVector<ModelCapabilities> &models, const QString &id)
{
    for (const ModelCapabilities &model : models) {
        if (model.id == id)
            return model;
    }
    return {};
}

} // namespace

class TestModelCatalog : public QObject
{
    Q_OBJECT

private slots:
    void parsesOpenRouterModalities()
    {
        const QByteArray body = R"({
            "data": [
                {"id": "google/gemini-3-pro-image",
                 "name": "Gemini 3 Pro Image",
                 "architecture": {"input_modalities": ["image", "text"],
                                  "output_modalities": ["image", "text"]}},
                {"id": "anthropic/claude-sonnet-4.6",
                 "architecture": {"input_modalities": ["text", "image"],
                                  "output_modalities": ["text"]}},
                {"id": "plain/text-model",
                 "architecture": {"input_modalities": ["text"], "output_modalities": ["text"]}}
            ]})";

        const QVector<ModelCapabilities> models =
            ModelCatalog::parse(ProviderType::OpenRouter, body);
        QCOMPARE(models.size(), 3);

        const ModelCapabilities image = find(models, QStringLiteral("google/gemini-3-pro-image"));
        QVERIFY(image.imageOutput);
        QVERIFY(image.imageInput);
        QVERIFY(image.modalitiesKnown);

        const ModelCapabilities claude =
            find(models, QStringLiteral("anthropic/claude-sonnet-4.6"));
        QVERIFY(!claude.imageOutput);
        QVERIFY(claude.imageInput);

        const ModelCapabilities text = find(models, QStringLiteral("plain/text-model"));
        QVERIFY(!text.imageOutput);
        QVERIFY(!text.imageInput);
        QVERIFY(text.textOutput);
    }

    void infersCapabilitiesWhenModalitiesAreMissing()
    {
        const QByteArray body = R"({
            "data": [
                {"id": "dall-e-3"},
                {"id": "gpt-4o"},
                {"id": "text-embedding-3-small"}
            ]})";

        const QVector<ModelCapabilities> models =
            ModelCatalog::parse(ProviderType::OpenAiCompatible, body);
        QVERIFY(find(models, QStringLiteral("dall-e-3")).imageOutput);
        QVERIFY(find(models, QStringLiteral("gpt-4o")).imageInput);
        QVERIFY(!find(models, QStringLiteral("gpt-4o")).imageOutput);
        QVERIFY(!find(models, QStringLiteral("text-embedding-3-small")).imageInput);
        QVERIFY(!find(models, QStringLiteral("dall-e-3")).modalitiesKnown);
    }

    void parsesOllamaTags()
    {
        const QByteArray body = R"({
            "models": [
                {"name": "llava:latest"},
                {"name": "llama3:latest"}
            ]})";

        const QVector<ModelCapabilities> models =
            ModelCatalog::parse(ProviderType::Ollama, body);
        QCOMPARE(models.size(), 2);
        QVERIFY(find(models, QStringLiteral("llava:latest")).imageInput);
        QVERIFY(!find(models, QStringLiteral("llava:latest")).imageOutput);
        QVERIFY(!find(models, QStringLiteral("llama3:latest")).imageInput);
        QVERIFY(!find(models, QStringLiteral("llama3:latest")).imageOutput);
    }

    void fetchesFromProviderEndpoint()
    {
        StubServer server;
        QVERIFY(server.listen());
        server.setBody(R"({"data":[{"id":"vision-model",
            "architecture":{"input_modalities":["text","image"],
                            "output_modalities":["text"]}}]})");

        ProviderProfile profile;
        profile.id = QStringLiteral("stub");
        profile.type = ProviderType::OpenAiCompatible;
        profile.baseUrl = QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort());

        ModelCatalog catalog;
        QSignalSpy finished(&catalog, &ModelCatalog::finished);
        catalog.fetch(profile, QStringLiteral("key"));

        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(server.lastPath(), QStringLiteral("/v1/models"));
        QVERIFY(find(catalog.models(), QStringLiteral("vision-model")).imageInput);
    }

    void cacheRoundTrips()
    {
        ModelCapabilities model;
        model.id = QStringLiteral("m");
        model.name = QStringLiteral("Model");
        model.imageOutput = true;
        model.imageInput = true;
        model.modalitiesKnown = true;

        const QString json = ModelCatalog::toJson({model});
        const QVector<ModelCapabilities> loaded = ModelCatalog::fromJson(json);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().id, model.id);
        QVERIFY(loaded.first().imageOutput);
        QVERIFY(loaded.first().imageInput);
        QVERIFY(loaded.first().modalitiesKnown);
    }
};

QTEST_MAIN(TestModelCatalog)
#include "tst_modelcatalog.moc"
