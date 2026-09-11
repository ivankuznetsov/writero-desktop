#pragma once

#include <QDateTime>
#include <QString>

#include "document/block.h"

namespace writero {

/// A single undoable mutation of a document.
///
/// Changes carry complete before/after snapshots of everything they touch so
/// that undo, redo, and revision recording never need to re-derive state.
struct DocumentChange
{
    enum class Kind {
        Title,
        InsertBlock,
        RemoveBlock,
        UpdateBlock,
        MoveBlock,
        ReplaceAll,
    };

    Kind kind = Kind::UpdateBlock;

    /// Block this change targets. Empty for Title and ReplaceAll.
    QString blockId;

    /// Full snapshots for structural changes (Insert/Remove/Update/Move).
    Block beforeBlock;
    Block afterBlock;

    /// Indexes for Insert/Remove/Move.
    int fromIndex = -1;
    int toIndex = -1;

    /// Title snapshots.
    QString beforeTitle;
    QString afterTitle;

    /// Full document snapshots for ReplaceAll (import, restore, resync).
    BlockList beforeBlocks;
    BlockList afterBlocks;

    /// Where the change came from: local, import, ai, restore, sync.
    QString source = QStringLiteral("local");
    QDateTime at;

    bool isValid() const { return kind == Kind::ReplaceAll || !blockId.isEmpty(); }
};

} // namespace writero
