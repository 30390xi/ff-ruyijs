"""Clang/lld command construction with all writable outputs inside ruyijs."""

import hashlib
import json
import os
import shutil
import subprocess
import threading
from pathlib import Path


class Native:
    def __init__(self, donor, root, directory, args):
        self.donor, self.root, self.directory = donor, root, directory
        self.args = args
        self.env = os.environ.copy()
        self.env["INCLUDE"] = donor.include(args.vs_root)
        self.env["TEMP"] = self.env["TMP"] = str(directory)
        self.commands = []
        self.command_lock = threading.Lock()
        self.versions = {}
        self.incremental = {"hits": 0, "misses": 0,
                            "disabled": bool(getattr(args, "no_incremental", False)),
                            "reuse": bool(getattr(args, "reuse_objects", False))}
        self._stamps = {}

    def _stamp(self, path):
        # (size, mtime_ns) of one header, read once per build across all units.
        # Two threads may stat the same path; the results are identical.
        stamp = self._stamps.get(path, False)
        if stamp is False:
            try:
                stat = os.stat(path)
                stamp = [stat.st_size, stat.st_mtime_ns]
            except OSError:
                stamp = None
            self._stamps[path] = stamp
        return stamp

    def _dependencies(self, log_path):
        # clang-cl -showIncludes: record every included file, then drop the
        # notes so the log keeps only diagnostics.
        prefix = "Note: including file:"
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        deps = sorted({line[len(prefix):].strip() for line in lines if line.startswith(prefix)})
        log_path.write_text("\n".join(line for line in lines if not line.startswith(prefix)),
                            encoding="utf-8")
        return {path: self._stamp(path) for path in deps}

    def _cache_file(self, name):
        return self.directory / "incremental" / (name + ".json")

    def tool(self, name, configured):
        path = (Path(self.args.toolchain_bin) / name
                if self.args.toolchain_bin else Path(configured))
        # absolute() keeps lld-link.exe; resolve() follows it to the generic lld.exe driver.
        return str(path.absolute())

    def librarian(self):
        """Return (exe, prefix_args) for static-library archiving."""
        path = self.tool('llvm-lib.exe', 'llvm-lib.exe')
        if Path(path).is_file():
            return path, []
        alt = Path(path).parent / 'lld-link.exe'
        if alt.is_file():
            return str(alt.absolute()), ['/lib']
        return path, []

    def compiler(self):
        configured = self.donor.substs["CXX"]
        return [self.tool("clang-cl.exe", configured[0]), *configured[1:]]

    def linker(self):
        return self.tool("lld-link.exe", self.donor.substs["LINKER"])

    def check_tools(self, compile_needed):
        paths = [self.linker()]
        if compile_needed:
            paths.append(self.compiler()[0])
        for path in paths:
            version = subprocess.run([path, "--version"], env=self.env,
                                     cwd=self.directory, text=True,
                                     capture_output=True, check=True)
            self.versions[path] = version.stdout.strip()

    def run(self, command, name):
        with self.command_lock:
            self.commands.append(command)
            (self.directory / "commands.json").write_text(
                json.dumps(self.commands, indent=2), encoding="utf-8")
            print(name, flush=True)
        with (self.directory / f"{name}.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(command, env=self.env, cwd=self.directory,
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            tail = "\n".join(line for line in (self.directory / f"{name}.log").read_text(
                encoding="utf-8", errors="replace").splitlines()
                if not line.startswith("Note: including file:"))[-12000:]
            raise RuntimeError(f"{name} exited {result.returncode}\n{tail}")

    def compile(self, source, name, exceptions=False):
        output = self.directory / f"{name}.obj"
        if name == "web_canvas_skia_target":
            import skia_native
            flags = skia_native.compile_flags(self.donor, self.root, self.directory)
        elif name == "geometry_harfbuzz":
            from geometry_fonts import FLAGS
            flags = ["-std:c++20", "-MD", "-O2", "-EHsc", "-DNDEBUG", *FLAGS,
                     "-I" + str(self.donor.src / "gfx/harfbuzz/src")]
        else:
            flags = self.donor.flags("COMPUTED_CXXFLAGS")
            if exceptions:
                flags = [flag for flag in flags if flag not in
                         ("-D_HAS_EXCEPTIONS=0", "-fno-exceptions")]
                flags += ["-D_HAS_EXCEPTIONS=1", "-EHsc"]
            # js.cpp contains quoted includes relative to its original shell directory.
            flags += ["-I" + str(self.donor.src / "js/src/shell"),
                      "-I" + str(self.root / "src"), "-I" + str(self.directory)]
            if source.parent.name == "web_geometry":
                flags += ["-I" + str(self.donor.src / "gfx/harfbuzz/src")]
            if source.name == "compression_brotli.cpp":
                flags += ["-I" + str(self.root / "vendor/compression-native/brotli/include")]
        command = [*self.compiler(), *flags, "-showIncludes", "-c", str(source), "-Fo" + str(output)]
        signature = hashlib.sha256(json.dumps({
            "source": str(Path(source).resolve()),
            "source_sha256": hashlib.sha256(Path(source).read_bytes()).hexdigest(),
            "command": command,
        }, sort_keys=True).encode("utf-8")).hexdigest()
        cache = self._cache_file(name)
        if (self.incremental["reuse"] and output.is_file() and
                output.stat().st_mtime_ns >= Path(source).stat().st_mtime_ns):
            self.incremental["hits"] += 1
            return output
        if (not self.incremental["disabled"] and output.is_file() and cache.is_file()):
            try:
                record = json.loads(cache.read_text(encoding="utf-8"))
                deps = record.get("dependencies")
                if (record["signature"] == signature and deps is not None and
                        all(self._stamp(path) == stamp for path, stamp in deps.items())):
                    self.incremental["hits"] += 1
                    return output
            except (OSError, KeyError, ValueError, json.JSONDecodeError):
                pass
        self.incremental["misses"] += 1
        try:
            self.run(command, name)
        finally:
            deps = self._dependencies(self.directory / f"{name}.log")
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps({"signature": signature, "dependencies": deps}),
                         encoding="utf-8")
        return output

    def link(self, objects, executable):
        # Nothing recompiled and the executable is newer than every object.
        if (executable.is_file() and self.incremental["misses"] == 0 and
                not self.incremental["disabled"]):
            built = executable.stat().st_mtime_ns
            if all(Path(path).stat().st_mtime_ns <= built for path in objects):
                print("link (up to date)", flush=True)
                return False
        stem = executable.stem
        flags = ["-OUT:" + str(executable),
                 "-PDB:" + str(self.directory / (stem + ".pdb")),
                 "-IMPLIB:" + str(self.directory / (stem + ".lib")), "-MACHINE:X64"]
        for key in ("WIN32_CONSOLE_EXE_LDFLAGS", "WIN32_EXE_DEFAULT_LDFLAGS"):
            flags.extend(self.donor.substs[key])
        flags += self.donor.flags("COMPUTED_LDFLAGS")
        if any("CETCOMPAT" in flag.upper() for flag in flags):
            raise ValueError("standalone shell JIT requires CETCOMPAT disabled")
        flags += self.donor.libpaths(self.args.vs_root)
        flags += [str(path) for path in objects + self.donor.libraries()]
        flags += self.donor.flags("OS_LIBS")
        response = self.directory / "link.rsp"
        response.write_text("\n".join(subprocess.list2cmdline([arg])
                                     for arg in flags), encoding="utf-8")
        self.run([self.linker(), "@" + str(response)], "link")
        return True

    def deploy(self, destination):
        paths = [self.donor.obj / "dist/bin/mozglue.dll"]
        if self.args.runtime_dll_dir:
            source = Path(self.args.runtime_dll_dir)
            names = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
                     "msvcp140_atomic_wait.dll", "vcruntime140_threads.dll")
            paths += [source / name for name in names if (source / name).is_file()]
        for path in paths:
            shutil.copy2(path, destination / path.name)
        return paths
