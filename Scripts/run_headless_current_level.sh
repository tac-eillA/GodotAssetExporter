#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 4 ]]; then
  echo "Usage: $0 /path/to/UnrealEditor-Cmd /path/to/MyProject.uproject /Game/Maps/LVL_TestArena /path/to/GodotProject/unreal_exports [OutputName]" >&2
  exit 2
fi
UNREAL_EDITOR_CMD="$1"
UPROJECT="$2"
MAP="$3"
EXPORT_ROOT="$4"
OUTPUT_NAME="${5:-}"
ARGS=("$UPROJECT" "-run=GodotAssetExporter" "-Mode=CurrentLevel" "-Map=$MAP" "-ExportRoot=$EXPORT_ROOT")
if [[ -n "$OUTPUT_NAME" ]]; then
  ARGS+=("-OutputName=$OUTPUT_NAME")
fi
exec "$UNREAL_EDITOR_CMD" "${ARGS[@]}"
