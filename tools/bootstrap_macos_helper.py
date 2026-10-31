"""Create the macOS Necko helper venv for this checkout.

An existing venv is left in place. Missing packages are installed only from
a local wheel directory, with pip --no-index. Firefox is not downloaded.
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENV = ROOT / "vendor" / "necko-derived" / "python"
WHEELHOUSE = ROOT / "vendor" / "necko-derived" / "wheels"
# h2 and cryptography serve direct HTTPS through libnecko_core.
# backports.zstd is the Python 3.12 name the wheel's _nss.py imports.
# publicsuffixlist is the product header table's site check.
PACKAGES = (
    "requests",
    "brotli",
    "zstandard",
    "h2",
    "cryptography",
    "backports.zstd",
    "publicsuffixlist",
)
IMPORTS = {
    "requests": "requests",
    "brotli": "brotli",
    "zstandard": "zstandard",
    "h2": "h2",
    "cryptography": "cryptography",
    "backports.zstd": "backports.zstd",
    "publicsuffixlist": "publicsuffixlist",
}


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


def venv_python():
    for name in ("bin/python3", "python3"):
        candidate = VENV / name
        if candidate.is_file():
            return candidate
    return None


def missing_packages(python):
    script = "import importlib\nmissing=[]\n"
    for package, module in IMPORTS.items():
        script += (
            "try:\n"
            f"    importlib.import_module({module!r})\n"
            "except Exception:\n"
            f"    missing.append({package!r})\n"
        )
    script += "print('\\n'.join(missing))\n"
    result = subprocess.run(
        [str(python), "-c", script],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return list(PACKAGES)
    return [line for line in result.stdout.splitlines() if line]


def install_wheels(python, packages):
    if not packages:
        return
    wheels = sorted(WHEELHOUSE.glob("*.whl")) if WHEELHOUSE.is_dir() else []
    if not wheels:
        raise SystemExit(
            "helper venv is missing packages; put wheels in vendor/necko-derived/wheels"
        )
    pip = python.parent / "pip3"
    subprocess.run(
        [str(pip), "install", "--no-index", "--find-links", str(WHEELHOUSE), *packages],
        check=True,
    )


def main():
    if sys.platform != "darwin":
        print("The Necko helper bootstrap runs on macOS.", file=sys.stderr)
        return 1
    if (VENV / "pyvenv.cfg").is_file():
        python = venv_python()
        if python is not None:
            install_wheels(python, missing_packages(python))
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
    python = venv_python()
    try:
        install_wheels(python, list(PACKAGES))
    except (OSError, subprocess.CalledProcessError, SystemExit):
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
