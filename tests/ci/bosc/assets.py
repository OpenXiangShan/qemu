"""Fixed, read-only inputs. No firmware build or publication. GPL-2.0-or-later."""
import os
import ipaddress
from pathlib import Path
import shutil
import ssl
import tempfile
import urllib.error
import urllib.parse
import urllib.request

from common import HERE, locked, read_json, sha256, write_json


def verify(path, spec):
    path = Path(path)
    if not path.is_file():
        raise ValueError(f"missing asset: {path}")
    if path.stat().st_size != spec["size"] or sha256(path) != spec["sha256"]:
        raise ValueError(f"asset size/SHA256 mismatch: {path}")
    return path


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward an authorization header to another origin.
        raise ValueError("asset redirect refused; configure its final location")


def validate_base(base):
    parsed = urllib.parse.urlsplit(base)
    if (not parsed.netloc or parsed.username or parsed.password or parsed.query
            or parsed.fragment or parsed.scheme not in ("http", "https")):
        raise ValueError("JF base must be HTTP(S) without credentials/query/fragment")
    if parsed.scheme == "http":
        origin = f"http://{parsed.netloc}"
        allowed = os.environ.get("BOSC_CI_ALLOW_HTTP_ORIGIN", "")
        try:
            address = ipaddress.ip_address(parsed.hostname)
        except ValueError:
            raise ValueError("HTTP exception requires an explicit private IP origin") from None
        if not address.is_private or allowed != origin:
            raise ValueError("HTTP requires BOSC_CI_ALLOW_HTTP_ORIGIN matching this private IP origin; prefer HTTPS")
    return parsed


def download(path, base, relative, token, spec):
    validate_base(base)
    if relative.startswith("/") or ".." in Path(relative).parts or ":" in relative:
        raise ValueError("JF asset path must be relative")
    url = base.rstrip("/") + "/" + urllib.parse.quote(relative, safe="/")
    headers = {"Authorization": f"Bearer {token}"} if token else {}
    request = urllib.request.Request(url, headers=headers)
    opener = urllib.request.build_opener(
        NoRedirect(), urllib.request.HTTPSHandler(context=ssl.create_default_context()))
    # A process-level alarm provides an absolute deadline too; socket timeout
    # alone is insufficient for a stalled peer that keeps trickling bytes.
    import signal
    old = signal.getsignal(signal.SIGALRM)
    def expired(_signum, _frame):
        raise TimeoutError("asset download exceeded 600 seconds")
    signal.signal(signal.SIGALRM, expired)
    signal.alarm(600)
    try:
        with opener.open(request, timeout=30) as response, path.open("wb") as dest:
            total = 0
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > spec["size"]:
                    raise ValueError("download exceeds locked size")
                dest.write(chunk)
    except urllib.error.HTTPError as exc:
        raise RuntimeError(f"JF returned HTTP {exc.code}") from None
    except urllib.error.URLError:
        raise RuntimeError("JF download failed; check network, CA and credentials") from None
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, old)


def prepare(work, cases, assets_dir=None, overrides=None, cache_dir=None,
            jf_paths=None, specs=None):
    specs = specs if specs is not None else read_json(HERE / "images.lock.json")["assets"]
    needed = sorted({a for c in cases for a in c["assets"]})
    unknown = set(overrides or {}) - set(specs)
    unknown |= set(jf_paths or {}) - set(specs)
    if unknown:
        raise ValueError(f"unknown asset overrides: {sorted(unknown)}")
    paths = {}
    evidence = {}
    cache = Path(cache_dir or work / "asset-cache").resolve()
    for name in needed:
        spec = specs[name]
        remote_path = (jf_paths or {}).get(name, spec.get("jf_path", spec["path"]))
        local = (overrides or {}).get(name)
        if local is None and assets_dir:
            local = Path(assets_dir) / spec["path"]
        if local is not None:
            source = verify(local, spec).resolve()
            origin = "local-verified"
        else:
            source = cache / spec["sha256"] / Path(spec["path"]).name
            with locked(cache / (spec["sha256"] + ".lock")):
                if source.exists():
                    verify(source, spec)
                    origin = "cache-verified"
                else:
                    if spec.get("publication") == "local-only-not-published":
                        raise ValueError(f"{name}: asset not published; supply locked input via --asset or --assets-dir")
                    base = os.environ.get("BOSC_CI_JF_BASE_URL")
                    if not base:
                        raise ValueError(f"{name}: no local asset/cache or BOSC_CI_JF_BASE_URL")
                    source.parent.mkdir(parents=True, exist_ok=True)
                    fd, tmpname = tempfile.mkstemp(dir=source.parent, prefix="download-")
                    os.close(fd)
                    tmp = Path(tmpname)
                    try:
                        download(tmp, base, remote_path,
                                 os.environ.get("BOSC_CI_JF_TOKEN"), spec)
                        verify(tmp, spec)
                        tmp.chmod(0o444)
                        tmp.replace(source)
                    finally:
                        tmp.unlink(missing_ok=True)
                    origin = "jf-verified"
        dest = work / "assets" / name / Path(spec["path"]).name
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            verify(dest, spec)
        else:
            shutil.copyfile(source, dest)
            dest.chmod(0o444)
            verify(dest, spec)
        paths[name] = str(dest.resolve())
        evidence[name] = {"origin": origin, "path": spec["path"], "jf_path": remote_path,
                          "sha256": spec["sha256"], "size": spec["size"]}
    # Verify a locked payload is also internally consistent with its locked
    # components, without downloading unused components or rebuilding it.
    if "qspi" in paths:
        import hashlib
        with Path(paths["qspi"]).open("rb") as stream:
            for component in specs["qspi"]["components"]:
                spec = specs[component["asset"]]
                stream.seek(component["offset"])
                if hashlib.sha256(stream.read(spec["size"])).hexdigest() != spec["sha256"]:
                    raise ValueError("QSPI component disagrees with locked baseline")
    group = cases[0]["group"] if cases and len({c["group"] for c in cases}) == 1 else "all"
    write_json(work / "asset-evidence" / f"{group}.json", evidence)
    return paths
