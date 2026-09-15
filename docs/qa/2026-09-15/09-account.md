# QA09 Account/OAuth

Baseline 149c6f6. Isolated qa-account worktree, keyring disabled. Red test run /tmp/qa09-red.txt: 6 passed, 10 failed.

| ID | Confirmed defect | Reproduction |
| --- | --- | --- |
| QA09-001 | Switching API origin exposes previous server bearer credential and connected identity | changingOriginDoesNotExposeToken |
| QA09-002 | Late capability response reconnects a cleared session | clearDuringCapabilitiesCannotReconnect |
| QA09-003 | Local clear retains hosted model catalogs | clearRemovesModelCatalogs |
| QA09-004 | HTTP200 malformed capabilities treated as authenticated success | invalidCapabilities(invalid-json) |
| QA09-005 | Unsupported protocol error is overwritten with successful connection | invalidCapabilities(unsupported-version) |
| QA09-006 | TCP-fragmented callback loses initial request bytes and fails OAuth | callbackFragments(fragmented) |
| QA09-007 | Unauthenticated error callback with forged state cancels real sign-in | callbackFragments(forged-error) |
| QA09-008 | Authorization code/state can trigger multiple simultaneous exchanges | authorizationCodeCannotBeSubmittedTwice |
| QA09-009 | Signout during registration still opens browser authorization | signOutCancelsPendingBrowser |
| QA09-010 | Expired token leaves previous account identity/entitlements visible | clearsExpiredSession |

All fixtures synthetic. Fixes in progress; no speculative bugs counted.

## Verified fixes

All ten confirmed defects fixed. Tokens use server-specific credential keys (existing production key retained only for production); session generations discard late registration, exchange, and capability results; local signout resets immediately before independent server revocation. Account identity and all catalogs reset together. Loopback requests accumulate complete bounded headers; only matching GET /callback/state can finish or cancel authorization. State is consumed before token exchange. Capability JSON/protocol validation must succeed before connected=true.

Validation: 19 AccountSession test rows pass; adjacent tst_sync passes. CTest 2/2 targets passed (5.62 seconds). Red baseline had ten failing rows. Added passing checks for server-specific restoration, an expired old account response racing a new account, and shared in-memory credential replacement/removal.

## Real service OAuth evidence

Ran a native C++ AccountSession harness against the isolated Rails service on http://127.0.0.1:45109. Opened its real dynamic-registration authorization URL through agent-browser session writero-qa-sweep, authenticated to synthetic qa03. The real browser followed Rails authorization back to the actual ephemeral Qt HTTP loopback listener. PKCE exchange and desktop capabilities succeeded twice; synthetic authorization used scope=desktop. No third-party provider contacted.

Sanitized native stdout:

```
OAuth connected=1 protocol=1 email=qa03@example.invalid token_present=1
Signed_out=1 token_cleared=1
```

Browser displayed the local callback completion page. The harness's private authorization URL was never included in output/report. Native harness is an uncommitted tmp artifact. Tests used WRITERO_DISABLE_KEYRING=1; actual locked/unavailable Secret Service behavior was not exercised and is not counted as a bug. Third-party OAuth providers are intentionally outside the synthetic local test environment.
