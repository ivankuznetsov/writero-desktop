#include "cloud/accountsession.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

#include "document/block.h"

namespace writero {

namespace {

constexpr auto TokenKey = "oauth.desktop.access_token";

QString bearerHeader(const QString &token)
{
    return QStringLiteral("Bearer ") + token;
}

} // namespace

AccountSession::AccountSession(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

AccountSession::~AccountSession()
{
    stopLoopback();
}

void AccountSession::setBaseUrl(const QString &baseUrl)
{
    QString normalized = baseUrl;
    while (normalized.endsWith(QLatin1Char('/')))
        normalized.chop(1);
    if (m_baseUrl == normalized)
        return;
    m_baseUrl = normalized;
    emit changed();
}

void AccountSession::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit changed();
}

void AccountSession::setError(const QString &error)
{
    m_lastError = error;
    emit changed();
}

QString AccountSession::makeVerifier()
{
    QByteArray bytes(32, Qt::Uninitialized);
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = char(QRandomGenerator::global()->bounded(256));
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding
                                              | QByteArray::OmitTrailingEquals));
}

QString AccountSession::challengeFor(const QString &verifier)
{
    const QByteArray digest = QCryptographicHash::hash(verifier.toUtf8(),
                                                       QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding
                                               | QByteArray::OmitTrailingEquals));
}

QString AccountSession::redirectUri() const
{
    if (m_loopback == nullptr)
        return {};
    return QStringLiteral("http://127.0.0.1:%1/callback").arg(m_loopback->serverPort());
}

void AccountSession::startLoopback()
{
    stopLoopback();
    m_loopback = new QTcpServer(this);
    if (!m_loopback->listen(QHostAddress::LocalHost, 0)) {
        setError(QStringLiteral("Cannot open the local callback port: %1")
                     .arg(m_loopback->errorString()));
        stopLoopback();
        return;
    }
    connect(m_loopback, &QTcpServer::newConnection, this, [this] {
        QTcpSocket *socket = m_loopback->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { handleCallbackRequest(socket, socket->readAll()); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    });
}

void AccountSession::stopLoopback()
{
    if (m_loopback == nullptr)
        return;
    m_loopback->close();
    m_loopback->deleteLater();
    m_loopback = nullptr;
}

void AccountSession::handleCallbackRequest(QTcpSocket *socket, const QByteArray &request)
{
    if (socket->property("handled").toBool())
        return;
    if (!request.contains("HTTP/"))
        return;
    socket->setProperty("handled", true);

    const QList<QByteArray> requestLine = request.split('\n').value(0).trimmed().split(' ');
    const QUrl url = QUrl::fromEncoded(requestLine.value(1));
    const QUrlQuery query(url);
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    const QString error = query.queryItemValue(QStringLiteral("error"));

    const QByteArray body = error.isEmpty()
        ? QByteArrayLiteral("<html><body><h3>Writero is connected.</h3>"
                            "<p>You can close this window and return to the app.</p></body></html>")
        : QByteArrayLiteral("<html><body><h3>Writero sign-in failed.</h3>"
                            "<p>Return to the app for details.</p></body></html>");
    socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                                    "Connection: close\r\nContent-Length: ")
                  + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n") + body);
    socket->flush();
    socket->disconnectFromHost();

    if (!error.isEmpty()) {
        setError(QStringLiteral("Authorization was denied: %1").arg(error));
        stopLoopback();
        setBusy(false);
        emit signInFinished(false);
        return;
    }
    completeAuthorization(code, state);
}

