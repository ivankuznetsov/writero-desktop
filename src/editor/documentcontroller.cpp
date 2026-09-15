#include "editor/documentcontroller.h"

#include "document/documentio.h"
#include "document/listcontent.h"
#include "editor/formatactions.h"
#include "markdown/markdown.h"

#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSet>
#include <QUrl>

namespace writero {

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
{
    m_blocks.setSession(&m_session);
    m_blocks.setMediaResolver(
        [this](qint64 mediaId) { return m_workspace ? m_workspace->mediaUrl(mediaId) : QString(); });
    connectSession();

    m_autosave.setSingleShot(true);
    m_autosave.setInterval(1200);
    connect(&m_autosave, &QTimer::timeout, this, [this] { saveIfDirty(); });
}

void DocumentController::setWorkspace(Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    m_workspace = workspace;
    emit workspaceChanged();
}

void DocumentController::load(const Document &document)
{
    m_session.load(document);
    emit loaded();
}

void DocumentController::createBlankDocument(const QString &title)
{
    m_autosave.stop();
    Document document;
    document.id = newId();
    document.title = title.isEmpty() ? QStringLiteral("Untitled") : title;
    document.blocks = {Block::create(BlockType::Text)};
    document.createdAt = QDateTime::currentDateTimeUtc();
    document.updatedAt = document.createdAt;
    m_saveError.clear();
    emit saveErrorChanged();
    load(document);
}

bool DocumentController::openDocument(const QString &documentId)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return false;

    if (m_session.id() == documentId && !m_session.isDirty())
        return true;

    if (m_session.isDirty() && !saveIfDirty())
        return false;

    QString error;
    const Document document = m_workspace->store()->loadDocument(documentId, &error);
    if (document.id.isEmpty()) {
        setSaveError(error);
        return false;
    }

    m_autosave.stop();
    m_saveError.clear();
    emit saveErrorChanged();
    load(document);
    return true;
}

QString DocumentController::createDocument(const QString &title)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return {};

    if (m_session.isDirty())
        saveIfDirty();

    const QString id = m_workspace->createDocument(title);
    if (id.isEmpty()) {
        setSaveError(m_workspace->lastError());
        return {};
    }
    openDocument(id);
    return id;
}

bool DocumentController::saveNow()
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return false;
    if (m_session.id().isEmpty())
        return false;
    if (!m_session.isDirty())
        return true;

    const QVector<DocumentChange> changes = m_session.journal();
    const QVector<PendingOperation> pending = pendingOperationsFor(changes);
    QString error;
    if (!m_workspace->store()->saveDocument(m_session.document(), changes, pending, &error)) {
        setSaveError(error);
        return false;
    }

    QSet<QString> touchedBlocks;
    for (const DocumentChange &change : changes) {
        if (!change.blockId.isEmpty())
            touchedBlocks.insert(change.blockId);
    }
    for (const QString &blockId : touchedBlocks)
        m_workspace->store()->pruneRevisions(m_session.id(), blockId, 50);

    m_session.clearJournal();
    m_session.markSaved();
    m_saveError.clear();
    emit saveErrorChanged();
    emit saved();
    return true;
}

