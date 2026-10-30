# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- **License:** ff-ruyijs is now dual-licensed under the AGPL-3.0-only or a commercial
  license (`COMMERCIAL-LICENSE.md`), replacing the ff-ruyijs Source Available License
  v1.0. Releases up to 0.2.1 keep the license they shipped with; Mozilla-derived code
  stays MPL-2.0. Contributions are accepted under the AGPL-3.0 plus a relicensing grant
  (`CONTRIBUTING.md`).

## [0.2.1] - 2026-10-30

### Fixed

- Timer precision is Firefox 159's: `performance.now()`, `timeOrigin`,
  `Date.now()`, `Event.timeStamp` and every navigation / resource time are
  clamped to 1 ms with Firefox's jittered rounding (`nsRFPService`), one seed
  per document. `Event.timeStamp` is on the document's performance timeline.
- Navigation and resource timing report the measured DNS, connect and TLS
  phases from Necko (a reused connection reads `fetchStart`, as in Firefox);
  a browser-initiated load's `fetchStart` precedes `navigationStart`.
  `deliveryType` is in Firefox's member order; unreached milestones read 0.
- Necko resolves IP literals without a lookup and caches names
  (`network.dnsCacheExpiration` 60 s, 1600 entries).
- Layout: CSS margin collapsing, 17 px classic scrollbars on the viewport and
  scroll containers, root `clientWidth/Height` and `scrollWidth/Height` from
  the scrollport, `scrollTopMax` / `scrollLeftMax`, `clientTop` /
  `clientLeft`, `window.scrollMaxX/Y`.
- Fonts: SimSun line height (bad-underline metrics), the Arial default when no
  listed family is installed, Firefox's per-script system fallback list.
- SVG text `getBBox`: font box versus frame descent, and no glyph ink below
  20 px (`text-rendering: auto`).
- Example result files no longer ship in the wheel.

## [0.2.0] - 2026-10-29

### Added

- Canvas 2D draws through Firefox 159's own Skia (`gfx/skia`), set up like
  Firefox's `DrawTargetSkia`: fills, strokes, paths, `clip`, gradients,
  patterns, `roundRect`, `drawImage`, shadows and `fillText` / `strokeText`.
- `Context(tls="100".."159")` selects the Firefox release whose TLS
  ClientHello, HTTP/2 settings and HTTP/3 priorities Necko sends.
- `DOMMatrix` / `DOMMatrixReadOnly`: every operation, `fromMatrix`,
  `fromFloat32/64Array`, CSS transform strings, `transformPoint`, `toString`.

### Fixed

- `Origin` and the body `Content-Type` on fetch / XHR / `sendBeacon`;
  `Sec-Fetch-Site` across redirects; `window.originAgentCluster`.
- `window.event` during microtasks queued by an event listener.
- Font fallback by language group, element `lang`, SVG presentation
  attributes and SVG text `getBBox`; `ctx.font` read-back.
- `python -m ruyijs.examples` lists the examples that ship; DataDome output on
  a Chinese Windows console; the Akamai example keeps its User-Agent and TLS
  version together (`--tls`).

### Changed

- The bundled runtime is rebuilt on Firefox 159 (SpiderMonkey, Necko, Skia).

## [0.1.9] - 2026-10-29

### Added

- README: document native TLS fingerprint switching across Firefox 100–159
  (Necko + NSS). Both English and Chinese READMEs updated.

## [0.1.8] - 2026-10-28

### Removed

- The `expedia-taap` example. It shipped a real account as its default login;
  the credentials are no longer in the source tree or the package.

### Changed

- Wheels are built from tracked files only, so local untracked example
  directories are no longer packaged. The bundled runtime is unchanged from
  0.1.7.

## [0.1.7] - 2026-10-28

### Fixed

