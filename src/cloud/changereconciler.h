#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <functional>

#include "document/documentsession.h"
#include "storage/workspacestore.h"

namespace writero {

/// Everything the reconciler needs from the engine: the local document, the
/// id map, which blocks have unsent local edits, and sinks for conflicts and
/// media downloads.
struct ReconcileContext
{
    DocumentSession *session = nullptr;
    WorkspaceStore *store = nullptr;
    QString documentId;
    QSet<QString> pendingBlockIds;
    bool pendingTitle = false;
    std::function<void(const SyncConflict &)> recordConflict;
    std::function<void(const QString &remoteBlockId, const QJsonObject &media)> wantMedia;
    std::function<void()> resultsChanged;
};

/// Applies remote snapshots and change events to the local document.
///
/// Remote application never journals, marks the document dirty, or touches
/// the undo stack: local work stays exactly as the writer left it, and
/// divergent blocks become retained conflicts instead of being overwritten.
class ChangeReconciler
{
public:
    static Block blockFromJson(const QJsonObject &json, const QString &localId);

    static void applySnapshot(const QJsonObject &snapshot, const ReconcileContext &context);
    static void applyChange(const QJsonObject &change, const ReconcileContext &context);

private:
    static void applyResultChange(const QJsonObject &change, const ReconcileContext &context);
    static void noteMedia(const QJsonObject &blockJson, const QString &remoteBlockId,
                          const QJsonObject &mediaJson, const ReconcileContext &context);
    static void recordConflict(const ReconcileContext &context, const QString &kind,
                               const QString &localBlockId, const QString &localContent,
                               const QJsonObject &remoteBlockJson, const QString &remoteContent,
                               qint64 remoteSequence, int remoteLockVersion);
};

} // namespace writero
