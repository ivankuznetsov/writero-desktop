#include "markdown/markdown.h"

#include <QRegularExpression>

#include "document/listcontent.h"

namespace writero::markdown {

namespace {

const QRegularExpression &headingPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^(#{1,6})\\s+(.+)$"));
    return pattern;
}

const QRegularExpression &ulPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^([ \\t]*)([-*+])[ \\t]+(.*)$"));
    return pattern;
}

const QRegularExpression &olPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^([ \\t]*)(\\d+)\\.[ \\t]+(.*)$"));
    return pattern;
}

const QRegularExpression &codeFencePattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[ \\t]*```([^\\s`]*)[ \\t]*$"));
    return pattern;
}

const QRegularExpression &quotePattern()
{
    static const QRegularExpression pattern(QStringLiteral("^>\\s?(.*)$"));
    return pattern;
}

const QRegularExpression &dividerPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^[ \\t]*(?:---+|\\*\\*\\*+|___+)[ \\t]*$"));
    return pattern;
}

struct ListMatch
{
    bool matched = false;
    BlockType type = BlockType::Ul;
    int indent = 0;
};

ListMatch listMarker(const QString &line)
{
    ListMatch result;
    const auto ul = ulPattern().match(line);
    if (ul.hasMatch()) {
        result.matched = true;
        result.type = BlockType::Ul;
        result.indent = listcontent::indentationWidth(ul.captured(1));
        return result;
    }
    const auto ol = olPattern().match(line);
    if (ol.hasMatch()) {
        result.matched = true;
        result.type = BlockType::Ol;
        result.indent = listcontent::indentationWidth(ol.captured(1));
    }
    return result;
}

bool isStructuralBoundary(const QString &line, int rootIndent)
{
    if (listcontent::indentationWidth(line) > rootIndent)
        return false;
    return headingPattern().match(line).hasMatch() || codeFencePattern().match(line).hasMatch()
        || quotePattern().match(line).hasMatch() || dividerPattern().match(line).hasMatch();
}

QString consumeList(const QStringList &lines, int startIndex, int *nextIndex)
{
    const ListMatch root = listMarker(lines.at(startIndex));
    if (!root.matched) {
        *nextIndex = startIndex;
        return {};
    }

    QStringList listLines;
    QStringList pendingBlankLines;
    int index = startIndex;

    while (index < lines.size()) {
        const QString &line = lines.at(index);

        if (line.trimmed().isEmpty()) {
            pendingBlankLines.append(QString());
            ++index;
            continue;
        }

        const ListMatch marker = listMarker(line);
        if (marker.matched) {
            if (marker.indent <= root.indent && marker.type != root.type)
                break;
            listLines.append(pendingBlankLines);
            pendingBlankLines.clear();
            listLines.append(QString(line).remove(QRegularExpression(QStringLiteral("[ \\t]+$"))));
            ++index;
            continue;
        }

        if (!pendingBlankLines.isEmpty()) {
            if (listcontent::indentationWidth(line) <= root.indent)
                break;
            listLines.append(pendingBlankLines);
            pendingBlankLines.clear();
            listLines.append(QString(line).remove(QRegularExpression(QStringLiteral("[ \\t]+$"))));
            ++index;
            continue;
        }

        if (isStructuralBoundary(line, root.indent))
            break;

        listLines.append(QString(line).remove(QRegularExpression(QStringLiteral("[ \\t]+$"))));
        ++index;
    }

    *nextIndex = index;
    QString content = listLines.join(QLatin1Char('\n')).trimmed();
    if (root.type == BlockType::Ul || root.type == BlockType::Ol)
        content = listcontent::normalize(content);
    return content;
}

} // namespace

