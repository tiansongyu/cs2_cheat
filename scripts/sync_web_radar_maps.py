#!/usr/bin/env python3
"""Install a pinned, checksummed CS2 radar map pack for the Web Radar UI.

The source code in this repository is clean-room code.  Radar images and
overview coordinates are game assets owned by Valve Corporation; they are
downloaded separately from the awpy-data release pipeline and retain Valve's
copyright and terms.  See the generated NOTICE.txt in the output directory.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import shutil
import urllib.request
import zipfile
from pathlib import Path


DEFAULT_VERSION = "2000879"
DEFAULT_IMAGES_SHA256 = (
    "99b5d304a3a6b407214fc53dcf071f59ef96a8e605269d17cd1f378c523bb357"
)
DEFAULT_MAP_DATA_SHA256 = (
    "a408448870f8ba607998e1daac6c421a6d6c437105b7cd47e83fd3dc562f2050"
)
BASE_URL = (
    "https://github.com/pnxenopoulos/awpy-data/releases/download/{version}"
)


def download(url: str) -> bytes:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "cs2-cheat-web-radar-map-sync/1"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def verify(payload: bytes, expected: str | None, label: str) -> str:
    digest = hashlib.sha256(payload).hexdigest()
    if expected and digest != expected:
        raise RuntimeError(
            f"{label} SHA-256 mismatch: expected {expected}, got {digest}"
        )
    return digest


def display_name(map_id: str) -> str:
    prefixes = {"de": "", "cs": "", "ar": ""}
    parts = map_id.split("_")
    if len(parts) > 1 and parts[0] in prefixes:
        parts = parts[1:]
    return " ".join(word.capitalize() for word in parts)


def numeric(value: object, fallback: float) -> float:
    try:
        return float(value)  # type: ignore[arg-type]
    except (TypeError, ValueError):
        return fallback


def build_manifest(
    map_data: dict[str, dict[str, object]],
    available_images: set[str],
    version: str,
) -> dict[str, object]:
    maps: list[dict[str, object]] = []
    for map_id in sorted(map_data):
        primary_name = f"{map_id}.png"
        if primary_name not in available_images:
            continue

        data = map_data[map_id]
        entry: dict[str, object] = {
            "id": map_id,
            "name": display_name(map_id),
            "origin": {
                "x": numeric(data.get("pos_x"), 0.0),
                "y": numeric(data.get("pos_y"), 0.0),
            },
            "scale": numeric(data.get("scale"), 1.0),
            "image": f"/maps/{map_id}/radar.png",
        }

        sections = data.get("verticalsections")
        levels: list[dict[str, object]] = []
        if isinstance(sections, dict):
            for section_id, raw_section in sections.items():
                if not isinstance(raw_section, dict):
                    continue
                image_name = (
                    primary_name
                    if section_id == "default"
                    else f"{map_id}_{section_id}.png"
                )
                if image_name not in available_images:
                    continue
                output_name = (
                    "radar.png"
                    if section_id == "default"
                    else f"radar_{section_id}.png"
                )
                levels.append(
                    {
                        "id": section_id,
                        "minZ": numeric(raw_section.get("AltitudeMin"), -10000.0),
                        "maxZ": numeric(raw_section.get("AltitudeMax"), 10000.0),
                        "image": f"/maps/{map_id}/{output_name}",
                    }
                )
        if levels:
            entry["levels"] = levels
        maps.append(entry)

    return {
        "version": 1,
        "source": {
            "project": "pnxenopoulos/awpy-data",
            "release": version,
        },
        "maps": maps,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default=DEFAULT_VERSION)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("web-radar/public/maps"),
    )
    parser.add_argument("--images-sha256")
    parser.add_argument("--map-data-sha256")
    args = parser.parse_args()

    pinned_default = args.version == DEFAULT_VERSION
    images_expected = args.images_sha256 or (
        DEFAULT_IMAGES_SHA256 if pinned_default else None
    )
    map_data_expected = args.map_data_sha256 or (
        DEFAULT_MAP_DATA_SHA256 if pinned_default else None
    )

    base = BASE_URL.format(version=args.version)
    images_payload = download(f"{base}/images.zip")
    map_data_payload = download(f"{base}/map_data.json")
    images_digest = verify(images_payload, images_expected, "images.zip")
    map_data_digest = verify(map_data_payload, map_data_expected, "map_data.json")

    map_data = json.loads(map_data_payload.decode("utf-8"))
    if not isinstance(map_data, dict):
        raise RuntimeError("map_data.json root must be an object")

    output = args.output.resolve()
    staging = output.with_name(f"{output.name}.staging")
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    available_images: set[str] = set()
    with zipfile.ZipFile(io.BytesIO(images_payload)) as archive:
        for info in archive.infolist():
            source = Path(info.filename)
            if info.is_dir() or source.parent != Path("radars"):
                continue
            if source.suffix.lower() != ".png" or source.name.startswith("."):
                continue
            available_images.add(source.name)

        manifest = build_manifest(map_data, available_images, args.version)
        for entry in manifest["maps"]:  # type: ignore[index]
            map_id = entry["id"]  # type: ignore[index]
            target_dir = staging / str(map_id)
            target_dir.mkdir(parents=True, exist_ok=True)
            for image in [entry.get("image"), *[
                level.get("image")
                for level in entry.get("levels", [])
            ]]:
                if not isinstance(image, str):
                    continue
                output_name = Path(image).name
                section_suffix = output_name.removeprefix("radar").removesuffix(".png")
                source_name = (
                    f"{map_id}.png"
                    if not section_suffix
                    else f"{map_id}{section_suffix}.png"
                )
                with archive.open(f"radars/{source_name}") as source_file:
                    (target_dir / output_name).write_bytes(source_file.read())

    (staging / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=False) + "\n",
        encoding="utf-8",
    )
    (staging / "NOTICE.txt").write_text(
        "CS2 radar images and overview coordinates are property of Valve "
        "Corporation and remain subject to Valve's copyright and terms.\n"
        "They were generated from a clean CS2 install by awpy-data and are "
        f"sourced from release {args.version}:\n"
        f"{base}\n\n"
        "awpy-data build scripts are MIT-licensed, copyright (c) 2025 "
        "Peter Xenopoulos. This project is not affiliated with Valve or Awpy.\n",
        encoding="utf-8",
    )
    (staging / "SOURCE.json").write_text(
        json.dumps(
            {
                "release": args.version,
                "images_sha256": images_digest,
                "map_data_sha256": map_data_digest,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )

    if output.exists():
        shutil.rmtree(output)
    staging.replace(output)
    print(f"Installed {len(manifest['maps'])} maps into {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
