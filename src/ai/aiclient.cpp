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

AiClient::AiClient(ProviderProfile profile, QString apiKey, ModelCapabilities capabilities,
                   QObject *parent)
    : QObject(parent)
    , m_profile(std::move(profile))
    , m_capabilities(std::move(capabilities))
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
    if (m_capabilities.modalitiesKnown)
        return m_capabilities.imageOutput;
    if (!m_capabilities.id.isEmpty())
        return m_capabilities.imageOutput;
    return m_profile.type != ProviderType::Ollama;
}

bool AiClient::supportsVision() const
{
    if (m_capabilities.modalitiesKnown)
        return m_capabilities.imageInput;
    if (!m_capabilities.id.isEmpty())
        return m_capabilities.imageInput;
    return m_profile.type != ProviderType::Ollama || m_capabilities.imageInput;
}

bool AiClient::supportsReferenceImage() const
{
    if (m_profile.type != ProviderType::OpenRouter)
        return false;
    if (m_capabilities.modalitiesKnown || !m_capabilities.id.isEmpty())
        return m_capabilities.imageInput;
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
    for (const AiMessage &message : messages) {
        if (!message.imageData.isEmpty() && !supportsVision()) {
            fail(QStringLiteral("The selected model cannot accept image input."));
            return;
        }
    }

    reset();
    m_mode = Mode::Chat;

    QString effectiveModel = model;
    if (webSearch && m_profile.type == ProviderType::OpenRouter
        && !effectiveModel.endsWith(QStringLiteral(":online"))) {
        effectiveModel += QStringLiteral(":online");
    }

    QJsonArray messageArray;
    for (const AiMessage &message : messages) {
        if (m_profile.type == ProviderType::Ollama) {
            QJsonObject object{{QStringLiteral("role"), message.role},
                               {QStringLiteral("content"), message.content}};
            if (!message.imageData.isEmpty())
                object.insert(QStringLiteral("images"),
                              QJsonArray{QString::fromLatin1(message.imageData.toBase64())});
            messageArray.append(object);
        } else {
            messageArray.append(messageToJson(message));
        }
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), effectiveModel);
    body.insert(QStringLiteral("messages"), messageArray);
    if (m_profile.type == ProviderType::Ollama) {
        QJsonObject options{{QStringLiteral("temperature"), temperature}};
        if (maxTokens > 0)
            options.insert(QStringLiteral("num_predict"), maxTokens);
        body.insert(QStringLiteral("options"), options);
    } else {
        body.insert(QStringLiteral("temperature"), temperature);
        if (maxTokens > 0)
            body.insert(QStringLiteral("max_tokens"), maxTokens);
    }

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
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_reply != reply)
            return;
        const QNetworkReply::NetworkError error = reply->error();
        if (error != QNetworkReply::NoError) {
            const QByteArray body = reply->readAll();
            fail(QStringLiteral("Provider request failed: %1")
                     .arg(body.isEmpty() ? reply->errorString()
                                        : QString::fromUtf8(body).left(300)));
            return;
        }
        handleChatReadyRead();
        if (m_reply != reply)
            return;
        if (!m_buffer.isEmpty()) {
            const QByteArray line = m_buffer;
            m_buffer.clear();
            handleStreamLine(line);
        }
        if (m_reply != reply)
            return;
        if (!m_eventData.isEmpty())
            handleStreamLine({});
        if (m_reply == reply)
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
    if (!referenceImage.isEmpty() && !supportsReferenceImage()) {
        fail(QStringLiteral("The selected model or provider does not support reference images."));
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
    // Keep HTTP error bodies intact for the finished handler's diagnostic.
    if (m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 400)
        return;
    QNetworkReply *reply = m_reply;
    m_buffer += reply->readAll();
    while (m_reply == reply) {
        if (m_skipLineFeed && !m_buffer.isEmpty()) {
            m_skipLineFeed = false;
            if (m_buffer.startsWith('\n'))
                m_buffer.remove(0, 1);
        }
        const int lf = m_buffer.indexOf('\n');
        const int cr = m_buffer.indexOf('\r');
        const int newline = lf < 0 ? cr : cr < 0 ? lf : qMin(lf, cr);
        if (newline < 0)
            break;
        const QByteArray line = m_buffer.left(newline);
        m_skipLineFeed = m_buffer.at(newline) == '\r';
        m_buffer.remove(0, newline + 1);
        handleStreamLine(line);
    }
}

