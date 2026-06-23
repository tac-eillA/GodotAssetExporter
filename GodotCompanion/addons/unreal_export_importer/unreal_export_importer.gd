@tool
extends EditorPlugin

const DEFAULT_EXPORT_ROOT := "res://unreal_exports"
const EXPORT_ROOT_SETTING := "unreal_export_importer/export_root"
const POSTPROCESS_SUBDIR_SETTING := "unreal_export_importer/postprocess_subdir"
const APPLY_PREFAB_ORIGIN_REBASE := false

func _enter_tree() -> void:
    _ensure_project_settings()
    add_tool_menu_item("Unreal Export Importer: Show Settings", _show_settings)
    add_tool_menu_item("Unreal Export Importer: Scan Manifests", _scan_manifests)
    add_tool_menu_item("Unreal Export Importer: Scan Import Presets", _scan_import_presets)
    add_tool_menu_item("Unreal Export Importer: Convert GLB Scenes to TSCN", _convert_glb_scenes_to_tscn)
    add_tool_menu_item("Unreal Export Importer: Post-process From Manifests", _postprocess_from_manifests)
    add_tool_menu_item("Unreal Export Importer: Apply Import Presets", _apply_import_presets)
    add_tool_menu_item("Unreal Export Importer: Clean Postprocess Output", _clean_postprocess_output)

func _exit_tree() -> void:
    remove_tool_menu_item("Unreal Export Importer: Show Settings")
    remove_tool_menu_item("Unreal Export Importer: Scan Manifests")
    remove_tool_menu_item("Unreal Export Importer: Scan Import Presets")
    remove_tool_menu_item("Unreal Export Importer: Convert GLB Scenes to TSCN")
    remove_tool_menu_item("Unreal Export Importer: Post-process From Manifests")
    remove_tool_menu_item("Unreal Export Importer: Apply Import Presets")
    remove_tool_menu_item("Unreal Export Importer: Clean Postprocess Output")

func _ensure_project_settings() -> void:
    if not ProjectSettings.has_setting(EXPORT_ROOT_SETTING):
        ProjectSettings.set_setting(EXPORT_ROOT_SETTING, DEFAULT_EXPORT_ROOT)
    if not ProjectSettings.has_setting(POSTPROCESS_SUBDIR_SETTING):
        ProjectSettings.set_setting(POSTPROCESS_SUBDIR_SETTING, "postprocess")
    ProjectSettings.add_property_info({
        "name": EXPORT_ROOT_SETTING,
        "type": TYPE_STRING,
        "hint": PROPERTY_HINT_DIR,
        "hint_string": ""
    })
    ProjectSettings.add_property_info({
        "name": POSTPROCESS_SUBDIR_SETTING,
        "type": TYPE_STRING,
        "hint": PROPERTY_HINT_NONE,
        "hint_string": ""
    })

func _export_root() -> String:
    var value := str(ProjectSettings.get_setting(EXPORT_ROOT_SETTING, DEFAULT_EXPORT_ROOT)).strip_edges()
    if value.is_empty():
        value = DEFAULT_EXPORT_ROOT
    return value.trim_suffix("/")

func _postprocess_root() -> String:
    var subdir := str(ProjectSettings.get_setting(POSTPROCESS_SUBDIR_SETTING, "postprocess")).strip_edges().replace("\\", "/")
    if subdir.is_empty():
        subdir = "postprocess"
    if subdir.begins_with("res://"):
        return subdir.trim_suffix("/")
    return _export_root().path_join(subdir).trim_suffix("/")

func _show_settings() -> void:
    print("[Unreal Export Importer] export root: %s" % _export_root())
    print("[Unreal Export Importer] postprocess root: %s" % _postprocess_root())
    print("[Unreal Export Importer] Change these under Project Settings > General > Unreal Export Importer.")

func _scan_manifests() -> void:
    var manifest_paths := _find_manifest_files(_export_root())
    print("[Unreal Export Importer] Manifest scan root: %s" % _export_root())
    if manifest_paths.is_empty():
        push_warning("No Unreal export manifests found under %s." % _export_root())
        return
    for manifest_path in manifest_paths:
        var manifest := _load_json(manifest_path)
        if manifest.is_empty():
            continue
        var export_type := str(manifest.get("export_type", "unknown"))
        var scene_path := str(manifest.get("scene", manifest.get("model", "")))
        var preset_path := str(manifest.get("godot_import_preset", ""))
        var warnings := manifest.get("warnings", [])
        print("[Unreal Export Importer] %s | type=%s | target=%s | preset=%s | warnings=%d" % [manifest_path, export_type, scene_path, preset_path, warnings.size() if typeof(warnings) == TYPE_ARRAY else 0])
        if typeof(warnings) == TYPE_ARRAY:
            for warning in warnings:
                push_warning("%s: %s" % [manifest_path, str(warning)])

