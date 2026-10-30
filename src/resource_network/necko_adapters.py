"""Necko/NSS session for the worker frames.

Direct HTTPS uses libnecko_core when that library reports NSS 3.131.
A proxy and any non-HTTPS URL keep the donor xpcshell. A request the
library cannot represent falls back for that request only. Certificate
verification failures stay on the library path. This process does not
store cookies and does not follow redirects.
"""

import os
from urllib.parse import urlsplit

from necko.client import NeckoClient


def _should_fallback(error):
    try:
        from necko.core_client import failure_kind
    except ImportError:
        return False
    return failure_kind(error) == "unwired"


class RoutingClient:
    def __init__(self):
        self.last_hello = b""
        self._core = None
        self._core_failed = False
        self._fallback = None

    def _xpcshell(self):
        if self._fallback is None:
            self._fallback = NeckoClient()
        return self._fallback

    def _library(self):
        if self._core_failed:
            return None
        if self._core is not None:
            return self._core
        try:
            from necko.core_client import CoreClient

            core = CoreClient()
        except (ImportError, OSError, RuntimeError):
            self._core_failed = True
            return None
        if not core.available:
            core.close()
            self._core_failed = True
            return None
        self._core = core
        return core

    def fetch(self, method, url, headers, body, budget_s):
        parts = urlsplit(url)
        use_core = (
            not os.environ.get("RUYIJS_HTTP_PROXY")
            and parts.scheme == "https"
            and bool(parts.hostname)
        )
        client = self._library() if use_core else None
        if client is None:
            client = self._xpcshell()
            use_core = False
        served = client
        try:
            response = client.fetch(method, url, headers, body, budget_s)
        except Exception as error:
            if not use_core or not _should_fallback(error):
                raise
            served = self._xpcshell()
            response = served.fetch(method, url, headers, body, budget_s)
        self.last_hello = getattr(served, "last_hello", b"") or b""
        return response

    def close(self):
        if self._core is not None:
            self._core.close()
        if self._fallback is not None:
            self._fallback.close()


class HTTPAdapter:
    def __init__(self):
        self.client = RoutingClient()

    def close(self):
        self.client.close()