/// Maps journaled document changes to durable cloud operations. Operation
/// ids are persisted with the save, so a crash between commit and
/// acknowledgement replays the same operation instead of duplicating it.
QVector<PendingOperation> DocumentController::pendingOperationsFor(
    const QVector<DocumentChange> &changes) const
{
    const Document &document = m_session.document();
    if (document.cloudId.isEmpty() || document.cloudState == QLatin1String("local")
        || document.cloudState == QLatin1String("conflict")) {
        return {};
    }

    const BlockList &blocks = document.blocks;
    const auto blockBefore = [this, &blocks](const QString &blockId) -> QString {
        const int index = m_session.document().indexOf(blockId);
        if (index <= 0)
            return {};
        return blocks.at(index - 1).id;
    };
    const auto enqueuePending = [](QVector<PendingOperation> &operations, const QString &kind,
                                   const QJsonObject &payload) {
        PendingOperation operation;
        operation.operationId = newId();
        operation.kind = kind;
        operation.payload = payload;
        operation.createdAt = QDateTime::currentDateTimeUtc();
        operations.append(operation);
    };
    const auto blockAttributes = [](const Block &block) {
        return QJsonObject{
            {QStringLiteral("content"), block.content},
            {QStringLiteral("block_type"), blocktype::toKey(block.type)},
            {QStringLiteral("metadata"), QJsonObject::fromVariantMap(block.metadata)},
        };
    };

    QVector<PendingOperation> operations;
    for (const DocumentChange &change : changes) {
        switch (change.kind) {
        case DocumentChange::Kind::Title:
            enqueuePending(operations, QStringLiteral("update_title"),
                           QJsonObject{{QStringLiteral("title"), change.afterTitle}});
            break;
        case DocumentChange::Kind::InsertBlock:
            enqueuePending(operations, QStringLiteral("create_block"),
                           QJsonObject{
                               {QStringLiteral("local_block_id"), change.afterBlock.id},
                               {QStringLiteral("after_local_block_id"),
                                blockBefore(change.afterBlock.id)},
                               {QStringLiteral("attributes"), blockAttributes(change.afterBlock)},
                           });
            if (change.afterBlock.type == BlockType::Media && change.afterBlock.mediaId > 0) {
                enqueuePending(operations, QStringLiteral("attach_media"),
                               QJsonObject{{QStringLiteral("local_block_id"),
                                            change.afterBlock.id}});
            }
            break;
        case DocumentChange::Kind::RemoveBlock:
            enqueuePending(operations, QStringLiteral("delete_block"),
                           QJsonObject{{QStringLiteral("local_block_id"), change.beforeBlock.id}});
            break;
        case DocumentChange::Kind::UpdateBlock:
            enqueuePending(operations, QStringLiteral("update_block"),
                           QJsonObject{
                               {QStringLiteral("local_block_id"), change.afterBlock.id},
                               {QStringLiteral("attributes"), blockAttributes(change.afterBlock)},
                           });
            if (change.afterBlock.type == BlockType::Media && change.afterBlock.mediaId > 0
                && change.beforeBlock.mediaId != change.afterBlock.mediaId) {
                enqueuePending(operations, QStringLiteral("attach_media"),
                               QJsonObject{{QStringLiteral("local_block_id"),
                                            change.afterBlock.id}});
            }
            break;
        case DocumentChange::Kind::MoveBlock:
            enqueuePending(operations, QStringLiteral("move_block"),
                           QJsonObject{
                               {QStringLiteral("local_block_id"), change.blockId},
                               {QStringLiteral("after_local_block_id"), blockBefore(change.blockId)},
                           });
            break;
        case DocumentChange::Kind::ReplaceAll:
            // Whole-document replacement during an active sync is not part of
            // the first sync protocol; the document should be reconnected or
            // the change applied through the browser.
            break;
        }
    }
    return operations;
}

bool DocumentController::saveIfDirty()
{
    if (!m_session.isDirty())
        return true;
    return saveNow();
}

bool DocumentController::trashCurrentDocument()
{
    if (m_workspace == nullptr || m_session.id().isEmpty())
        return false;
    if (!m_workspace->trashDocument(m_session.id()))
        return false;
    createBlankDocument();
    return true;
}

bool DocumentController::attachMedia(int index, const QString &source)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return false;
    if (index < 0 || index >= m_session.document().blocks.size())
        return false;

    const QUrl url(source);
    const QString path = url.isLocalFile() ? url.toLocalFile() : source;
    if (!QFileInfo::exists(path)) {
        setSaveError(QStringLiteral("Media file not found: %1").arg(path));
        return false;
    }

    const qint64 mediaId = m_workspace->importMedia(path);
    if (mediaId <= 0) {
        setSaveError(m_workspace->lastError());
        return false;
    }

    Block updated = m_session.document().blocks.at(index);
    const qint64 previousMedia = updated.mediaId;
    if (previousMedia > 0 && previousMedia != mediaId)
        m_workspace->store()->addMediaVersion(updated.id, previousMedia);
    updated.mediaId = mediaId;
    updated.type = BlockType::Media;
    updated.setMediaSource(QString());
    if (updated.mediaAlt().isEmpty())
        updated.setMediaAlt(QFileInfo(path).completeBaseName());
    return m_session.updateBlock(index, updated);
}

QString DocumentController::mediaUrl(qint64 mediaId) const
{
    return m_workspace ? m_workspace->mediaUrl(mediaId) : QString();
}

