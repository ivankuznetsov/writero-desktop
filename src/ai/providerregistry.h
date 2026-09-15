#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QVector>

#include "ai/aiclient.h"
#include "ai/modelcatalog.h"
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

    /// Model catalog: `operation` is `text`, `generate`, or `explain`.
    Q_INVOKABLE void refreshModels(const QString &id);
    Q_INVOKABLE bool modelsLoading(const QString &id) const;
    Q_INVOKABLE bool hasModels(const QString &id) const;
    Q_INVOKABLE QVariantList modelsFor(const QString &id, const QString &operation) const;
    Q_INVOKABLE bool modelSupportsReference(const QString &id, const QString &modelId) const;
    Q_INVOKABLE bool modelSupportsImageGeneration(const QString &id,
                                                  const QString &modelId) const;
    Q_INVOKABLE bool modelSupportsImageInput(const QString &id, const QString &modelId) const;

    ProviderProfile profile(const QString &id) const;
    ModelCapabilities capabilitiesFor(const QString &id, const QString &modelId) const;
    AiClient *createClient(const QString &id, const QString &modelId = {},
                           QObject *parent = nullptr);

signals:
    void changed();
    void modelsChanged(const QString &id);
    void modelsFailed(const QString &id, const QString &error);

private:
    static QString credentialKey(const QString &id);
    static QString modelsKey(const QString &id);
    void load();
    void save();
    void loadModelCache();
    void invalidateModels(const QString &id);
    void rebuildVariantProfiles();

    QVector<ProviderProfile> m_profiles;
    QVariantList m_variantProfiles;
    QHash<QString, QVector<ModelCapabilities>> m_models;
    QHash<QString, ModelCatalog *> m_loadingModels;
    Workspace *m_workspace = nullptr;
    CredentialStore m_credentials;
    QString m_error;
};

} // namespace writero
