#!/usr/bin/env python3
"""Extract graphics, UI, audio, and supporting assets from an iOS IPA."""

from __future__ import annotations

import argparse
import collections
import json
import plistlib
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import BinaryIO


DEFAULT_IPA = Path(__file__).resolve().parent / "my dumped copy.ipa"
DEFAULT_OUTPUT = (
    Path(__file__).resolve().parent
    / "android_project"
    / "app"
    / "src"
    / "main"
    / "assets"
)

IMAGE_EXTENSIONS = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp",
    ".pvr", ".pvrtc", ".ktx", ".ktx2", ".astc", ".dds", ".heic",
}
AUDIO_EXTENSIONS = {
    ".wav", ".aac", ".mp3", ".ogg", ".m4a", ".caf", ".aif", ".aiff",
    ".flac", ".opus",
}
GRAPHICS_DATA_EXTENSIONS = {".sst", ".tyf", ".tyfx", ".fsh", ".vsh"}
SUPPORT_EXTENSIONS = {".sml", ".txt", ".ini", ".plist", ".crc", ".ttf"}
UI_PATH_PARTS = {"ui", "hud", "menu", "interface", "controls"}


def safe_relative_path(name: str) -> Path:
    """Convert an archive path to a safe, platform-native relative path."""
    normalized = name.replace("\\", "/")
    parts = normalized.split("/")
    if (
        not normalized
        or normalized.startswith("/")
        or any(part in {"", ".", ".."} for part in parts)
        or any(
            part.endswith((" ", "."))
            or any(char in '<>:"|?*\x00' for char in part)
            or part.split(".", 1)[0].upper()
            in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}
            for part in parts
        )
    ):
        raise ValueError(f"Unsafe archive path: {name!r}")
    return Path(*parts)


def find_app(archive: zipfile.ZipFile, requested: str | None) -> tuple[str, bytes]:
    info_paths = [
        name
        for name in archive.namelist()
        if name.startswith("Payload/")
        and name.endswith(".app/Info.plist")
    ]
    if requested:
        selected = [
            path for path in info_paths
            if requested in path or PurePosixPath(path).parent.name == requested
        ]
    else:
        selected = info_paths
    if len(selected) != 1:
        found = ", ".join(str(PurePosixPath(p).parent) for p in info_paths) or "none"
        raise ValueError(
            f"Expected one app bundle (found {found}); select one with --app."
        )
    info_path = selected[0]
    return info_path[: -len("Info.plist")], archive.read(info_path)


def frameworks_in(archive: zipfile.ZipFile, app_prefix: str) -> list[str]:
    prefix = app_prefix + "Frameworks/"
    found = set()
    for name in archive.namelist():
        if name.startswith(prefix):
            relative = name[len(prefix):]
            component = relative.split("/", 1)[0]
            if component.endswith((".framework", ".dylib")):
                found.add(component)
    return sorted(found, key=str.casefold)


def sniff_format(path: Path, header: bytes) -> str | None:
    suffix = path.suffix.lower()
    if header.startswith(b"\x89PNG\r\n\x1a\n"):
        return "PNG"
    if header.startswith(b"PVR\x03"):
        return "PVR v3"
    if header.startswith(b"RIFF") and header[8:12] == b"WAVE":
        return "WAVE"
    if header.startswith(b"caff"):
        return "CAF"
    if header.startswith(b"OggS"):
        return "Ogg"
    if header.startswith(b"fLaC"):
        return "FLAC"
    if header.startswith(b"ID3"):
        return "ID3 audio"
    if suffix == ".sst" and header.startswith(b"\x00tss"):
        return "SST sprite container"
    return None


def is_audio(path: Path, header: bytes) -> bool:
    return path.suffix.lower() in AUDIO_EXTENSIONS or sniff_format(path, header) in {
        "WAVE", "CAF", "Ogg", "FLAC", "ID3 audio"
    }


