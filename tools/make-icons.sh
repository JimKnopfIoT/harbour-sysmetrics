#!/usr/bin/env bash
# Render icon assets from icons/icon.svg.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
svg="$root/icons/icon.svg"
base="harbour-sysmetrics"
command -v rsvg-convert >/dev/null || { echo "need rsvg-convert" >&2; exit 1; }
for s in 86 108 128 172 256; do
    mkdir -p "$root/icons/${s}x${s}"
    rsvg-convert -w "$s" -h "$s" "$svg" -o "$root/icons/${s}x${s}/$base.png"
done
echo "done"
