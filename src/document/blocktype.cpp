#include "document/blocktype.h"

#include <QHash>

namespace writero::blocktype {

QString toKey(BlockType type)
{
    switch (type) {
    case BlockType::Text:
        return QStringLiteral("text");
    case BlockType::Heading:
        return QStringLiteral("heading");
    case BlockType::Ul:
        return QStringLiteral("ul");
    case BlockType::Ol:
        return QStringLiteral("ol");
    case BlockType::Code:
        return QStringLiteral("code");
    case BlockType::Quote:
        return QStringLiteral("quote");
    case BlockType::Media:
        return QStringLiteral("media");
    case BlockType::Divider:
        return QStringLiteral("divider");
    }
    return QStringLiteral("text");
}

BlockType fromKey(const QString &key, bool *ok)
{
    static const QHash<QString, BlockType> map = {
        {QStringLiteral("text"), BlockType::Text},
        {QStringLiteral("heading"), BlockType::Heading},
        {QStringLiteral("ul"), BlockType::Ul},
        {QStringLiteral("ol"), BlockType::Ol},
        {QStringLiteral("code"), BlockType::Code},
        {QStringLiteral("quote"), BlockType::Quote},
        {QStringLiteral("media"), BlockType::Media},
        {QStringLiteral("divider"), BlockType::Divider},
    };
    const auto it = map.constFind(key);
    const bool found = it != map.constEnd();
    if (ok)
        *ok = found;
    return found ? it.value() : BlockType::Text;
}

bool isTextual(BlockType type)
{
    switch (type) {
    case BlockType::Text:
    case BlockType::Heading:
    case BlockType::Ul:
    case BlockType::Ol:
    case BlockType::Code:
    case BlockType::Quote:
        return true;
    case BlockType::Media:
    case BlockType::Divider:
        return false;
    }
    return false;
}

bool isList(BlockType type)
{
    return type == BlockType::Ul || type == BlockType::Ol;
}

bool isRewritable(BlockType type)
{
    switch (type) {
    case BlockType::Text:
    case BlockType::Heading:
    case BlockType::Ul:
    case BlockType::Ol:
    case BlockType::Code:
    case BlockType::Quote:
        return true;
    case BlockType::Media:
    case BlockType::Divider:
        return false;
    }
    return false;
}

bool countsWords(BlockType type)
{
    switch (type) {
    case BlockType::Text:
    case BlockType::Heading:
    case BlockType::Quote:
    case BlockType::Ul:
    case BlockType::Ol:
        return true;
    case BlockType::Code:
    case BlockType::Media:
    case BlockType::Divider:
        return false;
    }
    return false;
}

QList<BlockType> all()
{
    return {
        BlockType::Text,    BlockType::Heading, BlockType::Ul,     BlockType::Ol,
        BlockType::Code,    BlockType::Quote,   BlockType::Media,  BlockType::Divider,
    };
}

QStringList headingLevels()
{
    return {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
            QStringLiteral("h4")};
}

bool isValidHeadingLevel(const QString &level)
{
    return headingLevels().contains(level);
}

QString normalizeHeadingLevel(const QString &level)
{
    return isValidHeadingLevel(level) ? level : QStringLiteral("h2");
}

QString displayName(BlockType type)
{
    switch (type) {
    case BlockType::Text:
        return QStringLiteral("Text");
    case BlockType::Heading:
        return QStringLiteral("Heading 2");
    case BlockType::Ul:
        return QStringLiteral("Bullet List");
    case BlockType::Ol:
        return QStringLiteral("Numbered List");
    case BlockType::Code:
        return QStringLiteral("Code");
    case BlockType::Quote:
        return QStringLiteral("Quote");
    case BlockType::Media:
        return QStringLiteral("Image / Video");
    case BlockType::Divider:
        return QStringLiteral("Divider");
    }
    return {};
}

QString displayName(BlockType type, const QString &headingLevel)
{
    if (type == BlockType::Heading) {
        return QStringLiteral("Heading %1")
            .arg(normalizeHeadingLevel(headingLevel).mid(1));
    }
    return displayName(type);
}

} // namespace writero::blocktype
