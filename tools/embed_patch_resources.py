#!/usr/bin/env python3
import pathlib
import re
import sys


def collect_files(directory: pathlib.Path) -> dict[str, pathlib.Path]:
    files: dict[str, pathlib.Path] = {}
    if not directory.is_dir():
        return files

    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise SystemExit(f"Symlinks are not supported in {directory}: {path}")
        if path.is_file():
            relative = path.relative_to(directory).as_posix()
            files[relative] = path
    return files


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit(
            "Usage: embed_patch_resources.py <version-file> <res-dir> "
            "<original-dir> <build-dir>"
        )

    version_file, res_dir, original_dir, build_dir = map(
        pathlib.Path, sys.argv[1:]
    )
    version = version_file.read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise SystemExit(
            "patch-version.txt must contain a version like 1.0.0"
        )

    replacements = collect_files(res_dir)
    originals = collect_files(original_dir)
    if not replacements:
        raise SystemExit(f"No patch files found in {res_dir}")
    if not originals:
        raise SystemExit(f"No original files found in {original_dir}")

    missing = sorted(replacements.keys() - originals.keys())
    extra = sorted(originals.keys() - replacements.keys())
    if missing or extra:
        if missing:
            print("Originals missing for:", file=sys.stderr)
            for path in missing:
                print(f"  {path}", file=sys.stderr)
        if extra:
            print("Originals without replacements:", file=sys.stderr)
            for path in extra:
                print(f"  {path}", file=sys.stderr)
        raise SystemExit("Release/res and Release/original must have matching files.")

    build_dir.mkdir(parents=True, exist_ok=True)
    rc_path = build_dir / "patch_resources.rc"
    header_path = build_dir / "PatchResources.h"

    entries: list[tuple[int, str, pathlib.Path]] = []
    resource_id = 1
    for category, files in (("res", replacements), ("original", originals)):
        for relative, source in sorted(files.items()):
            entries.append((resource_id, f"{category}/{relative}", source))
            resource_id += 1

    if resource_id > 65536:
        raise SystemExit("Too many embedded patch files (maximum 65535).")

    def rc_quote(path: pathlib.Path) -> str:
        value = path.resolve().as_posix()
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'

    with rc_path.open("w", encoding="utf-8", newline="\n") as rc:
        for resource_id, _, source in entries:
            rc.write(f"{resource_id} RCDATA {rc_quote(source)}\n")

    with header_path.open("w", encoding="utf-8", newline="\n") as header:
        header.write("#pragma once\n\n#include <cstddef>\n\n")
        header.write("namespace PatchResources\n{\n")
        header.write(f'    inline constexpr char version[] = "{version}";\n')
        header.write("    struct EmbeddedFile { int id; const char* path; };\n")
        header.write("    inline constexpr EmbeddedFile files[] = {\n")
        for resource_id, path, _ in entries:
            header.write(f'        {{{resource_id}, "{path}"}},\n')
        header.write("    };\n")
        header.write(f"    inline constexpr std::size_t replacementCount = {len(replacements)};\n")
        header.write(f"    inline constexpr std::size_t originalCount = {len(originals)};\n")
        header.write("}\n")


if __name__ == "__main__":
    main()
