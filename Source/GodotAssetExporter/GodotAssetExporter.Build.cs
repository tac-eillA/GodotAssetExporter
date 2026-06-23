using UnrealBuildTool;

public class GodotAssetExporter : ModuleRules
{
    public GodotAssetExporter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DeveloperSettings"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "ContentBrowser",
            "DesktopPlatform",
            "Projects",
            "GLTFExporter",
            "ApplicationCore",
            "RHI"
        });
    }
}
