# Godot Asset Exporter for Unreal Engine

Editor-only Unreal Engine plugin for exporting Unreal Static Mesh assets, selected level actors, prefab-style actor groups, and full levels to Godot-friendly glTF/GLB.

The plugin wraps Unreal's built-in `GLTFExporter`.

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

