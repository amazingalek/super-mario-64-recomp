# The web page

The game plays in a browser: built with Emscripten on N64Recomp-browser's `frontend/` into `SuperMario64Recompiled.html`
(the launcher, the recomp's menus, the sound through N64Recomp-browser's `audio_output.h`, the keyboard and gamepads,
the ROM from Load ROM, and the saves kept in IndexedDB; `lib/N64Recomp-browser/frontend/CMakeLists.txt`'s header says
what each part does), drawn through WebGPU. The page is built from your ROM like the desktop program, so it is never
built in CI and never published with the game in it.

What is the game's own stays here: the `EMSCRIPTEN` branches of `CMakeLists.txt` and `src/main/main.cpp` that make the
game a page, `tools/build_web.sh`, the prebuilt shader list (`src/rt64_shaders.txt`, recorded by
`tools/record_shaders.sh` and `tools/shader_plan.json`; the list's header says how to record it again), the play plans
(`tools/play_plan.json`, `tools/play_plan_extended.json`) and the survey that drives them all
(`patches/shader_survey.c`, `tools/shader_survey.js`). The rest is N64Recomp-browser's: `docs/porting.md` (WebGPU and
the shaders, prebuilt shader lists, Emscripten), `docs/playing.md` (playing and measuring a page), `docs/forks.md` and
`docs/findings.md`. While this repo is private every play of the page is a line in `plays.pending.jsonl` here, in
N64Recomp-browser's `plays.jsonl` form (game `sm64`), to move there when the page is public.

## Build it

On Linux with Docker, the ROM at `roms/sm64.us.z64`:

```bash
tools/build_web.sh                  # [cpus] [cmake arguments]
```

