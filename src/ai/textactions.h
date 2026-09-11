#pragma once

#include <QString>
#include <QVector>

#include "ai/aiclient.h"
#include "document/document.h"

namespace writero::textactions {

enum class Operation {
    Rewrite,
    Humanize,
    Research,
    Polish,
    ImageExplanation,
};

/// Assembles chat messages for a text operation, mirroring the web app's
/// context rules: article title plus two surrounding blocks, truncated.
QVector<AiMessage> buildMessages(Operation operation, const Document &document,
                                 const Block &block, const QString &instruction);

/// Same as `buildMessages` but for a whole-document bulk operation.
QVector<AiMessage> buildBulkMessages(Operation operation, const Document &document,
                                     const QString &instruction);

bool isHumanizeRequest(const QString &instruction);

/// Prompts suggested in the rewrite panel (the pinned humanize first).
QStringList promptSuggestions();

} // namespace writero::textactions
