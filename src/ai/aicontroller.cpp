#include "ai/aicontroller.h"

#include <algorithm>
#include <utility>

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "markdown/markdown.h"

namespace writero {

namespace {

QString stripCodeFences(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1String("```"))) {
        const int firstNewline = text.indexOf(QLatin1Char('\n'));
        const int lastFence = text.lastIndexOf(QLatin1String("```"));
        if (firstNewline > 0 && lastFence > firstNewline)
            text = text.mid(firstNewline + 1, lastFence - firstNewline - 1).trimmed();
    }
    return text;
}

} // namespace

AiController::AiController(QObject *parent)
    : QObject(parent)
{
}

void AiController::setWorkspace(Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    m_workspace = workspace;
    m_resultRecords.clear();
    m_results.clear();
    emit resultsChanged();
    emit changed();
}

void AiController::setProviders(ProviderRegistry *providers)
{
    if (m_providers == providers)
        return;
    m_providers = providers;
    emit changed();
}

void AiController::setDocument(DocumentController *document)
{
    if (m_document == document)
        return;
    m_document = document;
    if (m_document) {
        connect(m_document, &DocumentController::loaded, this, [this] {
            m_currentBlock = -1;
            refreshResults();
        });
        connect(m_document, &DocumentController::saved, this, &AiController::refreshResults);
    }
    emit changed();
}

void AiController::setCurrentBlock(int index)
{
    if (m_currentBlock == index)
        return;
    m_currentBlock = index;
    emit currentBlockChanged();
    refreshResults();
}

void AiController::refreshResults()
{
    m_resultRecords.clear();
    m_results.clear();
    if (m_workspace != nullptr && m_workspace->isReady() && m_document != nullptr) {
        const QString blockId = m_currentBlock >= 0
                                    && m_currentBlock < m_document->blocks()->rowCount()
            ? m_document->blocks()->get(m_currentBlock).value(QStringLiteral("blockId")).toString()
            : QString();
        m_resultRecords = m_workspace->store()->aiResults(m_document->documentId(), blockId, 50);
    }

    for (const WorkspaceStore::AiResultRecord &record : std::as_const(m_resultRecords)) {
        if (record.status == QLatin1String("dismissed"))
            continue;
        // A completed result is stale when the block was edited after the
        // operation started; it then requires explicit review.
        bool stale = false;
        if (!record.blockId.isEmpty() && m_document) {
            const Block *block = blockById(record.blockId);
            stale = block && block->revision > record.baseRevision;
        }
        m_results.append(QVariantMap{
            {QStringLiteral("id"), record.id},
            {QStringLiteral("blockId"), record.blockId},
            {QStringLiteral("kind"), record.kind},
            {QStringLiteral("model"), record.model},
            {QStringLiteral("prompt"), record.prompt},
            {QStringLiteral("status"), record.status},
            {QStringLiteral("content"), record.content},
            {QStringLiteral("error"), record.error},
            {QStringLiteral("stale"), stale},
            {QStringLiteral("createdAt"), record.createdAt},
        });
    }
    emit resultsChanged();
}

const Block *AiController::blockById(const QString &blockId) const
{
    if (m_document == nullptr)
        return nullptr;
    const Document &document = m_document->session().document();
    return document.blockById(blockId);
}

WorkspaceStore::AiResultRecord AiController::createResult(
    const QString &blockId, const QString &kind, const QString &providerId, const QString &model,
    const QString &prompt, const QString &batchId)
{
    WorkspaceStore::AiResultRecord record;
    record.id = newId();
    record.documentId = m_document ? m_document->documentId() : QString();
    record.blockId = blockId;
    record.kind = kind;
    record.providerId = providerId;
    record.model = model;
    record.prompt = prompt;
    record.status = QStringLiteral("pending");
    record.batchId = batchId;
    record.createdAt = QDateTime::currentDateTimeUtc();
    if (!blockId.isEmpty()) {
        const Block *block = blockById(blockId);
        record.baseRevision = block ? block->revision : 0;
    }
    if (!m_workspace->store()->saveAiResult(record)) {
        qWarning().noquote() << "writero ai: failed to save result:"
                             << m_workspace->store()->lastError();
    }
    return record;
}

AiClient *AiController::makeClient(const QString &providerId, const QString &model)
{
    if (m_providers == nullptr)
        return nullptr;
    Q_UNUSED(model);
    return m_providers->createClient(providerId, this);
}

