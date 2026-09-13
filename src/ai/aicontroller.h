#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>

#include "ai/providerregistry.h"
#include "ai/providers/WriteroProvider.h"
#include "cloud/accountsession.h"
#include "ai/textactions.h"
#include "editor/documentcontroller.h"

namespace writero {

/// Runs AI operations for the open document and manages their results.
///
/// One operation model covers individual rewrites, multi-model alternatives,
/// research, image generation and explanation, and document-wide bulk work.
/// Results are persisted before dispatch so an interrupted job is never
/// silently repeated; application is version-checked against the block.
class AiController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Workspace *workspace READ workspace WRITE setWorkspace NOTIFY changed)
    Q_PROPERTY(ProviderRegistry *providers READ providers WRITE setProviders NOTIFY changed)
    Q_PROPERTY(DocumentController *document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(AccountSession *account READ account WRITE setAccount NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int currentBlock READ currentBlock NOTIFY currentBlockChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingChanged)

public:
    explicit AiController(QObject *parent = nullptr);

    Workspace *workspace() const { return m_workspace; }
    void setWorkspace(Workspace *workspace);
    ProviderRegistry *providers() const { return m_providers; }
    void setProviders(ProviderRegistry *providers);
    DocumentController *document() const { return m_document; }
    void setDocument(DocumentController *document);
    AccountSession *account() const { return m_account; }
    void setAccount(AccountSession *account);

    QVariantList results() const { return m_results; }
    int currentBlock() const { return m_currentBlock; }
    bool busy() const { return m_busy; }
    QString streamingText() const { return m_streamingText; }

    Q_INVOKABLE void setCurrentBlock(int index);
    Q_INVOKABLE void refreshResults();

    /// `models` is a comma-separated list; each model produces one alternative.
    Q_INVOKABLE QString runRewrite(int index, const QString &providerId, const QString &models,
                                   const QString &prompt);
    Q_INVOKABLE QString runImageGeneration(int index, const QString &providerId,
                                           const QString &model, const QString &prompt,
                                           bool useCurrentAsReference = false);
    Q_INVOKABLE QString runImageExplanation(int index, const QString &providerId,
                                            const QString &model, const QString &prompt);

    Q_INVOKABLE bool applyResult(const QString &resultId, bool insertBelow);
    Q_INVOKABLE void dismissResult(const QString &resultId);
    Q_INVOKABLE QStringList suggestions() const;

    /// Bulk operations apply automatically to unchanged blocks; stale blocks
    /// are left for review.
    Q_INVOKABLE void runPolish(const QString &providerId, const QString &model);
    Q_INVOKABLE void runBulkRewrite(const QString &providerId, const QString &model,
                                    const QString &prompt);

signals:
    void changed();
    void resultsChanged();
    void currentBlockChanged();
    void busyChanged();
    void streamingChanged();
    void notice(const QString &message);

private:
    WorkspaceStore::AiResultRecord createResult(const QString &blockId, const QString &kind,
                                                const QString &providerId, const QString &model,
                                                const QString &prompt, const QString &batchId = {},
                                                const QString &operationId = {});
    QString runHostedRewrite(int index, const QString &blockId, const QStringList &models,
                             const QString &prompt);
    QString hostedUnsupported(int index, const QString &kind, const QString &providerId,
                              const QString &model, const QString &prompt);
    static QStringList surroundingText(const Document &document, const Block &block);
    AiClient *makeClient(const QString &providerId, const QString &model);
    void executeChat(const WorkspaceStore::AiResultRecord &record,
                     textactions::Operation operation, const QString &instruction);
    void settleResult(const QString &resultId, const QString &status, const QString &content,
                      const QString &error);
    void operationStarted();
    void operationFinished();
    void appendStreaming(const QString &delta);
    void runBulk(const QString &providerId, const QString &model, textactions::Operation operation,
                 const QString &instruction);
    const Block *blockById(const QString &blockId) const;

    Workspace *m_workspace = nullptr;
    ProviderRegistry *m_providers = nullptr;
    DocumentController *m_document = nullptr;
    AccountSession *m_account = nullptr;
    WriteroProvider *m_hosted = nullptr;
    QHash<QString, QString> m_hostedResultIds;
    QVariantList m_results;
    QVector<WorkspaceStore::AiResultRecord> m_resultRecords;
    int m_currentBlock = -1;
    bool m_busy = false;
    int m_activeOperations = 0;
    QString m_streamingText;
};

} // namespace writero
