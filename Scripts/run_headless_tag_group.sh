#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 5 ]]; then
  echo "Usage: $0 /path/to/UnrealEditor-Cmd /path/to/MyProject.uproject /Game/Maps/LVL_TestArena Room_A /path/to/GodotProject/unreal_exports" >&2
  exit 2
fi
exec "$1" "$2" -run=GodotAssetExporter -Mode=TagGroup -Map="$3" -Tag="$4" -ExportRoot="$5"
