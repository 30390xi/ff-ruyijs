"""Offline smoke test for an installed ruyijs wheel (run by .github/workflows/ci.yml).

Exercises the public API against the bundled runtime without network access:
evaluation, value conversion, JS exceptions, page loading with offline
resources, the event loop, and the Firefox identity the engine reports.
Exit status is non-zero on the first failed check.
"""
import sys

from ruyijs import Context, JSError


def check(label, condition, detail=""):
    print(("ok   " if condition else "FAIL ") + label + (f"  ({detail})" if detail else ""), flush=True)
    if not condition:
        sys.exit(1)


def main():
    with Context(timeout=60) as ctx:
        check("eval arithmetic", ctx.eval("40 + 2") == 42)
        ctx.eval("globalThis.counter = 1")
        check("state persists across eval", ctx.eval("counter + 1") == 2)
        check("objects convert to Python", ctx.eval("({a: [1, 'x', true]})") == {"a": [1, "x", True]})

        ua = ctx.eval("navigator.userAgent")
        check("Firefox user agent", "Firefox/" in ua and "Gecko/" in ua, ua)
        check("SpiderMonkey-only API present", ctx.eval("typeof InternalError") == "function")

        try:
            ctx.eval("throw new TypeError('boom')")
            check("JS exception raises JSError", False)
        except JSError as error:
            check("JS exception raises JSError", error.name == "TypeError" and "boom" in error.message)

        ctx.set("payload", {"n": 3})
        check("set() injects data", ctx.eval("payload.n * 2") == 6)

        ctx.put_resource("https://smoke.test/app.js", "window.loaded = document.title + ':' + location.host;")
        ctx.load_page("https://smoke.test/", "<!doctype html><title>smoke</title><script src='/app.js'></script>")
        check("page loads offline script", ctx.eval("window.loaded") == "smoke:smoke.test")
        check("DOM is available", ctx.eval("document.querySelectorAll('script').length") == 1)

        ctx.eval("globalThis.fired = 0; setTimeout(() => { fired = 1 }, 50)")
        ctx.advance(100)
        check("timers run on advance()", ctx.eval("fired") == 1)
    print("all smoke checks passed")


if __name__ == "__main__":
    main()