- External scripts, stylesheets and preloads hung until the RPC timeout under
  Wine (#1). Wine loaded its builtin `msvcp140.dll` instead of the MSVC one
  shipped beside `js.exe`; its `condition_variable` waits never woke for
  completions pushed by the background transport threads. Under Wine,
  `Context` now sets the per-program override
  `HKCU\Software\Wine\AppDefaults\js.exe\DllOverrides` to
  `msvcp140=native,builtin` (an existing value is kept; native Windows is
  untouched). The 8 s / 10 s caps added in d33062f are reverted.
  (`python/ruyijs/_context.py`)

### Added

- The native build tools (`tools/build.py` and its modules), the third-party
  native sources compiled into `js.exe` (`vendor/compression-native`,
  `vendor/image-native`, `vendor/urlpattern-native`) and the environment
  baseline `profiles/firefox-environment.json` are public again (#3). Local
  paths are no longer hard-coded; pass `--donor-obj` etc. or the matching
  `RUYIJS_*` environment variables. Without a Firefox browser build, the
  recorded `tools/generated/` headers stand in for its font preferences and
  WebIDL member order, so a SpiderMonkey build of `FIREFOX_158_0_RELEASE` is
  enough (see `tools/README.md`).
- The page's `ruyijs` helper object (`createHostFunction`,
  `createHostCollection`, ...) is defined in `src/runtime/host_helpers.cpp` when
  the SpiderMonkey donor has no call tracing, so `src/` builds against official
  Firefox sources; a donor with tracing keeps its own implementation.
- The prebuilt runtime is attached to the GitHub release
  (`ruyijs-runtime-0.1.7-win-amd64.zip`), and `runtime.json` downloads it from
  there.

### Changed

- Bundled runtime rebuilt (`js.exe` SHA-256 `766e3742…`); build-machine paths
  are no longer embedded in `js.exe`, `necko_quic.dll` or the runtime
  manifests.

## [0.1.5] - 2026-10-27

### Fixed

- Host-initiated `Context.navigate()` deadlocked under Wine: the navigation was
  queued as SpiderMonkey tasks (`DeliverNavigation` / `DeliverCommit`) that are
  never delivered between host RPC calls under Wine, so `advance` waited on the
  pending task until the RPC timeout. The host `navigate` action now commits
  synchronously on the call, the same way the host `load` action already does
  (which is why `load_page` was unaffected). In-page navigation (`location.href`,
  `location.assign`, reload) and form submission keep Firefox's async two-task
  model, so the calling script still continues first. Reported in #1.
  (`src/runtime/page_navigation.cpp`, `src/runtime/page_load.*`)

### Changed

- Bundled runtime rebuilt with the navigation fix (`js.exe` SHA-256 `30581e7a…`)
  and re-pinned in `runtime.json`; the embedded Python and other runtime
  components are unchanged.

## [0.1.4] - 2026-10-26

### Fixed

- `python -m ruyijs.examples <name>` failed with "示例文件不存在" from an installed
  wheel: the example locator used a sentinel file (`turnstile/turnstile.py`) that is
  not shipped, so it never recognized the installed layout and looked in the wrong
  directory. It now uses `google/google_search.py`, which ships. Affected every
  example when run from the installed package.

### Removed

- `datadome` and `turnstile` entries from the examples CLI registry; their files are
  not distributed, so they could never run.

## [0.1.2] - 2026-10-22

### Added

- Examples ship inside the wheel as `ruyijs.examples`; run `python -m ruyijs.examples` to list them.
  DataDome offline replay captures are not included.

### Changed

- Bundled runtime rebuilt from the current sources (`js.exe` SHA-256 `0b827e48…`).

## [0.1.1] - 2026-10-22

### Added

- `Context.navigate(url)`: a browser-initiated top-level navigation (no Referer, `Sec-Fetch-Site: none`).

### Changed

- Bundled runtime rebuilt from the current SpiderMonkey shell (`js.exe` SHA-256 `c1e89f22…`), including
  the parser and event fixes made after 0.1.0.
- Published on PyPI as a Windows x64 wheel: `python -m pip install ruyijs`.

## [0.1.0] - 2026-10-21

### Added

- Installable Windows x64 Python wheel with a bundled, SHA256-pinned runtime.
- Source installation through Gitee, version metadata and `get_runtime_path()`.
- Installed-package examples and Python 3.10 / 3.14 integration verification.

- First public release of ruyijs: a SpiderMonkey (Firefox 158) runtime for Windows x64 with a
  Python binding, the `ruyijs_client.dll` C ABI and a CLI.
- A native browser environment across 38 bounded contexts, with a validated fingerprint/profile
  configuration.
- Page loading with iframes, a deterministic clock and event loop, trusted input, and console and API logs.
- The Necko network transport (HTTP/1.1, HTTP/2, HTTP/3) with per-context proxies.
- A portable release export with embedded Python, licenses, a source archive and checksums.
