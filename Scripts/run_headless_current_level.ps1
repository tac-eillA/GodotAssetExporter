param(
    [Parameter(Mandatory=$true)][string]$UnrealEditorCmd,
    [Parameter(Mandatory=$true)][string]$UProject,
    [Parameter(Mandatory=$true)][string]$Map,
    [Parameter(Mandatory=$true)][string]$ExportRoot,
    [string]$OutputName = ""
)

$args = @($UProject, "-run=GodotAssetExporter", "-Mode=CurrentLevel", "-Map=$Map", "-ExportRoot=$ExportRoot")
if ($OutputName -ne "") { $args += "-OutputName=$OutputName" }
& $UnrealEditorCmd @args
exit $LASTEXITCODE
