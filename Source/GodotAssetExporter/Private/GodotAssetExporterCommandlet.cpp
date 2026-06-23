#include "GodotAssetExporterCommandlet.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Exporters/GLTFExporter.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Options/GLTFExportOptions.h"

namespace GodotAssetExporterCommandletInternal
{
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

    static FString NormalizeExportRoot(FString ExportRoot)
    {
        if (ExportRoot.IsEmpty())
        {
            ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GodotExports")));
        }
        else if (FPaths::IsRelative(ExportRoot))
        {
            ExportRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ExportRoot));
        }
        FPaths::NormalizeDirectoryName(ExportRoot);
        return ExportRoot;
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
        if (FPaths::MakePathRelativeTo(Relative, *RootWithSlash))
        {
            Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
            return Relative;
        }
        Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
        return Relative;
    }

    static FString LongPackageNameToObjectPath(const FString& MapPath)
    {
        if (MapPath.Contains(TEXT(".")))
        {
            return MapPath;
        }
        const FString ShortName = FPackageName::GetShortName(MapPath);
        return MapPath + TEXT(".") + ShortName;
    }

    static UGLTFExportOptions* CreateOptionsFromParams(const FString& Params)
    {
        UGLTFExportOptions* Options = NewObject<UGLTFExportOptions>(GetTransientPackage());
        Options->bExportCameras = FParse::Param(*Params, TEXT("ExportCameras"));
        Options->bExportLights = FParse::Param(*Params, TEXT("ExportLights"));
        Options->bExportHiddenInGame = FParse::Param(*Params, TEXT("ExportHiddenInGame"));
        Options->bExportLightmaps = FParse::Param(*Params, TEXT("ExportLightmaps"));
        Options->bExportVertexColors = !FParse::Param(*Params, TEXT("NoVertexColors"));
        Options->bAdjustNormalmaps = !FParse::Param(*Params, TEXT("NoAdjustNormalMaps"));
        Options->bExportTextureTransforms = !FParse::Param(*Params, TEXT("NoTextureTransforms"));
        return Options;
    }

    static FString MakeManifestForWorld(const FString& ExportType, const FString& MapPath, const FString& ScenePath, const FString& ExportRoot, const FString& TagGroup, UWorld* World, const bool bSuccess)
    {
        FString Json;
        Json += TEXT("{\n");
        Json += TEXT("  \"exporter\": \"GodotAssetExporter\",\n");
        Json += TEXT("  \"runner\": \"commandlet\",\n");
        Json += FString::Printf(TEXT("  \"generated_at\": %s,\n"), *JsonString(FDateTime::Now().ToString()));
        Json += FString::Printf(TEXT("  \"export_type\": %s,\n"), *JsonString(ExportType));
        Json += FString::Printf(TEXT("  \"map\": %s,\n"), *JsonString(MapPath));
        Json += FString::Printf(TEXT("  \"scene\": %s,\n"), *JsonString(MakeRelativeForManifest(ScenePath, ExportRoot)));
        Json += FString::Printf(TEXT("  \"tag_group\": %s,\n"), *JsonString(TagGroup));
        Json += FString::Printf(TEXT("  \"success\": %s,\n"), *BoolToJson(bSuccess));
        Json += TEXT("  \"actors\": [\n");
        int32 ActorIndex = 0;
        if (World)
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                AActor* Actor = *It;
                if (!Actor || Actor->IsTemplate())
                {
                    continue;
                }
                Json += FString::Printf(TEXT("    {\"name\": %s, \"class\": %s}"), *JsonString(Actor->GetName()), *JsonString(Actor->GetClass() ? Actor->GetClass()->GetName() : FString()));
                ++ActorIndex;
                Json += TEXT(",\n");
            }
        }
        if (Json.EndsWith(TEXT(",\n")))
        {
            Json = Json.LeftChop(2) + TEXT("\n");
        }
        Json += TEXT("  ]\n");
        Json += TEXT("}\n");
        return Json;
    }
}

