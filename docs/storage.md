# Local workspace format

A workspace is a directory (default
`$XDG_DATA_HOME/Writero/Writero/workspace`) containing:

```
workspace.db        SQLite database (schema version in PRAGMA user_version)
workspace.db-wal    write-ahead log while the app is running
workspace.lock      single-instance guard (QLockFile, removed on clean exit)
media/
  ab/<sha256>       content-addressed blobs
  .staging/         in-flight imports, cleaned after one minute
```

## Schema version 1

| Table | Purpose |
|---|---|
| `documents` | Title, revision, timestamps, future cloud mapping, `trashed_at` |
| `blocks` | Ordered document blocks: type, markdown content, JSON metadata, media id, per-block revision |
| `revisions` | Immutable content history per block (`create`/`update`/`destroy`, source, timestamp) |
| `media` | One row per content hash: filename, MIME type, byte size |
| `media_versions` | Previous media attachments per block, for media history |
| `pending_operations` | Reserved for cloud sync; operations committed with local saves |
| `settings` | Key/value workspace settings |

Unknown metadata keys written by newer clients are preserved in the JSON
`metadata` column and round-trip untouched.

## Durability rules

- A save commits the document row, every block, and the revision entries for
  the changes that produced it in **one transaction**. WAL journaling plus
  `synchronous = NORMAL` makes acknowledged saves survive a crash.
- A default-constructed `QString` binds as SQL `NULL`; content is normalized
  to an empty string before binding because the column is `NOT NULL`.
- Autosave runs 1.2 s after the last change and on window close. A crash
  loses at most the un-saved interval; every earlier acknowledged save is
  recoverable.
- Media is staged, hashed, and moved into place before any database row
  references it. Abandoned staging files are removed after one minute.
- `MediaStore::removeUnreferenced` never removes blobs referenced by a block
  or a media version.
- Only one application instance may open a workspace; the second receives a
  clear error instead of risking concurrent writers.

## Migration policy

`WorkspaceStore::migrate` reads `PRAGMA user_version` and applies versioned
steps inside a transaction. A failed migration rolls back and leaves a
readable database. Future migrations must keep older data loadable and must
never destroy unknown block metadata.
