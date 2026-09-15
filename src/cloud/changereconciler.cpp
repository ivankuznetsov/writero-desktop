#include "cloud/changereconciler.h"

#include "document/blocktype.h"

namespace writero {

namespace {

QString remoteIdOf(const QJsonObject &change)
{
    return change.value(QStringLiteral("payload"))
        .toObject()
        .value(QStringLiteral("block"))
        .toObject()
        .value(QStringLiteral("id"))
        .toVariant().toString();
}

} // namespace

Block ChangeReconciler::blockFromJson(const QJsonObject &json, const QString &localId)
{
    Block block;
    block.id = localId;
    bool ok = false;
    block.type = blocktype::fromKey(json.value(QStringLiteral("block_type")).toString(), &ok);
    if (!ok)
        block.type = BlockType::Text;
    block.content = json.value(QStringLiteral("content")).toString();
    block.metadata = json.value(QStringLiteral("metadata")).toObject().toVariantMap();
    block.revision = 1;
    return block;
}

void ChangeReconciler::noteMedia(const QJsonObject &blockJson, const QString &remoteBlockId,
                                 const QJsonObject &mediaJson, const ReconcileContext &context)
{
    Q_UNUSED(blockJson);
    if (mediaJson.isEmpty() || !context.wantMedia)
        return;
    context.wantMedia(remoteBlockId, mediaJson);
}

void ChangeReconciler::recordConflict(const ReconcileContext &context, const QString &kind,
                                      const QString &localBlockId, const QString &localContent,
                                      const QJsonObject &remoteBlockJson,
                                      const QString &remoteContent, qint64 remoteSequence,
                                      int remoteLockVersion)
{
    if (!context.recordConflict)
        return;
    SyncConflict conflict;
    conflict.id = newId();
    conflict.documentId = context.documentId;
    conflict.blockId = localBlockId;
    conflict.kind = kind;
    conflict.localContent = localContent;
    conflict.remoteContent = remoteContent;
    conflict.remoteSequence = remoteSequence;
    conflict.remoteLockVersion = remoteLockVersion;
    Q_UNUSED(remoteBlockJson);
    context.recordConflict(conflict);
}

void ChangeReconciler::applySnapshot(const QJsonObject &snapshot, const ReconcileContext &context)
{
    if (!context.session || !context.store)
        return;

    const QJsonArray remoteBlocks = snapshot.value(QStringLiteral("blocks")).toArray();
    BlockList merged;
    QSet<QString> remoteIds;

    for (const QJsonValue &value : remoteBlocks) {
        const QJsonObject blockJson = value.toObject();
        const QString remoteId = blockJson.value(QStringLiteral("id")).toVariant().toString();
        if (remoteId.isEmpty())
            continue;
        remoteIds.insert(remoteId);

        const QString localId = context.store->localIdForRemote(context.documentId, remoteId);
        const QJsonObject mediaJson = blockJson.value(QStringLiteral("media")).toObject();

        if (localId.isEmpty()) {
            Block block = blockFromJson(blockJson, newId());
            context.store->mapBlock(context.documentId, block.id, remoteId);
            merged.append(block);
            noteMedia(blockJson, remoteId, mediaJson, context);
            continue;
        }

        if (context.pendingBlockIds.contains(localId)) {
            // Keep the local edit; retain the remote version for review.
            const Block *local = context.session->document().blockById(localId);
            const QString remoteContent = blockJson.value(QStringLiteral("content")).toString();
            if (local && local->content != remoteContent) {
                recordConflict(context, QStringLiteral("block_update"), localId, local->content,
                               blockJson, remoteContent,
                               snapshot.value(QStringLiteral("watermark"))
                                   .toObject()
                                   .value(QStringLiteral("sequence"))
                                   .toVariant()
                                   .toLongLong(),
                               blockJson.value(QStringLiteral("lock_version")).toInt());
            }
            if (local)
                merged.append(*local);
            continue;
        }

        Block block = blockFromJson(blockJson, localId);
        merged.append(block);
        noteMedia(blockJson, remoteId, mediaJson, context);
    }

    // Local blocks with unsent creates are not on the server yet; keep them.
    for (const Block &local : context.session->document().blocks) {
        if (!context.pendingBlockIds.contains(local.id))
            continue;
        const QString remoteId = context.store->remoteIdForLocal(context.documentId, local.id);
        if (remoteId.isEmpty()) {
            merged.append(local);
        } else if (!remoteIds.contains(remoteId)) {
            merged.append(local);
            recordConflict(context, QStringLiteral("block_destroy"), local.id, local.content,
                           {}, {}, snapshot.value(QStringLiteral("watermark")).toObject()
                                         .value(QStringLiteral("sequence")).toVariant().toLongLong(), 0);
        }
    }

    for (const QJsonValue &value : snapshot.value(QStringLiteral("results")).toArray()) {
        const QJsonObject result = value.toObject();
        applyResultChange({
            {QStringLiteral("event"), QStringLiteral("result_%1_result")
                 .arg(result.value(QStringLiteral("kind")).toString())},
            {QStringLiteral("created_at"), result.value(QStringLiteral("created_at"))},
            {QStringLiteral("payload"), QJsonObject{{QStringLiteral("result"),
                 result.value(QStringLiteral("attributes"))}}},
        }, context);
    }

    const QJsonObject document = snapshot.value(QStringLiteral("document")).toObject();
    const QString remoteTitle = document.value(QStringLiteral("title")).toString();
    const QString localTitle = context.session->document().title;
    if (context.pendingTitle && localTitle != remoteTitle) {
        SyncConflict conflict;
        conflict.id = newId();
        conflict.documentId = context.documentId;
        conflict.kind = QStringLiteral("title");
        conflict.localContent = localTitle;
        conflict.remoteContent = remoteTitle;
        if (context.recordConflict)
            context.recordConflict(conflict);
        context.session->applyRemoteReset(merged, localTitle, {});
        return;
    }

    context.session->applyRemoteReset(merged, remoteTitle, {});
}

void ChangeReconciler::applyResultChange(const QJsonObject &change, const ReconcileContext &context)
{
    const QString event = change.value(QStringLiteral("event")).toString();
    const QJsonObject result = change.value(QStringLiteral("payload"))
                                   .toObject()
                                   .value(QStringLiteral("result"))
                                   .toObject();
    const QString remoteId = result.value(QStringLiteral("id")).toVariant().toString();
    if (remoteId.isEmpty())
        return;

    const QString remoteBlockId = result.value(QStringLiteral("block_id")).toVariant().toString();
    const QString localBlockId =
        context.store->localIdForRemote(context.documentId, remoteBlockId);
    if (localBlockId.isEmpty())
        return;

    QString kind;
    if (event == QLatin1String("result_rewrite_result"))
        kind = QStringLiteral("rewrite");
    else if (event == QLatin1String("result_research_result"))
        kind = QStringLiteral("research");
    else if (event == QLatin1String("result_image_generation_result"))
        kind = QStringLiteral("image_generation");
    else if (event == QLatin1String("result_image_explanation_result"))
        kind = QStringLiteral("image_explanation");
    else
        return;

    // Editorial result tables have independent numeric primary keys.
    const QString resultKey = kind + QLatin1Char(':') + remoteId;
    WorkspaceStore::AiResultRecord record;
    record.id = context.store->aiResultIdForRemote(context.documentId, resultKey);
    if (record.id.isEmpty())
        record.id = newId();
    record.documentId = context.documentId;
    record.blockId = localBlockId;
    record.kind = kind;
    record.providerId = QStringLiteral("writero");
    record.model = result.value(QStringLiteral("ai_model")).toString();
    record.status = result.value(QStringLiteral("status")).toString();
    record.error = result.value(QStringLiteral("error_message")).toString();
    record.remoteId = resultKey;
    if (kind == QLatin1String("image_explanation"))
        record.content = result.value(QStringLiteral("explanation")).toString();
    else if (kind != QLatin1String("image_generation"))
        record.content = result.value(QStringLiteral("result_content")).toString();
    record.createdAt = QDateTime::fromString(
        change.value(QStringLiteral("created_at")).toString(), Qt::ISODateWithMs);
    if (!record.createdAt.isValid())
        record.createdAt = QDateTime::currentDateTimeUtc();

    context.store->saveAiResult(record);
    if (context.resultsChanged)
        context.resultsChanged();
}

void ChangeReconciler::applyChange(const QJsonObject &change, const ReconcileContext &context)
{
    if (!context.session || !context.store)
        return;

    const QString event = change.value(QStringLiteral("event")).toString();
    const QJsonObject payload = change.value(QStringLiteral("payload")).toObject();
    const qint64 sequence = change.value(QStringLiteral("sequence")).toVariant().toLongLong();

    if (event == QLatin1String("block_create") || event == QLatin1String("block_update")) {
        const QJsonObject blockJson = payload.value(QStringLiteral("block")).toObject();
        const QString remoteId = blockJson.value(QStringLiteral("id")).toVariant().toString();
        if (remoteId.isEmpty())
            return;

        const QString localId = context.store->localIdForRemote(context.documentId, remoteId);
        const QJsonObject mediaJson = blockJson.value(QStringLiteral("media")).toObject();

        if (localId.isEmpty()) {
            Block block = blockFromJson(blockJson, newId());
            context.store->mapBlock(context.documentId, block.id, remoteId);
            const int index = qMax(0, blockJson.value(QStringLiteral("position")).toInt() - 1);
            context.session->applyRemoteInsert(block, index);
            context.store->setRemoteVersion(context.documentId, block.id,
                                            blockJson.value(QStringLiteral("lock_version")).toInt());
            noteMedia(blockJson, remoteId, mediaJson, context);
            return;
        }

        const Block *local = context.session->document().blockById(localId);
        const QString remoteContent = blockJson.value(QStringLiteral("content")).toString();
        if (context.pendingBlockIds.contains(localId)) {
            if (local && local->content != remoteContent) {
                recordConflict(context, event, localId, local->content, blockJson, remoteContent,
                               sequence, blockJson.value(QStringLiteral("lock_version")).toInt());
            }
            return;
        }

        Block updated = blockFromJson(blockJson, localId);
        context.session->applyRemoteUpdate(localId, updated);
        context.store->setRemoteVersion(context.documentId, localId,
                                        blockJson.value(QStringLiteral("lock_version")).toInt());
        noteMedia(blockJson, remoteId, mediaJson, context);
        return;
    }

    if (event == QLatin1String("block_destroy")) {
        const QString remoteId = remoteIdOf(change);
        const QString localId = context.store->localIdForRemote(context.documentId, remoteId);
        if (localId.isEmpty())
            return;
        const Block *local = context.session->document().blockById(localId);
        if (context.pendingBlockIds.contains(localId)) {
            recordConflict(context, QStringLiteral("block_destroy"), localId,
                           local ? local->content : QString(), {}, {}, sequence, 0);
            return;
        }
        context.session->applyRemoteRemove(localId);
        return;
    }

    if (event == QLatin1String("block_move")) {
        const QString remoteId = remoteIdOf(change);
        const QString localId = context.store->localIdForRemote(context.documentId, remoteId);
        if (localId.isEmpty())
            return;
        const int position = payload.value(QStringLiteral("block"))
                                 .toObject()
                                 .value(QStringLiteral("position"))
                                 .toInt();
        context.session->applyRemoteMove(localId, qMax(0, position - 1));
        const QJsonObject blockJson = payload.value(QStringLiteral("block")).toObject();
        if (!context.pendingBlockIds.contains(localId)
            && blockJson.contains(QStringLiteral("lock_version"))) {
            context.store->setRemoteVersion(context.documentId, localId,
                                            blockJson.value(QStringLiteral("lock_version")).toInt());
        }
        return;
    }

    if (event.startsWith(QLatin1String("result_"))) {
        applyResultChange(change, context);
        return;
    }

    if (event == QLatin1String("title_update")) {
        const QString remoteTitle = payload.value(QStringLiteral("title")).toString();
        const QString localTitle = context.session->document().title;
        if (context.pendingTitle) {
            if (localTitle != remoteTitle) {
                SyncConflict conflict;
                conflict.id = newId();
                conflict.documentId = context.documentId;
                conflict.kind = QStringLiteral("title");
                conflict.localContent = localTitle;
                conflict.remoteContent = remoteTitle;
                if (context.recordConflict)
                    context.recordConflict(conflict);
            }
            return;
        }
        context.session->applyRemoteTitle(remoteTitle);
        if (payload.contains(QStringLiteral("title_version"))) {
            const Document &document = context.session->document();
            context.session->setCloudState(document.cloudId, document.cloudState,
                document.syncCursor, document.feedGeneration,
                payload.value(QStringLiteral("title_version")).toVariant().toLongLong());
        }
        return;
    }
}

} // namespace writero