func _scan_import_presets() -> void:
    var preset_paths := _find_import_preset_files(_export_root())
    if preset_paths.is_empty():
        push_warning("No Godot import preset JSON files found under %s/import_presets." % _export_root())
        return
    for preset_path in preset_paths:
        var preset := _load_json(preset_path)
        if preset.is_empty():
            continue
        print("[Unreal Export Importer] preset=%s target=%s actions=%s" % [preset_path, str(preset.get("target_file", "")), str(preset.get("actions", {}))])

func _convert_glb_scenes_to_tscn() -> void:
    var glb_paths := _find_files(_export_root(), ".glb")
    if glb_paths.is_empty():
        push_warning("No .glb scenes found under %s." % _export_root())
        return
    var converted_count := 0
    for glb_path in glb_paths:
        if _save_packed_scene_as_tscn(glb_path, glb_path.get_basename() + ".tscn", {}, {}) == OK:
            converted_count += 1
    print("[Unreal Export Importer] Converted %d GLB scene(s) to TSCN." % converted_count)
    get_editor_interface().get_resource_filesystem().scan()

func _postprocess_from_manifests() -> void:
    _postprocess_impl(false)

func _apply_import_presets() -> void:
    _postprocess_impl(true)

func _postprocess_impl(require_preset: bool) -> void:
    var manifest_paths := _find_manifest_files(_export_root())
    if manifest_paths.is_empty():
        push_warning("No Unreal export manifests found under %s." % _export_root())
        return
    _ensure_dir(_postprocess_root())
    var processed := 0
    for manifest_path in manifest_paths:
        var manifest := _load_json(manifest_path)
        if manifest.is_empty():
            continue
        var target_rel := str(manifest.get("scene", ""))
        if target_rel.is_empty():
            target_rel = _first_model_from_static_manifest(manifest)
        if target_rel.is_empty():
            continue
        var glb_path := _manifest_relative_to_res_path(_export_root(), target_rel)
        if not FileAccess.file_exists(glb_path):
            push_warning("Manifest target file does not exist yet: %s from %s" % [glb_path, manifest_path])
            continue
        var preset := _load_import_preset_for_manifest(manifest, manifest_path)
        if require_preset and preset.is_empty():
            push_warning("Skipping %s because no import preset JSON was found." % manifest_path)
            continue
        var target_path := _postprocessed_scene_path(glb_path, preset)
        if _save_packed_scene_as_tscn(glb_path, target_path, manifest, preset) == OK:
            processed += 1
    print("[Unreal Export Importer] Post-processed %d Unreal GLB scene(s)." % processed)
    get_editor_interface().get_resource_filesystem().scan()

func _clean_postprocess_output() -> void:
    if DirAccess.dir_exists_absolute(_postprocess_root()):
        _delete_dir_recursive(_postprocess_root())
    _ensure_dir(_postprocess_root())
    print("[Unreal Export Importer] Cleaned %s" % _postprocess_root())
    get_editor_interface().get_resource_filesystem().scan()

