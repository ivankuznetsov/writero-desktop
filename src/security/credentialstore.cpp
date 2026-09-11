#include "security/credentialstore.h"

// GLib headers use `signals` as a struct member, which collides with Qt's
// `signals` macro. Drop the macro before including them; this translation
// unit does not use Qt signal declarations past this point.
#ifdef WRITERO_HAVE_LIBSECRET
#undef signals
#include <libsecret/secret.h>
#endif

#include <QHash>
#include <QProcessEnvironment>

namespace writero {

namespace {

#ifdef WRITERO_HAVE_LIBSECRET
const SecretSchema *schema()
{
    static const SecretSchema schema = {
        "app.writero.Writero.Credentials",
        SECRET_SCHEMA_NONE,
        {
            {"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
    };
    return &schema;
}
#endif

QHash<QString, QString> &sessionSecrets()
{
    static QHash<QString, QString> secrets;
    return secrets;
}

} // namespace

CredentialStore::CredentialStore()
{
#ifdef WRITERO_HAVE_LIBSECRET
    const QString disabled = QProcessEnvironment::systemEnvironment()
                                 .value(QStringLiteral("WRITERO_DISABLE_KEYRING"));
    if (disabled == QLatin1String("1")) {
        m_useKeyring = false;
        return;
    }
    GError *error = nullptr;
    SecretService *service = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &error);
    m_useKeyring = service != nullptr;
    if (service)
        g_object_unref(service);
    if (error)
        g_error_free(error);
#endif
}

CredentialStore::~CredentialStore() = default;

bool CredentialStore::isPersistent() const
{
    return m_useKeyring;
}

bool CredentialStore::store(const QString &key, const QString &secret, QString *error)
{
    if (!m_useKeyring) {
        storeSession(key, secret);
        return true;
    }

#ifdef WRITERO_HAVE_LIBSECRET
    GError *gerror = nullptr;
    const gboolean ok = secret_password_store_sync(
        schema(), SECRET_COLLECTION_DEFAULT, "Writero provider credential", secret.toUtf8().constData(),
        nullptr, &gerror, "key", key.toUtf8().constData(), nullptr);
    if (!ok) {
        if (error)
            *error = QString::fromUtf8(gerror ? gerror->message : "Secret Service store failed");
        if (gerror)
            g_error_free(gerror);
        storeSession(key, secret);
        m_useKeyring = false;
        return true; // Session fallback keeps the app usable.
    }
    return true;
#else
    Q_UNUSED(error);
    return true;
#endif
}

QString CredentialStore::load(const QString &key, QString *error) const
{
    const QString session = lookupSession(key);
    if (!m_useKeyring)
        return session;

#ifdef WRITERO_HAVE_LIBSECRET
    GError *gerror = nullptr;
    gchar *secret = secret_password_lookup_sync(schema(), nullptr, &gerror, "key",
                                                key.toUtf8().constData(), nullptr);
    if (gerror) {
        if (error)
            *error = QString::fromUtf8(gerror->message);
        g_error_free(gerror);
        return session;
    }
    if (!secret)
        return session;
    const QString result = QString::fromUtf8(secret);
    secret_password_free(secret);
    return result;
#else
    Q_UNUSED(error);
    return session;
#endif
}

bool CredentialStore::remove(const QString &key, QString *error)
{
    sessionSecrets().remove(key);
    if (!m_useKeyring)
        return true;

#ifdef WRITERO_HAVE_LIBSECRET
    GError *gerror = nullptr;
    secret_password_clear_sync(schema(), nullptr, &gerror, "key", key.toUtf8().constData(),
                               nullptr);
    if (gerror) {
        if (error)
            *error = QString::fromUtf8(gerror->message);
        g_error_free(gerror);
        return false;
    }
#else
    Q_UNUSED(error);
#endif
    return true;
}

QString CredentialStore::lookupSession(const QString &key) const
{
    return sessionSecrets().value(key);
}

void CredentialStore::storeSession(const QString &key, const QString &secret)
{
    sessionSecrets().insert(key, secret);
}

} // namespace writero
