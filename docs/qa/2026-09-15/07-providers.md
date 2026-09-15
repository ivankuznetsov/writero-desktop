# QA07 — Provider protocols

Worktree: `writero-desktop.worktrees/qa-providers`; baseline `149c6f6424c9a9613fbd8b166964b2796fc2ead7`, clean. Owns AiClient/providerprofile and existing AiClient tests. Local ephemeral-port HTTP stubs only, no paid provider calls. Wiki research: no project wiki; master learnings recommends testing provider differences, async AI operations. Contract references: https://docs.ollama.com/api/chat and SSE event framing.

## Reproduced findings

All checks below are `tests/ai/tst_aiclient.cpp`, run against baseline implementation; `/tmp/qa07-focused-red.log` records 10 failing test rows (not ten distinct bugs).

| ID | Defect and causal chain | Reproduction | Status |
| --- | --- | --- | --- |
| QA07-001 | Final unterminated Ollama NDJSON frame sent to SSE-only parser; final text and usage disappear. | `ollamaFinalFrameWithoutNewline`: empty instead of final. | Fixed, regression verified |
| QA07-002 | Ollama vision serialized with OpenAI content parts; native endpoint expects text content plus images base64 array. | `ollamaUsesNativeOptionsAndImages`: text field not a string. | Fixed, regression verified |
| QA07-003 | Provider stream error envelopes ignored; failed inference emits successful chatFinished. | `streamErrorsDoNotCompleteSuccessfully:openai/ollama`: no failure. | Fixed, regression verified |
| QA07-004 | JSON parse errors ignored; corrupt streamed payload emits successful chatFinished. | `streamErrorsDoNotCompleteSuccessfully:malformed-sse/malformed-ndjson`: no failure. | Fixed, regression verified |
| QA07-005 | readyRead consumes HTTP error bodies before finished handler builds message; user loses provider error explanation. | `httpErrorRetainsDiagnostic`: quota exhausted missing. | Fixed, regression verified |
| QA07-006 | SSE parser treats each data line as complete JSON, dropping valid multiline events. | `sseMultilineData`: hello missing. | Fixed, regression verified |
| QA07-007 | Permissive base64 decoder reports invalid image as successful empty image bytes. | `rejectsInvalidBase64Image`: no failure. | Fixed, regression verified |
| QA07-008 | Final token callback can cancel, but finished handler unconditionally completes operation afterward. | `abortInsideFinalTokenDoesNotFinish`: chatFinished emitted after abort. | Fixed, regression verified |
| QA07-009 | Ollama temperature/max_tokens sent as ignored top-level OpenAI options, so user generation controls have no effect. | After fixing vision, `ollamaUsesNativeOptionsAndImages` fails native options temperature assertion (0 instead of 0.2). | Fixed, regression verified |
| QA07-010 | OpenRouter image download stored only in local reply variable; abort cancels already-finished generation POST, leaves image GET active. | `generatedImageDownloadCanBeCanceled`: stub socket remains ConnectedState after abort (1-second bound). | Fixed, regression verified |
| QA07-011 | Provider-supplied generated-image URLs passed to Qt without scheme validation; file URLs read local files and return private bytes as generated image. | `rejectsProviderLocalFileImageUrl`: synthetic private temporary file delivered through imageFinished. | Fixed, regression verified |
| QA07-012 | OpenRouter data URI parser skips URI structure/encoding/media validation; missing comma, non-base64, and text/plain payloads accepted as images. | `rejectsInvalidOpenRouterDataUri`: four failures; invalid-base64 overlaps QA07-007 and is not separately counted. | Fixed, regression verified |

## Verification progress

- First baseline red: 10 failing rows covering QA07-001..008; `/tmp/qa07-focused-red.log`.
- Native-options and image-download cancellation red: 2 failures; `/tmp/qa07-extra-red.log`.
- Local-file URL and malformed data-URI red: 5 failures; `/tmp/qa07-image-red.log`.
- SSE CR line framing additionally reproduced (`sseLineEndings:cr`); included under QA07-006, not another counted bug. LF and CRLF already passed.
- After fixes: 41 Qt checks passed, zero failures; `/tmp/qa07-green.log`. Twelve fragmentation rows cover UTF-8 split boundaries for SSE and NDJSON at chunk sizes 1,2,3,7,17,61. Three rows cover line endings and unterminated final DONE.
- Ten-repeat AiClient + model-catalog stress passed all 20 suite executions in 95 seconds, `/tmp/qa07-stress.log`.

## Practical limits and residual coverage

No real paid providers called. No hosted Writero API changes in this assignment. Providerprofile serializer inspected; no reproducible defect found. Configurable transfer timeouts, forced redirect chains, silently truncated valid JSON streams without a provider completion marker, and full image-byte format validation remain outside verified coverage; these are not counted as bugs. The client still accepts a clean transport EOF without requiring a completion marker, preserving the pre-existing compatibility behavior. Local HTTP stub tests verify provider contracts and cancellation, not upstream service availability.

- Additional successful remote image download regression passes (`generatedImageDownloadCompletes`); valid HTTP download bytes and MIME survive lifecycle changes. Final test binary contains 42 Qt checks including init/cleanup.
- Self-review and `git diff --check` clean. No paid-provider calls; no pushes from this worker. Parent owns review and PR shipping.

Commit: `ebabec9` on `qa/15-sep-providers`. Twelve distinct fixed bugs, QA07-001 through QA07-012.
