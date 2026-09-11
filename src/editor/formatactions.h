#pragma once

#include <QString>

namespace writero::formatactions {

/// Result of applying inline formatting to a text field.
struct Selection
{
    QString text;
    int start = 0;
    int end = 0;
    int cursor = 0;
};

/// Wraps `[start, end)` in the markers for `style`:
/// `bold` → `**`, `italic` → `*`, `code` → `` ` ``, `strikethrough` → `~~`.
/// With an empty selection the markers are inserted with the cursor between
/// them, mirroring the web editor's toolbar and keyboard shortcuts.
Selection wrap(const QString &text, int start, int end, const QString &style);

/// Builds `[selected](url)`, or `[link](url)` with an empty selection.
Selection link(const QString &text, int start, int end, const QString &url);

/// True when `style` is one of the wrapping styles handled by `wrap`.
bool isWrappingStyle(const QString &style);

} // namespace writero::formatactions
