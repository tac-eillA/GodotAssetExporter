#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GodotAssetExporterSettings.generated.h"

UENUM(BlueprintType)
enum class EGodotAssetExporterPreset : uint8
{
    StaticMeshLibrary UMETA(DisplayName = "Static Mesh Library"),
    PrefabSceneClean UMETA(DisplayName = "Prefab Scene - Clean"),
    PrefabSceneWithLights UMETA(DisplayName = "Prefab Scene - With Lights"),
    BlockoutGreybox UMETA(DisplayName = "Blockout / Greybox")
};

UENUM(BlueprintType)
enum class EGodotPrefabOriginMode : uint8
{
    KeepWorldOrigin UMETA(DisplayName = "Keep World Origin"),
    CenterSelectionBounds UMETA(DisplayName = "Center On Selection Bounds"),
    FirstSelectedActor UMETA(DisplayName = "Use First Selected Actor"),
    ActorNamedGodotRoot UMETA(DisplayName = "Use Actor Named GodotRoot")
};

UENUM(BlueprintType)
enum class EGodotCollisionExportMode : uint8
{
    None UMETA(DisplayName = "None"),
    GodotSuffixOnly UMETA(DisplayName = "Godot Suffix Only"),
    SimpleCollisionAsColOnly UMETA(DisplayName = "Simple Collision Helpers As -colonly"),
    ConvexCollisionAsConvColOnly UMETA(DisplayName = "Convex Collision Helpers As -convcolonly")
};

UENUM(BlueprintType)
enum class EGodotImportPreset : uint8
{
    StaticMeshProp UMETA(DisplayName = "Static Mesh Prop"),
    LevelChunk UMETA(DisplayName = "Level Chunk"),
    Greybox UMETA(DisplayName = "Greybox"),
    FinalEnvironment UMETA(DisplayName = "Final Environment")
};

UENUM(BlueprintType)
enum class EGodotOutputCleanupMode : uint8
{
    KeepExisting UMETA(DisplayName = "Keep Existing Files"),
    ArchiveExisting UMETA(DisplayName = "Archive Existing Files Before Overwrite"),
    CleanGeneratedFolders UMETA(DisplayName = "Clean Generated Export Folders Before Export")
};

