# Godot Asset Exporter for Unreal Engine

Editor-only Unreal Engine plugin for exporting Unreal Static Mesh assets, selected level actors, prefab-style actor groups, and full levels to Godot-friendly glTF/GLB.

The plugin wraps Unreal's built-in `GLTFExporter` plugin and adds Godot-oriented workflow features around it: presets, manifests, validation, dry-run diffs, import-hint suffixes, prefab-origin metadata, output cleanup/archive controls, Godot import preset metadata, a Godot companion addon, and first-pass CI/headless export support.

## Install

Copy this folder to:

```text
<YourUnrealProject>/Plugins/GodotAssetExporter/
```

Or run the installer from the unzipped plugin folder:

```bash
python install_godot_asset_exporter.py /path/to/MyProject.uproject
```

With the optional Godot companion addon:

```bash
python install_godot_asset_exporter.py /path/to/MyProject.uproject --godot-project /path/to/MyGodotProject
```

Windows PowerShell:

```powershell
.\Install-GodotAssetExporter.ps1 "C:\Path\To\MyProject.uproject" -GodotProject "C:\Path\To\MyGodotProject"
```

The plugin descriptor enables `GLTFExporter` as a dependency. The installers also patch the `.uproject` to enable both plugins.

## Main Unreal UI

Use either:

```text
Window > Godot Asset Exporter
```

or:

```text
Tools > Godot Export: ...
```

The dockable panel exposes the common workflow:

- Preflight Check
- Validate Selection
- Export Selected Static Mesh Assets
- Export Selected Actors as Prefab Scene
- Export Current Level Scene
- Export Actor Tag Groups
- Re-export Last Static Mesh Set
- Clean Generated Export Folders
- Copy Last Godot `res://` Path
- Open Export Folder

## Export modes

### Static Mesh asset export

Select Static Mesh assets in the Content Browser, then run:

```text
Tools > Godot Export: Selected Static Mesh Assets
```

Output:

```text
GodotExports/
  models/
  textures/
  logs/
  cache/
  import_presets/
  archives/
  godot_export_manifest.json
```

With folder preservation enabled, `/Game/Environment/Walls/SM_Wall_A` exports to:

```text
models/Environment/Walls/SM_Wall_A.glb
```

### Selected actors as prefab scene

Select actors in the viewport or World Outliner, then run:

```text
Tools > Godot Export: Selected Level Actors as Prefab Scene
```

Output:

```text
GodotExports/
  scenes/Prefab_<Level>_SelectedActors_<timestamp>.glb
  manifests/Prefab_<Level>_SelectedActors_<timestamp>_manifest.json
  logs/Prefab_<Level>_SelectedActors_<timestamp>_validation_report.txt
```

The manifest includes actor labels, classes, tags, transforms, validation warnings, dry-run diff path, and prefab-origin metadata.

### Current level scene

Run:

```text
Tools > Godot Export: Current Level Scene
```

This exports the current `UWorld` as one `.glb` scene.

### Actor tag groups

Tag actors with:

```text
GodotExport
GodotExport:Room_A
GodotExport:Blockout
GodotNoExport
```

Then run:

```text
Tools > Godot Export: Actor Tag Groups
```

Each group exports as its own prefab scene.

## Pre-test hardening features

### Preflight Check

Before your first UE5.8 test, run:

```text
Tools > Godot Export: Preflight Check
```

It checks:

- `GLTFExporter` plugin presence/enabled state
- Export root creation/writability
- Current editor world availability
- Selected Static Mesh asset count
- Selected level actor count
- Godot `project.godot` detection when exporting into a Godot project
- Companion addon installation detection

Report output:

```text
logs/preflight_check.txt
```

### Safe Godot Defaults

`Use Safe Godot Defaults` is enabled by default. It forces conservative first-test behavior:

```text
GLB output
No cameras
No lights
No lightmaps
Adjusted normal maps
Texture transforms
Folder preservation
Validation reports
Import preset metadata
```

Disable it later if you want manual/preset settings to control every option.

### Godot path copy helper

After an export, run:

```text
Tools > Godot Export: Copy Last Godot res:// Path
```

This reads `cache/last_export.json`, converts the first output to a Godot `res://` path when possible, and copies it to the clipboard.

### Full-level warning

