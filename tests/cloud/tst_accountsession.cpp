#include <QtTest/QtTest>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

#include "cloud/accountsession.h"

using namespace writero;

namespace {

/// Minimal routing HTTP stub for the OAuth and capabilities endpoints.
class StubServer : public QTcpServer
{
public:
    explicit StubServer(QObject *parent = nullptr)
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
                const QByteArray head = buffer->left(headerEnd);
                const QList<QByteArray> lines = head.split('\n');
                const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
                const QByteArray method = requestLine.value(0);
                const QString path = QString::fromUtf8(requestLine.value(1));

                int contentLength = 0;
                QByteArray authorization;
                for (const QByteArray &line : lines) {
                    const QByteArray trimmed = line.trimmed();
                    if (trimmed.toLower().startsWith("content-length:"))
                        contentLength = trimmed.mid(15).trimmed().toInt();
                    if (trimmed.toLower().startsWith("authorization:"))
                        authorization = trimmed.mid(14).trimmed();
                }
                if (buffer->size() < headerEnd + 4 + contentLength)
                    return; // wait for the body

                const QByteArray body = buffer->mid(headerEnd + 4, contentLength);
                const QByteArray response = route(method, path, authorization,
                                                  QJsonDocument::fromJson(body).object());
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            });
        });
    }

    bool listen() { return QTcpServer::listen(QHostAddress::LocalHost, 0); }

    void setExpectedChallenge(const QString &challenge) { m_expectedChallenge = challenge; }
    bool tokenRequestOk() const { return m_tokenRequestOk; }
    bool capabilitiesCalled() const { return m_capabilitiesCalled; }
    bool revokeCalled() const { return m_revokeCalled; }

private:
    QByteArray route(const QByteArray &method, const QString &path,
                     const QByteArray &authorization, const QJsonObject &body)
    {
        const auto respond = [](int status, const QByteArray &reason, const QByteArray &json) {
            return QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason
                + QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ")
                + QByteArray::number(json.size())
                + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + json;
        };

        if (method == "POST" && path == QLatin1String("/oauth/register")) {
            m_registeredRedirect = body.value(QStringLiteral("redirect_uris")).toArray().at(0).toString();
            return respond(201, "Created", R"({"client_id":"stub-client"})");
        }

        if (method == "POST" && path == QLatin1String("/oauth/token")) {
            const QString verifier = body.value(QStringLiteral("code_verifier")).toString();
            const QByteArray digest = QCryptographicHash::hash(verifier.toUtf8(),
                                                               QCryptographicHash::Sha256);
            const QString challenge = QString::fromLatin1(
                digest.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
            m_tokenRequestOk = body.value(QStringLiteral("code")).toString() == "test-code"
                && body.value(QStringLiteral("client_id")).toString() == "stub-client"
                && !body.value(QStringLiteral("redirect_uri")).toString().isEmpty()
                && challenge == m_expectedChallenge
                && m_registeredRedirect == body.value(QStringLiteral("redirect_uri")).toString();
            if (!m_tokenRequestOk)
                return respond(400, "Bad Request", R"({"error":"invalid_grant"})");
            return respond(200, "OK",
                           R"({"access_token":"wrt_stub_token","token_type":"Bearer",
                               "expires_in":3600,"scope":"desktop"})");
        }

        if (method == "GET" && path == QLatin1String("/api/desktop/v1/capabilities")) {
            m_capabilitiesCalled = authorization == "Bearer wrt_stub_token";
            if (m_expireSession)
                return respond(401, "Unauthorized", R"({"error":"invalid token"})");
            if (!m_capabilitiesCalled)
                return respond(403, "Forbidden", R"({"error":"wrong audience"})");
            if (!capabilitiesOverride.isEmpty())
                return respond(200, "OK", capabilitiesOverride);
            return respond(200, "OK",
                           R"({"protocol":{"name":"writero-desktop","version":1},
                               "account":{"id":7,"email":"desk@example.com"},
                               "subscription":{"plan":"starter","active":true},
                               "hosted_ai":{"enabled":true,"remaining_credit_usd":12.5,"models":["text"],"image_models":["image"],"explanation_models":["explain"]},
                               "entitlements":["cloud_sync","hosted_ai"],
                               "limits":{"blocks_per_page":100,"max_media_mb":50}})");
        }

        if (method == "DELETE" && path == QLatin1String("/api/v1/auth/token")) {
            m_revokeCalled = authorization == "Bearer wrt_stub_token";
            return respond(200, "OK", R"({"message":"revoked"})");
        }

        return respond(404, "Not Found", R"({"error":"not found"})");
    }