func _save_packed_scene_as_tscn(glb_path: String, target_path: String, manifest: Dictionary, preset: Dictionary) -> int:
    var resource := ResourceLoader.load(glb_path)
    if resource == null:
        push_warning("Could not load GLB: %s. Wait for Godot's import scan to finish, then try again." % glb_path)
        return ERR_CANT_OPEN
    if not resource is PackedScene:
        push_warning("Resource is not a PackedScene: %s" % glb_path)
        return ERR_INVALID_DATA
    if manifest.is_empty() and preset.is_empty():
        var err := ResourceSaver.save(resource, target_path)
        if err == OK:
            print("[Unreal Export Importer] Saved %s" % target_path)
        return err
    var imported_root := resource.instantiate()
    if imported_root == null:
        push_warning("Could not instantiate imported GLB: %s" % glb_path)
        return ERR_CANT_CREATE
    var wrapper := Node3D.new()
    wrapper.name = _safe_node_name(target_path.get_file().get_basename())
    wrapper.set_meta("unreal_export_manifest_path", _find_manifest_for_target(glb_path))
    wrapper.set_meta("unreal_export_type", str(manifest.get("export_type", preset.get("export_type", ""))))
    wrapper.set_meta("unreal_source_level", str(manifest.get("source_level", "")))
    wrapper.set_meta("unreal_tag_group", str(manifest.get("tag_group", "")))
    wrapper.set_meta("unreal_prefab_origin", manifest.get("prefab_origin", {}))
    wrapper.set_meta("unreal_validation_report", str(manifest.get("validation_report", "")))
    wrapper.set_meta("unreal_dry_run_diff_report", str(manifest.get("dry_run_diff_report", "")))
    wrapper.set_meta("unreal_godot_import_preset", preset)
    wrapper.add_child(imported_root)
    _set_owner_recursive(imported_root, wrapper)
    var actions := preset.get("actions", {})
    var should_rebase := false
    if typeof(actions) == TYPE_DICTIONARY:
        should_rebase = bool(actions.get("rebase_prefab_origin", false))
    if APPLY_PREFAB_ORIGIN_REBASE or should_rebase:
        _apply_prefab_origin_rebase(wrapper, imported_root, manifest.get("prefab_origin", {}))
    _apply_actor_metadata(wrapper, manifest)
    _apply_preset_metadata(wrapper, preset)
    var packed := PackedScene.new()
    var pack_err := packed.pack(wrapper)
    if pack_err != OK:
        push_warning("Failed to pack post-processed scene %s. Error code: %s" % [target_path, pack_err])
        return pack_err
    _ensure_dir(target_path.get_base_dir())
    var err := ResourceSaver.save(packed, target_path)
    if err == OK:
        print("[Unreal Export Importer] Saved post-processed scene %s" % target_path)
    else:
        push_warning("Failed to save %s. Error code: %s" % [target_path, err])
    return err

func _postprocessed_scene_path(glb_path: String, preset: Dictionary) -> String:
    var base := glb_path.get_file().get_basename()
    var preset_name := str(preset.get("godot_import_preset", "post"))
    preset_name = preset_name.replace(" ", "_").replace("/", "_")
    return _postprocess_root().path_join("%s.%s.post.tscn" % [base, preset_name])

func _apply_preset_metadata(root: Node, preset: Dictionary) -> void:
    if preset.is_empty():
        return
    root.set_meta("unreal_import_preset_file", str(preset.get("_preset_path", "")))
    root.set_meta("unreal_import_preset_name", str(preset.get("godot_import_preset", "")))
    root.set_meta("unreal_import_actions", preset.get("actions", {}))

func _set_owner_recursive(node: Node, owner_node: Node) -> void:
    node.owner = owner_node
    for child in node.get_children():
        _set_owner_recursive(child, owner_node)

func _apply_prefab_origin_rebase(wrapper: Node3D, imported_root: Node, origin: Variant) -> void:
    if typeof(origin) != TYPE_DICTIONARY:
        return
    var location_cm := origin.get("location_cm", [0.0, 0.0, 0.0])
    if typeof(location_cm) != TYPE_ARRAY or location_cm.size() < 3:
        return
    var offset_m := Vector3(float(location_cm[0]) / 100.0, float(location_cm[2]) / 100.0, -float(location_cm[1]) / 100.0)
    if imported_root is Node3D:
        imported_root.position -= offset_m
    wrapper.set_meta("unreal_prefab_origin_rebased", true)
    wrapper.set_meta("unreal_prefab_origin_offset_m", offset_m)

func _apply_actor_metadata(root: Node, manifest: Dictionary) -> void:
    var actors := manifest.get("actors", [])
    if typeof(actors) != TYPE_ARRAY:
        return
    for actor in actors:
        if typeof(actor) != TYPE_DICTIONARY:
            continue
        var label := str(actor.get("label", ""))
        if label.is_empty():
            continue
        var node := root.find_child(label, true, false)
        if node == null:
            continue
        node.set_meta("unreal_actor_path", str(actor.get("actor_path", "")))
        node.set_meta("unreal_actor_class", str(actor.get("class", "")))
        node.set_meta("unreal_actor_tags", actor.get("tags", []))
        node.set_meta("unreal_transform_cm", actor.get("transform", {}))

