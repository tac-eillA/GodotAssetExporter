#include "GodotAssetExporterSettings.h"

#include "AssetRegistry/AssetData.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Exporters/Exporter.h"
#include "Exporters/GLTFExporter.h"
#include "Exporters/TextureExporterPNG.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IPluginManager.h"
#include "IDesktopPlatform.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Options/GLTFExportOptions.h"
#include "RHI.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FGodotAssetExporterModule"

DEFINE_LOG_CATEGORY_STATIC(LogGodotAssetExporter, Log, All);
static const FName GodotAssetExporterTabName(TEXT("GodotAssetExporter"));

namespace GodotAssetExporter
{
    struct FExportedMeshRecord
    {
        FString AssetPath;
        FString ModelFile;
        FString ImportPresetFile;
        TArray<FString> TextureFiles;
        TArray<FString> Warnings;
    };

    enum class ESceneExportType : uint8
    {
        SelectedActorsPrefab,
        CurrentLevel,
        TaggedActorGroup
    };

    struct FPrefabOriginRecord
    {
        EGodotPrefabOriginMode Mode = EGodotPrefabOriginMode::KeepWorldOrigin;
        FString ModeName = TEXT("keep_world_origin");
        FString RootActorLabel;
        bool bRootActorFound = false;
        FVector LocationCm = FVector::ZeroVector;
        FRotator RotationDeg = FRotator::ZeroRotator;
        FVector SelectionBoundsExtentCm = FVector::ZeroVector;
    };

    struct FSceneExportRecord
    {
        ESceneExportType ExportType = ESceneExportType::CurrentLevel;
        FString SourceLevel;
        FString SceneFile;
        FString ValidationReportFile;
        FString DryRunDiffReportFile;
        FString ImportPresetFile;
        FString TagGroup;
        FPrefabOriginRecord PrefabOrigin;
        TArray<AActor*> Actors;
        TArray<FString> Warnings;
        bool bSelectedActorsSetWasEmpty = false;
    };

    struct FResolvedExportProfile
    {
        bool bExportCameras = false;
        bool bExportLights = false;
        bool bExportHiddenInGame = false;
        bool bExportLightmaps = false;
        bool bExportVertexColors = true;
        bool bAdjustNormalmaps = true;
        bool bExportTextureTransforms = true;
        bool bPreserveFolders = true;
        bool bExportTexturesSeparately = true;
        bool bValidationReports = true;
        bool bWarnAboutMissingImportHints = false;
        bool bWriteGodotImportPresetFiles = true;
        bool bWriteVersionedGodotPostProcessManifests = true;
        bool bWriteDryRunDiffJson = true;
        bool bUseDetailedDryRunSnapshots = true;
        bool bVersionedSceneExports = false;
        int32 MaxArchivedExportsToKeep = 25;
        EGodotCollisionExportMode CollisionMode = EGodotCollisionExportMode::GodotSuffixOnly;
        EGodotImportPreset GodotImportPreset = EGodotImportPreset::LevelChunk;
        EGodotOutputCleanupMode OutputCleanupMode = EGodotOutputCleanupMode::ArchiveExisting;
        FString GodotImportPresetName = TEXT("Level Chunk");
        FString PresetName = TEXT("Manual");
    };

    static FString EscapeJsonString(const FString& Input)
    {
        FString Output = Input;
        Output.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        Output.ReplaceInline(TEXT("\""), TEXT("\\\""));
        Output.ReplaceInline(TEXT("\r"), TEXT("\\r"));
        Output.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        Output.ReplaceInline(TEXT("\t"), TEXT("\\t"));
        return Output;
    }

    static FString JsonString(const FString& Input)
    {
        return FString::Printf(TEXT("\"%s\""), *EscapeJsonString(Input));
    }

    static FString BoolToJson(const bool bValue)
    {
        return bValue ? TEXT("true") : TEXT("false");
    }

    static FString GetPresetName(const EGodotAssetExporterPreset Preset)
    {
        switch (Preset)
        {
        case EGodotAssetExporterPreset::StaticMeshLibrary:
            return TEXT("Static Mesh Library");
        case EGodotAssetExporterPreset::PrefabSceneClean:
            return TEXT("Prefab Scene - Clean");
        case EGodotAssetExporterPreset::PrefabSceneWithLights:
            return TEXT("Prefab Scene - With Lights");
        case EGodotAssetExporterPreset::BlockoutGreybox:
            return TEXT("Blockout / Greybox");
        default:
            return TEXT("Manual");
        }
    }

    static FString GetSceneExportTypeString(const ESceneExportType ExportType)
    {
        switch (ExportType)
        {
        case ESceneExportType::SelectedActorsPrefab:
            return TEXT("selected_actors_prefab");
        case ESceneExportType::TaggedActorGroup:
            return TEXT("tagged_actor_group_prefab");
        case ESceneExportType::CurrentLevel:
        default:
            return TEXT("current_level_scene");
        }
    }

    static FString GetPrefabOriginModeString(const EGodotPrefabOriginMode Mode)
    {
        switch (Mode)
        {
        case EGodotPrefabOriginMode::KeepWorldOrigin:
            return TEXT("keep_world_origin");
        case EGodotPrefabOriginMode::CenterSelectionBounds:
            return TEXT("center_selection_bounds");
        case EGodotPrefabOriginMode::FirstSelectedActor:
            return TEXT("first_selected_actor");
        case EGodotPrefabOriginMode::ActorNamedGodotRoot:
            return TEXT("actor_named_godot_root");
        default:
            return TEXT("unknown");
        }
    }

    static FString GetCollisionModeString(const EGodotCollisionExportMode Mode)
    {
        switch (Mode)
        {
        case EGodotCollisionExportMode::None:
            return TEXT("none");
        case EGodotCollisionExportMode::GodotSuffixOnly:
            return TEXT("godot_suffix_only");
        case EGodotCollisionExportMode::SimpleCollisionAsColOnly:
            return TEXT("simple_collision_helpers_colonly");
        case EGodotCollisionExportMode::ConvexCollisionAsConvColOnly:
            return TEXT("convex_collision_helpers_convcolonly");
        default:
            return TEXT("unknown");
        }
    }

    static FString GetGodotImportPresetString(const EGodotImportPreset Preset)
    {
        switch (Preset)
        {
        case EGodotImportPreset::StaticMeshProp:
            return TEXT("static_mesh_prop");
        case EGodotImportPreset::Greybox:
            return TEXT("greybox");
        case EGodotImportPreset::FinalEnvironment:
            return TEXT("final_environment");
        case EGodotImportPreset::LevelChunk:
        default:
            return TEXT("level_chunk");
        }
    }

    static FString GetOutputCleanupModeString(const EGodotOutputCleanupMode Mode)
    {
        switch (Mode)
        {
        case EGodotOutputCleanupMode::KeepExisting:
            return TEXT("keep_existing");
        case EGodotOutputCleanupMode::CleanGeneratedFolders:
            return TEXT("clean_generated_folders");
        case EGodotOutputCleanupMode::ArchiveExisting:
        default:
            return TEXT("archive_existing");
        }
    }

    static FResolvedExportProfile ResolveExportProfile(const UGodotAssetExporterSettings* Settings)
    {
        FResolvedExportProfile Profile;
        if (!Settings)
        {
            return Profile;
        }

        Profile.bExportCameras = Settings->bExportSceneCameras;
        Profile.bExportLights = Settings->bExportSceneLights;
        Profile.bExportHiddenInGame = Settings->bExportHiddenInGameSceneActors;
        Profile.bExportLightmaps = Settings->bExportSceneLightmaps;
        Profile.bExportVertexColors = Settings->bExportVertexColors;
        Profile.bAdjustNormalmaps = Settings->bAdjustNormalMapsForGLTF;
        Profile.bExportTextureTransforms = Settings->bExportTextureTransforms;
        Profile.bPreserveFolders = Settings->bPreserveContentBrowserFolders;
        Profile.bExportTexturesSeparately = Settings->bExportTexturesSeparately;
        Profile.bValidationReports = Settings->bWriteValidationReports;
        Profile.bWarnAboutMissingImportHints = Settings->bWarnAboutMissingGodotImportHints;
        Profile.bWriteGodotImportPresetFiles = Settings->bWriteGodotImportPresetFiles;
        Profile.bWriteVersionedGodotPostProcessManifests = Settings->bWriteVersionedGodotPostProcessManifests;
        Profile.bWriteDryRunDiffJson = Settings->bWriteDryRunDiffJson;
        Profile.bUseDetailedDryRunSnapshots = Settings->bUseDetailedDryRunSnapshots;
        Profile.bVersionedSceneExports = Settings->bVersionedSceneExports;
        Profile.MaxArchivedExportsToKeep = Settings->MaxArchivedExportsToKeep;
        Profile.CollisionMode = Settings->CollisionExportMode;
        Profile.GodotImportPreset = Settings->GodotImportPreset;
        Profile.OutputCleanupMode = Settings->OutputCleanupMode;
        Profile.GodotImportPresetName = GetGodotImportPresetString(Settings->GodotImportPreset);
        Profile.PresetName = Settings->bUseActivePreset ? GetPresetName(Settings->ActivePreset) : TEXT("Manual");

        if (Settings->bUseSafeGodotDefaults)
        {
            Profile.bExportCameras = false;
            Profile.bExportLights = false;
            Profile.bExportHiddenInGame = false;
            Profile.bExportLightmaps = false;
            Profile.bExportVertexColors = true;
            Profile.bAdjustNormalmaps = true;
            Profile.bExportTextureTransforms = true;
            Profile.bPreserveFolders = true;
            Profile.bExportTexturesSeparately = true;
            Profile.bValidationReports = true;
            Profile.bWriteGodotImportPresetFiles = true;
            Profile.PresetName = Settings->bUseActivePreset ? GetPresetName(Settings->ActivePreset) + TEXT(" + Safe Godot Defaults") : TEXT("Safe Godot Defaults");
            return Profile;
        }

        if (!Settings->bUseActivePreset)
        {
            return Profile;
        }

        switch (Settings->ActivePreset)
        {
        case EGodotAssetExporterPreset::StaticMeshLibrary:
            Profile.bExportCameras = false;
            Profile.bExportLights = false;
            Profile.bExportHiddenInGame = false;
            Profile.bExportLightmaps = false;
            Profile.bExportVertexColors = true;
            Profile.bAdjustNormalmaps = true;
            Profile.bExportTextureTransforms = true;
            Profile.bPreserveFolders = true;
            Profile.bExportTexturesSeparately = true;
            break;
        case EGodotAssetExporterPreset::PrefabSceneWithLights:
            Profile.bExportCameras = true;
            Profile.bExportLights = true;
            Profile.bExportHiddenInGame = false;
            Profile.bExportLightmaps = false;
            Profile.bExportVertexColors = true;
            Profile.bAdjustNormalmaps = true;
            Profile.bExportTextureTransforms = true;
            break;
        case EGodotAssetExporterPreset::BlockoutGreybox:
            Profile.bExportCameras = false;
            Profile.bExportLights = false;
            Profile.bExportHiddenInGame = false;
            Profile.bExportLightmaps = false;
            Profile.bExportVertexColors = false;
            Profile.bAdjustNormalmaps = true;
            Profile.bExportTextureTransforms = false;
            break;
        case EGodotAssetExporterPreset::PrefabSceneClean:
        default:
            Profile.bExportCameras = false;
            Profile.bExportLights = false;
            Profile.bExportHiddenInGame = false;
            Profile.bExportLightmaps = false;
            Profile.bExportVertexColors = true;
            Profile.bAdjustNormalmaps = true;
            Profile.bExportTextureTransforms = true;
            break;
        }

        return Profile;
    }

    static FString MakeRelativeForManifest(const FString& FullPath, const FString& RootPath)
    {
        FString Relative = FullPath;
        FString RootWithSlash = RootPath;
        FPaths::NormalizeDirectoryName(RootWithSlash);
        if (!RootWithSlash.EndsWith(TEXT("/")))
        {
            RootWithSlash += TEXT("/");
        }
        FPaths::MakePathRelativeTo(Relative, *RootWithSlash);
        Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
        return Relative;
    }

    static bool FindGodotProjectRootFromExportRoot(const FString& ExportRoot, FString& OutGodotRoot)
    {
        FString Probe = ExportRoot;
        FPaths::NormalizeDirectoryName(Probe);
        for (int32 Depth = 0; Depth < 8 && !Probe.IsEmpty(); ++Depth)
        {
            const FString ProjectFile = FPaths::Combine(Probe, TEXT("project.godot"));
            if (IFileManager::Get().FileExists(*ProjectFile))
            {
                OutGodotRoot = Probe;
                FPaths::NormalizeDirectoryName(OutGodotRoot);
                return true;
            }
            const FString Parent = FPaths::GetPath(Probe);
            if (Parent == Probe)
            {
                break;
            }
            Probe = Parent;
        }
        OutGodotRoot.Reset();
        return false;
    }

    static FString MakeGodotResPath(const FString& ExportRoot, const FString& FilePathOrRelative)
    {
        if (FilePathOrRelative.IsEmpty())
        {
            return FString();
        }
        if (FilePathOrRelative.StartsWith(TEXT("res://")))
        {
            return FilePathOrRelative;
        }

        FString GodotRoot;
        if (!FindGodotProjectRootFromExportRoot(ExportRoot, GodotRoot))
        {
            return FString();
        }

        FString FullPath = FilePathOrRelative;
        if (FPaths::IsRelative(FullPath))
        {
            FullPath = FPaths::Combine(ExportRoot, FullPath);
        }
        FPaths::NormalizeFilename(FullPath);
        FString Relative = FullPath;
        FString RootWithSlash = GodotRoot;
        FPaths::NormalizeDirectoryName(RootWithSlash);
        if (!RootWithSlash.EndsWith(TEXT("/")))
        {
            RootWithSlash += TEXT("/");
        }
        if (FPaths::MakePathRelativeTo(Relative, *RootWithSlash))
        {
            Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
            return FString(TEXT("res://")) + Relative;
        }
        return FString();
    }

