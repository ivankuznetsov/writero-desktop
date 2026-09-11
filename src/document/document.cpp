#include "document/document.h"

#include <QRegularExpression>

namespace writero {

int Document::indexOf(const QString &blockId) const
{
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks.at(i).id == blockId)
            return i;
    }
    return -1;
}

const Block *Document::blockById(const QString &blockId) const
{
    const int index = indexOf(blockId);
    return index < 0 ? nullptr : &blocks.at(index);
}

Block *Document::blockById(const QString &blockId)
{
    const int index = indexOf(blockId);
    return index < 0 ? nullptr : &blocks[index];
}

bool Document::isEmpty() const
{
    for (const Block &block : blocks) {
        if (!block.content.isEmpty())
            return false;
    }
    return true;
}

int Document::wordCount() const
{
    static const QRegularExpression whitespace(QStringLiteral("\\S+"));
    int total = 0;
    for (const Block &block : blocks) {
        if (!blocktype::countsWords(block.type))
            continue;
        auto it = whitespace.globalMatch(block.content);
        while (it.hasNext()) {
            it.next();
            ++total;
        }
    }
    return total;
}

int Document::characterCount() const
{
    int total = 0;
    for (const Block &block : blocks)
        total += block.content.size();
    return total;
}

bool Document::hasTrailingEmptyTextBlock() const
{
    if (blocks.isEmpty())
        return false;
    const Block &last = blocks.last();
    return last.type == BlockType::Text && last.content.isEmpty();
}

void Document::ensureTrailingEmptyTextBlock()
{
    if (hasTrailingEmptyTextBlock())
        return;
    blocks.append(Block::create(BlockType::Text));
}

QString Document::textPreview(int maxLength) const
{
    for (const Block &block : blocks) {
        if (block.content.isEmpty() || !blocktype::isTextual(block.type))
            continue;
        QString preview = block.content.simplified();
        if (preview.size() > maxLength)
            preview = preview.left(maxLength - 1) + QChar(0x2026);
        return preview;
    }
    return {};
}

} // namespace writero
