#include "ai/aiclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace writero {

namespace {

QJsonObject messageToJson(const AiMessage &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("role"), message.role);
    if (message.imageData.isEmpty()) {
        object.insert(QStringLiteral("content"), message.content);
        return object;
    }

    QJsonArray parts;
    if (!message.content.isEmpty()) {
        QJsonObject text;
        text.insert(QStringLiteral("type"), QStringLiteral("text"));
        text.insert(QStringLiteral("text"), message.content);
        parts.append(text);
    }
    QJsonObject image;
    image.insert(QStringLiteral("type"), QStringLiteral("image_url"));
    QJsonObject url;
    url.insert(QStringLiteral("url"),
               QStringLiteral("data:%1;base64,%2")
                   .arg(message.imageMime,
                        QString::fromLatin1(message.imageData.toBase64())));
    image.insert(QStringLiteral("image_url"), url);
    parts.append(image);
    object.insert(QStringLiteral("content"), parts);
    return object;
}

} // namespace

AiClient::AiClient(ProviderProfile profile, QString apiKey, QObject *parent)
    : QObject(parent)
    , m_profile(std::move(profile))
    , m_apiKey(std::move(apiKey))
    , m_network(new QNetworkAccessManager(this))
{
}

AiClient::~AiClient()
{
    abort();
}

bool AiClient::isRunning() const
{
    return m_reply != nullptr;
}

bool AiClient::supportsImageGeneration() const
{
    return m_profile.type != ProviderType::Ollama;
}

bool AiClient::supportsVision() const
{
    return true;
}

QString AiClient::endpoint(const QString &suffix) const
{
    QString base = m_profile.baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base + suffix;
}

void AiClient::chat(const QString &model, const QVector<AiMessage> &messages, double temperature,
                    int maxTokens, bool webSearch)
{
    reset();
    m_mode = Mode::Chat;

    QString effectiveModel = model;
    if (webSearch && m_profile.type == ProviderType::OpenRouter
        && !effectiveModel.endsWith(QStringLiteral(":online"))) {
        effectiveModel += QStringLiteral(":online");
    }

    QJsonArray messageArray;
    for (const AiMessage &message : messages)
        messageArray.append(messageToJson(message));

    QJsonObject body;
    body.insert(QStringLiteral("model"), effectiveModel);
    body.insert(QStringLiteral("messages"), messageArray);
    body.insert(QStringLiteral("temperature"), temperature);
    if (maxTokens > 0)
        body.insert(QStringLiteral("max_tokens"), maxTokens);

    if (m_profile.type == ProviderType::Ollama) {
        body.insert(QStringLiteral("stream"), true);
        QNetworkRequest request(QUrl(endpoint(QStringLiteral("/api/chat"))));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        m_reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    } else {
        body.insert(QStringLiteral("stream"), true);
        body.insert(QStringLiteral("stream_options"),
                    QJsonObject{{QStringLiteral("include_usage"), true}});
        QNetworkRequest request(QUrl(endpoint(QStringLiteral("/chat/completions"))));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!m_apiKey.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
        if (m_profile.type == ProviderType::OpenRouter) {
            request.setRawHeader("HTTP-Referer", "https://writero.app");
            request.setRawHeader("X-Title", "Writero");
        }
        m_reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }

    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::handleChatReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const QNetworkReply::NetworkError error = m_reply->error();
        const QByteArray body = m_reply->readAll();
        if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
            fail(QStringLiteral("Provider request failed: %1")
                     .arg(QString::fromUtf8(body).left(300)));
            return;
        }
        if (!m_buffer.isEmpty()) {
            handleStreamLine(m_buffer);
            m_buffer.clear();
        }
        finishChat();
    });
}

void AiClient::generateImage(const QString &model, const QString &prompt,
                             const QByteArray &referenceImage, const QString &referenceMime)
{
    if (!supportsImageGeneration()) {
        fail(QStringLiteral("This provider does not support image generation."));
        return;
    }

    if (m_profile.type == ProviderType::OpenRouter) {
        reset();
        m_mode = Mode::Image;

        AiMessage message;
        message.role = QStringLiteral("user");
        message.content = referenceImage.isEmpty()
            ? prompt
            : QStringLiteral("Generate a new image based on this reference image. "
                             "Instructions: %1. You must output an image, not text.")
                  .arg(prompt);
        message.imageData = referenceImage;
        message.imageMime = referenceMime;

        QJsonObject body;
        body.insert(QStringLiteral("model"), model);
        body.insert(QStringLiteral("messages"),
                    QJsonArray{messageToJson(message)});
        body.insert(QStringLiteral("modalities"),
                    QJsonArray{QStringLiteral("image"), QStringLiteral("text")});
        body.insert(QStringLiteral("stream"), false);

        QNetworkRequest request(QUrl(endpoint(QStringLiteral("/chat/completions"))));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        if (!m_apiKey.isEmpty())
            request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
        request.setRawHeader("HTTP-Referer", "https://writero.app");
        request.setRawHeader("X-Title", "Writero");
        m_reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

        connect(m_reply, &QNetworkReply::finished, this, [this] {
            const QNetworkReply::NetworkError error = m_reply->error();
            const QByteArray body = m_reply->readAll();
            if (error != QNetworkReply::NoError
                && error != QNetworkReply::OperationCanceledError) {
                fail(QStringLiteral("Image request failed: %1")
                         .arg(QString::fromUtf8(body).left(300)));
                return;
            }
            finishChatImage(body);
        });
        return;
    }

    reset();
    m_mode = Mode::Image;

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("prompt"), prompt);
    body.insert(QStringLiteral("response_format"), QStringLiteral("b64_json"));
    body.insert(QStringLiteral("n"), 1);

    QNetworkRequest request(QUrl(endpoint(QStringLiteral("/images/generations"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    m_reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const QNetworkReply::NetworkError error = m_reply->error();
        const QByteArray body = m_reply->readAll();
        if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
            fail(QStringLiteral("Image request failed: %1")
                     .arg(QString::fromUtf8(body).left(300)));
            return;
        }
        finishImage(body);
    });
}

