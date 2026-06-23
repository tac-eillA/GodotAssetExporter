# Unreal Export Importer Godot Companion

Copy `addons/unreal_export_importer` into your Godot project's `addons/` folder, then enable it from:

```text
Project > Project Settings > Plugins
```

Default expected export root:

```text
res://unreal_exports
```

Configurable Godot project settings:

```text
Project Settings > General > Unreal Export Importer > Export Root
Project Settings > General > Unreal Export Importer > Postprocess Subdir
```

Use `Unreal Export Importer: Show Settings` from `Project > Tools` to print the active paths.

Menu items added under Project > Tools:

```text
Unreal Export Importer: Show Settings
Unreal Export Importer: Scan Manifests
Unreal Export Importer: Scan Import Presets
Unreal Export Importer: Convert GLB Scenes to TSCN
Unreal Export Importer: Post-process From Manifests
Unreal Export Importer: Apply Import Presets
Unreal Export Importer: Clean Postprocess Output
```

The Unreal plugin writes JSON files under `import_presets/`. These are safer companion-addon instructions rather than raw Godot `.import` files. The addon reads them, creates `.post.tscn` wrapper scenes under `postprocess/`, and attaches Unreal metadata to the wrapper and matching imported nodes.

`unreal_post_import.gd` is also included as an optional Godot `EditorScenePostImport` script. Set it as a GLB scene's Custom Script Import if you want import-time metadata stamping; use the addon menu for full manifest-aware cleanup.

Prefab-origin rebasing is disabled by default. Turn on `APPLY_PREFAB_ORIGIN_REBASE` in `unreal_export_importer.gd` only after you verify axis/origin behavior for your project.
