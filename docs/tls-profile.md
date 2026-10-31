# macOS TLS profiles 100–159

The listener capture is unchanged. The HTTP/2 and page measurements in this note are on branch `tls/necko-core-arm64` and are not committed. The test write-up is `docs/tls-profile-report.zh-CN.md`.

## Decision

Mac follows the native entry the Windows 0.2.1 wheel already uses. `oneshot.py` imports `necko.adapters.HTTPAdapter`. In that wheel the adapter builds a `necko_tls_profile` and calls `necko_tls_open_profile` on `necko_core`. The version comes from `NECKO_FIREFOX`. That C source is absent from the official Firefox tree and from the public Gitee repository, which contains only a README.

This round writes an arm64 adapter in `native/necko_tls/necko_tls.c` against that ABI and the donor NSS 3.131 headers, and builds `libnecko_core.dylib`. The only profile table is `native/necko_tls/necko_win/_tls_profiles.py`, byte-identical to the file in the Windows wheel (4883 bytes, MPL-2.0). The duplicate at `native/necko_tls/_tls_profiles.py` is removed. `hello_probe.py` loads the `necko_win` copy. Mac does not encode Firefox 100–159 as xpcshell preferences, and it does not assemble a ClientHello in JavaScript.

The library is a TLS filter over read and write callbacks. Direct HTTPS still speaks HTTP/2 with hyper-h2 on the wheel's `_nss.TLS`. Connection setup is `native/necko_tls/necko_win/_wire.py`, a small copy of the 0.2.1 open path. It is not a vendored `_http2.py`, and it does not import the HTTP/3 module. When the caller omits a header, `necko_win/_firefox.py` (the 0.2.1 `_firefox.py`) fills it for that `tls=` value. A header the caller sets wins. The same file supplies the HTTP/2 SETTINGS, the priority-tree flag, and the HPACK encoder policy. HTTP/3 fields in that file are read by `libnecko_quic` and are not written into the TLS ClientHello. See `docs/closed-route.zh-CN.md`. `wire_headers` looks for `ruyijs_necko.wire_fields.WireFields` and continues on `ImportError`. The Mac helper does not ship `wire_fields.py`. The table's User-Agent stays the wheel's Windows string. `configure_fingerprint` remains the navigator surface. ALPN `h2` is version code 20. ALPN `http/1.1` writes HTTP/1.1 on the same socket, version code 11, including Content-Length, chunked bodies, and a skipped 1xx. That parser was unit-tested. A later direct-TLS check of `https://www.gnu.org/` returned status 200 and version 11. Any other ALPN closes the socket and is unwired.

Direct HTTPS uses the library when `necko_tls_nss_version()` is `3.131`. `python/ruyijs/_context.py` sets `NECKO_CORE_LIBRARY` only when that variable is unset. It prefers `dist/necko_tls/libnecko_core.dylib`, then `dist/necko/libnecko_core.dylib`. The staged tree already has the NSS siblings in `dist/necko`, and the dylib rpath is `@loader_path`. No runtime zip was rebuilt, and `macos.url` stays empty. The oneshot client uses the environment path when that file exists, then walks parents for the same two locations, then a sibling `libnecko_core.dylib`. Certificate checks use the wheel's cryptography verifier and the Firefox roots. A verification failure does not fall back to xpcshell. `https://expired.badssl.com/` raised `SSLCertVerificationError` with failure kind `verify`. A client certificate is not in the oneshot frame. `fetch(..., cert=)` raises before connect, and the router falls back only for that unwired case, for an unsupported ALPN, or when HTTP/2 GOAWAY says the peer requires HTTP/1.1. A proxy, a non-HTTPS URL, a missing library, or a different NSS build stays on the donor xpcshell, whose verifier remains mozpkix. `RUYIJS_HTTP_PROXY` was unset in this run, so the proxy path was not retested. `necko_fetch.js` still does not read `NECKO_FIREFOX`.

## Direct library capture

`native/necko_tls/hello_probe.py` loads the profile table above, connects the library to a local TCP listener, and keeps the first TLS record. The listener then closes, so `necko_tls_open_profile` returns -1 and the error text is `handshake (PR_END_OF_FILE_ERROR)`. That result is expected. `necko_tls_nss_version()` is `3.131`. stderr on this rerun was empty.

| tls | Cipher count | Group count | Key shares | Bytes | SCT | ECH | Compression algorithms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 100 | 17 | 6 | 2: 32, 65 | 517 | no | no | 0 |
| 159 | 15 | 5 | 3: 1216, 32, 65 | 1832 | yes | yes | 3 |

The 159 counts and key-share lengths sit on the 156–159 cluster below. 100 has left the 15-cipher, 5-group, 3-share shape. The 1832-byte listener record differs from the 1869-byte xpcshell page capture, so those two hellos are not treated as byte-identical. The comparison is counts and key-share layout.

