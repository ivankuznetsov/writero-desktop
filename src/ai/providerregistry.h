#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVector>

#include "ai/aiclient.h"
#include "ai/providerprofile.h"
#include "security/credentialstore.h"
#include "storage/workspace.h"

namespace writero {

/// Configured AI providers for the current workspace.
///
/// Profiles (name, type, base URL, default model) live in workspace settings;
/// API keys live in the credential store and never touch the database or
/// export bundles.
class ProviderRegistry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Workspace *workspace READ workspace WRITE setWorkspace NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profileList NOTIFY changed)
    Q_PROPERTY(bool keyringAvailable READ keyringAvailable NOTIFY changed)

public:
    explicit ProviderRegistry(QObject *parent = nullptr);

    Workspace *workspace() const { return m_workspace; }
    void setWorkspace(Workspace *workspace);

    QVariantList profileList() const { return m_variantProfiles; }
    bool keyringAvailable() const { return m_credentials.isPersistent(); }

    Q_INVOKABLE QString addProvider(const QString &name, const QString &typeKey,
                                    const QString &baseUrl, const QString &defaultModel,
                                    const QString &apiKey);
    Q_INVOKABLE void updateProvider(const QString &id, const QString &name,
                                    const QString &baseUrl, const QString &defaultModel);
    Q_INVOKABLE void removeProvider(const QString &id);
    Q_INVOKABLE void setCredential(const QString &id, const QString &apiKey);
    Q_INVOKABLE bool hasCredential(const QString &id) const;
    Q_INVOKABLE QString error() const { return m_error; }

    ProviderProfile profile(const QString &id) const;
    AiClient *createClient(const QString &id, QObject *parent = nullptr);

signals:
    void changed();

private:
    static QString credentialKey(const QString &id);
    void load();
    void save();
    void rebuildVariantProfiles();

    QVector<ProviderProfile> m_profiles;
    QVariantList m_variantProfiles;
    Workspace *m_workspace = nullptr;
    CredentialStore m_credentials;
    QString m_error;
};

} // namespace writero