void AiClient::handleChatReadyRead()
{
    if (!m_reply)
        return;
    m_buffer += m_reply->readAll();

    if (m_profile.type == ProviderType::Ollama) {
        while (true) {
            const int newline = m_buffer.indexOf('\n');
            if (newline < 0)
                break;
            const QByteArray line = m_buffer.left(newline).trimmed();
            m_buffer.remove(0, newline + 1);
            if (line.isEmpty())
                continue;
            const QJsonObject object = QJsonDocument::fromJson(line).object();
            const QString delta = object.value(QStringLiteral("message"))
                                      .toObject()
                                      .value(QStringLiteral("content"))
                                      .toString();
            if (!delta.isEmpty()) {
                m_streamedText += delta;
                emit tokenReceived(delta);
            }
            if (object.value(QStringLiteral("done")).toBool()) {
                m_promptTokens = object.value(QStringLiteral("prompt_eval_count")).toInt();
                m_completionTokens = object.value(QStringLiteral("eval_count")).toInt();
            }
        }
        return;
    }

    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        handleStreamLine(line);
    }
}

void AiClient::handleStreamLine(const QByteArray &line)
{
    if (line.isEmpty() || !line.startsWith("data:"))
        return;
    const QByteArray payload = line.mid(5).trimmed();
    if (payload == "[DONE]")
        return;

    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const QString delta = choices.first()
                                  .toObject()
                                  .value(QStringLiteral("delta"))
                                  .toObject()
                                  .value(QStringLiteral("content"))
                                  .toString();
        if (!delta.isEmpty()) {
            m_streamedText += delta;
            emit tokenReceived(delta);
        }
    }

    const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
    if (!usage.isEmpty()) {
        m_promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
        m_completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt();
    }
}

void AiClient::finishChat()
{
    const QString text = m_streamedText;
    const int promptTokens = m_promptTokens;
    const int completionTokens = m_completionTokens;
    reset();
    emit chatFinished(text, promptTokens, completionTokens);
}

void AiClient::finishChatImage(const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        fail(QStringLiteral("The provider returned no image data."));
        return;
    }
    const QJsonArray images = choices.first()
                                  .toObject()
                                  .value(QStringLiteral("message"))
                                  .toObject()
                                  .value(QStringLiteral("images"))
                                  .toArray();
    if (images.isEmpty()) {
        fail(QStringLiteral("The provider returned no image data."));
        return;
    }

    const QString url = images.first()
                            .toObject()
                            .value(QStringLiteral("image_url"))
                            .toObject()
                            .value(QStringLiteral("url"))
                            .toString();
    if (url.isEmpty()) {
        fail(QStringLiteral("The provider returned no image data."));
        return;
    }

    if (url.startsWith(QLatin1String("data:"))) {
        const int comma = url.indexOf(QLatin1Char(','));
        const QString header = url.left(comma);
        const QString mime = header.section(QLatin1Char(';'), 0, 0).mid(5);
        const QByteArray data = QByteArray::fromBase64(url.mid(comma + 1).toUtf8());
        reset();
        emit imageFinished(data, mime.isEmpty() ? QStringLiteral("image/png") : mime);
        return;
    }

    QNetworkRequest request{QUrl(url)};
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const bool ok = reply->error() == QNetworkReply::NoError && !data.isEmpty();
        reply->deleteLater();
        if (!ok) {
            fail(QStringLiteral("Could not download the generated image."));
            return;
        }
        reset();
        emit imageFinished(data, contentType.isEmpty() ? QStringLiteral("image/png")
                                                       : contentType);
    });
}

void AiClient::finishImage(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    const QJsonArray data = object.value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) {
        fail(QStringLiteral("The provider returned no image data."));
        return;
    }
    const QString base64 = data.first().toObject().value(QStringLiteral("b64_json")).toString();
    if (base64.isEmpty()) {
        fail(QStringLiteral("The provider returned no image data."));
        return;
    }
    const QByteArray bytes = QByteArray::fromBase64(base64.toUtf8());
    reset();
    emit imageFinished(bytes, QStringLiteral("image/png"));
}

void AiClient::abort()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void AiClient::fail(const QString &error)
{
    reset();
    emit failed(error);
}

void AiClient::reset()
{
    m_buffer.clear();
    m_streamedText.clear();
    m_promptTokens = 0;
    m_completionTokens = 0;
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->deleteLater();
    }
}

} // namespace writero
