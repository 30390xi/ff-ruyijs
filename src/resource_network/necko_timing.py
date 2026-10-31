"""Timing trailer consumed by oneshot.py.

--timing-v2 reads the request, response, and header instants.
--timing-v3 appends DNS, connect, and TLS observations in the order
ReadNeckoResponse reads them. This adapter records only the first three.
Unmeasured phases stay 0, and the navigation clock treats 0 as absent.
"""

FIELDS = (
    "request_ms",
    "response_ms",
    "headers_ms",
    "dns_start_ms",
    "dns_end_ms",
    "connect_start_ms",
    "secure_ms",
    "connect_end_ms",
)


def snapshot(control, fields=None):
    if fields is None:
        return (float(control.request_ms), float(control.response_ms), float(control.headers_ms))
    return tuple(float(getattr(control, name, 0.0) or 0.0) for name in fields)
