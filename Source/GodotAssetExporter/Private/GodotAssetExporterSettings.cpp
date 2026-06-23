#include "GodotAssetExporterSettings.h"

UGodotAssetExporterSettings::UGodotAssetExporterSettings()
{
    DefaultExportDirectory = TEXT("Saved/GodotExports");
    bExportToGodotProject = false;
    GodotProjectExportDirectory = TEXT("");
    bPromptForFolder = true;
    bExportBinaryGLB = true;

    ActivePreset = EGodotAssetExporterPreset::PrefabSceneClean;
    bUseActivePreset = true;
    bUseSafeGodotDefaults = true;

    bExportTexturesSeparately = true;
    bPreserveContentBrowserFolders = true;

    bExportSceneCameras = false;
    bExportSceneLights = false;
    bExportHiddenInGameSceneActors = false;
    bExportSceneLightmaps = false;
    bExportVertexColors = true;
    bAdjustNormalMapsForGLTF = true;
    bExportTextureTransforms = true;
    bTimestampSelectedActorPrefabNames = true;

    PrefabOriginMode = EGodotPrefabOriginMode::CenterSelectionBounds;
    GodotRootActorLabel = TEXT("GodotRoot");
    ExportTagPrefix = TEXT("GodotExport");
    bSkipGodotNoExportTaggedActorsInManifest = true;
    CollisionExportMode = EGodotCollisionExportMode::GodotSuffixOnly;

    bWriteValidationReports = true;
    bWarnAboutMissingGodotImportHints = false;
    bShowFullLevelExportWarning = true;
    bWriteReexportCache = true;

    bShowDryRunDiffBeforeExport = true;
    bWriteDryRunDiffReports = true;
    bWriteDryRunDiffJson = true;
    bUseDetailedDryRunSnapshots = true;

    bWriteGodotImportPresetFiles = true;
    GodotImportPreset = EGodotImportPreset::LevelChunk;
    bWriteVersionedGodotPostProcessManifests = true;

    OutputCleanupMode = EGodotOutputCleanupMode::ArchiveExisting;
    bVersionedSceneExports = false;
    MaxArchivedExportsToKeep = 25;
}
