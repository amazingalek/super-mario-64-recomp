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
`docs/findings.md`. The page plays at [amazingalek.github.io/super-mario-64-recomp](https://amazingalek.github.io/super-mario-64-recomp/),
published by N64Recomp-browser's `tools/publish_pages.sh`, and every play of it is a line in N64Recomp-browser's
`plays.jsonl` (game `sm64`).

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

For what a player meets past a course's start, `tools/shader_survey.js` also writes the game's memory from the page:
every course's stars and the save's doors and caps, the act an act select takes, Mario's position and lives, Mario put
on a star, a key or the grand star, and a Bowser beaten (his death set, Mario beside him). Those are for recording and
finding only: the play plans reach their levels as a player does. Only the warps a door or a painting makes are safe:
a warp to a death exit in the area Mario is in, the castle's Bowser in the Dark World exit from Peach's slide, or the
courtyard's Big Boo's Haunt exit from the castle grounds crashes the game, on the desktop too, and a warp to a castle
door's own node (the castle from its front door, the courtyard from its castle door) leaves Mario in a room the game
doesn't draw around him, as it is on the desktop, until he walks into one it does.

## The plans

`tools/play_plan.json` (about 2 minutes 40) boots, waits for the prebuilt shaders, shoots the title and the file select,
starts file A, passes its intro (Peach's letter, Lakitu) with A, walks and jumps in the castle grounds, then warps into
Bob-omb Battlefield, takes its act select and plays about a minute of the game's time from where it starts Mario:
running out and back, jumping, a long jump, turning the camera both ways. `tools/play_plan_extended.json` (about 4
minutes 40) goes on to Whomp's Fortress, Jolly Roger Bay and Cool, Cool Mountain, about 30 s of the game's time in each.
Each ends with the game's own frame rate over its courses (`rate()`). Play them with N64Recomp-browser's
`tools/play_page.mjs` (its `docs/playing.md`).

## The shader list

`src/rt64_shaders.txt` holds 189 shaders, which RT64 builds as 141 pipelines, in 34 sections (a shader is in each
section that draws it): the boot's (the launcher, the logo, the title's Mario head, the file select), the castle
grounds', and one for each place in about the order a player meets it: the castle's floors room by room, the
courtyard, the 15 courses at the start of each of their first six acts and in their other areas, the secret stages,
and the three Bowser stages with their fights, keys, the grand star and the ending, with a star's dance, a death and the
Game Over in the first two courses. It was recorded in headless Chrome at 1256x645 by `tools/record_shaders.sh`, a page
load a section; its header says what each section played, what it leaves out and the 20 shaders added by hand, which
plays drew and the recordings missed. Chrome builds the whole list behind the
launcher in about 7 s, so the game never waits. Firefox builds none there: it waits 2.7-3.7 s for the boot's section
before the game's first frame, 1.5-2.2 s for the castle grounds' as file A starts, and 0.6-0.9 s for a course's as it
starts.

## Every level

Every place in the list was reached and played on the page as its section was recorded, in headless Chrome at
1256x645 on a desktop GPU from a fresh profile. Then every section was played again with shots, uncapped, and the shots
looked at (N64Recomp-browser's `plays.jsonl`, notes `level sweep`): the 15 courses at every act's start and in their other areas
(Dire, Dire Docks' and Wet-Dry World's second through the floors that take Mario there, Tall, Tall Mountain's slide),
the castle's three floors room by room, the courtyard and the grounds, Peach's slide, the three cap stages, Wing Mario
Over the Rainbow, the aquarium, the three Bowser fights (Bowser beaten, his key or the grand star taken, its cutscene,
the "Congratulations" screen, the castle again, the ending), Bob-omb Battlefield's first star (the dance, the save
dialog), a death and the Game Over with its Mario head, the pause menu and the act select; the castle's first floor,
basement and courtyard at 4:3 (960x720) too. Where something looked wrong, the same spot was played on the desktop
build. What it found:

- Nothing of the web's: no WebGPU error, no exception, no trap, the game at 30.0 frames a second. The HUD, the sky, the
  fog, water and transparency draw right, in widescreen and at 4:3. The Game Over's dizzy Mario head draws
  whole at Framerate Display in every shot of a burst of twelve.
- Eleven shaders the recordings missed, each drawn as a black or empty scene for the frames it took to build: Bowser
  fading as he teleports in his Fire Sea fight (six), Mario with the vanish cap (two), the basement's and the second
  floor's rooms as Mario leaves Hazy Maze Cave and Tiny-Huge Island, and Dire, Dire Docks' floor into its second area.
  They are in the list (its header), and the same sections played on the page built with it miss none. In Firefox,
  Bob-omb Battlefield, Bowser in the Dark World's fight and Tick Tock Clock played clean, and the castle's first floor
  missed one more (in the list too), with no WebGPU error and no exception.
- "PRESS START" over Rainbow Ride, the plan's: Mario's falls in its acts used up his lives, the Game Over went back to
  the title, whose demo plays a level, and the warp to the next act, taken from the demo, kept its input and its text.
  The plan gives Mario four lives at each act, and the survey warps only from a level that is played.
- The ending's scenes draw in a smaller window in the middle of the screen: the game's own viewport for them
  (`sEndCutsceneVp`).
- The sound drops only at a level's load, as the game stops its music for it: 1,200-2,700 frames short (30-60 ms) a
  load and none while a level plays, as at the boot in the first plays.
- The warps and writes that reach the levels (The survey) have their own limits, the game's: a warp that no player
  makes can crash it, natively too (a warp to an area that has no warp node, as Dire, Dire Docks' and Wet-Dry World's
  second and Tall, Tall Mountain's third have none), and a warp to a door's own node leaves Mario in a room the game
  doesn't draw.

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
stand-in's 3840x2000 canvas, uncapped, an idle host (N64Recomp-browser's `plays.jsonl`, sets `sm64-opts-<option>`), and the game
made 30.0 frames a second in every play, with no shader missed and no WebGPU error. Uncapped, every play's median was
337-359 presents a second, the ceiling, so what an option costs shows in how busy the GPU was (nvidia-smi's medians),
the defaults first:

- Aspect Ratio and HUD Placement (widescreen): 345.0 presents and the GPU 80% busy, against 359.4 and 61% at Original.
  That is the canvas's width drawn: at 4:3 a 3840x2000 canvas draws 2667 columns of it and leaves the rest black. On the
  page widescreen follows the canvas's shape, as Expand does a window's.
- Input Lag: 343.0 and 79%, against 344.5 and 81%.
- Level of Detail: 336.9 and 79%, against 338.4 and 79%.
- Draw Distance: 333.8 and 86%, then 338.7 and 85%, against 333.1 and 89%, then 338.2 and 85% at Original (two pairs,
  interleaved; the second High play started on a busy host). What it adds is a few objects drawn far away, small on
  the screen.
- Copy Frames to RAM: 343.5 and 80%, against 342.3 and 81%. The game never reads a frame back, so Off (the default)
  copies none.
- MS Anti-Aliasing: 343.3 and 80%, against 345.5 and 79% with None.

Hi-res Font is Original by default, on the page as on the desktop (the game's own pixel art), and On on the Graphics
tab. On, it makes its textures on the page as on the desktop, into the app folder, which the page keeps in the
browser's IndexedDB: a first visit upscales the text as it meets it (about 90 textures over the regular plan), a later
one loads them. The regular plan played with it On, in Chrome and Firefox at 3840x2000, makes them with no WebGPU error
and no exception.

Framerate and Resolution weren't measured: every page interpolates to the display's rate and draws at the canvas's size
(N64Recomp-browser's CLAUDE.md), which the defaults, Display and Auto, are.