UCLASS(config = EditorPerProjectUserSettings, defaultconfig, meta = (DisplayName = "Godot Asset Exporter"))
class GODOTASSETEXPORTER_API UGodotAssetExporterSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UGodotAssetExporterSettings();

    /** Relative paths are resolved from the Unreal project directory. Used when Export To Godot Project is disabled. */
    UPROPERTY(EditAnywhere, config, Category = "Export Target")
    FString DefaultExportDirectory;

    /** Export directly into a Godot project folder, usually something like /MyGodotProject/external/unreal_exports or /MyGodotProject/assets/unreal_exports. */
    UPROPERTY(EditAnywhere, config, Category = "Export Target")
    bool bExportToGodotProject;

    /** Destination folder inside your Godot project. This should be a real filesystem path, not res://. */
    UPROPERTY(EditAnywhere, config, Category = "Export Target", meta = (EditCondition = "bExportToGodotProject"))
    FString GodotProjectExportDirectory;

    /** Ask for an export folder each time a menu item is run. If disabled, the configured export target is used. */
    UPROPERTY(EditAnywhere, config, Category = "Export Target")
    bool bPromptForFolder;

    /** Export .glb instead of .gltf. GLB is recommended for moving scenes/prefabs into Godot. */
    UPROPERTY(EditAnywhere, config, Category = "Export Format")
    bool bExportBinaryGLB;

    /** Quick export behavior profile. The individual checkboxes below are still visible for manual tuning. */
    UPROPERTY(EditAnywhere, config, Category = "Presets")
    EGodotAssetExporterPreset ActivePreset;

    /** Apply the active preset at export time. Disable if you want only the manual checkboxes to matter. */
    UPROPERTY(EditAnywhere, config, Category = "Presets")
    bool bUseActivePreset;

    /** Force conservative Godot-friendly defaults at export time: GLB, no lights/cameras/lightmaps, adjusted normal maps, texture transforms, preserved folders, validation, manifests, and import preset metadata. */
    UPROPERTY(EditAnywhere, config, Category = "Presets")
    bool bUseSafeGodotDefaults;

    /** Also export textures referenced by selected mesh assets' assigned materials as PNG files. */
    UPROPERTY(EditAnywhere, config, Category = "Static Mesh Export")
    bool bExportTexturesSeparately;

    /** Recreate /Game folder paths under models/ and textures/ instead of flattening all files. */
    UPROPERTY(EditAnywhere, config, Category = "Static Mesh Export")
    bool bPreserveContentBrowserFolders;

    /** Export camera components when exporting selected actors or the current level. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bExportSceneCameras;

    /** Export directional, point, and spot light components when exporting selected actors or the current level. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bExportSceneLights;

    /** Include actors/components flagged Hidden In Game during scene export. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bExportHiddenInGameSceneActors;

    /** Export Lightmass lightmaps during scene export when supported by the active Unreal version's GLTFExporter. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bExportSceneLightmaps;

    /** Export vertex colors on meshes. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bExportVertexColors;

    /** Adjust normal maps from Unreal's convention to glTF's convention. Recommended for Godot. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bAdjustNormalMapsForGLTF;

    /** Export UV texture transforms used in materials when supported by Unreal's GLTFExporter. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bExportTextureTransforms;

    /** Use a timestamp in selected-actor prefab file names to avoid overwriting previous prefab exports. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export")
    bool bTimestampSelectedActorPrefabNames;

    /** Prefab origin metadata written into selected-actor scene manifests. The Godot companion plugin can re-root imported scenes from this data. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export|Prefab Origin")
    EGodotPrefabOriginMode PrefabOriginMode;

    /** Actor label used when Prefab Origin Mode is Actor Named GodotRoot. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export|Prefab Origin")
    FString GodotRootActorLabel;

    /** Prefix used to discover actor export groups, for example GodotExport:Room_A. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export|Actor Tags")
    FString ExportTagPrefix;

    /** When exporting the current level manifest, skip actors tagged GodotNoExport. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export|Actor Tags")
    bool bSkipGodotNoExportTaggedActorsInManifest;

    /** How aggressively the Unreal side should prepare collision helper labels for Godot import. */
    UPROPERTY(EditAnywhere, config, Category = "Scene Export|Collision")
    EGodotCollisionExportMode CollisionExportMode;

    /** Write warnings and compatibility notes under logs/ for every export. */
    UPROPERTY(EditAnywhere, config, Category = "Validation")
    bool bWriteValidationReports;

    /** Warn when selected actor labels do not use a Godot import-hint suffix for collision/navigation helper meshes. */
    UPROPERTY(EditAnywhere, config, Category = "Validation")
    bool bWarnAboutMissingGodotImportHints;

    /** Show a reminder before exporting the whole current level that this is a geometry/layout export, not a gameplay/project converter. */
    UPROPERTY(EditAnywhere, config, Category = "Validation")
    bool bShowFullLevelExportWarning;

    /** Write cache/last export files so the plugin can re-export the last static mesh asset set. */
    UPROPERTY(EditAnywhere, config, Category = "Re-export Cache")
    bool bWriteReexportCache;

    /** Ask before continuing when the dry-run diff detects previous source snapshot data. */
    UPROPERTY(EditAnywhere, config, Category = "Dry Run / Diff")
    bool bShowDryRunDiffBeforeExport;

    /** Write diff text files under logs/ before export. */
    UPROPERTY(EditAnywhere, config, Category = "Dry Run / Diff")
    bool bWriteDryRunDiffReports;

    /** Write Godot-side import preset JSON files under import_presets/. The companion addon reads these during post-processing. */
    UPROPERTY(EditAnywhere, config, Category = "Godot Companion")
    bool bWriteGodotImportPresetFiles;

    /** Import preset metadata written for the Godot companion addon. */
    UPROPERTY(EditAnywhere, config, Category = "Godot Companion")
    EGodotImportPreset GodotImportPreset;

    /** Save a versioned copy of scene manifests/presets so Godot-side post-processing can identify export history. */
    UPROPERTY(EditAnywhere, config, Category = "Godot Companion")
    bool bWriteVersionedGodotPostProcessManifests;

    /** How the exporter handles existing generated files before writing a new export. */
    UPROPERTY(EditAnywhere, config, Category = "Output Cleanup")
    EGodotOutputCleanupMode OutputCleanupMode;

    /** Keep timestamped scene exports even when selected-actor timestamping is disabled. Useful for CI artifact history. */
    UPROPERTY(EditAnywhere, config, Category = "Output Cleanup")
    bool bVersionedSceneExports;

    /** Maximum number of files to keep in archives/. Use 0 to keep all archives. */
    UPROPERTY(EditAnywhere, config, Category = "Output Cleanup", meta = (ClampMin = "0"))
    int32 MaxArchivedExportsToKeep;

    /** Write a machine-readable diff JSON beside the human-readable dry-run diff text. */
    UPROPERTY(EditAnywhere, config, Category = "Dry Run / Diff")
    bool bWriteDryRunDiffJson;

    /** Include actor/material/asset path hashes in dry-run snapshots where available. */
    UPROPERTY(EditAnywhere, config, Category = "Dry Run / Diff")
    bool bUseDetailedDryRunSnapshots;
};
