#include "ai/modelcatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace writero {

namespace {

bool nameHintsImageOutput(const QString &id)
{
    static const QRegularExpression pattern(
        QStringLiteral("image|dall-?e|imagen|flux|stable-diffusion|sd-?xl|sdxl|playground-v"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(id).hasMatch();
}

bool nameHintsImageInput(const QString &id)
{
    static const QRegularExpression pattern(
        QStringLiteral("vision|\\bvl\\b|llava|moondream|pixtral|internvl|gpt-4o|gpt-4\\.|gpt-5|"
                       "gemini|claude|qwen2?\\.?5?-vl"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(id).hasMatch();
}

QString normalizeBase(const QString &baseUrl)
{
    QString base = baseUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

ModelCapabilities fromOpenRouterObject(const QJsonObject &object)
{
    ModelCapabilities model;
    model.id = object.value(QStringLiteral("id")).toString();
    model.name = object.value(QStringLiteral("name")).toString();
    if (model.name.isEmpty())
        model.name = model.id;

    const QJsonObject architecture = object.value(QStringLiteral("architecture")).toObject();
    const QJsonArray inputs = architecture.value(QStringLiteral("input_modalities")).toArray();
    const QJsonArray outputs = architecture.value(QStringLiteral("output_modalities")).toArray();

    if (!inputs.isEmpty() || !outputs.isEmpty()) {
        model.modalitiesKnown = true;
        model.imageInput = false;
        model.textOutput = false;
        for (const QJsonValue &value : inputs) {
            if (value.toString() == QLatin1String("image"))
                model.imageInput = true;
        }
        model.imageOutput = false;
        for (const QJsonValue &value : outputs) {
            if (value.toString() == QLatin1String("image"))
                model.imageOutput = true;
            if (value.toString() == QLatin1String("text"))
                model.textOutput = true;
        }
    }
    return model;
}

ModelCapabilities inferModel(const QString &id, ProviderType type)
{
    ModelCapabilities model;
    model.id = id;
    model.name = id;
    model.modalitiesKnown = false;
    model.textOutput = true;
    if (type == ProviderType::Ollama) {
        model.imageOutput = false;
        model.imageInput = nameHintsImageInput(id);
    } else {
        model.imageOutput = nameHintsImageOutput(id);
        model.imageInput = nameHintsImageInput(id);
    }
    return model;
}

} // namespace

ModelCatalog::ModelCatalog(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void ModelCatalog::fetch(const ProviderProfile &profile, const QString &apiKey)
{
    m_models.clear();
    m_error.clear();

    const QString base = normalizeBase(profile.baseUrl);
    const QString path = profile.type == ProviderType::Ollama ? QStringLiteral("/api/tags")
                                                              : QStringLiteral("/models");
    QNetworkRequest request(QUrl(base + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    if (profile.type == ProviderType::OpenRouter) {
        request.setRawHeader("HTTP-Referer", "https://writero.app");
        request.setRawHeader("X-Title", "Writero");
    }

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, profile] {
        const QNetworkReply::NetworkError error = m_reply->error();
        const QByteArray body = m_reply->readAll();
        m_reply->deleteLater();
        m_reply = nullptr;

        if (error != QNetworkReply::NoError) {
            fail(QStringLiteral("Could not load models: %1")
                     .arg(QString::fromUtf8(body).left(200)));
            return;
        }
        m_models = parse(profile.type, body);
        if (m_models.isEmpty()) {
            fail(QStringLiteral("The provider returned no models."));
            return;
        }
        emit finished(true);
    });
}

void ModelCatalog::fail(const QString &error)
{
    m_error = error;
    emit finished(false);
}

ModelCapabilities ModelCatalog::infer(const QString &modelId, ProviderType type)
{
    return inferModel(modelId, type);
}

QVector<ModelCapabilities> ModelCatalog::parse(ProviderType type, const QByteArray &body)
{
    QVector<ModelCapabilities> models;
    const QJsonDocument json = QJsonDocument::fromJson(body);
    const QJsonObject root = json.object();

    if (type == ProviderType::Ollama) {
        const QJsonArray entries = root.value(QStringLiteral("models")).toArray();
        for (const QJsonValue &value : entries) {
            const QString id = value.toObject().value(QStringLiteral("name")).toString();
            if (!id.isEmpty())
                models.append(inferModel(id, type));
        }
        return models;
    }

    const QJsonArray entries = root.value(QStringLiteral("data")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        if (type == ProviderType::OpenRouter) {
            ModelCapabilities model = fromOpenRouterObject(object);
            if (!model.modalitiesKnown)
                model = inferModel(id, type);
            models.append(model);
        } else {
            const ModelCapabilities inferred = inferModel(id, type);
            // Some compatible endpoints do report OpenRouter-style modalities.
            const QJsonObject architecture = object.value(QStringLiteral("architecture")).toObject();
            models.append(architecture.isEmpty() ? inferred : fromOpenRouterObject(object));
        }
    }
    return models;
}

QString ModelCatalog::toJson(const QVector<ModelCapabilities> &models)
{
    QJsonArray array;
    for (const ModelCapabilities &model : models) {
        array.append(QJsonObject{
            {QStringLiteral("id"), model.id},
            {QStringLiteral("name"), model.name},
            {QStringLiteral("textOutput"), model.textOutput},
            {QStringLiteral("imageOutput"), model.imageOutput},
            {QStringLiteral("imageInput"), model.imageInput},
            {QStringLiteral("known"), model.modalitiesKnown},
        });
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<ModelCapabilities> ModelCatalog::fromJson(const QString &json)
{
    QVector<ModelCapabilities> models;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ModelCapabilities model;
        model.id = object.value(QStringLiteral("id")).toString();
        if (model.id.isEmpty())
            continue;
        model.name = object.value(QStringLiteral("name")).toString();
        if (model.name.isEmpty())
            model.name = model.id;
        model.textOutput = object.value(QStringLiteral("textOutput")).toBool(true);
        model.imageOutput = object.value(QStringLiteral("imageOutput")).toBool(false);
        model.imageInput = object.value(QStringLiteral("imageInput")).toBool(false);
        model.modalitiesKnown = object.value(QStringLiteral("known")).toBool(false);
        models.append(model);
    }
    return models;
}

} // namespace writero