It builds `build/web/SuperMario64Recompiled.html` with its `.js`, `.wasm` (about 12 MB) and `.data` (about 9 MB:
RecompFrontend's fonts and icons for the menus and the controller mappings, nothing of the game) in
`tools/docker/Dockerfile.web`, the image every N64Recomp-browser page is built in, with ccache in `build/ccache-web`, so
a rebuild after a patch takes about a minute. The script's header says each step. What differs from the desktop build:

- `CMakeLists.txt`'s `EMSCRIPTEN` branches: N64Recomp-browser's `build_speed.cmake` and `patches/apply.cmake` over
  `lib/rt64`, `lib/N64ModernRuntime` and `lib/RecompFrontend`, the page (`n64recomp_browser_frontend_page`), no mods, no
  nfd and no haptics. The patches library (`patches/`, built for MIPS) is recompiled by the native N64Recomp that
  `tools/recompile.sh` builds, not the runtime's, which Emscripten would build for the browser.
- `src/main/main.cpp`: the sound goes through `n64recomp_browser::audio` (`audio_output.h`), and the window is the
  page's canvas, sized by the page.
- RT64 builds its pipelines from `src/rt64_shaders.txt` (`RT64_PREBUILT_SHADER_LIST`), in sections (The shader list).

The settings are the desktop program's (README, Modern enhancements) with the same defaults: Framerate Display,
Resolution Auto at the canvas's size, Aspect Ratio Expand to the canvas's shape. The enhancements below say what each
costs the page.

Serve `build/web` over HTTP from localhost (any static server; the page brings its own `coi_serviceworker.js` for the
headers threads need) and open `SuperMario64Recompiled.html` in Chrome or Firefox with WebGPU, then Load ROM and Start
Game. The keyboard is the desktop's (CLAUDE.md, Input): WASD the stick, Space A, Left Shift B, Enter Start, the arrows
the C buttons.

## The survey

A play plan or a shader recording needs Mario in each course without walking the castle to its painting.
`patches/shader_survey.c` is a struct in RDRAM with two magic words, which `tools/shader_survey.js` finds from the page;
the script asks for a level, an area and a warp node, and the game warps Mario there as a painting or a door does, a
course to its act select. It also says which level plays, whether Mario is in control, and the game's clock
(`gGlobalTimer`, 30 a second), which the plans hold their waits on (`["clock", "clock()", 30]`), keeps Mario's health
full, says whether the file select shows, and counts the game's own frame rate from a `mark()` to a `rate()`. Its header
says each field. It does nothing unless a script asks.

## The plans

`tools/play_plan.json` (about 2 minutes 40) boots, waits for the prebuilt shaders, shoots the title and the file select,
starts file A, passes its intro (Peach's letter, Lakitu) with A, walks and jumps in the castle grounds, then warps into
Bob-omb Battlefield, takes its act select and plays about a minute of the game's time from where it starts Mario:
running out and back, jumping, a long jump, turning the camera both ways. `tools/play_plan_extended.json` (about 4
minutes 40) goes on to Whomp's Fortress, Jolly Roger Bay and Cool, Cool Mountain, about 30 s of the game's time in each.
Each ends with the game's own frame rate over its courses (`rate()`). Play them with N64Recomp-browser's
`tools/play_page.mjs` (its `docs/playing.md`).

## The shader list

`src/rt64_shaders.txt` holds 145 shaders, which RT64 builds as 112 pipelines, in 18 sections of 290 lines (a shader is
in each section that draws it): the boot's (the launcher, the logo, the title's Mario head, the file select), the castle
grounds', the castle's and one for each of the 15 courses. It was recorded in headless Chrome at 1256x645 by
`tools/record_shaders.sh`, a page load a section; its header says what each section played and what it leaves out. The
plans' plays then missed three more, added by hand at the end of the boot's section: one in Bob-omb Battlefield in
Chrome, and two of the title's and the file select's text in both browsers once Hi-res Font set the text tiles to clamp,
which changes their sampler flags. With them neither plan misses a shader in either browser. Chrome builds the whole list behind the launcher in about 5 s,
so the game never waits. Firefox builds none there: it waits 2 s for the boot's section before the game's first frame,
1.4 s for the castle grounds' as file A starts, and 0.3-0.4 s for a course's as it starts.

## Frame rate

The game makes 30 frames a second, two VIs a frame (CLAUDE.md, The game's own pacing), and RT64 interpolates to the
display's rate (Framerate Display). On the page, uncapped in headless Chrome on a desktop GPU at 1256x645, an idle host,
the regular plan presented 347.7 a second, held by the uncapped rate (360), and the game made 30.0 frames a second over
its minute in Bob-omb Battlefield; in Firefox, paced to its 60 animation frames a second, 60.6 presents and 30.0 frames.
Over the extended plan's courses, their loads included, the game made 29.8 in Firefox. No WebGPU error and no exception
in either browser.

## The enhancements

The page keeps every enhancement at the desktop's default (README, Modern enhancements): none costs it a frame. Each was
measured at Original against the defaults, the regular plan played in turn in headless Chrome on a desktop GPU at the
stand-in's 3840x2000 canvas, uncapped, an idle host (`plays.pending.jsonl`, sets `sm64-opts-<option>`), and the game
made 30.0 frames a second in every play, with no shader missed and no WebGPU error. Uncapped, every play's median was
337-359 presents a second, the ceiling, so what an option costs shows in how busy the GPU was (nvidia-smi's medians),
the defaults first:

- Aspect Ratio and HUD Placement (widescreen): 345.0 presents and the GPU 80% busy, against 359.4 and 61% at Original.
  That is the canvas's width drawn: at 4:3 a 3840x2000 canvas draws 2667 columns of it and leaves the rest black. On the
  page widescreen follows the canvas's shape, as Expand does a window's.
- Input Lag: 343.0 and 79%, against 344.5 and 81%.
- Level of Detail: 336.9 and 79%, against 338.4 and 79%.
- Copy Frames to RAM: 343.5 and 80%, against 342.3 and 81%. The game never reads a frame back, so Off (the default)
  copies none.
- MS Anti-Aliasing: 343.3 and 80%, against 345.5 and 79% with None.

Hi-res Font (On by default) makes its textures on the page as on the desktop, into the app folder, which the page keeps
in the browser's IndexedDB: a first visit upscales the text as it meets it (about 90 textures over the regular plan), a
later one loads them. The regular plan played with it On, in Chrome and Firefox at 3840x2000, makes them with no WebGPU
error and no exception.

Framerate and Resolution weren't measured: every page interpolates to the display's rate and draws at the canvas's size
(N64Recomp-browser's CLAUDE.md), which the defaults, Display and Auto, are.

## For N64Recomp-browser

When the repo is public, these move to N64Recomp-browser: the page's part of `docs/game-forks.md`, the Games table's
row, and `plays.pending.jsonl`'s lines into `plays.jsonl`.

The part for `docs/game-forks.md`, after the last game's:

> `amazingalek/super-mario-64-recomp` is this project's own recomp of Super Mario 64 (USA 1.0), from the n64decomp/sm64
> symbols, with RecompFrontend's launcher and menus, on upstream's rt64, N64ModernRuntime and RecompFrontend patched by
> the N64Recomp-browser tag its `lib/N64Recomp-browser` pins, with `frontend/build_speed.cmake`. Its patches (MIPS C
> against the decomp's headers) tag every matrix for RT64's interpolation, widen the view, the HUD and the sky to the
> window's shape, and keep a few modern options, each with an Original value; the page keeps them all at the desktop's
> defaults, which cost it nothing measured. Render to RAM is off: the game never reads a frame it drew. Its list is 143
> shaders: the boot (the launcher, the logo, the title's Mario head, the file select, 19) and 17 sections (the castle
> grounds, the castle, the 15 courses), recorded by `tools/shader_plan.json`, which warps Mario into each course from
> the castle grounds through the survey in `patches/shader_survey.c`. IDO narrows a char or short argument in the
> function that takes it, so a patch replacing a game function whose caller passes a wider value takes it as a word (its
> CLAUDE.md, Narrow arguments). Its docs/web.md and the headers of `tools/build_web.sh`, `tools/shader_survey.js` and
> `src/rt64_shaders.txt` have the rest.

In The games' own frame rates, Super Mario 64 joins the cartridge's: 30, two VIs a frame.

The Games table's row, its page where `tools/publish_page.sh` would put it. The cells are the stand-in's (`--uncapped
--size 3840x2000 --play 25`, the title's Mario head) on this page built on v0.86.0, until a published page is played:

```
| Super Mario 64 | this project's own recomp | [super-mario-64-recomp](https://github.com/amazingalek/super-mario-64-recomp) | [play](https://amazingalek.github.io/super-mario-64-recomp/) | 356* | 352* | 2026-10-22, v0.86.0 |
```
