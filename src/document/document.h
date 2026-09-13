#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include "document/block.h"

namespace writero {

/// In-memory representation of one document. Storage and UI both operate on
/// this type; it is intentionally free of Qt signals and I/O.
class Document
{
public:
    QString id;
    QString title;
    qint64 revision = 0;
    BlockList blocks;
    QDateTime createdAt;
    QDateTime updatedAt;

    /// Present once the document is connected to a Writero cloud article.
    QString cloudId;
    QString cloudState = QStringLiteral("local");
    qint64 syncCursor = 0;
    qint64 feedGeneration = 0;
    qint64 syncTitleVersion = 0;

    int indexOf(const QString &blockId) const;
    const Block *blockById(const QString &blockId) const;
    Block *blockById(const QString &blockId);

    bool isEmpty() const;
    int wordCount() const;
    int characterCount() const;

    /// The web editor keeps at least one empty trailing text block so there is
    /// always a place to type. Mirrors `Article#ensure_trailing_empty_block!`.
    bool hasTrailingEmptyTextBlock() const;
    void ensureTrailingEmptyTextBlock();

    QString textPreview(int maxLength = 100) const;
};

} // namespace writero