def category_for(path: Path, header: bytes) -> str:
    suffix = path.suffix.lower()
    parts = {part.casefold() for part in path.parts[:-1]}
    if is_audio(path, header):
        return "audio"
    if suffix in IMAGE_EXTENSIONS:
        return "ui" if parts & UI_PATH_PARTS else "graphics"
    if suffix in GRAPHICS_DATA_EXTENSIONS:
        if suffix in {".sst", ".tyf", ".tyfx", ".fsh", ".vsh"}:
            return "graphics"
    if "sprites" in parts or "fonts" in parts or "shaders" in parts:
        return "graphics"
    return "data"


class AssetExtractor:
    def __init__(
        self,
        archive: zipfile.ZipFile,
        app_prefix: str,
        output: Path,
        executable: str,
    ) -> None:
        self.archive = archive
        self.app_prefix = app_prefix
        self.output = output
        self.executable = executable
        self.category_counts: collections.Counter[str] = collections.Counter()
        self.extension_counts: collections.Counter[str] = collections.Counter()
        self.format_counts: collections.Counter[str] = collections.Counter()
        self.bundles: list[dict[str, object]] = []
        self.raw_files: list[dict[str, str]] = []
        self.extracted_files = 0

    def extract(self) -> None:
        entries = [
            entry for entry in self.archive.infolist()
            if entry.filename.startswith(self.app_prefix)
            and not entry.is_dir()
        ]
        for entry in entries:
            relative_name = entry.filename[len(self.app_prefix):]
            relative_path = safe_relative_path(relative_name)
            if relative_path.name == self.executable:
                continue
            if relative_path.parts[0] == "_CodeSignature":
                continue
            if relative_path.parts[0] == "Frameworks":
                continue
            if relative_path.name == "Info.plist" and len(relative_path.parts) == 1:
                continue
            if relative_path.suffix.lower() == ".zip":
                with self.archive.open(entry) as source:
                    self.extract_bundle(
                        source,
                        relative_path,
                    )
            else:
                with self.archive.open(entry) as source:
                    self.extract_file(source, relative_path)

    def extract_file(self, source: BinaryIO, relative_path: Path) -> None:
        destination_category: str
        prefix = bytearray()
        destination = None

        # Read enough to classify by signature while streaming the entire asset.
        first_chunk = source.read(16)
        prefix.extend(first_chunk)
        destination_category = category_for(relative_path, bytes(prefix))
        destination = self.output / destination_category / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            raise ValueError(f"Duplicate asset output path: {destination}")
        with destination.open("wb") as output:
            output.write(first_chunk)
            shutil.copyfileobj(source, output, length=1024 * 1024)

        self.extracted_files += 1
        self.category_counts[destination_category] += 1
        extension = relative_path.suffix.lower() or "<none>"
        self.extension_counts[extension] += 1
        detected = sniff_format(relative_path, bytes(prefix))
        if detected:
            self.format_counts[detected] += 1
        if extension in {".sst", ".pvr", ".pvrtc", ".ccz"}:
            self.raw_files.append(
                {
                    "path": relative_path.as_posix(),
                    "extension": extension,
                    "header_hex": bytes(prefix).hex(),
                    "detected_format": detected or "unidentified binary texture",
                }
            )

    def extract_bundle(
        self,
        source: BinaryIO,
        archive_path: Path,
    ) -> None:
        bundle_destination = self.output / "asset_bundles" / archive_path
        bundle_destination.parent.mkdir(parents=True, exist_ok=True)
        if bundle_destination.exists():
            raise ValueError(f"Duplicate asset bundle path: {bundle_destination}")
        with tempfile.SpooledTemporaryFile(max_size=8 * 1024 * 1024, mode="w+b") as spool:
            shutil.copyfileobj(source, spool, length=1024 * 1024)
            spool.seek(0)
            with bundle_destination.open("wb") as output:
                shutil.copyfileobj(spool, output, length=1024 * 1024)
            spool.seek(0)

            try:
                nested_archive = zipfile.ZipFile(spool)
            except zipfile.BadZipFile as error:
                raise ValueError(f"Invalid ZIP asset bundle: {archive_path}") from error
            with nested_archive:
                self.bundles.append(
                    {
                        "path": archive_path.as_posix(),
                        "depth": 0,
                        "members": sum(
                            not entry.is_dir()
                            for entry in nested_archive.infolist()
                        ),
                    }
                )


