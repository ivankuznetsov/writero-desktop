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
        .toString();
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
        const QString remoteId = blockJson.value(QStringLiteral("id")).toString();
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
        if (context.store->remoteIdForLocal(context.documentId, local.id).isEmpty())
            merged.append(local);
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

void ChangeReconciler::applyChange(const QJsonObject &change, const ReconcileContext &context)
{
    if (!context.session || !context.store)
        return;

    const QString event = change.value(QStringLiteral("event")).toString();
    const QJsonObject payload = change.value(QStringLiteral("payload")).toObject();
    const qint64 sequence = change.value(QStringLiteral("sequence")).toVariant().toLongLong();

    if (event == QLatin1String("block_create") || event == QLatin1String("block_update")) {
        const QJsonObject blockJson = payload.value(QStringLiteral("block")).toObject();
        const QString remoteId = blockJson.value(QStringLiteral("id")).toString();
        if (remoteId.isEmpty())
            return;

        const QString localId = context.store->localIdForRemote(context.documentId, remoteId);
        const QJsonObject mediaJson = blockJson.value(QStringLiteral("media")).toObject();

        if (localId.isEmpty()) {
            Block block = blockFromJson(blockJson, newId());
            context.store->mapBlock(context.documentId, block.id, remoteId);
            const int index = qMax(0, blockJson.value(QStringLiteral("position")).toInt() - 1);
            context.session->applyRemoteInsert(block, index);
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
        return;
    }
}

} // namespace writero
