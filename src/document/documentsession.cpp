#include "document/documentsession.h"

#include "document/listcontent.h"

#include <QDateTime>
#include <utility>

namespace writero {

DocumentSession::DocumentSession(QObject *parent)
    : QObject(parent)
{
}

void DocumentSession::load(const Document &document)
{
    m_loading = true;
    emit aboutToReset();
    m_document = document;
    m_undoStack.clear();
    m_redoStack.clear();
    m_journal.clear();
    m_sinceLastChange.invalidate();
    emit reset();
    m_loading = false;

    setDirty(false);
    emit titleChanged(m_document.title);
    emit historyChanged();
}

void DocumentSession::setCloudState(const QString &cloudId, const QString &cloudState,
                                    qint64 syncCursor, qint64 feedGeneration,
                                    qint64 titleVersion)
{
    m_document.cloudId = cloudId;
    m_document.cloudState = cloudState;
    m_document.syncCursor = syncCursor;
    m_document.feedGeneration = feedGeneration;
    m_document.syncTitleVersion = titleVersion;
    emit syncStateChanged();
}

void DocumentSession::markClean()
{
    setDirty(false);
}

void DocumentSession::markSaved()
{
    markClean();
    m_sinceLastChange.invalidate();
}

QVector<DocumentChange> DocumentSession::drainJournal()
{
    QVector<DocumentChange> changes = std::move(m_journal);
    m_journal.clear();
    return changes;
}

bool DocumentSession::setTitle(const QString &title, bool coalesce)
{
    if (m_document.title == title)
        return false;

    DocumentChange change;
    change.kind = DocumentChange::Kind::Title;
    change.beforeTitle = m_document.title;
    change.afterTitle = title;

    applyTitle(title);
    pushChange(change, coalesce);
    return true;
}

bool DocumentSession::insertBlock(int index, const Block &block, const QString &source)
{
    Block inserted = block;
    if (inserted.id.isEmpty())
        inserted.id = newId();

    const int at = qBound(0, index, m_document.blocks.size());
    DocumentChange change;
    change.kind = DocumentChange::Kind::InsertBlock;
    change.blockId = inserted.id;
    change.afterBlock = inserted;
    change.fromIndex = at;
    change.source = source;

    applyInsert(at, inserted);
    pushChange(change, false);
    ensureTrailingEmptyBlock(source);
    return true;
}

bool DocumentSession::appendBlock(Block block, const QString &source)
{
    return insertBlock(m_document.blocks.size(), std::move(block), source);
}

bool DocumentSession::removeBlock(int index, const QString &source)
{
    if (index < 0 || index >= m_document.blocks.size())
        return false;

    const Block removed = m_document.blocks.at(index);
    DocumentChange change;
    change.kind = DocumentChange::Kind::RemoveBlock;
    change.blockId = removed.id;
    change.beforeBlock = removed;
    change.fromIndex = index;
    change.source = source;

    applyRemove(index);
    pushChange(change, false);
    ensureTrailingEmptyBlock(source);
    return true;
}

bool DocumentSession::updateContent(int index, const QString &content, bool coalesce,
                                    const QString &source)
{
    if (index < 0 || index >= m_document.blocks.size())
        return false;

    Block updated = m_document.blocks.at(index);
    QString text = content;
    text.remove(QChar(0x200B));
    if (blocktype::isList(updated.type))
        text = listcontent::normalize(text);
    updated.content = text;
    return updateBlock(index, updated, source, coalesce);
}

bool DocumentSession::updateBlock(int index, const Block &block, const QString &source,
                                  bool coalesce)
{
    if (index < 0 || index >= m_document.blocks.size())
        return false;

    Block updated = block;
    if (blocktype::isList(updated.type))
        updated.content = listcontent::normalize(updated.content);

    const Block before = m_document.blocks.at(index);
    if (before == updated)
        return false;

    updated.id = before.id;
    updated.revision = before.revision + 1;

    DocumentChange change;
    change.kind = DocumentChange::Kind::UpdateBlock;
    change.blockId = updated.id;
    change.beforeBlock = before;
    change.afterBlock = updated;
    change.source = source;

    applyUpdate(index, updated);
    pushChange(change, coalesce);

    if (index == m_document.blocks.size() - 1)
        ensureTrailingEmptyBlock(source);
    return true;
}

bool DocumentSession::updateType(int index, BlockType type, const QString &headingLevel,
                                 const QString &source)
{
    if (index < 0 || index >= m_document.blocks.size())
        return false;

    Block updated = m_document.blocks.at(index);
    if (updated.type == type && (type != BlockType::Heading
                                 || updated.headingLevel() == blocktype::normalizeHeadingLevel(headingLevel)))
        return false;

    updated.type = type;
    if (type == BlockType::Heading)
        updated.setHeadingLevel(headingLevel.isEmpty() ? QStringLiteral("h2") : headingLevel);
    if (type == BlockType::Divider)
        updated.content.clear();
    return updateBlock(index, updated, source);
}

bool DocumentSession::updateMetadata(int index, const QVariantMap &metadata, const QString &source)
{
    if (index < 0 || index >= m_document.blocks.size())
        return false;

    Block updated = m_document.blocks.at(index);
    if (updated.metadata == metadata)
        return false;
    updated.metadata = metadata;
    return updateBlock(index, updated, source);
}

bool DocumentSession::moveBlock(int from, int to, const QString &source)
{
    const int count = m_document.blocks.size();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return false;

    const Block moved = m_document.blocks.at(from);
    DocumentChange change;
    change.kind = DocumentChange::Kind::MoveBlock;
    change.blockId = moved.id;
    change.fromIndex = from;
    change.toIndex = to;
    change.source = source;

    applyMove(from, to);
    pushChange(change, false);
    return true;
}

bool DocumentSession::splitBlock(int index, int offset, const QString &source)
{
    if (index < 0 || index >= m_document.blocks.size())
        return false;

    const Block original = m_document.blocks.at(index);
    const int position = qBound(0, offset, original.content.size());

    Block first = original;
    first.content = original.content.left(position).trimmed();

    Block second = original;
    second.id = newId();
    second.content = original.content.mid(position).trimmed();
    second.revision = 1;

    if (blocktype::isList(original.type) && !second.content.isEmpty()
        && !listcontent::isMarkerLine(second.content.section(QLatin1Char('\n'), 0, 0))) {
        const QString marker = listcontent::continuationMarker(listcontent::lastMarkerLine(first.content));
        if (!marker.isEmpty())
            second.content = marker + second.content;
    }

    const bool changedFirst = first.content != original.content;
    if (changedFirst)
        updateBlock(index, first, source);
    insertBlock(index + 1, second, source);
    return true;
}

bool DocumentSession::mergeWithPrevious(int index, const QString &source)
{
    if (index <= 0 || index >= m_document.blocks.size())
        return false;

    const Block previous = m_document.blocks.at(index - 1);
    const Block current = m_document.blocks.at(index);

    Block merged = previous;
    if (previous.content.isEmpty()) {
        merged.type = current.type;
        merged.metadata = current.metadata;
        merged.content = current.content;
    } else if (!current.content.isEmpty()) {
        merged.content = previous.content + QLatin1Char('\n') + current.content;
    }

    updateBlock(index - 1, merged, source);
    removeBlock(index, source);
    return true;
}

bool DocumentSession::replaceAll(const BlockList &blocks, const QString &source)
{
    DocumentChange change;
    change.kind = DocumentChange::Kind::ReplaceAll;
    change.beforeBlocks = m_document.blocks;
    change.afterBlocks = blocks;
    change.source = source;

    applyReplaceAll(blocks);
    pushChange(change, false);
    return true;
}

void DocumentSession::applyRemoteTitle(const QString &title)
{
    m_loading = true;
    applyTitle(title);
    m_loading = false;
}

void DocumentSession::applyRemoteUpdate(const QString &blockId, const Block &block)
{
    const int index = m_document.indexOf(blockId);
    if (index < 0)
        return;
    Block updated = block;
    updated.id = blockId;
    updated.revision = m_document.blocks.at(index).revision + 1;
    m_loading = true;
    applyUpdate(index, updated);
    m_loading = false;
}

void DocumentSession::applyRemoteInsert(const Block &block, int index)
{
    m_loading = true;
    applyInsert(qBound(0, index, m_document.blocks.size()), block);
    m_loading = false;
}

void DocumentSession::applyRemoteRemove(const QString &blockId)
{
    const int index = m_document.indexOf(blockId);
    if (index < 0)
        return;
    m_loading = true;
    applyRemove(index);
    m_loading = false;
}

void DocumentSession::applyRemoteMove(const QString &blockId, int toIndex)
{
    const int index = m_document.indexOf(blockId);
    if (index < 0 || index == toIndex || toIndex < 0 || toIndex >= m_document.blocks.size())
        return;
    m_loading = true;
    applyMove(index, toIndex);
    m_loading = false;
}

void DocumentSession::applyRemoteReset(const BlockList &blocks, const QString &title,
                                       const QString &titleVersionSource)
{
    Q_UNUSED(titleVersionSource);
    m_loading = true;
    if (m_document.title != title)
        applyTitle(title);
    applyReplaceAll(blocks);
    m_loading = false;
}

bool DocumentSession::undo()
{
    if (m_undoStack.isEmpty())
        return false;

    DocumentChange change = m_undoStack.takeLast();
    m_loading = true;
    applyChange(change, false);
    m_loading = false;
    m_redoStack.append(change);

    DocumentChange journalEntry = invert(change);
    journalEntry.source = QStringLiteral("undo");
    journalEntry.at = QDateTime::currentDateTimeUtc();
    m_journal.append(journalEntry);

    m_sinceLastChange.invalidate();
    emit historyChanged();
    setDirty(true);
    return true;
}

bool DocumentSession::redo()
{
    if (m_redoStack.isEmpty())
        return false;

    DocumentChange change = m_redoStack.takeLast();
    m_loading = true;
    applyChange(change, true);
    m_loading = false;
    m_undoStack.append(change);

    DocumentChange journalEntry = change;
    journalEntry.source = QStringLiteral("redo");
    journalEntry.at = QDateTime::currentDateTimeUtc();
    m_journal.append(journalEntry);

    m_sinceLastChange.invalidate();
    emit historyChanged();
    setDirty(true);
    return true;
}

void DocumentSession::ensureTrailingEmptyBlock(const QString &source)
{
    if (m_loading || m_document.hasTrailingEmptyTextBlock())
        return;

    Block block = Block::create(BlockType::Text);
    const int index = m_document.blocks.size();

    DocumentChange change;
    change.kind = DocumentChange::Kind::InsertBlock;
    change.blockId = block.id;
    change.afterBlock = block;
    change.fromIndex = index;
    change.source = source;

    applyInsert(index, block);
    recordJournalOnly(change);
}

void DocumentSession::applyChange(const DocumentChange &change, bool forward)
{
    switch (change.kind) {
    case DocumentChange::Kind::Title:
        applyTitle(forward ? change.afterTitle : change.beforeTitle);
        break;
    case DocumentChange::Kind::InsertBlock:
        if (forward) {
            applyInsert(change.fromIndex, change.afterBlock);
        } else {
            const int index = m_document.indexOf(change.blockId);
            if (index >= 0)
                applyRemove(index);
        }
        break;
    case DocumentChange::Kind::RemoveBlock:
        if (forward) {
            const int index = m_document.indexOf(change.blockId);
            if (index >= 0)
                applyRemove(index);
        } else {
            applyInsert(change.fromIndex, change.beforeBlock);
        }
        break;
    case DocumentChange::Kind::UpdateBlock: {
        const int index = m_document.indexOf(change.blockId);
        if (index >= 0)
            applyUpdate(index, forward ? change.afterBlock : change.beforeBlock);
        break;
    }
    case DocumentChange::Kind::MoveBlock: {
        const int index = m_document.indexOf(change.blockId);
        if (index >= 0)
            applyMove(index, forward ? change.toIndex : change.fromIndex);
        break;
    }
    case DocumentChange::Kind::ReplaceAll:
        applyReplaceAll(forward ? change.afterBlocks : change.beforeBlocks);
        break;
    }
}

void DocumentSession::applyInsert(int index, const Block &block)
{
    emit aboutToInsertBlock(index);
    m_document.blocks.insert(index, block);
    emit blockInserted(index);
}

void DocumentSession::applyRemove(int index)
{
    emit aboutToRemoveBlock(index);
    m_document.blocks.removeAt(index);
    emit blockRemoved(index);
}

void DocumentSession::applyMove(int from, int to)
{
    emit aboutToMoveBlock(from, to);
    const Block block = m_document.blocks.takeAt(from);
    m_document.blocks.insert(to, block);
    emit blockMoved(from, to);
}

void DocumentSession::applyUpdate(int index, const Block &block)
{
    m_document.blocks[index] = block;
    emit blockChanged(index);
}

void DocumentSession::applyReplaceAll(const BlockList &blocks)
{
    emit aboutToReset();
    m_document.blocks = blocks;
    emit reset();
}

void DocumentSession::applyTitle(const QString &title)
{
    m_document.title = title;
    emit titleChanged(title);
}

void DocumentSession::pushChange(DocumentChange change, bool coalesce)
{
    change.at = QDateTime::currentDateTimeUtc();

    const bool merge = coalesce && !m_undoStack.isEmpty() && m_sinceLastChange.isValid()
        && m_sinceLastChange.elapsed() <= CoalesceWindowMs
        && canCoalesce(m_undoStack.last(), change);

    if (merge) {
        DocumentChange &top = m_undoStack.last();
        top.afterBlock = change.afterBlock;
        top.afterTitle = change.afterTitle;
        top.at = change.at;

        if (!m_journal.isEmpty() && canCoalesce(m_journal.last(), change))
            m_journal.last() = top;
        else
            m_journal.append(top);
    } else {
        m_redoStack.clear();
        m_undoStack.append(change);
        m_journal.append(change);
    }

    m_sinceLastChange.restart();
    emit historyChanged();
    setDirty(true);
}

void DocumentSession::recordJournalOnly(const DocumentChange &change)
{
    DocumentChange entry = change;
    entry.at = QDateTime::currentDateTimeUtc();
    m_journal.append(entry);
    setDirty(true);
}

bool DocumentSession::canCoalesce(const DocumentChange &previous, const DocumentChange &next) const
{
    if (previous.kind != next.kind || previous.blockId != next.blockId)
        return false;
    return previous.kind == DocumentChange::Kind::Title
        || previous.kind == DocumentChange::Kind::UpdateBlock;
}

DocumentChange DocumentSession::invert(const DocumentChange &change) const
{
    DocumentChange inverse = change;
    switch (change.kind) {
    case DocumentChange::Kind::Title:
        inverse.beforeTitle = change.afterTitle;
        inverse.afterTitle = change.beforeTitle;
        break;
    case DocumentChange::Kind::InsertBlock:
        inverse.kind = DocumentChange::Kind::RemoveBlock;
        inverse.beforeBlock = change.afterBlock;
        inverse.afterBlock = {};
        break;
    case DocumentChange::Kind::RemoveBlock:
        inverse.kind = DocumentChange::Kind::InsertBlock;
        inverse.afterBlock = change.beforeBlock;
        inverse.beforeBlock = {};
        break;
    case DocumentChange::Kind::UpdateBlock:
        inverse.beforeBlock = change.afterBlock;
        inverse.afterBlock = change.beforeBlock;
        break;
    case DocumentChange::Kind::MoveBlock:
        std::swap(inverse.fromIndex, inverse.toIndex);
        break;
    case DocumentChange::Kind::ReplaceAll:
        inverse.beforeBlocks = change.afterBlocks;
        inverse.afterBlocks = change.beforeBlocks;
        break;
    }
    return inverse;
}

void DocumentSession::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged(dirty);
}

} // namespace writero
