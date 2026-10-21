# Super Mario 64: Recompiled

> **Made with AI.** Almost all of the code, documentation and tests in this repo were written by AI agents
> (Claude), directed and play-tested by a person. None of it is offered to the upstream projects it builds on, which
> don't take AI-written code.

A native PC port of the N64 *Super Mario 64* (USA), made by static recompilation
([N64Recomp](https://github.com/N64Recomp/N64Recomp) + [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
+ [RT64](https://github.com/rt64/rt64), with [RecompFrontend](https://github.com/N64Recomp/RecompFrontend)'s launcher
and menus): the game's own MIPS code translated to C and run natively, with RT64 drawing it. The symbols come from the
[n64decomp/sm64](https://github.com/n64decomp/sm64) decompilation. It is not an emulator.

**This repo is private for now**, and it holds no ROM, no asset from the game and no data extracted from it: you
supply your own ROM, and nothing in git or CI is made from one except the symbol names and addresses in `syms/`.

**Status:** the desktop build boots through the launcher to the title screen. No sound yet.

## Building

You need Super Mario 64 (USA), SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`, as a big-endian `.z64`, at
`roms/sm64.us.z64`. On Windows: LLVM's clang-cl, CMake 3.31 and Ninja, and a clang with a MIPS backend plus ld.lld
for `patches/` (`tools/env.sh` sets the paths one machine uses; adjust it to yours). On Linux:
`clang lld libsdl2-dev libgtk-3-dev libfreetype-dev`, CMake 3.31 and Ninja.

From the repo's root in a bash shell:

```sh
git submodule update --init --recursive
bash tools/recompile.sh -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl   # the game's code as C
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl \
      -DPATCHES_C_COMPILER=<MIPS clang> -DPATCHES_LD=<ld.lld>
ninja -C build SuperMario64Recompiled
```

`tools/recompile.sh` builds N64Recomp from the submodule the first time, then translates the game. Run
`build/SuperMario64Recompiled.exe` from the repo's root, pick the ROM with Load ROM, then Start Game.

The symbols in `syms/` are made from the decomp's own build of the game (`tools/build_decomp.sh`, which needs the ROM
and Docker, then `tools/gen_syms.sh`). They only change when `lib/sm64` moves.

## License

GPL-3.0 (`COPYING`), as the recompilations it follows. The decomp in `lib/sm64` is its own project, under CC0.