bool DocumentController::exportDocument(const QString &path, const QString &format)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return false;

    const QUrl url(path);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : path;
    const documentio::MediaAccess access = m_workspace->mediaAccess();
    QString error;
    bool ok = false;
    if (format == QLatin1String("md"))
        ok = documentio::exportMarkdown(m_session.document(), localPath, access, &error);
    else if (format == QLatin1String("html"))
        ok = documentio::exportHtml(m_session.document(), localPath, access, &error);
    else if (format == QLatin1String("pdf"))
        ok = documentio::exportPdf(m_session.document(), localPath, access, &error);
    else
        error = QStringLiteral("Unsupported export format: %1").arg(format);

    if (!ok)
        setSaveError(error);
    return ok;
}

QString DocumentController::exportBundle(const QString &directory)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return {};
    if (!saveIfDirty())
        return {};
    const QUrl url(directory);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : directory;
    QString error;
    const QString result = m_workspace->exportBundleTo(localPath, m_session.id(), &error);
    if (result.isEmpty())
        setSaveError(error);
    return result;
}

QString DocumentController::importBundle(const QString &directory)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return {};
    if (m_session.isDirty())
        saveIfDirty();

    const QUrl url(directory);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : directory;
    QString error;
    const QString id = m_workspace->importBundleFrom(localPath, &error);
    if (id.isEmpty()) {
        setSaveError(error);
        return {};
    }
    openDocument(id);
    return id;
}

bool DocumentController::importMarkdownFile(const QString &path)
{
    const QUrl url(path);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : path;
    QString error;
    const BlockList blocks = documentio::importMarkdownFile(localPath, &error);
    if (blocks.isEmpty()) {
        setSaveError(error.isEmpty() ? QStringLiteral("Nothing to import") : error);
        return false;
    }
    m_session.replaceAll(blocks, QStringLiteral("import"));
    ensureTrailingBlock();
    return true;
}

bool DocumentController::pasteMarkdown(int index, const QString &markdown)
{
    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return false;

    BlockList parsed = markdown::parse(markdown);
    if (parsed.isEmpty())
        return false;

    Block first = parsed.takeFirst();
    Block current = blocks.at(index);
    current.type = first.type;
    current.content = first.content;
    current.metadata = first.metadata;
    bool changed = false;
    m_session.editGroup([&] {
        changed = m_session.updateBlock(index, current, QStringLiteral("paste"));
        int at = index + 1;
        for (const Block &block : parsed)
            changed = m_session.insertBlock(at++, block, QStringLiteral("paste")) || changed;
    });
    return changed;
}

QString DocumentController::clipboardText() const
{
    return QGuiApplication::clipboard()->text();
}

void DocumentController::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool DocumentController::looksLikeMarkdown(const QString &text) const
{
    return markdown::looksLikeMarkdown(text);
}

QVariantList DocumentController::blockRevisions(int index) const
{
    QVariantList result;
    if (m_workspace == nullptr || !m_workspace->isReady())
        return result;

    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return result;

    const QVector<Revision> revisions =
        m_workspace->store()->revisions(m_session.id(), blocks.at(index).id, 100);
    for (const Revision &revision : revisions) {
        result.append(QVariantMap{
            {QStringLiteral("id"), revision.id},
            {QStringLiteral("event"), revision.event},
            {QStringLiteral("source"), revision.source},
            {QStringLiteral("content"), revision.content},
            {QStringLiteral("createdAt"), revision.createdAt},
        });
    }
    return result;
}

QVariantList DocumentController::blockMediaVersions(int index) const
{
    QVariantList result;
    if (m_workspace == nullptr || !m_workspace->isReady())
        return result;

    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return result;

    const QVector<qint64> versions =
        m_workspace->store()->mediaVersions(blocks.at(index).id);
    for (qint64 mediaId : versions) {
        const WorkspaceStore::MediaRecord record = m_workspace->store()->mediaRecord(mediaId);
        if (!record.isValid())
            continue;
        result.append(QVariantMap{
            {QStringLiteral("mediaId"), record.id},
            {QStringLiteral("filename"), record.filename},
            {QStringLiteral("mimeType"), record.mimeType},
            {QStringLiteral("url"), m_workspace->mediaUrl(record.id)},
        });
    }
    return result;
}

bool DocumentController::restoreRevision(int index, qint64 revisionId)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return false;

    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return false;

    const QVector<Revision> revisions =
        m_workspace->store()->revisions(m_session.id(), blocks.at(index).id, 500);
    for (const Revision &revision : revisions) {
        if (revision.id != revisionId)
            continue;
        Block updated = blocks.at(index);
        updated.type = revision.type;
        updated.content = revision.content;
        updated.metadata = revision.metadata;
        return m_session.updateBlock(index, updated, QStringLiteral("restore"));
    }
    return false;
}

