#include "cloud/cloudclient.h"

#include "cloud/accountsession.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace writero {

CloudClient::CloudClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void CloudClient::setAccount(AccountSession *account)
{
    cancelRequests();
    m_account = account;
}

void CloudClient::cancelRequests()
{
    ++m_requestGeneration;
    for (QNetworkReply *reply : m_network->findChildren<QNetworkReply *>()) {
        if (reply->isRunning())
            reply->abort();
    }
}

QString CloudClient::baseUrl() const
{
    return m_account ? m_account->baseUrl() : QString();
}

bool CloudClient::hasToken() const
{
    return m_account && !m_account->accessToken().isEmpty();
}

QNetworkRequest CloudClient::requestFor(const QString &path) const
{
    QNetworkRequest request(QUrl(baseUrl() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (hasToken()) {
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + m_account->accessToken().toUtf8());
    }
    return request;
}

void CloudClient::finishJson(QNetworkReply *reply, const QString &operation,
                             std::function<void(const QJsonObject &)> onSuccess)
{
    const quint64 generation = m_requestGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, operation, onSuccess, generation] {
        if (generation != m_requestGeneration) {
            reply->deleteLater();
            return;
        }
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();

        const QJsonObject json = QJsonDocument::fromJson(body).object();
        if (ok) {
            onSuccess(json);
            return;
        }
        emit requestFailed(operation, status, json,
                           json.value(QStringLiteral("error")).toString());
    });
}

void CloudClient::createDocument(const QString &title)
{
    const QJsonObject body{{QStringLiteral("title"), title}};
    QNetworkReply *reply = m_network->post(
        requestFor(QStringLiteral("/api/desktop/v1/documents")),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    finishJson(reply, QStringLiteral("create_document"),
               [this](const QJsonObject &json) { emit documentCreated(json); });
}

void CloudClient::fetchSnapshot(const QString &documentId)
{
    const QNetworkRequest request = requestFor(
        QStringLiteral("/api/desktop/v1/documents/%1/snapshot").arg(documentId));
    finishJson(m_network->get(request), QStringLiteral("snapshot"),
               [this](const QJsonObject &json) { emit snapshotReceived(json); });
}

void CloudClient::fetchSnapshotPage(const QString &documentId, const QString &leaseId, int page)
{
    const QNetworkRequest request = requestFor(
        QStringLiteral("/api/desktop/v1/documents/%1/snapshot?lease_id=%2&page=%3")
            .arg(documentId, leaseId, QString::number(page)));
    finishJson(m_network->get(request), QStringLiteral("snapshot"),
               [this](const QJsonObject &json) { emit snapshotReceived(json); });
}

void CloudClient::fetchChanges(const QString &documentId, qint64 cursor, qint64 generation,
                               int limit)
{
    const QNetworkRequest request = requestFor(
        QStringLiteral("/api/desktop/v1/documents/%1/changes?cursor=%2&generation=%3&limit=%4")
            .arg(documentId, QString::number(cursor), QString::number(generation),
                 QString::number(limit)));
    finishJson(m_network->get(request), QStringLiteral("changes"),
               [this](const QJsonObject &json) { emit changesReceived(json); });
}

void CloudClient::fetchHistory(const QString &documentId, const QString &remoteBlockId)
{
    const QNetworkRequest request = requestFor(
        QStringLiteral("/api/desktop/v1/documents/%1/history?block_id=%2")
            .arg(documentId, remoteBlockId));
    finishJson(m_network->get(request), QStringLiteral("history"),
               [this](const QJsonObject &json) { emit historyReceived(json); });
}

void CloudClient::fetchShareLink(const QString &documentId)
{
    const QNetworkRequest request =
        requestFor(QStringLiteral("/api/desktop/v1/documents/%1/share_link").arg(documentId));
    finishJson(m_network->get(request), QStringLiteral("share_link"),
               [this](const QJsonObject &json) {
                   emit shareLinkReceived(
                       json.value(QStringLiteral("share_url")).toString());
               });
}

void CloudClient::postMutations(const QString &documentId, const QJsonArray &mutations)
{
    const QJsonObject body{{QStringLiteral("mutations"), mutations}};
    QNetworkReply *reply = m_network->post(
        requestFor(QStringLiteral("/api/desktop/v1/documents/%1/mutations").arg(documentId)),
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    finishJson(reply, QStringLiteral("mutations"),
               [this](const QJsonObject &json) { emit mutationsApplied(json); });
}

void CloudClient::uploadMedia(const QString &path)
{
    QFile *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        emit requestFailed(QStringLiteral("upload_media"), 0, {},
                           QStringLiteral("Cannot read media file"));
        return;
    }

    QHttpMultiPart *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                       .arg(QFileInfo(path).fileName()));
    part.setHeader(QNetworkRequest::ContentTypeHeader,
                   QFileInfo(path).suffix() == QLatin1String("png")
                       ? QStringLiteral("image/png")
                       : QStringLiteral("application/octet-stream"));
    part.setBodyDevice(file);
    file->setParent(multipart);
    multipart->append(part);

    QNetworkRequest request(QUrl(baseUrl() + QStringLiteral("/api/desktop/v1/media")));
    if (hasToken())
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + m_account->accessToken().toUtf8());
    QNetworkReply *reply = m_network->post(request, multipart);
    multipart->setParent(reply);
    finishJson(reply, QStringLiteral("upload_media"),
               [this](const QJsonObject &json) { emit mediaUploaded(json); });
}

void CloudClient::downloadMedia(const QString &documentId, const QString &remoteBlockId)
{
    const QNetworkRequest request = requestFor(
        QStringLiteral("/api/desktop/v1/documents/%1/blocks/%2/media")
            .arg(documentId, remoteBlockId));
    QNetworkReply *reply = m_network->get(request);
    const quint64 generation = m_requestGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, remoteBlockId, generation] {
        if (generation != m_requestGeneration) {
            reply->deleteLater();
            return;
        }
        const QByteArray data = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok) {
            emit requestFailed(QStringLiteral("download_media"), status, {}, {});
            return;
        }
        emit mediaDownloaded(remoteBlockId, data, contentType);
    });
}

} // namespace writero
