param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Project,

    [switch]$NoCopy,

    [string]$GodotProject
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Installer = Join-Path $ScriptDir "install_godot_asset_exporter.py"

if (-not (Test-Path $Installer)) {
    throw "Could not find installer script: $Installer"
}

$PythonCommand = $null
if (Get-Command py -ErrorAction SilentlyContinue) {
    $PythonCommand = "py"
    $PythonArgs = @("-3", $Installer, $Project)
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $PythonCommand = "python"
    $PythonArgs = @($Installer, $Project)
} elseif (Get-Command python3 -ErrorAction SilentlyContinue) {
    $PythonCommand = "python3"
    $PythonArgs = @($Installer, $Project)
} else {
    throw "Python 3 was not found on PATH. Install Python 3 or run install_godot_asset_exporter.py manually with a known interpreter."
}

if ($NoCopy) {
    $PythonArgs += "--no-copy"
}

if ($GodotProject) {
    $PythonArgs += @("--godot-project", $GodotProject)
}

& $PythonCommand @PythonArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
