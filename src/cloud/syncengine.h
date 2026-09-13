#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QVector>

#include "cloud/accountsession.h"
#include "cloud/changereconciler.h"
#include "cloud/cloudclient.h"
#include "editor/documentcontroller.h"
#include "storage/workspace.h"

namespace writero {

/// Synchronizes one open document with its Writero cloud article.
///
/// Local storage stays authoritative: edits are appended to a durable
/// pending queue with the save that produced them, pushed in order, and
/// deleted only after the server acknowledges. Remote changes flow back
/// through the change feed and are applied without touching undo or marking
/// the document dirty; divergent blocks become retained conflicts.
class SyncEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Workspace *workspace READ workspace WRITE setWorkspace NOTIFY changed)
    Q_PROPERTY(AccountSession *account READ account WRITE setAccount NOTIFY changed)
    Q_PROPERTY(DocumentController *document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(QString cloudId READ cloudId NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QVariantList remoteContentVersions READ remoteContentVersions
                   NOTIFY remoteHistoryChanged)
    Q_PROPERTY(QVariantList remoteMediaVersions READ remoteMediaVersions
                   NOTIFY remoteHistoryChanged)

public:
    explicit SyncEngine(QObject *parent = nullptr);

    Workspace *workspace() const { return m_workspace; }
    void setWorkspace(Workspace *workspace);
    AccountSession *account() const { return m_account; }
    void setAccount(AccountSession *account);
    DocumentController *document() const { return m_document; }
    void setDocument(DocumentController *document);

    QString cloudId() const { return m_cloudId; }
    QString state() const { return m_state; }
    bool busy() const { return m_busy; }
    int pendingCount() const { return m_pendingCount; }
    int conflictCount() const { return m_conflictCount; }
    QString lastError() const { return m_lastError; }
    QVariantList remoteContentVersions() const { return m_remoteContentVersions; }
    QVariantList remoteMediaVersions() const { return m_remoteMediaVersions; }

    Q_INVOKABLE void connectDocument();
    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void disconnectDocument();
    Q_INVOKABLE void resolveConflict(const QString &conflictId, bool keepLocal);
    Q_INVOKABLE QVariantList conflictList() const;
    Q_INVOKABLE void requestShareLink();
    Q_INVOKABLE void loadRemoteHistory(int blockIndex);
    Q_INVOKABLE void restoreRemoteVersion(int blockIndex, qint64 versionId);
    Q_INVOKABLE void restoreRemoteMediaVersion(int blockIndex, qint64 attachmentId);
    Q_INVOKABLE void refreshCounts();

signals:
    void changed();
    void conflictsChanged();
    void cloudResultsChanged();
    void remoteHistoryChanged();
    void shareLinkReady(const QString &shareUrl);
    void shareLinkFailed(const QString &message);
    void notice(const QString &message);

private:
    enum class Stage { Idle, Creating, Pushing, Pulling, Snapshotting, Resolving };

    void setState(const QString &state);
    void setBusy(bool busy);
    void setError(const QString &error);
    void refreshSummary();

    void handleDocumentCreated(const QJsonObject &body);
    void pushNextBatch();
    void onMutationsApplied(const QJsonObject &body);
    void applyMutationResults(const QJsonObject &body);
    void handleConflict(int status, const QJsonObject &body);
    void pullChanges();
    void onChangesReceived(const QJsonObject &body);
    void resnapshot();
    void onSnapshotReceived(const QJsonObject &body);
    void applySnapshotLockVersions();
    void onRequestFailed(const QString &operation, int status, const QJsonObject &body,
                         const QString &message);

    ReconcileContext reconcileContext();
    QJsonArray buildBatch(const QVector<PendingOperation> &operations,
                          QVector<PendingOperation> *batched) const;
    bool enqueueOperation(const PendingOperation &operation);
    bool enqueueReconnectUploads();
    void finishSync();
    bool accountMatchesDocument(QString *message) const;

    Workspace *m_workspace = nullptr;
    AccountSession *m_account = nullptr;
    DocumentController *m_document = nullptr;
    CloudClient m_client;

    Stage m_stage = Stage::Idle;
    QString m_cloudId;
    QString m_state = QStringLiteral("local");
    QString m_lastError;
    bool m_busy = false;
    int m_pendingCount = 0;
    int m_conflictCount = 0;
    int m_snapshotPage = 1;
    int m_snapshotTotalPages = 1;
    BlockList m_snapshotBlocks;
    QHash<QString, int> m_snapshotLockVersions;
    QVector<PendingOperation> m_inFlightOperations;
    QVariantList m_remoteContentVersions;
    QVariantList m_remoteMediaVersions;
    QString m_historyRemoteBlockId;
};

} // namespace writero
