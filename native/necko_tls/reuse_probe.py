"""Two GETs on one client should share one ClientHello."""

import json
import os
from pathlib import Path

os.environ.pop("RUYIJS_HTTP_PROXY", None)
os.environ["NECKO_FIREFOX"] = "159"
log = Path("/tmp/ruyi-gap/reuse.jsonl")
if log.exists():
    log.unlink()
os.environ["NECKO_CORE_HELLO_LOG"] = str(log)

from necko.core_client import CoreClient

client = CoreClient()
try:
    first = client.fetch("GET", "https://example.com/", [], b"", 20)
    second = client.fetch("GET", "https://example.com/", [], b"", 20)
finally:
    client.close()
lines = log.read_text(encoding="utf-8").splitlines() if log.exists() else []
report = {
    "hellos": len(lines),
    "pass": first.status_code == 200 and second.status_code == 200 and len(lines) == 1,
    "statuses": [first.status_code, second.status_code],
}
print(json.dumps(report))
raise SystemExit(0 if report["pass"] else 1)