void AiClient::handleStreamLine(const QByteArray &line)
{
    if (m_profile.type == ProviderType::Ollama) {
        if (!line.trimmed().isEmpty())
            handleStreamPayload(line);
        return;
    }
    if (line.isEmpty()) {
        if (!m_eventData.isEmpty()) {
            QByteArray payload = m_eventData;
            m_eventData.clear();
            payload.chop(1); // SSE joins data fields with a newline.
            handleStreamPayload(payload);
        }
    } else if (line.startsWith("data:")) {
        QByteArray data = line.mid(5);
        if (data.startsWith(' '))
            data.remove(0, 1);
        m_eventData += data + '\n';
    }
}

void AiClient::handleStreamPayload(const QByteArray &payload)
{
    if (payload.trimmed() == "[DONE]")
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(QStringLiteral("Provider returned invalid streaming JSON."));
        return;
    }
    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("error"))) {
        const QJsonValue error = object.value(QStringLiteral("error"));
        const QString message = error.isString() ? error.toString()
            : error.toObject().value(QStringLiteral("message")).toString();
        fail(QStringLiteral("Provider request failed: %1")
                 .arg(message.isEmpty() ? QStringLiteral("stream error") : message.left(300)));
        return;
    }
    QString delta;
    if (m_profile.type == ProviderType::Ollama) {
        delta = object.value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("content")).toString();
        if (object.value(QStringLiteral("done")).toBool()) {
            m_promptTokens = object.value(QStringLiteral("prompt_eval_count")).toInt();
            m_completionTokens = object.value(QStringLiteral("eval_count")).toInt();
        }
    } else {
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            delta = choices.first().toObject().value(QStringLiteral("delta")).toObject()
                        .value(QStringLiteral("content")).toString();
        }
        const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
        if (!usage.isEmpty()) {
            m_promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
            m_completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt();
        }
    }
    if (!delta.isEmpty()) {
        m_streamedText += delta;
        emit tokenReceived(delta);
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
        const auto decoded = QByteArray::fromBase64Encoding(url.mid(comma + 1).toUtf8(),
                                                           QByteArray::AbortOnBase64DecodingErrors);
        if (comma < 0 || !header.endsWith(QLatin1String(";base64"))
            || !mime.startsWith(QLatin1String("image/")) || !decoded
            || decoded.decoded.isEmpty()) {
            fail(QStringLiteral("The provider returned invalid image data."));
            return;
        }
        reset();
        emit imageFinished(decoded.decoded, mime);
        return;
    }

    const QUrl imageUrl(url);
    if (!imageUrl.isValid() || imageUrl.host().isEmpty()
        || (imageUrl.scheme() != QLatin1String("https")
            && imageUrl.scheme() != QLatin1String("http"))) {
        fail(QStringLiteral("The provider returned an invalid image URL."));
        return;
    }
    reset();
    QNetworkRequest request{imageUrl};
    m_reply = m_network->get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray data = reply->readAll();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const bool ok = reply->error() == QNetworkReply::NoError && !data.isEmpty();
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
    const auto decoded = QByteArray::fromBase64Encoding(base64.toUtf8(),
                                                       QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty()) {
        fail(QStringLiteral("The provider returned invalid image data."));
        return;
    }
    const QByteArray bytes = decoded.decoded;
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
    m_eventData.clear();
    m_skipLineFeed = false;
    m_streamedText.clear();
    m_promptTokens = 0;
    m_completionTokens = 0;
    abort();
}

} // namespace writero
