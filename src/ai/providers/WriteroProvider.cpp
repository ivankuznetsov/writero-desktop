#include "ai/providers/WriteroProvider.h"

#include "cloud/accountsession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace writero {

const char *WriteroProvider::providerId()
{
    return "writero";
}

bool WriteroProvider::isHostedProviderId(const QString &id)
{
    return id == QLatin1String(providerId());
}

WriteroProvider::WriteroProvider(AccountSession *account, QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_network(new QNetworkAccessManager(this))
{
}

WriteroProvider::~WriteroProvider()
{
    const auto replies = m_replies;
    m_replies.clear();
    for (QNetworkReply *reply : replies) {
        if (reply) {
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
            reply->deleteLater();
        }
    }
}

bool WriteroProvider::isRunning(const QString &operationId) const
{
    return m_replies.contains(operationId);
}

void WriteroProvider::cancel(const QString &operationId)
{
    QNetworkReply *reply = m_replies.take(operationId);
    if (!reply)
        return;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void WriteroProvider::submit(const QString &operationId, const QString &kind,
                             const QString &model, const QString &prompt,
                             const HostedContext &context,
                             const QString &referenceSignedId, const QString &imageSignedId)
{
    if (!m_account || m_account->accessToken().isEmpty()) {
        emit failed(operationId, QStringLiteral("Sign in to use hosted AI."));
        return;
    }
    if (model.isEmpty()) {
        emit failed(operationId, QStringLiteral("Choose a hosted model."));
        return;
    }
    if (m_replies.contains(operationId))
        return;

    QJsonArray surrounding;
    for (const QString &entry : context.surrounding) {
        if (entry.trimmed().isEmpty())
            continue;
        surrounding.append(QJsonObject{{QStringLiteral("content"), entry}});
    }

    QJsonObject body{
        {QStringLiteral("operation_id"), operationId},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("model"), model},
        {QStringLiteral("prompt"), prompt},
        {QStringLiteral("reference_signed_id"), referenceSignedId},
        {QStringLiteral("image_signed_id"), imageSignedId},
        {QStringLiteral("context"),
         QJsonObject{
             {QStringLiteral("content"), context.content},
             {QStringLiteral("block_type"), context.blockType},
             {QStringLiteral("article_title"), context.articleTitle},
             {QStringLiteral("surrounding"), surrounding},
         }},
    };

    QString base = m_account->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);

    QNetworkRequest request(QUrl(base + QStringLiteral("/api/desktop/v1/ai_jobs")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + m_account->accessToken().toUtf8());

    QNetworkReply *reply = m_network->post(request,
                                           QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_replies.insert(operationId, reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, operationId] {
        if (m_replies.value(operationId) != reply)
            return;
        m_replies.remove(operationId);
        const QByteArray raw = reply->readAll();
        const QJsonObject json = QJsonDocument::fromJson(raw).object();
        const QNetworkReply::NetworkError networkError = reply->error();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError
            && networkError != QNetworkReply::OperationCanceledError) {
            const QString message = json.value(QStringLiteral("message")).toString();
            emit failed(operationId,
                        message.isEmpty()
                            ? QStringLiteral("Hosted AI request failed. Your credits were not spent "
                                             "unless the provider had already started; try again later.")
                            : message);
            return;
        }

        const QString status = json.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("completed")) {
            if (json.value(QStringLiteral("media_available")).toBool()) {
                fetchResultMedia(operationId, json.value(QStringLiteral("id")).toVariant().toLongLong());
                return;
            }
            emit finished(operationId, json.value(QStringLiteral("content")).toString(),
                          json.value(QStringLiteral("usage")).toObject());
        } else if (status == QLatin1String("ambiguous")) {
            emit ambiguous(operationId,
                           json.value(QStringLiteral("error"))
                               .toString(QStringLiteral("The provider outcome is unknown.")));
        } else {
            emit failed(operationId,
                        json.value(QStringLiteral("error"))
                            .toString(QStringLiteral("The hosted operation failed.")));
        }
    });
}

void WriteroProvider::uploadMedia(const QString &requestId, const QString &path)
{
    if (!m_account || m_account->accessToken().isEmpty()) {
        emit mediaUploadFailed(requestId, QStringLiteral("Sign in to use hosted AI."));
        return;
    }

    if (m_replies.contains(requestId))
        return;
    QFile *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        emit mediaUploadFailed(requestId, QStringLiteral("Cannot read the media file."));
        return;
    }

    QHttpMultiPart *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                       .arg(QFileInfo(path).fileName()));
    part.setHeader(QNetworkRequest::ContentTypeHeader,
                   QMimeDatabase().mimeTypeForFile(path).name());
    part.setBodyDevice(file);
    file->setParent(multipart);
    multipart->append(part);

    QString base = m_account->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);

    QNetworkRequest request(QUrl(base + QStringLiteral("/api/desktop/v1/media")));
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + m_account->accessToken().toUtf8());
    QNetworkReply *reply = m_network->post(request, multipart);
    multipart->setParent(reply);
    m_replies.insert(requestId, reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId] {
        if (m_replies.value(requestId) != reply)
            return;
        m_replies.remove(requestId);
        const QByteArray raw = reply->readAll();
        const QJsonObject json = QJsonDocument::fromJson(raw).object();
        const bool ok = reply->error() == QNetworkReply::NoError
                        && !json.value(QStringLiteral("signed_id")).toString().isEmpty();
        reply->deleteLater();
        if (!ok) {
            emit mediaUploadFailed(
                requestId, json.value(QStringLiteral("error"))
                               .toString(QStringLiteral("Media upload failed.")));
            return;
        }
        emit mediaUploaded(requestId, json.value(QStringLiteral("signed_id")).toString());
    });
}

void WriteroProvider::fetchResultMedia(const QString &operationId, qint64 jobId)
{
    if (!m_account || m_account->accessToken().isEmpty()) {
        emit failed(operationId, QStringLiteral("Sign in to use hosted AI."));
        return;
    }
    if (m_replies.contains(operationId))
        return;
    QString base = m_account->baseUrl();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);

    QNetworkRequest request(
        QUrl(QStringLiteral("%1/api/desktop/v1/ai_jobs/%2/media").arg(base).arg(jobId)));
    if (m_account && !m_account->accessToken().isEmpty()) {
        request.setRawHeader("Authorization",
                             QByteArrayLiteral("Bearer ") + m_account->accessToken().toUtf8());
    }
    QNetworkReply *reply = m_network->get(request);
    m_replies.insert(operationId, reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, operationId] {
        if (m_replies.value(operationId) != reply)
            return;
        m_replies.remove(operationId);
        const QByteArray data = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const bool ok = reply->error() == QNetworkReply::NoError && !data.isEmpty()
                        && contentType.startsWith(QLatin1String("image/"), Qt::CaseInsensitive);
        reply->deleteLater();
        if (!ok) {
            emit failed(operationId, QStringLiteral("Could not download the generated image."));
            return;
        }
        emit imageFinished(operationId, data,
                           contentType.isEmpty() ? QStringLiteral("image/png") : contentType);
    });
}

} // namespace writero
