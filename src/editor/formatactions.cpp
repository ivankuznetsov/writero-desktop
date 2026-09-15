#include "editor/formatactions.h"

#include <QUrl>

namespace writero::formatactions {

namespace {

QString markersFor(const QString &style)
{
    if (style == QLatin1String("bold"))
        return QStringLiteral("**");
    if (style == QLatin1String("italic"))
        return QStringLiteral("*");
    if (style == QLatin1String("code"))
        return QStringLiteral("`");
    if (style == QLatin1String("strikethrough"))
        return QStringLiteral("~~");
    return {};
}

} // namespace

bool isWrappingStyle(const QString &style)
{
    return !markersFor(style).isEmpty();
}

Selection wrap(const QString &text, int start, int end, const QString &style)
{
    Selection result;

    QString markers = markersFor(style);
    if (markers.isEmpty()) {
        result.text = text;
        result.start = result.end = result.cursor = qBound(0, end, text.size());
        return result;
    }

    int from = qBound(0, qMin(start, end), text.size());
    int to = qBound(0, qMax(start, end), text.size());
    const QString selected = text.mid(from, to - from);

    QString padding;
    if (style == QLatin1String("code") && !selected.isEmpty()) {
        int longestRun = 0;
        int run = 0;
        for (QChar character : selected) {
            run = character == QLatin1Char('`') ? run + 1 : 0;
            longestRun = qMax(longestRun, run);
        }
        markers = QString(longestRun + 1, QLatin1Char('`'));
        // CommonMark strips one surrounding space pair from code spans.
        if (selected.startsWith(QLatin1Char('`')) || selected.endsWith(QLatin1Char('`'))
            || (selected.startsWith(QLatin1Char(' ')) && selected.endsWith(QLatin1Char(' '))
                && !selected.trimmed().isEmpty())) {
            padding = QStringLiteral(" ");
        }
    }
    result.text = text.left(from) + markers + padding + selected + padding + markers + text.mid(to);
    result.start = from + markers.size() + padding.size();
    result.end = result.start + selected.size();
    result.cursor = result.end;
    return result;
}

Selection link(const QString &text, int start, int end, const QString &url)
{
    Selection result;

    int from = qBound(0, qMin(start, end), text.size());
    int to = qBound(0, qMax(start, end), text.size());
    const QString selected = text.mid(from, to - from);
    QString label = selected.isEmpty() ? QStringLiteral("link") : selected;
    label.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    label.replace(QStringLiteral("["), QStringLiteral("\\["));
    label.replace(QStringLiteral("]"), QStringLiteral("\\]"));
    // Preserve URI delimiters and existing percent escapes, but encode the
    // characters that can terminate or invalidate a Markdown destination.
    const QString destination = QString::fromUtf8(
        QUrl::toPercentEncoding(url, QByteArray(":/?#[]@!$&'*+,;=%")));

    result.text = text.left(from) + QLatin1Char('[') + label + QStringLiteral("](") + destination
        + QLatin1Char(')') + text.mid(to);
    result.start = from + 1;
    result.end = result.start + label.size();
    result.cursor = result.end;
    return result;
}

} // namespace writero::formatactions
