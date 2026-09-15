# QA 11 — Hosted AI and credits

Status: complete. 10 confirmed root causes fixed (4 desktop, 6 API).

- Desktop delta commit: `f4d9220f07d1d9d9ee590cb06e57c92c4d13046e` (apply after QA08 `bcaff22`; do not cherry-pick dependency `a78d582` again).
- API delta commit: `341e18bf98296c575aa837b0a53652e4c413285c` (based API `1a42c58`).

## Confirmed findings

| ID | Root cause and user-visible failure | Regression evidence | Fix |
|---|---|---|---|
| QA11-001 | Provider tracks only job POST replies; upload/download cannot be cancelled and cancelling POST emits a spurious failure. | Three request-stage rows fail baseline; all pass after fix. | Track every stage, remove ownership before abort, disconnect callbacks during cancel/destruction. |
| QA11-002 | HTTP 200 media upload with missing signed_id emits success; controller then submits AI without the requested reference. | Empty-signature stub emitted uploaded rather than failure. | Require nonempty signature before mediaUploaded. |
| QA11-003 | Upload MIME selection labels every non-PNG file JPEG, including valid WebP. | Real Qt-encoded WebP was sent with JPEG Content-Type on baseline. | Qt MIME database identifies actual supported file type. |
| QA11-004 | Download treats any nonempty 200 response as an image, storing HTML/error pages as generated media. | text/html stub emitted imageFinished on baseline. | Require image MIME type before publishing bytes. |
| QA11-005 | Job row is committed before reserve succeeds; low positive balance leaves pending job that subsequent top-up/retry never dispatches. | $0.01 balance creates orphan pending job; test asserts rollback, then same operation completes after top-up. | Create job and reserve credit in one transaction. |
| QA11-006 | A failed ledger debit returns nil, but billing releases the hold and claims settlement anyway. | Deplete balance after reservation: baseline reported settled despite zero usage transactions. | Raise settlement failure and retain held reservation atomically. |
| QA11-007 | Every image service Error is considered pre-dispatch, including successful provider responses with unusable results, incorrectly restoring spendable credit. | Provider200 without image was failed/released; now ambiguous/held. | Typed PreDispatchError for known configuration/model/input failures; other outcomes retain hold. |
| QA11-008 | Context shape is unchecked: strings can dispatch empty paid work and malformed surrounding entries can raise500. | Malformed context created job unexpectedly; four invalid forms now422 with no job/reservation. | Validate object/array-of-objects at HTTP boundary. |
| QA11-009 | Provider-generated image URLs use unrestricted Faraday.get and can fetch internal services. | WebMock proves GET127.0.0.1/private executed before fix. | Existing SsrfFilter transport pins public IPs and validates redirects; public positive path passes, metadata redirect blocked. |
| QA11-010 | Permissive base64 decoding turns garbage into empty bytes, then stores/charges a completed image. | data:image/png;base64,!!!! completed baseline; now ambiguous without attachment. | Strict decode and nonempty-byte validation. |

## Validation

- Original six server regression failures reproduced in two passes; initial four cases green, then internal URL/base64 cases added and independently red/green.
- Desktop hosted suite: 14 Qt checks pass; 10 consecutive suite executions pass. Final suite with QA08 hosted account-replacement regression:15 checks pass. Cancellation covers submit, upload, and image-download stages. The account replacement case independently failed baseline busy-state assertion and passes QA08; counted in QA08 only.
- Rails hosted billing + hosted API + sync contract: 49 tests, 250 assertions pass (SQLite); final exact totals verified in evidence log.
- Gateway45111 runs this server branch with isolated synthetic SQLite database and paid synthetic account, no configured provider credentials. Actual HTTP missing-provider job fails explicitly; replay returns same ID without settlement.16 malformed submissions across4 workers returned13x422 and3x429; rate limiting was respected, not bypassed.
- No paid provider calls or real internal-network requests were made. External network prohibited by WebMock for Rails provider tests.

## Residuals / limits

- Existing test explicitly defines silently ignoring foreign reference-upload IDs. Preserved that contract; changing it to explicit422 would need a product/API decision.
- Cancellation stops local delivery; synchronous remote provider work may already incur cost, which is retained through server job/idempotency state.
- Image bytes are MIME/base64 validated, not fully decoded by Rails; oversized provider response limits are not covered.
- Fixed estimated reservations can be smaller than eventual usage. Failed settlement now stays held/ambiguous; debt/reconciliation policy is not redesigned here.
- Concurrent same-operation server create races and expired pending/ambiguous reservation reconciliation need dedicated PostgreSQL/concurrency coverage. No invented findings counted for these unverified risks.
- Account/workspace controller lifetime belongs to QA08. The additional hosted account-replacement regression now passes and is not counted again here.

## Integration evidence

Parent integrated the API delta as f53f8320 and reports75Rails tests357assertions
passing on isolated PostgreSQL. This supplements the local SQLite results.
Provider/controller combined test after QA08:15 checks pass in1.65seconds.