void AccountSession::signIn()
{
    if (m_busy)
        return;
    setBusy(true);
    setError({});
    m_pendingVerifier = makeVerifier();
    m_pendingState = newId();
    startLoopback();
    if (m_loopback == nullptr)
        return;
    m_pendingRedirectUri = redirectUri();

    QJsonObject registration;
    registration.insert(QStringLiteral("client_name"), QStringLiteral("Writero Desktop"));
    registration.insert(QStringLiteral("redirect_uris"),
                        QJsonArray{m_pendingRedirectUri});

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/oauth/register")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_network->post(
        request, QJsonDocument(registration).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok) {
            setError(QStringLiteral("Could not register the client: %1")
                         .arg(QString::fromUtf8(body).left(200)));
            stopLoopback();
            setBusy(false);
            emit signInFinished(false);
            return;
        }

        m_clientId = QJsonDocument::fromJson(body)
                         .object()
                         .value(QStringLiteral("client_id"))
                         .toString();
        if (m_clientId.isEmpty()) {
            setError(QStringLiteral("The server did not return a client id."));
            stopLoopback();
            setBusy(false);
            emit signInFinished(false);
            return;
        }

        QUrl authorizeUrl(m_baseUrl + QStringLiteral("/oauth/authorize"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("client_id"), m_clientId);
        query.addQueryItem(QStringLiteral("redirect_uri"), m_pendingRedirectUri);
        query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
        query.addQueryItem(QStringLiteral("code_challenge"), challengeFor(m_pendingVerifier));
        query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
        query.addQueryItem(QStringLiteral("scope"), QStringLiteral("desktop"));
        query.addQueryItem(QStringLiteral("state"), m_pendingState);
        authorizeUrl.setQuery(query);

        emit authorizationRequired(authorizeUrl.toString());
    });
}

bool AccountSession::completeAuthorization(const QString &code, const QString &state)
{
    if (m_pendingState.isEmpty() || state != m_pendingState) {
        setError(QStringLiteral("The authorization response did not match this sign-in."));
        emit signInFinished(false);
        return false;
    }
    stopLoopback();
    exchangeCode(code, m_pendingVerifier);
    return true;
}

void AccountSession::exchangeCode(const QString &code, const QString &codeVerifier)
{
    QJsonObject body;
    body.insert(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("code_verifier"), codeVerifier);
    body.insert(QStringLiteral("client_id"), m_clientId);
    body.insert(QStringLiteral("redirect_uri"), m_pendingRedirectUri);

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/oauth/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_network->post(request,
                                           QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok) {
            setError(QStringLiteral("Token exchange failed: %1")
                         .arg(QString::fromUtf8(body).left(200)));
            setBusy(false);
            emit signInFinished(false);
            return;
        }
        const QString token = QJsonDocument::fromJson(body)
                                  .object()
                                  .value(QStringLiteral("access_token"))
                                  .toString();
        if (token.isEmpty()) {
            setError(QStringLiteral("The server did not return an access token."));
            setBusy(false);
            emit signInFinished(false);
            return;
        }
        storeToken(token);
        m_pendingState.clear();
        m_pendingVerifier.clear();
        fetchCapabilities();
    });
}

bool AccountSession::importToken(const QString &baseUrl, const QString &token)
{
    if (token.isEmpty())
        return false;
    setBaseUrl(baseUrl);
    storeToken(token);
    setBusy(true);
    setError({});
    fetchCapabilities();
    return true;
}

void AccountSession::restoreSession()
{
    if (storedToken().isEmpty()) {
        m_connected = false;
        emit changed();
        return;
    }
    setBusy(true);
    fetchCapabilities();
}

void AccountSession::refreshCapabilities()
{
    if (!isConnected())
        return;
    fetchCapabilities();
}

void AccountSession::fetchCapabilities()
{
    const QString token = storedToken();
    if (token.isEmpty()) {
        m_connected = false;
        setBusy(false);
        emit changed();
        return;
    }

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/api/desktop/v1/capabilities")));
    request.setRawHeader("Authorization", bearerHeader(token).toUtf8());
    QNetworkReply *reply = m_network->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();

        if (status == 401) {
            m_credentials.remove(QString::fromLatin1(TokenKey));
            m_connected = false;
            setError(QStringLiteral("The session expired. Sign in again."));
            setBusy(false);
            emit signInFinished(false);
            return;
        }
        if (!ok) {
            setError(QStringLiteral("Could not load account capabilities: %1")
                         .arg(QString::fromUtf8(body).left(200)));
            setBusy(false);
            emit signInFinished(false);
            return;
        }

        applyCapabilities(body);
        m_connected = true;
        setError({});
        setBusy(false);
        emit signInFinished(true);
    });
}

