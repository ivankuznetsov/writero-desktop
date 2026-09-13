#include "cloud/syncengine.h"

#include <algorithm>

#include <QDateTime>
#include <QJsonDocument>

#include "document/blocktype.h"

namespace writero {

namespace {

constexpr int MaxBatchSize = 200;

QJsonObject blockAttributesJson(const Block &block)
{
    return QJsonObject{
        {QStringLiteral("content"), block.content},
        {QStringLiteral("block_type"), blocktype::toKey(block.type)},
        {QStringLiteral("metadata"), QJsonObject::fromVariantMap(block.metadata)},
    };
}

} // namespace

SyncEngine::SyncEngine(QObject *parent)
    : QObject(parent)
{
    connect(&m_client, &CloudClient::documentCreated, this, &SyncEngine::handleDocumentCreated);
    connect(&m_client, &CloudClient::mutationsApplied, this, &SyncEngine::onMutationsApplied);
    connect(&m_client, &CloudClient::changesReceived, this, &SyncEngine::onChangesReceived);
    connect(&m_client, &CloudClient::snapshotReceived, this, &SyncEngine::onSnapshotReceived);
    connect(&m_client, &CloudClient::requestFailed, this, &SyncEngine::onRequestFailed);
    connect(&m_client, &CloudClient::mediaUploaded, this, [this](const QJsonObject &body) {
        if (m_inFlightOperations.isEmpty() || !m_document)
            return;
        const PendingOperation operation = m_inFlightOperations.first();
        const QString localId = operation.payload.value(QStringLiteral("local_block_id")).toString();
        const QString remoteId =
            m_workspace->store()->remoteIdForLocal(m_document->documentId(), localId);
        if (remoteId.isEmpty()) {
            m_workspace->store()->deletePendingOperation(m_document->documentId(),
                                                         operation.operationId);
            m_inFlightOperations.clear();
            pushNextBatch();
            return;
        }
        const QString signedId = body.value(QStringLiteral("signed_id")).toString();
        QJsonObject mutation{
            {QStringLiteral("operation_id"), operation.operationId},
            {QStringLiteral("kind"), QStringLiteral("attach_media")},
            {QStringLiteral("block_id"), remoteId},
            {QStringLiteral("lock_version"),
             m_workspace->store()->remoteVersionForLocal(m_document->documentId(), localId)},
            {QStringLiteral("signed_id"), signedId},
        };
        m_stage = Stage::Pushing;
        m_client.postMutations(m_cloudId, QJsonArray{mutation});
    });
    connect(&m_client, &CloudClient::mediaDownloaded, this,
            [this](const QString &remoteBlockId, const QByteArray &data, const QString &mime) {
                if (!m_workspace || !m_document || m_cloudId.isEmpty())
                    return;
                const QString localId =
                    m_workspace->store()->localIdForRemote(m_document->documentId(), remoteBlockId);
                if (localId.isEmpty())
                    return;

                const QString sha = m_workspace->media()->importData(
                    data, QStringLiteral("cloud-media"), mime);
                const qint64 mediaId = m_workspace->store()->ensureMedia(
                    sha, QStringLiteral("cloud-media"), mime, data.size());
                if (mediaId <= 0)
                    return;

                const Block *local = m_document->session().document().blockById(localId);
                if (!local)
                    return;
                Block updated = *local;
                updated.mediaId = mediaId;
                updated.type = BlockType::Media;
                m_document->session().applyRemoteUpdate(localId, updated);
            });
}

void SyncEngine::setWorkspace(Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    m_workspace = workspace;
    emit changed();
}

void SyncEngine::setAccount(AccountSession *account)
{
    if (m_account == account)
        return;
    if (m_account)
        m_account->disconnect(this);
    m_account = account;
    m_client.setAccount(account);
    if (m_account) {
        connect(m_account, &AccountSession::changed, this, [this] {
            if (!m_account || m_account->isConnected() || !m_busy)
                return;
            setBusy(false);
            setError(QStringLiteral("Signed out. Local documents and queued changes are kept."));
            const bool linked = m_document
                && !m_document->session().document().cloudId.isEmpty();
            setState(linked ? QStringLiteral("auth") : QStringLiteral("local"));
        });
    }
    emit changed();
}

void SyncEngine::setDocument(DocumentController *document)
{
    if (m_document == document)
        return;
    m_document = document;
    if (m_document) {
        connect(m_document, &DocumentController::loaded, this, [this] {
            const Document &doc = m_document->session().document();
            m_cloudId = doc.cloudId;
            m_state = m_cloudId.isEmpty() ? QStringLiteral("local") : doc.cloudState;
            refreshSummary();
            emit changed();
        });
        connect(m_document, &DocumentController::saved, this, [this] {
            refreshSummary();
            if (!m_cloudId.isEmpty() && m_state == QLatin1String("synced")
                && m_stage == Stage::Idle) {
                syncNow();
            }
        });
    }
    const Document &doc = document ? document->session().document() : Document();
    m_cloudId = doc.cloudId;
    m_state = m_cloudId.isEmpty() ? QStringLiteral("local") : doc.cloudState;
    refreshSummary();
    emit changed();
}

void SyncEngine::setState(const QString &state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit changed();
}

void SyncEngine::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit changed();
}

