# macOS arm64

This is the maintainer path for the macOS shell. Windows x64 still installs from the PyPI wheel. macOS does not: there is no macOS file in `runtime.json`, and `setup.py` only packs a wheel after a local `dist/js` and `dist/libmozglue.dylib` already exist.

The same facts are written in [macos.zh-CN.md](macos.zh-CN.md).

## What lives where

ruyijs on macOS is the outer shell. SpiderMonkey and Necko/NSS come from one official Firefox tree, built outside this repository and never committed here.

| Piece | Where it comes from |
| --- | --- |
| `dist/js`, `dist/libmozglue.dylib` | SpiderMonkey standalone donor, then `tools/build.py` |
| Necko/NSS, TLS, HTTP/2 | `xpcshell` from the browser donor |
| Frame pipe, process launch, C ABI client | this repository |
| Python helper venv | `vendor/necko-derived/python`, gitignored |

A maintainer checkout is two sibling directories:

```
firefox/                 official Firefox source, not part of ruyijs
  obj-js-standalone/     SpiderMonkey standalone
  obj-necko/             browser build that produces xpcshell
ff-ruyijs/               this repository
```

The default xpcshell path is the parent of this repo plus `firefox-159/obj-necko/dist/bin/xpcshell`. Rename the Firefox directory or pass `RUYIJS_XPCSHELL`. The fetch script defaults to `src/resource_network/necko_fetch.js`, found by walking parents of the installed client. Override it with `RUYIJS_NECKO_SCRIPT`.

Pass the standalone objdir to `tools/build.py --donor-obj`. The matching environment variable is `RUYIJS_DONOR_OBJ`. Pass it for that build. Do not export it from a long-lived shell profile.

## Donor builds

Use the Firefox tag `FIREFOX_159_0_RELEASE`. Keep both objdirs. A later rebuild resumes in place.

SpiderMonkey standalone, `mozconfig-js`:

```
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-js-standalone
ac_add_options --enable-project=js
ac_add_options --enable-optimize
ac_add_options --disable-debug
ac_add_options --disable-tests
ac_add_options --enable-rust-simd
```

Browser donor used for xpcshell, `mozconfig-necko`:

```
mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-necko
ac_add_options --enable-application=browser
ac_add_options --enable-optimize
ac_add_options --disable-debug
ac_add_options --disable-tests
ac_add_options --disable-nodejs
ac_add_options --enable-rust-simd
ac_add_options --without-wasm-sandboxed-libraries
```

`--disable-nodejs` leaves the newtab bundle unfinished. The helper does not need that bundle. Stop when `obj-necko/dist/bin/xpcshell` runs. The linked XUL library is `obj-necko/dist/bin/XUL`.

The helper turns the socket process off. The client sets `MOZ_DISABLE_SOCKET_PROCESS=1`. `necko_fetch.js` sets `network.process.enabled` and `network.http.network_access_on_socket_process.enabled` to false. Leave those in place.

## Build the shell and the helper

The helper venv uses Python 3.12. `tools/bootstrap_macos_helper.py` looks up `python3.12` on `PATH`, then `~/.local/bin`, Homebrew, and `/usr/local/bin`. If none of those exists it stops. When `vendor/necko-derived/python/pyvenv.cfg` already exists, the script leaves that venv alone.

From the repository root, with `MACOSX_DEPLOYMENT_TARGET=26.0`:

```sh
python3.12 -B tools/bootstrap_macos_helper.py
python3.12 -B tools/build.py --donor-obj ../firefox-159/obj-js-standalone
```

If PyPI is only reachable through a proxy, export that proxy in the environment before bootstrap. The script does not contain a proxy address.

Bootstrap installs three packages the frame adapter still imports: `requests`, `brotli`, and `zstandard`. TLS is the donor xpcshell. The `requests` package only prepares the frame that oneshot already spoke on Windows.

`tools/build.py` then copies tracked sources into the venv:

| Source | Installed path |
| --- | --- |
| `src/resource_network/necko_oneshot.py` | `ruyijs_necko/oneshot.py` |
| `src/resource_network/necko_ruyi_init.py` | `ruyijs_necko/__init__.py` |
| `src/resource_network/necko_timing.py` | `ruyijs_necko/timing.py` |
| `src/resource_network/necko_client.py` | `necko/client.py` |
| `src/resource_network/necko_adapters.py` | `necko/adapters.py` |
| `src/resource_network/necko_pkg_init.py` | `necko/__init__.py` |

