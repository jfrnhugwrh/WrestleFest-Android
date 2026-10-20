#!/usr/bin/env python3
"""Compile Android expansion assets into an OBB file and manage uncompressed assets."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
import time
import zipfile
import zlib
from pathlib import Path


DEFAULT_ASSETS_DIR = (
    Path(__file__).resolve().parent
    / "android_project"
    / "app"
    / "src"
    / "main"
    / "assets"
)
DEFAULT_PACKAGE_NAME = "com.mfm.wrestlefest"
DEFAULT_VERSION_CODE = 1


def compile_obb(
    assets_dir: Path,
    output_obb: Path,
    delete_uncompressed: bool = False,
) -> int:
    assets_dir = assets_dir.resolve()
    output_obb = output_obb.resolve()

    if not assets_dir.is_dir():
        print(f"Error: Assets directory not found: {assets_dir}", file=sys.stderr, flush=True)
        return 1

    print(f"Scanning assets in: {assets_dir}", flush=True)
    source_files: list[tuple[Path, str, int]] = []
    for root, _, filenames in os.walk(assets_dir):
        for filename in filenames:
            file_path = Path(root) / filename
            rel_path = file_path.relative_to(assets_dir).as_posix()
            source_files.append((file_path, rel_path, file_path.stat().st_size))

    if not source_files:
        print("Error: No asset files found to compile.", file=sys.stderr, flush=True)
        return 1

    source_files.sort(key=lambda x: x[1])
    total_uncompressed_bytes = sum(size for _, _, size in source_files)
    print(f"Found {len(source_files)} files ({total_uncompressed_bytes / (1024 * 1024):.2f} MB uncompressed)", flush=True)

    output_obb.parent.mkdir(parents=True, exist_ok=True)
    temp_obb = output_obb.with_name(f"{output_obb.name}.tmp")
    if temp_obb.exists():
        temp_obb.unlink()

    start_time = time.time()
    print(f"Creating OBB: {output_obb.name}...", flush=True)

    with zipfile.ZipFile(temp_obb, "w", compression=zipfile.ZIP_DEFLATED) as zip_out:
        for idx, (file_path, rel_path, _) in enumerate(source_files, 1):
            if idx % 150 == 0 or idx == len(source_files):
                print(f"  Packaging [{idx}/{len(source_files)}] {rel_path}...", flush=True)
            zip_out.write(file_path, rel_path)

    elapsed = time.time() - start_time
    compressed_size = temp_obb.stat().st_size
    print(f"Archived {len(source_files)} files in {elapsed:.1f}s.", flush=True)
    print(f"Compressed size: {compressed_size / (1024 * 1024):.2f} MB (saved {(total_uncompressed_bytes - compressed_size) / (1024 * 1024):.2f} MB)", flush=True)

    # Verification phase
    print("Verifying OBB archive structure and entries...", flush=True)
    if not zipfile.is_zipfile(temp_obb):
        temp_obb.unlink()
        print("Error: Output is not a valid zip archive.", file=sys.stderr, flush=True)
        return 1

    with zipfile.ZipFile(temp_obb, "r") as zip_in:
        infolist = zip_in.infolist()
        if len(infolist) != len(source_files):
            temp_obb.unlink()
            print(f"Error: File count mismatch: expected {len(source_files)}, got {len(infolist)}", file=sys.stderr, flush=True)
            return 1

        info_map = {info.filename: info for info in infolist}
        for _, rel_path, expected_size in source_files:
            info = info_map.get(rel_path)
            if info is None:
                temp_obb.unlink()
                print(f"Error: Missing entry in OBB: {rel_path}", file=sys.stderr, flush=True)
                return 1
            if info.file_size != expected_size:
                temp_obb.unlink()
                print(f"Error: Size mismatch for {rel_path}: expected {expected_size}, got {info.file_size}", file=sys.stderr, flush=True)
                return 1

        # Test decompression of sample files across the archive
        sample_indices = [0, len(source_files) // 4, len(source_files) // 2, (3 * len(source_files)) // 4, len(source_files) - 1]
        for s_idx in sample_indices:
            sample_rel = source_files[s_idx][1]
            data = zip_in.read(sample_rel)
            if len(data) != source_files[s_idx][2]:
                temp_obb.unlink()
                print(f"Error: Sample decompression size mismatch for {sample_rel}", file=sys.stderr, flush=True)
                return 1

    # Atomic move to final destination
    if output_obb.exists():
        output_obb.unlink()
    temp_obb.rename(output_obb)
    print(f"Verification successful: 100% of {len(source_files)} files verified intact.", flush=True)
    print(f"Successfully compiled OBB: {output_obb} ({output_obb.stat().st_size / (1024 * 1024):.2f} MB)", flush=True)

    # Deletion of uncompressed files
    if delete_uncompressed:
        print(f"Deleting uncompressed asset files from: {assets_dir}...", flush=True)
        deleted_count = 0
        for item in assets_dir.iterdir():
            if item.is_dir():
                shutil.rmtree(item)
                deleted_count += 1
            elif item.is_file():
                item.unlink()
                deleted_count += 1
        print(f"Deleted {deleted_count} top-level asset directories/files ({len(source_files)} uncompressed files total).", flush=True)
        print(f"Assets directory is now clean: {assets_dir}", flush=True)

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile assets into an Android OBB expansion file and delete uncompressed files."
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=DEFAULT_ASSETS_DIR,
        help="Path to uncompressed assets directory (default: %(default)s)",
    )
    parser.add_argument(
        "--package-name",
        type=str,
        default=DEFAULT_PACKAGE_NAME,
        help="Android package name (default: %(default)s)",
    )
    parser.add_argument(
        "--version-code",
        type=int,
        default=DEFAULT_VERSION_CODE,
        help="App versionCode (default: %(default)s)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output OBB file path (default: main.<versionCode>.<package_name>.obb in project root)",
    )
    parser.add_argument(
        "--delete-uncompressed",
        action="store_true",
        help="Delete uncompressed asset files after successful compilation and verification",
    )

    args = parser.parse_args()

    output_path = args.output
    if output_path is None:
        output_name = f"main.{args.version_code}.{args.package_name}.obb"
        output_path = (
            Path(__file__).resolve().parent
            / "obb"
            / args.package_name
            / output_name
        )

    res = compile_obb(
        assets_dir=args.assets_dir,
        output_obb=output_path,
        delete_uncompressed=args.delete_uncompressed,
    )
    if res == 0:
        # Also ensure a root copy exists for convenience
        root_obb = Path(__file__).resolve().parent / output_path.name
        if output_path != root_obb:
            try:
                shutil.copyfile(output_path, root_obb)
            except OSError:
                pass
    return res


if __name__ == "__main__":
    raise SystemExit(main())