void SyncEngine::setError(const QString &error)
{
    m_lastError = error;
    emit changed();
}

void SyncEngine::refreshSummary()
{
    const QString documentId = m_document ? m_document->documentId() : QString();
    if (m_workspace && m_workspace->isReady() && !documentId.isEmpty()) {
        m_pendingCount = m_workspace->store()->pendingOperations(documentId).size();
        m_conflictCount = m_workspace->store()->conflictCount(documentId);
    } else {
        m_pendingCount = 0;
        m_conflictCount = 0;
    }
    emit changed();
    emit conflictsChanged();
}

void SyncEngine::refreshCounts()
{
    refreshSummary();
}

bool SyncEngine::accountMatchesDocument(QString *message) const
{
    if (!m_document || !m_account)
        return true;
    const QString bound = m_document->session().document().syncAccountEmail;
    if (bound.isEmpty())
        return true;
    const QString current = m_account->accountEmail();
    if (current.isEmpty() || bound == current)
        return true;
    if (message) {
        *message = QStringLiteral("This document is linked to %1. Sign in with that account "
                                  "or duplicate the document.")
                       .arg(bound);
    }
    return false;
}

void SyncEngine::connectDocument()
{
    if (!m_workspace || !m_workspace->isReady() || !m_document || !m_account) {
        setError(QStringLiteral("Open a document and sign in first."));
        return;
    }
    if (m_account->accessToken().isEmpty() || !m_account->isConnected()) {
        setError(QStringLiteral("Connect your Writero account first."));
        return;
    }
    if (m_document->documentId().isEmpty())
        return;

    QString guardMessage;
    if (!accountMatchesDocument(&guardMessage)) {
        setError(guardMessage);
        setState(QStringLiteral("account_mismatch"));
        setBusy(false);
        return;
    }

    setError({});
    setBusy(true);
    const Document &doc = m_document->session().document();
    if (doc.cloudId.isEmpty()) {
        m_stage = Stage::Creating;
        m_client.createDocument(doc.title);
        return;
    }

    m_cloudId = doc.cloudId;
    setState(QStringLiteral("syncing"));
    pushNextBatch();
}

void SyncEngine::handleDocumentCreated(const QJsonObject &body)
{
    const QJsonObject document = body.value(QStringLiteral("document")).toObject();
    const QString id = document.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        setError(QStringLiteral("The server did not return a document id."));
        setBusy(false);
        return;
    }

    const qint64 generation =
        body.value(QStringLiteral("watermark")).toObject().value(QStringLiteral("generation")).toVariant().toLongLong();
    const qint64 titleVersion = document.value(QStringLiteral("title_version")).toVariant().toLongLong();
    m_cloudId = id;

    m_document->session().setCloudState(id, QStringLiteral("connected"), 0, generation,
                                        titleVersion,
                                        m_account ? m_account->accountEmail() : QString());
    m_workspace->store()->updateDocumentSync(m_document->documentId(), id,
                                             QStringLiteral("connected"), 0, generation);
    m_workspace->store()->saveDocument(m_document->session().document(), {}, {});
    enqueueReconnectUploads();
    setState(QStringLiteral("syncing"));
    pushNextBatch();
}

