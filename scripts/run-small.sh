#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
binary="$repo_root/build/alife"
config="$repo_root/configs/small.conf"

if [ ! -f "$config" ]; then
    printf 'error: configuration not found: %s\n' "$config" >&2
    exit 1
fi

make -C "$repo_root" all

if [ ! -x "$binary" ]; then
    printf 'error: build did not create executable: %s\n' "$binary" >&2
    exit 1
fi

cd "$repo_root"
exec "$binary" run --config "$config" "$@"
