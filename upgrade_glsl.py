#!/usr/bin/env python3
"""Upgrade legacy GLSL ES shaders to GLSL ES 3.00."""

from __future__ import annotations

import argparse
import ast
import json
import re
import sys
from pathlib import Path


ATTRIBUTE_LOCATIONS = {
    "tyPosition": 0,
    "tyColor": 1,
    "tyTexcoord": 2,
}
VARYING_LOCATIONS = {
    "vColor": 0,
    "vTexcoord": 1,
}
SHADER_SUFFIXES = {".vsh", ".fsh"}
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".m", ".mm", ".java",
    ".kt", ".kts", ".cs", ".js", ".jsx", ".ts", ".tsx", ".py", ".swift",
    ".go", ".rs",
}

ATTRIBUTE_RE = re.compile(
    r"(?m)^(?P<indent>[ \t]*)attribute(?P<space>[ \t]+)"
    r"(?P<type>[A-Za-z_]\w*(?:[ \t]*\[[^\]]+\])?)[ \t]+"
    r"(?P<name>[A-Za-z_]\w*)"
)
VARYING_RE = re.compile(
    r"(?m)^(?P<indent>[ \t]*)varying(?P<space>[ \t]+)"
    r"(?P<type>[A-Za-z_]\w*(?:[ \t]*\[[^\]]+\])?)[ \t]+"
    r"(?P<name>[A-Za-z_]\w*)"
)
VERSION_RE = re.compile(r"(?m)^[ \t]*#\s*version[^\r\n]*")
FRAG_COLOR_RE = re.compile(r"\bgl_FragColor\b")
TEXTURE_2D_RE = re.compile(r"\btexture2D\b")

STRING_RE = re.compile(
    r'"""(?P<double_triple>(?:\\.|(?!""")[\s\S])*?)"""'
    r"|'''(?P<single_triple>(?:\\.|(?!''')[\s\S])*?)'''"
    r"|`(?P<template>(?:\\.|(?!`)[\s\S])*?)`"
    r'|"(?P<double>(?:\\.|[^"\\])*)"'
    r"|'(?P<single>(?:\\.|[^'\\])*)'"
)


def _add_location(
    match: re.Match[str],
    direction: str,
    locations: dict[str, int],
    preferred_locations: dict[str, int],
) -> str:
    name = match.group("name")
    location = locations.get(name)
    if location is None:
        location = preferred_locations.get(name)
        if location is None:
            used_locations = set(locations.values()) | set(preferred_locations.values())
            location = next(
                index
                for index in range(len(used_locations) + 1)
                if index not in used_locations
            )
        locations[name] = location
    return (
        f'{match.group("indent")}layout(location = {location}) {direction} '
        f'{match.group("type")} {name}'
    )


def convert_shader(source: str, stage: str) -> str:
    """Convert one vertex or fragment shader while preserving its formatting."""
    if stage not in {"vertex", "fragment"}:
        raise ValueError(f"Unknown shader stage: {stage}")

    attribute_locations: dict[str, int] = {}
    varying_locations: dict[str, int] = {}
    declared_attributes = {match.group("name") for match in ATTRIBUTE_RE.finditer(source)}
    declared_varyings = {match.group("name") for match in VARYING_RE.finditer(source)}
    attribute_preferences = {
        name: location
        for name, location in ATTRIBUTE_LOCATIONS.items()
        if name in declared_attributes
    }
    varying_preferences = {
        name: location
        for name, location in VARYING_LOCATIONS.items()
        if name in declared_varyings
    }
    source = VERSION_RE.sub("#version 300 es", source, count=1)
    if not VERSION_RE.search(source):
        source = "#version 300 es\n" + source.lstrip("\ufeff")

    if stage == "vertex":
        source = ATTRIBUTE_RE.sub(
            lambda match: _add_location(
                match, "in", attribute_locations, attribute_preferences
            ),
            source,
        )
        source = VARYING_RE.sub(
            lambda match: _add_location(
                match, "out", varying_locations, varying_preferences
            ),
            source,
        )
    else:
        source = VARYING_RE.sub(
            lambda match: _add_location(
                match, "in", varying_locations, varying_preferences
            ),
            source,
        )

    source, color_count = FRAG_COLOR_RE.subn("fragColor", source)
    if stage == "fragment" and color_count and not re.search(
        r"\bout\s+(?:lowp\s+|mediump\s+|highp\s+)?vec4\s+fragColor\s*;", source
    ):
        output = "layout(location = 0) out vec4 fragColor;\n"
        source = re.sub(
            r"(?m)^(precision[ \t]+[^;\r\n]+;[ \t]*\r?\n)",
            lambda match: match.group(1) + output,
            source,
            count=1,
        )
        if output not in source:
            source = source.replace("#version 300 es\n", "#version 300 es\n" + output, 1)

    return TEXTURE_2D_RE.sub("texture", source)