bool SyncEngine::enqueueOperation(const PendingOperation &operation)
{
    if (!m_document || !m_workspace)
        return false;
    QString error;
    if (!m_workspace->store()->saveDocument(m_document->session().document(), {}, { operation },
                                            &error)) {
        setError(error);
        return false;
    }
    refreshSummary();
    return true;
}

bool SyncEngine::enqueueReconnectUploads()
{
    const Document &document = m_document->session().document();
    const BlockList &blocks = document.blocks;
    bool queued = false;

    for (int i = 0; i < blocks.size(); ++i) {
        const Block &block = blocks.at(i);
        if (block.type == BlockType::Divider && block.content.isEmpty())
            continue;
        if (!m_workspace->store()->remoteIdForLocal(document.id, block.id).isEmpty())
            continue;

        PendingOperation operation;
        operation.operationId = newId();
        operation.kind = QStringLiteral("create_block");
        operation.payload = QJsonObject{
            {QStringLiteral("local_block_id"), block.id},
            {QStringLiteral("after_local_block_id"), i > 0 ? blocks.at(i - 1).id : QString()},
            {QStringLiteral("attributes"), blockAttributesJson(block)},
        };
        queued = enqueueOperation(operation) || queued;

        if (block.type == BlockType::Media && block.mediaId > 0) {
            PendingOperation media;
            media.operationId = newId();
            media.kind = QStringLiteral("attach_media");
            media.payload = QJsonObject{{QStringLiteral("local_block_id"), block.id}};
            queued = enqueueOperation(media) || queued;
        }
    }
    return queued;
}

void SyncEngine::pushNextBatch()
{
    if (!m_document || m_cloudId.isEmpty()) {
        finishSync();
        return;
    }

    const QVector<PendingOperation> pending =
        m_workspace->store()->pendingOperations(m_document->documentId());
    if (pending.isEmpty()) {
        pullChanges();
        return;
    }

    setBusy(true);
    setState(QStringLiteral("syncing"));

    const PendingOperation first = pending.first();
    if (first.kind == QLatin1String("attach_media")) {
        const QString localId = first.payload.value(QStringLiteral("local_block_id")).toString();
        const Block *block = m_document->session().document().blockById(localId);
        if (!block || block->mediaId <= 0) {
            m_workspace->store()->deletePendingOperation(m_document->documentId(),
                                                         first.operationId);
            pushNextBatch();
            return;
        }
        const QString path = m_workspace->mediaPath(block->mediaId);
        if (path.isEmpty()) {
            m_workspace->store()->deletePendingOperation(m_document->documentId(),
                                                         first.operationId);
            pushNextBatch();
            return;
        }
        m_inFlightOperations = { first };
        m_stage = Stage::Pushing;
        m_client.uploadMedia(path);
        return;
    }

    QVector<PendingOperation> batched;
    const QJsonArray batch = buildBatch(pending, &batched);
    if (batch.isEmpty()) {
        // Nothing can be pushed yet (missing references); stop rather than spin.
        setState(QStringLiteral("paused"));
        setBusy(false);
        return;
    }
    m_inFlightOperations = batched;
    m_stage = Stage::Pushing;
    m_client.postMutations(m_cloudId, batch);
}

