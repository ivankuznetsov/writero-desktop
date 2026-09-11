#pragma once

#include <functional>

#include "document/block.h"

namespace writero::markdown {

/// Parses Markdown into atomic blocks the same way the web editor does:
/// headings `#`–`####` (h5/h6 degrade to text), fenced code with language,
/// atomic list blocks with nesting and lazy continuation, merged `>` quotes,
/// dividers, and blank-line separated text paragraphs.
BlockList parse(const QString &text);

/// True when the text contains structures worth splitting into typed blocks.
bool looksLikeMarkdown(const QString &text);

/// Serializes blocks to Markdown. `mediaSource` resolves a block's media to
/// the URL/path to reference; without a resolver, media blocks are skipped.
QString serialize(const BlockList &blocks,
                  const std::function<QString(const Block &)> &mediaSource = {});

} // namespace writero::markdown
