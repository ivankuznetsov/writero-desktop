#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "ai/aicontroller.h"
#include "ai/providers/WriteroProvider.h"
#include "cloud/accountsession.h"
#include "editor/documentcontroller.h"
#include "storage/workspace.h"

using namespace writero;

namespace {

/// Stub for the hosted AI endpoints: capabilities for token import and
/// /ai_jobs with a switchable outcome.
class StubHostedApi : public QTcpServer
{
public:
    enum class Outcome { Completed, Failed, Ambiguous, ServerError };

    explicit StubHostedApi(QObject *parent = nullptr)
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

                lastBody = QJsonDocument::fromJson(buffer->mid(headerEnd + 4, contentLength))
                               .object();
                requestCount++;

                if (method == "GET" && path.startsWith(QLatin1String("/api/desktop/v1/capabilities"))) {
                    respond(socket, 200, capabilitiesBody());
                } else if (method == "POST"
                           && path.startsWith(QLatin1String("/api/desktop/v1/ai_jobs"))) {
                    respond(socket, 200, jobBody());
                } else {
                    respond(socket, 404, QJsonObject{{QStringLiteral("error"), QStringLiteral("not found")}});
                }
            });
        });
    }

    bool listen() { return QTcpServer::listen(QHostAddress::LocalHost, 0); }
    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }

    QJsonObject lastBody;
    int requestCount = 0;
    Outcome outcome = Outcome::Completed;

private:
    QJsonObject capabilitiesBody() const
    {
        return QJsonObject{
            {QStringLiteral("protocol"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("writero-desktop")},
                         {QStringLiteral("version"), 1}}},
            {QStringLiteral("account"),
             QJsonObject{{QStringLiteral("id"), 1},
                         {QStringLiteral("email"), QStringLiteral("hosted@example.com")}}},
            {QStringLiteral("subscription"),
             QJsonObject{{QStringLiteral("plan"), QStringLiteral("starter")},
                         {QStringLiteral("active"), true}}},
            {QStringLiteral("hosted_ai"),
             QJsonObject{{QStringLiteral("enabled"), true},
                         {QStringLiteral("remaining_credit_usd"), 9.99},
                         {QStringLiteral("models"),
                          QJsonArray{QStringLiteral("google/gemini-3.6-flash"),
                                     QStringLiteral("x-ai/grok-4.5")}}}},
        };
    }

    QJsonObject jobBody() const
    {
        switch (outcome) {
        case Outcome::Completed:
            return QJsonObject{
                {QStringLiteral("id"), 1},
                {QStringLiteral("status"), QStringLiteral("completed")},
                {QStringLiteral("content"), QStringLiteral("Hosted result")},
                {QStringLiteral("settled"), true},
                {QStringLiteral("usage"),
                 QJsonObject{{QStringLiteral("input_tokens"), 10},
                             {QStringLiteral("output_tokens"), 5},
                             {QStringLiteral("cost_usd"), 0.0001}}},
            };
        case Outcome::Failed:
            return QJsonObject{
                {QStringLiteral("id"), 2},
                {QStringLiteral("status"), QStringLiteral("failed")},
                {QStringLiteral("error"), QStringLiteral("Model not available")},
                {QStringLiteral("settled"), false},
            };
        case Outcome::Ambiguous:
            return QJsonObject{
                {QStringLiteral("id"), 3},
                {QStringLiteral("status"), QStringLiteral("ambiguous")},
                {QStringLiteral("error"), QStringLiteral("Provider outcome unknown")},
                {QStringLiteral("settled"), false},
            };
        case Outcome::ServerError:
            return {};
        }
        return {};
    }

    static void respond(QTcpSocket *socket, int status, const QJsonObject &body)
    {
        const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
        const QByteArray reason = status == 200 ? "OK" : "Error";
        socket->write(QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason
                      + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
                      + QByteArray::number(json.size())
                      + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + json);
        socket->flush();
        socket->disconnectFromHost();
    }
};

} // namespace

