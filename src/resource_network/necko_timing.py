"""Timing trailer consumed by oneshot.py when --timing-v2 is set."""


def snapshot(control):
    return (float(control.request_ms), float(control.response_ms), float(control.headers_ms))
