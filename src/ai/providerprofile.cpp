#include "ai/providerprofile.h"

#include "document/block.h"

namespace writero {

namespace providertype {

QString toKey(ProviderType type)
{
    switch (type) {
    case ProviderType::OpenRouter:
        return QStringLiteral("openrouter");
    case ProviderType::OpenAiCompatible:
        return QStringLiteral("openai-compatible");
    case ProviderType::Ollama:
        return QStringLiteral("ollama");
    }
    return QStringLiteral("openai-compatible");
}

ProviderType fromKey(const QString &key, bool *ok)
{
    if (ok)
        *ok = true;
    if (key == QLatin1String("openrouter"))
        return ProviderType::OpenRouter;
    if (key == QLatin1String("ollama"))
        return ProviderType::Ollama;
    if (key == QLatin1String("openai-compatible"))
        return ProviderType::OpenAiCompatible;
    if (ok)
        *ok = false;
    return ProviderType::OpenAiCompatible;
}

QString displayName(ProviderType type)
{
    switch (type) {
    case ProviderType::OpenRouter:
        return QStringLiteral("OpenRouter");
    case ProviderType::OpenAiCompatible:
        return QStringLiteral("OpenAI-compatible");
    case ProviderType::Ollama:
        return QStringLiteral("Ollama (local)");
    }
    return {};
}

QString defaultBaseUrl(ProviderType type)
{
    switch (type) {
    case ProviderType::OpenRouter:
        return QStringLiteral("https://openrouter.ai/api/v1");
    case ProviderType::OpenAiCompatible:
        return QStringLiteral("https://api.openai.com/v1");
    case ProviderType::Ollama:
        return QStringLiteral("http://localhost:11434");
    }
    return {};
}

} // namespace providertype

QVariantMap ProviderProfile::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("type"), providertype::toKey(type)},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("defaultModel"), defaultModel},
    };
}

ProviderProfile ProviderProfile::fromJson(const QVariantMap &json)
{
    ProviderProfile profile;
    profile.id = json.value(QStringLiteral("id")).toString();
    if (profile.id.isEmpty())
        profile.id = newId();
    profile.name = json.value(QStringLiteral("name")).toString();
    profile.type =
        providertype::fromKey(json.value(QStringLiteral("type")).toString());
    profile.baseUrl = json.value(QStringLiteral("baseUrl")).toString();
    if (profile.baseUrl.isEmpty())
        profile.baseUrl = providertype::defaultBaseUrl(profile.type);
    profile.defaultModel = json.value(QStringLiteral("defaultModel")).toString();
    return profile;
}

} // namespace writero