QJsonArray SyncEngine::buildBatch(const QVector<PendingOperation> &operations,
                                  QVector<PendingOperation> *batched) const
{
    QJsonArray batch;
    const QString documentId = m_document->documentId();
    WorkspaceStore *store = m_workspace->store();
    QSet<QString> touchedLocalIds;

    for (const PendingOperation &operation : operations) {
        if (batch.size() >= MaxBatchSize || operation.kind == QLatin1String("attach_media"))
            break;

        const QString localId = operation.payload.value(QStringLiteral("local_block_id")).toString();
        // A block may appear once per batch: the server applies operations in
        // order and bumps the lock each time, so a second mutation for the
        // same block must build on the acknowledgement of the first.
        if (!localId.isEmpty() && touchedLocalIds.contains(localId))
            break;
        if (!localId.isEmpty())
            touchedLocalIds.insert(localId);
        const QString remoteId = store->remoteIdForLocal(documentId, localId);
        QJsonObject mutation{{QStringLiteral("operation_id"), operation.operationId}};

        if (operation.kind == QLatin1String("update_block") && remoteId.isEmpty()) {
            // The block was created locally and never pushed; upsert it.
            mutation.insert(QStringLiteral("kind"), QStringLiteral("create_block"));
            const QString afterLocal =
                operation.payload.value(QStringLiteral("after_local_block_id")).toString();
            const QString afterRemote = store->remoteIdForLocal(documentId, afterLocal);
            if (!afterRemote.isEmpty())
                mutation.insert(QStringLiteral("after_block_id"), afterRemote);
            mutation.insert(QStringLiteral("attributes"),
                            operation.payload.value(QStringLiteral("attributes")).toObject());
        } else if (operation.kind == QLatin1String("create_block")) {
            const QString afterLocal =
                operation.payload.value(QStringLiteral("after_local_block_id")).toString();
            const QString afterRemote = store->remoteIdForLocal(documentId, afterLocal);
            if (!afterRemote.isEmpty())
                mutation.insert(QStringLiteral("after_block_id"), afterRemote);
            mutation.insert(QStringLiteral("kind"), operation.kind);
            mutation.insert(QStringLiteral("attributes"),
                            operation.payload.value(QStringLiteral("attributes")).toObject());
        } else if (operation.kind == QLatin1String("update_block")) {
            mutation.insert(QStringLiteral("kind"), operation.kind);
            mutation.insert(QStringLiteral("block_id"), remoteId);
            mutation.insert(QStringLiteral("lock_version"),
                            store->remoteVersionForLocal(documentId, localId));
            mutation.insert(QStringLiteral("attributes"),
                            operation.payload.value(QStringLiteral("attributes")).toObject());
        } else if (operation.kind == QLatin1String("delete_block")) {
            if (remoteId.isEmpty())
                continue; // never reached the server; drop it.
            mutation.insert(QStringLiteral("kind"), operation.kind);
            mutation.insert(QStringLiteral("block_id"), remoteId);
            mutation.insert(QStringLiteral("lock_version"),
                            store->remoteVersionForLocal(documentId, localId));
        } else if (operation.kind == QLatin1String("move_block")) {
            if (remoteId.isEmpty())
                continue;
            const QString afterLocal =
                operation.payload.value(QStringLiteral("after_local_block_id")).toString();
            const QString afterRemote = store->remoteIdForLocal(documentId, afterLocal);
            mutation.insert(QStringLiteral("kind"), operation.kind);
            mutation.insert(QStringLiteral("block_id"), remoteId);
            mutation.insert(QStringLiteral("lock_version"),
                            store->remoteVersionForLocal(documentId, localId));
            if (!afterRemote.isEmpty())
                mutation.insert(QStringLiteral("after_block_id"), afterRemote);
        } else if (operation.kind == QLatin1String("update_title")) {
            mutation.insert(QStringLiteral("kind"), operation.kind);
            mutation.insert(QStringLiteral("title"),
                            operation.payload.value(QStringLiteral("title")).toString());
            mutation.insert(QStringLiteral("title_version"),
                            m_document->session().document().syncTitleVersion);
        } else {
            continue;
        }

        batch.append(mutation);
        if (batched)
            batched->append(operation);
    }
    return batch;
}

void SyncEngine::onMutationsApplied(const QJsonObject &body)
{
    if (m_inFlightOperations.size() == 1
        && m_inFlightOperations.first().kind == QLatin1String("attach_media")) {
        // The attach mutation was acknowledged; retire its pending operation.
        m_workspace->store()->deletePendingOperation(m_document->documentId(),
                                                     m_inFlightOperations.first().operationId);
        const qint64 cursor = body.value(QStringLiteral("cursor")).toVariant().toLongLong();
        const qint64 generation = body.value(QStringLiteral("generation")).toVariant().toLongLong();
        const Document &document = m_document->session().document();
        m_document->session().setCloudState(document.cloudId, QStringLiteral("connected"), cursor,
                                            generation, document.syncTitleVersion);
        m_workspace->store()->updateDocumentSync(m_document->documentId(), m_cloudId,
                                                 QStringLiteral("connected"), cursor, generation);
        m_inFlightOperations.clear();
        refreshSummary();
        pushNextBatch();
        return;
    }

    applyMutationResults(body);
    m_inFlightOperations.clear();
    pushNextBatch();
}

