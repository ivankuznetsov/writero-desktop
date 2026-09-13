#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QJsonObject>
#include <QTimer>
#include <QVariantMap>

#include "document/documentsession.h"
#include "editor/blocklistmodel.h"
#include "storage/workspace.h"

namespace writero {

/// QML-facing facade for one open document.
///
/// All editor gestures (typing, splitting, list continuation, formatting,
/// undo) go through this controller so the document session stays the single
/// source of truth. `blocks` is a list model over the same session.
class DocumentController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(BlockListModel *blocks READ blocks CONSTANT)
    Q_PROPERTY(Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged)
    Q_PROPERTY(QString documentId READ documentId NOTIFY loaded)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY countsChanged)
    Q_PROPERTY(int characterCount READ characterCount NOTIFY countsChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY saveErrorChanged)

public:
    explicit DocumentController(QObject *parent = nullptr);

    DocumentSession &session() { return m_session; }
    const DocumentSession &session() const { return m_session; }
    BlockListModel *blocks() { return &m_blocks; }

    void load(const Document &document);

    Workspace *workspace() const { return m_workspace; }
    void setWorkspace(Workspace *workspace);

    QString documentId() const { return m_session.id(); }
    QString title() const { return m_session.document().title; }
    int wordCount() const { return m_session.document().wordCount(); }
    int characterCount() const { return m_session.document().characterCount(); }
    bool isDirty() const { return m_session.isDirty(); }
    bool canUndo() const { return m_session.canUndo(); }
    bool canRedo() const { return m_session.canRedo(); }
    QString saveError() const { return m_saveError; }

    Q_INVOKABLE void createBlankDocument(const QString &title = QString());
    Q_INVOKABLE void setTitle(const QString &title);

    /// Loads a stored document. Any pending changes to the current document
    /// are saved first.
    Q_INVOKABLE bool openDocument(const QString &documentId);
    Q_INVOKABLE QString createDocument(const QString &title = QString());
    Q_INVOKABLE bool saveNow();
    Q_INVOKABLE bool saveIfDirty();
    Q_INVOKABLE bool trashCurrentDocument();

    Q_INVOKABLE void setBlockContent(int index, const QString &content, bool coalesce = false);
    Q_INVOKABLE void setBlockType(int index, const QString &typeKey, const QString &headingLevel = QString());
    Q_INVOKABLE void setBlockMetadataValue(int index, const QString &key, const QVariant &value);

    /// Copies a local file into the workspace media store and attaches it to
    /// the block, converting the block to a media block.
    Q_INVOKABLE bool attachMedia(int index, const QString &source);
    Q_INVOKABLE QString mediaUrl(qint64 mediaId) const;

    /// Import/export. `format` is `md`, `html`, or `pdf`.
    Q_INVOKABLE bool exportDocument(const QString &path, const QString &format);
    Q_INVOKABLE QString exportBundle(const QString &directory);
    Q_INVOKABLE QString importBundle(const QString &directory);
    Q_INVOKABLE bool importMarkdownFile(const QString &path);
    Q_INVOKABLE bool pasteMarkdown(int index, const QString &markdown);
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool looksLikeMarkdown(const QString &text) const;

    /// Block history: content revisions and media versions, newest first.
    Q_INVOKABLE QVariantList blockRevisions(int index) const;
    Q_INVOKABLE QVariantList blockMediaVersions(int index) const;
    Q_INVOKABLE bool restoreRevision(int index, qint64 revisionId);
    Q_INVOKABLE bool restoreMediaVersion(int index, qint64 mediaId);
    Q_INVOKABLE int insertBlockAfter(int index);
    Q_INVOKABLE int appendBlock();
    Q_INVOKABLE void removeBlock(int index);
    Q_INVOKABLE void moveBlock(int from, int to);

    /// Splits at the cursor and returns the new block's index.
    Q_INVOKABLE int splitBlock(int index, int cursorPosition);

    /// Merges the block into its predecessor; returns the merged block index.
    Q_INVOKABLE int mergeWithPrevious(int index);

    /// Handles Enter inside a list block. Returns the new cursor position, or
    /// -1 when the block is not a list.
    Q_INVOKABLE int handleListEnter(int index, int cursorPosition);

    /// Indents (Tab) or outdents (Shift+Tab) the list line at the cursor.
    /// Returns the new cursor position, or -1 when the block is not a list.
    Q_INVOKABLE int indentListItem(int index, int cursorPosition, bool outdent);

    /// Applies inline markdown formatting around a selection. Returns
    /// `{ text, selectionStart, selectionEnd, cursor }`.
    Q_INVOKABLE QVariantMap applyFormat(int index, int selectionStart, int selectionEnd,
                                        const QString &style);
    Q_INVOKABLE QVariantMap applyLink(int index, int selectionStart, int selectionEnd,
                                      const QString &url);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void loaded();
    void saved();
    void workspaceChanged();
    void saveErrorChanged();
    void titleChanged(const QString &title);
    void countsChanged();
    void dirtyChanged(bool dirty);
    void historyChanged();

private:
    void connectSession();
    void scheduleAutosave();
    void setSaveError(const QString &error);
    void ensureTrailingBlock();
    QVector<PendingOperation> pendingOperationsFor(const QVector<DocumentChange> &changes) const;

    DocumentSession m_session;
    BlockListModel m_blocks;
    Workspace *m_workspace = nullptr;
    QTimer m_autosave;
    QString m_saveError;
};

} // namespace writero

