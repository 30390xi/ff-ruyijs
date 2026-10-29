"""Build the transport-only C ABI library, without any SpiderMonkey linkage."""

import ctypes
import os
import shutil
import subprocess

from provenance import digest


def build_client(native, destination):
    sources = sorted((native.root / "src/client").glob("*.cpp"))
    if not sources:
        raise ValueError("src/client must contain implementation files")
    objects = []
    # The client is self-contained: any change under src/client rebuilds it.
    newest = max(path.stat().st_mtime_ns for path in (native.root / "src/client").iterdir()
                 if path.is_file())
    for source in sources:
        name = "client_" + source.stem
        output = native.directory / (name + native.donor.object_suffix)
        if native.donor.windows:
            command = [*native.compiler(), "-MD", "-EHsc", "-W3", "-O2", "-Z7",
                       "-guard:cf", "-c", str(source), "-Fo" + str(output)]
        else:
            command = [*native.compiler(), "-std=c++17", "-O2", "-fvisibility=hidden",
                       "-c", str(source), "-o", str(output)]
        if not output.is_file() or output.stat().st_mtime_ns < newest:
            native.run(command, name)
        objects.append(output)
    output = destination / ("ruyijs_client.dll" if native.donor.windows else "libruyijs_client.dylib")
    current = output.is_file() and all(
        path.stat().st_mtime_ns <= output.stat().st_mtime_ns for path in objects)
    if native.donor.windows:
        flags = ["-DLL", "-OUT:" + str(output), "-MACHINE:X64", "-DEBUG",
                 "-OPT:REF,ICF", "-guard:cf", "-PDB:" +
                 str(native.directory / "ruyijs_client.pdb"), "-IMPLIB:" +
                 str(native.directory / "ruyijs_client.lib")]
        flags += native.donor.libpaths(native.args.vs_root)
        flags += [str(path) for path in objects] + ["kernel32.lib", "bcrypt.lib"]
        response = native.directory / "link-client.rsp"
        response.write_text("\n".join(subprocess.list2cmdline([arg]) for arg in flags),
                            encoding="utf-8")
        if not current:
            native.run([native.linker(), "@" + str(response)], "link-client")
        include = destination / "include"
        include.mkdir(exist_ok=True)
        shutil.copy2(native.root / "src/client/ruyijs.h", include / "ruyijs.h")
        shutil.copy2(native.directory / "ruyijs_client.lib", destination)
        with os.add_dll_directory(str(destination)):
            library = ctypes.WinDLL(str(output))
            exports = _exports(library)
    else:
        if not current:
            native.run([native.compiler()[0], *native._c_sdk_flags(), "-stdlib=libc++",
                        "-fvisibility=hidden", "-dynamiclib",
                        "-o", str(output), *[str(path) for path in objects]],
                       "link-client")
        include = destination / "include"
        include.mkdir(exist_ok=True)
        shutil.copy2(native.root / "src/client/ruyijs.h", include / "ruyijs.h")
        library = ctypes.CDLL(str(output))
        exports = _exports(library)
    return {"path": str(output), "sha256": digest(output), "exports": exports}


def _exports(library):
    exports = ["ruyijs_create", "ruyijs_create_mode", "ruyijs_call",
               "ruyijs_free", "ruyijs_close",
               "ruyijs_set_callback", "ruyijs_alloc"]
    for name in exports:
        getattr(library, name)
    return exports
