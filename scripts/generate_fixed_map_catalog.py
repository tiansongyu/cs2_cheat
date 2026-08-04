#!/usr/bin/env python3
"""Generate the C++ fixed-map catalog from the Web Radar manifest.

The JSON manifest is the only hand-maintained source of map calibration data.
This script validates that data and its referenced 1024x1024 PNG assets before
writing a deterministic C++20 header.  ``--check`` performs the same validation
and fails when the checked-in header is stale, without modifying the tree.
"""

from __future__ import annotations

import argparse
import difflib
import json
import math
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import NoReturn


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPOSITORY_ROOT / "web-radar/public/maps/manifest.json"
DEFAULT_OUTPUT = (
    REPOSITORY_ROOT
    / "external-cheat-base/src/core/game/fixed_map_catalog.hpp"
)
EXPECTED_MANIFEST_VERSION = 1
EXPECTED_TEXTURE_SIZE = 1024
MAP_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class ManifestError(ValueError):
    """Raised when the fixed-map manifest cannot safely generate C++ data."""


@dataclass(frozen=True)
class Level:
    id: str
    image_path: str
    minimum_z: float
    maximum_z: float


@dataclass(frozen=True)
class Map:
    id: str
    display_name: str
    image_path: str
    origin_x: float
    origin_y: float
    scale: float
    levels: tuple[Level, ...]


@dataclass(frozen=True)
class Manifest:
    version: int
    source_project: str
    source_release: str
    maps: tuple[Map, ...]


def fail(message: str) -> NoReturn:
    raise ManifestError(message)


def require_object(value: object, context: str) -> dict[str, object]:
    if not isinstance(value, dict):
        fail(f"{context} must be an object")
    return value


def require_list(value: object, context: str) -> list[object]:
    if not isinstance(value, list):
        fail(f"{context} must be an array")
    return value


def require_string(value: object, context: str) -> str:
    if not isinstance(value, str) or not value or "\x00" in value:
        fail(f"{context} must be a non-empty string without NUL bytes")
    return value


