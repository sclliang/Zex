"""Retain original Qt license texts and embedded third-party notices from source ZIPs."""
from pathlib import Path, PurePosixPath
import json
import zipfile
import requests

root = Path(__file__).resolve().parents[1]
for module in ("qtbase", "qt5compat", "qtsvg"):
    archive = root / ".downloads" / f"{module}-src.zip"
    if not archive.exists():
        url = f"https://download.qt.io/archive/qt/6.8/6.8.3/submodules/{module}-everywhere-src-6.8.3.zip"
        response = requests.get(url, timeout=120)
        response.raise_for_status()
        archive.write_bytes(response.content)
    output = root / "vendor" / "qt-notices" / module
    with zipfile.ZipFile(archive) as source:
        names = source.namelist()
        chosen = set()
        for name in names:
            if name.endswith("/"):
                continue
            if "/tests/" in name:
                continue
            base = PurePosixPath(name).name.upper()
            if "/LICENSES/" in name or ("/3rdparty/" in name and ("LICENSE" in base or "COPYING" in base or base == "QT_ATTRIBUTION.JSON")):
                chosen.add(name)
            if name.endswith("qt_attribution.json"):
                chosen.add(name)
                entries = json.loads(source.read(name), strict=False)
                if not isinstance(entries, list):
                    entries = [entries]
                for entry in entries:
                    license_files = entry.get("LicenseFile", [])
                    if isinstance(license_files, str):
                        license_files = [license_files]
                    for relative in license_files:
                        target = str(PurePosixPath(name).parent / relative)
                        # Normalize relative '..' inside the source archive, never outside it.
                        parts = []
                        for part in PurePosixPath(target).parts:
                            if part == "..":
                                if parts:
                                    parts.pop()
                            elif part != ".":
                                parts.append(part)
                        candidate = "/".join(parts)
                        if candidate in names:
                            chosen.add(candidate)
        for name in sorted(chosen):
            parts = PurePosixPath(name).parts[1:]
            if not parts or ".." in parts:
                raise RuntimeError("Unexpected archive path")
            destination = output.joinpath(*parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read(name))
        print(f"{module}: retained {len(chosen)} license / attribution files")
