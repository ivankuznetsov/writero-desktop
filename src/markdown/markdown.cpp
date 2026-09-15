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
    static const QRegularExpression pattern(QStringLiteral("^[ \\t]*(`{3,}|~{3,})([^\\s]*)[ \\t]*$"));
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

QString unescapeMarkdown(QString value)
{
    static const QRegularExpression escape(QStringLiteral(R"(\\([\\`*{}\[\]()#+\-.!_<>]))"));
    return value.replace(escape, QStringLiteral("\\1"));
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
    QString openingFence;
    QStringList codeLines;

    const auto pushCurrent = [&]() {
        if (hasCurrent)
            blocks.append(current);
        hasCurrent = false;
    };

    for (int index = 0; index < lines.size(); ++index) {
        const QString &line = lines.at(index);
        const auto fence = codeFencePattern().match(line);

        if (inCode) {
            if (fence.hasMatch() && fence.captured(1).front() == openingFence.front()
                && fence.captured(1).size() >= openingFence.size()
                && fence.captured(2).isEmpty()) {
                current.content = codeLines.join(QLatin1Char('\n'));
                pushCurrent();
                inCode = false;
            } else {
                codeLines.append(line);
            }
            continue;
        }

        if (fence.hasMatch()) {
            pushCurrent();
            inCode = true;
            openingFence = fence.captured(1);
            codeLines.clear();
            current = Block::create(BlockType::Code);
            current.setLanguage(fence.captured(2));
            hasCurrent = true;
            continue;
        }

        static const QRegularExpression imagePattern(
            QStringLiteral(R"(^!\[((?:\\.|[^\]\\])*)\]\((?:<([^>\n]*)>|((?:\\.|[^\s])*)?)\)[ \t]*$)"));
        const auto image = imagePattern.match(line);
        if (image.hasMatch()) {
            pushCurrent();
            Block block = Block::create(BlockType::Media);
            block.setMediaAlt(unescapeMarkdown(image.captured(1)));
            block.setMediaSource(unescapeMarkdown(image.captured(2).isNull()
                                                     ? image.captured(3) : image.captured(2)));
            blocks.append(block);
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

    if (inCode)
        current.content = codeLines.join(QLatin1Char('\n'));
    pushCurrent();

    BlockList result;
    for (Block &block : blocks) {
        if (block.type != BlockType::Code)
            block.content = block.content.trimmed();
        if (block.content.isEmpty() && block.type != BlockType::Divider
            && block.type != BlockType::Code && block.type != BlockType::Media)
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
        QRegularExpression(QStringLiteral("^[ \\t]*(?:`{3,}|~{3,})"), QRegularExpression::MultilineOption),
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
        case BlockType::Code: {
            int length = 3;
            static const QRegularExpression runs(QStringLiteral("`+"));
            auto matches = runs.globalMatch(block.content);
            while (matches.hasNext())
                length = qMax(length, int(matches.next().capturedLength()) + 1);
            const QString fence(length, QLatin1Char('`'));
            parts << fence + block.language() + QLatin1Char('\n') + block.content
                + QLatin1Char('\n') + fence;
            break;
        }
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
            QString alt = block.mediaAlt().isEmpty() ? QStringLiteral("image")
                                                     : block.mediaAlt();
            alt.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
            alt.replace(QStringLiteral("["), QStringLiteral("\\["));
            alt.replace(QStringLiteral("]"), QStringLiteral("\\]"));
            if (source.contains(QRegularExpression(QStringLiteral("[\\s()<>]")))) {
                source.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
                source.replace(QStringLiteral("<"), QStringLiteral("%3C"));
                source.replace(QStringLiteral(">"), QStringLiteral("%3E"));
                source = QLatin1Char('<') + source + QLatin1Char('>');
            }
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
