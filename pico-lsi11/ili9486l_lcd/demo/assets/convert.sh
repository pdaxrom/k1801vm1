#!/bin/bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$(cd "$script_dir/../.." && pwd)"

python3 "$project_dir/demo/tools/png_to_logo.py" "$1" --output "$script_dir/logo.jpg"
