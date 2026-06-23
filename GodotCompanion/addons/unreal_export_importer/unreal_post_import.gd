@tool
extends EditorScenePostImport

# Optional Godot-side post-import hook for GLB scenes exported by GodotAssetExporter.
# In Godot's Import dock, set this script as the scene's Custom Script Import when
# you want the import itself to stamp metadata before the companion addon creates
# wrapper .tscn scenes.

func _post_import(scene: Node) -> Object:
    scene.set_meta("unreal_export_post_import_processed", true)
    if has_method("get_source_file"):
        scene.set_meta("unreal_export_source_file", get_source_file())
    scene.set_meta("unreal_export_note", "Processed by unreal_post_import.gd. Use the Unreal Export Importer addon for manifest-aware wrapping and metadata.")
    return scene