void AiController::executeChat(const WorkspaceStore::AiResultRecord &record,
                               textactions::Operation operation, const QString &instruction,
                               bool webSearch)
{
    const Document &document = m_document->session().document();
    const Block *block = blockById(record.blockId);
    if (!block) {
        settleResult(record.id, QStringLiteral("failed"), {}, QStringLiteral("Block not found"));
        return;
    }

    QVector<AiMessage> messages =
        textactions::buildMessages(operation, document, *block, instruction);

    if (operation == textactions::Operation::ImageExplanation && block->mediaId > 0) {
        QFile file(m_workspace->mediaPath(block->mediaId));
        if (file.open(QIODevice::ReadOnly)) {
            messages.last().imageData = file.readAll();
            messages.last().imageMime = m_workspace->store()->mediaRecord(block->mediaId).mimeType;
        }
    }

    AiClient *client = makeClient(record.providerId, record.model);
    if (!client) {
        settleResult(record.id, QStringLiteral("failed"), {},
                     QStringLiteral("Provider is not configured"));
        return;
    }

    const QString resultId = record.id;
    setBusy(true);
    m_streamingText.clear();
    emit streamingChanged();

    connect(client, &AiClient::tokenReceived, this, &AiController::appendStreaming);
    connect(client, &AiClient::chatFinished, this,
            [this, resultId, client](const QString &text, int, int) {
                settleResult(resultId, QStringLiteral("completed"), text.trimmed(), {});
                setBusy(false);
                client->deleteLater();
            });
    connect(client, &AiClient::failed, this, [this, resultId, client](const QString &error) {
        settleResult(resultId, QStringLiteral("failed"), {}, error);
        setBusy(false);
        client->deleteLater();
    });

    const int maxTokens = operation == textactions::Operation::Research ? 16000 : 4096;
    client->chat(record.model, messages, 0.7, maxTokens, webSearch);
}

QString AiController::runRewrite(int index, const QString &providerId, const QString &models,
                                 const QString &prompt)
{
    if (!m_document || !m_workspace || m_currentBlock != index)
        setCurrentBlock(index);
    if (index < 0 || index >= m_document->blocks()->rowCount())
        return {};

    const QString blockId =
        m_document->blocks()->get(index).value(QStringLiteral("blockId")).toString();

    QStringList modelList;
    for (const QString &model : models.split(QLatin1Char(','), Qt::SkipEmptyParts))
        modelList << model.trimmed();
    if (modelList.isEmpty())
        return {};

    QString firstId;
    for (const QString &model : modelList) {
        const WorkspaceStore::AiResultRecord record = createResult(
            blockId, QStringLiteral("rewrite"), providerId, model, prompt);
        if (firstId.isEmpty())
            firstId = record.id;
        executeChat(record, textactions::Operation::Rewrite, prompt, false);
    }
    refreshResults();
    return firstId;
}

QString AiController::runResearch(int index, const QString &providerId, const QString &model,
                                  const QString &note)
{
    if (!m_document || !m_workspace)
        return {};
    setCurrentBlock(index);
    if (index < 0 || index >= m_document->blocks()->rowCount())
        return {};

    const QString blockId =
        m_document->blocks()->get(index).value(QStringLiteral("blockId")).toString();
    const WorkspaceStore::AiResultRecord record = createResult(
        blockId, QStringLiteral("research"), providerId, model, note);
    executeChat(record, textactions::Operation::Research, note, true);
    refreshResults();
    return record.id;
}

