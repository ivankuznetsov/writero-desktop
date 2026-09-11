#include "editor/formatactions.h"

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

    const QString markers = markersFor(style);
    if (markers.isEmpty()) {
        result.text = text;
        result.start = result.end = result.cursor = qBound(0, end, text.size());
        return result;
    }

    int from = qBound(0, qMin(start, end), text.size());
    int to = qBound(0, qMax(start, end), text.size());
    const QString selected = text.mid(from, to - from);

    result.text = text.left(from) + markers + selected + markers + text.mid(to);
    result.start = from + markers.size();
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
    const QString label = selected.isEmpty() ? QStringLiteral("link") : selected;

    result.text = text.left(from) + QLatin1Char('[') + label + QStringLiteral("](") + url
        + QLatin1Char(')') + text.mid(to);
    result.start = from + 1;
    result.end = result.start + label.size();
    result.cursor = result.end;
    return result;
}

} // namespace writero::formatactions
