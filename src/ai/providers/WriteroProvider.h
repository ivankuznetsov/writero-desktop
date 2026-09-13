#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

namespace writero {

class AccountSession;

/// Transient context for one hosted operation: the target content and
/// bounded surrounding text, with no requirement for a persisted document.
struct HostedContext
{
    QString content;
    QString blockType = QStringLiteral("text");
    QString articleTitle;
    QStringList surrounding;
};

/// Executes hosted AI operations against the Writero desktop API.
///
/// This is the cloud route: the account's credits are reserved and settled
/// server-side, and the request carries only the selected operation context.
/// It never falls back to another provider; failures and ambiguous outcomes
/// are reported explicitly.
class WriteroProvider : public QObject
{
    Q_OBJECT

public:
    static const char *providerId();
    static bool isHostedProviderId(const QString &id);

    explicit WriteroProvider(AccountSession *account, QObject *parent = nullptr);
    ~WriteroProvider() override;

    void submit(const QString &operationId, const QString &kind, const QString &model,
                const QString &prompt, const HostedContext &context,
                const QString &referenceSignedId = {}, const QString &imageSignedId = {});
    void uploadMedia(const QString &requestId, const QString &path);
    void fetchResultMedia(const QString &operationId, qint64 jobId);
    void cancel(const QString &operationId);
    bool isRunning(const QString &operationId) const;

signals:
    void finished(const QString &operationId, const QString &content, const QJsonObject &usage);
    void imageFinished(const QString &operationId, const QByteArray &data,
                       const QString &contentType);
    void failed(const QString &operationId, const QString &error);
    void ambiguous(const QString &operationId, const QString &error);
    void mediaUploaded(const QString &requestId, const QString &signedId);
    void mediaUploadFailed(const QString &requestId, const QString &error);

private:
    AccountSession *m_account = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QHash<QString, QNetworkReply *> m_replies;
};

} // namespace writero