def _literal_value(match: re.Match[str]) -> tuple[str, str] | None:
    for name in ("double_triple", "single_triple", "template", "double", "single"):
        body = match.group(name)
        if body is not None:
            delimiter = {
                "double_triple": '"""',
                "single_triple": "'''",
                "template": "`",
                "double": '"',
                "single": "'",
            }[name]
            if name == "template":
                return body, name
            try:
                return ast.literal_eval(delimiter + body + delimiter), name
            except (SyntaxError, ValueError):
                return None
    return None


def _encode_literal(value: str, kind: str) -> str:
    delimiter = {
        "double_triple": '"""',
        "single_triple": "'''",
        "template": "`",
    }.get(kind)
    if delimiter:
        return value.replace(delimiter, "\\" + delimiter)
    if kind == "double":
        return json.dumps(value, ensure_ascii=False)[1:-1]
    return (
        value.replace("\\", "\\\\")
        .replace("'", "\\'")
        .replace("\r", "\\r")
        .replace("\n", "\\n")
        .replace("\t", "\\t")
    )


def _embedded_stage(source: str) -> str | None:
    if re.search(r"\bgl_Position\b", source):
        return "vertex"
    if re.search(r"\bgl_FragColor\b|\btexture2D\s*\(", source):
        return "fragment"
    return None


def convert_embedded_strings(source: str) -> tuple[str, int]:
    """Convert GLSL-looking string literals in common source-code files."""
    converted = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal converted
        literal = _literal_value(match)
        if literal is None:
            return match.group(0)
        shader, kind = literal
        if "void main" not in shader or not re.search(
            r"\b(?:attribute|varying|texture2D|gl_FragColor|#\s*version)\b", shader
        ):
            return match.group(0)
        stage = _embedded_stage(shader)
        if stage is None:
            return match.group(0)
        updated = convert_shader(shader, stage)
        if updated == shader:
            return match.group(0)
        converted += 1
        return _rebuild_literal(match, kind, updated)

    return STRING_RE.sub(replace, source), converted


def _rebuild_literal(match: re.Match[str], kind: str, value: str) -> str:
    delimiters = {
        "double_triple": '"""',
        "single_triple": "'''",
        "template": "`",
        "double": '"',
        "single": "'",
    }
    delimiter = delimiters[kind]
    return delimiter + _encode_literal(value, kind) + delimiter


def _stage_for(path: Path) -> str | None:
    suffix = path.suffix.casefold()
    if suffix == ".vsh":
        return "vertex"
    if suffix == ".fsh":
        return "fragment"
    return None


def _files_from(paths: list[Path]) -> list[Path]:
    found: set[Path] = set()
    for path in paths:
        if path.is_file():
            found.add(path)
        elif path.is_dir():
            found.update(
                file for file in path.rglob("*")
                if file.is_file()
                and file.suffix.casefold() in SHADER_SUFFIXES | SOURCE_SUFFIXES
            )
        else:
            raise FileNotFoundError(path)
    return sorted(found)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        default=[Path(__file__).resolve().parent],
        help="shader files, source files, or directories (default: repository tree)",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="report files that would change without writing them",
    )
    args = parser.parse_args()

    changed: list[Path] = []
    for path in _files_from(args.paths):
        original = path.read_text(encoding="utf-8")
        stage = _stage_for(path)
        if stage:
            updated = convert_shader(original, stage)
        elif path.suffix.casefold() in SOURCE_SUFFIXES:
            updated, _ = convert_embedded_strings(original)
        else:
            continue
        if updated == original:
            continue
        changed.append(path)
        if not args.check:
            path.write_text(updated, encoding="utf-8", newline="")

    for path in changed:
        print(f"{'needs upgrade' if args.check else 'upgraded'}: {path}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
