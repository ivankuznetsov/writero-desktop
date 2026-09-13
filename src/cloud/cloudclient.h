#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace writero {

class AccountSession;

/// Authenticated transport for the Writero desktop API.
///
/// One request per call; results and failures are reported through signals.
/// Failures carry the HTTP status and parsed body so callers can distinguish
/// authentication (401), conflicts (409), and resnapshot requests (410).
class CloudClient : public QObject
{
    Q_OBJECT

public:
    explicit CloudClient(QObject *parent = nullptr);

    void setAccount(AccountSession *account);
    QString baseUrl() const;
    bool hasToken() const;

    void createDocument(const QString &title);
    void fetchSnapshot(const QString &documentId);
    void fetchSnapshotPage(const QString &documentId, const QString &leaseId, int page);
    void fetchChanges(const QString &documentId, qint64 cursor, qint64 generation, int limit = 200);
    void fetchHistory(const QString &documentId, const QString &remoteBlockId);
    void postMutations(const QString &documentId, const QJsonArray &mutations);
    void uploadMedia(const QString &path);
    void downloadMedia(const QString &documentId, const QString &remoteBlockId);

signals:
    void documentCreated(const QJsonObject &body);
    void snapshotReceived(const QJsonObject &body);
    void changesReceived(const QJsonObject &body);
    void historyReceived(const QJsonObject &body);
    void mutationsApplied(const QJsonObject &body);
    void mediaUploaded(const QJsonObject &body);
    void mediaDownloaded(const QString &remoteBlockId, const QByteArray &data,
                         const QString &contentType);
    void requestFailed(const QString &operation, int status, const QJsonObject &body,
                       const QString &message);

private:
    QNetworkRequest requestFor(const QString &path) const;
    void finishJson(QNetworkReply *reply, const QString &operation,
                    std::function<void(const QJsonObject &)> onSuccess);

    QNetworkAccessManager *m_network = nullptr;
    AccountSession *m_account = nullptr;
};

} // namespace writero
