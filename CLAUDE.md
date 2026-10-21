# CLAUDE.md

A static recompilation of the N64 *Super Mario 64* (USA 1.0) with N64Recomp, N64ModernRuntime, RecompFrontend and
RT64, from the n64decomp/sm64 symbols. Almost all of it is written by AI agents, so this file is what the next one
needs: the rules, where things are, and the facts about the game the code can't say on its own.

## What lives where

This repo is the game: its recompilation, the patches over it, the desktop build, and later its web page.

- `syms/`: the symbols N64Recomp reads, generated (never edited by hand) by `tools/build_decomp.sh` then
  `tools/gen_syms.sh` (`tools/gen_syms.py` says how).
- `us.toml`: N64Recomp's configuration; `tools/recompile.sh` writes `RecompiledFuncs/` with it.
- `patches/` and `patches.toml`: MIPS C against `lib/sm64`'s headers, recompiled into PatchesLib. Its `ui_funcs.h`,
  `patch_helpers.h` and `recompui_event_structs.h` are RecompFrontend's, which includes the first from the game's
  `patches/`.
- `src/`: RecompFrontend's main (`src/main/main.cpp`, with the GameEntry), the natives the patches call
  (`src/game/recomp_api.cpp`) and the settings.
- `lib/`: `sm64` (the decomp, for the symbols and the patches' headers), upstream `rt64`, `N64ModernRuntime` and
  `RecompFrontend` at the commits N64Recomp-browser's web forks are built on, and `N64Recomp-browser` at a release tag.
  The web support every recomp's page shares is developed and documented there; `tools/build_n64recomp.sh` and
  `CMakeLists.txt` apply its `patches/` to the other three.
- Comments beside the code, for how and why: each file's header. Read the header of the file you change and update
  it in the same commit.

## Rules

- No ROMs, ROM-derived assets or extracted game data in git, CI or PRs (`tools/ci/check_repo.py` checks). Nothing
  from the decomp's `assets/` or its build is committed but `syms/`. The launcher's art is text only (`icons/`): no
  logo, character or picture from the game.
- The decomp is built only where the ROM is, never in CI.
- The repo is private. Until it goes public nothing public names or links it, CI is Linux only, and there are no
  releases or Pages.
- The scope is the game as it plays plus what a web page needs (RT64 interpolation to the display's rate, resolution
  Auto): no widescreen, camera, cheats or other enhancements. USA 1.0 only.
- Write for a stranger, in the present tense: nothing about a contributor's machine, servers, paths or accounts, and
  nothing about where anyone keeps ROMs.
- GPL-3.0, as the recompilations it follows.

## How it fits together

- **The code.** Three code segments, all stored raw in the ROM (MIO0 is only used for data), each a static section
  at a fixed address:
  - `main`: ROM 0x1000, VRAM 0x80246000, 0xF4580 bytes. The boot code copies it; the runtime maps it at start
    (it registers what lies in ROM 0x1000 plus 1 MB).
  - `engine`: ROM 0xF5580, VRAM 0x80378800, 0x13490 bytes, DMA'd in once by `load_engine_code_segment`.
  - `goddard` (the title screen's Mario head, the file and star select menus): ROM 0x21F4C0, VRAM 0x8016F000,
    0x4A9E0 bytes, loaded again and again by the level scripts' FIXED_LOAD.
  Every DMA the game makes goes through `dma_read` (src/game/memory.c), so `patches/overlays.c` patches it to tell the
  runtime which code it just brought in.
- **No TLB.** The runtime has none. The game's one real mapping is goddard's dynlists (the Mario head's), a
  segment linked at 0x04000000 that `load_dynlist` DMAs into a buffer and maps there: `patches/dynlist.c`
  relocates the buffer instead, which is exact for US 1.0 (its header says how that was checked). The boot's
  other osMapTLB is a no-op (`src/game/recomp_api.cpp`).
- **Busy waits.** ultramodern runs one game thread at a time and preempts none, so a loop that waits for another
  game thread has to yield (`recomp_yield`). `wait_for_audio_frames`, where the game waits for the sound thread
  while it resets the audio, is the one met so far (`patches/audio.c`).
- **Stubs.** `us.toml` stubs four functions whose code doesn't recompile or calls what the runtime lacks: debug and
  never-called code, each with its reason there.
- **The symbols.** The decomp's US build is IDO 5.3 at `-g`, so it is unoptimised. IDO writes no symbol for a static
  function, so the ELF is relinked keeping `.mdebug` and N64Recomp reads them from there (`use_mdebug`). Its `.mdebug`
  reader leaves those sizes 0 and the decomp's assembly gives its functions no size: `gen_syms.py` fills both in.
- **The entry point** is the decomp's `entry_point` at 0x80246000. `us.toml` ignores the labels inside libultra's
  exception handler, which use cop0 registers.
- **Microcodes.** Graphics: f3d_old, Fast3D 2.0D (`rsp/fast3d.s`), which RT64 knows by its hash as F3D_SM64 and
  draws as F3D, as it does Pilotwings 64's and Wave Race 64's. Audio: aspMain, not recompiled yet: the program runs
  audio tasks as a no-op, so the game is silent.
- **The game's own pacing**: 30 frames a second, two VIs a frame, triple-buffered. It yields its graphics task for
  audio (osSpTaskYield), which the runtime ignores.
