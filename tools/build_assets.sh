#!/bin/bash
set -euo pipefail

NEKOMOTE="$(cd "$(dirname "$0")/.." && pwd)"
REFERENCE="${1:-/run/media/chocola/DATA/Project/NekoWebShow}"
ULYSSES="${2:-$NEKOMOTE/refer/Ulysses-FreeMoteToolkit-v4.8.0}"
OUTPUT="$NEKOMOTE/assets/characters"
IMPORT="$NEKOMOTE/build/release/bin/nekomote_import"
SESSION_TOOL="$NEKOMOTE/tools/import_session/import_session.py"

MODELS=(
    "chocola:chocola-date-a"
    "milk:milk-winter"
    "vanilla:vanilla-lolita"
    "fraise:fraise-france-maid-b"
)

echo "Building assets to $OUTPUT"
echo "Reference: $REFERENCE"
echo "Ulysses: $ULYSSES"

for entry in "${MODELS[@]}"; do
    IFS=':' read -r character model <<< "$entry"
    target="$OUTPUT/$model"
    echo "=== $model ==="
    mkdir -p "$target"

    src_dir="$REFERENCE/model_conversion/$character/$model"
    json_file="$src_dir/$model.json"
    if [ ! -f "$json_file" ]; then
        json_file="$src_dir/$model.pure.json"
    fi
    resx_file="$src_dir/$model.resx.json"
    if [ ! -f "$resx_file" ]; then
        resx_file="$src_dir/$model.pure.resx.json"
    fi

    "$IMPORT" "$json_file" "$resx_file" --output "$target" --resource-root "$src_dir"
    python3 "$SESSION_TOOL" --reference-root "$REFERENCE" --model "$model" --output "$target/session.nks.json"
done

echo "Done. Assets in $OUTPUT"