BlockList parse(const QString &text)
{
    BlockList blocks;
    if (text.isEmpty())
        return blocks;

    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = normalized.split(QLatin1Char('\n'));

    Block current;
    bool hasCurrent = false;
    bool inCode = false;

    const auto pushCurrent = [&]() {
        if (hasCurrent)
            blocks.append(current);
        hasCurrent = false;
    };

    for (int index = 0; index < lines.size(); ++index) {
        const QString &line = lines.at(index);
        const auto fence = codeFencePattern().match(line);

        if (fence.hasMatch()) {
            if (inCode) {
                pushCurrent();
                inCode = false;
            } else {
                pushCurrent();
                inCode = true;
                current = Block::create(BlockType::Code);
                current.setLanguage(fence.captured(1));
                hasCurrent = true;
            }
            continue;
        }

        if (inCode && hasCurrent) {
            current.content += (current.content.isEmpty() ? QString() : QStringLiteral("\n")) + line;
            continue;
        }

        if (listMarker(line).matched) {
            pushCurrent();
            int nextIndex = index + 1;
            const QString content = consumeList(lines, index, &nextIndex);
            Block block = Block::create(listMarker(line).type, content);
            blocks.append(block);
            index = nextIndex - 1;
            continue;
        }

        const auto heading = headingPattern().match(line);
        if (heading.hasMatch()) {
            pushCurrent();
            const int level = heading.captured(1).size();
            if (level <= 4) {
                Block block = Block::create(BlockType::Heading, heading.captured(2).trimmed());
                block.setHeadingLevel(QStringLiteral("h%1").arg(level));
                blocks.append(block);
            } else {
                blocks.append(Block::create(BlockType::Text, heading.captured(2).trimmed()));
            }
            continue;
        }

        const auto quote = quotePattern().match(line);
        if (quote.hasMatch()) {
            if (hasCurrent && current.type == BlockType::Quote) {
                current.content += QStringLiteral("\n") + quote.captured(1);
            } else {
                pushCurrent();
                current = Block::create(BlockType::Quote, quote.captured(1));
                hasCurrent = true;
            }
            continue;
        }

        if (dividerPattern().match(line).hasMatch()) {
            pushCurrent();
            blocks.append(Block::create(BlockType::Divider));
            continue;
        }

        if (line.trimmed().isEmpty()) {
            pushCurrent();
            continue;
        }

        if (hasCurrent && current.type == BlockType::Text) {
            current.content += QStringLiteral("\n") + line;
        } else {
            pushCurrent();
            current = Block::create(BlockType::Text, line);
            hasCurrent = true;
        }
    }

    pushCurrent();

    BlockList result;
    for (Block &block : blocks) {
        block.content = block.content.trimmed();
        if (block.content.isEmpty() && block.type != BlockType::Divider)
            continue;
        result.append(block);
    }
    return result;
}

bool looksLikeMarkdown(const QString &text)
{
    if (text.isEmpty())
        return false;

    static const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("^#{1,6}\\s+.+$"), QRegularExpression::MultilineOption),
        QRegularExpression(QStringLiteral("^[-*+]\\s+.+$"), QRegularExpression::MultilineOption),
        QRegularExpression(QStringLiteral("^\\d+\\.\\s+.+$"), QRegularExpression::MultilineOption),
        QRegularExpression(QStringLiteral("^```"), QRegularExpression::MultilineOption),
        QRegularExpression(QStringLiteral("^>\\s+.+$"), QRegularExpression::MultilineOption),
        QRegularExpression(QStringLiteral("^(?:---+|\\*\\*\\*+|___+)$"),
                           QRegularExpression::MultilineOption),
        QRegularExpression(QStringLiteral("\\*\\*.+\\*\\*")),
        QRegularExpression(QStringLiteral("\\[.+\\]\\(.+\\)")),
    };

    for (const QRegularExpression &pattern : patterns) {
        if (pattern.match(text).hasMatch())
            return true;
    }
    return false;
}

QString serialize(const BlockList &blocks, const std::function<QString(const Block &)> &mediaSource)
{
    QStringList parts;
    for (const Block &block : blocks) {
        switch (block.type) {
        case BlockType::Heading: {
            const QString level = blocktype::normalizeHeadingLevel(block.headingLevel());
            parts << QString(level.mid(1).toInt(), QLatin1Char('#')) + QLatin1Char(' ')
                    + block.content;
            break;
        }
        case BlockType::Code:
            parts << QStringLiteral("```%1\n%2\n```").arg(block.language(), block.content);
            break;
        case BlockType::Quote: {
            const QStringList lines = block.content.split(QLatin1Char('\n'));
            QStringList quoted;
            for (const QString &line : lines)
                quoted << QStringLiteral("> ") + QString(line).remove(QRegularExpression(
                            QStringLiteral("[ \\t]+$")));
            parts << quoted.join(QLatin1Char('\n'));
            break;
        }
        case BlockType::Ul:
        case BlockType::Ol:
            parts << block.content;
            break;
        case BlockType::Media: {
            QString source = block.mediaSource();
            if (source.isEmpty() && mediaSource)
                source = mediaSource(block);
            if (source.isEmpty())
                break;
            const QString alt = block.mediaAlt().isEmpty() ? QStringLiteral("image")
                                                           : block.mediaAlt();
            parts << QStringLiteral("![%1](%2)").arg(alt, source);
            break;
        }
        case BlockType::Divider:
            parts << QStringLiteral("---");
            break;
        case BlockType::Text:
            parts << block.content;
            break;
        }
        parts << QString();
    }

    while (!parts.isEmpty() && parts.constLast().isEmpty())
        parts.removeLast();
    return parts.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace writero::markdown
