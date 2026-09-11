#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace writero {

/// The atomic content units of a Writero document. The string keys match the
/// wire format used by the Writero service and the local SQLite workspace.
enum class BlockType {
    Text,
    Heading,
    Ul,
    Ol,
    Code,
    Quote,
    Media,
    Divider,
};

namespace blocktype {

QString toKey(BlockType type);
BlockType fromKey(const QString &key, bool *ok = nullptr);

/// Text-like blocks hold editable content. Media and dividers do not.
bool isTextual(BlockType type);

/// List blocks store their markers inside the content string.
bool isList(BlockType type);

/// Block types the web app offers to text AI actions (`Block::REWRITABLE_TYPES`).
bool isRewritable(BlockType type);

/// Block types included in the word count (`Block::WORD_COUNT_BLOCK_TYPES`).
bool countsWords(BlockType type);

QList<BlockType> all();

QStringList headingLevels();
bool isValidHeadingLevel(const QString &level);
QString normalizeHeadingLevel(const QString &level);

QString displayName(BlockType type);
QString displayName(BlockType type, const QString &headingLevel);

} // namespace blocktype
} // namespace writero
