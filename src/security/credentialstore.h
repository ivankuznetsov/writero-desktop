#pragma once

#include <QString>

namespace writero {

/// Stores provider secrets in the Secret Service when available.
///
/// If the service is unavailable or locked, or the environment disables it
/// (tests, headless sessions), credentials live in memory for the session
/// only. Plaintext fallback files are never written.
class CredentialStore
{
public:
    CredentialStore();
    ~CredentialStore();

    /// True when secrets can be persisted through the Secret Service.
    bool isPersistent() const;

    bool store(const QString &key, const QString &secret, QString *error = nullptr);
    QString load(const QString &key, QString *error = nullptr) const;
    bool remove(const QString &key, QString *error = nullptr);

private:
    QString lookupSession(const QString &key) const;
    void storeSession(const QString &key, const QString &secret);

    bool m_useKeyring = false;
};

} // namespace writero