public:
    void expireSession() { m_expireSession = true; }
    QByteArray capabilitiesOverride;

private:
    QString m_expectedChallenge;
    QString m_registeredRedirect;
    bool m_tokenRequestOk = false;
    bool m_capabilitiesCalled = false;
    bool m_revokeCalled = false;
    bool m_expireSession = false;
};

QUrl authorizeUrlFrom(const QSignalSpy &spy)
{
    return QUrl(spy.first().at(0).toString());
}

void completeBrowserCallback(StubServer &server, const QUrl &authorizeUrl)
{
    const QUrlQuery query(authorizeUrl);
    server.setExpectedChallenge(query.queryItemValue(QStringLiteral("code_challenge")));
    const QUrl redirect(query.queryItemValue(QStringLiteral("redirect_uri")));
    const QString state = query.queryItemValue(QStringLiteral("state"));

    QTcpSocket socket;
    socket.connectToHost(redirect.host(), quint16(redirect.port()));
    QVERIFY(socket.waitForConnected(2000));
    socket.write(QStringLiteral("GET /callback?code=test-code&state=%1 HTTP/1.1\r\n"
                                "Host: 127.0.0.1\r\n\r\n")
                     .arg(state)
                     .toUtf8());
    // Pump the event loop so the app's loopback server accepts and answers.
    QTRY_VERIFY_WITH_TIMEOUT(socket.bytesAvailable() > 0, 3000);
    QVERIFY(socket.readAll().contains("connected"));
}

} // namespace

