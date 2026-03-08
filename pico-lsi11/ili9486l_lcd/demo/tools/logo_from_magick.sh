#!/bin/bash

set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  echo "Usage: bash demo/tools/logo_from_magick.sh <image> [none|cw|ccw|180]" >&2
  exit 1
fi

input_path="$1"
rotate="${2:-none}"

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$(cd "$script_dir/../.." && pwd)"
tmp_png="$(mktemp /tmp/ili9486l_logo_XXXXXX.png)"
output_path="$project_dir/demo/assets/logo.jpg"

cleanup() {
  rm -f "$tmp_png"
}
trap cleanup EXIT

case "$rotate" in
  none)
    magick "$input_path" -auto-orient -background black -alpha remove -alpha off -resize 480x320\! "$tmp_png"
    ;;
  cw)
    magick "$input_path" -auto-orient -background black -alpha remove -alpha off -rotate 90 -resize 480x320\! "$tmp_png"
    ;;
  ccw)
    magick "$input_path" -auto-orient -background black -alpha remove -alpha off -rotate -90 -resize 480x320\! "$tmp_png"
    ;;
  180)
    magick "$input_path" -auto-orient -background black -alpha remove -alpha off -rotate 180 -resize 480x320\! "$tmp_png"
    ;;
  *)
    echo "Unknown rotation: $rotate" >&2
    echo "Expected one of: none, cw, ccw, 180" >&2
    exit 1
    ;;
esac

python3 "$project_dir/demo/tools/png_to_logo.py" "$tmp_png" --output "$output_path"

echo "Updated $output_path"