def prepare_output(output: Path, clean: bool) -> None:
    output = output.expanduser().absolute()
    if output.is_symlink():
        raise ValueError(f"Refusing to use a symlink as output: {output}")
    resolved = output.resolve()
    script_directory = Path(__file__).resolve().parent
    if (
        resolved == Path(resolved.anchor)
        or resolved == script_directory
        or resolved in script_directory.parents
    ):
        raise ValueError(f"Refusing unsafe output directory: {output}")
    if resolved.exists():
        if not resolved.is_dir():
            raise ValueError(f"Output path exists and is not a directory: {resolved}")
        if clean:
            shutil.rmtree(resolved)
        elif any(resolved.iterdir()):
            raise FileExistsError(
                f"Output directory is not empty: {resolved}. Use --clean to replace it."
            )
    resolved.mkdir(parents=True, exist_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "ipa",
        nargs="?",
        type=Path,
        default=DEFAULT_IPA,
        help="IPA file (default: %(default)s)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="asset output directory (default: %(default)s)",
    )
    parser.add_argument(
        "--app",
        help="app bundle name or path if the IPA contains more than one app",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="replace an existing output directory before extracting",
    )
    args = parser.parse_args()

    ipa_path = args.ipa.expanduser().resolve()
    if not ipa_path.is_file():
        parser.error(f"IPA file not found: {ipa_path}")

    try:
        with zipfile.ZipFile(ipa_path) as archive:
            app_prefix, plist_data = find_app(archive, args.app)
            metadata = plistlib.loads(plist_data)
            if not isinstance(metadata, dict):
                raise ValueError("Info.plist root is not a dictionary")
            bundle_id = metadata.get("CFBundleIdentifier", "<not set>")
            executable = metadata.get("CFBundleExecutable", "")
            if not isinstance(executable, str) or not executable:
                raise ValueError("Info.plist has no valid CFBundleExecutable")
            frameworks = frameworks_in(archive, app_prefix)
            output = args.output.expanduser().absolute()
            prepare_output(output, args.clean)
            output = output.resolve()
            extractor = AssetExtractor(archive, app_prefix, output, executable)
            extractor.extract()

        report = {
            "source_ipa": str(ipa_path),
            "app_bundle": app_prefix.rstrip("/"),
            "bundle_id": bundle_id,
            "executable": executable,
            "frameworks": frameworks,
            "extracted_files": extractor.extracted_files,
            "files_by_category": dict(sorted(extractor.category_counts.items())),
            "files_by_extension": dict(sorted(extractor.extension_counts.items())),
            "detected_signatures": dict(sorted(extractor.format_counts.items())),
            "asset_bundles": extractor.bundles,
            "raw_texture_candidates": extractor.raw_files,
            "notes": [
                "Assets are copied without format conversion.",
                "Nested ZIP asset bundles are preserved under asset_bundles/ without unpacking their members.",
                "SST sprite containers remain in their original binary format.",
            ],
        }
        report_path = output.parent / "asset_extraction_report.json"
        report_path.write_text(
            json.dumps(report, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

        print(f"Bundle ID: {bundle_id}")
        print(f"Executable Name: {executable}")
        print(f"Frameworks: {', '.join(frameworks) if frameworks else '(none)'}")
        print(f"App bundle: {app_prefix.rstrip('/')}")
        print(f"Extracted files: {extractor.extracted_files}")
        print(f"Files by category: {dict(extractor.category_counts)}")
        print(f"Nested asset bundles: {len(extractor.bundles)}")
        print(f"Detected signatures: {dict(extractor.format_counts)}")
        print(f"Output: {output}")
        print(f"Report: {report_path}")
        return 0
    except (OSError, ValueError, zipfile.BadZipFile, plistlib.InvalidFileException) as error:
        print(f"Extraction failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
