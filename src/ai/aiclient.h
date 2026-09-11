#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

#include "ai/providerprofile.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace writero {

/// One message in a chat request. An optional image turns the message into a
/// vision request for providers that support it.
struct AiMessage
{
    QString role;
    QString content;
    QByteArray imageData;
    QString imageMime;
};

/// Executes one AI operation against a configured provider.
///
/// The client owns its network request; create one per operation and delete it
/// when the operation settles. Streaming text arrives through `tokenReceived`.
class AiClient : public QObject
{
    Q_OBJECT

public:
    AiClient(ProviderProfile profile, QString apiKey, QObject *parent = nullptr);
    ~AiClient() override;

    void chat(const QString &model, const QVector<AiMessage> &messages, double temperature = 0.7,
              int maxTokens = 4096, bool webSearch = false);
    void generateImage(const QString &model, const QString &prompt,
                       const QByteArray &referenceImage = {}, const QString &referenceMime = {});

    void abort();
    bool isRunning() const;

    bool supportsImageGeneration() const;
    bool supportsVision() const;

signals:
    void tokenReceived(const QString &delta);
    void chatFinished(const QString &text, int promptTokens, int completionTokens);
    void imageFinished(const QByteArray &data, const QString &mimeType);
    void failed(const QString &error);

private:
    enum class Mode { Chat, Image };

    void sendChatRequest(const QVector<AiMessage> &messages, const QString &model, double temperature,
                         int maxTokens, bool webSearch, bool imageModalities);
    void sendImageRequest(const QString &model, const QString &prompt,
                          const QByteArray &referenceImage, const QString &referenceMime);
    void handleChatReadyRead();
    void finishChat();
    void finishChatImage(const QByteArray &body);
    void finishImage(const QByteArray &body);
    void handleStreamLine(const QByteArray &line);
    void fail(const QString &error);
    void reset();
    QString endpoint(const QString &suffix) const;

    ProviderProfile m_profile;
    QString m_apiKey;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QString m_streamedText;
    Mode m_mode = Mode::Chat;
    int m_promptTokens = 0;
    int m_completionTokens = 0;
};

} // namespace writero
