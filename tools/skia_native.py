"""Firefox's own Skia for canvas drawing.

A Firefox browser build's gfx/skia objects (the same machine code Firefox's
DrawTargetSkia runs) are archived into one static library, so the linker takes
only the members canvas drawing reaches. src/web_canvas/skia_target.cpp is the
one product unit compiled against the Skia headers of the same source tree.
"""
import hashlib
import json
from pathlib import Path

# The defines gfx/skia's own objects are built with that change Skia's headers
# (SkUserConfig.h supplies the rest, MOZ_SKIA included; mfbt comes from the
# donor's dist/include).
FLAGS = ['-DUNICODE', '-D_UNICODE', '-DNDEBUG=1', '-DTRIMMED=1']
UNIT = 'web_canvas_skia_target'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compile_flags(donor, root, directory):
    skia = donor.src / 'gfx/skia'
    return ['-std:c++20', '-MD', '-O2', '-GR-', '-D_HAS_EXCEPTIONS=0', '-Zc:sizedDealloc-',
            '-Xclang', '-ffp-contract=off', *FLAGS,
            '-I' + str(skia), '-I' + str(skia / 'skia'), '-I' + str(donor.obj / 'dist/include'),
            '-I' + str(root / 'src'), '-I' + str(directory)]


def objects_dir(gfx_obj):
    folder = Path(gfx_obj) / 'gfx/skia'
    objs = sorted(folder.glob('*.obj'))
    if not objs:
        raise ValueError(f'no Skia objects under {folder}: --firefox-gfx-obj must be a '
                         'Firefox browser build objdir (the one whose dist/bin/firefox.exe '
                         'is the reference)')
    return folder, objs


def inputs(donor, gfx_obj):
    """Files whose change must rebuild: the objects and the Skia headers."""
    _, objs = objects_dir(gfx_obj)
    headers = [p for p in (donor.src / 'gfx/skia/skia/include').rglob('*.h')]
    return objs + headers


def build(native, gfx_obj):
    folder, objs = objects_dir(gfx_obj)
    milestone = (Path(gfx_obj) / 'config/autoconf.mk')
    directory = native.directory / 'skia-native'
    directory.mkdir(parents=True, exist_ok=True)
    librarian, lib_prefix = native.librarian()
    identity = {'objects': {p.name: digest(p) for p in objs},
                'librarian': digest(librarian), 'builder': digest(__file__)}
    signature = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    library = directory / 'firefox_skia.lib'
    stamp = directory / 'build.json'
    if stamp.is_file() and library.is_file() and not native.incremental['disabled']:
        old = json.loads(stamp.read_text('utf8'))
        if old.get('signature') == signature and old.get('library') == digest(library):
            return [library]
    native.run([librarian, *lib_prefix, '-OUT:' + str(library), *map(str, objs)], 'archive_skia')
    version = None
    if milestone.is_file():
        for line in milestone.read_text('utf8', errors='replace').splitlines():
            if line.startswith('MOZILLA_VERSION ='):
                version = line.split('=', 1)[1].strip()
    stamp.write_text(json.dumps({'signature': signature, 'library': digest(library),
                                 'source': str(folder), 'firefoxVersion': version,
                                 'identity': identity}, indent=2) + '\n', 'utf8')
    return [library]
