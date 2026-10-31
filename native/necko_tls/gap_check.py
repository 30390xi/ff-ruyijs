"""Offline checks for authority, headers, HTTP/2 preface, and fallback.

Prints pass or fail only. Live network checks live in boundary_probe.py.
"""

from __future__ import annotations

import socket
import ssl
import sys
import tempfile
import threading
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))
sys.path.insert(0, str(REPO / "native" / "necko_tls"))

from necko.adapters import RoutingClient  # noqa: E402
from necko.client import NeckoResponse  # noqa: E402
from necko.core_client import (  # noqa: E402
    CoreClient,
    Unwired,
    _body_h1,
    _read_h1,
    failure_kind,
)
from necko_win._wire import (  # noqa: E402
    format_authority,
    merge_headers,
    open_connection,
    profile_flags,
    request_fields,
)
from ruyijs._context import macos_core_library  # noqa: E402


def _frame_types(data: bytes) -> list[int]:
    preface = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    if not data.startswith(preface):
        raise AssertionError("missing HTTP/2 preface")
    index = len(preface)
    found = []
    while index + 9 <= len(data):
        length = int.from_bytes(data[index : index + 3], "big")
        found.append(data[index + 3])
        index += 9 + length
    return found


def check_authority():
    if format_authority("example.com", 443, "https") != "example.com":
        raise AssertionError("default port leaked into authority")
    if format_authority("example.com", 8443, "https") != "example.com:8443":
        raise AssertionError("non-default port missing")
    if format_authority("::1", 8443, "https") != "[::1]:8443":
        raise AssertionError("IPv6 authority was not bracketed")
    if format_authority("::1", 443, "https") != "[::1]":
        raise AssertionError("IPv6 default port was kept")


def check_headers():
    current = merge_headers(
        "GET", "https://example.com/", [("User-Agent", "sentinel-ua")], "159", True, "example.com"
    )
    names = [name.lower() for name in current]
    if names[0] != "host" or current["Host"] != "example.com":
        raise AssertionError("Host is not the authority")
    if current["User-Agent"] != "sentinel-ua":
        raise AssertionError("caller User-Agent was replaced")
    if "accept-language" not in names or "priority" not in names or "te" not in names:
        raise AssertionError("profile headers were not filled")
    older = merge_headers("GET", "https://example.com/", [], "100", True, "example.com")
    if "Priority" in older:
        raise AssertionError("version 100 gained a Priority header")
    if "zstd" in older["Accept-Encoding"] or "zstd" not in current["Accept-Encoding"]:
        raise AssertionError("Accept-Encoding did not follow the profile")
    fields, document = request_fields("GET", "/", current, current["Host"])
    pseudo = [name.decode("ascii") for name, _value in fields[:4]]
    if pseudo != [":method", ":path", ":authority", ":scheme"] or not document:
        raise AssertionError("HTTP/2 pseudo-header order changed")


def check_preface():
    early = profile_flags("100")
    middle = profile_flags("132")
    current = profile_flags("159")
    if not early["priority_tree"] or early["push_disabled"] or early["no_rfc7540"]:
        raise AssertionError("version 100 flags")
    if not middle["push_disabled"] or not middle["no_rfc7540"] or middle["priority_tree"]:
        raise AssertionError("version 132 flags")
    if not current["push_disabled"] or current["priority_tree"] or current["no_rfc7540"]:
        raise AssertionError("version 159 flags")
    early_types = _frame_types(open_connection("100").data_to_send())
    current_types = _frame_types(open_connection("159").data_to_send())
    if 0x4 not in early_types or 0x2 not in early_types:
        raise AssertionError("version 100 preface missing settings or priority")
    if 0x4 not in current_types or 0x2 in current_types:
        raise AssertionError("version 159 preface did not drop the priority tree")


