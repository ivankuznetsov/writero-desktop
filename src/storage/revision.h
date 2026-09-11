#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>

#include "document/blocktype.h"

namespace writero {

/// One immutable entry of a block's local content history.
///
/// Local history is deliberately separate from any future sync transport
/// history: it records what this workspace observed, not what a server would
/// attribute.
struct Revision
{
    qint64 id = 0;
    QString blockId;
    QString event = QStringLiteral("update");
    QString source = QStringLiteral("local");
    QString content;
    BlockType type = BlockType::Text;
    QVariantMap metadata;
    QDateTime createdAt;

    bool isValid() const { return id > 0; }
};

} // namespace writero
