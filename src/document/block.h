#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include "document/blocktype.h"

namespace writero {

/// One atomic block of a document.
///
/// Most blocks carry their payload in `content`. Structured extras live in
/// `metadata` so unknown keys from newer clients survive a load/save cycle:
/// `heading_level` for headings, `language` for code, `src`/`alt` for media
/// blocks that reference an external file.
struct Block
{
    QString id;
    BlockType type = BlockType::Text;
    QString content;
    QVariantMap metadata;
    int revision = 1;
    qint64 mediaId = 0;

    static Block create(BlockType type, const QString &content = {});

    QString headingLevel() const;
    void setHeadingLevel(const QString &level);

    QString language() const;
    void setLanguage(const QString &language);

    QString mediaSource() const;
    void setMediaSource(const QString &source);

    QString mediaAlt() const;
    void setMediaAlt(const QString &alt);

    bool hasMedia() const;
    bool isEmpty() const;

    QVariantMap toJson() const;
    static Block fromJson(const QVariantMap &json);

    bool operator==(const Block &other) const;
    bool operator!=(const Block &other) const { return !(*this == other); }
};

using BlockList = QVector<Block>;

/// Stable client-side identity. Never derived from content.
QString newId();

} // namespace writero