On Windows the same build copies only `oneshot.py`. If the Darwin venv is missing, the shell still links and no release archive is created.

The shell binary is `dist/js`. Point examples at it with `RUYIJS_SHELL` set to that path, and put `python/` on `PYTHONPATH`. The client library is `libruyijs_client.dylib`.

A local wheel, when you choose to build one, copies `dist/` and is tagged `macosx_26_0_arm64`. It is not published from `runtime.json`.

## How a request moves

The length-prefixed frame format is the same as Windows. On macOS the pipe is a Unix socket. The shell starts `python -I -B -m ruyijs_necko.oneshot`, and that process keeps one xpcshell for the session. The xpcshell owns TLS, HTTP/2, cookies, and redirects. `redirectionLimit` is 0, so a 3xx response is returned as the response. Python does not store cookies and does not follow redirects.

Verified on this path: HTTP/1.1 and HTTP/2. A framed HTTPS check prints the status, the version code (`20` for HTTP/2), and the body length. HTTP/3 is not a verified macOS result. Do not add a second HTTP/3 implementation.

## System frameworks

The shell links CoreFoundation, CoreGraphics, CoreText, ImageIO, and Security. SHA, AES-CBC, AES-GCM, and HMAC use CommonCrypto. ECDSA and ECDH use Security.framework. GCM oneshot symbols are declared locally because the macOS 26 SDK headers omit them.

Do not add OpenSSL, FreeType, fontconfig, libpng, libjpeg, or libwebp. Image decode is ImageIO. Font metrics and glyph masks are Core Text. SVG path bounds are Core Graphics.

POSIX changes stay on the non-Windows side of the existing `#ifndef _WIN32` split. The Windows `#else` body stays byte-for-byte.

## What already works

- Unix-socket frames, worker `posix_spawn`, and the C ABI client.
- Core Text font metrics and glyph masks.
- ImageIO still images: PNG, JPEG, GIF, WebP, BMP, and ICO, first frame, premultiplied BGRA.
- SVG path bounds for M, L, C, Q, A, and Z.
- WebCrypto SHA, AES-CBC, AES-GCM, HMAC, ECDSA, and ECDH. An ECDH parameter-dictionary failure returns false, and the caller surfaces `OperationError`.
- Necko/NSS through the donor xpcshell. The Akamai example has passed with `init_real` set, over HTTP/2.

CreepJS has passed, including `svg` and `canvas 2d`, on the shell built before the later crypto comment-only edits. Re-run it after a real relink.

## What is still empty

These POSIX entry points return false, return an empty table, or return a constant. They keep the `RUYIJS_POSIX_STUB` marker. CreepJS did not require them, so they are not part of this port:

| Surface | File | macOS behavior |
| --- | --- | --- |
| Animated image compose | `src/web_media/image_composition.cpp` | `ComposeImageAnimation` returns false |
| GIF frame compose | `src/web_media/image_gif.cpp` | compose stays on the Windows path |
| Image color transform | `src/web_media/image_color.cpp` | `ImageColorTransform` is empty |
| Canvas text path | `src/web_geometry/canvas_text_path.cpp` | true only for empty text |
| Font aliases | `src/web_geometry/font_aliases.cpp` | alias table is empty |
| COLR glyph mask | `src/web_geometry/glyph_color.cpp` | returns false |
| COLR palette | `src/web_geometry/glyph_palette.cpp` | returns false |
| Glyph gamma and contrast | `src/web_geometry/glyph_style.cpp` | fixed gamma 1.8 and contrast 1.0 |

`src/web_media/image_codec.cpp` and `src/web_crypto/digest_backend.cpp` are real ImageIO and CommonCrypto code. They are not stubs.

The DataDome example's homepage result is a server score. It is not a missing Mac binding, and the example's pass condition stays as it is.

## What not to commit

Do not commit the Firefox tree, either objdir, `dist/js`, the helper venv, cookies, tokens, proxy credentials, or response bodies. Do not add a macOS archive URL to `runtime.json` until a real archive exists and its sha256 is known.

## Reporting a macOS issue

Include the ruyijs commit, the macOS version, the Xcode version, the Python version, the Firefox tag, and which of the two objdirs the build used.