class TestHostedJobs : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { qputenv("WRITERO_DISABLE_KEYRING", "1"); }

    void init()
    {
        AccountSession cleanup;
        cleanup.clearLocalSession();
    }

    void providerSubmitsTransientContext()
    {
        StubHostedApi api;
        QVERIFY(api.listen());

        AccountSession account;
        QVERIFY(account.importToken(api.baseUrl(), QStringLiteral("token")));
        QTRY_VERIFY_WITH_TIMEOUT(account.isConnected(), 5000);
        QVERIFY(account.hostedAiEnabled());
        QCOMPARE(account.hostedModels().size(), 2);

        WriteroProvider provider(&account);
        QSignalSpy finished(&provider, &WriteroProvider::finished);

        HostedContext context;
        context.content = QStringLiteral("local paragraph");
        context.blockType = QStringLiteral("text");
        context.articleTitle = QStringLiteral("Local doc");
        context.surrounding = { QStringLiteral("previous paragraph") };
        provider.submit(QStringLiteral("op-1"), QStringLiteral("rewrite"),
                        QStringLiteral("google/gemini-3.6-flash"), QStringLiteral("shorten"),
                        context);

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("Hosted result"));
        QCOMPARE(api.lastBody.value(QStringLiteral("operation_id")).toString(),
                 QStringLiteral("op-1"));
        const QJsonObject sentContext = api.lastBody.value(QStringLiteral("context")).toObject();
        QCOMPARE(sentContext.value(QStringLiteral("content")).toString(),
                 QStringLiteral("local paragraph"));
        QCOMPARE(sentContext.value(QStringLiteral("article_title")).toString(),
                 QStringLiteral("Local doc"));
        QCOMPARE(sentContext.value(QStringLiteral("surrounding")).toArray().size(), 1);
    }

    void providerReportsFailuresAndAmbiguity()
    {
        StubHostedApi api;
        QVERIFY(api.listen());

        AccountSession account;
        QVERIFY(account.importToken(api.baseUrl(), QStringLiteral("token")));
        QTRY_VERIFY_WITH_TIMEOUT(account.isConnected(), 5000);

        WriteroProvider provider(&account);
        QSignalSpy failed(&provider, &WriteroProvider::failed);
        QSignalSpy ambiguous(&provider, &WriteroProvider::ambiguous);

        HostedContext context;
        context.content = QStringLiteral("text");

        api.outcome = StubHostedApi::Outcome::Failed;
        provider.submit(QStringLiteral("op-fail"), QStringLiteral("rewrite"),
                        QStringLiteral("google/gemini-3.6-flash"), QStringLiteral("x"), context);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("Model not available"));

        api.outcome = StubHostedApi::Outcome::Ambiguous;
        provider.submit(QStringLiteral("op-ambiguous"), QStringLiteral("rewrite"),
                        QStringLiteral("google/gemini-3.6-flash"), QStringLiteral("x"), context);
        QTRY_COMPARE_WITH_TIMEOUT(ambiguous.count(), 1, 5000);
    }

    void controllerRunsHostedRewriteAndAppliesResult()
    {
        StubHostedApi api;
        QVERIFY(api.listen());

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController document;
        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("Hosted doc"));
        document.setBlockContent(0, QStringLiteral("transient text"));

        AccountSession account;
        QVERIFY(account.importToken(api.baseUrl(), QStringLiteral("token")));
        QTRY_VERIFY_WITH_TIMEOUT(account.isConnected(), 5000);

        AiController ai;
        ai.setWorkspace(&workspace);
        ai.setDocument(&document);
        ai.setAccount(&account);

        ai.runRewrite(0, QStringLiteral("writero"), QStringLiteral("google/gemini-3.6-flash"),
                      QStringLiteral("make it shorter"));
        QTRY_VERIFY_WITH_TIMEOUT(!ai.results().isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(ai.results().first().toMap()
                                      .value(QStringLiteral("status")).toString(),
                                  QStringLiteral("completed"), 5000);

        const QVariantMap result = ai.results().first().toMap();
        QCOMPARE(result.value(QStringLiteral("content")).toString(),
                 QStringLiteral("Hosted result"));
        QVERIFY(ai.applyResult(result.value(QStringLiteral("id")).toString(), false));
        QCOMPARE(document.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("Hosted result"));
    }

    void controllerReportsHostedImageToolsAsUnavailable()
    {
        StubHostedApi api;
        QVERIFY(api.listen());

        QTemporaryDir dir;
        Workspace workspace;
        QVERIFY(workspace.open(dir.path()));

        DocumentController document;
        document.setWorkspace(&workspace);
        document.createDocument(QStringLiteral("Hosted doc"));

        AccountSession account;
        QVERIFY(account.importToken(api.baseUrl(), QStringLiteral("token")));
        QTRY_VERIFY_WITH_TIMEOUT(account.isConnected(), 5000);

        AiController ai;
        ai.setWorkspace(&workspace);
        ai.setDocument(&document);
        ai.setAccount(&account);

        ai.runImageGeneration(0, QStringLiteral("writero"),
                              QStringLiteral("google/gemini-2.5-flash-image"),
                              QStringLiteral("a cat"));
        QTRY_VERIFY_WITH_TIMEOUT(!ai.results().isEmpty(), 5000);
        const QVariantMap result = ai.results().first().toMap();
        QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(
            QStringLiteral("not available")));
    }
};

QTEST_MAIN(TestHostedJobs)
#include "tst_hosted_jobs.moc"
