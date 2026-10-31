"""Certificate failure must not become a successful response."""

import os

os.environ.pop("RUYIJS_HTTP_PROXY", None)
os.environ["NECKO_FIREFOX"] = "159"

from necko.adapters import RoutingClient
from necko.core_client import failure_kind

router = RoutingClient()
try:
    response = router.fetch("GET", "https://expired.badssl.com/", [], b"", 20)
    print("open", response.status_code)
    raise SystemExit(1)
except Exception as error:
    kind = failure_kind(error)
    print("closed", type(error).__name__, kind)
    raise SystemExit(0 if kind == "verify" else 1)
finally:
    router.close()