func _first_model_from_static_manifest(manifest: Dictionary) -> String:
    var models := manifest.get("models", [])
    if typeof(models) == TYPE_ARRAY and not models.is_empty() and typeof(models[0]) == TYPE_DICTIONARY:
        return str(models[0].get("model", ""))
    return ""

func _load_import_preset_for_manifest(manifest: Dictionary, manifest_path: String) -> Dictionary:
    var preset_rel := str(manifest.get("godot_import_preset", ""))
    if preset_rel.is_empty():
        var models := manifest.get("models", [])
        if typeof(models) == TYPE_ARRAY and not models.is_empty() and typeof(models[0]) == TYPE_DICTIONARY:
            preset_rel = str(models[0].get("godot_import_preset", ""))
    if preset_rel.is_empty():
        return {}
    var preset_path := _manifest_relative_to_res_path(_export_root(), preset_rel)
    var preset := _load_json(preset_path)
    preset["_preset_path"] = preset_path
    preset["_manifest_path"] = manifest_path
    return preset

func _find_manifest_files(root_path: String) -> Array[String]:
    var results: Array[String] = []
    results.append_array(_find_files(root_path.path_join("manifests"), "manifest.json"))
    results.append_array(_find_files(root_path, "godot_export_manifest.json"))
    results.append_array(_find_files(root_path.path_join("manifests"), "headless_manifest.json"))
    if results.is_empty():
        results.append_array(_find_files(root_path, "manifest.json"))
    return results

func _find_import_preset_files(root_path: String) -> Array[String]:
    return _find_files(root_path.path_join("import_presets"), "godot_import_preset.json")

func _load_json(path: String) -> Dictionary:
    if not FileAccess.file_exists(path):
        return {}
    var text := FileAccess.get_file_as_string(path)
    var parsed = JSON.parse_string(text)
    if typeof(parsed) != TYPE_DICTIONARY:
        push_warning("Could not parse JSON: %s" % path)
        return {}
    return parsed

func _manifest_relative_to_res_path(root_path: String, relative_path: String) -> String:
    var normalized := relative_path.replace("\\", "/")
    if normalized.begins_with("res://"):
        return normalized
    return root_path.path_join(normalized)

func _find_manifest_for_target(target_path: String) -> String:
    var manifest_paths := _find_manifest_files(_export_root())
    for manifest_path in manifest_paths:
        var manifest := _load_json(manifest_path)
        var target_rel := str(manifest.get("scene", ""))
        if target_rel.is_empty():
            target_rel = _first_model_from_static_manifest(manifest)
        if not target_rel.is_empty() and _manifest_relative_to_res_path(_export_root(), target_rel) == target_path:
            return manifest_path
    return ""

func _safe_node_name(value: String) -> String:
    var result := value.replace(".", "_").replace("-", "_").replace(" ", "_")
    if result.is_empty():
        result = "UnrealExport"
    return result

func _ensure_dir(path: String) -> void:
    if path.is_empty():
        return
    DirAccess.make_dir_recursive_absolute(path)

func _find_files(root_path: String, suffix: String) -> Array[String]:
    var results: Array[String] = []
    if not DirAccess.dir_exists_absolute(root_path):
        return results
    _find_files_recursive(root_path, suffix, results)
    return results

func _find_files_recursive(path: String, suffix: String, results: Array[String]) -> void:
    var dir := DirAccess.open(path)
    if dir == null:
        return
    dir.list_dir_begin()
    while true:
        var file_name := dir.get_next()
        if file_name == "":
            break
        if file_name.begins_with("."):
            continue
        var child_path := path.path_join(file_name)
        if dir.current_is_dir():
            _find_files_recursive(child_path, suffix, results)
        elif file_name.ends_with(suffix):
            results.append(child_path)
    dir.list_dir_end()

func _delete_dir_recursive(path: String) -> void:
    var dir := DirAccess.open(path)
    if dir == null:
        return
    dir.list_dir_begin()
    while true:
        var file_name := dir.get_next()
        if file_name == "":
            break
        if file_name.begins_with("."):
            continue
        var child_path := path.path_join(file_name)
        if dir.current_is_dir():
            _delete_dir_recursive(child_path)
        else:
            DirAccess.remove_absolute(child_path)
    dir.list_dir_end()
    DirAccess.remove_absolute(path)