QString AiController::runImageGeneration(int index, const QString &providerId, const QString &model,
                                         const QString &prompt, bool useCurrentAsReference)
{
    if (!m_document || !m_workspace)
        return {};
    setCurrentBlock(index);
    if (index < 0 || index >= m_document->blocks()->rowCount())
        return {};

    const Block *block = blockById(
        m_document->blocks()->get(index).value(QStringLiteral("blockId")).toString());
    if (!block)
        return {};

    const WorkspaceStore::AiResultRecord record = createResult(
        block->id, QStringLiteral("image_generation"), providerId, model, prompt);
    AiClient *client = makeClient(providerId, model);
    if (!client) {
        settleResult(record.id, QStringLiteral("failed"), {},
                     QStringLiteral("Provider is not configured"));
        return record.id;
    }

    const QString resultId = record.id;
    setBusy(true);
    connect(client, &AiClient::imageFinished, this,
            [this, resultId, client](const QByteArray &data, const QString &mimeType) {
                const QString sha = m_workspace->media()->importData(
                    data, QStringLiteral("generated.png"), mimeType);
                const qint64 mediaId = m_workspace->store()->ensureMedia(
                    sha, QStringLiteral("generated.png"), mimeType, data.size());
                settleResult(resultId, QStringLiteral("completed"), QString::number(mediaId), {});
                setBusy(false);
                client->deleteLater();
            });
    connect(client, &AiClient::failed, this, [this, resultId, client](const QString &error) {
        settleResult(resultId, QStringLiteral("failed"), {}, error);
        setBusy(false);
        client->deleteLater();
    });

    QByteArray reference;
    QString referenceMime;
    if (useCurrentAsReference && block->mediaId > 0) {
        QFile file(m_workspace->mediaPath(block->mediaId));
        if (file.open(QIODevice::ReadOnly)) {
            reference = file.readAll();
            referenceMime = m_workspace->store()->mediaRecord(block->mediaId).mimeType;
        }
    }
    client->generateImage(model, prompt, reference, referenceMime);
    client->setParent(this);
    refreshResults();
    return record.id;
}

QString AiController::runImageExplanation(int index, const QString &providerId,
                                          const QString &model, const QString &prompt)
{
    if (!m_document || !m_workspace)
        return {};
    setCurrentBlock(index);
    if (index < 0 || index >= m_document->blocks()->rowCount())
        return {};

    const QString blockId =
        m_document->blocks()->get(index).value(QStringLiteral("blockId")).toString();
    const WorkspaceStore::AiResultRecord record = createResult(
        blockId, QStringLiteral("image_explanation"), providerId, model, prompt);
    executeChat(record, textactions::Operation::ImageExplanation, prompt, false);
    refreshResults();
    return record.id;
}

bool AiController::applyResult(const QString &resultId, bool insertBelow)
{
    if (!m_document)
        return false;

    const auto it = std::find_if(m_resultRecords.cbegin(), m_resultRecords.cend(),
                                 [&resultId](const WorkspaceStore::AiResultRecord &record) {
                                     return record.id == resultId;
                                 });
    if (it == m_resultRecords.cend())
        return false;
    const WorkspaceStore::AiResultRecord record = *it;
    if (record.status != QLatin1String("completed"))
        return false;

    const Block *block = blockById(record.blockId);
    if (!block && !insertBelow)
        return false;
    const int index = block ? m_document->session().document().indexOf(block->id) : -1;

    if (record.kind == QLatin1String("image_generation")) {
        const qint64 mediaId = record.content.toLongLong();
        if (mediaId <= 0)
            return false;
        if (insertBelow) {
            Block media = Block::create(BlockType::Media);
            media.mediaId = mediaId;
            // Insert through the session so undo and autosave stay consistent.
            m_document->session().insertBlock(index + 1, media, QStringLiteral("ai"));
        } else {
            Block updated = *block;
            if (updated.mediaId > 0)
                m_workspace->store()->addMediaVersion(updated.id, updated.mediaId);
            updated.mediaId = mediaId;
            updated.type = BlockType::Media;
            m_document->session().updateBlock(index, updated, QStringLiteral("ai"));
        }
        refreshResults();
        return true;
    }

    if (insertBelow) {
        Block created = *block;
        created.content = record.content;
        created.revision = 1;
        m_document->session().insertBlock(index + 1, created, QStringLiteral("ai"));
    } else {
        Block updated = *block;
        updated.content = record.content;
        m_document->session().updateBlock(index, updated, QStringLiteral("ai"));
    }
    refreshResults();
    return true;
}

void AiController::dismissResult(const QString &resultId)
{
    if (m_workspace == nullptr)
        return;
    m_workspace->store()->updateAiResult(resultId, QStringLiteral("dismissed"));
    refreshResults();
}

QStringList AiController::suggestions() const
{
    return textactions::promptSuggestions();
}

void AiController::runPolish(const QString &providerId, const QString &model)
{
    runBulk(providerId, model, textactions::Operation::Polish,
            QStringLiteral("Fix spelling, grammar, and typography."));
}

