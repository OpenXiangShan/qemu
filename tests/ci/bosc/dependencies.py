"""Restore fixed native dependencies for a fresh checkout. GPL-2.0-or-later."""
import configparser
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile

from assets import prepare
from common import HERE, SOURCE, locked, read_json, sha256, write_json


def safe_members(archive, name):
    members = archive.getmembers()
    names = set()
    total = 0
    for member in members:
        path = PurePosixPath(member.name)
        if (path.is_absolute() or '..' in path.parts or not path.parts
                or path.parts[0] != name or not (member.isfile() or member.isdir())
                or member.name in names):
            raise ValueError("unsafe dependency archive entry")
        names.add(member.name)
        total += member.size
        if total > 64 * 1024 * 1024:
            raise ValueError("dependency archive exceeds extraction bound")
    return members


def prepare_dependencies(work, cases, assets_dir=None, cache_dir=None, source=SOURCE):
    if not cases:
        return
    specs = read_json(HERE / 'dependencies.lock.json')['assets']
    subprojects = source / 'subprojects'
    evidence = {}
    with locked(subprojects / '.bosc-ci-dependencies.lock'):
        for name, spec in specs.items():
            wrap = configparser.ConfigParser()
            wrap.read(subprojects / (name + '.wrap'))
            if wrap['wrap-git']['revision'] != spec['revision']:
                raise ValueError(f'{name}: wrap revision changed; publish and review a new dependency lock')
            target = subprojects / name
            overlays = subprojects / 'packagefiles' / name
            # Existing developer Git checkout: validate, never reset/overwrite.
            if (target / '.git').exists():
                revision = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], timeout=10).decode().strip()
                dirty = subprocess.check_output(['git', '-C', str(target), 'diff', '--name-only', 'HEAD'], timeout=10)
                if revision != spec['revision'] or dirty:
                    raise ValueError(f'{name}: existing checkout differs from locked revision')
                if overlays.is_dir():
                    for patch in overlays.rglob('*'):
                        if patch.is_file():
                            dest = target / patch.relative_to(overlays)
                            if dest.exists() and sha256(dest) != sha256(patch):
                                raise ValueError(f'{name}: existing Meson overlay differs')
                evidence[name] = {'revision': revision, 'origin': 'existing-verified-git'}
                continue
            paths = prepare(work, [{'group': 'native-deps-' + name, 'assets': [name]}],
                            assets_dir=assets_dir, cache_dir=cache_dir, specs=specs)
            with tempfile.TemporaryDirectory(prefix='bosc-dep-', dir=subprojects) as tmp:
                temp = Path(tmp)
                with tarfile.open(paths[name]) as archive:
                    for member in safe_members(archive, name):
                        dest = temp / member.name
                        if member.isdir():
                            dest.mkdir(parents=True, exist_ok=True)
                        else:
                            dest.parent.mkdir(parents=True, exist_ok=True)
                            with archive.extractfile(member) as src, dest.open('wb') as out:
                                shutil.copyfileobj(src, out)
                            dest.chmod(0o755 if member.mode & 0o111 else 0o644)
                restored = temp / name
                if overlays.is_dir():
                    shutil.copytree(overlays, restored, dirs_exist_ok=True)
                expected = {str(p.relative_to(restored)): sha256(p) for p in restored.rglob('*') if p.is_file()}
                if target.exists():
                    # Prior archive restore: verify actual files, not merely a
                    # marker that an old invocation might have left behind.
                    for relative, digest in expected.items():
                        p = target / relative
                        if not p.is_file() or p.is_symlink() or sha256(p) != digest:
                            raise ValueError(f'{name}: existing dependency differs; use a clean checkout')
                else:
                    restored.rename(target)
            evidence[name] = {'revision': spec['revision'], 'archive_sha256': spec['sha256'], 'origin': 'locked-archive'}
    write_json(work / 'native-dependencies.json', evidence)
