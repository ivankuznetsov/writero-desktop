#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace writero {

enum class ProviderType {
    OpenRouter,
    OpenAiCompatible,
    Ollama,
};

namespace providertype {
QString toKey(ProviderType type);
ProviderType fromKey(const QString &key, bool *ok = nullptr);
QString displayName(ProviderType type);
QString defaultBaseUrl(ProviderType type);
} // namespace providertype

/// One configured AI provider. The API key is never part of the profile and
/// never touches the workspace database.
struct ProviderProfile
{
    QString id;
    QString name;
    ProviderType type = ProviderType::OpenAiCompatible;
    QString baseUrl;
    QString defaultModel;

    QVariantMap toJson() const;
    static ProviderProfile fromJson(const QVariantMap &json);
};

} // namespace writero