`Export Current Level Scene` shows a reminder that this is a glTF geometry/layout export. It does not convert gameplay logic, Blueprint behavior, landscapes, World Partition behavior, or complex material graphs. You can disable the reminder in plugin settings.

### Testing checklist

See:

```text
TESTING.md
```

for the recommended UE5.8 smoke-test order.

## QoL features included

### 1. Dockable panel

Open it from `Window > Godot Asset Exporter`.

### 2. Validate-only workflow

Run validation without exporting:

```text
Tools > Godot Export: Validate Selection
Tools > Godot Export: Validate Current Level
```

Reports are written under `logs/`.

### 3. Prefab origin metadata

Settings include:

```text
Keep World Origin
Center On Selection Bounds
Use First Selected Actor
Use Actor Named GodotRoot
```

The raw GLB export still comes from Unreal's GLTFExporter, but the manifest records origin metadata so the Godot companion addon can wrap/re-root imported scenes.

### 4. Actor tag export groups

Use `GodotExport` or `GodotExport:GroupName` actor tags to export prefab groups without manual reselection.

### 5. Collision/import-hint helpers

Menu commands can append Godot import suffixes:

```text
-col
-convcol
-colonly
-convcolonly
-navmesh
-noimp
```

There is also a command to prepare collision suffixes from the configured collision mode.

### 6. Godot companion post-processing

Included under:

```text
GodotCompanion/addons/unreal_export_importer
```

The addon can:

- Scan manifests
- Scan Unreal-written Godot import preset JSON files
- Convert GLBs to `.tscn`
- Post-process GLBs from manifests
- Apply import preset metadata
- Write `.post.tscn` wrapper scenes under `postprocess/`
- Wrap imported scenes in a root `Node3D`
- Attach Unreal metadata to nodes
- Optionally rebase prefab origin metadata, disabled by default until you verify axis/origin behavior for your project
- Clean generated postprocess output

### 7. Dry-run diffs

Before export, the plugin can compare the current source snapshot against the previous one and write reports like:

```text
logs/Prefab_Room_A_dry_run_diff.txt
logs/Prefab_Room_A_dry_run_diff.json
cache/last_Prefab_Room_A_snapshot.txt
```

The diff is lightweight. It compares source paths, labels, transforms, tags, LOD/material counts, and selection membership; it is not a binary asset hash.

### 8. Godot import preset metadata

Exports can now write companion metadata under:

```text
import_presets/<ExportName>_godot_import_preset.json
```

These are intentionally not raw Godot `.import` files. They are safer, versionable instructions for the companion addon: save as TSCN, wrap scene, attach metadata, honor collision suffixes, and optionally apply project-specific material/collision cleanup.

### 9. Companion post-import script

The addon now includes:

```text
addons/unreal_export_importer/unreal_post_import.gd
```

You can set this as a GLB scene's Custom Script Import in Godot when you want import-time metadata stamping. The main addon menu still performs manifest-aware post-processing and wrapper scene creation.

### 10. Expanded dry-run diff output

Dry-run diffs now write both human-readable text and machine-readable JSON, which is friendlier for CI logs and future Godot-side comparisons.

### 11. Output cleanup/archive controls

New settings and commands can clean generated folders or archive existing scene files before overwrite. Archives are written under:

```text
archives/<timestamp>/
```

The manual clean command preserves `archives/` and `cache/`.

### 12. First-pass CI/headless export support

A commandlet scaffold is included:

```powershell
UnrealEditor-Cmd.exe MyProject.uproject -run=GodotAssetExporter -Mode=CurrentLevel -Map=/Game/Maps/LVL_TestArena -ExportRoot=C:/MyGodotProject/unreal_exports
```

Tag-group export from CI:

```powershell
UnrealEditor-Cmd.exe MyProject.uproject -run=GodotAssetExporter -Mode=TagGroup -Map=/Game/Maps/LVL_TestArena -Tag=Room_A -ExportRoot=C:/MyGodotProject/unreal_exports
```

Commandlet manifests write scene paths relative to `-ExportRoot`, which keeps Godot companion post-processing portable.

Useful switches:

```text
-GLTF
-ExportCameras
-ExportLights
-ExportHiddenInGame
-ExportLightmaps
-NoVertexColors
-NoAdjustNormalMaps
-NoTextureTransforms
-OutputName=MySceneName
```

## Settings

Find settings under:

```text
Project Settings > Plugins > Godot Asset Exporter
```
