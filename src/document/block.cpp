#include "document/block.h"

#include <QUuid>

namespace writero {

Block Block::create(BlockType type, const QString &content)
{
    Block block;
    block.id = newId();
    block.type = type;
    block.content = content;
    if (type == BlockType::Heading)
        block.setHeadingLevel(QStringLiteral("h2"));
    return block;
}

QString Block::headingLevel() const
{
    return metadata.value(QStringLiteral("heading_level")).toString();
}

void Block::setHeadingLevel(const QString &level)
{
    metadata.insert(QStringLiteral("heading_level"), blocktype::normalizeHeadingLevel(level));
}

QString Block::language() const
{
    return metadata.value(QStringLiteral("language")).toString();
}

void Block::setLanguage(const QString &language)
{
    if (language.isEmpty())
        metadata.remove(QStringLiteral("language"));
    else
        metadata.insert(QStringLiteral("language"), language);
}

QString Block::mediaSource() const
{
    return metadata.value(QStringLiteral("src")).toString();
}

void Block::setMediaSource(const QString &source)
{
    if (source.isEmpty())
        metadata.remove(QStringLiteral("src"));
    else
        metadata.insert(QStringLiteral("src"), source);
}

QString Block::mediaAlt() const
{
    return metadata.value(QStringLiteral("alt")).toString();
}

void Block::setMediaAlt(const QString &alt)
{
    if (alt.isEmpty())
        metadata.remove(QStringLiteral("alt"));
    else
        metadata.insert(QStringLiteral("alt"), alt);
}

bool Block::hasMedia() const
{
    return mediaId > 0 || !mediaSource().isEmpty();
}

bool Block::isEmpty() const
{
    return content.isEmpty();
}

QVariantMap Block::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), blocktype::toKey(type)},
        {QStringLiteral("content"), content},
        {QStringLiteral("metadata"), metadata},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("media_id"), mediaId},
    };
}

Block Block::fromJson(const QVariantMap &json)
{
    Block block;
    block.id = json.value(QStringLiteral("id")).toString();
    if (block.id.isEmpty())
        block.id = newId();
    bool ok = false;
    block.type = blocktype::fromKey(json.value(QStringLiteral("type")).toString(), &ok);
    if (!ok)
        block.type = BlockType::Text;
    block.content = json.value(QStringLiteral("content")).toString();
    block.metadata = json.value(QStringLiteral("metadata")).toMap();
    block.revision = qMax(1, json.value(QStringLiteral("revision"), 1).toInt());
    block.mediaId = json.value(QStringLiteral("media_id")).toLongLong();
    return block;
}

bool Block::operator==(const Block &other) const
{
    return id == other.id && type == other.type && content == other.content
        && metadata == other.metadata && revision == other.revision
        && mediaId == other.mediaId;
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace writero
