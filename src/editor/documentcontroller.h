#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

#include "document/documentsession.h"
#include "editor/blocklistmodel.h"

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
    Q_PROPERTY(QString documentId READ documentId NOTIFY loaded)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY countsChanged)
    Q_PROPERTY(int characterCount READ characterCount NOTIFY countsChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    explicit DocumentController(QObject *parent = nullptr);

    DocumentSession &session() { return m_session; }
    const DocumentSession &session() const { return m_session; }
    BlockListModel *blocks() { return &m_blocks; }

    void load(const Document &document);

    QString documentId() const { return m_session.id(); }
    QString title() const { return m_session.document().title; }
    int wordCount() const { return m_session.document().wordCount(); }
    int characterCount() const { return m_session.document().characterCount(); }
    bool isDirty() const { return m_session.isDirty(); }
    bool canUndo() const { return m_session.canUndo(); }
    bool canRedo() const { return m_session.canRedo(); }

    Q_INVOKABLE void createBlankDocument(const QString &title = QString());
    Q_INVOKABLE void setTitle(const QString &title);

    Q_INVOKABLE void setBlockContent(int index, const QString &content, bool coalesce = false);
    Q_INVOKABLE void setBlockType(int index, const QString &typeKey, const QString &headingLevel = QString());
    Q_INVOKABLE void setBlockMetadataValue(int index, const QString &key, const QVariant &value);
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
    void titleChanged(const QString &title);
    void countsChanged();
    void dirtyChanged(bool dirty);
    void historyChanged();

private:
    void connectSession();

    DocumentSession m_session;
    BlockListModel m_blocks;
};

} // namespace writero
