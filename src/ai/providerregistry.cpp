#include "ai/providerregistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "storage/workspace.h"

namespace writero {

namespace {
constexpr auto SettingsKey = "ai.providers";
constexpr auto CredentialPrefix = "provider.";
} // namespace

ProviderRegistry::ProviderRegistry(QObject *parent)
    : QObject(parent)
{
}

void ProviderRegistry::setWorkspace(Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    m_workspace = workspace;
    load();
}

void ProviderRegistry::load()
{
    m_profiles.clear();
    if (m_workspace != nullptr && m_workspace->isReady()) {
        const QString json = m_workspace->setting(QString::fromLatin1(SettingsKey));
        const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
        for (const QJsonValue &value : array)
            m_profiles.append(ProviderProfile::fromJson(value.toObject().toVariantMap()));
    }
    rebuildVariantProfiles();
    emit changed();
}

void ProviderRegistry::save()
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return;

    QJsonArray array;
    for (const ProviderProfile &profile : m_profiles)
        array.append(QJsonObject::fromVariantMap(profile.toJson()));
    m_workspace->setSetting(QString::fromLatin1(SettingsKey),
                            QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    rebuildVariantProfiles();
    emit changed();
}

void ProviderRegistry::rebuildVariantProfiles()
{
    m_variantProfiles.clear();
    for (const ProviderProfile &profile : m_profiles) {
        QVariantMap map = profile.toJson();
        map.insert(QStringLiteral("typeLabel"), providertype::displayName(profile.type));
        map.insert(QStringLiteral("hasCredential"), hasCredential(profile.id));
        m_variantProfiles.append(map);
    }
}

QString ProviderRegistry::addProvider(const QString &name, const QString &typeKey,
                                      const QString &baseUrl, const QString &defaultModel,
                                      const QString &apiKey)
{
    bool ok = false;
    const ProviderType type = providertype::fromKey(typeKey, &ok);
    if (!ok) {
        m_error = QStringLiteral("Unknown provider type: %1").arg(typeKey);
        return {};
    }

    ProviderProfile profile;
    profile.id = newId();
    profile.name = name.isEmpty() ? providertype::displayName(type) : name;
    profile.type = type;
    profile.baseUrl = baseUrl.isEmpty() ? providertype::defaultBaseUrl(type) : baseUrl;
    profile.defaultModel = defaultModel;

    m_profiles.append(profile);

    QString credentialError;
    if (!apiKey.isEmpty())
        m_credentials.store(credentialKey(profile.id), apiKey, &credentialError);
    m_error = credentialError;

    save();
    return profile.id;
}

void ProviderRegistry::updateProvider(const QString &id, const QString &name,
                                      const QString &baseUrl, const QString &defaultModel)
{
    for (ProviderProfile &profile : m_profiles) {
        if (profile.id != id)
            continue;
        if (!name.isEmpty())
            profile.name = name;
        if (!baseUrl.isEmpty())
            profile.baseUrl = baseUrl;
        if (!defaultModel.isEmpty())
            profile.defaultModel = defaultModel;
        save();
        return;
    }
}

void ProviderRegistry::removeProvider(const QString &id)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id != id)
            continue;
        m_profiles.removeAt(i);
        m_credentials.remove(credentialKey(id));
        save();
        return;
    }
}

void ProviderRegistry::setCredential(const QString &id, const QString &apiKey)
{
    QString error;
    m_credentials.store(credentialKey(id), apiKey, &error);
    m_error = error;
    rebuildVariantProfiles();
    emit changed();
}

bool ProviderRegistry::hasCredential(const QString &id) const
{
    return !m_credentials.load(credentialKey(id)).isEmpty();
}

ProviderProfile ProviderRegistry::profile(const QString &id) const
{
    for (const ProviderProfile &profile : m_profiles) {
        if (profile.id == id)
            return profile;
    }
    return {};
}

AiClient *ProviderRegistry::createClient(const QString &id, QObject *parent)
{
    const ProviderProfile selected = profile(id);
    if (selected.id.isEmpty())
        return nullptr;
    return new AiClient(selected, m_credentials.load(credentialKey(id)), parent);
}

QString ProviderRegistry::credentialKey(const QString &id)
{
    return QString::fromLatin1(CredentialPrefix) + id;
}

} // namespace writero