UGodotAssetExporterCommandlet::UGodotAssetExporterCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UGodotAssetExporterCommandlet::Main(const FString& Params)
{
    FString Mode;
    FString MapPath;
    FString ExportRoot;
    FString OutputName;
    FString TagGroup;

    FParse::Value(*Params, TEXT("Mode="), Mode);
    FParse::Value(*Params, TEXT("Map="), MapPath);
    FParse::Value(*Params, TEXT("ExportRoot="), ExportRoot);
    FParse::Value(*Params, TEXT("OutputName="), OutputName);
    FParse::Value(*Params, TEXT("Tag="), TagGroup);

    if (Mode.IsEmpty())
    {
        Mode = TEXT("CurrentLevel");
    }
    if (MapPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("GodotAssetExporter commandlet requires -Map=/Game/Maps/YourMap."));
        return 2;
    }

    ExportRoot = GodotAssetExporterCommandletInternal::NormalizeExportRoot(ExportRoot);
    const FString ScenesDir = FPaths::Combine(ExportRoot, TEXT("scenes"));
    const FString ManifestsDir = FPaths::Combine(ExportRoot, TEXT("manifests"));
    IFileManager::Get().MakeDirectory(*ScenesDir, true);
    IFileManager::Get().MakeDirectory(*ManifestsDir, true);

    const FString ObjectPath = GodotAssetExporterCommandletInternal::LongPackageNameToObjectPath(MapPath);
    UWorld* World = LoadObject<UWorld>(nullptr, *ObjectPath);
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("Could not load world: %s (object path: %s)"), *MapPath, *ObjectPath);
        return 3;
    }

    if (OutputName.IsEmpty())
    {
        OutputName = FString::Printf(TEXT("Headless_%s"), *FPackageName::GetShortName(MapPath));
        if (!TagGroup.IsEmpty())
        {
            OutputName += TEXT("_") + FPaths::MakeValidFileName(TagGroup);
        }
    }

    const bool bUseGltf = FParse::Param(*Params, TEXT("GLTF"));
    const FString Extension = bUseGltf ? TEXT(".gltf") : TEXT(".glb");
    const FString ScenePath = FPaths::Combine(ScenesDir, FPaths::MakeValidFileName(OutputName) + Extension);

    UGLTFExportOptions* Options = GodotAssetExporterCommandletInternal::CreateOptionsFromParams(Params);
    TSet<AActor*> SelectedActors;

    if (Mode.Equals(TEXT("TagGroup"), ESearchCase::IgnoreCase) || !TagGroup.IsEmpty())
    {
        if (TagGroup.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("Mode=TagGroup requires -Tag=GroupName or -Tag=GodotExport:GroupName."));
            return 4;
        }
        const FString PrefixTag = TagGroup.StartsWith(TEXT("GodotExport")) ? TagGroup : FString(TEXT("GodotExport:")) + TagGroup;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor || Actor->IsTemplate())
            {
                continue;
            }
            for (const FName& Tag : Actor->Tags)
            {
                if (Tag.ToString().Equals(PrefixTag, ESearchCase::IgnoreCase))
                {
                    SelectedActors.Add(Actor);
                    break;
                }
            }
        }
        if (SelectedActors.Num() == 0)
        {
            UE_LOG(LogTemp, Error, TEXT("No actors found for tag group: %s"), *PrefixTag);
            return 5;
        }
    }

    const bool bSuccess = UGLTFExporter::ExportToGLTF(World, ScenePath, Options, SelectedActors);
    const FString Manifest = GodotAssetExporterCommandletInternal::MakeManifestForWorld(Mode, MapPath, ScenePath, ExportRoot, TagGroup, World, bSuccess);
    const FString ManifestPath = FPaths::Combine(ManifestsDir, FPaths::GetBaseFilename(ScenePath) + TEXT("_headless_manifest.json"));
    FFileHelper::SaveStringToFile(Manifest, *ManifestPath);

    UE_LOG(LogTemp, Display, TEXT("GodotAssetExporter commandlet wrote scene: %s"), *ScenePath);
    UE_LOG(LogTemp, Display, TEXT("GodotAssetExporter commandlet wrote manifest: %s"), *ManifestPath);
    return bSuccess ? 0 : 1;
}
