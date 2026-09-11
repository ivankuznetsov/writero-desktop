#include "editor/documentcontroller.h"

#include "document/listcontent.h"
#include "editor/formatactions.h"

#include <QDateTime>

namespace writero {

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
{
    m_blocks.setSession(&m_session);
    connectSession();
}

void DocumentController::load(const Document &document)
{
    m_session.load(document);
    emit loaded();
}

void DocumentController::createBlankDocument(const QString &title)
{
    Document document;
    document.id = newId();
    document.title = title.isEmpty() ? QStringLiteral("Untitled") : title;
    document.blocks = {Block::create(BlockType::Text)};
    document.createdAt = QDateTime::currentDateTimeUtc();
    document.updatedAt = document.createdAt;
    load(document);
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
}

} // namespace writero
