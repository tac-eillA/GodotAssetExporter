#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GodotAssetExporterCommandlet.generated.h"

/**
 * First-pass CI/headless exporter.
 *
 * Example:
 * UnrealEditor-Cmd.exe MyProject.uproject -run=GodotAssetExporter -Mode=CurrentLevel -Map=/Game/Maps/LVL_TestArena -ExportRoot=C:/MyGodotProject/unreal_exports
 */
UCLASS()
class GODOTASSETEXPORTER_API UGodotAssetExporterCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UGodotAssetExporterCommandlet();
    virtual int32 Main(const FString& Params) override;
};
