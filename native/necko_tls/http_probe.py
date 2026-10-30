"""Compare plain HTTP on the old client and the router. Prints the nsresult only."""

import os
import sys

from necko.adapters import RoutingClient
from necko.client import NeckoClient

kind = sys.argv[1]
url = "http://example.com/"
client = NeckoClient() if kind == "necko" else RoutingClient()
try:
    response = client.fetch("GET", url, [], b"", 20)
    print(kind, "status", response.status_code, "version", response.raw.version)
    raise SystemExit(0)
except Exception as error:
    text = str(error)
    marker = "necko "
    code = text[text.find(marker) + len(marker):].split()[0] if marker in text else type(error).__name__
    print(kind, "error", code)
    raise SystemExit(0)
finally:
    client.close()
