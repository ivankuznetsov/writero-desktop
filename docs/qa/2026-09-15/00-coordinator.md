# Coordinator integration findings

Three additional root causes reproduced during integrated code review.

| ID | Failure and root cause | Regression evidence | Outcome |
|---|---|---|---|
| QA00-001 | Snapshot completion updates durable cursor separately, then ignores document-save failure. After injected SQLite block-insert failure, cursor advances from2to3 while old content remains; restart permanently skips snapshot content. | `failedSnapshotSaveDoesNotAdvanceDurableCursor` fails cursor assertion before fix. | Save snapshot content and cursor together; on failure retain previous in-memory cursor and report error. Retry after removing trigger succeeds. |
| QA00-002 | Change replay assumes an existing remote/local mapping means its block was saved. Mapping survives interrupted reconciliation; reopening old document and replaying create attempts update of missing block, silently losing the remote create. | `replayRestoresMappedBlockMissingAfterFailedSave` fails restored-block assertion before fix. | Reinsert missing mapped block with the same stable ID, while respecting pending local removals. |

Validation: sync and reconciliation suites pass,5.56seconds. Red/green logs and temporary build logs remain under this report directory. These extend QA10 recovery coverage; neither duplicates QA10-005, which covered the normal feed path never saving remote content. General atomicity of all mapping/conflict side effects is not claimed; interrupted mapping writes now replay safely for this reproduced create path.

## QA00-003 — Missing WebP runtime dependency

The Arch package and CI install only Qt Base/Declarative/Svg. WebP support lives in `qt6-imageformats`, confirmed by `pacman -Qo /usr/lib/qt6/plugins/imageformats/libqwebp.so`. A copied test executable with `qt.conf` restricted to the declared packages reproduces `webpUploadPreservesMimeType` failing to encode WebP. Adding just the WebP plugin makes the same test pass. CI, Arch package dependencies and platform build instructions now include image-format plugins. Logs: `packaging-webp-red.log`, `packaging-webp-green.log`. This also exposes the dependency missing from a clean install; the developer machine's extra plugins masked it.

Other platform package names verified against [Fedora packages](https://packages.fedoraproject.org/pkgs/qt6-qtimageformats/qt6-qtimageformats/) and [Ubuntu Noble packages](https://packages.ubuntu.com/noble/qt6-image-formats-plugins). No Fedora/Ubuntu runtime result is claimed.
