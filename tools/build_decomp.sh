#!/usr/bin/env bash
# Builds the decomp's US version (lib/sm64's commit) from your ROM and brings its ELF back for tools/gen_syms.sh. It runs
# in the decomp's own build image (tools/docker/Dockerfile.decomp) with Docker, here or on a Linux host over ssh
# (BUILD_HOST). Never in CI: the build needs the ROM.
#
# `make VERSION=us` extracts the ROM's assets, builds with IDO 5.3 and checks the result against sm64.us.sha1. Then the
# ELF is linked again with each object's .mdebug kept (sm64.ld discards it), for the static functions' symbols IDO
# leaves out of the symbol table (tools/gen_syms.py); the code is the same bytes. Into build/decomp/: sm64.us.elf and
# sm64.us.mdebug.elf with its map. Nothing from it is committed but what tools/gen_syms.sh writes from it.
#
# Usage: [BUILD_HOST=<ssh host>] [DECOMP_DIR=<folder under ~, default sm64-decomp>] tools/build_decomp.sh [roms/sm64.us.z64]
set -euo pipefail
cd "$(dirname "$0")/.."
rom="${1:-roms/sm64.us.z64}"
sha1="9bef1128717f958171a4afac3ed78ee2bb4e86ce"
[[ "$(sha1sum "$rom" | cut -d' ' -f1)" == "$sha1" ]] || { echo "$rom isn't Super Mario 64 (USA) as a .z64" >&2; exit 1; }
commit="$(git ls-tree HEAD lib/sm64 | awk '{print $3}')"
image="sm64-decomp:$(git hash-object tools/docker/Dockerfile.decomp | cut -c1-12)"
work="${DECOMP_DIR:-sm64-decomp}/${commit:0:12}"

if [[ -n "${BUILD_HOST:-}" ]]; then
    run() { ssh "$BUILD_HOST" "cd && $1"; }
    put() { scp -q "$1" "$BUILD_HOST:$2"; }
    get() { scp -q "$BUILD_HOST:$1" "$2"; }
else
    run() { (cd ~ && bash -c "$1"); }
    put() { cp "$1" ~/"$2"; }
    get() { cp ~/"$1" "$2"; }
fi

run "docker image inspect $image > /dev/null 2>&1 || docker build -q -t $image - > /dev/null" < tools/docker/Dockerfile.decomp
run "[ -d $work/.git ] || git clone -q https://github.com/n64decomp/sm64 $work; git -C $work checkout -q $commit"
put "$rom" "$work/baserom.us.z64"
run "docker run --rm --name sm64-decomp-${commit:0:12} -u \$(id -u):\$(id -g) -v \$HOME/$work:/sm64 -w /sm64 $image bash -c '
    set -e
    make VERSION=us -j\$(nproc) > build.log 2>&1 || { tail -30 build.log; exit 1; }
    cmd=\$(make VERSION=us -n -W build/us/sm64.ld build/us/sm64.us.elf | grep \"^mips-linux-gnu-ld -L\")
    sed \"/\\/DISCARD\\/ :/i\\    .mdebug 0 : { *(.mdebug) }\" build/us/sm64.ld > build/us/sm64.mdebug.ld
    cmd=\${cmd/-T build\\/us\\/sm64.ld/-T build/us/sm64.mdebug.ld}
    cmd=\${cmd/-Map build\\/us\\/sm64.us.map/-Map build/us/sm64.us.mdebug.elf.map}
    cmd=\${cmd/-o build\\/us\\/sm64.us.elf/-o build/us/sm64.us.mdebug.elf}
    eval \"\$cmd\"
    for s in .main .engine .goddard; do
        mips-linux-gnu-objcopy -O binary --only-section=\$s build/us/sm64.us.elf a.bin
        mips-linux-gnu-objcopy -O binary --only-section=\$s build/us/sm64.us.mdebug.elf b.bin
        cmp -s a.bin b.bin || { echo \"\$s differs in the relinked ELF\"; exit 1; }
    done
    rm a.bin b.bin'"

mkdir -p build/decomp
for f in sm64.us.elf sm64.us.mdebug.elf sm64.us.mdebug.elf.map; do get "$work/build/us/$f" build/decomp/; done
echo "build/decomp/: the decomp's ELF at ${commit:0:12}, matching the ROM; next, tools/gen_syms.sh"
