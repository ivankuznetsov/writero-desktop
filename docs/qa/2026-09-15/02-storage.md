# QA02 Storage sweep

Branch `qa/15-sep-storage`, baseline `149c6f6`, clean initial worktree. Synthetic QTemporaryDir data only. Wiki context: desktop initiative specifies durable local documents/media and sync queue; prior runtime correctness was unverified. Applied wiki-researcher local fallback and ce-debug return-to-caller.

## Findings under reproduction

- QA02-001: Closed media GC resolves empty root against cwd, potentially deleting unrelated files in immediate subdirectories. Root: `MediaStore::removeUnreferenced` lacks open guard. Regression creates disposable bucket/valuable and calls closed GC.
- QA02-002: Failed media staging directory creation leaves `isOpen()` true. Root: `open` assigns member paths before mkdir succeeds. Regression blocks media path with regular file.
- QA02-003: Invalid/empty media hashes resolve directories or traversal paths. Root: hash path builder only requires length 2; empty `contains` tests media directory existence. Regression invalid/empty hashes and closed lookup.
- QA02-004: Failed SQLite migration leaves `isOpen()` true despite `open` false. Root: migration failure returns without closing connection. Regression incompatible synthetic documents schema.
- QA02-005: Permanent deletion retains pending operations, sync map/conflicts, and media versions, including deleted-block history. Root: auxiliary tables lack document FK and delete only removes document row. Regression save + pending/map/conflict/media history + removed block revision + permanent delete.

Validation: targeted tests compiling; red/green results pending.

## Confirmed reproduction and fixes

QA02-001 through QA02-005 reproduced in QtTest before implementation and now pass (16/16 storage cases including existing coverage). QA02-004 initial incompatible fixture was too permissive; revised fixture duplicates a migration column and demonstrates migration failure with `isOpen == true`. Implemented closed-state guards, state reset after failed open, strict lowercase SHA256 lookup validation, closing failed migrations, and transactionally deleting detached document data.

- QA02-006: Bundle import reports success with missing attached bytes when staging writes fail. Repro exports a valid bundle, opens a fresh synthetic target, replaces `.staging` directory with a file, imports. Before fix returns a new ID and commits unusable attachment. Root: workspace import discards MediaStore error and creates document before media writes. Fix checks writes/DB media registration and saves document only after media success. Regression `bundleImportFailsWhenMediaCannotBeStored` failed before fix.
- QA02-007: Bundle export reports success after attached media disappears. Repro persisted media record with missing blob then export. Before fix returns document ID and produces a bundle that cannot import. Root: export ignores QFile open/read errors and exports empty media. Fix fails with explicit read error. Regression `bundleExportFailsWhenAttachedBlobIsMissing` failed before fix.

- QA02-008: Failed SQLite commit leaves transaction and unsaved content active; subsequent save cannot begin a transaction. Repro enables deferred FK checking on synthetic connection, saves nonexistent media reference, observes failed save but reads uncommitted new title. Root: save commit failure returns without rollback (migration same pattern). Regression `failedCommitRollsBackAndAllowsRetry` fails before fix with actual title `Must roll back`, expected `Committed`. Fix rolls back failed commits; regression also verifies a subsequent valid save succeeds.

QA02-006/007 and full storage/workspace tests pass after fixes. Twenty repetitions of both suites now running for persistence, autosave, lock and media stress.

## Final verification and residual coverage

All **8 confirmed bugs fixed**, each with failing-before/passing-after regression evidence. `cmake -B build -G Ninja`; targeted build `-j2`; `ctest --test-dir build -R tst_workspace --output-on-failure --repeat until-fail:20`: **40 suite executions passed**, ~30 seconds. Final deletion self-review added a shared historical-block guard so deleting an imported revision cannot remove source media versions; new guard regression and both suites passed again. Final suite totals: storage 18 passed, workspace 13 passed (includes QtTest lifecycle cases). `git diff --check` clean.

Coverage: document SQL round trips, unknown metadata, history restore, trash/restore/delete, pending/sync/media cleanup, staged-media failures, media lookup confinement, abandoned staging, autosave timer, workspace lock acquisition/release, bundle media transfer/error propagation, migration failure cleanup, failed-commit rollback/retry. No live API required for these storage tests; no gateway started on reserved 45102.

Honest residuals (not counted as bugs): actual process-kill/power-loss durability was not exercised; the preexisting test called `crashBeforeSaveLosesNothingAlreadyCommitted` merely destroys/reopens a store. Low-level disk full and fsync failures were represented by deterministic staging and commit fault injection, not a full filesystem fault harness. Bundle revision insertion still occurs after document save and ignores insertion failure; needs dedicated atomic import follow-up. Historical media versions are not exported by current bundle format; IO owner should assess preservation contract. Future schema versions are accepted by current migrate code without explicit compatibility checks; not changed without established version contract.