void SyncEngine::applyMutationResults(const QJsonObject &body)
{
    const QJsonArray results = body.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject result = value.toObject();
        const QString operationId = result.value(QStringLiteral("operation_id")).toString();
        const auto it = std::find_if(m_inFlightOperations.cbegin(), m_inFlightOperations.cend(),
                                     [&operationId](const PendingOperation &operation) {
                                         return operation.operationId == operationId;
                                     });
        if (it == m_inFlightOperations.cend())
            continue;

        const QString localId = it->payload.value(QStringLiteral("local_block_id")).toString();
        const QJsonObject block = result.value(QStringLiteral("block")).toObject();
        if (!block.isEmpty() && !localId.isEmpty()) {
            const QString remoteId = block.value(QStringLiteral("id")).toString();
            if (m_workspace->store()->remoteIdForLocal(m_document->documentId(), localId).isEmpty())
                m_workspace->store()->mapBlock(m_document->documentId(), localId, remoteId);
            m_workspace->store()->setRemoteVersion(
                m_document->documentId(), localId,
                block.value(QStringLiteral("lock_version")).toInt());
        }
        if (result.contains(QStringLiteral("title_version"))) {
            const qint64 titleVersion = result.value(QStringLiteral("title_version")).toVariant().toLongLong();
            const Document &document = m_document->session().document();
            m_document->session().setCloudState(document.cloudId, document.cloudState,
                                                document.syncCursor, document.feedGeneration,
                                                titleVersion);
        }
        m_workspace->store()->deletePendingOperation(m_document->documentId(), operationId);
    }

    const qint64 cursor = body.value(QStringLiteral("cursor")).toVariant().toLongLong();
    const qint64 generation = body.value(QStringLiteral("generation")).toVariant().toLongLong();
    const Document &document = m_document->session().document();
    m_document->session().setCloudState(document.cloudId, QStringLiteral("connected"), cursor,
                                        generation, document.syncTitleVersion);
    m_workspace->store()->updateDocumentSync(m_document->documentId(), m_cloudId,
                                             QStringLiteral("connected"), cursor, generation);
    refreshSummary();
}

void SyncEngine::pullChanges()
{
    if (!m_document || m_cloudId.isEmpty()) {
        finishSync();
        return;
    }
    if (m_document->session().document().feedGeneration <= 0) {
        resnapshot();
        return;
    }
    m_stage = Stage::Pulling;
    setState(QStringLiteral("syncing"));
    m_client.fetchChanges(m_cloudId, m_document->session().document().syncCursor,
                          m_document->session().document().feedGeneration);
}

ReconcileContext SyncEngine::reconcileContext()
{
    ReconcileContext context;
    context.session = const_cast<DocumentSession *>(&m_document->session());
    context.store = m_workspace->store();
    context.documentId = m_document->documentId();

    const QVector<PendingOperation> pending =
        m_workspace->store()->pendingOperations(context.documentId);
    for (const PendingOperation &operation : pending) {
        const QString localId = operation.payload.value(QStringLiteral("local_block_id")).toString();
        if (!localId.isEmpty())
            context.pendingBlockIds.insert(localId);
        if (operation.kind == QLatin1String("update_title"))
            context.pendingTitle = true;
    }

    context.resultsChanged = [this] { emit cloudResultsChanged(); };
    context.recordConflict = [this](const SyncConflict &conflict) {
        m_workspace->store()->saveConflict(conflict);
        refreshSummary();
        emit conflictsChanged();
    };
    context.wantMedia = [this](const QString &remoteBlockId, const QJsonObject &media) {
        Q_UNUSED(media);
        m_client.downloadMedia(m_cloudId, remoteBlockId);
    };
    return context;
}

void SyncEngine::onChangesReceived(const QJsonObject &body)
{
    const qint64 generation = body.value(QStringLiteral("generation")).toVariant().toLongLong();
    if (generation != m_document->session().document().feedGeneration) {
        resnapshot();
        return;
    }

    const ReconcileContext context = reconcileContext();
    for (const QJsonValue &value : body.value(QStringLiteral("changes")).toArray())
        ChangeReconciler::applyChange(value.toObject(), context);

    const qint64 nextCursor = body.value(QStringLiteral("next_cursor")).toVariant().toLongLong();
    const Document &document = m_document->session().document();
    m_document->session().setCloudState(m_cloudId, QStringLiteral("connected"), nextCursor,
                                        generation, document.syncTitleVersion);
    m_workspace->store()->updateDocumentSync(m_document->documentId(), m_cloudId,
                                             QStringLiteral("connected"), nextCursor, generation);

    if (body.value(QStringLiteral("has_more")).toBool()) {
        m_client.fetchChanges(m_cloudId, nextCursor, generation);
        return;
    }
    finishSync();
}