def require_float32(value: object, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{context} must be a number")
    try:
        number = float(value)
    except (OverflowError, ValueError):
        fail(f"{context} is outside the finite float32 range")
    if not math.isfinite(number):
        fail(f"{context} must be finite")
    try:
        converted = struct.unpack("!f", struct.pack("!f", number))[0]
    except (OverflowError, struct.error):
        fail(f"{context} is outside the finite float32 range")
    if not math.isfinite(converted):
        fail(f"{context} is outside the finite float32 range")
    return converted


def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            fail(f"manifest contains duplicate JSON key {key!r}")
        result[key] = value
    return result


def relative_image_path(
    value: object,
    context: str,
    map_id: str,
) -> str:
    image = require_string(value, context)
    if "\\" in image or "?" in image or "#" in image:
        fail(f"{context} must be a plain POSIX URL path")

    path = PurePosixPath(image)
    parts = path.parts
    if (
        not path.is_absolute()
        or len(parts) < 4
        or parts[1] != "maps"
        or parts[2] != map_id
        or path.suffix.lower() != ".png"
        or any(part in ("", ".", "..") for part in parts[1:])
    ):
        fail(
            f"{context} must be a PNG below /maps/{map_id}/"
        )

    canonical = "/" + "/".join(parts[1:])
    if canonical != image:
        fail(f"{context} must be a canonical path, got {image!r}")
    return canonical.removeprefix("/")


def validate_png(
    public_root: Path,
    image_path: str,
    context: str,
) -> None:
    resolved_root = public_root.resolve()
    candidate = (resolved_root / image_path).resolve()
    if not candidate.is_relative_to(resolved_root):
        fail(f"{context} resolves outside the Web Radar public directory")
    if not candidate.is_file():
        fail(f"{context} references missing asset {image_path!r}")

    with candidate.open("rb") as image:
        header = image.read(24)
    if (
        len(header) != 24
        or header[:8] != PNG_SIGNATURE
        or header[12:16] != b"IHDR"
    ):
        fail(f"{context} references an invalid PNG: {image_path!r}")
    width, height = struct.unpack(">II", header[16:24])
    if width != EXPECTED_TEXTURE_SIZE or height != EXPECTED_TEXTURE_SIZE:
        fail(
            f"{context} must be {EXPECTED_TEXTURE_SIZE}x"
            f"{EXPECTED_TEXTURE_SIZE}, got {width}x{height}"
        )


def load_manifest(path: Path) -> Manifest:
    try:
        raw_text = path.read_text(encoding="utf-8")
    except OSError as error:
        fail(f"unable to read manifest {path}: {error}")

    try:
        raw = json.loads(
            raw_text,
            object_pairs_hook=reject_duplicate_keys,
            parse_constant=lambda value: fail(
                f"manifest contains non-finite JSON number {value}"
            ),
        )
    except json.JSONDecodeError as error:
        fail(f"invalid JSON in {path}: {error}")

    root = require_object(raw, "manifest")
    version = root.get("version")
    if (
        isinstance(version, bool)
        or not isinstance(version, int)
        or version != EXPECTED_MANIFEST_VERSION
    ):
        fail(
            "manifest.version must be integer "
            f"{EXPECTED_MANIFEST_VERSION}"
        )

    source = require_object(root.get("source"), "manifest.source")
    source_project = require_string(
        source.get("project"),
        "manifest.source.project",
    )
    source_release = require_string(
        source.get("release"),
        "manifest.source.release",
    )
    raw_maps = require_list(root.get("maps"), "manifest.maps")
    if not raw_maps:
        fail("manifest.maps must not be empty")

    public_root = path.parent.parent
    seen_map_ids: set[str] = set()
    validated_images: set[str] = set()
    maps: list[Map] = []
    for map_index, raw_map in enumerate(raw_maps):
        context = f"manifest.maps[{map_index}]"
        entry = require_object(raw_map, context)
        map_id = require_string(entry.get("id"), f"{context}.id")
        if not MAP_ID_PATTERN.fullmatch(map_id):
            fail(
                f"{context}.id must use lowercase ASCII letters, digits, "
                "underscores, or hyphens"
            )
        if map_id in seen_map_ids:
            fail(f"duplicate map id {map_id!r}")
        seen_map_ids.add(map_id)

        display_name = require_string(entry.get("name"), f"{context}.name")
        origin = require_object(entry.get("origin"), f"{context}.origin")
        origin_x = require_float32(origin.get("x"), f"{context}.origin.x")
        origin_y = require_float32(origin.get("y"), f"{context}.origin.y")
        scale = require_float32(entry.get("scale"), f"{context}.scale")
        if scale <= 0.0:
            fail(f"{context}.scale must be greater than zero")
        image_path = relative_image_path(
            entry.get("image"),
            f"{context}.image",
            map_id,
        )

        raw_levels = entry.get("levels", [])
        levels: list[Level] = []
        seen_level_ids: set[str] = set()
        for level_index, raw_level in enumerate(
            require_list(raw_levels, f"{context}.levels")
        ):
            level_context = f"{context}.levels[{level_index}]"
            level = require_object(raw_level, level_context)
            level_id = require_string(level.get("id"), f"{level_context}.id")
            if not MAP_ID_PATTERN.fullmatch(level_id):
                fail(
                    f"{level_context}.id must use lowercase ASCII letters, "
                    "digits, underscores, or hyphens"
                )
            if level_id in seen_level_ids:
                fail(f"{context} contains duplicate level id {level_id!r}")
            seen_level_ids.add(level_id)
            minimum_z = require_float32(
                level.get("minZ"),
                f"{level_context}.minZ",
            )
            maximum_z = require_float32(
                level.get("maxZ"),
                f"{level_context}.maxZ",
            )
            if minimum_z >= maximum_z:
                fail(f"{level_context} must satisfy minZ < maxZ")
            level_image_path = relative_image_path(
                level.get("image"),
                f"{level_context}.image",
                map_id,
            )
            levels.append(
                Level(
                    id=level_id,
                    image_path=level_image_path,
                    minimum_z=minimum_z,
                    maximum_z=maximum_z,
                )
            )

        image_contexts = [(image_path, f"{context}.image")]
        image_contexts.extend(
            (level.image_path, f"{context}.levels image")
            for level in levels
        )
        for referenced_image, image_context in image_contexts:
            if referenced_image in validated_images:
                continue
            validate_png(public_root, referenced_image, image_context)
            validated_images.add(referenced_image)

        maps.append(
            Map(
                id=map_id,
                display_name=display_name,
                image_path=image_path,
                origin_x=origin_x,
                origin_y=origin_y,
                scale=scale,
                levels=tuple(levels),
            )
        )

    maps.sort(key=lambda entry: entry.id)
    return Manifest(
        version=version,
        source_project=source_project,
        source_release=source_release,
        maps=tuple(maps),
    )


def cpp_string(value: str) -> str:
    """Return an ASCII-only C++ string literal containing UTF-8 bytes."""

    segments: list[str] = []
    current: list[str] = []

    def flush() -> None:
        if current:
            segments.append('"' + "".join(current) + '"')
            current.clear()

    for byte in value.encode("utf-8"):
        if byte == 0x22:
            current.append('\\"')
        elif byte == 0x5C:
            current.append("\\\\")
        elif byte == 0x0A:
            current.append("\\n")
        elif byte == 0x0D:
            current.append("\\r")
        elif byte == 0x09:
            current.append("\\t")
        elif 0x20 <= byte <= 0x7E:
            current.append(chr(byte))
        else:
            flush()
            segments.append(f'"\\x{byte:02X}"')
    flush()
    return "".join(segments) if segments else '""'


def float_literal(value: float) -> str:
    if value == 0.0:
        return "0.0F"
    if value.is_integer() and abs(value) < 1_000_000_000:
        return f"{int(value)}.0F"
    # Use the shortest readable decimal that round-trips to the already
    # validated float32 value.  This keeps source values such as 4.4 legible
    # without changing the C++ representation.
    rendered = format(value, ".9g")
    for precision in range(1, 10):
        candidate = format(value, f".{precision}g")
        round_trip = struct.unpack(
            "!f",
            struct.pack("!f", float(candidate)),
        )[0]
        if round_trip == value:
            rendered = candidate
            break
    if "." not in rendered and "e" not in rendered.lower():
        rendered += ".0"
    return rendered + "F"


def render_header(manifest: Manifest, manifest_path: Path) -> str:
    try:
        source_label = manifest_path.resolve().relative_to(REPOSITORY_ROOT)
    except ValueError:
        source_label = manifest_path

    lines = [
        "#pragma once",
        "",
        "// Generated by scripts/generate_fixed_map_catalog.py from",
        f"// {source_label.as_posix()}.",
        "// Do not edit this file directly; update the manifest and regenerate.",
        "",
        '#include "core/game/fixed_map_radar.hpp"',
        "",
        "#include <algorithm>",
        "#include <array>",
        "#include <string_view>",
        "",
        "namespace game::fixed_map_catalog",
        "{",
        f"    inline constexpr int kManifestVersion = {manifest.version};",
        "    inline constexpr std::string_view kSourceProject =",
        f"        {cpp_string(manifest.source_project)};",
        "    inline constexpr std::string_view kSourceRelease =",
        f"        {cpp_string(manifest.source_release)};",
        "",
        "    [[nodiscard]] inline const auto& all()",
        "    {",
        "        // Entries are sorted by canonical map id for binary lookup.",
        "        static const std::array<fixed_map_radar::MapDefinition,",
        f"            {len(manifest.maps)}> definitions{{{{",
    ]

    for map_entry in manifest.maps:
        lines.extend(
            [
                "            fixed_map_radar::MapDefinition{",
                f"                {cpp_string(map_entry.id)},",
                f"                {cpp_string(map_entry.display_name)},",
                f"                {cpp_string(map_entry.image_path)},",
                f"                {float_literal(map_entry.origin_x)},",
                f"                {float_literal(map_entry.origin_y)},",
                f"                {float_literal(map_entry.scale)},",
                f"                {float_literal(float(EXPECTED_TEXTURE_SIZE))},",
                f"                {float_literal(float(EXPECTED_TEXTURE_SIZE))},",
                "                {",
            ]
        )
        for level in map_entry.levels:
            lines.extend(
                [
                    "                    fixed_map_radar::MapLevel{",
                    f"                        {cpp_string(level.id)},",
                    f"                        {cpp_string(level.image_path)},",
                    f"                        {float_literal(level.minimum_z)},",
                    f"                        {float_literal(level.maximum_z)}",
                    "                    },",
                ]
            )
        lines.extend(
            [
                "                }",
                "            },",
            ]
        )

    lines.extend(
        [
            "        }};",
            "        return definitions;",
            "    }",
            "",
            "    [[nodiscard]] inline const fixed_map_radar::MapDefinition* find(",
            "        std::string_view mapId)",
            "    {",
            "        const auto& definitions = all();",
            "        const auto match = std::lower_bound(",
            "            definitions.begin(),",
            "            definitions.end(),",
            "            mapId,",
            "            [](const fixed_map_radar::MapDefinition& definition,",
            "                std::string_view id) noexcept",
            "            {",
            "                return std::string_view(definition.id) < id;",
            "            });",
            "        if (match == definitions.end()",
            "            || std::string_view(match->id) != mapId)",
            "        {",
            "            return nullptr;",
            "        }",
            "        return &*match;",
            "    }",
            "}",
            "",
        ]
    )
    return "\n".join(lines)


def write_if_changed(path: Path, content: str) -> bool:
    try:
        if path.read_text(encoding="utf-8") == content:
            return False
    except FileNotFoundError:
        pass
    except OSError as error:
        fail(f"unable to read output {path}: {error}")

    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    try:
        temporary.write_text(content, encoding="utf-8", newline="\n")
        temporary.replace(path)
    except OSError as error:
        fail(f"unable to write output {path}: {error}")
    return True


def check_output(path: Path, expected: str) -> bool:
    try:
        actual = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        print(f"error: generated catalog is missing: {path}", file=sys.stderr)
        return False
    except OSError as error:
        print(f"error: unable to read generated catalog {path}: {error}", file=sys.stderr)
        return False

    if actual == expected:
        return True
    print(
        "error: generated fixed-map catalog is stale; run "
        "python3 scripts/generate_fixed_map_catalog.py",
        file=sys.stderr,
    )
    diff = difflib.unified_diff(
        actual.splitlines(),
        expected.splitlines(),
        fromfile=str(path),
        tofile="expected generated catalog",
        lineterm="",
        n=3,
    )
    for line in diff:
        print(line, file=sys.stderr)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="validate inputs and fail if the output is missing or stale",
    )
    args = parser.parse_args()

    try:
        manifest_path = args.manifest.resolve()
        output_path = args.output.resolve()
        manifest = load_manifest(manifest_path)
        generated = render_header(manifest, manifest_path)
        if args.check:
            if not check_output(output_path, generated):
                return 1
            print(
                f"Fixed-map catalog is current ({len(manifest.maps)} maps): "
                f"{output_path}"
            )
            return 0

        changed = write_if_changed(output_path, generated)
        action = "Generated" if changed else "Already current"
        print(f"{action} {len(manifest.maps)} maps in {output_path}")
        return 0
    except ManifestError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
