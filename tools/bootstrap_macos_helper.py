"""Create the macOS Necko helper venv for this checkout.

An existing venv is left in place. Firefox is not downloaded.
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENV = ROOT / "vendor" / "necko-derived" / "python"
PACKAGES = ("requests", "brotli", "zstandard")


def python312():
    found = shutil.which("python3.12")
    if found:
        return Path(found)
    candidates = (
        Path.home() / ".local" / "bin" / "python3.12",
        Path("/opt/homebrew/bin/python3.12"),
        Path("/usr/local/bin/python3.12"),
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def main():
    if sys.platform != "darwin":
        print("The Necko helper bootstrap runs on macOS.", file=sys.stderr)
        return 1
    if (VENV / "pyvenv.cfg").is_file():
        print("helper venv already exists")
        return 0
    binary = python312()
    if binary is None:
        print(
            "Python 3.12 was not found; the Necko helper venv was not created.",
            file=sys.stderr,
        )
        return 1
    VENV.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(binary), "-m", "venv", str(VENV)], check=True)
    pip = VENV / "bin" / "pip3"
    try:
        subprocess.run([str(pip), "install", *PACKAGES], check=True)
    except (OSError, subprocess.CalledProcessError):
        shutil.rmtree(VENV)
        raise
    print(VENV)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