A completed handshake to `https://example.com/` through the same library, with certificate verification on, returned status 200, ALPN `h2`, and 577 body bytes for both 100 and 159. The 100 record stayed 517 bytes. The 159 record on that live connection was 1834 bytes.

## What the Windows wheel does

The published `ruyijs` 0.2.1 `win_amd64` wheel vendors `necko` 0.8.2. `js.exe` contains the UTF-16 text `ruyijs_necko` and `oneshot`, and it does not contain `NECKO_FIREFOX` or `necko_tls_open_profile`. The shell starts the Python helper. The helper reads the variable.

`necko_core.dll` in the related `ruyihttp` 0.9.0 wheel is 123392 bytes. It exports `necko_tls_open`, `necko_tls_open_profile`, `necko_tls_nss_version`, and the read, write, ALPN, cipher, and close calls. `_nss.py` refuses to run unless `necko_tls_nss_version()` is `3.131`. HTTP headers, HTTP/2 settings, and the HTTP/3 profile are a second table in `_firefox.py`. They are not preferences.

`_tls_profiles.py` is 4883 bytes and is byte-identical in `ruyihttp` 0.9.0 and in the ruyijs wheel. It is seven clusters, not sixty unrelated hellos:

| Versions | What changes from the previous cluster |
| --- | --- |
| 100–118 | 17 cipher values, 6 groups, one extra key share, no GREASE ECH, no certificate compression, no SCT |
| 119–131 | GREASE ECH turns on |
| 132–134 | A hybrid group is first, two extra key shares, certificate compression turns on |
| 135–150 | SCT turns on |
| 151–153 | The cipher list loses one value (16 left) |
| 154–155 | The cipher list loses one more value (15 left) |
| 156–159 | Two finite-field groups drop out (5 groups left) |

Counts above are the lengths of the tuples in that file. The file does not store a captured JA3 string. Its header says the tuples were generated from Firefox release tags and checked against official builds.

## What this macOS tree does

`python/ruyijs/_context.py` copies `tls` into the child environment as `NECKO_FIREFOX`, then sets `NECKO_CORE_LIBRARY` by the lookup above when the variable is unset. `src/resource_network/necko_fetch.js` still does not read the variable. `install_network_helper` copies `necko_core_client.py` to `necko/core_client.py` and copies every non-bytecode file in `native/necko_tls/necko_win/` onto the helper path. That directory holds `_nss.py`, `_tls_profiles.py`, `_firefox.py`, `_wire.py`, `firefox-suffixes.dat`, and the Firefox roots. The header table imports `publicsuffixlist` and reads the suffix file beside it. The helper also imports `h2` and `cryptography`. `tools/bootstrap_macos_helper.py` lists `publicsuffixlist` with those packages. An existing venv stays. Missing imports install only from gitignored `vendor/necko-derived/wheels` with `pip --no-index`. The script contains no proxy address. Bootstrap was not run again this round. The current helper already imports `publicsuffixlist`.

The donor `XUL` (the obj-necko binary) has no `necko_tls_open_profile` string. `libnss3.dylib` does contain the text `NSS 3.131`, which is the version the Windows entry point requires. The export is missing, not the NSS version.

The next two paragraphs are the earlier xpcshell page path. They show why a preference cannot select these clusters. They are not the current direct-HTTPS hello.

A local capture against this xpcshell kept one handshake shape for an unset variable and for `100`, `120`, and `159`: 1869 bytes, 15 cipher values, 5 groups, 11 signature values, 3 key shares (1216, 32, and 65 bytes), SCT present, a 6-byte certificate-compression algorithm list, an ECH extension of 282 bytes, ALPN `h2` then `http/1.1`, supported versions TLS 1.3 and TLS 1.2. That shape matches the 156–159 cluster above, including when the API asked for 100 or 120. Two captures with the variable unset matched each other.

The capture method can see a real change. A throwaway copy of `necko_fetch.js` set `security.tls.version.max` to 3 and the hello fell to 172 bytes, 12 cipher values, no key share, and no ECH extension. A preference can move this NSS. It cannot select the clusters above. Those clusters also change HTTP/2 settings and headers, which this preference does not touch.

This round opened the page through the existing `dist/js` shell and did not rebuild it. `Context()` and `Context(tls="100")` both loaded `https://example.com/` with status 200, protocol `h2`, and `readyState` complete. The attributes were `159` and `100`. xpcshell was not running for those navigations. The first connection followed `tls=`: 15 ciphers, 5 groups, and 3 key shares for 159, with SCT, ECH, 3 compression algorithms, and 1834 bytes; 17, 6, and 2 for 100, with no SCT, no ECH, and 517 bytes. A second connection from the same page kept those counts and used a different record length, so length is not the acceptance check. None of these records is the old 1869-byte xpcshell hello.