bool DocumentController::restoreMediaVersion(int index, qint64 mediaId)
{
    if (m_workspace == nullptr || !m_workspace->isReady())
        return false;

    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return false;

    const WorkspaceStore::MediaRecord record = m_workspace->store()->mediaRecord(mediaId);
    if (!record.isValid())
        return false;

    Block updated = blocks.at(index);
    if (updated.mediaId == mediaId)
        return true;
    if (updated.mediaId > 0)
        m_workspace->store()->addMediaVersion(updated.id, updated.mediaId);
    updated.mediaId = mediaId;
    updated.type = BlockType::Media;
    return m_session.updateBlock(index, updated, QStringLiteral("restore"));
}

void DocumentController::ensureTrailingBlock()
{
    m_session.ensureTrailingEmptyBlock(QStringLiteral("import"));
}

void DocumentController::setTitle(const QString &title)
{
    m_session.setTitle(title, true);
}

void DocumentController::setBlockContent(int index, const QString &content, bool coalesce)
{
    m_session.updateContent(index, content, coalesce);
}

void DocumentController::setBlockType(int index, const QString &typeKey, const QString &headingLevel)
{
    bool ok = false;
    const BlockType type = blocktype::fromKey(typeKey, &ok);
    if (!ok)
        return;
    m_session.updateType(index, type, headingLevel);
}

void DocumentController::setBlockMetadataValue(int index, const QString &key, const QVariant &value)
{
    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return;

    QVariantMap metadata = blocks.at(index).metadata;
    if (value.isValid() && !value.toString().isEmpty())
        metadata.insert(key, value);
    else
        metadata.remove(key);
    m_session.updateMetadata(index, metadata);
}

int DocumentController::insertBlockAfter(int index)
{
    const int at = qBound(0, index + 1, m_session.document().blocks.size());
    m_session.insertBlock(at, Block::create(BlockType::Text));
    return at;
}

int DocumentController::appendBlock()
{
    return insertBlockAfter(m_session.document().blocks.size() - 1);
}

void DocumentController::removeBlock(int index)
{
    m_session.removeBlock(index);
}

void DocumentController::moveBlock(int from, int to)
{
    m_session.moveBlock(from, to);
}

int DocumentController::splitBlock(int index, int cursorPosition)
{
    if (!m_session.splitBlock(index, cursorPosition))
        return -1;
    return index + 1;
}

int DocumentController::mergeWithPrevious(int index)
{
    if (!m_session.mergeWithPrevious(index))
        return -1;
    return index - 1;
}

int DocumentController::handleListEnter(int index, int cursorPosition)
{
    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return -1;

    const Block block = blocks.at(index);
    if (!blocktype::isList(block.type))
        return -1;

    const QString content = block.content;
    if (content.trimmed().isEmpty()) {
        // Enter on an empty list block exits the list.
        m_session.updateType(index, BlockType::Text);
        return 0;
    }

    const int cursor = qBound(0, cursorPosition, content.size());
    const int lineStart = cursor > 0 ? content.lastIndexOf(QLatin1Char('\n'), cursor - 1) + 1 : 0;
    int lineEnd = content.indexOf(QLatin1Char('\n'), cursor);
    if (lineEnd < 0)
        lineEnd = content.size();
    const QString line = content.mid(lineStart, lineEnd - lineStart);
    const QString marker = listcontent::markerPrefix(line);

    if (marker.isEmpty()) {
        const QString previousMarker =
            listcontent::lastMarkerLine(content.left(qMax(0, lineStart - 1)));
        if (previousMarker.isEmpty())
            return -1;
        const QString continuation = listcontent::continuationMarker(previousMarker);
        QString updated = content;
        updated.insert(cursor, QStringLiteral("\n") + continuation);
        m_session.updateContent(index, updated);
        return cursor + 1 + continuation.size();
    }

    const QString itemText = line.mid(marker.size()).trimmed();
    if (itemText.isEmpty()) {
        // Enter on an empty item exits the list.
        QString updated = content;
        updated.remove(lineStart, marker.size());
        if (updated.trimmed().isEmpty()) {
            Block textBlock = block;
            textBlock.type = BlockType::Text;
            textBlock.content.clear();
            m_session.updateBlock(index, textBlock);
        } else
            m_session.updateContent(index, updated);
        return lineStart;
    }

    const QString continuation = listcontent::continuationMarker(line);
    // The marker belongs to the item; a cursor inside it must not split it.
    const int split = qMax(cursor, lineStart + marker.size());
    QString updated = content;
    updated.insert(split, QStringLiteral("\n") + continuation);
    m_session.updateContent(index, updated);
    return split + 1 + continuation.size();
}

