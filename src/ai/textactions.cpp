#include "ai/textactions.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace writero::textactions {

namespace {

constexpr int ContextRadius = 2;
constexpr int ContextBlockLimit = 200;

QString truncate(const QString &text, int limit)
{
    const QString simplified = text.simplified();
    return simplified.size() > limit ? simplified.left(limit - 1) + QChar(0x2026) : simplified;
}

QString blockTypeInstruction(BlockType type)
{
    switch (type) {
    case BlockType::Heading:
        return QStringLiteral("The block is a heading. Return a concise heading only, "
                              "without markdown markers.");
    case BlockType::Text:
        return QStringLiteral("The block is a paragraph. Return only the paragraph text; "
                              "do not add headings or list markers.");
    case BlockType::Ul:
        return QStringLiteral("The block is a bullet list. Preserve the list structure and "
                              "use '- ' markers for items.");
    case BlockType::Ol:
        return QStringLiteral("The block is a numbered list. Preserve the list structure and "
                              "use '1.' style markers.");
    case BlockType::Quote:
        return QStringLiteral("The block is a quote. Return only the quote text, without "
                              "quote markers.");
    case BlockType::Code:
        return QStringLiteral("The block is code. Preserve indentation and return only code, "
                              "without fences.");
    case BlockType::Media:
    case BlockType::Divider:
        return QStringLiteral("The block has no editable text.");
    }
    return {};
}

QString articleContext(const Document &document, const Block &target)
{
    const int index = document.indexOf(target.id);
    if (index < 0)
        return {};

    QStringList parts;
    if (!document.title.isEmpty())
        parts << QStringLiteral("Article title: %1").arg(document.title);

    QStringList context;
    for (int i = qMax(0, index - ContextRadius); i <= qMin(document.blocks.size() - 1,
                                                           index + ContextRadius);
         ++i) {
        if (i == index)
            continue;
        const Block &block = document.blocks.at(i);
        if (block.content.trimmed().isEmpty())
            continue;
        context << QStringLiteral("[%1] %2")
                       .arg(blocktype::displayName(block.type), truncate(block.content,
                                                                        ContextBlockLimit));
    }
    if (!context.isEmpty()) {
        parts << QStringLiteral("Surrounding context (do not include in output):");
        parts << context;
    }
    return parts.join(QLatin1Char('\n'));
}

QString humanizePrompt()
{
    return QStringLiteral(
        "You are a careful human editor. Rewrite the text so it reads as if a person wrote it "
        "naturally. Remove formulaic AI patterns: inflated significance, marketing language, "
        "overused transitions, em-dash pileups, hedging, chatbot pleasantries, and generic "
        "conclusions. Vary sentence length, prefer active voice and concrete details, and keep "
        "the author's meaning, jargon, and markdown structure. Return only the rewritten text.");
}

QString operationSystemPrompt(Operation operation)
{
    switch (operation) {
    case Operation::Rewrite:
        return QStringLiteral("You are a writing assistant. Rewrite the following text block "
                              "according to the user's instructions. Return ONLY the rewritten "
                              "version of the single block, without surrounding blocks or "
                              "commentary.");
    case Operation::Humanize:
        return humanizePrompt()
            + QStringLiteral(" Return ONLY the rewritten block, without surrounding blocks or "
                             "commentary.");
    case Operation::Research:
        return QStringLiteral("You are a research assistant with web search. Produce flowing "
                              "prose with sparing headings, prefer short paragraphs and bullet "
                              "lists where they help, and cite sources for every claim.");
    case Operation::Polish:
        return QStringLiteral("You are a proofreader. Fix spelling, grammar, punctuation, and "
                              "typography. Preserve meaning, markdown, lists, and code exactly. "
                              "Return only the corrected text.");
    case Operation::ImageExplanation:
        return QStringLiteral("Describe the provided image in detail.");
    }
    return {};
}

} // namespace

bool isHumanizeRequest(const QString &instruction)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\b(humanize|more human|sound human|more natural|sound natural|"
                       "less ai|less robotic)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(instruction).hasMatch();
}

QVector<AiMessage> buildMessages(Operation operation, const Document &document, const Block &block,
                                 const QString &instruction)
{
    Operation effective = operation;
    if (operation == Operation::Rewrite && isHumanizeRequest(instruction))
        effective = Operation::Humanize;

    QString system = operationSystemPrompt(effective);
    const QString typeInstruction = blockTypeInstruction(block.type);
    if (!typeInstruction.isEmpty())
        system += QLatin1Char('\n') + typeInstruction;
    const QString context = articleContext(document, block);
    if (!context.isEmpty())
        system += QLatin1Char('\n') + context;

    QString user;
    switch (effective) {
    case Operation::Rewrite:
        user = QStringLiteral("Text block to rewrite:\n%1\n\nInstruction: %2\n\nRewritten block:")
                   .arg(block.content, instruction);
        break;
    case Operation::Humanize:
        user = QStringLiteral("Text to humanize:\n\n%1").arg(block.content);
        break;
    case Operation::Research:
        if (!block.content.trimmed().isEmpty())
            user = QStringLiteral("Text to research: %1").arg(block.content);
        else
            user = QStringLiteral("Topic to research: %1").arg(instruction);
        if (!instruction.isEmpty() && !block.content.trimmed().isEmpty())
            user += QStringLiteral("\n\nAdditional context: %1").arg(instruction);
        user += QStringLiteral("\n\nResearch results:");
        break;
    case Operation::Polish:
        user = block.content;
        break;
    case Operation::ImageExplanation:
        user = instruction.isEmpty() ? QStringLiteral("Describe this image in detail.")
                                     : instruction;
        break;
    }

    AiMessage systemMessage;
    systemMessage.role = QStringLiteral("system");
    systemMessage.content = system;

    AiMessage userMessage;
    userMessage.role = QStringLiteral("user");
    userMessage.content = user;

    return {systemMessage, userMessage};
}

QVector<AiMessage> buildBulkMessages(Operation operation, const Document &document,
                                     const QString &instruction)
{
    QString system = operationSystemPrompt(operation);
    if (operation == Operation::Polish) {
        system = QStringLiteral("You are a proofreader. Fix spelling, grammar, punctuation, and "
                                "typography in the list of blocks. Preserve meaning, markdown, "
                                "lists, and code exactly. Reply with a JSON array of objects "
                                "{\"id\": \"<block id>\", \"content\": \"<corrected text>\"} "
                                "in the same order, without commentary.");
    }

    QStringList items;
    for (const Block &block : document.blocks) {
        if (!blocktype::isRewritable(block.type) || block.content.trimmed().isEmpty())
            continue;
        items << QStringLiteral("{\"id\": \"%1\", \"content\": %2}")
                     .arg(block.id,
                          QString::fromUtf8(QJsonDocument(QJsonArray{block.content}).toJson(
                                               QJsonDocument::Compact))
                              .mid(1)
                              .chopped(1));
    }

    AiMessage systemMessage;
    systemMessage.role = QStringLiteral("system");
    systemMessage.content = system;

    AiMessage userMessage;
    userMessage.role = QStringLiteral("user");
    userMessage.content = QStringLiteral("Article title: %1\nBlocks:\n[%2]\nInstruction: %3")
                              .arg(document.title, items.join(QStringLiteral(",")), instruction);

    return {systemMessage, userMessage};
}

QStringList promptSuggestions()
{
    return {
        QStringLiteral("Humanize"),
        QStringLiteral("Make it more concise"),
        QStringLiteral("Fix grammar and spelling"),
        QStringLiteral("Improve clarity"),
    };
}

} // namespace writero::textactions
