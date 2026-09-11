#pragma once

#include <QString>

namespace writero::listcontent {

/// Normalize list block content the same way the web app does:
/// tabs count as four spaces, and a blank line is preserved only between
/// markers or before an indented continuation of the current item.
QString normalize(const QString &content);

/// True when the line starts with `-`, `*`, `+`, or `N.` followed by space.
bool isMarkerLine(const QString &line);

/// Marker prefix of a list line (`indentation + marker + spaces`), or empty.
QString markerPrefix(const QString &line);

/// Visual indentation width of a line, counting tabs as four spaces.
int indentationWidth(const QString &line);

/// The marker that continues `line`: bullets keep their character, ordered
/// lists increment their number.
QString continuationMarker(const QString &line);

/// Last non-empty line of `content` that starts a list item, if any.
QString lastMarkerLine(const QString &content);

} // namespace writero::listcontent
