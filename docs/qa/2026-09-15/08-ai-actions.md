# QA08 — AI actions and provider catalogs

Worktree: `qa-ai-actions`, baseline `149c6f6424c9a9613fbd8b166964b2796fc2ead7` (clean). Local synthetic HTTP only.

## Confirmed failing regressions

- **QA08-001** Unconfigured rewrite crashes on null document. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-002** Inserted alternative reuses original block identity. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-003** Deleted target result insertion crashes. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-004** Bulk summary insertion crashes. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-005** Image explanation inserts an invisible Media block. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-006** Malformed bulk content erases original text. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-007** Stale bulk output discarded despite review contract. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-008** Replacing DocumentController leaves old result list. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-009** Whitespace model names create bogus operations. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-010** Bulk rewrite asks for single-block prose but parser needs JSON. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-011** Humanize routing drops additional user instructions. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-012** Removing provider retains model cache. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-013** Changing endpoint retains old model capabilities. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-014** Removed provider repopulated by late catalog response. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.
- **QA08-015** Workspace switch writes old provider catalog into new workspace. Reproduced before fix in `/tmp/qa08-red.log`; fixed; corresponding regression passes.

- **QA08-016** Concurrent ModelCatalog fetch dereferences a cleared reply; reproduced SIGSEGV in `concurrentFetchReplacesPreviousRequest`, fixed by cancelling the superseded request and binding completion to its own reply.
- **QA08-017** Compatible catalog architecture without modalities suppresses vision inference; `compatibleMetadataWithoutModalitiesRetainsInference` failed before fix.
- **QA08-018** Workspace replacement leaves AI work busy/running and pending records orphaned; `workspaceChangeCancelsPendingRequests` failed before fix (`/tmp/qa08-red2.log`).
- **QA08-019** Controller trims leading indentation from Code rewrites; `codeRewritePreservesIndentation` failed before fix.
- **QA08-020** Image explanation dispatches without an attached readable image; `explanationWithoutImageDoesNotDispatch` failed before fix.
- **QA08-021** Provider profile updates reset selected provider to first entry; `modelRefreshRetainsSelectedProvider` reproduced actual selected provider changing (`/tmp/qa08-qml-red.log`). Focused AiPanel state fix verified passing.

## Verification progress

Initial scoped run passed catalog, text actions, and controller; registry cache deletion remained failing because a null QString violates settings NOT NULL. Corrected to a non-null empty string before final verification. Extended lifecycle suite adds three failing-before regressions and 40 simultaneous alternatives; final scoped verification passed. Closed workspace persistence gate is a passing negative test, not counted as another bug. Additional hosted-controller changes retain upload source title and cancel jobs on account replacement; lower-level hosted protocol is owned by QA11.

- **QA08-022** `createResult` logs a failed SQLite write but still returns an operation ID and dispatches external work. Synthetic SQLite trigger failure reproduces the broken persisted-before-dispatch contract. All callers now stop if persistence fails. Regression: `failedResultWriteDoesNotDispatch`, red `/tmp/qa08-red3.log`.
- **QA08-023** Image generation silently omits explicitly requested missing references. It now rejects the request before dispatch, including hosted media gates. Regression: `requestedReferenceMustExist`, red `/tmp/qa08-red3.log`.

## Root cause and fix map

- `src/ai/aicontroller.cpp`: valid setup/readable media/persisted-result gates; fresh IDs for inserted alternatives; explicit text type for explanations; reject deleted-target and aggregate application; preserve code whitespace; validate bulk JSON entries before any mutation and persist skipped output for review; clear results/disconnect previous document controller; cancel pending requests on workspace/account replacement, maintain busy counts, snapshot hosted upload article title.
- `src/ai/textactions.cpp`: bulk-specific structured response instruction and retention of additional Humanize instructions.
- `src/ai/providerregistry.{h,cpp}`: own in-flight catalogs by provider ID, destroy obsolete requests, invalidate both memory/disk catalog on endpoint edit/removal and drop requests on workspace replacement.
- `src/ai/modelcatalog.cpp`: supersede previous request safely, capture the reply being completed, retain capability inference when architecture metadata lacks modality fields.
- `qml/ai/AiPanel.qml`: refresh provider/model lists through explicit handlers while preserving selected IDs, avoid synchronous selector feedback, build hosted model arrays before assignment.

## Coverage and evidence

- Local synthetic HTTP exercises rewrite, multi-model comparison, bulk polish, invalid bulk JSON, stale edits, 40 simultaneous alternatives, catalog refresh/removal/edit/workspace races.
- Native QML instantiation verifies provider selection survives unrelated profile updates. Existing hosted-job suite covers rewrite, image generation/application and explanation against the synthetic hosted API.
- Negative checks cover unconfigured/closed workspace, deleted block and aggregate application, missing explanation/reference image, whitespace-only models, and injected SQLite result-write failure.
- Final pre-commit scoped run: `ctest --test-dir build -R 'tst_(aicontroller|textactions|providerregistry|modelcatalog|hosted_jobs)$' --output-on-failure`: **5/5 passed**, 2.25 seconds (`/tmp/qa08-final3-tests.log`). Build limited to `-j2`. No paid calls or real-provider credentials used.
- Parent owns shipping/review; QA11 adds hosted controller account-cancellation coverage on top of these changes.

## Explicit residuals

- No real paid-provider run; response content quality and remote provider drift are outside these synthetic contracts.
- Native pointer/mouse visual walkthrough of the integrated app remains with parent/QA05; this work verifies the actual AiPanel component in Qt rather than the complete desktop window.
- Workspace reopening on the *same* Workspace QObject needs lifecycle coverage beyond pointer replacement; current Workspace lacks a before-close signal, so cancellation before database swap needs coordination with its owner. Not counted as a confirmed bug here.
- There is no public cancellation action in the current controller UI; replacement cancellation is tested, and adding a user-facing cancel feature is outside a convergent bug fix.

## Handoff

Committed `bcaff22` on `qa/15-sep-ai-actions`; clean worktree, no push. Final controller run after AiPanel account-change handler: **23 passed, zero failures/warnings** (`/tmp/qa08-last-controller.log`). Fix scope: 11 files. No dependency commits included.

## Coordinator count audit

QA08-003 (deleted target) and QA08-004 (bulk aggregate) reach the same nullable-block insertion dereference. Both regression scenarios remain fixed and tested; they count as **one** root cause in the final total. This assignment contributes **22 distinct roots**, preserving23 stable scenario IDs.
