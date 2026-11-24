#!/usr/bin/env python3
"""Package a built novaling (modified Sunshine) install tree into novaling-remote-win64.zip.

The resulting archive matches the layout expected by lin/packaging/stage_remote.py
and lin/linbot/remote_host.py (bundled under <linbot-bundle>/remote/):
  - novaling.exe
  - zlib1.dll (and any runtime DLLs from install-dir)
  - assets/ (apps.json, shaders, web UI, etc.)
  - tools/ (audio-info.exe, dxgi-info.exe, novalingsvc.exe)
  - umdf_mouse/ (optional: novaling_umdf_mouse.inf, .dll, .cat, .cer)
  - manifest.json (version, commit, file SHA-256 inventory)
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import zipfile

EXCLUDE_SUFFIXES = {".pdb", ".dbg", ".lib", ".a", ".exp", ".ilk"}
EXCLUDE_DIRS = {"__pycache__", ".git"}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def copy_tree_filtered(src: Path, dst: Path) -> None:
    for item in sorted(src.rglob("*")):
        if any(part in EXCLUDE_DIRS for part in item.relative_to(src).parts):
            continue
        if item.is_symlink():
            continue
        rel = item.relative_to(src)
        target = dst / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        if item.suffix.lower() in EXCLUDE_SUFFIXES:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, target)


def package_remote(
    install_dir: Path,
    out_zip: Path,
    *,
    version: str = "0.0.0",
    commit: str = "",
    umdf_dir: Path | None = None,
) -> dict:
    install_dir = install_dir.resolve()
    if not (install_dir / "novaling.exe").is_file():
        raise FileNotFoundError(f"novaling.exe not found in {install_dir}")
    if not (install_dir / "assets").is_dir():
        raise FileNotFoundError(f"assets/ directory not found in {install_dir}")

    with tempfile.TemporaryDirectory(prefix="novaling_remote_pkg_") as temp:
        stage = Path(temp) / "novaling-remote"
        stage.mkdir(parents=True)
        copy_tree_filtered(install_dir, stage)

        if umdf_dir is not None:
            umdf_dir = umdf_dir.resolve()
            if not (umdf_dir / "novaling_umdf_mouse.inf").is_file():
                raise FileNotFoundError(f"novaling_umdf_mouse.inf not found in {umdf_dir}")
            copy_tree_filtered(umdf_dir, stage / "umdf_mouse")

        files: dict[str, str] = {}
        for path in sorted(stage.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(stage).as_posix()
            if rel == "manifest.json":
                continue
            files[rel] = sha256_file(path)

        manifest = {
            "schema": 1,
            "product": "novaling-remote",
            "platform": "win64",
            "version": version,
            "commit": commit,
            "entry": "novaling.exe",
            "files": files,
        }
        (stage / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

        out_zip = out_zip.resolve()
        out_zip.parent.mkdir(parents=True, exist_ok=True)
        tmp_zip = out_zip.with_suffix(out_zip.suffix + ".tmp")
        try:
            with zipfile.ZipFile(tmp_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
                for path in sorted(stage.rglob("*")):
                    if path.is_file():
                        arc = Path("novaling-remote") / path.relative_to(stage)
                        zf.write(path, arc.as_posix())
            tmp_zip.replace(out_zip)
        finally:
            tmp_zip.unlink(missing_ok=True)

    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-dir", type=Path, required=True, help="Directory from `cmake --install` containing novaling.exe and assets/")
    parser.add_argument("--out-zip", type=Path, required=True, help="Output path for novaling-remote-win64.zip")
    parser.add_argument("--version", default="0.0.0")
    parser.add_argument("--commit", default="")
    parser.add_argument("--umdf-dir", type=Path, default=None, help="Optional built UMDF virtual mouse driver directory")
    args = parser.parse_args()

    manifest = package_remote(
        args.install_dir,
        args.out_zip,
        version=args.version,
        commit=args.commit,
        umdf_dir=args.umdf_dir,
    )
    print(f"Packaged {len(manifest['files'])} files -> {args.out_zip}")


if __name__ == "__main__":
    main()
