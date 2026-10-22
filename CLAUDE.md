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
  draws as F3D, as it does Pilotwings 64's and Wave Race 64's. Audio: aspMain, libultra's standard audio microcode,
  recompiled by RSPRecomp from `aspMain.us.toml` (its header says how its layout and handler table were found).
  librecomp's vector unit uses SSE4.1, so `RecompiledFuncs` builds with `-march=nehalem` on x86-64.
- **Jump tables.** N64Recomp finds all of the game's itself (147, each a `switch` in `RecompiledFuncs/`, and
  `recomp.log` names no unresolved `jr`), so `us.toml` lists none by hand.
- **Saves.** EEPROM, 4 Kbit: the decomp's `save_file.h` has `EEPROM_SIZE` 0x200 and `save_file.c` asserts the save
  buffer is exactly that. `main.cpp` declares `SaveType::Eep4k`; the runtime keeps it in
  `saves/sm64.n64.us.1.0.bin`, in the game's own byte order, and writes it a moment after the game saves, so a test
  waits a few seconds before it stops the game.
- **Input.** RecompFrontend's default bindings, nothing of the game's own. Keyboard: WASD the stick, Space A, Left
  Shift B, Q Z, R R, E L, Enter Start, the arrows the C buttons. A gamepad: the left stick, the south button A, the
  west one B, the left trigger Z, the right trigger R, the left bumper L, Start, and the right stick for the C
  buttons (or north, east, the right stick's click and the right bumper for C left, right, up and down).
- **Testing without the speakers.** `main.cpp` asks SDL for WASAPI only when `SDL_AUDIODRIVER` isn't set, so a test
  records the game's sound with SDL's disk driver (`SDL_AUDIODRIVER=disk`, `SDL_DISKAUDIOFILE=<file>`: F32 stereo at
  48 kHz). On Windows that driver runs slow, about 0.92 of real time: it sleeps 5 ms per 5.33 ms buffer, and the
  sleeps overrun. The game makes its sound to the device's clock within 16 samples a frame, so a slow device shows
  up as skipped samples (`queue_samples` drops some above 100 ms queued). To keep real time, point
  `SDL_DISKAUDIOFILE` at a named pipe that the test reads at 48000 frames a second, with the delay off. Because
  SDL 2.30's `SDL_getenv` on Windows returns one shared buffer, `SDL_DISKAUDIODELAY` must hold the same text as the
  file name (its `atoi` is 0). Any other value replaces the file name, and SDL writes to a file named by it as fast
  as it can.
- **Driving it from a test.** A test that plays by posting `WM_KEYDOWN`/`WM_KEYUP` to the game's window (it reaches
  the game whether or not the window has focus) can't hold Left Shift, so B never registers: at every event pump
  SDL2 on Windows releases Left Shift if `GetKeyState(VK_LSHIFT)` says it is up, which is its fix for lost shift
  key-ups, and a posted message never sets that state. Such a test gives B a second key in the checkout's
  `controls.json` (`{"keyboard": {"B": [{"input_type": 1, "input_id": 225}, {"input_type": 1, "input_id": 27}]}}`
  keeps Left Shift and adds X; an input the file leaves out keeps its default) and presses that.
- **The game's own pacing**: 30 frames a second, two VIs a frame, triple-buffered. It yields its graphics task for
  audio (osSpTaskYield), which the runtime ignores.
