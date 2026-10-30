"""Page smoke for the default profile and tls=100. Prints status and protocol only."""

import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))
os.environ.pop("RUYIJS_HTTP_PROXY", None)
log = Path("/tmp/ruyi-gap/page-hello.jsonl")
if log.exists():
    log.unlink()
os.environ["NECKO_CORE_HELLO_LOG"] = str(log)

from ruyijs import Context


def xpcshell_count():
    result = subprocess.run(["pgrep", "-x", "xpcshell"], capture_output=True, text=True)
    return len([line for line in result.stdout.splitlines() if line.strip()])


def value(raw):
    if isinstance(raw, dict) and "value" in raw:
        return raw["value"]
    return raw


def run(version):
    context = Context() if version == "159" else Context(tls=version)
    try:
        context.configure_clock("system")
        context.navigate("https://example.com/")
        context.advance(8000)
        ready = value(context.eval("document.readyState"))
        proto = value(context.eval("performance.getEntriesByType('navigation')[0].nextHopProtocol"))
        status = value(context.eval("performance.getEntriesByType('navigation')[0].responseStatus"))
        shells = xpcshell_count()
    finally:
        context.close()
    return {"proto": proto, "ready": ready, "shells": shells, "status": status, "tls": version}


def main():
    report = {"runs": [run("159"), run("100")]}
    shapes = []
    if log.exists():
        for line in log.read_text(encoding="utf-8").splitlines():
            item = json.loads(line)
            shapes.append({
                "cipher_count": item.get("cipher_count"),
                "group_count": item.get("group_count"),
                "key_share_count": item.get("key_share_count"),
                "tls": item.get("tls"),
            })
    report["shapes"] = shapes
    report["pass"] = (
        all(item["status"] == 200 and item["proto"] == "h2" and item["ready"] == "complete"
            and item["shells"] == 0 for item in report["runs"])
        and any(item["tls"] == "100" and item["cipher_count"] == 17 for item in shapes)
        and any(item["tls"] == "159" and item["cipher_count"] == 15 for item in shapes)
    )
    print(json.dumps(report, sort_keys=True))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
