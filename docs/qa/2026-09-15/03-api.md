# QA03 — real Rails desktop API gateway

Gateway runs Rails test environment with synthetic SQLite DB, test mail delivery/storage, no provider credentials. Ruby 3.4.7 global gems. Startup script: `/home/asterio/Dev/writero.worktrees/qa-desktop-api/tmp/qa_gateway.sh PORT` (45103–45112). Each port clones `db/qa-seed.sqlite3` to `db/qa-PORT.sqlite3`. Credentials in worktree `tmp/qa-credentials.json` (0600; never included here).

Baseline: desktop_sync_contract_test.rb: 16 tests / 94 assertions pass. Actual HTTP capabilities on port45103 returns200.

## Confirmed defects

- QA03-001: Nonobject mutation entries or attributes trigger500 via unchecked `to_unsafe_h`/`slice`, rather than422. Regression added.
- QA03-002: Operation receipt lookup ignores target document, so reusing identical create mutation against another owned document returns200 replay with the wrong document block and never creates requested block. Regression added.
- QA03-003: attach_media accepts another account's signed blob ID without checking DesktopMediaUpload ownership. A signed ID from shared output grants unauthorized attachment into another account. Regression added.
- QA03-004: Snapshot releases article lock before reading watermark; a concurrent edit between those reads is absent from snapshot yet included in watermark, permanently skipped by subsequent changes. Deterministic real-model interleaving reproduces missing edit; fix captures watermark while still locked.
- QA03-005: Lease pages serialize current article title/version instead of pinned document metadata, mixing title revisions across snapshot pages. Regression changes title after lease and observes mismatched document; fix pins document summary.
- QA03-006: When retention deletes every change, old cursor receives200 empty feed forever instead of410 resnapshot. Regression expires full log after edit; fix uses current article sequence plus one as empty-log floor.
- QA03-007 (parent live Qt client reproduction): mutation replies omit operation_id, so desktop never acknowledges pending work, endlessly replays until rate limited. Existing apply/replay test strengthened and legacy receipt regression both fail before fix. Fix returns operation_id for applied/replayed results, including historical stored receipts.

## Verification

All seven defects have regression coverage and fixes. Desktop integration suite (sync, capture, cloud features, OAuth, hosted AI): **57 tests / 264 assertions / 0 failures**. Hosted suite requires synthetic `OPENROUTER_API_KEY=test_api_key` at boot; HTTP is WebMock-stubbed. A run without that boot variable produced one preexisting hosted-configuration test failure, then the properly configured run passed.

Actual HTTP gateway: desktop capabilities200, MCP-scope capabilities403; eight concurrent identical create mutation submissions all200, exactly one matching block persisted; malformed mutation422. Bounded concurrency4, about0.15s. Parent Qt harness discovered QA03-007; subsequent client probe is parent-owned.

Larger actual HTTP pagination probe: 200 typed block creations in two batches; snapshot pages returned200 +1 blocks, all201 IDs unique and same watermark. Change feed limit17 traversed601 unique monotonically increasing events. No skipped/duplicate events observed.

Root-cause fixes committed on server branch `qa/15-sep-desktop-api`; parent owns review/push/PR stacked on `feat/desktop-sync`. RuboCop four changed Ruby files: clean. Gateway45103 remains running (exec session40865); immutable seed and scripts remain in ignored worktree tmp/db paths. No source checkout or production data modified.
