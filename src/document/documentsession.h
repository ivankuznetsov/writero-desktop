#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

#include "document/document.h"
#include "document/documentchange.h"

namespace writero {

/// Owns one open document, applies every mutation through explicit commands,
/// and tracks undo/redo plus a journal of changes for storage.
///
/// UI code and storage never mutate `Document` directly. Structural signals
/// come in about-to/applied pairs so a QAbstractItemModel can wrap them with
/// begin/end row calls.
class DocumentSession : public QObject
{
    Q_OBJECT

public:
    explicit DocumentSession(QObject *parent = nullptr);

    void load(const Document &document);
    const Document &document() const { return m_document; }

    /// Updates cloud connection state without recording undo/history.
    void setCloudState(const QString &cloudId, const QString &cloudState, qint64 syncCursor,
                       qint64 feedGeneration, qint64 titleVersion,
                       const QString &accountEmail = QString());
    QString id() const { return m_document.id; }

    bool isDirty() const { return m_dirty; }
    void markClean();
    void markSaved();

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    /// Changes applied since the last `drainJournal()`.
    const QVector<DocumentChange> &journal() const { return m_journal; }
    QVector<DocumentChange> drainJournal();
    void clearJournal() { m_journal.clear(); }

    /// Mutations return true when the document actually changed.
    bool setTitle(const QString &title, bool coalesce = false);
    bool insertBlock(int index, const Block &block, const QString &source = QStringLiteral("local"));
    bool appendBlock(Block block, const QString &source = QStringLiteral("local"));
    bool removeBlock(int index, const QString &source = QStringLiteral("local"));
    bool updateContent(int index, const QString &content, bool coalesce = false,
                       const QString &source = QStringLiteral("local"));
    bool updateBlock(int index, const Block &block, const QString &source = QStringLiteral("local"),
                     bool coalesce = false);
    bool updateType(int index, BlockType type, const QString &headingLevel = QString(),
                    const QString &source = QStringLiteral("local"));
    bool updateMetadata(int index, const QVariantMap &metadata,
                        const QString &source = QStringLiteral("local"));
    bool moveBlock(int from, int to, const QString &source = QStringLiteral("local"));
    bool splitBlock(int index, int offset, const QString &source = QStringLiteral("local"));
    bool mergeWithPrevious(int index, const QString &source = QStringLiteral("local"));
    bool replaceAll(const BlockList &blocks, const QString &source = QStringLiteral("import"));

    /// Keeps at least one empty trailing text block, like the web editor.
    /// Skipped while undo/redo is applying a snapshot.
    void ensureTrailingEmptyBlock(const QString &source = QStringLiteral("local"));

    bool undo();
    bool redo();

    // --- Remote application (no journal, undo, or dirty marking) ---

    void applyRemoteTitle(const QString &title);
    void applyRemoteUpdate(const QString &blockId, const Block &block);
    void applyRemoteInsert(const Block &block, int index);
    void applyRemoteRemove(const QString &blockId);
    void applyRemoteMove(const QString &blockId, int toIndex);
    void applyRemoteReset(const BlockList &blocks, const QString &title,
                          const QString &titleVersionSource);

signals:
    void aboutToReset();
    void reset();
    void titleChanged(const QString &title);

    void aboutToInsertBlock(int index);
    void blockInserted(int index);
    void aboutToRemoveBlock(int index);
    void blockRemoved(int index);
    void aboutToMoveBlock(int from, int to);
    void blockMoved(int from, int to);
    void blockChanged(int index);

    void dirtyChanged(bool dirty);
    void historyChanged();
    void syncStateChanged();

private:
    static constexpr qint64 CoalesceWindowMs = 2500;

    void applyChange(const DocumentChange &change, bool forward);
    void applyInsert(int index, const Block &block);
    void applyRemove(int index);
    void applyMove(int from, int to);
    void applyUpdate(int index, const Block &block);
    void applyReplaceAll(const BlockList &blocks);
    void applyTitle(const QString &title);

    void pushChange(DocumentChange change, bool coalesce);
    void recordJournalOnly(const DocumentChange &change);
    bool canCoalesce(const DocumentChange &previous, const DocumentChange &next) const;
    DocumentChange invert(const DocumentChange &change) const;

    void setDirty(bool dirty);

    Document m_document;
    QVector<DocumentChange> m_undoStack;
    QVector<DocumentChange> m_redoStack;
    QVector<DocumentChange> m_journal;
    QElapsedTimer m_sinceLastChange;
    bool m_dirty = false;
    bool m_loading = false;
};

} // namespace writero
