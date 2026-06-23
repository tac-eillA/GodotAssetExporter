# UE5.8 Test Checklist

Use this checklist before trying a Marketplace pack or a large prefab level.

## 1. Install and enable

1. Copy `GodotAssetExporter/` into `<UnrealProject>/Plugins/` or run the installer.
2. Regenerate project files if this is a C++ project.
3. Build/open the project in Unreal.
4. Confirm both plugins are enabled:
   - `GodotAssetExporter`
   - `GLTFExporter`
5. Open `Window > Godot Asset Exporter`.
6. Run `Preflight Check`.

The preflight report is written to:

```text
<ExportRoot>/logs/preflight_check.txt
```

## 2. Recommended safe starting settings

For the first UE5.8 test, use the default safe mode:

```text
Use Safe Godot Defaults: enabled
Format: GLB
Export cameras: off
Export lights: off
Export lightmaps: off
Adjust normal maps: on
Export texture transforms: on
Preserve Content Browser folders: on
Write validation reports: on
Write import preset JSON: on
```

## 3. Create a tiny controlled test scene

Create or open a blank map and add:

```text
SM_Cube_Floor
SM_Cube_Wall
SM_Ramp
PointLight_Test
Camera_Test
```

Then add/rename optional helper actors:

```text
Floor-col
Crate-convcol
Arena-navmesh
DebugBox-noimp
GodotRoot
```

Add actor tags:

```text
GodotExport:Room_A
GodotExport:Blockout
GodotNoExport
```

## 4. Test selected Static Mesh asset export

1. Select one or two Static Mesh assets in the Content Browser.
2. Run:

```text
Tools > Godot Export: Selected Static Mesh Assets
```

Expected output:

```text
models/
textures/
godot_export_manifest.json
logs/static_mesh_validation_report.txt
import_presets/
cache/last_export.json
```

## 5. Test selected actor prefab export

1. Select a few placed actors in the viewport or World Outliner.
2. Run:

```text
Tools > Godot Export: Selected Level Actors as Prefab Scene
```

Expected output:

```text
scenes/Prefab_<Level>_SelectedActors_*.glb
manifests/Prefab_<Level>_SelectedActors_*_manifest.json
logs/*_validation_report.txt
logs/*_dry_run_diff.txt
logs/*_dry_run_diff.json
import_presets/*_godot_import_preset.json
```

## 6. Test actor tag groups

1. Tag a few actors with `GodotExport:Room_A`.
2. Tag a different set with `GodotExport:Room_B`.
3. Run:

```text
Tools > Godot Export: Actor Tag Groups
```

Expected result: one `.glb` scene per tag group.

## 7. Test current level export

Run:

```text
Tools > Godot Export: Current Level Scene
```

A warning should remind you that this is geometry/layout export only. Continue for the small test map.

## 8. Test Godot import

1. Copy/install the companion addon into your Godot project if you did not use the installer.
2. Enable the addon in Godot.
3. Set the addon export root under:

```text
Project Settings > General > Unreal Export Importer > Export Root
```

Default:

```text
res://unreal_exports
```

4. Let Godot import the `.glb` files.
5. Use:

```text
Project > Tools > Unreal Export Importer: Scan Manifests
Project > Tools > Unreal Export Importer: Scan Import Presets
Project > Tools > Unreal Export Importer: Convert GLB Scenes to TSCN
Project > Tools > Unreal Export Importer: Post-process From Manifests
```

Expected output:

```text
postprocess/*.post.tscn
```

## 9. First real-content test order

Test in this order:

1. One Static Mesh asset.
2. A selected prefab room/chunk of 5-20 actors.
3. A tag group prefab.
4. A medium modular environment chunk.
5. A full level.

Avoid starting with full Marketplace maps. It is much harder to tell whether problems are from the plugin, Unreal's GLTFExporter, Godot import settings, or unsupported Unreal-only features.

## 10. Known limitations

This plugin exports scene geometry/layout through Unreal's GLTFExporter. It does not convert:

```text
Blueprint gameplay logic
Construction scripts
Unreal gameplay components
Niagara/VFX
Landscape and foliage systems
World Partition behavior
Collision profile semantics
Complex Unreal material graphs
Networking/game rules
```
