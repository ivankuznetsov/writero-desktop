#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "security/credentialstore.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

namespace writero {

/// Connects the desktop app to a Writero account.
///
/// Implements the authorization-code + PKCE flow with a loopback redirect:
/// the system browser opens the authorization URL and the app receives the
/// code on 127.0.0.1, exchanges it for a `desktop`-scoped Bearer token, and
/// stores the token in the credential store. Capabilities reported by the
/// server drive the account settings display; entitlement checks stay on the
/// server.
class AccountSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY changed)
    Q_PROPERTY(bool connected READ isConnected NOTIFY changed)
    Q_PROPERTY(bool busy READ isBusy NOTIFY changed)
    Q_PROPERTY(QString accountEmail READ accountEmail NOTIFY changed)
    Q_PROPERTY(QString plan READ plan NOTIFY changed)
    Q_PROPERTY(bool subscriptionActive READ subscriptionActive NOTIFY changed)
    Q_PROPERTY(bool hostedAiEnabled READ hostedAiEnabled NOTIFY changed)
    Q_PROPERTY(double remainingCreditUsd READ remainingCreditUsd NOTIFY changed)
    Q_PROPERTY(QStringList hostedModels READ hostedModels NOTIFY changed)
    Q_PROPERTY(QVariantList entitlements READ entitlements NOTIFY changed)
    Q_PROPERTY(int protocolVersion READ protocolVersion NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    explicit AccountSession(QObject *parent = nullptr);
    ~AccountSession() override;

    QString baseUrl() const { return m_baseUrl; }
    void setBaseUrl(const QString &baseUrl);

    bool isConnected() const { return m_connected; }
    bool isBusy() const { return m_busy; }
    QString accountEmail() const { return m_accountEmail; }
    QString plan() const { return m_plan; }
    bool subscriptionActive() const { return m_subscriptionActive; }
    bool hostedAiEnabled() const { return m_hostedAiEnabled; }
    double remainingCreditUsd() const { return m_remainingCreditUsd; }
    QStringList hostedModels() const { return m_hostedModels; }
    QVariantList entitlements() const { return m_entitlements; }
    int protocolVersion() const { return m_protocolVersion; }
    QString lastError() const { return m_lastError; }

    /// Starts sign-in: registers a loopback client if needed, opens the
    /// browser through `authorizationRequired`, and waits for the callback.
    Q_INVOKABLE void signIn();

    /// Fetches capabilities with the stored token (called on startup).
    Q_INVOKABLE void restoreSession();

    /// Revokes the token server-side and clears local credentials. Local
    /// documents are never touched.
    Q_INVOKABLE void signOut();

    /// Forgets the local session without contacting the server.
    Q_INVOKABLE void clearLocalSession();

    Q_INVOKABLE void refreshCapabilities();

    /// Bearer token for desktop API calls (empty when not signed in).
    Q_INVOKABLE QString accessToken() const;

    /// Imports an existing desktop-scoped token (for example from an
    /// environment variable or a test harness) and validates it against the
    /// server. Returns true when the server accepts it.
    Q_INVOKABLE bool importToken(const QString &baseUrl, const QString &token);

    /// Completes the flow from the loopback callback. Returns false when the
    /// state does not match the pending sign-in.
    Q_INVOKABLE bool completeAuthorization(const QString &code, const QString &state);

signals:
    void changed();
    void authorizationRequired(const QString &url);
    void signInFinished(bool success);

private:
    void setError(const QString &error);
    void setBusy(bool busy);
    void startLoopback();
    void stopLoopback();
    void handleCallbackRequest(QTcpSocket *socket, const QByteArray &request);
    void exchangeCode(const QString &code, const QString &codeVerifier);
    void fetchCapabilities();
    void applyCapabilities(const QByteArray &body);
    void storeToken(const QString &token);
    QString storedToken() const;
    QString redirectUri() const;
    static QString makeVerifier();
    static QString challengeFor(const QString &verifier);

    QNetworkAccessManager *m_network = nullptr;
    QTcpServer *m_loopback = nullptr;
    CredentialStore m_credentials;
    QString m_baseUrl = QStringLiteral("https://writero.app");
    QString m_clientId;
    QString m_pendingState;
    QString m_pendingVerifier;
    QString m_pendingRedirectUri;
    bool m_connected = false;
    bool m_busy = false;
    QString m_accountEmail;
    QString m_plan;
    bool m_subscriptionActive = false;
    bool m_hostedAiEnabled = false;
    double m_remainingCreditUsd = 0.0;
    QVariantList m_entitlements;
    QStringList m_hostedModels;
    int m_protocolVersion = 0;
    QString m_lastError;
};

} // namespace writero
