# QA10 — desktop cloud synchronization

Branch `qa/15-sep-sync`; baseline `149c6f6`. Isolated worktree, Qt 6.11.2; real Rails gateway `127.0.0.1:45110`. Skills: wiki research and ce-debug return-to-caller. Credentials read privately from gateway fixture.

## Verified bugs

- **QA10-001 — Integer block/result identifiers discarded.** Rails returns integer database IDs; `QJsonValue::toString()` returns empty for numbers. Mutation acknowledgements delete queued creates without storing a remote mapping; subsequent edits create duplicates. Parent live run: 13 remote blocks vs 2 local after 10 edits. Regression `numericServerIdsRemainMappedAcrossEdits` failed empty mapping before fix; now passes 10 edits with 2 remote blocks. Shared parsing fix covers incremental events and snapshots/results.
- **QA10-002 — Mutation acknowledgements skip unread remote changes.** Receipt cursor replaces last consumed feed cursor, skipping browser changes committed before a local mutation. `mutationAcknowledgementDoesNotSkipUnseenRemoteChanges` failed local title vs browser title; passes preserving cursor until feed consumption.
- **QA10-003 — Snapshot pagination rebuilds server blocks with new local IDs.** Conversion to `Block` then back destroys remote IDs, media and lock metadata. Expired-cursor regression failed stable local ID assertion; passes with raw JSON accumulation.
- **QA10-004 — Remote edits leave stale lock versions.** Reconciler updates text without storing incoming optimistic-lock version; subsequent desktop edit conflicts despite incorporating browser change. Extended `remoteChangesApplyWithoutDirtying` failed version 1 vs 2; passes storing version with accepted update/create/move. Live Rails feed showed create version 0 followed by move versions 1/2; missing move-version propagation caused real conflicts until fixed. Title-event regression also failed version 0 vs 5 and now passes. These are grouped as one stale optimistic-version propagation defect.
- **QA10-005 — Incremental feed content is not persisted with cursor.** Only sync metadata is saved, while remote application deliberately leaves document clean. Reopening loses downloaded changes while cursor prevents replay. Reopen assertion passes after saving full document with consumed cursor; independently reverting only the persistence fix reproduces reopened `local paragraph` instead of `browser edit` (`qa-sync-persistence-red.log`); restored fix passes.
- **QA10-006 — Resnapshot deletes pending edits to remotely removed blocks.** Merge retains only unmapped pending creates and silently drops mapped pending edits absent from snapshot. Regression failed block existence; passes retaining local text and recording delete conflict.
- **QA10-007 — Snapshot ignores all editorial results.** Rails snapshot `results` wrappers are never reconciled, so results older than retention floor never arrive. `snapshotImportsEditorialResults` failed 0 vs 1; now passes importing wrappers and accumulating first-page results across snapshot pages.

- **QA10-008 — In-flight responses operate on a newly selected document.** Starting cloud creation then opening another local document before the reply linked/uploaded the replacement document. `switchingDocumentsIgnoresOutstandingCreateResponse` failed cloud-ID emptiness; now passes. Request-generation cancellation applies to document replacement/load, workspace/account replacement, sign-out and pause; old document signals are disconnected.
- **QA10-009 — Typing during a request is overwritten without conflict.** Pending context only consulted saved queue; edits in the live journal were absent. Feed regression failed with browser content replacing typing. Save-before-reconcile preserves typing and retains conflict. Snapshot variant additionally reproduced automatic upload overwriting server text despite retained conflict; snapshot now waits for resolution and preserves pending block versions. Both variants pass.
- **QA10-010 — Editorial results from different tables overwrite each other.** RewriteResult ID 7 and ResearchResult ID 7 shared the same local remote-ID key. After numeric parsing was corrected, dedicated regression reproduced 1 result instead of 2. Identity now includes result kind; both retained and same-kind upserts remain stable.

## Final validation

- Build: `cmake --build build --target tst_sync tst_reconciliation -j2`.
- Qt offscreen tests: 23 sync cases and 14 reconciliation cases pass (including init/cleanup); no skipped cases.
- Ten-edit real C++/Rails probe: latest Unicode text exact, 3 local = 3 remote blocks including Rails initial blank, zero pending/conflicts. Original baseline produced 13 remote vs 2 local.
- Bounded real stress: 100 successive Unicode edits, then 201 API-created blocks, forcing two-page resnapshot. Exact **204 block IDs, order, content and optimistic-lock versions** match authoritative leased Rails snapshot after local reopen. Zero pending/conflicts. Evidence `live-sync-stress-exact.log`; harness `live-sync-stress.cpp`.
- No external accounts or production data touched; gateway fixture is local synthetic data.
- Owned implementation/tests only; parent owns review, integration and PR shipping.

## Coverage limits for later passes

History/share/media existing tests pass, but this pass did not exhaust every delayed history selection, media completion ordering, conflict resolution variant, or network/disk failure. Do not interpret the ten verified root defects as exhaustive coverage of all sync behavior.