int DocumentController::indentListItem(int index, int cursorPosition, bool outdent)
{
    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return -1;

    const Block block = blocks.at(index);
    if (!blocktype::isList(block.type))
        return -1;

    const QString content = block.content;
    const int cursor = qBound(0, cursorPosition, content.size());
    const int lineStart = cursor > 0 ? content.lastIndexOf(QLatin1Char('\n'), cursor - 1) + 1 : 0;
    int lineEnd = content.indexOf(QLatin1Char('\n'), cursor);
    if (lineEnd < 0)
        lineEnd = content.size();

    QString updated = content;
    int delta = 0;
    if (outdent) {
        int removed = 0;
        while (removed < 2 && lineStart + removed < content.size()
               && content.at(lineStart + removed) == QLatin1Char(' ')) {
            ++removed;
        }
        if (removed == 0 && lineStart < content.size()
            && content.at(lineStart) == QLatin1Char('\t')) {
            removed = 1;
        }
        if (removed == 0)
            return cursor;
        updated.remove(lineStart, removed);
        delta = -removed;
    } else {
        updated.insert(lineStart, QStringLiteral("  "));
        delta = 2;
    }

    if (!m_session.updateContent(index, updated))
        return cursor;
    Q_UNUSED(lineEnd);
    return qMax(lineStart, cursor + delta);
}

QVariantMap DocumentController::applyFormat(int index, int selectionStart, int selectionEnd,
                                            const QString &style)
{
    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size())
        return {};

    formatactions::Selection selection;
    if (style == QLatin1String("link"))
        selection = formatactions::link(blocks.at(index).content, selectionStart, selectionEnd,
                                        QStringLiteral("https://"));
    else
        selection = formatactions::wrap(blocks.at(index).content, selectionStart, selectionEnd, style);

    if (selection.text == blocks.at(index).content)
        return {};
    if (!m_session.updateContent(index, selection.text))
        return {};

    return {
        {QStringLiteral("text"), selection.text},
        {QStringLiteral("selectionStart"), selection.start},
        {QStringLiteral("selectionEnd"), selection.end},
        {QStringLiteral("cursor"), selection.cursor},
    };
}

QVariantMap DocumentController::applyLink(int index, int selectionStart, int selectionEnd,
                                          const QString &url)
{
    const auto &blocks = m_session.document().blocks;
    if (index < 0 || index >= blocks.size() || url.isEmpty())
        return {};

    const formatactions::Selection selection =
        formatactions::link(blocks.at(index).content, selectionStart, selectionEnd, url);
    if (!m_session.updateContent(index, selection.text))
        return {};

    return {
        {QStringLiteral("text"), selection.text},
        {QStringLiteral("selectionStart"), selection.start},
        {QStringLiteral("selectionEnd"), selection.end},
        {QStringLiteral("cursor"), selection.cursor},
    };
}

void DocumentController::undo()
{
    m_session.undo();
}

void DocumentController::redo()
{
    m_session.redo();
}

void DocumentController::connectSession()
{
    connect(&m_session, &DocumentSession::titleChanged, this, &DocumentController::titleChanged);
    connect(&m_session, &DocumentSession::dirtyChanged, this, &DocumentController::dirtyChanged);
    connect(&m_session, &DocumentSession::historyChanged, this, &DocumentController::historyChanged);
    connect(&m_session, &DocumentSession::titleChanged, this, &DocumentController::countsChanged);
    connect(&m_session, &DocumentSession::blockChanged, this, &DocumentController::countsChanged);
    connect(&m_session, &DocumentSession::blockInserted, this, &DocumentController::countsChanged);
    connect(&m_session, &DocumentSession::blockRemoved, this, &DocumentController::countsChanged);
    connect(&m_session, &DocumentSession::reset, this, &DocumentController::countsChanged);
    connect(&m_session, &DocumentSession::dirtyChanged, this, [this](bool dirty) {
        if (dirty)
            scheduleAutosave();
        else
            m_autosave.stop();
    });
}

void DocumentController::scheduleAutosave()
{
    if (m_workspace != nullptr && m_workspace->isReady())
        m_autosave.start();
}

void DocumentController::setSaveError(const QString &error)
{
    if (m_saveError == error)
        return;
    m_saveError = error;
    emit saveErrorChanged();
}

} // namespace writero
