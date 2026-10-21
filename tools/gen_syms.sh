#!/usr/bin/env bash
# Writes syms/sm64.us.syms.toml and syms/sm64.us.datasyms.toml from the ELF tools/build_decomp.sh brought back
# (build/decomp/sm64.us.mdebug.elf and its map): builds the pinned N64Recomp (tools/build_n64recomp.sh) and runs
# tools/gen_syms.py, which says how.
#
# Usage: tools/gen_syms.sh [cmake arguments for tools/build_n64recomp.sh]
set -euo pipefail
cd "$(dirname "$0")/.."
elf=build/decomp/sm64.us.mdebug.elf
[[ -f "$elf" && -f "$elf.map" ]] || { echo "no $elf and $elf.map: run tools/build_decomp.sh first" >&2; exit 1; }
bash tools/build_n64recomp.sh "$@"
if command -v py > /dev/null; then python=(py -3); else python=(python3); fi
"${python[@]}" -I tools/gen_syms.py build/n64recomp/N64Recomp "$elf" "$elf.map"
