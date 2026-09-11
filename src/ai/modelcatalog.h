#pragma once

#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QVector>

#include "ai/providerprofile.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace writero {

/// What one model can accept and produce.
///
/// OpenRouter reports this through `architecture.input_modalities` and
/// `architecture.output_modalities`; endpoints without modality metadata fall
/// back to name-based inference and are marked `modalitiesKnown = false` so
/// the interface can say "capability unknown" instead of claiming support.
struct ModelCapabilities
{
    QString id;
    QString name;
    bool textOutput = true;
    bool imageOutput = false;
    bool imageInput = false;
    bool modalitiesKnown = false;

    bool producesImage() const { return imageOutput; }
    bool acceptsImage() const { return imageInput; }
};

/// Fetches and parses a provider's model catalog.
class ModelCatalog : public QObject
{
    Q_OBJECT

public:
    explicit ModelCatalog(QObject *parent = nullptr);

    void fetch(const ProviderProfile &profile, const QString &apiKey);
    const QVector<ModelCapabilities> &models() const { return m_models; }
    QString error() const { return m_error; }

    static QString toJson(const QVector<ModelCapabilities> &models);
    static QVector<ModelCapabilities> fromJson(const QString &json);
    static QVector<ModelCapabilities> parse(ProviderType type, const QByteArray &body);

    /// Name-based fallback for models the catalog does not describe.
    static ModelCapabilities infer(const QString &modelId, ProviderType type);

signals:
    void finished(bool ok);

private:
    void fail(const QString &error);

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QVector<ModelCapabilities> m_models;
    QString m_error;
};

} // namespace writero
