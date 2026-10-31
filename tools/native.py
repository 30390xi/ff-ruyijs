"""Clang/lld command construction with all writable outputs inside ruyijs."""

import hashlib
import json
import os
import shutil
import subprocess
import struct
import sys
import threading
from pathlib import Path


class Native:
    def __init__(self, donor, root, directory, args):
        self.donor, self.root, self.directory = donor, root, directory
        self.args = args
        self.env = os.environ.copy()
        include = donor.include(args.vs_root)
        if include:
            self.env["INCLUDE"] = include
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
        if not self.donor.windows:
            path = self.tool("llvm-ar", "/usr/bin/ar")
            if not Path(path).is_file():
                path = "/usr/bin/ar"
            return path, ["rcs"]
        path = self.tool('llvm-lib.exe', 'llvm-lib.exe')
        if Path(path).is_file():
            return path, []
        alt = Path(path).parent / 'lld-link.exe'
        if alt.is_file():
            return str(alt.absolute()), ['/lib']
        return path, []

    def archive(self, library, members):
        tool, prefix = self.librarian()
        paths = [str(path) for path in members]
        if self.donor.windows:
            return [tool, *prefix, "-OUT:" + str(library), *paths]
        # Darwin ar stores an rlib as one non-Mach-O member and ranlib drops it.
        # Expand rlibs when the archive command runs, after rustc has written them.
        script = 'import subprocess, sys\nfrom pathlib import Path\nar, library_text, *member_text = sys.argv[1:]\nlibrary = Path(library_text)\nstaging = library.parent / (library.stem + "-members")\nstaging.mkdir(parents=True, exist_ok=True)\nfor old in list(staging.rglob("*")):\n    if old.is_file():\n        old.unlink()\nmagics = {b"\\xcf\\xfa\\xed\\xfe", b"\\xfe\\xed\\xfa\\xcf", b"\\xce\\xfa\\xed\\xfe", b"\\xfe\\xed\\xfa\\xce", b"\\xca\\xfe\\xba\\xbe"}\nobjects = []\nfor member_text in member_text:\n    member = Path(member_text)\n    if member.suffix != ".rlib":\n        objects.append(str(member))\n        continue\n    extract = staging / member.name\n    extract.mkdir(parents=True, exist_ok=True)\n    subprocess.run([ar, "x", str(member.resolve())], cwd=extract, check=True)\n    found = []\n    for obj in sorted(extract.iterdir()):\n        if not obj.is_file() or obj.suffix != ".o":\n            continue\n        if (obj.stat().st_mode & 0o444) != 0o444:\n            obj.chmod(0o644)\n        if obj.read_bytes()[:4] in magics:\n            found.append(str(obj))\n    if not found:\n        raise SystemExit("rlib has no mach-o objects: " + str(member))\n    objects.extend(found)\nif library.exists():\n    library.unlink()\nsubprocess.run([ar, "rcs", str(library), *objects], check=True)\n'
        return [sys.executable, "-c", script, tool, str(library), *paths]

    def static_library(self, directory, stem):
        return Path(directory) / (stem + self.donor.static_suffix)

    def rustc_base(self, rustc, edition):
        command = [rustc, "--edition=" + edition, "--target=" + self.donor.rust_target,
                   "-C", "opt-level=2", "-C", "panic=abort"]
        if self.donor.windows:
            command += ["-C", "target-feature=-crt-static"]
        return command

    def _c_sdk_flags(self):
        flags = []
        configured = self.donor.substs["CXX"]
        if "-isysroot" in configured:
            index = configured.index("-isysroot")
            flags.extend(configured[index:index + 2])
        if not self.donor.windows:
            flags.append("-mmacosx-version-min=26.0")
        return flags

    def compile_foreign(self, source, output, extra):
        compiler = self.compiler()[0]
        source = Path(source)
        if self.donor.windows:
            language = ["-TP", "-std:c++17"] if source.suffix == ".cc" else ["-TC", "-std:c11"]
            return [compiler, "-nologo", *language, "-MD", "-O2", "-Brepro", *extra,
                    "-c", str(source), "-Fo" + str(output)]
        # clang++ rejects -std=c11. C translation units use the sibling clang driver.
        # Mozbuild clang does not see the Mac SDK unless -isysroot is repeated here.
        if source.suffix in (".cc", ".cpp"):
            tool, standard = compiler, "-std=c++17"
            prefix = [*self._c_sdk_flags(), "-stdlib=libc++"]
        else:
            tool = str(Path(compiler).with_name("clang"))
            standard = "-std=c11"
            prefix = self._c_sdk_flags()
        return [tool, *prefix, "-O2", standard, *extra, "-c", str(source), "-o", str(output)]

    def compiler(self):
        configured = self.donor.substs["CXX"]
        tool_name = "clang-cl.exe" if self.donor.windows else "clang++"
        command = [self.tool(tool_name, configured[0]), *configured[1:]]
        # libc++ floating std::to_chars needs macOS 13.3 and std::from_chars needs 26.0.
        # COMPUTED_CXXFLAGS does not set the deployment target, so this sticks.
        if not self.donor.windows:
            command.append("-mmacosx-version-min=26.0")
        return command

    def linker(self):
        # Darwin's standalone config has no LINKER subst. The link uses clang++.
        if not self.donor.windows:
            return self.compiler()[0]
        return self.tool("lld-link.exe", self.donor.substs["LINKER"])

    def check_tools(self, compile_needed):
        paths = [self.compiler()[0]] if not self.donor.windows else [self.linker()]
        if compile_needed and self.donor.windows:
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

    def _makefile_deps(self, path):
        text = path.read_text(encoding="utf-8", errors="replace").replace("\\\n", " ")
        deps = []
        for line in text.splitlines():
            if ":" not in line:
                continue
            for token in line.split(":", 1)[1].split():
                if token:
                    deps.append(str(Path(token).resolve()))
        return {item: self._stamp(item) for item in sorted(set(deps))}

    def compile(self, source, name, exceptions=False):
        output = self.directory / f"{name}{self.donor.object_suffix}"
        if name == "web_canvas_skia_target":
            import skia_native
            flags = skia_native.compile_flags(self.donor, self.root, self.directory)
        elif name == "geometry_harfbuzz":
            from geometry_fonts import FLAGS
            include = "-I" + str(self.donor.src / "gfx/harfbuzz/src")
            if self.donor.windows:
                flags = ["-std:c++20", "-MD", "-O2", "-EHsc", "-DNDEBUG", *FLAGS, include]
            else:
                flags = ["-O2", "-fexceptions", "-DNDEBUG", *FLAGS, include]
        else:
            flags = self.donor.flags("COMPUTED_CXXFLAGS")
            if exceptions:
                flags = [flag for flag in flags if flag not in
                         ("-D_HAS_EXCEPTIONS=0", "-fno-exceptions")]
                if self.donor.windows:
                    flags += ["-D_HAS_EXCEPTIONS=1", "-EHsc"]
                else:
                    flags += ["-fexceptions"]
            # js.cpp contains quoted includes relative to its original shell directory.
            flags += ["-I" + str(self.donor.src / "js/src/shell"),
                      "-I" + str(self.root / "src"), "-I" + str(self.directory)]
            if source.parent.name == "web_geometry":
                flags += ["-I" + str(self.donor.src / "gfx/harfbuzz/src")]
            if source.name == "compression_brotli.cpp":
                flags += ["-I" + str(self.root / "vendor/compression-native/brotli/include")]
        depfile = self.directory / f"{name}.d"
        if self.donor.windows:
            command = [*self.compiler(), *flags, "-showIncludes", "-c", str(source), "-Fo" + str(output)]
        else:
            command = [*self.compiler(), *flags, "-c", str(source), "-o", str(output),
                       "-MMD", "-MF", str(depfile)]
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
            if self.donor.windows:
                deps = self._dependencies(self.directory / f"{name}.log")
            else:
                deps = self._makefile_deps(depfile) if depfile.is_file() else {}
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps({"signature": signature, "dependencies": deps}),
                         encoding="utf-8")
        return output



    def _archive_symdef(self, archive):
        names = set()
        nul = bytes([0])
        for name, member in self._walk_archive(Path(archive).read_bytes()):
            if name not in (b"__.SYMDEF", b"__.SYMDEF SORTED"):
                continue
            table_bytes = struct.unpack_from("<I", member, 0)[0]
            if table_bytes % 8:
                raise RuntimeError("bad archive index: " + str(archive))
            str_at = 4 + table_bytes
            str_size = struct.unpack_from("<I", member, str_at)[0]
            strings = member[str_at + 4:str_at + 4 + str_size]
            cursor = 0
            while cursor < len(strings):
                end = strings.find(nul, cursor)
                if end < 0:
                    break
                if end > cursor:
                    names.add(bytes(strings[cursor:end]))
                cursor = end + 1
            return names
        raise RuntimeError("archive has no symbol index: " + str(archive))

    def _walk_archive(self, data):
        if not data.startswith(b"!<arch>" + bytes([10])):
            raise RuntimeError("not an archive")
        off = 8
        nul = bytes([0])
        while off + 60 <= len(data):
            header = data[off:off + 60]
            size_text = header[48:58].strip()
            if not size_text:
                break
            size = int(size_text)
            name = header[:16]
            body = data[off + 60:off + 60 + size]
            if name.startswith(b"#1/"):
                namelen = int(name[3:].strip() or b"0")
                member_name = body[:namelen].split(nul, 1)[0]
                member = body[namelen:]
            else:
                member_name = name.strip()
                member = body
            yield member_name, member
            off += 60 + size
            if off % 2:
                off += 1

    def _localize_overlap(self, obj, shared):
        magic = bytes.fromhex("cffaedfe")
        if obj[:4] != magic:
            return obj, 0
        nul = bytes([0])
        ncmds = struct.unpack_from("<I", obj, 16)[0]
        off = 32
        symoff = nsyms = stroff = strsize = None
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", obj, off)
            if cmd == 2:
                symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", obj, off + 8)
            off += cmdsize
        if symoff is None:
            return obj, 0
        defined = []
        for index in range(nsyms):
            ent = symoff + index * 16
            n_strx, n_type = struct.unpack_from("<IB", obj, ent)
            if not n_strx or n_strx >= strsize:
                continue
            if not (n_type & 1) or (n_type & 0x0E) != 0x0E:
                continue
            end = obj.index(nul, stroff + n_strx)
            defined.append((ent, obj[stroff + n_strx:end]))
        names = {item for _, item in defined}
        overlap = names & shared
        if not overlap or names <= shared:
            return obj, 0
        buf = bytearray(obj)
        for ent, item in defined:
            if item in overlap:
                buf[ent + 4] = buf[ent + 4] & 0xFE
        return bytes(buf), len(overlap)

    def _hide_jsrust_overlap(self, archive):
        """Keep unique std symbols global and hide names libjsrust also defines.

        std's main object is pulled for panic and formatting, and that same
        object defines rust_eh_personality and EMPTY_PANIC. libjsrust's shell
        object defines them too. Localizing only the shared names in a mixed
        object removes the duplicate without dropping the std-only symbols.
        """
        matches = [Path(item) for item in self.donor.libraries()
                   if str(item).endswith("libjsrust.a")]
        if len(matches) != 1:
            raise RuntimeError("Darwin rust runtime expected one libjsrust.a")
        shared = self._archive_symdef(matches[0])
        magic = bytes.fromhex("cffaedfe")
        members = []
        changed = 0
        for name, member in self._walk_archive(Path(archive).read_bytes()):
            if member[:4] != magic:
                continue
            patched, count = self._localize_overlap(member, shared)
            changed += count
            members.append((name, patched))
        if not changed:
            return
        staging = Path(archive).parent / (Path(archive).stem + "-hidden")
        staging.mkdir(parents=True, exist_ok=True)
        for old in staging.iterdir():
            if old.is_file():
                old.unlink()
        paths = []
        for name, blob in members:
            text_name = name.decode("ascii")
            if "/" in text_name or len(text_name) > 240:
                raise RuntimeError("unexpected archive member name: " + text_name)
            dest = staging / text_name
            dest.write_bytes(blob)
            paths.append(str(dest))
        tool, _prefix = self.librarian()
        Path(archive).unlink()
        self.run([tool, "rcs", str(archive), *paths], "rust_std_hide")

    def darwin_rust_runtime(self):
        """Std and the allocator shim for Darwin rlibs.

        Donor libjsrust keeps Rust runtime symbols private. The sysroot rlibs
        define core/std, and one staticlib object defines the system allocator.
        """
        directory = self.directory / "rust-runtime"
        directory.mkdir(parents=True, exist_ok=True)
        archive = directory / "librust_std.a"
        shim = directory / "rust_alloc_shim.o"
        stamp = directory / "build.json"
        rustc = self.donor.substs["RUSTC"]
        if isinstance(rustc, list):
            rustc = rustc[0]
        version = subprocess.run(
            [rustc, "--version"], check=True, capture_output=True, text=True, env=self.env
        ).stdout.strip()
        libdir = Path(subprocess.run(
            [rustc, "--print", "target-libdir"], check=True, capture_output=True,
            text=True, env=self.env,
        ).stdout.strip())
        rlibs = sorted(
            item for item in libdir.glob("*.rlib")
            if "panic_unwind" not in item.name and "proc_macro" not in item.name
        )
        if not rlibs:
            raise RuntimeError("rust sysroot has no rlibs: " + str(libdir))
        signature = hashlib.sha256(json.dumps({
            "rustc": version,
            "libs": [(item.name, item.stat().st_size) for item in rlibs],
            "hide": "shared-object",
        }, sort_keys=True).encode("utf-8")).hexdigest()
        cached = False
        if stamp.is_file() and archive.is_file():
            try:
                cached = json.loads(stamp.read_text(encoding="utf-8")).get("signature") == signature
            except (OSError, ValueError, json.JSONDecodeError):
                cached = False
        if cached and shim.is_file():
            return [archive, shim]
        if not cached:
            self.run(self.archive(archive, rlibs), "rust_std")
            self._hide_jsrust_overlap(archive)
            stamp.write_text(json.dumps({"signature": signature}) + "\n", encoding="utf-8")
        source = directory / "alloc_shim.rs"
        source.write_text(
            "// rustc emits the system allocator symbols into this crate object.\n",
            encoding="utf-8",
        )
        staticlib = directory / "alloc_shim_static.a"
        self.run(
            [*self.rustc_base(rustc, "2021"), "--crate-name=ruyijs_alloc_shim",
             "--crate-type=staticlib", str(source), "-o", str(staticlib)],
            "rust_alloc_shim",
        )
        self._extract_alloc_shim(staticlib, shim)
        return [archive, shim]

    def _extract_alloc_shim(self, staticlib, output):
        data = Path(staticlib).read_bytes()
        if not data.startswith(b"!<arch>\n"):
            raise RuntimeError("alloc shim staticlib is not an archive")
        magics = {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
                  b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xce", b"\xca\xfe\xba\xbe"}
        off = 8
        chosen = None
        while off + 60 <= len(data):
            header = data[off:off + 60]
            size_text = header[48:58].strip()
            if not size_text:
                break
            size = int(size_text)
            name = header[:16]
            body = data[off + 60:off + 60 + size]
            if name.startswith(b"#1/"):
                namelen = int(name[3:].strip() or b"0")
                member_name = body[:namelen].split(b"\x00", 1)[0]
                member = body[namelen:]
            else:
                member_name = name.split(b"/", 1)[0].strip()
                member = body
            if (member_name.endswith(b".o") and b"no_alloc_shim" in member and
                    member[:4] in magics):
                if chosen is None or len(member) < len(chosen[1]):
                    chosen = (member_name, member)
            off += 60 + size
            if off % 2:
                off += 1
        if chosen is None:
            raise RuntimeError("alloc shim object missing from " + str(staticlib))
        if len(chosen[1]) > 262144:
            raise RuntimeError("alloc shim object is unexpectedly large: " + str(len(chosen[1])))
        Path(output).write_bytes(chosen[1])

    def link(self, objects, executable):
        # Nothing recompiled and the executable is newer than every object.
        if (executable.is_file() and self.incremental["misses"] == 0 and
                not self.incremental["disabled"]):
            built = executable.stat().st_mtime_ns
            if all(Path(path).stat().st_mtime_ns <= built for path in objects):
                print("link (up to date)", flush=True)
                return False
        if not self.donor.windows:
            # compiler() keeps -isysroot and -stdlib=libc++. Passing only the
            # clang++ path made ld64.lld miss libSystem, libc++, and libresolv.
            flags = ["-o", str(executable), *self._c_sdk_flags(), "-stdlib=libc++"]
            flags += self.donor.flags("COMPUTED_LDFLAGS")
            flags += self.donor.libpaths(self.args.vs_root)
            flags += [str(path) for path in objects + self.donor.libraries()]
            flags += self.donor.flags("OS_LIBS")
            # libmozglue.dylib's install name is @rpath/libmozglue.dylib.
            flags.append("-Wl,-rpath,@executable_path")
            flags.append("-mmacosx-version-min=26.0")
            # libjsrust does not export core/std/alloc. Satisfy rlibs from the
            # matching rustc sysroot, then the donor archive.
            runtime = [str(item) for item in self.darwin_rust_runtime()]
            indexes = [i for i, arg in enumerate(flags) if str(arg).endswith("libjsrust.a")]
            if len(indexes) != 1:
                raise RuntimeError("Darwin link expected one libjsrust.a")
            flags[indexes[0]:indexes[0]] = runtime
            flags += ["-framework", "CoreFoundation", "-framework", "CoreGraphics",
                      "-framework", "CoreText", "-framework", "ImageIO", "-framework", "Security"]
            self.run([self.compiler()[0], *flags], "link")
            return True
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
        paths = [self.donor.obj / "dist/bin" / self.donor.glue_name]
        if self.donor.windows and self.args.runtime_dll_dir:
            source = Path(self.args.runtime_dll_dir)
            names = ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
                     "msvcp140_atomic_wait.dll", "vcruntime140_threads.dll")
            paths += [source / name for name in names if (source / name).is_file()]
        for path in paths:
            shutil.copy2(path, destination / path.name)
        return paths