Library GETs with certificate verification on, for 100, 119, 132, 135, 151, 154, 156, and 159, each returned status 200 and ALPN `h2`. The hello counts matched the profile table, including 156 on the 156–159 cluster. The post-quantum signature change for 156 is only in the HTTP/3 table and is not written into the TLS ClientHello, so it was not added to the TCP hello. Pages were rechecked only for 100 and 159. This note does not say sixty versions were opened as pages.

Inside one process, two GETs to `https://example.com/` on one adapter sent one ClientHello and both returned status 200. The pool key is hostname, port, and the `tls=` value, and it keeps only ALPN `h2`. HTTP/1.1 closes after one exchange. A failed exchange drops the lane. Session tickets stay on, so a later handshake may be longer. Length is not the acceptance check. The page shell still starts one oneshot per request. `--session` is what reuses the adapter. Cookies are not stored, and `redirectionLimit` stays 0.

`NECKO_CORE_HELLO_LOG`, when set to a file path, appends one JSON line of those counts per direct fetch. It does not record cipher values.

## Work order

1. Done. `tools/build.py --necko-core-only --firefox-gfx-obj <obj-necko>` builds arm64 `libnecko_core.dylib` and links it to donor NSS 3.131. The binary stays in gitignored `dist/necko_tls/`. Do not copy the Windows DLL into the Mac tree.
2. Done for direct HTTPS. oneshot sends `https` without a proxy through the library and keeps xpcshell for a proxy or any other scheme. Both 100 and 159 returned status 200 and ALPN `h2` for `https://example.com/`. When the dylib exists, `tools/macos_runtime.py` copies it into the staged directory.
3. Done. In the direct-library capture, `tls=100` leaves the 15-cipher, 5-group, 3-share shape and `tls=159` stays on it. Compare clusters.
4. Done for the product header and SETTINGS table on this socket. Caller headers win. Omitted headers are filled from the 0.2.1 `_firefox.py` copied to `necko_win/_firefox.py`. HTTP/3 fields in that file are read by `libnecko_quic` and are not written into the TLS ClientHello. See `docs/closed-route.zh-CN.md`. The table's User-Agent stays the wheel's Windows string. `configure_fingerprint` remains the navigator surface. Eight cluster boundaries (100, 119, 132, 135, 151, 154, 156, 159) were library GETs: status 200, ALPN `h2`, counts matched the table. Pages were rechecked only for 100 and 159. Inside one process, two GETs to the same origin used one ClientHello. The page shell still starts one oneshot per subresource unless `--session` is used. Session tickets stay on. Record length is not an acceptance check, and it is not claimed identical to the donor page hello. Do not say the Mac page switches all sixty versions.
5. Done for same-process HTTP/3. `libnecko_quic` reads the HTTP/3 profile fields and does not write them into the TLS ClientHello. The socket process stays off. See `docs/closed-route.zh-CN.md`.

No profile patch lands in `necko_fetch.js`.

## Acceptance

- The same `tls` value selects the same cluster in the Mac adapter and in the Windows wheel's `_tls_profiles.py`. The direct capture already separates 100 and 159 on that rule.
- A direct capture for 100 and a direct capture for 159 differ in the cipher count, group count, and key-share layout described above. This round passed.
- `Context()` and `Context(tls="100")` each opened `https://example.com/` at status 200 and protocol `h2`, and the handshake counts followed `tls=`. xpcshell was not running. The eight boundary library GETs above did the same.
- A verification failure stays on the library. `https://expired.badssl.com/` raised `SSLCertVerificationError` and did not return a status. The router falls back only for an unwired call: a client certificate, an unsupported ALPN, or a peer that requires HTTP/1.1. The client-certificate check raises before connect. No live client-certificate server was used.
- Same-origin HTTP/2 reuse inside one process passed: one ClientHello and two status 200 responses. A proxy still uses xpcshell. `RUYIJS_HTTP_PROXY` was unset, so that path was not retested.
- Direct cleartext uses `necko_socket_*` and `necko_exchange_*` in `libnecko_core`. A request that still reaches the donor xpcshell returns `necko 804b0033` (`NS_ERROR_UNKNOWN_SOCKET_TYPE`). The socket process stays off. See `docs/closed-route.zh-CN.md`.
- The repo does not gain a JA3 table, a second QUIC stack, a Windows DLL, or a filled `macos.url`. The profile tuples live only in the MPL file.

## Rejected

- A preference list in `necko_fetch.js` named as sixty versions. The control capture shows a preference changes something else.
- A ClientHello built in the fetch script, or a second TLS stack beside NSS.
- Sixty Firefox trees.
- Turning the socket process on to chase HTTP/3. That run already exits with signal 11.
