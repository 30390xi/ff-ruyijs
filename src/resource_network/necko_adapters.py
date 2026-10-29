"""Necko/NSS session for the worker frames.

The donor xpcshell owns TLS, HTTP/2, and redirect policy. This process does
not store cookies and does not follow redirects.
"""
from necko.client import NeckoClient


class HTTPAdapter:
    def __init__(self):
        self.client = NeckoClient()

    def close(self):
        self.client.close()