void SyncEngine::resnapshot()
{
    if (m_cloudId.isEmpty()) {
        finishSync();
        return;
    }
    m_stage = Stage::Snapshotting;
    setState(QStringLiteral("syncing"));
    m_snapshotBlocks.clear();
    m_snapshotLockVersions.clear();
    m_snapshotPage = 1;
    m_snapshotTotalPages = 1;
    m_client.fetchSnapshot(m_cloudId);
}

void SyncEngine::onSnapshotReceived(const QJsonObject &body)
{
    for (const QJsonValue &value : body.value(QStringLiteral("blocks")).toArray()) {
        const QJsonObject block = value.toObject();
        const Block parsed = ChangeReconciler::blockFromJson(block, newId());
        m_snapshotBlocks.append(parsed);
        const QString remoteId = block.value(QStringLiteral("id")).toString();
        m_snapshotLockVersions.insert(remoteId, block.value(QStringLiteral("lock_version")).toInt());
    }

    const QJsonObject page = body.value(QStringLiteral("page")).toObject();
    const int number = page.value(QStringLiteral("number")).toInt();
    m_snapshotTotalPages = page.value(QStringLiteral("total_pages")).toInt();
    if (number < m_snapshotTotalPages) {
        const QString leaseId = body.value(QStringLiteral("lease")).toObject()
                                    .value(QStringLiteral("id")).toString();
        m_snapshotPage = number + 1;
        m_client.fetchSnapshotPage(m_cloudId, leaseId, m_snapshotPage);
        return;
    }

    // Rebuild a single snapshot body from the accumulated pages.
    QJsonArray blocks;
    for (const Block &block : m_snapshotBlocks) {
        QJsonObject json = QJsonObject::fromVariantMap(block.toJson());
        json.insert(QStringLiteral("block_type"), blocktype::toKey(block.type));
        blocks.append(json);
    }
    QJsonObject combined = body;
    combined.insert(QStringLiteral("blocks"), blocks);
    combined.insert(QStringLiteral("document"), body.value(QStringLiteral("document")));
    combined.insert(QStringLiteral("watermark"), body.value(QStringLiteral("watermark")));

    const ReconcileContext context = reconcileContext();
    ChangeReconciler::applySnapshot(combined, context);

    const QJsonObject watermark = body.value(QStringLiteral("watermark")).toObject();
    const qint64 cursor = watermark.value(QStringLiteral("sequence")).toVariant().toLongLong();
    const qint64 generation = watermark.value(QStringLiteral("generation")).toVariant().toLongLong();
    const qint64 titleVersion = body.value(QStringLiteral("document"))
                                    .toObject()
                                    .value(QStringLiteral("title_version"))
                                    .toVariant()
                                    .toLongLong();

    // Refresh remote lock versions for every mapped block.
    for (const Block &local : m_document->session().document().blocks) {
        const QString remoteId =
            m_workspace->store()->remoteIdForLocal(m_document->documentId(), local.id);
        if (remoteId.isEmpty())
            continue;
        const int version = m_snapshotLockVersions.value(remoteId, 0);
        if (version > 0)
            m_workspace->store()->setRemoteVersion(m_document->documentId(), local.id, version);
    }

    m_document->session().setCloudState(m_cloudId, QStringLiteral("connected"), cursor, generation,
                                        titleVersion,
                                        m_account ? m_account->accountEmail() : QString());
    m_workspace->store()->updateDocumentSync(m_document->documentId(), m_cloudId,
                                             QStringLiteral("connected"), cursor, generation);
    m_workspace->store()->saveDocument(m_document->session().document(), {}, {});
    refreshSummary();

    if (!m_workspace->store()->pendingOperations(m_document->documentId()).isEmpty())
        pushNextBatch();
    else
        finishSync();
}

