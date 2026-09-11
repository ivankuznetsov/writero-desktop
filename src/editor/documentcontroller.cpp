#include "editor/documentcontroller.h"

#include "document/listcontent.h"
#include "editor/formatactions.h"

#include <QDateTime>
#include <QFileInfo>
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
    QString error;
    if (!m_workspace->store()->saveDocument(m_session.document(), changes, &error)) {
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
        if (updated.trimmed().isEmpty())
            m_session.updateType(index, BlockType::Text);
        else
            m_session.updateContent(index, updated);
        return lineStart;
    }

    const QString continuation = listcontent::continuationMarker(line);
    QString updated = content;
    updated.insert(cursor, QStringLiteral("\n") + continuation);
    m_session.updateContent(index, updated);
    return cursor + 1 + continuation.size();
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
    return cursor + delta;
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
