# macOS arm64

This is the maintainer path for the macOS shell. Windows x64 still installs from the PyPI wheel. macOS builds a local `macosx_26_0_arm64` wheel after `dist/js` and `dist/libmozglue.dylib` exist. `runtime.json` then records that archive's size and sha256. Its macOS URL stays empty, and this tree does not upload the wheel.

The same facts are written in [macos.zh-CN.md](macos.zh-CN.md).

## What lives where

ruyijs on macOS is the outer shell. SpiderMonkey and Necko/NSS come from one official Firefox tree, built outside this repository and never committed here.

| Piece | Where it comes from |
| --- | --- |
| `dist/js`, `dist/libmozglue.dylib` | SpiderMonkey standalone donor, then `tools/build.py` |
| Necko/NSS, TLS, HTTP/2 | `xpcshell` from the browser donor |
| Frame pipe, process launch, C ABI client | this repository |
| Python helper venv | `vendor/necko-derived/python`, gitignored. A wheel uses the Python that installed it |

A maintainer checkout is two sibling directories:

```
firefox/                 official Firefox source, not part of ruyijs
  obj-js-standalone/     SpiderMonkey standalone
  obj-necko/             browser build that produces xpcshell
ff-ruyijs/               this repository
```

The default xpcshell path is the parent of this repo plus `firefox-159/obj-necko/dist/bin/xpcshell`. Rename the Firefox directory or pass `RUYIJS_XPCSHELL`. An installed wheel looks for `dist/necko/xpcshell` inside the runtime before that donor path. The fetch script defaults to `src/resource_network/necko_fetch.js`, found by walking parents of the client, or to `necko_fetch.js` beside the installed client. Override it with `RUYIJS_NECKO_SCRIPT`.

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

A local wheel is tagged `macosx_26_0_arm64`. It places the shell at `_runtime/dist/js` and copies the donor xpcshell closure to `_runtime/dist/necko`. It is not uploaded. Regenerate the archive with `python3.12 -B tools/macos_runtime.py <zip-path>`, then store the printed size and sha256 in the `runtime.json` `macos` object and leave `url` empty.

## How a request moves

The length-prefixed frame format is the same as Windows. On macOS the pipe is a Unix socket. The shell starts `python -I -B -m ruyijs_necko.oneshot`, and that process keeps one xpcshell for the session. The xpcshell owns TLS, HTTP/2, cookies, and redirects. `redirectionLimit` is 0, so a 3xx response is returned as the response. Python does not store cookies and does not follow redirects.

Verified on this path: HTTP/1.1 and HTTP/2. A framed HTTPS check prints the status, the version code (`20` for HTTP/2), and the body length. The donor XUL contains neqo and the preference `network.http.http3.enable`. With the socket process left off, two requests to an HTTP/3 origin still return version 20. Turning the socket process on makes this xpcshell close the pipe and write no error. Do not add another HTTP/3 stack.

## System frameworks

The shell links CoreFoundation, CoreGraphics, CoreText, ImageIO, and Security. SHA, AES-CBC, AES-GCM, and HMAC use CommonCrypto. ECDSA and ECDH use Security.framework. GCM oneshot symbols are declared locally because the macOS 26 SDK headers omit them.

Do not add OpenSSL, FreeType, fontconfig, libpng, libjpeg, or libwebp. Image decode is ImageIO. Font metrics and glyph masks are Core Text. SVG path bounds are Core Graphics.

POSIX changes stay on the non-Windows side of the existing `#ifndef _WIN32` split. The Windows `#else` body stays byte-for-byte.

## What already works

- Unix-socket frames, worker `posix_spawn`, and the C ABI client.
- Core Text font metrics and glyph masks.
- ImageIO still images: PNG, JPEG, GIF, WebP, BMP, and ICO, premultiplied BGRA. A multi-frame GIF uses ImageIO's already composited display frames.
- SVG path bounds for M, L, C, Q, A, and Z.
- WebCrypto SHA, AES-CBC, AES-GCM, HMAC, ECDSA, and ECDH. An ECDH parameter-dictionary failure returns false, and the caller surfaces `OperationError`.
- Necko/NSS through the donor xpcshell. The Akamai example has passed with `init_real` set, over HTTP/2.

CreepJS passed again after the adapter relink, including `svg` and `canvas 2d`, with no failed section.

## Known differences

These surfaces now call the same system and Firefox code the donor uses. They are not a second image or font stack.

| Surface | macOS behavior |
| --- | --- |
| Animated GIF | ImageIO display frames. Disposal is already applied, so a frame Windows clears to transparent can keep the background color. After decode, the repetition count is the GIF metadata value. |
| APNG and animated WebP | ImageIO frames, then the shared blend. Scaling uses Core Graphics and leaves pixels premultiplied. |
| Image color | The display ICC comes from `CGDisplayCopyColorSpace` and `CGColorSpaceCopyICCData`, then Firefox qcms. No profile leaves the pixels unchanged. |
| Canvas text path | `CTFontCreatePathForGlyph`. `fillText` builds that path before the glyph mask. |
| Font alias | `Osaka-Mono` maps to `Osaka` only when the PostScript name and the family both match. That is Firefox's single-face list, not a new table. |
| COLR and CPAL | COLR version 0 and CPAL palette 0. Table bytes come from `CTFontCopyTable`. COLRv1 is not implemented. |
| Glyph gamma | `SystemTextGamma` stays 1.8 and `SystemTextContrast` stays 1.0. `gasp` and `maxp` are readable. The mask does not invent a DirectWrite rendering mode. |

`src/web_media/image_codec.cpp` and `src/web_crypto/digest_backend.cpp` are real ImageIO and CommonCrypto code. They are not stubs.

The DataDome example's homepage result is a server score. It is not a missing Mac binding, and the example's pass condition stays as it is.

## What not to commit

Do not commit the Firefox tree, either objdir, `dist/js`, the helper venv, the macOS runtime zip, cookies, tokens, proxy credentials, or response bodies. The macOS `runtime.json` object may store size and sha256. Leave its URL empty until a real download exists.

## Reporting a macOS issue

Include the ruyijs commit, the macOS version, the Xcode version, the Python version, the Firefox tag, and which of the two objdirs the build used.