    static FString MakeSafeRelativeAssetFolder(const UObject* Asset)
    {
        if (!Asset)
        {
            return FString();
        }

        FString PackagePath = Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : Asset->GetPathName();
        PackagePath.ReplaceInline(TEXT("\\"), TEXT("/"));
        if (PackagePath.StartsWith(TEXT("/Game/")))
        {
            PackagePath = PackagePath.RightChop(6);
        }
        else if (PackagePath.StartsWith(TEXT("/")))
        {
            PackagePath = PackagePath.RightChop(1);
        }

        PackagePath = FPaths::GetPath(PackagePath);
        TArray<FString> Parts;
        PackagePath.ParseIntoArray(Parts, TEXT("/"), true);
        for (FString& Part : Parts)
        {
            Part = FPaths::MakeValidFileName(Part);
        }
        return FString::Join(Parts, TEXT("/"));
    }

    static FString MakeUniqueFilePath(const FString& Directory, const FString& BaseName, const FString& Extension, TSet<FString>& UsedFilePaths)
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
        FString SafeBaseName = FPaths::MakeValidFileName(BaseName);
        if (SafeBaseName.IsEmpty())
        {
            SafeBaseName = TEXT("Asset");
        }
        FString CandidatePath = FPaths::Combine(Directory, SafeBaseName + Extension);
        int32 Index = 2;
        while (UsedFilePaths.Contains(CandidatePath) || IFileManager::Get().FileExists(*CandidatePath))
        {
            CandidatePath = FPaths::Combine(Directory, FString::Printf(TEXT("%s_%d%s"), *SafeBaseName, Index, *Extension));
            ++Index;
        }
        UsedFilePaths.Add(CandidatePath);
        return CandidatePath;
    }

    static FString GetCurrentTimestampForFileName()
    {
        return FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    }

    static FString GetWorldDisplayName(const UWorld* World)
    {
        if (!World)
        {
            return TEXT("UnknownWorld");
        }
        if (const UPackage* Package = World->GetOutermost())
        {
            const FString ShortName = FPackageName::GetShortName(Package->GetName());
            if (!ShortName.IsEmpty())
            {
                return ShortName;
            }
        }
        return World->GetMapName();
    }

    static FString GetWorldPackagePath(const UWorld* World)
    {
        return (World && World->GetOutermost()) ? World->GetOutermost()->GetName() : FString();
    }

    static TArray<FString> KnownGodotImportHintSuffixes()
    {
        return {
            TEXT("-noimp"), TEXT("-col"), TEXT("-convcol"), TEXT("-colonly"), TEXT("-convcolonly"),
            TEXT("-navmesh"), TEXT("-occ"), TEXT("-occonly"), TEXT("-rigid"), TEXT("-vehicle"), TEXT("-wheel")
        };
    }

    static bool HasKnownGodotImportHint(const FString& Label)
    {
        for (const FString& Suffix : KnownGodotImportHintSuffixes())
        {
            if (Label.EndsWith(Suffix, ESearchCase::CaseSensitive))
            {
                return true;
            }
        }
        return false;
    }

    static FString RemoveKnownGodotImportHint(const FString& Label)
    {
        FString Result = Label;
        for (const FString& Suffix : KnownGodotImportHintSuffixes())
        {
            if (Result.EndsWith(Suffix, ESearchCase::CaseSensitive))
            {
                Result = Result.LeftChop(Suffix.Len());
                break;
            }
        }
        return Result;
    }

    static FString CollisionSuffixForMode(const EGodotCollisionExportMode Mode)
    {
        switch (Mode)
        {
        case EGodotCollisionExportMode::SimpleCollisionAsColOnly:
            return TEXT("-colonly");
        case EGodotCollisionExportMode::ConvexCollisionAsConvColOnly:
            return TEXT("-convcolonly");
        case EGodotCollisionExportMode::GodotSuffixOnly:
            return TEXT("-col");
        default:
            return FString();
        }
    }

    static UGLTFExportOptions* CreateGodotGLTFExportOptions(const UGodotAssetExporterSettings* Settings)
    {
        const FResolvedExportProfile Profile = ResolveExportProfile(Settings);
        UGLTFExportOptions* ExportOptions = NewObject<UGLTFExportOptions>(GetTransientPackage());
        ExportOptions->bExportCameras = Profile.bExportCameras;
        ExportOptions->bExportLights = Profile.bExportLights;
        ExportOptions->bExportHiddenInGame = Profile.bExportHiddenInGame;
        ExportOptions->bExportLightmaps = Profile.bExportLightmaps;
        ExportOptions->bExportVertexColors = Profile.bExportVertexColors;
        ExportOptions->bAdjustNormalmaps = Profile.bAdjustNormalmaps;
        ExportOptions->bExportTextureTransforms = Profile.bExportTextureTransforms;
        return ExportOptions;
    }

    static void CollectTexturesFromStaticMesh(UStaticMesh* StaticMesh, TArray<UTexture*>& OutTextures)
    {
        if (!StaticMesh)
        {
            return;
        }
        for (int32 MaterialIndex = 0; MaterialIndex < StaticMesh->GetStaticMaterials().Num(); ++MaterialIndex)
        {
            UMaterialInterface* Material = StaticMesh->GetMaterial(MaterialIndex);
            if (!Material)
            {
                continue;
            }
            TArray<UTexture*> MaterialTextures;
            Material->GetUsedTextures(MaterialTextures, EMaterialQualityLevel::High, false, GMaxRHIFeatureLevel, false);
            for (UTexture* Texture : MaterialTextures)
            {
                if (Texture)
                {
                    OutTextures.AddUnique(Texture);
                }
            }
        }
    }

    static bool ExportTextureToPng(UTexture* Texture, const FString& FilePath)
    {
        UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
        if (!Texture2D)
        {
            UE_LOG(LogGodotAssetExporter, Warning, TEXT("Skipping non-Texture2D texture: %s"), *GetNameSafe(Texture));
            return false;
        }

        UAssetExportTask* ExportTask = NewObject<UAssetExportTask>();
        ExportTask->Object = Texture2D;
        ExportTask->Exporter = NewObject<UTextureExporterPNG>(ExportTask);
        ExportTask->Filename = FilePath;
        ExportTask->bAutomated = true;
        ExportTask->bPrompt = false;
        ExportTask->bReplaceIdentical = true;
        ExportTask->bSelected = false;
        return UExporter::RunAssetExportTask(ExportTask);
    }

    static TArray<FString> ValidateStaticMeshAsset(UStaticMesh* StaticMesh)
    {
        TArray<FString> Warnings;
        if (!StaticMesh)
        {
            Warnings.Add(TEXT("Null Static Mesh asset."));
            return Warnings;
        }
        if (StaticMesh->GetStaticMaterials().Num() == 0)
        {
            Warnings.Add(TEXT("Mesh has no material slots."));
        }
        for (int32 MaterialIndex = 0; MaterialIndex < StaticMesh->GetStaticMaterials().Num(); ++MaterialIndex)
        {
            if (!StaticMesh->GetMaterial(MaterialIndex))
            {
                Warnings.Add(FString::Printf(TEXT("Material slot %d is empty."), MaterialIndex));
            }
        }
        if (StaticMesh->GetNumLODs() == 0)
        {
            Warnings.Add(TEXT("Mesh reports zero LODs."));
        }
        return Warnings;
    }

    static TArray<FString> ValidateActorForGodot(AActor* Actor, const FResolvedExportProfile& Profile)
    {
        TArray<FString> Warnings;
        if (!Actor)
        {
            Warnings.Add(TEXT("Null actor."));
            return Warnings;
        }

        TArray<UStaticMeshComponent*> StaticMeshComponents;
        Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
        TArray<ULightComponent*> LightComponents;
        Actor->GetComponents<ULightComponent>(LightComponents);
        TArray<UCameraComponent*> CameraComponents;
        Actor->GetComponents<UCameraComponent>(CameraComponents);

        if (StaticMeshComponents.Num() == 0 && LightComponents.Num() == 0 && CameraComponents.Num() == 0)
        {
            Warnings.Add(TEXT("Actor has no StaticMesh, Light, or Camera components; glTF may skip it or export only an empty transform."));
        }
        if (Actor->GetClass() && Actor->GetClass()->ClassGeneratedBy)
        {
            Warnings.Add(TEXT("Blueprint actor detected. Mesh/components may export, but Blueprint logic and construction-script behavior will not convert to Godot."));
        }
        const FVector Scale = Actor->GetActorScale3D();
        if (Scale.X < 0.0f || Scale.Y < 0.0f || Scale.Z < 0.0f)
        {
            Warnings.Add(TEXT("Actor has negative scale. Check winding, normals, and collision after import."));
        }
        if (Actor->IsHiddenEd())
        {
            Warnings.Add(TEXT("Actor is hidden in editor."));
        }
        if (Actor->IsHidden())
        {
            Warnings.Add(TEXT("Actor is hidden in game. Export depends on the Export Hidden In Game setting."));
        }
        if (LightComponents.Num() > 0 && !Profile.bExportLights)
        {
            Warnings.Add(TEXT("Actor contains light components, but the active export profile does not export lights."));
        }
        if (CameraComponents.Num() > 0 && !Profile.bExportCameras)
        {
            Warnings.Add(TEXT("Actor contains camera components, but the active export profile does not export cameras."));
        }
        if (Profile.bWarnAboutMissingImportHints && StaticMeshComponents.Num() > 0 && !HasKnownGodotImportHint(Actor->GetActorLabel()))
        {
            Warnings.Add(TEXT("Static mesh actor has no Godot import-hint suffix such as -col, -convcol, -navmesh, or -noimp."));
        }
        if (StaticMeshComponents.Num() > 0 && Profile.CollisionMode != EGodotCollisionExportMode::None && !HasKnownGodotImportHint(Actor->GetActorLabel()))
        {
            Warnings.Add(FString::Printf(TEXT("Collision mode is %s, but actor label has no Godot collision/navigation suffix. Use the marker commands or collision prep."), *GetCollisionModeString(Profile.CollisionMode)));
        }
        for (UStaticMeshComponent* MeshComponent : StaticMeshComponents)
        {
            if (MeshComponent && !MeshComponent->GetStaticMesh())
            {
                Warnings.Add(FString::Printf(TEXT("StaticMeshComponent %s has no mesh assigned."), *MeshComponent->GetName()));
            }
        }
        return Warnings;
    }

    static FPrefabOriginRecord CalculatePrefabOrigin(const TArray<AActor*>& Actors, const UGodotAssetExporterSettings* Settings)
    {
        FPrefabOriginRecord Origin;
        Origin.Mode = Settings ? Settings->PrefabOriginMode : EGodotPrefabOriginMode::CenterSelectionBounds;
        Origin.ModeName = GetPrefabOriginModeString(Origin.Mode);
        Origin.RootActorLabel = Settings ? Settings->GodotRootActorLabel : TEXT("GodotRoot");

        if (Actors.Num() == 0)
        {
            return Origin;
        }

        switch (Origin.Mode)
        {
        case EGodotPrefabOriginMode::KeepWorldOrigin:
            Origin.LocationCm = FVector::ZeroVector;
            Origin.RotationDeg = FRotator::ZeroRotator;
            break;
        case EGodotPrefabOriginMode::CenterSelectionBounds:
        {
            FBox Bounds(ForceInit);
            for (AActor* Actor : Actors)
            {
                if (Actor)
                {
                    Bounds += Actor->GetComponentsBoundingBox(true);
                }
            }
            if (Bounds.IsValid)
            {
                Origin.LocationCm = Bounds.GetCenter();
                Origin.SelectionBoundsExtentCm = Bounds.GetExtent();
            }
            break;
        }
        case EGodotPrefabOriginMode::FirstSelectedActor:
            if (Actors[0])
            {
                Origin.LocationCm = Actors[0]->GetActorLocation();
                Origin.RotationDeg = Actors[0]->GetActorRotation();
                Origin.RootActorLabel = Actors[0]->GetActorLabel();
                Origin.bRootActorFound = true;
            }
            break;
        case EGodotPrefabOriginMode::ActorNamedGodotRoot:
        {
            const FString DesiredLabel = Origin.RootActorLabel.IsEmpty() ? TEXT("GodotRoot") : Origin.RootActorLabel;
            for (AActor* Actor : Actors)
            {
                if (!Actor)
                {
                    continue;
                }
                const FString Label = Actor->GetActorLabel();
                if (Label.Equals(DesiredLabel, ESearchCase::IgnoreCase) || Label.Equals(TEXT("GodotExportRoot"), ESearchCase::IgnoreCase) || Label.Equals(TEXT("BP_GodotExportRoot"), ESearchCase::IgnoreCase))
                {
                    Origin.LocationCm = Actor->GetActorLocation();
                    Origin.RotationDeg = Actor->GetActorRotation();
                    Origin.RootActorLabel = Label;
                    Origin.bRootActorFound = true;
                    break;
                }
            }
            break;
        }
        default:
            break;
        }
        return Origin;
    }

    static FString MakeActorSnapshotLine(const AActor* Actor)
    {
        if (!Actor)
        {
            return TEXT("actor|<null>");
        }
        const FVector Location = Actor->GetActorLocation();
        const FRotator Rotation = Actor->GetActorRotation();
        const FVector Scale = Actor->GetActorScale3D();
        FString Tags;
        for (int32 Index = 0; Index < Actor->Tags.Num(); ++Index)
        {
            Tags += Actor->Tags[Index].ToString();
            if (Index + 1 < Actor->Tags.Num())
            {
                Tags += TEXT(",");
            }
        }
        return FString::Printf(TEXT("actor|%s|%s|loc=%.3f,%.3f,%.3f|rot=%.3f,%.3f,%.3f|scale=%.3f,%.3f,%.3f|tags=%s"),
            *Actor->GetPathName(), *Actor->GetActorLabel(),
            Location.X, Location.Y, Location.Z,
            Rotation.Roll, Rotation.Pitch, Rotation.Yaw,
            Scale.X, Scale.Y, Scale.Z,
            *Tags);
    }

    static FString MakeStaticMeshSnapshotLine(const UStaticMesh* StaticMesh)
    {
        if (!StaticMesh)
        {
            return TEXT("staticmesh|<null>");
        }
        return FString::Printf(TEXT("staticmesh|%s|lods=%d|materials=%d"), *StaticMesh->GetPathName(), StaticMesh->GetNumLODs(), StaticMesh->GetStaticMaterials().Num());
    }

    static FString BuildSnapshotDiffText(const FString& Title, const TArray<FString>& PreviousLines, const TArray<FString>& CurrentLines)
    {
        auto ContainsLine = [](const TArray<FString>& Lines, const FString& Needle)
        {
            for (const FString& Line : Lines)
            {
                if (Line == Needle)
                {
                    return true;
                }
            }
            return false;
        };

        TArray<FString> Added;
        TArray<FString> Removed;
        for (const FString& Line : CurrentLines)
        {
            if (!ContainsLine(PreviousLines, Line))
            {
                Added.Add(Line);
            }
        }
        for (const FString& Line : PreviousLines)
        {
            if (!ContainsLine(CurrentLines, Line))
            {
                Removed.Add(Line);
            }
        }

        FString Report;
        Report += FString::Printf(TEXT("Godot Asset Exporter - %s Dry Run Diff\n"), *Title);
        Report += TEXT("================================================\n\n");
        Report += FString::Printf(TEXT("Generated: %s\n"), *FDateTime::Now().ToString());
        Report += FString::Printf(TEXT("Previous snapshot lines: %d\n"), PreviousLines.Num());
        Report += FString::Printf(TEXT("Current snapshot lines: %d\n"), CurrentLines.Num());
        Report += FString::Printf(TEXT("Added/changed lines: %d\n"), Added.Num());
        Report += FString::Printf(TEXT("Removed/changed lines: %d\n\n"), Removed.Num());
        if (PreviousLines.Num() == 0)
        {
            Report += TEXT("No previous snapshot was found. This looks like a first export for this mode.\n\n");
        }
        Report += TEXT("Added or changed:\n");
        if (Added.Num() == 0)
        {
            Report += TEXT("- none\n");
        }
        for (const FString& Line : Added)
        {
            Report += FString::Printf(TEXT("+ %s\n"), *Line);
        }
        Report += TEXT("\nRemoved or changed from previous snapshot:\n");
        if (Removed.Num() == 0)
        {
            Report += TEXT("- none\n");
        }
        for (const FString& Line : Removed)
        {
            Report += FString::Printf(TEXT("- %s\n"), *Line);
        }
        Report += TEXT("\nNote: this lightweight diff compares source paths, labels, transforms, tags, LOD/material counts, and selection membership. It is not a binary asset hash.\n");
        return Report;
    }

    static FString BuildSnapshotDiffJson(const FString& Title, const TArray<FString>& PreviousLines, const TArray<FString>& CurrentLines)
    {
        auto ContainsLine = [](const TArray<FString>& Lines, const FString& Needle)
        {
            for (const FString& Line : Lines)
            {
                if (Line == Needle)
                {
                    return true;
                }
            }
            return false;
        };

        TArray<FString> Added;
        TArray<FString> Removed;
        for (const FString& Line : CurrentLines)
        {
            if (!ContainsLine(PreviousLines, Line))
            {
                Added.Add(Line);
            }
        }
        for (const FString& Line : PreviousLines)
        {
            if (!ContainsLine(CurrentLines, Line))
            {
                Removed.Add(Line);
            }
        }

        auto AppendJsonArray = [](FString& Json, const TCHAR* Name, const TArray<FString>& Lines, bool bTrailingComma)
        {
            Json += FString::Printf(TEXT("  \"%s\": [\n"), Name);
            for (int32 Index = 0; Index < Lines.Num(); ++Index)
            {
                Json += FString::Printf(TEXT("    %s"), *JsonString(Lines[Index]));
                if (Index + 1 < Lines.Num())
                {
                    Json += TEXT(",");
                }
                Json += TEXT("\n");
            }
            Json += bTrailingComma ? TEXT("  ],\n") : TEXT("  ]\n");
        };

        FString Json;
        Json += TEXT("{\n");
        Json += TEXT("  \"exporter\": \"GodotAssetExporter\",\n");
        Json += FString::Printf(TEXT("  \"title\": %s,\n"), *JsonString(Title));
        Json += FString::Printf(TEXT("  \"generated_at\": %s,\n"), *JsonString(FDateTime::Now().ToString()));
        Json += FString::Printf(TEXT("  \"previous_count\": %d,\n"), PreviousLines.Num());
        Json += FString::Printf(TEXT("  \"current_count\": %d,\n"), CurrentLines.Num());
        Json += FString::Printf(TEXT("  \"added_or_changed_count\": %d,\n"), Added.Num());
        Json += FString::Printf(TEXT("  \"removed_or_changed_count\": %d,\n"), Removed.Num());
        AppendJsonArray(Json, TEXT("added_or_changed"), Added, true);
        AppendJsonArray(Json, TEXT("removed_or_changed"), Removed, false);
        Json += TEXT("}\n");
        return Json;
    }

    static bool WriteStaticMeshValidationReport(const FString& RootPath, const TArray<FExportedMeshRecord>& Records)
    {
        FString Report;
        Report += TEXT("Godot Asset Exporter - Static Mesh Validation Report\n");
        Report += TEXT("===================================================\n\n");
        Report += FString::Printf(TEXT("Generated: %s\n\n"), *FDateTime::Now().ToString());
        int32 WarningCount = 0;
        for (const FExportedMeshRecord& Record : Records)
        {
            Report += FString::Printf(TEXT("Asset: %s\n"), *Record.AssetPath);
            if (!Record.ModelFile.IsEmpty())
            {
                Report += FString::Printf(TEXT("Model: %s\n"), *MakeRelativeForManifest(Record.ModelFile, RootPath));
            }
            if (Record.Warnings.Num() == 0)
            {
                Report += TEXT("Warnings: none\n\n");
            }
            else
            {
                for (const FString& Warning : Record.Warnings)
                {
                    Report += FString::Printf(TEXT("- %s\n"), *Warning);
                    ++WarningCount;
                }
                Report += TEXT("\n");
            }
        }
        Report += FString::Printf(TEXT("Total warnings: %d\n"), WarningCount);
        const FString ReportPath = FPaths::Combine(RootPath, TEXT("logs"), TEXT("static_mesh_validation_report.txt"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
        return FFileHelper::SaveStringToFile(Report, *ReportPath);
    }

    static bool WriteSceneValidationReport(const FString& RootPath, FSceneExportRecord& Record, const FResolvedExportProfile& Profile)
    {
        FString Report;
        Report += TEXT("Godot Asset Exporter - Scene Validation Report\n");
        Report += TEXT("==============================================\n\n");
        Report += FString::Printf(TEXT("Generated: %s\n"), *FDateTime::Now().ToString());
        Report += FString::Printf(TEXT("Export type: %s\n"), *GetSceneExportTypeString(Record.ExportType));
        Report += FString::Printf(TEXT("Source level: %s\n"), *Record.SourceLevel);
        Report += FString::Printf(TEXT("Scene file: %s\n"), Record.SceneFile.IsEmpty() ? TEXT("<validation only>") : *MakeRelativeForManifest(Record.SceneFile, RootPath));
        Report += FString::Printf(TEXT("Preset/profile: %s\n"), *Profile.PresetName);
        Report += FString::Printf(TEXT("Collision mode: %s\n"), *GetCollisionModeString(Profile.CollisionMode));
        Report += FString::Printf(TEXT("Tag group: %s\n\n"), Record.TagGroup.IsEmpty() ? TEXT("<none>") : *Record.TagGroup);

        int32 WarningCount = 0;
        Record.Warnings.Reset();
        for (AActor* Actor : Record.Actors)
        {
            const TArray<FString> ActorWarnings = ValidateActorForGodot(Actor, Profile);
            if (ActorWarnings.Num() == 0)
            {
                continue;
            }
            Report += FString::Printf(TEXT("Actor: %s (%s)\n"), Actor ? *Actor->GetActorLabel() : TEXT("<null>"), Actor && Actor->GetClass() ? *Actor->GetClass()->GetName() : TEXT("<null>"));
            for (const FString& Warning : ActorWarnings)
            {
                Report += FString::Printf(TEXT("- %s\n"), *Warning);
                Record.Warnings.Add(FString::Printf(TEXT("%s: %s"), Actor ? *Actor->GetActorLabel() : TEXT("<null>"), *Warning));
                ++WarningCount;
            }
            Report += TEXT("\n");
        }
        if (WarningCount == 0)
        {
            Report += TEXT("No warnings detected by the lightweight validator.\n\n");
        }
        Report += TEXT("Reminder: this validator catches common workflow issues only. It cannot prove that a glTF import will be perfect in Godot.\n");
        Report += FString::Printf(TEXT("Total warnings: %d\n"), WarningCount);

        const FString SceneForName = Record.SceneFile.IsEmpty() ? FString(TEXT("Validation_Scene")) : Record.SceneFile;
        const FString BaseName = FPaths::GetBaseFilename(SceneForName);
        const FString ReportPath = FPaths::Combine(RootPath, TEXT("logs"), BaseName + TEXT("_validation_report.txt"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
        Record.ValidationReportFile = ReportPath;
        return FFileHelper::SaveStringToFile(Report, *ReportPath);
    }

    static void AppendActorTransformJson(FString& Manifest, const AActor* Actor, const FString& Indent)
    {
        const FVector Location = Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
        const FRotator Rotation = Actor ? Actor->GetActorRotation() : FRotator::ZeroRotator;
        const FVector Scale = Actor ? Actor->GetActorScale3D() : FVector::OneVector;
        Manifest += FString::Printf(TEXT("%s\"transform\": {\n"), *Indent);
        Manifest += FString::Printf(TEXT("%s  \"location_cm\": [%.6f, %.6f, %.6f],\n"), *Indent, Location.X, Location.Y, Location.Z);
        Manifest += FString::Printf(TEXT("%s  \"rotation_deg\": [%.6f, %.6f, %.6f],\n"), *Indent, Rotation.Roll, Rotation.Pitch, Rotation.Yaw);
        Manifest += FString::Printf(TEXT("%s  \"scale\": [%.6f, %.6f, %.6f]\n"), *Indent, Scale.X, Scale.Y, Scale.Z);
        Manifest += FString::Printf(TEXT("%s}"), *Indent);
    }

    static void AppendActorTagsJson(FString& Manifest, const AActor* Actor)
    {
        Manifest += TEXT("[");
        if (Actor)
        {
            for (int32 TagIndex = 0; TagIndex < Actor->Tags.Num(); ++TagIndex)
            {
                Manifest += JsonString(Actor->Tags[TagIndex].ToString());
                if (TagIndex + 1 < Actor->Tags.Num())
                {
                    Manifest += TEXT(", ");
                }
            }
        }
        Manifest += TEXT("]");
    }

    static bool WriteGodotImportPresetFile(const FString& RootPath, const FString& TargetFilePath, const FString& ManifestRelativePath, const FString& ExportType, const FResolvedExportProfile& Profile, FString& OutPresetFilePath)
    {
        if (!Profile.bWriteGodotImportPresetFiles || TargetFilePath.IsEmpty())
        {
            OutPresetFilePath.Reset();
            return false;
        }

        const FString BaseName = FPaths::GetBaseFilename(TargetFilePath);
        OutPresetFilePath = FPaths::Combine(RootPath, TEXT("import_presets"), BaseName + TEXT("_godot_import_preset.json"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPresetFilePath), true);

        const bool bGenerateCollision = Profile.CollisionMode != EGodotCollisionExportMode::None;
        const bool bIsSceneExport = ExportType != TEXT("static_mesh_asset");
        const bool bSaveAsTscn = bIsSceneExport || Profile.GodotImportPreset != EGodotImportPreset::StaticMeshProp;

        FString Json;
        Json += TEXT("{\n");
        Json += TEXT("  \"exporter\": \"GodotAssetExporter\",\n");
        Json += FString::Printf(TEXT("  \"generated_at\": %s,\n"), *JsonString(FDateTime::Now().ToString()));
        Json += FString::Printf(TEXT("  \"export_type\": %s,\n"), *JsonString(ExportType));
        Json += FString::Printf(TEXT("  \"target_file\": %s,\n"), *JsonString(MakeRelativeForManifest(TargetFilePath, RootPath)));
        Json += FString::Printf(TEXT("  \"manifest\": %s,\n"), *JsonString(ManifestRelativePath));
        Json += FString::Printf(TEXT("  \"unreal_preset\": %s,\n"), *JsonString(Profile.PresetName));
        Json += FString::Printf(TEXT("  \"godot_import_preset\": %s,\n"), *JsonString(Profile.GodotImportPresetName));
        Json += TEXT("  \"actions\": {\n");
        Json += FString::Printf(TEXT("    \"save_as_tscn\": %s,\n"), *BoolToJson(bSaveAsTscn));
        Json += FString::Printf(TEXT("    \"create_postprocess_wrapper\": %s,\n"), *BoolToJson(bIsSceneExport));
        Json += TEXT("    \"attach_unreal_metadata\": true,\n");
        Json += FString::Printf(TEXT("    \"generate_collision_from_suffixes\": %s,\n"), *BoolToJson(bGenerateCollision));
        Json += FString::Printf(TEXT("    \"extract_materials_hint\": %s,\n"), *BoolToJson(Profile.GodotImportPreset == EGodotImportPreset::FinalEnvironment));
        Json += FString::Printf(TEXT("    \"disable_animation_import_hint\": %s,\n"), *BoolToJson(Profile.GodotImportPreset != EGodotImportPreset::FinalEnvironment));
        Json += FString::Printf(TEXT("    \"post_import_script\": %s\n"), *JsonString(TEXT("res://addons/unreal_export_importer/unreal_post_import.gd")));
        Json += TEXT("  },\n");
        Json += TEXT("  \"cleanup\": {\n");
        Json += FString::Printf(TEXT("    \"output_cleanup_mode\": %s,\n"), *JsonString(GetOutputCleanupModeString(Profile.OutputCleanupMode)));
        Json += FString::Printf(TEXT("    \"versioned_scene_exports\": %s\n"), *BoolToJson(Profile.bVersionedSceneExports));
        Json += TEXT("  },\n");
        Json += TEXT("  \"notes\": [\n");
        Json += TEXT("    \"This is companion-addon metadata, not a raw Godot .import file.\",\n");
        Json += TEXT("    \"Use the Unreal Export Importer addon to convert GLB files, apply metadata, and write post-processed TSCN scenes.\"\n");
        Json += TEXT("  ]\n");
        Json += TEXT("}\n");
        return FFileHelper::SaveStringToFile(Json, *OutPresetFilePath);
    }

    static bool WriteStaticMeshManifest(const FString& RootPath, const TArray<FExportedMeshRecord>& Records, const FString& FormatExtension, const FResolvedExportProfile& Profile)
    {
        FString Manifest;
        Manifest += TEXT("{\n");
        Manifest += TEXT("  \"exporter\": \"GodotAssetExporter\",\n");
        Manifest += TEXT("  \"export_type\": \"static_mesh_assets\",\n");
        Manifest += FString::Printf(TEXT("  \"generated_at\": %s,\n"), *JsonString(FDateTime::Now().ToString()));
        Manifest += FString::Printf(TEXT("  \"format\": %s,\n"), *JsonString(FormatExtension));
        Manifest += FString::Printf(TEXT("  \"preset\": %s,\n"), *JsonString(Profile.PresetName));
        Manifest += FString::Printf(TEXT("  \"preserve_content_browser_folders\": %s,\n"), *BoolToJson(Profile.bPreserveFolders));
        Manifest += TEXT("  \"models\": [\n");
        for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
        {
            const FExportedMeshRecord& Record = Records[RecordIndex];
            Manifest += TEXT("    {\n");
            Manifest += FString::Printf(TEXT("      \"asset\": %s,\n"), *JsonString(Record.AssetPath));
            Manifest += FString::Printf(TEXT("      \"model\": %s,\n"), *JsonString(MakeRelativeForManifest(Record.ModelFile, RootPath)));
            Manifest += FString::Printf(TEXT("      \"godot_import_preset\": %s,\n"), *JsonString(Record.ImportPresetFile.IsEmpty() ? FString() : MakeRelativeForManifest(Record.ImportPresetFile, RootPath)));
            Manifest += TEXT("      \"textures\": [");
            for (int32 TextureIndex = 0; TextureIndex < Record.TextureFiles.Num(); ++TextureIndex)
            {
                Manifest += JsonString(MakeRelativeForManifest(Record.TextureFiles[TextureIndex], RootPath));
                if (TextureIndex + 1 < Record.TextureFiles.Num())
                {
                    Manifest += TEXT(", ");
                }
            }
            Manifest += TEXT("],\n");
            Manifest += TEXT("      \"warnings\": [");
            for (int32 WarningIndex = 0; WarningIndex < Record.Warnings.Num(); ++WarningIndex)
            {
                Manifest += JsonString(Record.Warnings[WarningIndex]);
                if (WarningIndex + 1 < Record.Warnings.Num())
                {
                    Manifest += TEXT(", ");
                }
            }
            Manifest += TEXT("]\n");
            Manifest += TEXT("    }");
            if (RecordIndex + 1 < Records.Num())
            {
                Manifest += TEXT(",");
            }
            Manifest += TEXT("\n");
        }
        Manifest += TEXT("  ]\n");
        Manifest += TEXT("}\n");
        const FString ManifestPath = FPaths::Combine(RootPath, TEXT("godot_export_manifest.json"));
        return FFileHelper::SaveStringToFile(Manifest, *ManifestPath);
    }

    static bool WriteSceneManifest(const FString& RootPath, const FSceneExportRecord& Record, const FString& FormatExtension, const FResolvedExportProfile& Profile)
    {
        FString Manifest;
        Manifest += TEXT("{\n");
        Manifest += TEXT("  \"exporter\": \"GodotAssetExporter\",\n");
        Manifest += FString::Printf(TEXT("  \"export_type\": %s,\n"), *JsonString(GetSceneExportTypeString(Record.ExportType)));
        Manifest += FString::Printf(TEXT("  \"generated_at\": %s,\n"), *JsonString(FDateTime::Now().ToString()));
        Manifest += FString::Printf(TEXT("  \"format\": %s,\n"), *JsonString(FormatExtension));
        Manifest += FString::Printf(TEXT("  \"preset\": %s,\n"), *JsonString(Profile.PresetName));
        Manifest += FString::Printf(TEXT("  \"source_level\": %s,\n"), *JsonString(Record.SourceLevel));
        Manifest += FString::Printf(TEXT("  \"scene\": %s,\n"), *JsonString(MakeRelativeForManifest(Record.SceneFile, RootPath)));
        Manifest += FString::Printf(TEXT("  \"validation_report\": %s,\n"), *JsonString(Record.ValidationReportFile.IsEmpty() ? FString() : MakeRelativeForManifest(Record.ValidationReportFile, RootPath)));
        Manifest += FString::Printf(TEXT("  \"dry_run_diff_report\": %s,\n"), *JsonString(Record.DryRunDiffReportFile.IsEmpty() ? FString() : MakeRelativeForManifest(Record.DryRunDiffReportFile, RootPath)));
        Manifest += FString::Printf(TEXT("  \"godot_import_preset\": %s,\n"), *JsonString(Record.ImportPresetFile.IsEmpty() ? FString() : MakeRelativeForManifest(Record.ImportPresetFile, RootPath)));
        Manifest += FString::Printf(TEXT("  \"tag_group\": %s,\n"), *JsonString(Record.TagGroup));
        Manifest += FString::Printf(TEXT("  \"selected_actors_set_was_empty\": %s,\n"), *BoolToJson(Record.bSelectedActorsSetWasEmpty));
        Manifest += TEXT("  \"options\": {\n");
        Manifest += FString::Printf(TEXT("    \"export_cameras\": %s,\n"), *BoolToJson(Profile.bExportCameras));
        Manifest += FString::Printf(TEXT("    \"export_lights\": %s,\n"), *BoolToJson(Profile.bExportLights));
        Manifest += FString::Printf(TEXT("    \"export_hidden_in_game\": %s,\n"), *BoolToJson(Profile.bExportHiddenInGame));
        Manifest += FString::Printf(TEXT("    \"export_lightmaps\": %s,\n"), *BoolToJson(Profile.bExportLightmaps));
        Manifest += FString::Printf(TEXT("    \"export_vertex_colors\": %s,\n"), *BoolToJson(Profile.bExportVertexColors));
        Manifest += FString::Printf(TEXT("    \"adjust_normal_maps\": %s,\n"), *BoolToJson(Profile.bAdjustNormalmaps));
        Manifest += FString::Printf(TEXT("    \"export_texture_transforms\": %s,\n"), *BoolToJson(Profile.bExportTextureTransforms));
        Manifest += FString::Printf(TEXT("    \"collision_mode\": %s\n"), *JsonString(GetCollisionModeString(Profile.CollisionMode)));
        Manifest += TEXT("  },\n");
        Manifest += TEXT("  \"prefab_origin\": {\n");
        Manifest += FString::Printf(TEXT("    \"mode\": %s,\n"), *JsonString(Record.PrefabOrigin.ModeName));
        Manifest += FString::Printf(TEXT("    \"root_actor_label\": %s,\n"), *JsonString(Record.PrefabOrigin.RootActorLabel));
        Manifest += FString::Printf(TEXT("    \"root_actor_found\": %s,\n"), *BoolToJson(Record.PrefabOrigin.bRootActorFound));
        Manifest += FString::Printf(TEXT("    \"location_cm\": [%.6f, %.6f, %.6f],\n"), Record.PrefabOrigin.LocationCm.X, Record.PrefabOrigin.LocationCm.Y, Record.PrefabOrigin.LocationCm.Z);
        Manifest += FString::Printf(TEXT("    \"rotation_deg\": [%.6f, %.6f, %.6f],\n"), Record.PrefabOrigin.RotationDeg.Roll, Record.PrefabOrigin.RotationDeg.Pitch, Record.PrefabOrigin.RotationDeg.Yaw);
        Manifest += FString::Printf(TEXT("    \"selection_bounds_extent_cm\": [%.6f, %.6f, %.6f]\n"), Record.PrefabOrigin.SelectionBoundsExtentCm.X, Record.PrefabOrigin.SelectionBoundsExtentCm.Y, Record.PrefabOrigin.SelectionBoundsExtentCm.Z);
        Manifest += TEXT("  },\n");
        Manifest += FString::Printf(TEXT("  \"actor_count_in_manifest\": %d,\n"), Record.Actors.Num());
        Manifest += TEXT("  \"actors\": [\n");
        for (int32 ActorIndex = 0; ActorIndex < Record.Actors.Num(); ++ActorIndex)
        {
            const AActor* Actor = Record.Actors[ActorIndex];
            Manifest += TEXT("    {\n");
            Manifest += FString::Printf(TEXT("      \"label\": %s,\n"), *JsonString(Actor ? Actor->GetActorLabel() : FString()));
            Manifest += FString::Printf(TEXT("      \"name\": %s,\n"), *JsonString(GetNameSafe(Actor)));
            Manifest += FString::Printf(TEXT("      \"class\": %s,\n"), *JsonString(Actor && Actor->GetClass() ? Actor->GetClass()->GetName() : FString()));
            Manifest += FString::Printf(TEXT("      \"class_path\": %s,\n"), *JsonString(Actor && Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString()));
            Manifest += FString::Printf(TEXT("      \"actor_path\": %s,\n"), *JsonString(Actor ? Actor->GetPathName() : FString()));
            Manifest += TEXT("      ");
            AppendActorTransformJson(Manifest, Actor, TEXT("      "));
            Manifest += TEXT(",\n");
            Manifest += TEXT("      \"tags\": ");
            AppendActorTagsJson(Manifest, Actor);
            Manifest += TEXT("\n    }");
            if (ActorIndex + 1 < Record.Actors.Num())
            {
                Manifest += TEXT(",");
            }
            Manifest += TEXT("\n");
        }
        Manifest += TEXT("  ],\n");
        Manifest += TEXT("  \"warnings\": [\n");
        for (int32 WarningIndex = 0; WarningIndex < Record.Warnings.Num(); ++WarningIndex)
        {
            Manifest += FString::Printf(TEXT("    %s"), *JsonString(Record.Warnings[WarningIndex]));
            if (WarningIndex + 1 < Record.Warnings.Num())
            {
                Manifest += TEXT(",");
            }
            Manifest += TEXT("\n");
        }
        Manifest += TEXT("  ],\n");
        Manifest += TEXT("  \"notes\": [\n");
        Manifest += TEXT("    \"The GLB contains scene data exported by Unreal's GLTFExporter.\",\n");
        Manifest += TEXT("    \"Prefab origin data is metadata for Godot-side post-import re-rooting; Unreal's raw GLB export may still use the original world transforms.\",\n");
        Manifest += TEXT("    \"Blueprint logic, construction scripts, Unreal collision profiles, Landscape, foliage systems, World Partition metadata, and custom shader graphs do not map 1:1 to Godot.\"\n");
        Manifest += TEXT("  ]\n");
        Manifest += TEXT("}\n");
        const FString BaseName = FPaths::GetBaseFilename(Record.SceneFile);
        const FString ManifestPath = FPaths::Combine(RootPath, TEXT("manifests"), BaseName + TEXT("_manifest.json"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ManifestPath), true);
        return FFileHelper::SaveStringToFile(Manifest, *ManifestPath);
    }

    static bool WriteGodotNotes(const FString& RootPath, const FString& FormatExtension)
    {
        const FString Notes = FString::Printf(
            TEXT("Godot Asset Exporter\n")
            TEXT("====================\n\n")
            TEXT("Use Window > Godot Asset Exporter for the dockable panel or Tools > Godot Export for direct commands. Run Preflight Check before first use in a new project.\n\n")
            TEXT("Static mesh asset export writes files under models/. With folder preservation enabled, /Game/Environment/Walls/SM_Wall becomes models/Environment/Walls/SM_Wall.%s.\n\n")
            TEXT("Scene/prefab export writes files under scenes/ and manifests/ with prefab origin metadata, actor tags, validation reports, and dry-run diff report paths.\n\n")
            TEXT("Actor tag groups: tag actors with GodotExport or GodotExport:Room_A, then use Export Actor Tag Groups. Tag actors GodotNoExport to omit them from current-level manifest bookkeeping.\n\n")
            TEXT("Collision prep: use the marker commands to add Godot import hint suffixes such as -col, -convcol, -colonly, -convcolonly, -navmesh, and -noimp.\n\n")
            TEXT("Godot companion plugin: copy GodotCompanion/addons/unreal_export_importer into your Godot project's addons/ folder, enable it, set the export root in Project Settings > Unreal Export Importer if needed, then use Project > Tools to scan manifests, convert GLBs to TSCN, and apply manifest metadata.\n\n")
            TEXT("Limitations: glTF/GLB is not a full Unreal-to-Godot converter. Blueprint logic, construction scripts, complex materials, collision profiles, Landscape, foliage systems, World Partition metadata, and gameplay behavior require Godot-side cleanup.\n"),
            *FormatExtension
        );
        const FString NotesPath = FPaths::Combine(RootPath, TEXT("README_GodotImport.txt"));
        return FFileHelper::SaveStringToFile(Notes, *NotesPath);
    }

    static bool WriteLastStaticMeshAssetSet(const FString& RootPath, const TArray<UStaticMesh*>& StaticMeshes)
    {
        TArray<FString> Lines;
        for (UStaticMesh* StaticMesh : StaticMeshes)
        {
            if (StaticMesh)
            {
                Lines.Add(StaticMesh->GetPathName());
            }
        }
        const FString CachePath = FPaths::Combine(RootPath, TEXT("cache"), TEXT("last_static_mesh_assets.txt"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);
        return FFileHelper::SaveStringArrayToFile(Lines, *CachePath);
    }

    static bool WriteExportCache(const FString& RootPath, const FString& ExportType, const TArray<FString>& SourcePaths, const TArray<FString>& OutputPaths)
    {
        FString Cache;
        Cache += TEXT("{\n");
        Cache += TEXT("  \"exporter\": \"GodotAssetExporter\",\n");
        Cache += FString::Printf(TEXT("  \"generated_at\": %s,\n"), *JsonString(FDateTime::Now().ToString()));
        Cache += FString::Printf(TEXT("  \"export_type\": %s,\n"), *JsonString(ExportType));
        Cache += TEXT("  \"sources\": [\n");
        for (int32 Index = 0; Index < SourcePaths.Num(); ++Index)
        {
            Cache += FString::Printf(TEXT("    %s"), *JsonString(SourcePaths[Index]));
            if (Index + 1 < SourcePaths.Num())
            {
                Cache += TEXT(",");
            }
            Cache += TEXT("\n");
        }
        Cache += TEXT("  ],\n");
        Cache += TEXT("  \"outputs\": [\n");
        for (int32 Index = 0; Index < OutputPaths.Num(); ++Index)
        {
            Cache += FString::Printf(TEXT("    %s"), *JsonString(MakeRelativeForManifest(OutputPaths[Index], RootPath)));
            if (Index + 1 < OutputPaths.Num())
            {
                Cache += TEXT(",");
            }
            Cache += TEXT("\n");
        }
        Cache += TEXT("  ]\n");
        Cache += TEXT("}\n");
        const FString CachePath = FPaths::Combine(RootPath, TEXT("cache"), TEXT("last_export.json"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), true);
        return FFileHelper::SaveStringToFile(Cache, *CachePath);
    }
}

class FGodotAssetExporterModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            GodotAssetExporterTabName,
            FOnSpawnTab::CreateRaw(this, &FGodotAssetExporterModule::SpawnGodotAssetExporterTab)
        )
        .SetDisplayName(LOCTEXT("GodotAssetExporterTabTitle", "Godot Asset Exporter"))
        .SetTooltipText(LOCTEXT("GodotAssetExporterTabTooltip", "Export Unreal assets, prefab chunks, and levels to Godot-friendly glTF/GLB."))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGodotAssetExporterModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GodotAssetExporterTabName);
    }

private:
    TSharedRef<SDockTab> SpawnGodotAssetExporterTab(const FSpawnTabArgs& Args)
    {
        const FString ExportRoot = GetConfiguredExportRoot();
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        const GodotAssetExporter::FResolvedExportProfile Profile = GodotAssetExporter::ResolveExportProfile(Settings);

        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(10)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(FString::Printf(TEXT("Export root: %s\nFormat: .%s\nPreset: %s"), *ExportRoot, *GetFormatExtension(), *Profile.PresetName)))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelPreflight", "Preflight Check"))
                        .OnClicked_Lambda([this]() { PreflightCheck(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelValidateSelection", "Validate Selection"))
                        .OnClicked_Lambda([this]() { ValidateSelectionForGodot(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelExportMeshes", "Export Selected Static Mesh Assets"))
                        .OnClicked_Lambda([this]() { ExportSelectedStaticMeshes(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelExportPrefab", "Export Selected Actors as Prefab Scene"))
                        .OnClicked_Lambda([this]() { ExportSelectedActorsAsPrefab(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelExportLevel", "Export Current Level Scene"))
                        .OnClicked_Lambda([this]() { ExportCurrentLevelScene(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelExportTagged", "Export Actor Tag Groups"))
                        .OnClicked_Lambda([this]() { ExportActorTagGroups(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelReExport", "Re-export Last Static Mesh Set"))
                        .OnClicked_Lambda([this]() { ReExportLastStaticMeshSet(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelCleanGenerated", "Clean Generated Export Folders"))
                        .OnClicked_Lambda([this]() { CleanGeneratedExportFolders(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelCopyGodotPath", "Copy Last Godot res:// Path"))
                        .OnClicked_Lambda([this]() { CopyLastGodotPath(); return FReply::Handled(); })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10, 4)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("PanelOpenFolder", "Open Export Folder"))
                        .OnClicked_Lambda([this]() { OpenExportFolder(); return FReply::Handled(); })
                    ]
                ]
            ];
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);

        UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("GodotAssetExporter"));

        Section.AddMenuEntry(TEXT("GodotAssetExporter_OpenPanel"), LOCTEXT("OpenPanel_Label", "Godot Export: Open Export Panel"), LOCTEXT("OpenPanel_Tooltip", "Open the dockable Godot Asset Exporter panel."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::OpenGodotExporterPanel)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_Preflight"), LOCTEXT("Preflight_Label", "Godot Export: Preflight Check"), LOCTEXT("Preflight_Tooltip", "Verify GLTFExporter, export paths, selected assets/actors, and optional Godot companion installation before exporting."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::PreflightCheck)));
        Section.AddSeparator(TEXT("GodotAssetExporter_ExportSeparator"));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ValidateSelection"), LOCTEXT("ValidateSelection_Label", "Godot Export: Validate Selection"), LOCTEXT("ValidateSelection_Tooltip", "Validate selected Static Mesh assets or selected level actors without exporting."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ValidateSelectionForGodot)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ValidateCurrentLevel"), LOCTEXT("ValidateCurrentLevel_Label", "Godot Export: Validate Current Level"), LOCTEXT("ValidateCurrentLevel_Tooltip", "Validate the current level without exporting."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ValidateCurrentLevelForGodot)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ExportSelectedStaticMeshes"), LOCTEXT("ExportSelectedStaticMeshes_Label", "Godot Export: Selected Static Mesh Assets"), LOCTEXT("ExportSelectedStaticMeshes_Tooltip", "Exports selected Content Browser Static Mesh assets as one glTF/GLB file per mesh for Godot."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ExportSelectedStaticMeshes)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ReExportLastStaticMeshes"), LOCTEXT("ReExportLastStaticMeshes_Label", "Godot Export: Re-export Last Static Mesh Set"), LOCTEXT("ReExportLastStaticMeshes_Tooltip", "Re-exports the Static Mesh asset paths recorded in cache/last_static_mesh_assets.txt."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ReExportLastStaticMeshSet)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ExportSelectedActorsAsPrefab"), LOCTEXT("ExportSelectedActorsAsPrefab_Label", "Godot Export: Selected Level Actors as Prefab Scene"), LOCTEXT("ExportSelectedActorsAsPrefab_Tooltip", "Exports selected viewport/Outliner actors from the current level as one Godot-friendly glTF/GLB scene."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ExportSelectedActorsAsPrefab)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ExportCurrentLevel"), LOCTEXT("ExportCurrentLevel_Label", "Godot Export: Current Level Scene"), LOCTEXT("ExportCurrentLevel_Tooltip", "Exports the entire currently open Unreal level as one Godot-friendly glTF/GLB scene."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ExportCurrentLevelScene)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ExportActorTagGroups"), LOCTEXT("ExportActorTagGroups_Label", "Godot Export: Actor Tag Groups"), LOCTEXT("ExportActorTagGroups_Tooltip", "Exports every actor group tagged GodotExport or GodotExport:GroupName as separate prefab scenes."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ExportActorTagGroups)));
        Section.AddSeparator(TEXT("GodotAssetExporter_ImportHintSeparator"));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_PrepareCollision"), LOCTEXT("PrepareCollision_Label", "Godot Export: Prepare Collision Suffixes From Mode"), LOCTEXT("PrepareCollision_Tooltip", "Applies the configured collision suffix to selected actors that do not already have a Godot import-hint suffix."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::PrepareCollisionSuffixesForSelectedActors)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_MarkCollision"), LOCTEXT("MarkCollision_Label", "Godot Export: Mark Selected Actors -col"), LOCTEXT("MarkCollision_Tooltip", "Appends Godot's -col import-hint suffix to selected actor labels."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::MarkSelectedActorsCollision)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_MarkConvexCollision"), LOCTEXT("MarkConvexCollision_Label", "Godot Export: Mark Selected Actors -convcol"), LOCTEXT("MarkConvexCollision_Tooltip", "Appends Godot's -convcol import-hint suffix to selected actor labels."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::MarkSelectedActorsConvexCollision)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_MarkCollisionOnly"), LOCTEXT("MarkCollisionOnly_Label", "Godot Export: Mark Selected Actors -colonly"), LOCTEXT("MarkCollisionOnly_Tooltip", "Appends Godot's -colonly import-hint suffix to selected actor labels."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::MarkSelectedActorsCollisionOnly)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_MarkConvexCollisionOnly"), LOCTEXT("MarkConvexCollisionOnly_Label", "Godot Export: Mark Selected Actors -convcolonly"), LOCTEXT("MarkConvexCollisionOnly_Tooltip", "Appends Godot's -convcolonly import-hint suffix to selected actor labels."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::MarkSelectedActorsConvexCollisionOnly)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_MarkNavmesh"), LOCTEXT("MarkNavmesh_Label", "Godot Export: Mark Selected Actors -navmesh"), LOCTEXT("MarkNavmesh_Tooltip", "Appends Godot's -navmesh import-hint suffix to selected actor labels."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::MarkSelectedActorsNavmesh)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_MarkNoImport"), LOCTEXT("MarkNoImport_Label", "Godot Export: Mark Selected Actors -noimp"), LOCTEXT("MarkNoImport_Tooltip", "Appends Godot's -noimp import-hint suffix to selected actor labels."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::MarkSelectedActorsNoImport)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_ClearImportHints"), LOCTEXT("ClearImportHints_Label", "Godot Export: Clear Selected Actor Import Hints"), LOCTEXT("ClearImportHints_Tooltip", "Removes known Godot import-hint suffixes from selected actor labels."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::ClearSelectedActorImportHints)));
        Section.AddSeparator(TEXT("GodotAssetExporter_FolderSeparator"));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_CleanGenerated"), LOCTEXT("CleanGenerated_Label", "Godot Export: Clean Generated Export Folders"), LOCTEXT("CleanGenerated_Tooltip", "Deletes generated models/scenes/manifests/import presets/logs/postprocess folders, leaving archives and cache intact."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::CleanGeneratedExportFolders)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_OpenFolder"), LOCTEXT("OpenFolder_Label", "Godot Export: Open Export Folder"), LOCTEXT("OpenFolder_Tooltip", "Open the configured export folder."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::OpenExportFolder)));
        Section.AddMenuEntry(TEXT("GodotAssetExporter_CopyLastGodotPath"), LOCTEXT("CopyLastGodotPath_Label", "Godot Export: Copy Last Godot res:// Path"), LOCTEXT("CopyLastGodotPath_Tooltip", "Copies the first output from cache/last_export.json as a Godot res:// path when the export root is inside a Godot project."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::CopyLastGodotPath)));

        if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window")))
        {
            FToolMenuSection& WindowSection = WindowMenu->FindOrAddSection(TEXT("GodotAssetExporter"));
            WindowSection.AddMenuEntry(TEXT("GodotAssetExporter_WindowOpenPanel"), LOCTEXT("WindowOpenPanel_Label", "Godot Asset Exporter"), LOCTEXT("WindowOpenPanel_Tooltip", "Open the Godot Asset Exporter panel."), FSlateIcon(), FUIAction(FExecuteAction::CreateRaw(this, &FGodotAssetExporterModule::OpenGodotExporterPanel)));
        }
    }

    void OpenGodotExporterPanel()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(FTabId(GodotAssetExporterTabName));
    }

    FString GetConfiguredExportRoot() const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        FString ExportRoot;
        if (Settings && Settings->bExportToGodotProject && !Settings->GodotProjectExportDirectory.IsEmpty())
        {
            ExportRoot = Settings->GodotProjectExportDirectory;
        }
        else if (Settings)
        {
            ExportRoot = Settings->DefaultExportDirectory;
        }
        if (ExportRoot.IsEmpty())
        {
            ExportRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GodotExports"));
        }
        else if (FPaths::IsRelative(ExportRoot))
        {
            ExportRoot = FPaths::Combine(FPaths::ProjectDir(), ExportRoot);
        }
        FPaths::NormalizeDirectoryName(ExportRoot);
        return ExportRoot;
    }

    bool ChooseExportRoot(FString& OutExportRoot) const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        OutExportRoot = GetConfiguredExportRoot();
        if (!Settings || !Settings->bPromptForFolder)
        {
            return true;
        }
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (!DesktopPlatform)
        {
            return true;
        }
        void* ParentWindowHandle = nullptr;
        if (FSlateApplication::IsInitialized())
        {
            ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
        }
        FString ChosenFolder;
        const bool bFolderChosen = DesktopPlatform->OpenDirectoryDialog(ParentWindowHandle, TEXT("Choose Godot export folder"), OutExportRoot, ChosenFolder);
        if (!bFolderChosen || ChosenFolder.IsEmpty())
        {
            return false;
        }
        FPaths::NormalizeDirectoryName(ChosenFolder);
        OutExportRoot = ChosenFolder;
        return true;
    }

    FString GetFormatExtension() const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        if (Settings && Settings->bUseSafeGodotDefaults)
        {
            return TEXT("glb");
        }
        return Settings && Settings->bExportBinaryGLB ? TEXT("glb") : TEXT("gltf");
    }

    FString MakeGodotResPathForOutput(const FString& ExportRoot, const FString& FilePathOrRelative) const
    {
        return GodotAssetExporter::MakeGodotResPath(ExportRoot, FilePathOrRelative);
    }

    FString FindFirstOutputFromLastExport(const FString& ExportRoot) const
    {
        FString CacheText;
        const FString CachePath = FPaths::Combine(ExportRoot, TEXT("cache"), TEXT("last_export.json"));
        if (!FFileHelper::LoadFileToString(CacheText, *CachePath))
        {
            return FString();
        }
        const int32 OutputsIndex = CacheText.Find(TEXT("\"outputs\""));
        if (OutputsIndex == INDEX_NONE)
        {
            return FString();
        }
        const int32 ArrayStart = CacheText.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, OutputsIndex);
        if (ArrayStart == INDEX_NONE)
        {
            return FString();
        }
        const int32 FirstQuote = CacheText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ArrayStart);
        if (FirstQuote == INDEX_NONE)
        {
            return FString();
        }
        const int32 SecondQuote = CacheText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstQuote + 1);
        if (SecondQuote == INDEX_NONE || SecondQuote <= FirstQuote)
        {
            return FString();
        }
        return CacheText.Mid(FirstQuote + 1, SecondQuote - FirstQuote - 1);
    }

    void CopyLastGodotPath()
    {
        const FString ExportRoot = GetConfiguredExportRoot();
        const FString LastOutput = FindFirstOutputFromLastExport(ExportRoot);
        if (LastOutput.IsEmpty())
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoLastExportPath", "No previous export output was found in cache/last_export.json."));
            return;
        }
        const FString GodotPath = MakeGodotResPathForOutput(ExportRoot, LastOutput);
        if (GodotPath.IsEmpty())
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Could not convert the last output to a Godot res:// path. Make sure the export root is inside a Godot project containing project.godot.\n\nLast output:\n%s\n\nExport root:\n%s"), *LastOutput, *ExportRoot)));
            return;
        }
        FPlatformApplicationMisc::ClipboardCopy(*GodotPath);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Copied Godot path to clipboard:\n%s"), *GodotPath)));
    }

    bool MaybeWarnFullLevelExport() const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        if (!Settings || !Settings->bShowFullLevelExportWarning)
        {
            return true;
        }
        const FString Message = TEXT("This exports scene geometry/layout through glTF. It does not convert Blueprint logic, gameplay systems, Landscape/foliage workflows, World Partition behavior, or complex Unreal material graphs.\n\nContinue exporting the current level?");
        return FMessageDialog::Open(EAppMsgType::OkCancel, FText::FromString(Message)) == EAppReturnType::Ok;
    }

    void PreflightCheck()
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        const FString ExportRoot = GetConfiguredExportRoot();
        EnsureCommonExportDirectories(ExportRoot, true);

        int32 ErrorCount = 0;
        int32 WarningCount = 0;
        FString Report;
        auto AddLine = [&Report](const FString& Status, const FString& Text)
        {
            Report += FString::Printf(TEXT("[%s] %s\n"), *Status, *Text);
        };
        auto AddOk = [&AddLine](const FString& Text) { AddLine(TEXT("OK"), Text); };
        auto AddWarn = [&AddLine, &WarningCount](const FString& Text) { ++WarningCount; AddLine(TEXT("WARN"), Text); };
        auto AddError = [&AddLine, &ErrorCount](const FString& Text) { ++ErrorCount; AddLine(TEXT("ERROR"), Text); };

        Report += TEXT("Godot Asset Exporter - Preflight Check\n");
        Report += TEXT("======================================\n\n");
        Report += FString::Printf(TEXT("Generated: %s\n"), *FDateTime::Now().ToString());
        Report += FString::Printf(TEXT("Export root: %s\n"), *ExportRoot);
        Report += FString::Printf(TEXT("Format: .%s\n"), *GetFormatExtension());
        Report += FString::Printf(TEXT("Safe Godot Defaults: %s\n\n"), Settings && Settings->bUseSafeGodotDefaults ? TEXT("enabled") : TEXT("disabled"));

        const TSharedPtr<IPlugin> GltfPlugin = IPluginManager::Get().FindPlugin(TEXT("GLTFExporter"));
        if (!GltfPlugin.IsValid())
        {
            AddError(TEXT("GLTFExporter plugin was not found. Enable/install Unreal's built-in GLTFExporter plugin before exporting."));
        }
        else if (!GltfPlugin->IsEnabled())
        {
            AddError(TEXT("GLTFExporter plugin was found but is not enabled for this project."));
        }
        else
        {
            AddOk(TEXT("GLTFExporter plugin is enabled."));
        }

        if (IFileManager::Get().DirectoryExists(*ExportRoot))
        {
            AddOk(TEXT("Export root exists."));
        }
        else
        {
            AddError(TEXT("Export root could not be created."));
        }

        const FString ProbeFile = FPaths::Combine(ExportRoot, TEXT(".godot_asset_exporter_write_test.tmp"));
        if (FFileHelper::SaveStringToFile(TEXT("write test"), *ProbeFile))
        {
            IFileManager::Get().Delete(*ProbeFile);
            AddOk(TEXT("Export root is writable."));
        }
        else
        {
            AddError(TEXT("Export root is not writable."));
        }

        UWorld* World = GetEditorWorld();
        if (World)
        {
            AddOk(FString::Printf(TEXT("Current editor world: %s"), *GodotAssetExporter::GetWorldPackagePath(World)));
        }
        else
        {
            AddWarn(TEXT("No current editor world was found. Static mesh export can still work; scene/level export cannot."));
        }

        const int32 SelectedMeshCount = GetSelectedStaticMeshes().Num();
        const int32 SelectedActorCount = GetSelectedLevelActors().Num();
        if (SelectedMeshCount > 0)
        {
            AddOk(FString::Printf(TEXT("Selected Static Mesh assets: %d"), SelectedMeshCount));
        }
        else
        {
            AddWarn(TEXT("No Static Mesh assets selected in the Content Browser."));
        }
        if (SelectedActorCount > 0)
        {
            AddOk(FString::Printf(TEXT("Selected level actors: %d"), SelectedActorCount));
        }
        else
        {
            AddWarn(TEXT("No level actors selected in the viewport/World Outliner."));
        }

        FString GodotRoot;
        if (GodotAssetExporter::FindGodotProjectRootFromExportRoot(ExportRoot, GodotRoot))
        {
            AddOk(FString::Printf(TEXT("Godot project detected: %s"), *GodotRoot));
            const FString AddonCfg = FPaths::Combine(GodotRoot, TEXT("addons"), TEXT("unreal_export_importer"), TEXT("plugin.cfg"));
            if (IFileManager::Get().FileExists(*AddonCfg))
            {
                AddOk(TEXT("Godot companion addon files are installed."));
            }
            else if (Settings && Settings->bExportToGodotProject)
            {
                AddWarn(TEXT("Godot project was detected, but addons/unreal_export_importer/plugin.cfg was not found. Install the companion addon if you want Godot-side post-processing."));
            }
        }
        else if (Settings && Settings->bExportToGodotProject)
        {
            AddWarn(TEXT("Export To Godot Project is enabled, but no project.godot file was found by walking upward from the export root."));
        }

        const FString ReportPath = FPaths::Combine(ExportRoot, TEXT("logs"), TEXT("preflight_check.txt"));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
        FFileHelper::SaveStringToFile(Report, *ReportPath);

        const FString DialogText = FString::Printf(TEXT("Preflight complete.\n\nErrors: %d\nWarnings: %d\n\nReport:\n%s\n\n%s"), ErrorCount, WarningCount, *ReportPath, *Report.Left(1400));
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogText));
    }

    void EnsureCommonExportDirectories(const FString& ExportRoot, const bool bIncludeTextures) const
    {
        IFileManager::Get().MakeDirectory(*ExportRoot, true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("models")), true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("scenes")), true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("manifests")), true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("logs")), true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("cache")), true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("import_presets")), true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("archives")), true);
        IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("postprocess")), true);
        if (bIncludeTextures)
        {
            IFileManager::Get().MakeDirectory(*FPaths::Combine(ExportRoot, TEXT("textures")), true);
        }
    }

    void CleanGeneratedFoldersIfRequested(const FString& ExportRoot, const bool bIncludeTextures) const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        if (!Settings || Settings->OutputCleanupMode != EGodotOutputCleanupMode::CleanGeneratedFolders)
        {
            return;
        }

        TArray<FString> Folders;
        if (bIncludeTextures)
        {
            Folders.Add(TEXT("models"));
            Folders.Add(TEXT("textures"));
        }
        Folders.Add(TEXT("scenes"));
        Folders.Add(TEXT("manifests"));
        Folders.Add(TEXT("import_presets"));
        Folders.Add(TEXT("logs"));
        Folders.Add(TEXT("postprocess"));

        for (const FString& Folder : Folders)
        {
            const FString FolderPath = FPaths::Combine(ExportRoot, Folder);
            if (IFileManager::Get().DirectoryExists(*FolderPath))
            {
                IFileManager::Get().DeleteDirectory(*FolderPath, false, true);
            }
        }
        EnsureCommonExportDirectories(ExportRoot, bIncludeTextures);
    }

    void ArchiveExistingFileIfRequested(const FString& ExportRoot, const FString& FilePath) const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        if (!Settings || Settings->OutputCleanupMode != EGodotOutputCleanupMode::ArchiveExisting || FilePath.IsEmpty() || !IFileManager::Get().FileExists(*FilePath))
        {
            return;
        }
        const FString ArchiveDir = FPaths::Combine(ExportRoot, TEXT("archives"), GodotAssetExporter::GetCurrentTimestampForFileName());
        IFileManager::Get().MakeDirectory(*ArchiveDir, true);
        const FString ArchivePath = FPaths::Combine(ArchiveDir, FPaths::GetCleanFilename(FilePath));
        IFileManager::Get().Move(*ArchivePath, *FilePath, true, true);
    }

    FString MakeExportFilePathWithCleanup(const FString& ExportRoot, const FString& Directory, const FString& BaseName, const FString& Extension, TSet<FString>& UsedFilePaths) const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        IFileManager::Get().MakeDirectory(*Directory, true);
        FString SafeBaseName = FPaths::MakeValidFileName(BaseName);
        if (SafeBaseName.IsEmpty())
        {
            SafeBaseName = TEXT("Export");
        }

        if (Settings && Settings->bVersionedSceneExports)
        {
            SafeBaseName += TEXT("_") + GodotAssetExporter::GetCurrentTimestampForFileName();
        }

        FString CandidatePath = FPaths::Combine(Directory, SafeBaseName + Extension);
        if (Settings && Settings->OutputCleanupMode == EGodotOutputCleanupMode::ArchiveExisting)
        {
            ArchiveExistingFileIfRequested(ExportRoot, CandidatePath);
            UsedFilePaths.Add(CandidatePath);
            return CandidatePath;
        }
        return GodotAssetExporter::MakeUniqueFilePath(Directory, SafeBaseName, Extension, UsedFilePaths);
    }

    TArray<UStaticMesh*> GetSelectedStaticMeshes() const
    {
        TArray<UStaticMesh*> StaticMeshes;
        FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
        TArray<FAssetData> SelectedAssets;
        ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
        for (const FAssetData& AssetData : SelectedAssets)
        {
            if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(AssetData.GetAsset()))
            {
                StaticMeshes.AddUnique(StaticMesh);
            }
        }
        return StaticMeshes;
    }

    TArray<AActor*> GetSelectedLevelActors() const
    {
        TArray<AActor*> Actors;
        if (!GEditor)
        {
            return Actors;
        }
        USelection* Selection = GEditor->GetSelectedActors();
        if (!Selection)
        {
            return Actors;
        }
        for (FSelectionIterator It(*Selection); It; ++It)
        {
            if (AActor* Actor = Cast<AActor>(*It))
            {
                Actors.AddUnique(Actor);
            }
        }
        return Actors;
    }

    UWorld* GetEditorWorld() const
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    TArray<AActor*> GetAllLevelActorsForManifest(UWorld* World) const
    {
        TArray<AActor*> Actors;
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        if (!World)
        {
            return Actors;
        }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor || Actor->IsTemplate())
            {
                continue;
            }
            if (Settings && Settings->bSkipGodotNoExportTaggedActorsInManifest && Actor->Tags.Contains(FName(TEXT("GodotNoExport"))))
            {
                continue;
            }
            Actors.Add(Actor);
        }
        return Actors;
    }

    TArray<UStaticMesh*> LoadCachedStaticMeshSet(const FString& ExportRoot) const
    {
        TArray<UStaticMesh*> Result;
        TArray<FString> Lines;
        const FString CachePath = FPaths::Combine(ExportRoot, TEXT("cache"), TEXT("last_static_mesh_assets.txt"));
        if (!FFileHelper::LoadFileToStringArray(Lines, *CachePath))
        {
            return Result;
        }
        for (FString Line : Lines)
        {
            Line.TrimStartAndEndInline();
            if (Line.IsEmpty())
            {
                continue;
            }
            if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *Line)))
            {
                Result.AddUnique(StaticMesh);
            }
        }
        return Result;
    }

    void BuildStaticMeshSnapshot(const TArray<UStaticMesh*>& StaticMeshes, TArray<FString>& OutLines) const
    {
        OutLines.Reset();
        for (UStaticMesh* StaticMesh : StaticMeshes)
        {
            OutLines.Add(GodotAssetExporter::MakeStaticMeshSnapshotLine(StaticMesh));
        }
        OutLines.Sort();
    }

    void BuildActorSnapshot(const TArray<AActor*>& Actors, TArray<FString>& OutLines) const
    {
        OutLines.Reset();
        for (AActor* Actor : Actors)
        {
            OutLines.Add(GodotAssetExporter::MakeActorSnapshotLine(Actor));
        }
        OutLines.Sort();
    }

    bool DryRunDiffShouldProceed(const FString& ExportRoot, const FString& CacheFileName, const FString& DiffTitle, const FString& DiffReportBaseName, const TArray<FString>& CurrentLines, FString& OutDiffReportPath) const
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        if (!Settings || (!Settings->bShowDryRunDiffBeforeExport && !Settings->bWriteDryRunDiffReports))
        {
            return true;
        }

        TArray<FString> PreviousLines;
        const FString SnapshotPath = FPaths::Combine(ExportRoot, TEXT("cache"), CacheFileName);
        FFileHelper::LoadFileToStringArray(PreviousLines, *SnapshotPath);
        const FString DiffText = GodotAssetExporter::BuildSnapshotDiffText(DiffTitle, PreviousLines, CurrentLines);
        const FString DiffJson = GodotAssetExporter::BuildSnapshotDiffJson(DiffTitle, PreviousLines, CurrentLines);

        if (Settings->bWriteDryRunDiffReports)
        {
            OutDiffReportPath = FPaths::Combine(ExportRoot, TEXT("logs"), DiffReportBaseName + TEXT("_dry_run_diff.txt"));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutDiffReportPath), true);
            FFileHelper::SaveStringToFile(DiffText, *OutDiffReportPath);
            if (Settings->bWriteDryRunDiffJson)
            {
                const FString JsonReportPath = FPaths::Combine(ExportRoot, TEXT("logs"), DiffReportBaseName + TEXT("_dry_run_diff.json"));
                FFileHelper::SaveStringToFile(DiffJson, *JsonReportPath);
            }
        }

        if (Settings->bShowDryRunDiffBeforeExport && PreviousLines.Num() > 0)
        {
            FString Summary = DiffText.Left(1800);
            if (DiffText.Len() > Summary.Len())
            {
                Summary += TEXT("\n...diff truncated in dialog. Full diff was written under logs/.\n");
            }
            Summary += TEXT("\nContinue with export?");
            const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::OkCancel, FText::FromString(Summary));
            return Choice == EAppReturnType::Ok;
        }
        return true;
    }

    void SaveSnapshot(const FString& ExportRoot, const FString& CacheFileName, const TArray<FString>& CurrentLines) const
    {
        const FString SnapshotPath = FPaths::Combine(ExportRoot, TEXT("cache"), CacheFileName);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(SnapshotPath), true);
        FFileHelper::SaveStringArrayToFile(CurrentLines, *SnapshotPath);
    }

    void ApplyCollisionModeToActors(const TArray<AActor*>& Actors)
    {
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        const EGodotCollisionExportMode Mode = Settings ? Settings->CollisionExportMode : EGodotCollisionExportMode::GodotSuffixOnly;
        const FString Suffix = GodotAssetExporter::CollisionSuffixForMode(Mode);
        if (Suffix.IsEmpty())
        {
            return;
        }
        int32 ChangedCount = 0;
        for (AActor* Actor : Actors)
        {
            if (!Actor || GodotAssetExporter::HasKnownGodotImportHint(Actor->GetActorLabel()))
            {
                continue;
            }
            TArray<UStaticMeshComponent*> StaticMeshComponents;
            Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);
            if (StaticMeshComponents.Num() == 0)
            {
                continue;
            }
            Actor->Modify();
            Actor->SetActorLabel(Actor->GetActorLabel() + Suffix, true);
            ++ChangedCount;
        }
        if (ChangedCount > 0)
        {
            UE_LOG(LogGodotAssetExporter, Log, TEXT("Applied collision suffix %s to %d actor(s)."), *Suffix, ChangedCount);
        }
    }

    void ExportStaticMeshesInternal(const TArray<UStaticMesh*>& StaticMeshes, const bool bFromCache)
    {
        if (StaticMeshes.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, bFromCache ? LOCTEXT("NoCachedStaticMeshes", "No cached Static Mesh asset set was found. Export selected Static Mesh assets once before using re-export.") : LOCTEXT("NoStaticMeshesSelected", "Select one or more Static Mesh assets in the Content Browser, then run Tools > Godot Export: Selected Static Mesh Assets."));
            return;
        }

        FString ExportRoot;
        if (!ChooseExportRoot(ExportRoot))
        {
            return;
        }
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        const GodotAssetExporter::FResolvedExportProfile Profile = GodotAssetExporter::ResolveExportProfile(Settings);
        const FString FormatExtension = GetFormatExtension();
        const FString FileExtension = FString(TEXT(".")) + FormatExtension;
        EnsureCommonExportDirectories(ExportRoot, Profile.bExportTexturesSeparately);
        CleanGeneratedFoldersIfRequested(ExportRoot, Profile.bExportTexturesSeparately);

        TArray<FString> SnapshotLines;
        BuildStaticMeshSnapshot(StaticMeshes, SnapshotLines);
        FString DiffReportPath;
        if (!DryRunDiffShouldProceed(ExportRoot, TEXT("last_static_mesh_snapshot.txt"), TEXT("Static Mesh Export"), TEXT("static_mesh"), SnapshotLines, DiffReportPath))
        {
            return;
        }

        UGLTFExportOptions* ExportOptions = GodotAssetExporter::CreateGodotGLTFExportOptions(Settings);
        const TSet<AActor*> EmptySelectedActors;
        const FString ModelsRootDir = FPaths::Combine(ExportRoot, TEXT("models"));
        const FString TexturesRootDir = FPaths::Combine(ExportRoot, TEXT("textures"));
        int32 MeshSuccessCount = 0;
        int32 MeshFailCount = 0;
        int32 TextureSuccessCount = 0;
        int32 TextureFailCount = 0;
        TSet<FString> UsedModelFilePaths;
        TSet<FString> UsedTextureFilePaths;
        TArray<GodotAssetExporter::FExportedMeshRecord> Records;
        TArray<FString> SourcePaths;
        TArray<FString> OutputPaths;

        for (UStaticMesh* StaticMesh : StaticMeshes)
        {
            if (!StaticMesh)
            {
                continue;
            }
            const FString MeshSubFolder = Profile.bPreserveFolders ? GodotAssetExporter::MakeSafeRelativeAssetFolder(StaticMesh) : FString();
            const FString ModelDirectory = MeshSubFolder.IsEmpty() ? ModelsRootDir : FPaths::Combine(ModelsRootDir, MeshSubFolder);
            const FString ModelPath = GodotAssetExporter::MakeUniqueFilePath(ModelDirectory, StaticMesh->GetName(), FileExtension, UsedModelFilePaths);

            GodotAssetExporter::FExportedMeshRecord Record;
            Record.AssetPath = StaticMesh->GetPathName();
            Record.ModelFile = ModelPath;
            GodotAssetExporter::WriteGodotImportPresetFile(ExportRoot, ModelPath, TEXT("godot_export_manifest.json"), TEXT("static_mesh_asset"), Profile, Record.ImportPresetFile);
            Record.Warnings = GodotAssetExporter::ValidateStaticMeshAsset(StaticMesh);

            const bool bMeshExported = UGLTFExporter::ExportToGLTF(StaticMesh, ModelPath, ExportOptions, EmptySelectedActors);
            if (bMeshExported)
            {
                ++MeshSuccessCount;
                SourcePaths.Add(StaticMesh->GetPathName());
                OutputPaths.Add(ModelPath);
            }
            else
            {
                ++MeshFailCount;
                Record.Warnings.Add(TEXT("UGLTFExporter::ExportToGLTF returned false."));
            }

            if (Profile.bExportTexturesSeparately)
            {
                TArray<UTexture*> Textures;
                GodotAssetExporter::CollectTexturesFromStaticMesh(StaticMesh, Textures);
                for (UTexture* Texture : Textures)
                {
                    if (!Texture)
                    {
                        continue;
                    }
                    const FString TextureSubFolder = Profile.bPreserveFolders ? GodotAssetExporter::MakeSafeRelativeAssetFolder(Texture) : FString();
                    const FString TextureDirectory = TextureSubFolder.IsEmpty() ? TexturesRootDir : FPaths::Combine(TexturesRootDir, TextureSubFolder);
                    const FString TexturePath = GodotAssetExporter::MakeUniqueFilePath(TextureDirectory, Texture->GetName(), TEXT(".png"), UsedTextureFilePaths);
                    if (GodotAssetExporter::ExportTextureToPng(Texture, TexturePath))
                    {
                        ++TextureSuccessCount;
                        Record.TextureFiles.Add(TexturePath);
                    }
                    else
                    {
                        ++TextureFailCount;
                        Record.Warnings.Add(FString::Printf(TEXT("Failed to export texture as PNG: %s"), *GetNameSafe(Texture)));
                    }
                }
            }
            Records.Add(MoveTemp(Record));
        }

        GodotAssetExporter::WriteStaticMeshManifest(ExportRoot, Records, FormatExtension, Profile);
        if (Profile.bValidationReports)
        {
            GodotAssetExporter::WriteStaticMeshValidationReport(ExportRoot, Records);
        }
        if (Settings && Settings->bWriteReexportCache)
        {
            GodotAssetExporter::WriteLastStaticMeshAssetSet(ExportRoot, StaticMeshes);
            GodotAssetExporter::WriteExportCache(ExportRoot, bFromCache ? TEXT("static_mesh_assets_reexport") : TEXT("static_mesh_assets"), SourcePaths, OutputPaths);
            SaveSnapshot(ExportRoot, TEXT("last_static_mesh_snapshot.txt"), SnapshotLines);
        }
        GodotAssetExporter::WriteGodotNotes(ExportRoot, FormatExtension);

        const FString FirstOutputPath = OutputPaths.Num() > 0 ? OutputPaths[0] : FString();
        const FString FirstGodotPath = MakeGodotResPathForOutput(ExportRoot, FirstOutputPath);
        const FString Summary = FString::Printf(TEXT("Godot static mesh export finished.\n\nPreset/profile: %s\nMeshes exported: %d\nMeshes failed: %d\nTextures exported: %d\nTextures failed/skipped: %d\nDiff report: %s\nFirst model: %s\nGodot path: %s\n\nUse Tools > Godot Export: Copy Last Godot res:// Path to copy the last cached output.\n\nFolder:\n%s"), *Profile.PresetName, MeshSuccessCount, MeshFailCount, TextureSuccessCount, TextureFailCount, DiffReportPath.IsEmpty() ? TEXT("<none>") : *DiffReportPath, FirstOutputPath.IsEmpty() ? TEXT("<none>") : *FirstOutputPath, FirstGodotPath.IsEmpty() ? TEXT("<not inside detected Godot project>") : *FirstGodotPath, *ExportRoot);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
    }

    bool ExportActorArrayAsScene(UWorld* World, const TArray<AActor*>& Actors, const FString& BaseName, GodotAssetExporter::ESceneExportType ExportType, const FString& TagGroup, bool bExportAllActorsWhenSetEmpty, FString ForcedExportRoot = FString())
    {
        if (!World)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoEditorWorld", "Could not find the current editor world."));
            return false;
        }
        if (!bExportAllActorsWhenSetEmpty && Actors.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoLevelActorsForExport", "No actors were found for this export."));
            return false;
        }

        FString ExportRoot = ForcedExportRoot;
        if (ExportRoot.IsEmpty())
        {
            if (!ChooseExportRoot(ExportRoot))
            {
                return false;
            }
        }
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        const GodotAssetExporter::FResolvedExportProfile Profile = GodotAssetExporter::ResolveExportProfile(Settings);
        const FString FormatExtension = GetFormatExtension();
        const FString FileExtension = FString(TEXT(".")) + FormatExtension;
        const FString ScenesDir = FPaths::Combine(ExportRoot, TEXT("scenes"));
        EnsureCommonExportDirectories(ExportRoot, false);
        if (ForcedExportRoot.IsEmpty())
        {
            CleanGeneratedFoldersIfRequested(ExportRoot, false);
        }

        TArray<AActor*> ActorsForManifest = bExportAllActorsWhenSetEmpty ? GetAllLevelActorsForManifest(World) : Actors;
        TArray<FString> SnapshotLines;
        BuildActorSnapshot(ActorsForManifest, SnapshotLines);
        FString DiffReportPath;
        const FString DiffBaseName = FPaths::MakeValidFileName(BaseName);
        const FString SnapshotName = FString::Printf(TEXT("last_%s_snapshot.txt"), *DiffBaseName);
        if (!DryRunDiffShouldProceed(ExportRoot, SnapshotName, BaseName, DiffBaseName, SnapshotLines, DiffReportPath))
        {
            return false;
        }

        if (!bExportAllActorsWhenSetEmpty)
        {
            ApplyCollisionModeToActors(ActorsForManifest);
        }

        FString SceneBaseName = BaseName;
        if (Settings && Settings->bTimestampSelectedActorPrefabNames && ExportType != GodotAssetExporter::ESceneExportType::CurrentLevel)
        {
            SceneBaseName += TEXT("_") + GodotAssetExporter::GetCurrentTimestampForFileName();
        }
        TSet<FString> UsedScenePaths;
        const FString ScenePath = MakeExportFilePathWithCleanup(ExportRoot, ScenesDir, SceneBaseName, FileExtension, UsedScenePaths);

        TSet<AActor*> SelectedActorsSet;
        if (!bExportAllActorsWhenSetEmpty)
        {
            for (AActor* Actor : Actors)
            {
                if (Actor)
                {
                    SelectedActorsSet.Add(Actor);
                }
            }
        }

        UGLTFExportOptions* ExportOptions = GodotAssetExporter::CreateGodotGLTFExportOptions(Settings);
        const bool bExported = UGLTFExporter::ExportToGLTF(World, ScenePath, ExportOptions, SelectedActorsSet);

        GodotAssetExporter::FSceneExportRecord Record;
        Record.ExportType = ExportType;
        Record.SourceLevel = GodotAssetExporter::GetWorldPackagePath(World);
        Record.SceneFile = ScenePath;
        Record.DryRunDiffReportFile = DiffReportPath;
        Record.TagGroup = TagGroup;
        Record.Actors = ActorsForManifest;
        Record.bSelectedActorsSetWasEmpty = bExportAllActorsWhenSetEmpty;
        Record.PrefabOrigin = GodotAssetExporter::CalculatePrefabOrigin(ActorsForManifest, Settings);
        if (Record.PrefabOrigin.Mode == EGodotPrefabOriginMode::ActorNamedGodotRoot && !Record.PrefabOrigin.bRootActorFound)
        {
            Record.Warnings.Add(FString::Printf(TEXT("Prefab origin mode expected actor label '%s', but no matching actor was found."), *Record.PrefabOrigin.RootActorLabel));
        }

        if (Profile.bValidationReports)
        {
            GodotAssetExporter::WriteSceneValidationReport(ExportRoot, Record, Profile);
        }
        {
            const FString SceneBaseNameForManifest = FPaths::GetBaseFilename(Record.SceneFile);
            const FString ManifestRelative = FString::Printf(TEXT("manifests/%s_manifest.json"), *SceneBaseNameForManifest);
            GodotAssetExporter::WriteGodotImportPresetFile(ExportRoot, Record.SceneFile, ManifestRelative, GodotAssetExporter::GetSceneExportTypeString(ExportType), Profile, Record.ImportPresetFile);
        }
        GodotAssetExporter::WriteSceneManifest(ExportRoot, Record, FormatExtension, Profile);

        TArray<FString> SourcePaths;
        if (bExportAllActorsWhenSetEmpty)
        {
            SourcePaths.Add(GodotAssetExporter::GetWorldPackagePath(World));
        }
        else
        {
            for (AActor* Actor : ActorsForManifest)
            {
                if (Actor)
                {
                    SourcePaths.Add(Actor->GetPathName());
                }
            }
        }
        if (Settings && Settings->bWriteReexportCache)
        {
            TArray<FString> OutputPaths;
            OutputPaths.Add(ScenePath);
            GodotAssetExporter::WriteExportCache(ExportRoot, GodotAssetExporter::GetSceneExportTypeString(ExportType), SourcePaths, OutputPaths);
            SaveSnapshot(ExportRoot, SnapshotName, SnapshotLines);
        }
        GodotAssetExporter::WriteGodotNotes(ExportRoot, FormatExtension);

        const FString SceneGodotPath = MakeGodotResPathForOutput(ExportRoot, ScenePath);
        const FString ManifestPath = FPaths::Combine(ExportRoot, TEXT("manifests"), FPaths::GetBaseFilename(Record.SceneFile) + TEXT("_manifest.json"));
        const FString Summary = FString::Printf(TEXT("Godot scene export %s.\n\nMode: %s\nPreset/profile: %s\nActors listed in manifest: %d\nValidation warnings: %d\nPrefab origin: %s\nDiff report: %s\nValidation report: %s\nManifest: %s\nScene file:\n%s\nGodot path:\n%s\n\nUse Tools > Godot Export: Copy Last Godot res:// Path to copy the last cached output.\n\nFolder:\n%s"), bExported ? TEXT("finished") : TEXT("failed"), *GodotAssetExporter::GetSceneExportTypeString(ExportType), *Profile.PresetName, ActorsForManifest.Num(), Record.Warnings.Num(), *Record.PrefabOrigin.ModeName, DiffReportPath.IsEmpty() ? TEXT("<none>") : *DiffReportPath, Record.ValidationReportFile.IsEmpty() ? TEXT("<none>") : *Record.ValidationReportFile, *ManifestPath, *ScenePath, SceneGodotPath.IsEmpty() ? TEXT("<not inside detected Godot project>") : *SceneGodotPath, *ExportRoot);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return bExported;
    }

    void ExportSelectedStaticMeshes()
    {
        ExportStaticMeshesInternal(GetSelectedStaticMeshes(), false);
    }

    void ReExportLastStaticMeshSet()
    {
        const FString ExportRoot = GetConfiguredExportRoot();
        ExportStaticMeshesInternal(LoadCachedStaticMeshSet(ExportRoot), true);
    }

    void ExportSelectedActorsAsPrefab()
    {
        UWorld* World = GetEditorWorld();
        const TArray<AActor*> Actors = GetSelectedLevelActors();
        if (Actors.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoLevelActorsSelected", "Select one or more actors in the level viewport or World Outliner, then run the prefab export."));
            return;
        }
        const FString BaseName = FString::Printf(TEXT("Prefab_%s_SelectedActors"), *GodotAssetExporter::GetWorldDisplayName(World));
        ExportActorArrayAsScene(World, Actors, BaseName, GodotAssetExporter::ESceneExportType::SelectedActorsPrefab, FString(), false);
    }

    void ExportCurrentLevelScene()
    {
        if (!MaybeWarnFullLevelExport())
        {
            return;
        }
        UWorld* World = GetEditorWorld();
        const FString BaseName = FString::Printf(TEXT("Level_%s"), *GodotAssetExporter::GetWorldDisplayName(World));
        const TArray<AActor*> EmptyActors;
        ExportActorArrayAsScene(World, EmptyActors, BaseName, GodotAssetExporter::ESceneExportType::CurrentLevel, FString(), true);
    }

    void CollectTaggedActorGroups(UWorld* World, TMap<FString, TArray<AActor*>>& OutGroups) const
    {
        OutGroups.Reset();
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        FString Prefix = Settings ? Settings->ExportTagPrefix : TEXT("GodotExport");
        if (Prefix.IsEmpty())
        {
            Prefix = TEXT("GodotExport");
        }
        const FString PrefixWithColon = Prefix + TEXT(":");
        if (!World)
        {
            return;
        }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor || Actor->IsTemplate())
            {
                continue;
            }
            if (Actor->Tags.Contains(FName(TEXT("GodotNoExport"))))
            {
                continue;
            }
            for (const FName& TagName : Actor->Tags)
            {
                const FString Tag = TagName.ToString();
                FString GroupName;
                if (Tag.Equals(Prefix, ESearchCase::IgnoreCase))
                {
                    GroupName = TEXT("Default");
                }
                else if (Tag.StartsWith(PrefixWithColon, ESearchCase::IgnoreCase))
                {
                    GroupName = Tag.RightChop(PrefixWithColon.Len());
                    if (GroupName.IsEmpty())
                    {
                        GroupName = TEXT("Default");
                    }
                }
                if (!GroupName.IsEmpty())
                {
                    OutGroups.FindOrAdd(GroupName).AddUnique(Actor);
                }
            }
        }
    }

    void ExportActorTagGroups()
    {
        UWorld* World = GetEditorWorld();
        if (!World)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWorldForTagGroups", "Could not find the current editor world."));
            return;
        }
        TMap<FString, TArray<AActor*>> Groups;
        CollectTaggedActorGroups(World, Groups);
        if (Groups.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoTagGroups", "No actors were tagged GodotExport or GodotExport:GroupName."));
            return;
        }
        FString ExportRoot;
        if (!ChooseExportRoot(ExportRoot))
        {
            return;
        }
        CleanGeneratedFoldersIfRequested(ExportRoot, false);
        int32 GroupCount = 0;
        for (const TPair<FString, TArray<AActor*>>& Pair : Groups)
        {
            const FString BaseName = FString::Printf(TEXT("Prefab_%s_%s"), *GodotAssetExporter::GetWorldDisplayName(World), *FPaths::MakeValidFileName(Pair.Key));
            if (ExportActorArrayAsScene(World, Pair.Value, BaseName, GodotAssetExporter::ESceneExportType::TaggedActorGroup, Pair.Key, false, ExportRoot))
            {
                ++GroupCount;
            }
        }
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Exported %d actor tag group(s)."), GroupCount)));
    }

    void ValidateSelectionForGodot()
    {
        const TArray<UStaticMesh*> StaticMeshes = GetSelectedStaticMeshes();
        const TArray<AActor*> Actors = GetSelectedLevelActors();
        if (Actors.Num() > 0)
        {
            ValidateActorsForGodot(Actors, TEXT("Validation_SelectedActors"), GodotAssetExporter::ESceneExportType::SelectedActorsPrefab, FString());
            return;
        }
        if (StaticMeshes.Num() > 0)
        {
            ValidateStaticMeshesForGodot(StaticMeshes);
            return;
        }
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NothingSelectedForValidation", "Select level actors or Static Mesh assets before running validation."));
    }

    void ValidateCurrentLevelForGodot()
    {
        UWorld* World = GetEditorWorld();
        ValidateActorsForGodot(GetAllLevelActorsForManifest(World), TEXT("Validation_CurrentLevel"), GodotAssetExporter::ESceneExportType::CurrentLevel, FString());
    }

    void ValidateStaticMeshesForGodot(const TArray<UStaticMesh*>& StaticMeshes)
    {
        FString ExportRoot = GetConfiguredExportRoot();
        EnsureCommonExportDirectories(ExportRoot, false);
        TArray<GodotAssetExporter::FExportedMeshRecord> Records;
        int32 WarningCount = 0;
        for (UStaticMesh* StaticMesh : StaticMeshes)
        {
            GodotAssetExporter::FExportedMeshRecord Record;
            Record.AssetPath = StaticMesh ? StaticMesh->GetPathName() : TEXT("<null>");
            Record.Warnings = GodotAssetExporter::ValidateStaticMeshAsset(StaticMesh);
            WarningCount += Record.Warnings.Num();
            Records.Add(MoveTemp(Record));
        }
        GodotAssetExporter::WriteStaticMeshValidationReport(ExportRoot, Records);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Validated %d Static Mesh asset(s). Warnings: %d\n\nReport:\n%s"), StaticMeshes.Num(), WarningCount, *FPaths::Combine(ExportRoot, TEXT("logs"), TEXT("static_mesh_validation_report.txt")))));
    }

    void ValidateActorsForGodot(const TArray<AActor*>& Actors, const FString& ReportBaseName, GodotAssetExporter::ESceneExportType ExportType, const FString& TagGroup)
    {
        UWorld* World = GetEditorWorld();
        FString ExportRoot = GetConfiguredExportRoot();
        EnsureCommonExportDirectories(ExportRoot, false);
        const UGodotAssetExporterSettings* Settings = GetDefault<UGodotAssetExporterSettings>();
        const GodotAssetExporter::FResolvedExportProfile Profile = GodotAssetExporter::ResolveExportProfile(Settings);
        GodotAssetExporter::FSceneExportRecord Record;
        Record.ExportType = ExportType;
        Record.SourceLevel = GodotAssetExporter::GetWorldPackagePath(World);
        Record.SceneFile = FPaths::Combine(ExportRoot, TEXT("scenes"), ReportBaseName + TEXT(".glb"));
        Record.TagGroup = TagGroup;
        Record.Actors = Actors;
        Record.PrefabOrigin = GodotAssetExporter::CalculatePrefabOrigin(Actors, Settings);
        GodotAssetExporter::WriteSceneValidationReport(ExportRoot, Record, Profile);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Validated %d actor(s). Warnings: %d\n\nReport:\n%s"), Actors.Num(), Record.Warnings.Num(), *Record.ValidationReportFile)));
    }

    void ApplySuffixToSelectedActors(const FString& Suffix)
    {
        const TArray<AActor*> Actors = GetSelectedLevelActors();
        if (Actors.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoActorsForImportHint", "Select one or more actors in the level viewport or World Outliner first."));
            return;
        }
        int32 ChangedCount = 0;
        for (AActor* Actor : Actors)
        {
            if (!Actor)
            {
                continue;
            }
            Actor->Modify();
            const FString OldLabel = Actor->GetActorLabel();
            const FString BaseLabel = GodotAssetExporter::RemoveKnownGodotImportHint(OldLabel);
            const FString NewLabel = BaseLabel + Suffix;
            if (OldLabel != NewLabel)
            {
                Actor->SetActorLabel(NewLabel, true);
                ++ChangedCount;
            }
        }
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Applied %s to %d selected actor label(s)."), *Suffix, ChangedCount)));
    }

    void ClearImportHintsOnSelectedActors()
    {
        const TArray<AActor*> Actors = GetSelectedLevelActors();
        if (Actors.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoActorsForClearImportHint", "Select one or more actors in the level viewport or World Outliner first."));
            return;
        }
        int32 ChangedCount = 0;
        for (AActor* Actor : Actors)
        {
            if (!Actor)
            {
                continue;
            }
            Actor->Modify();
            const FString OldLabel = Actor->GetActorLabel();
            const FString NewLabel = GodotAssetExporter::RemoveKnownGodotImportHint(OldLabel);
            if (OldLabel != NewLabel)
            {
                Actor->SetActorLabel(NewLabel, true);
                ++ChangedCount;
            }
        }
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Cleared Godot import hints from %d selected actor label(s)."), ChangedCount)));
    }

    void PrepareCollisionSuffixesForSelectedActors()
    {
        const TArray<AActor*> Actors = GetSelectedLevelActors();
        if (Actors.Num() == 0)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoActorsForCollisionPrep", "Select one or more actors first."));
            return;
        }
        ApplyCollisionModeToActors(Actors);
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CollisionPrepFinished", "Prepared selected actor labels using the configured collision mode. Actors that already had a Godot import-hint suffix were left alone."));
    }

    void CleanGeneratedExportFolders()
    {
        const FString ExportRoot = GetConfiguredExportRoot();
        TArray<FString> Folders;
        Folders.Add(TEXT("models"));
        Folders.Add(TEXT("textures"));
        Folders.Add(TEXT("scenes"));
        Folders.Add(TEXT("manifests"));
        Folders.Add(TEXT("import_presets"));
        Folders.Add(TEXT("logs"));
        Folders.Add(TEXT("postprocess"));
        for (const FString& Folder : Folders)
        {
            const FString FolderPath = FPaths::Combine(ExportRoot, Folder);
            if (IFileManager::Get().DirectoryExists(*FolderPath))
            {
                IFileManager::Get().DeleteDirectory(*FolderPath, false, true);
            }
        }
        EnsureCommonExportDirectories(ExportRoot, true);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Printf(TEXT("Cleaned generated export folders under:\n%s\n\nArchives and cache were preserved."), *ExportRoot)));
    }

    void OpenExportFolder()
    {
        const FString ExportRoot = GetConfiguredExportRoot();
        IFileManager::Get().MakeDirectory(*ExportRoot, true);
        FPlatformProcess::ExploreFolder(*ExportRoot);
    }

    void MarkSelectedActorsCollision() { ApplySuffixToSelectedActors(TEXT("-col")); }
    void MarkSelectedActorsConvexCollision() { ApplySuffixToSelectedActors(TEXT("-convcol")); }
    void MarkSelectedActorsCollisionOnly() { ApplySuffixToSelectedActors(TEXT("-colonly")); }
    void MarkSelectedActorsConvexCollisionOnly() { ApplySuffixToSelectedActors(TEXT("-convcolonly")); }
    void MarkSelectedActorsNavmesh() { ApplySuffixToSelectedActors(TEXT("-navmesh")); }
    void MarkSelectedActorsNoImport() { ApplySuffixToSelectedActors(TEXT("-noimp")); }
    void ClearSelectedActorImportHints() { ClearImportHintsOnSelectedActors(); }
};

IMPLEMENT_MODULE(FGodotAssetExporterModule, GodotAssetExporter)

#undef LOCTEXT_NAMESPACE