def check_h1_parser():
    def once(blob):
        box = [blob]

        def read(_size):
            if not box:
                raise ConnectionError("closed")
            return box.pop(0)

        status, listed, leftover = _read_h1(read)
        body = _body_h1(read, listed, leftover)
        return status, body

    status, body = once(b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello")
    if status != 200 or body != b"hello":
        raise AssertionError("HTTP/1.1 content-length parse failed")
    status, body = once(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n")
    if status != 200 or body != b"hello":
        raise AssertionError("HTTP/1.1 chunked parse failed")


def check_fallback():
    if failure_kind(Unwired("client certificate is not wired")) != "unwired":
        raise AssertionError("client certificate was not unwired")
    if failure_kind(ssl.SSLCertVerificationError("expired")) != "verify":
        raise AssertionError("verification failure would fall back")

    class FakeCore:
        available = True
        last_hello = b"core"

        def fetch(self, *args, **kwargs):
            raise Unwired("client certificate is not wired")

        def close(self):
            pass

    class FakeShell:
        def __init__(self):
            self.calls = 0
            self.last_hello = b"shell"

        def fetch(self, *args, **kwargs):
            self.calls += 1
            return NeckoResponse(200, b"ok", 11, [])

        def close(self):
            pass

    shell = FakeShell()
    router = RoutingClient()
    router._core = FakeCore()
    router._fallback = shell
    response = router.fetch("GET", "https://example.com/", [], b"", 5)
    if response.status_code != 200 or shell.calls != 1 or router.last_hello != b"shell":
        raise AssertionError("unwired request did not fall back")

    class VerifyCore:
        available = True
        last_hello = b""

        def fetch(self, *args, **kwargs):
            raise ssl.SSLCertVerificationError("expired")

        def close(self):
            pass

    shell = FakeShell()
    router = RoutingClient()
    router._core = VerifyCore()
    router._fallback = shell
    try:
        router.fetch("GET", "https://example.com/", [], b"", 5)
    except ssl.SSLCertVerificationError:
        pass
    else:
        raise AssertionError("verification failure returned a response")
    if shell.calls:
        raise AssertionError("verification failure used xpcshell")

    client = CoreClient()
    try:
        client.fetch("GET", "https://example.com/", [], b"", 1, cert="unused")
    except Unwired:
        pass
    else:
        raise AssertionError("client certificate entered the library")
    finally:
        client.close()


def check_socket_close():
    client = CoreClient()
    if not client.available:
        client.close()
        return
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)
    accepted = []

    def accept():
        conn, _addr = server.accept()
        accepted.append(conn)

    thread = threading.Thread(target=accept)
    thread.start()
    port = server.getsockname()[1]
    import necko.core_client as core_module

    original = core_module.TLS

    def boom(*_args, **_kwargs):
        raise ssl.SSLCertVerificationError("stop before handshake")

    core_module.TLS = boom
    try:
        try:
            client._connect("127.0.0.1", port, "159", 5)
        except ssl.SSLCertVerificationError:
            pass
        else:
            raise AssertionError("stand-in TLS did not fail")
        thread.join(2)
        if not accepted:
            raise AssertionError("local listener accepted nothing")
        accepted[0].settimeout(1)
        if accepted[0].recv(8) != b"":
            raise AssertionError("TCP socket stayed open after TLS setup failed")
    finally:
        core_module.TLS = original
        client.close()
        for conn in accepted:
            conn.close()
        server.close()


def check_library_lookup():
    with tempfile.TemporaryDirectory() as tmp:
        runtime = Path(tmp) / "_runtime"
        staged = runtime / "dist" / "necko" / "libnecko_core.dylib"
        staged.parent.mkdir(parents=True)
        staged.write_bytes(b"staged")
        if macos_core_library(runtime) != staged:
            raise AssertionError("staged runtime did not find dist/necko")
        fresh = runtime / "dist" / "necko_tls" / "libnecko_core.dylib"
        fresh.parent.mkdir()
        fresh.write_bytes(b"fresh")
        if macos_core_library(runtime) != fresh:
            raise AssertionError("dev tree did not prefer dist/necko_tls")


def main():
    check_authority()
    check_headers()
    check_preface()
    check_h1_parser()
    check_fallback()
    check_socket_close()
    check_library_lookup()
    print("pass")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"fail {error}")
        raise SystemExit(1)
