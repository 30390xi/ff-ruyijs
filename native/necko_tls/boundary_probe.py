"""Library GET for the TLS cluster boundaries. Prints counts, status, and ALPN."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "native" / "necko_tls"))

from necko.core_client import CoreClient, _hello_shape  # noqa: E402
from necko_win._tls_profiles import PROFILES  # noqa: E402

VERSIONS = ("100", "119", "132", "135", "151", "154", "156", "159")


def expected(version):
    profile = PROFILES[version]
    shares = 1 + int(profile.additional_key_shares)
    return {
        "cipher_count": len(profile.ciphers),
        "compression_algorithms": len(profile.compression),
        "ech": bool(profile.grease_ech),
        "group_count": len(profile.groups),
        "key_share_count": shares,
        "signed_cert_timestamps": bool(profile.signed_cert_timestamps),
    }


def main():
    os.environ.pop("RUYIJS_HTTP_PROXY", None)
    client = CoreClient()
    if not client.available:
        print(json.dumps({"pass": False, "error": "library missing"}))
        return 1
    report = {"pass": True, "versions": {}}
    try:
        for version in VERSIONS:
            os.environ["NECKO_FIREFOX"] = version
            client.close()
            try:
                response = client.fetch("GET", "https://example.com/", [], b"", 20)
            except Exception as error:
                report["pass"] = False
                report["versions"][version] = {"error": type(error).__name__}
                break
            shape = _hello_shape(client.last_hello) or {}
            want = expected(version)
            got = {key: shape.get(key) for key in want}
            version_code = getattr(getattr(response, "raw", None), "version", 0)
            ok = response.status_code == 200 and version_code == 20 and got == want
            report["versions"][version] = {
                "alpn": "h2" if version_code == 20 else version_code,
                "match": ok,
                "status": response.status_code,
                **got,
            }
            if not ok:
                report["pass"] = False
                break
    finally:
        client.close()
    print(json.dumps(report, sort_keys=True))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