void AiController::runBulkRewrite(const QString &providerId, const QString &model,
                                  const QString &prompt)
{
    runBulk(providerId, model, textactions::Operation::Rewrite, prompt);
}

void AiController::runBulk(const QString &providerId, const QString &model,
                           textactions::Operation operation, const QString &instruction)
{
    if (!m_document || !m_workspace)
        return;

    const Document &document = m_document->session().document();
    QStringList blockIds;
    for (const Block &block : document.blocks) {
        if (blocktype::isRewritable(block.type) && !block.content.trimmed().isEmpty())
            blockIds << block.id;
    }
    if (blockIds.isEmpty()) {
        emit notice(QStringLiteral("There is nothing to rewrite."));
        return;
    }

    const WorkspaceStore::AiResultRecord record =
        createResult({}, operation == textactions::Operation::Polish
                             ? QStringLiteral("polish")
                             : QStringLiteral("bulk_rewrite"),
                     providerId, model, instruction);

    QVector<AiMessage> messages = textactions::buildBulkMessages(operation, document, instruction);
    AiClient *client = makeClient(providerId, model);
    if (!client) {
        settleResult(record.id, QStringLiteral("failed"), {},
                     QStringLiteral("Provider is not configured"));
        return;
    }

    const QString resultId = record.id;
    setBusy(true);

    QHash<QString, int> revisions;
    for (const QString &blockId : blockIds) {
        const Block *block = blockById(blockId);
        if (block)
            revisions.insert(blockId, block->revision);
    }

    connect(client, &AiClient::chatFinished, this,
            [this, resultId, client, revisions](const QString &text, int, int) {
                const QString payload = stripCodeFences(text);
                const QJsonDocument json = QJsonDocument::fromJson(payload.toUtf8());
                QJsonArray entries = json.isArray() ? json.array()
                                                    : json.object()
                                                          .value(QStringLiteral("blocks"))
                                                          .toArray();
                if (entries.isEmpty()) {
                    settleResult(resultId, QStringLiteral("failed"), {},
                                 QStringLiteral("The provider returned no block changes."));
                    setBusy(false);
                    client->deleteLater();
                    return;
                }

                int applied = 0;
                int skipped = 0;
                for (const QJsonValue &value : std::as_const(entries)) {
                    const QJsonObject object = value.toObject();
                    const QString blockId = object.value(QStringLiteral("id")).toString();
                    const QString content = object.value(QStringLiteral("content")).toString();
                    if (blockId.isEmpty())
                        continue;
                    const Block *block = blockById(blockId);
                    const int index = block ? m_document->session().document().indexOf(blockId)
                                            : -1;
                    if (!block || index < 0
                        || revisions.value(blockId, -1) != block->revision) {
                        ++skipped;
                        continue;
                    }
                    Block updated = *block;
                    updated.content = content;
                    m_document->session().updateBlock(index, updated, QStringLiteral("ai"));
                    ++applied;
                }

                settleResult(resultId, QStringLiteral("completed"),
                             QStringLiteral("%1 blocks updated").arg(applied), {});
                emit notice(skipped > 0
                                ? QStringLiteral("Updated %1 blocks; %2 changed while running "
                                                 "and were skipped.")
                                      .arg(applied)
                                      .arg(skipped)
                                : QStringLiteral("Updated %1 blocks.").arg(applied));
                setBusy(false);
                client->deleteLater();
            });
    connect(client, &AiClient::failed, this, [this, resultId, client](const QString &error) {
        settleResult(resultId, QStringLiteral("failed"), {}, error);
        setBusy(false);
        client->deleteLater();
    });

    client->chat(model, messages, 0.7, 16000, false);
    refreshResults();
}

void AiController::settleResult(const QString &resultId, const QString &status,
                                const QString &content, const QString &error)
{
    if (m_workspace != nullptr)
        m_workspace->store()->updateAiResult(resultId, status, content, error);
    for (WorkspaceStore::AiResultRecord &record : m_resultRecords) {
        if (record.id == resultId) {
            record.status = status;
            record.content = content;
            record.error = error;
        }
    }
    refreshResults();
    if (status == QLatin1String("failed"))
        emit notice(error);
}

void AiController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    if (!busy) {
        m_streamingText.clear();
        emit streamingChanged();
    }
    emit busyChanged();
}

void AiController::appendStreaming(const QString &delta)
{
    m_streamingText += delta;
    emit streamingChanged();
}

} // namespace writero
