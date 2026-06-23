#!/usr/bin/env python3
"""
Install GodotAssetExporter into an Unreal project and enable required plugins.

Usage:
  python install_godot_asset_exporter.py /path/to/MyProject.uproject
  python install_godot_asset_exporter.py /path/to/MyProjectDirectory
  python install_godot_asset_exporter.py /path/to/MyProject.uproject --godot-project /path/to/MyGodotProject

What it does:
  1. Copies this GodotAssetExporter plugin folder into <Project>/Plugins/GodotAssetExporter
     unless it is already running from that location.
  2. Creates a backup of the .uproject file.
  3. Adds/enables both GodotAssetExporter and GLTFExporter in the .uproject Plugins array.
  4. Optionally copies the Godot companion plugin into <GodotProject>/addons/unreal_export_importer.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from datetime import datetime
from pathlib import Path

PLUGIN_NAME = "GodotAssetExporter"
REQUIRED_PROJECT_PLUGINS = ["GLTFExporter", PLUGIN_NAME]
EXCLUDED_COPY_NAMES = {
    ".git",
    ".vs",
    "Binaries",
    "Intermediate",
    "Saved",
    "DerivedDataCache",
    "__pycache__",
}
EXCLUDED_COPY_PATTERNS = ["*.zip", "*.pyc", "*.pdb", "*.obj"]


def find_uproject(path_arg: str) -> Path:
    path = Path(path_arg).expanduser().resolve()

    if path.is_file() and path.suffix.lower() == ".uproject":
        return path

    if path.is_dir():
        matches = sorted(path.glob("*.uproject"))
        if len(matches) == 1:
            return matches[0]
        if not matches:
            raise FileNotFoundError(f"No .uproject file found in: {path}")
        names = ", ".join(m.name for m in matches)
        raise RuntimeError(f"Multiple .uproject files found in {path}: {names}. Pass one explicitly.")

    raise FileNotFoundError(f"Project path does not exist: {path}")


def copy_plugin_to_project(source_plugin_dir: Path, project_dir: Path) -> Path:
    dest_plugin_dir = project_dir / "Plugins" / PLUGIN_NAME

    if source_plugin_dir.resolve() == dest_plugin_dir.resolve():
        print(f"Plugin is already in project: {dest_plugin_dir}")
        return dest_plugin_dir

    source_descriptor = source_plugin_dir / f"{PLUGIN_NAME}.uplugin"
    if not source_descriptor.exists():
        raise FileNotFoundError(f"Expected plugin descriptor not found: {source_descriptor}")

    dest_plugin_dir.parent.mkdir(parents=True, exist_ok=True)

    if dest_plugin_dir.exists():
        print(f"Replacing existing plugin folder: {dest_plugin_dir}")
        shutil.rmtree(dest_plugin_dir)

    def ignore_filter(current_dir: str, names: list[str]) -> set[str]:
        ignored: set[str] = set()
        for name in names:
            if name in EXCLUDED_COPY_NAMES:
                ignored.add(name)
                continue
            for pattern in EXCLUDED_COPY_PATTERNS:
                if Path(name).match(pattern):
                    ignored.add(name)
                    break
        return ignored

    shutil.copytree(source_plugin_dir, dest_plugin_dir, ignore=ignore_filter)
    print(f"Copied plugin to: {dest_plugin_dir}")
    return dest_plugin_dir


def enable_plugins_in_uproject(uproject_path: Path) -> None:
    raw = uproject_path.read_text(encoding="utf-8-sig")
    data = json.loads(raw)

    plugins = data.setdefault("Plugins", [])
    if not isinstance(plugins, list):
        raise RuntimeError(f"Unexpected .uproject format: Plugins is not a list in {uproject_path}")

    changed = False

    for required_name in REQUIRED_PROJECT_PLUGINS:
        existing = next((p for p in plugins if isinstance(p, dict) and p.get("Name") == required_name), None)
        if existing is None:
            plugins.append({"Name": required_name, "Enabled": True})
            print(f"Added plugin entry: {required_name}=Enabled")
            changed = True
        elif existing.get("Enabled") is not True:
            existing["Enabled"] = True
            print(f"Enabled existing plugin entry: {required_name}")
            changed = True
        else:
            print(f"Already enabled: {required_name}")

    if not changed:
        print(".uproject already had all required plugins enabled.")
        return

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_path = uproject_path.with_suffix(uproject_path.suffix + f".bak-{stamp}")
    shutil.copy2(uproject_path, backup_path)
    uproject_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    print(f"Backed up original project file to: {backup_path}")
    print(f"Updated project file: {uproject_path}")


def copy_godot_companion(source_plugin_dir: Path, godot_project_dir_arg: str) -> None:
    godot_project_dir = Path(godot_project_dir_arg).expanduser().resolve()
    if not godot_project_dir.exists() or not godot_project_dir.is_dir():
        raise FileNotFoundError(f"Godot project directory does not exist: {godot_project_dir}")

    source_addon_dir = source_plugin_dir / "GodotCompanion" / "addons" / "unreal_export_importer"
    if not source_addon_dir.exists():
        raise FileNotFoundError(f"Godot companion addon not found: {source_addon_dir}")

    dest_addon_dir = godot_project_dir / "addons" / "unreal_export_importer"
    dest_addon_dir.parent.mkdir(parents=True, exist_ok=True)

    if dest_addon_dir.exists():
        print(f"Replacing existing Godot companion addon: {dest_addon_dir}")
        shutil.rmtree(dest_addon_dir)

    shutil.copytree(source_addon_dir, dest_addon_dir)
    print(f"Copied Godot companion addon to: {dest_addon_dir}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Install GodotAssetExporter and enable GLTFExporter in an Unreal project.")
    parser.add_argument("project", help="Path to a .uproject file or a directory containing one.")
    parser.add_argument(
        "--no-copy",
        action="store_true",
        help="Only patch the .uproject file; do not copy the plugin into the project.",
    )
    parser.add_argument(
        "--godot-project",
        help="Optional path to a Godot project directory. Copies GodotCompanion/addons/unreal_export_importer into its addons folder.",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    uproject_path = find_uproject(args.project)
    project_dir = uproject_path.parent

    if not args.no_copy:
        copy_plugin_to_project(script_dir, project_dir)

    enable_plugins_in_uproject(uproject_path)

    if args.godot_project:
        copy_godot_companion(script_dir, args.godot_project)

    print("\nDone. Regenerate project files/build if your project uses C++, then restart Unreal Editor. Enable the Godot companion plugin inside Godot if you installed it.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
