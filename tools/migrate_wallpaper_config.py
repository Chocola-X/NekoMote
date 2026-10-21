#!/usr/bin/env python3
"""Explicit offline migration of the former asset-local wallpaper preferences."""

import argparse
import json
import math
import os
from pathlib import Path
import tempfile


STRINGS = {"character", "background", "language"}
FLAGS = {"mute", "fit", "transform_saved", "icon_position_saved",
         "sidebar_position_saved", "sidebar_free"}
NUMBERS = {"scale", "origin_x", "origin_y", "icon_x", "icon_y",
           "sidebar_x", "sidebar_y", "background_fade_seconds"}


def convert(source):
    """Reject damaged input; only discard former launch-only width/height fields."""
    if not isinstance(source, dict) or "version" in source:
        raise ValueError("expected legacy preferences without a version field")
    unknown = set(source) - STRINGS - FLAGS - NUMBERS - {"fps", "width", "height"}
    if unknown:
        raise ValueError("unknown legacy fields: " + ", ".join(sorted(unknown)))
    result = {"version": 1}
    for key, value in source.items():
        if key in {"width", "height"}:
            continue
        if key in STRINGS and not isinstance(value, str):
            raise ValueError(f"{key} must be a string")
        if key in STRINGS and "\0" in value:
            raise ValueError(f"{key} contains an embedded NUL")
        if key in FLAGS and not isinstance(value, bool):
            raise ValueError(f"{key} must be boolean")
        if key in NUMBERS:
            if (isinstance(value, bool) or not isinstance(value, (int, float))
                    or not math.isfinite(value) or abs(value) > 3.4028234663852886e38):
                raise ValueError(f"{key} must be a finite number")
            if key == "scale" and value <= 0:
                raise ValueError("scale must be positive")
            if key == "background_fade_seconds" and value < 0:
                raise ValueError("background_fade_seconds must be nonnegative")
        if key == "fps" and (type(value) is not int or (value != 0 and not 5 <= value <= 240)):
            raise ValueError("fps must be zero or an integer in 5..240")
        if key == "language" and value not in {"en", "zh-CN", "zh-TW", "ja"}:
            raise ValueError("language must be en, zh-CN, zh-TW or ja")
        result[key] = value
    for flag, coordinates in (
        ("transform_saved", {"scale", "origin_x", "origin_y"}),
        ("icon_position_saved", {"icon_x", "icon_y"}),
        ("sidebar_position_saved", {"sidebar_x", "sidebar_y"}),
    ):
        if result.get(flag) and not coordinates <= result.keys():
            raise ValueError(f"{flag} requires all saved coordinates")
    return result


def migrate(source, destination):
    source, destination = Path(source), Path(destination)
    value = convert(json.loads(source.read_text(encoding="utf-8")))
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"output already exists: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=destination.parent,
                                         prefix=".nekomote-migrate-", delete=False) as file:
            temporary = Path(file.name)
            json.dump(value, file, ensure_ascii=False, indent=2, allow_nan=False)
            file.write("\n")
            file.flush()
            os.fsync(file.fileno())
        # Atomic publication without replacing a concurrently created destination.
        os.link(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        migrate(arguments.source, arguments.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Migration failed: {error}\n")
    print(f"Saved panel preferences: {arguments.output}; source unchanged")


if __name__ == "__main__":
    main()
