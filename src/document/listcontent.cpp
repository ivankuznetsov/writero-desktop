#include "document/listcontent.h"

#include <QRegularExpression>
#include <QStringList>

namespace writero::listcontent {

namespace {

const QRegularExpression &markerPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^[ \\t]*(?:[-*+]|\\d+\\.)(?:[ \\t]+|$)"));
    return pattern;
}

QString visualizeTabs(QString value)
{
    return value.replace(QLatin1Char('\t'), QStringLiteral("    "));
}

} // namespace

QString normalize(const QString &content)
{
    const QStringList lines = content.split(QLatin1Char('\n'));
    QStringList normalized;
    bool pendingBlankLine = false;
    int contentIndentation = -1;

    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            pendingBlankLine = true;
            continue;
        }

        const auto marker = markerPattern().match(line);
        const bool hasMarker = marker.hasMatch();
        const bool preservesBlankLine =
            hasMarker || (contentIndentation >= 0 && indentationWidth(line) >= contentIndentation);

        if (pendingBlankLine && !normalized.isEmpty() && preservesBlankLine)
            normalized.append(QString());
        normalized.append(line);
        if (hasMarker)
            contentIndentation = visualizeTabs(marker.captured(0)).size();
        pendingBlankLine = false;
    }

    return normalized.join(QLatin1Char('\n'));
}

bool isMarkerLine(const QString &line)
{
    return markerPattern().match(line).hasMatch();
}

QString markerPrefix(const QString &line)
{
    const auto match = markerPattern().match(line);
    return match.hasMatch() ? match.captured(0) : QString();
}

int indentationWidth(const QString &line)
{
    const auto match = QRegularExpression(QStringLiteral("^[ \\t]*")).match(line);
    return visualizeTabs(match.captured(0)).size();
}

QString continuationMarker(const QString &line)
{
    const auto match = markerPattern().match(line);
    if (!match.hasMatch())
        return {};

    const QString prefix = match.captured(0);
    static const QRegularExpression leadingWhitespace(QStringLiteral("^[ \\t]*"));
    const QString indent = leadingWhitespace.match(prefix).captured(0);
    const QString marker = prefix.mid(indent.size()).trimmed();

    static const QRegularExpression ordered(QStringLiteral("^(\\d+)\\.$"));
    const auto orderedMatch = ordered.match(marker);
    if (orderedMatch.hasMatch()) {
        // Markers are user text, not machine-sized integers. Increment their
        // decimal representation so long list numbers cannot wrap or reset.
        QString number = orderedMatch.captured(1);
        while (number.size() > 1 && number.front() == QLatin1Char('0'))
            number.remove(0, 1);
        qsizetype digit = number.size() - 1;
        while (digit >= 0 && number.at(digit) == QLatin1Char('9')) {
            number[digit] = QLatin1Char('0');
            --digit;
        }
        if (digit < 0)
            number.prepend(QLatin1Char('1'));
        else
            number[digit] = QChar(number.at(digit).unicode() + 1);
        return indent + number + QStringLiteral(". ");
    }
    return indent + marker + QStringLiteral(" ");
}

QString lastMarkerLine(const QString &content)
{
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (!it->trimmed().isEmpty() && isMarkerLine(*it))
            return *it;
    }
    return {};
}

} // namespace writero::listcontent
