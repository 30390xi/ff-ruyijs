# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""HTTP header order and HTTP/2 connection setup from the product table.

The QUIC profile inside ``_firefox`` is not used here.
"""

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.settings import Settings
from hpack import Encoder
from hpack.hpack import INDEX_INCREMENTAL, INDEX_NEVER
from requests.structures import CaseInsensitiveDict

from necko_win._firefox import (
    PROFILES,
    apply_headers,
    default_headers,
    firefox_version,
    wire_headers,
)

# Idle dependency tree sent before the first request on older profiles.
_PRIORITY_TREE = (
    (3, 0, 201),
    (5, 0, 101),
    (7, 0, 1),
    (9, 7, 1),
    (11, 3, 1),
    (13, 0, 241),
)

_SKIP = {
    "host",
    "connection",
    "proxy-connection",
    "keep-alive",
    "upgrade",
    "transfer-encoding",
}


class _HeaderRequest:
    def __init__(self, method, url, headers, explicit):
        self.method = method
        self.url = url
        self.headers = headers
        self._firefox_explicit = explicit


class FirefoxEncoder(Encoder):
    @Encoder.header_table_size.setter
    def header_table_size(self, value):
        self.header_table.maxsize = value
        self.header_table.resized = True
        minimum = (
            min(value, *self.table_size_changes) if self.table_size_changes else value
        )
        self.table_size_changes = [minimum, value] if minimum < value else [value]

    def add(self, to_add, sensitive, huffman=False):
        name, value = to_add
        never = name == b"authorization" or name == b"cookie" and len(value) < 20
        plain = (
            name == b":path"
            or len(name) + len(value) + 32 > self.header_table_size // 2
            or self.header_table_size < 128
        )
        entries = (*self.header_table.STATIC_TABLE, *self.header_table.dynamic_entries)
        reference = 0
        matched = None
        for index, (entry_name, entry_value) in enumerate(entries, 1):
            if entry_name == name:
                reference = index
                if entry_value == value:
                    matched = index
                    break
        if matched and not never and not plain:
            return self._encode_indexed(matched)
        mode = INDEX_NEVER if never else b"\x00" if plain else INDEX_INCREMENTAL
        encoded = (
            self._encode_indexed_literal(reference, value, mode, True)
            if reference
            else self._encode_literal(name, value, mode, True)
        )
        if not never and not plain:
            self.header_table.add(name, value)
        return encoded


def format_authority(hostname, port, scheme):
    host = hostname
    if ":" in host and not host.startswith("["):
        host = f"[{host}]"
    default = 443 if scheme == "https" else 80
    if port and int(port) != default:
        return f"{host}:{int(port)}"
    return host


def merge_headers(method, url, headers, version, http2, authority=None):
    """Fill profile headers the caller left empty. Caller values stay."""
    version = firefox_version(version)
    caller = CaseInsensitiveDict(headers or [])
    dest = str(caller.get("Sec-Fetch-Dest", ""))
    context = "navigation" if dest in ("", "document") else "fetch"
    merged = default_headers(context, version)
    for name, value in caller.items():
        merged[name] = value
    if authority and "Host" not in caller:
        merged["Host"] = authority
    request = _HeaderRequest(method, url, merged, set(caller.keys()))
    apply_headers(request, context, None, True, version)
    return wire_headers(request.headers, http2, method)


def profile_flags(version):
    profile = PROFILES[firefox_version(version)]
    settings = tuple(profile["h2_settings"])
    return {
        "no_rfc7540": any(code == 9 for code, _value in settings),
        "priority_tree": bool(profile["h2_priority_tree"]),
        "push_disabled": any(code == 2 and value == 0 for code, value in settings),
        "settings_count": len(settings),
    }


def open_connection(version):
    version = firefox_version(version)
    profile = PROFILES[version]
    connection = H2Connection(H2Configuration(client_side=True, header_encoding=None))
    connection.encoder = FirefoxEncoder()
    wanted = dict(profile["h2_settings"])
    connection.local_settings = Settings(client=True, initial_values=wanted)
    for code in list(connection.local_settings):
        if code not in wanted:
            del connection.local_settings[code]
    connection.decoder.max_allowed_table_size = 65536
    connection.initiate_connection()
    connection.increment_flow_control_window(12517377)
    if profile["h2_priority_tree"]:
        for stream, depends_on, weight in _PRIORITY_TREE:
            connection.prioritize(stream, weight=weight, depends_on=depends_on)
    return connection


def request_fields(method, path, wired, authority):
    fields = [
        (b":method", method.encode("ascii")),
        (b":path", path.encode("ascii")),
        (b":authority", authority.encode("ascii")),
        (b":scheme", b"https"),
    ]
    excluded = set(_SKIP)
    connection = wired.get("Connection", "")
    excluded.update(part.strip().lower() for part in str(connection).split(",") if part.strip())
    document = str(wired.get("Sec-Fetch-Dest", "")) == "document"
    for name, value in wired.items():
        if name.lower() in excluded:
            continue
        encoded = value.encode("latin-1") if isinstance(value, str) else value
        if name.lower() == "cookie":
            fields.extend((b"cookie", part) for part in encoded.split(b"; "))
        else:
            fields.append((name.lower().encode("ascii"), encoded))
    return fields, document


def send_request(connection, version, fields, end_stream, document):
    profile = PROFILES[firefox_version(version)]
    first = 15 if profile["h2_priority_tree"] else 3
    stream_id = max(first, connection.get_next_available_stream_id())
    depends_on = (13 if document else 11) if profile["h2_priority_tree"] else 0
    connection.send_headers(
        stream_id,
        fields,
        end_stream=end_stream,
        priority_weight=42 if document else 22,
        priority_depends_on=depends_on,
        priority_exclusive=False,
    )
    connection.increment_flow_control_window(12582912 - 131072, stream_id)
    return stream_id