void AccountSession::applyCapabilities(const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const int version = root.value(QStringLiteral("protocol"))
                            .toObject()
                            .value(QStringLiteral("version"))
                            .toInt();
    m_protocolVersion = version;
    if (version != 1) {
        setError(QStringLiteral("This app version does not support the server protocol (v%1).")
                     .arg(version));
    }

    m_accountEmail = root.value(QStringLiteral("account"))
                         .toObject()
                         .value(QStringLiteral("email"))
                         .toString();
    const QJsonObject subscription = root.value(QStringLiteral("subscription")).toObject();
    m_plan = subscription.value(QStringLiteral("plan")).toString();
    m_subscriptionActive = subscription.value(QStringLiteral("active")).toBool();

    const QJsonObject hostedAi = root.value(QStringLiteral("hosted_ai")).toObject();
    m_hostedAiEnabled = hostedAi.value(QStringLiteral("enabled")).toBool();
    m_remainingCreditUsd = hostedAi.value(QStringLiteral("remaining_credit_usd")).toDouble();
    m_hostedModels.clear();
    for (const QJsonValue &value : hostedAi.value(QStringLiteral("models")).toArray())
        m_hostedModels.append(value.toString());
    m_hostedImageModels.clear();
    for (const QJsonValue &value : hostedAi.value(QStringLiteral("image_models")).toArray())
        m_hostedImageModels.append(value.toString());
    m_hostedExplanationModels.clear();
    for (const QJsonValue &value : hostedAi.value(QStringLiteral("explanation_models")).toArray())
        m_hostedExplanationModels.append(value.toString());

    m_entitlements.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("entitlements")).toArray())
        m_entitlements.append(value.toString());
}

void AccountSession::storeToken(const QString &token)
{
    QString error;
    m_credentials.store(QString::fromLatin1(TokenKey), token, &error);
    if (!error.isEmpty())
        setError(error);
}

QString AccountSession::storedToken() const
{
    return m_credentials.load(QString::fromLatin1(TokenKey));
}

QString AccountSession::accessToken() const
{
    return storedToken();
}

void AccountSession::clearLocalSession()
{
    m_credentials.remove(QString::fromLatin1(TokenKey));
    stopLoopback();
    m_connected = false;
    m_accountEmail.clear();
    m_plan.clear();
    m_subscriptionActive = false;
    m_hostedAiEnabled = false;
    m_remainingCreditUsd = 0.0;
    m_entitlements.clear();
    m_protocolVersion = 0;
    m_pendingState.clear();
    m_pendingVerifier.clear();
    m_pendingRedirectUri.clear();
    setBusy(false);
    emit changed();
}

void AccountSession::signOut()
{
    setBusy(true);
    const QString token = storedToken();
    m_credentials.remove(QString::fromLatin1(TokenKey));

    if (token.isEmpty()) {
        m_connected = false;
        setBusy(false);
        emit changed();
        return;
    }

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/api/v1/auth/token")));
    request.setRawHeader("Authorization", bearerHeader(token).toUtf8());
    QNetworkReply *reply = m_network->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_connected = false;
        m_accountEmail.clear();
        m_plan.clear();
        m_subscriptionActive = false;
        m_hostedAiEnabled = false;
        m_remainingCreditUsd = 0.0;
        m_entitlements.clear();
        m_hostedModels.clear();
        m_hostedImageModels.clear();
        m_hostedExplanationModels.clear();
        m_protocolVersion = 0;
        setError({});
        setBusy(false);
        emit changed();
    });
}

} // namespace writero