void SyncEngine::finishSync()
{
    const Document &document = m_document->session().document();
    if (document.cloudState == QLatin1String("conflict")) {
        setState(QStringLiteral("conflict"));
    } else {
        setState(QStringLiteral("synced"));
    }
    setBusy(false);
    m_stage = Stage::Idle;
    refreshSummary();
}

void SyncEngine::syncNow()
{
    if (!m_document || m_cloudId.isEmpty()) {
        connectDocument();
        return;
    }
    if (m_busy)
        return;
    if (!m_account || m_account->accessToken().isEmpty() || !m_account->isConnected()) {
        setError(QStringLiteral("Sign in to sync this document. Local work is kept."));
        setState(QStringLiteral("auth"));
        return;
    }
    QString guardMessage;
    if (!accountMatchesDocument(&guardMessage)) {
        setError(guardMessage);
        setState(QStringLiteral("account_mismatch"));
        return;
    }
    setError({});
    setBusy(true);
    setState(QStringLiteral("syncing"));
    pushNextBatch();
}

void SyncEngine::disconnectDocument()
{
    if (!m_document)
        return;
    const Document &document = m_document->session().document();
    if (document.cloudId.isEmpty())
        return;

    m_workspace->store()->updateDocumentSync(m_document->documentId(), m_cloudId,
                                             QStringLiteral("paused"),
                                             document.syncCursor, document.feedGeneration);
    m_document->session().setCloudState(m_cloudId, QStringLiteral("paused"), document.syncCursor,
                                        document.feedGeneration, document.syncTitleVersion);
    setState(QStringLiteral("paused"));
    setBusy(false);
    refreshSummary();
}

QVariantList SyncEngine::conflictList() const
{
    QVariantList result;
    if (!m_workspace || !m_document)
        return result;
    for (const SyncConflict &conflict : m_workspace->store()->conflicts(m_document->documentId())) {
        result.append(QVariantMap{
            {QStringLiteral("id"), conflict.id},
            {QStringLiteral("blockId"), conflict.blockId},
            {QStringLiteral("kind"), conflict.kind},
            {QStringLiteral("localContent"), conflict.localContent},
            {QStringLiteral("remoteContent"), conflict.remoteContent},
            {QStringLiteral("createdAt"), conflict.createdAt},
        });
    }
    return result;
}

