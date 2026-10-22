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

**Status:** the desktop build plays: sound (the audio microcode recompiled), saves (EEPROM, kept across restarts) and
keyboard and gamepad input, drawn at the display's rate (the game runs at 30 and RT64 interpolates the frames between,
with nothing smeared at camera cuts, warps or spawns). Bob-omb Battlefield has been played from the file select
through King Bob-omb's star and back to the castle. No web page yet.

## Modern enhancements

Every one of these is an option on the settings menu's Graphics tab, and every option has an Original value that runs
the game as it shipped.

- **High resolution**: renders at the window's size, up to 4K and beyond, instead of 320×240, with 2× MSAA. The 3D,
  the fades and the wipes are sharp at any size. The HUD's numbers and icons, the dialog font and the menus' letters
  stay the game's own pixel art, so at 4K each of their texels is a block of about 9×9 pixels: they are pictures of
  16×16 texels or fewer, drawn texel by texel, with no finer detail to recover, and a smoother version would be new
  art.
- **Smooth motion at the display's rate**: RT64 interpolates between the game's frames up to the monitor's refresh
  rate, 60 on a 60 Hz screen and more on a faster one, or at a rate set by hand. The game logic stays at 30, as on the
  console: Mario, the camera and every object move 30 times a second, and only what is drawn between is new.
- **Lower input lag**: the game shows each frame at the first screen refresh after it has drawn it, instead of a frame
  later, so what you press shows on screen one frame (33 ms) sooner.
- **Widescreen**: the view widens to the window's aspect ratio (the same height of the world, more of it at the
  sides), and the game keeps up at the sides: objects there are drawn instead of culled as off screen, the sky has
  enough columns, the HUD's lives, stars, timer and camera icon move out to the window's edges, the fades, the pause
  screen's shade and the cannon's black cover the whole window, the star and circle wipes start and end beyond its
  corners, and the title screen's tiled backdrop reaches its edges. The dialogs, the menus and the coin counter stay
  centred, as the game centres them. The file select keeps its 4:3 box.
- **Model detail**: models keep their detailed versions four times as far out. Mario switches to a plainer model at
  600 and 1600 units while he moves, which at the camera's usual distance means the middle one: with this, it is the
  full one.
- **No frame copies to RAM**: each frame stays on the GPU instead of also being drawn again at 320×240 and copied back
  into the console's memory, which the game never reads.

Settings are saved as JSON in `%LOCALAPPDATA%\SuperMario64Recompiled\` (`~/.config/SuperMario64Recompiled/` on
Linux), or in the working directory when it holds a `portable.txt`, so they can also be edited by hand while the game
is closed. For the game exactly as it shipped, set every Original value:

| Enhancement | Option | Key | Default | Original |
|---|---|---|---|---|
| Smooth motion | Framerate | `graphics.json` `rr_option` | `"Display"` (the monitor's refresh rate; `"Manual"` sets one) | `"Original"` (30 fps) |
| Lower input lag | Input Lag | `graphics.json` `input_lag` | `"Reduced"` | `"Original"` |
| Widescreen | Aspect Ratio | `graphics.json` `ar_option` | `"Expand"` (the window's) | `"Original"` (4:3) |
| Widescreen HUD | HUD Placement | `graphics.json` `hr_option` | `"Clamp16x9"` (out to 16:9's edges; `"Expand"`: the window's) | `"Original"` (4:3's edges) |
| Rendering resolution | Resolution | `graphics.json` `res_option` | `"Auto"` (the window's size) | `"Original"` (320×240) |
| Anti-aliasing | MS Anti-Aliasing | `graphics.json` `msaa_option` | `"MSAA2X"` | `"None"` |
| Model detail | Level of Detail | `graphics.json` `level_of_detail` | `"High"` (4× the distances; `"Highest"`: always the most detailed) | `"Original"` |
| No frame copies to RAM | Copy Frames to RAM | `graphics.json` `copy_frames_to_ram` | `"Off"` (takes effect on the next start) | `"Original"` (every frame copied) |

Left out, because they would change how the game plays:

- **60 fps game logic.** The game's movement, physics, timers and animations are all counted in its 30 frames a
  second; running them at 60 is a port of every one of them, not an option.
- **Draw distance.** The game hides an object past its own drawing distance (4000 units for most), and an object's
  animation only advances while it is drawn, so drawing objects farther would change when the far ones move, and some
  of their behaviours wait on their animations. Level geometry already draws to the camera's far plane.

Not options, because they don't change the game: the keyboard and gamepad layouts (Controls), which are RecompFrontend's
defaults: WASD or the left stick for the stick, the arrows or the right stick for the C buttons.

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