class TestAccountSession : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("WRITERO_DISABLE_KEYRING", "1");
    }

    void init()
    {
        AccountSession cleanup;
        cleanup.clearLocalSession();
    }

    void connectsThroughBrowserLoopbackFlow()
    {
        StubServer server;
        QVERIFY(server.listen());

        AccountSession session;
        session.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

        QSignalSpy authorization(&session, &AccountSession::authorizationRequired);
        QSignalSpy finished(&session, &AccountSession::signInFinished);

        session.signIn();
        QTRY_COMPARE(authorization.count(), 1);

        const QUrl url = authorizeUrlFrom(authorization);
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("scope")), QStringLiteral("desktop"));
        QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")),
                 QStringLiteral("S256"));
        completeBrowserCallback(server, url);

        QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 5000);
        QCOMPARE(session.accountEmail(), QStringLiteral("desk@example.com"));
        QCOMPARE(session.plan(), QStringLiteral("starter"));
        QVERIFY(session.subscriptionActive());
        QVERIFY(session.hostedAiEnabled());
        QCOMPARE(session.remainingCreditUsd(), 12.5);
        QCOMPARE(session.protocolVersion(), 1);
        QVERIFY(session.entitlements().contains(QStringLiteral("hosted_ai")));
        QVERIFY(server.tokenRequestOk());
        QVERIFY(server.capabilitiesCalled());
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
    }

    void rejectsMismatchedState()
    {
        StubServer server;
        QVERIFY(server.listen());

        AccountSession session;
        session.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

        QSignalSpy authorization(&session, &AccountSession::authorizationRequired);
        QSignalSpy finished(&session, &AccountSession::signInFinished);
        session.signIn();
        QTRY_COMPARE(authorization.count(), 1);

        QVERIFY(!session.completeAuthorization(QStringLiteral("test-code"),
                                                QStringLiteral("not-the-state")));
        QVERIFY(session.lastError().contains(QStringLiteral("did not match")));
        QVERIFY(!session.isConnected());
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
    }

    void clearsExpiredSession()
    {
        StubServer server;
        QVERIFY(server.listen());

        AccountSession session;
        session.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

        QSignalSpy authorization(&session, &AccountSession::authorizationRequired);
        session.signIn();
        QTRY_COMPARE(authorization.count(), 1);
        completeBrowserCallback(server, authorizeUrlFrom(authorization));
        QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 5000);

        server.expireSession();
        session.refreshCapabilities();

        QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(), 5000);
        QVERIFY(session.lastError().contains(QStringLiteral("expired")));
        QVERIFY(session.accountEmail().isEmpty());
        QVERIFY(session.hostedModels().isEmpty());
    }

    void signOutRevokesAndClearsAccount()
    {
        StubServer server;
        QVERIFY(server.listen());

        AccountSession session;
        session.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));

        QSignalSpy authorization(&session, &AccountSession::authorizationRequired);
        session.signIn();
        QTRY_COMPARE(authorization.count(), 1);
        completeBrowserCallback(server, authorizeUrlFrom(authorization));
        QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 5000);

        session.signOut();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isConnected(), 5000);
        QTRY_VERIFY(server.revokeCalled());
        QVERIFY(session.accountEmail().isEmpty());
        QVERIFY(session.entitlements().isEmpty());
    }

    void changingOriginDoesNotExposeToken()
    {
        StubServer server;
        QVERIFY(server.listen());
        AccountSession session;
        QVERIFY(session.importToken(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), "wrt_stub_token"));
        QTRY_VERIFY(session.isConnected());
        session.setBaseUrl("http://127.0.0.1:1");
        QVERIFY(session.accessToken().isEmpty());
        QVERIFY(!session.isConnected());
        QVERIFY(session.accountEmail().isEmpty());
    }

    void clearDuringCapabilitiesCannotReconnect()
    {
        StubServer server;
        QVERIFY(server.listen());
        AccountSession session;
        session.importToken(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), "wrt_stub_token");
        session.clearLocalSession();
        QTest::qWait(200);
        QVERIFY(!session.isConnected());
        QVERIFY(session.accountEmail().isEmpty());
    }

    void clearRemovesModelCatalogs()
    {
        StubServer server;
        QVERIFY(server.listen());
        AccountSession session;
        session.importToken(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), "wrt_stub_token");
        QTRY_VERIFY(session.isConnected());
        QVERIFY(!session.hostedModels().isEmpty());
        session.clearLocalSession();
        QVERIFY(session.hostedModels().isEmpty());
        QVERIFY(session.hostedImageModels().isEmpty());
        QVERIFY(session.hostedExplanationModels().isEmpty());
    }

    void invalidCapabilities_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("invalid-json") << QByteArray("<html>Error</html>");
        QTest::newRow("unsupported-version") << QByteArray(R"({"protocol":{"version":2},"account":{"email":"a@example.com"}})");
    }

    void invalidCapabilities()
    {
        QFETCH(QByteArray, body);
        StubServer server;
        QVERIFY(server.listen());
        server.capabilitiesOverride = body;
        AccountSession session;
        session.importToken(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), "wrt_stub_token");
        QTRY_VERIFY(!session.isBusy());
        QVERIFY(!session.isConnected());
        QVERIFY(!session.lastError().isEmpty());
    }

    void callbackFragments_data()
    {
        QTest::addColumn<bool>("fragmented");
        QTest::newRow("fragmented") << true;
        QTest::newRow("forged-error") << false;
    }

    void callbackFragments()
    {
        QFETCH(bool, fragmented);
        StubServer server;
        QVERIFY(server.listen());
        AccountSession session;
        session.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy authorization(&session, &AccountSession::authorizationRequired);
        session.signIn();
        QTRY_COMPARE(authorization.count(), 1);
        const QUrlQuery query(authorizeUrlFrom(authorization));
        server.setExpectedChallenge(query.queryItemValue("code_challenge"));
        const QUrl redirect(query.queryItemValue("redirect_uri"));
        QTcpSocket socket;
        socket.connectToHost(redirect.host(), redirect.port());
        QVERIFY(socket.waitForConnected(2000));
        if (fragmented) {
            socket.write("GET /call");
            socket.flush();
            QTest::qWait(30);
            socket.write(QString("back?code=test-code&state=%1 HTTP/1.1\r\nHost: localhost\r\n\r\n").arg(query.queryItemValue("state")).toUtf8());
            socket.flush();
            QTRY_VERIFY_WITH_TIMEOUT(session.isConnected(), 1000);
        } else {
            socket.write("GET /callback?error=access_denied&state=forged HTTP/1.1\r\nHost: localhost\r\n\r\n");
            socket.flush();
            QTRY_VERIFY(socket.bytesAvailable() > 0);
            QVERIFY(session.isBusy());
            completeBrowserCallback(server, authorizeUrlFrom(authorization));
            QTRY_VERIFY(session.isConnected());
        }
    }

    void authorizationCodeCannotBeSubmittedTwice()
    {
        StubServer server;
        QVERIFY(server.listen());
        AccountSession session;
        session.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy authorization(&session, &AccountSession::authorizationRequired);
        session.signIn();
        QTRY_COMPARE(authorization.count(), 1);
        const QUrlQuery query(authorizeUrlFrom(authorization));
        server.setExpectedChallenge(query.queryItemValue("code_challenge"));
        QVERIFY(session.completeAuthorization("test-code", query.queryItemValue("state")));
        QVERIFY(!session.completeAuthorization("test-code", query.queryItemValue("state")));
        QTRY_VERIFY(session.isConnected());
    }

    void signOutCancelsPendingBrowser()
    {
        StubServer server;
        QVERIFY(server.listen());
        AccountSession session;
        session.setBaseUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy authorization(&session, &AccountSession::authorizationRequired);
        session.signIn();
        session.signOut();
        QTest::qWait(200);
        QCOMPARE(authorization.count(), 0);
        QVERIFY(!session.isBusy());
    }

    void originCredentialsRestoreOnlyAtTheirServer()
    {
        StubServer first;
        StubServer second;
        QVERIFY(first.listen());
        QVERIFY(second.listen());
        const QString firstUrl = QStringLiteral("http://127.0.0.1:%1").arg(first.serverPort());
        const QString secondUrl = QStringLiteral("http://127.0.0.1:%1").arg(second.serverPort());
        AccountSession session;
        session.importToken(firstUrl, "wrt_stub_token");
        QTRY_VERIFY(session.isConnected());
        session.setBaseUrl(secondUrl);
        session.restoreSession();
        QVERIFY(!session.isConnected());
        QVERIFY(session.accessToken().isEmpty());
        session.setBaseUrl(firstUrl);
        session.restoreSession();
        QTRY_VERIFY(session.isConnected());
        session.clearLocalSession();
    }

    void replacingAccountInvalidatesOldResponse()
    {
        StubServer first;
        StubServer second;
        QVERIFY(first.listen());
        QVERIFY(second.listen());
        first.expireSession();
        AccountSession session;
        session.importToken(QStringLiteral("http://127.0.0.1:%1").arg(first.serverPort()), "wrt_stub_token");
        session.importToken(QStringLiteral("http://127.0.0.1:%1").arg(second.serverPort()), "wrt_stub_token");
        QTRY_VERIFY(session.isConnected());
        QTest::qWait(100);
        QCOMPARE(session.accessToken(), QStringLiteral("wrt_stub_token"));
        QVERIFY(session.isConnected());
        session.clearLocalSession();
    }

    void sessionCredentialFallbackSharesAndRemovesSecrets()
    {
        CredentialStore first;
        CredentialStore second;
        QVERIFY(!first.isPersistent());
        const QString key = QStringLiteral("qa09.synthetic.credential");
        QVERIFY(first.store(key, "synthetic-one"));
        QCOMPARE(second.load(key), QStringLiteral("synthetic-one"));
        QVERIFY(second.store(key, "synthetic-two"));
        QCOMPARE(first.load(key), QStringLiteral("synthetic-two"));
        QVERIFY(first.remove(key));
        QVERIFY(second.load(key).isEmpty());
    }

    void restoreWithoutTokenStaysDisconnected()
    {
        AccountSession session;
        session.restoreSession();
        QVERIFY(!session.isConnected());
        QVERIFY(!session.isBusy());
    }
};

QTEST_MAIN(TestAccountSession)
#include "tst_accountsession.moc"