void SyncEngine::resolveConflict(const QString &conflictId, bool keepLocal)
{
    if (!m_workspace || !m_document)
        return;

    SyncConflict selected;
    bool found = false;
    for (const SyncConflict &conflict : m_workspace->store()->conflicts(m_document->documentId())) {
        if (conflict.id == conflictId) {
            selected = conflict;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    if (!keepLocal) {
        if (selected.kind == QLatin1String("block_destroy")) {
            m_document->session().applyRemoteRemove(selected.blockId);
        } else if (selected.kind == QLatin1String("title")) {
            m_document->session().applyRemoteTitle(selected.remoteContent);
        } else {
            const Block *local = m_document->session().document().blockById(selected.blockId);
            if (local) {
                Block updated = *local;
                updated.content = selected.remoteContent;
                m_document->session().applyRemoteUpdate(selected.blockId, updated);
            }
        }
        // Drop local pending edits for this block so the remote version wins.
        for (const PendingOperation &operation :
             m_workspace->store()->pendingOperations(m_document->documentId())) {
            const QString localId =
                operation.payload.value(QStringLiteral("local_block_id")).toString();
            if (localId == selected.blockId
                || (selected.kind == QLatin1String("title")
                    && operation.kind == QLatin1String("update_title"))) {
                m_workspace->store()->deletePendingOperation(m_document->documentId(),
                                                             operation.operationId);
            }
        }
    } else {
        if (selected.kind == QLatin1String("block_destroy")) {
            const Block *local = m_document->session().document().blockById(selected.blockId);
            if (local) {
                PendingOperation operation;
                operation.operationId = newId();
                operation.kind = QStringLiteral("create_block");
                operation.payload = QJsonObject{
                    {QStringLiteral("local_block_id"), local->id},
                    {QStringLiteral("attributes"), blockAttributesJson(*local)},
                };
                enqueueOperation(operation);
            }
        } else if (selected.kind == QLatin1String("title")) {
            PendingOperation operation;
            operation.operationId = newId();
            operation.kind = QStringLiteral("update_title");
            operation.payload = QJsonObject{
                {QStringLiteral("title"), m_document->session().document().title},
            };
            enqueueOperation(operation);
        } else {
            const Block *local = m_document->session().document().blockById(selected.blockId);
            if (local) {
                m_workspace->store()->setRemoteVersion(m_document->documentId(), local->id,
                                                       selected.remoteLockVersion);
                PendingOperation operation;
                operation.operationId = newId();
                operation.kind = QStringLiteral("update_block");
                operation.payload = QJsonObject{
                    {QStringLiteral("local_block_id"), local->id},
                    {QStringLiteral("attributes"), blockAttributesJson(*local)},
                };
                enqueueOperation(operation);
            }
        }
    }

    m_workspace->store()->resolveConflict(conflictId);
    refreshSummary();
    emit conflictsChanged();
    syncNow();
}

void SyncEngine::handleConflict(int status, const QJsonObject &body)
{
    Q_UNUSED(status);
    const QJsonObject current = body.value(QStringLiteral("current_block")).toObject();
    const int index = body.value(QStringLiteral("index")).toInt();
    if (index < 0 || index >= m_inFlightOperations.size()) {
        setError(QStringLiteral("The server reported a conflict for an unknown operation."));
        setState(QStringLiteral("conflict"));
        setBusy(false);
        return;
    }
    const PendingOperation operation = m_inFlightOperations.at(index);
    const QString localId = operation.payload.value(QStringLiteral("local_block_id")).toString();
    const Block *local = m_document->session().document().blockById(localId);
    if (local) {
        SyncConflict conflict;
        conflict.id = newId();
        conflict.documentId = m_document->documentId();
        conflict.blockId = localId;
        conflict.kind = QStringLiteral("block_update");
        conflict.localContent = local->content;
        conflict.remoteContent = current.value(QStringLiteral("content")).toString();
        conflict.remoteLockVersion = current.value(QStringLiteral("lock_version")).toInt();
        if (!m_workspace->store()->saveConflict(conflict))
            qWarning().noquote() << "writero sync: saveConflict failed:"
                                 << m_workspace->store()->lastError();
    }

    m_workspace->store()->updateDocumentSync(m_document->documentId(), m_cloudId,
                                             QStringLiteral("conflict"),
                                             m_document->session().document().syncCursor,
                                             m_document->session().document().feedGeneration);
    const Document &document = m_document->session().document();
    m_document->session().setCloudState(m_cloudId, QStringLiteral("conflict"),
                                        document.syncCursor, document.feedGeneration,
                                        document.syncTitleVersion);
    setState(QStringLiteral("conflict"));
    setBusy(false);
    refreshSummary();
    emit notice(QStringLiteral("This document changed in another session. Review the conflicts."));
}

void SyncEngine::onRequestFailed(const QString &operation, int status, const QJsonObject &body,
                                 const QString &message)
{
    if (operation == QLatin1String("download_media") || operation == QLatin1String("upload_media")) {
        // Media failures leave the pending operation queued for a later sync.
        setError(message.isEmpty() ? QStringLiteral("Media transfer failed.") : message);
        setState(QStringLiteral("offline"));
        setBusy(false);
        refreshSummary();
        return;
    }

    if (status == 401) {
        setError(QStringLiteral("The session expired. Sign in again."));
        setState(QStringLiteral("auth"));
        setBusy(false);
        return;
    }
    if (status == 410 && operation == QLatin1String("changes")) {
        resnapshot();
        return;
    }
    if (status == 404
        && (operation == QLatin1String("snapshot") || operation == QLatin1String("changes"))) {
        setError(QStringLiteral("This document was deleted on the server. "
                                "Your local copy and queued changes are kept."));
        setState(QStringLiteral("deleted"));
        setBusy(false);
        refreshSummary();
        return;
    }
    if (status == 409 && operation == QLatin1String("mutations")) {
        handleConflict(status, body);
        return;
    }

    if (status == 0) {
        setError(QStringLiteral("You are offline. Changes stay queued locally."));
        setState(QStringLiteral("offline"));
    } else {
        setError(message.isEmpty()
                     ? QStringLiteral("Sync failed with status %1.").arg(status)
                     : message);
        setState(QStringLiteral("error"));
    }
    setBusy(false);
    refreshSummary();
}

} // namespace writero
