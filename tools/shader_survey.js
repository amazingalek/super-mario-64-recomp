// Evaluated in the page by N64Recomp-browser's tools/record_shaders.mjs (tools/shader_plan.json) and play_page.mjs
// (tools/play_plan.json, tools/play_plan_extended.json): finds patches/shader_survey.c's struct in RDRAM by its magic
// words and offers level(), the level playing (gCurrLevelNum: 1 at the title and the file select, 16 the castle
// grounds, 6 the castle, 9 Bob-omb Battlefield; the decomp's levels/level_defines.h numbers them all); area(), its
// area (-1 between levels and in the act select); untilLevel(level, ms), which resolves true once that level plays,
// false after ms; warp(level, area, node, ms), which warps Mario there as a painting or a door does and resolves true
// once the game has taken the request and is in that level (a course is then at its act select), false after ms;
// inControl(), true once Mario is in a level, the game plays normally and his action is no cutscene's (an intro, a
// dialog, a door, a painting's entry); clock(), the game's frame count (gGlobalTimer, 30 a second of its own time),
// for a plan's ["clock", "clock()", 30]; setInvincible(), which keeps Mario's health full for as long as the page
// runs; fileSelect(), true while the file select shows with its cursor on file A, where it opens; and mark() and
// rate(), which say how many frames a second the game made from the one to the other: its own rate as the page plays.
//
// For tools/shader_plan.json's later sections, which reach what a player meets past a course's start without playing
// there: setStars(mask, flags), every course's stars and the save's flags (file A's in gSaveBuffer: 0x1f makes a
// course's first six acts selectable, 0x10ffff its caps, keys, unlocked doors and the drained moat), so every door and
// act opens; pickAct(n), act n at the act select (sSelectableStarIndex: the selector writes the act's index from it
// each frame); act(), the act playing; marioPos() and tp(x, y, z), Mario's position (gMarioStates); setLives(n), his
// lives (a death exit, which a castle room is reached by, takes one); findObj(behavior) and toObj(behavior, dy), the
// first live object with that behavior (its offset in the behaviors' segment 0x13, found from Mario's own bhvMario,
// 0x2ec0: bhvStar 0x3e3c, bhvBowserKey 0x16e4, bhvGrandStar 0x1714, bhvBowser 0x1850) and Mario put on it, so he takes
// a star or a key as a player does; killBowser(), Bowser's health spent and his death set; nearBowser(home), Mario put
// back home (a tilting arena slides him off) and Bowser brought beside him, Mario turned to him, where that death waits
// for Mario before its dialog and his key (the decomp's bowser_dead_wait_for_mario); die(lives), invincibility off and
// Mario's health gone, with that many lives left (0 for the Game Over). Addresses are the US 1.0 game's
// (syms/sm64.us.datasyms.toml).
(() => {
  const magic = (a) => n64r(a) === 0x534d3634 && n64r(a + 4) === 0x53525659;
  if (!window.__survey) {
    // The patches' memory first (patches/patches.ld), then the rest of RDRAM.
    for (let a = 0x80801000; a < 0x81000000 && !window.__survey; a += 4) if (magic(a)) window.__survey = a;
    for (let a = 0x80000000; a < 0x80801000 && !window.__survey; a += 4) if (magic(a)) window.__survey = a;
  }
  const s = window.__survey;
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  window.level = () => n64r(s + 32) | 0;
  window.area = () => n64r(s + 36) | 0;
  window.clock = () => n64r(s + 48) >>> 0;
  // ACT_GROUP_MASK and ACT_GROUP_CUTSCENE (the decomp's include/sm64.h).
  window.inControl = () => n64r(s + 44) !== 0 && (n64r(s + 40) | 0) === 0 && (n64r(s + 44) & 0x1c0) !== 0x100;
  window.untilLevel = async (level, ms) => {
    const end = Date.now() + ms;
    while (window.level() !== level && Date.now() < end) await sleep(100);
    return window.level() === level;
  };
  window.warp = async (level, area, node, ms) => {
    const warps = n64r(s + 24);
    n64w(s + 12, level);
    n64w(s + 16, area);
    n64w(s + 20, node);
    n64w(s + 8, 1);
    const end = Date.now() + ms;
    while ((n64r(s + 24) === warps || window.level() !== level) && Date.now() < end) await sleep(100);
    return n64r(s + 24) !== warps && window.level() === level;
  };
  // The file select's cursor (the decomp's sCursorPos, two floats in the menu's memory) is set to file A's button as
  // the file select opens on a new boot, and nothing else puts both of those values there.
  const f32 = (a) => new Float32Array(new Uint32Array([n64r(a)]).buffer)[0];
  window.fileSelect = () => f32(0x801a7d1c) === -94 && f32(0x801a7d20) === 46;
  window.mark = () => { window.__mark = [window.clock(), performance.now()]; return 'clock ' + window.__mark[0]; };
  window.rate = () => {
    const [frames, ms] = [window.clock() - window.__mark[0], performance.now() - window.__mark[1]];
    return `${frames} game frames in ${(ms / 1000).toFixed(1)} s: ${(frames * 1000 / ms).toFixed(1)} a second`;
  };
  window.setInvincible = () => { n64w(s + 28, 1); return 'level ' + window.level(); };
  const rb = (a) => (n64r(a & ~3) >>> ((3 - (a & 3)) * 8)) & 0xff;
  // A byte's or a half's write, within its word (n64w writes words).
  const put = (a, v, mask, sh) => { const w = a & ~3; n64w(w, ((n64r(w) & ~(mask << sh)) | ((v & mask) << sh)) >>> 0); };
  const wb = (a, v) => put(a, v, 0xff, (3 - (a & 3)) * 8);
  const wh = (a, v) => put(a, v, 0xffff, (2 - (a & 2)) * 8);
  const f2u = (f) => new Uint32Array(new Float32Array([f]).buffer)[0];
  const SAVE = 0x80207700, MARIO = 0x8033b170, POOL = 0x8033d488, MARIO_OBJ = 0x80361158;
  window.setStars = (mask, flags) => {
    for (let c = 0; c < 25; c++) wb(SAVE + 12 + c, mask);
    n64w(SAVE + 8, flags >>> 0);
    return 'stars ' + mask.toString(16) + ' flags ' + n64r(SAVE + 8).toString(16);
  };
  window.pickAct = (n) => { wb(0x801a81a4, n - 1); return 'act index ' + rb(0x801a81a4); };
  window.act = () => (n64r(0x8033bac8) >>> 16) & 0xffff;
  window.marioPos = () => [0x3c, 0x40, 0x44].map((o) => f32(MARIO + o).toFixed(0)).join(',');
  window.tp = (x, y, z) => {
    n64w(MARIO + 0x3c, f2u(x)); n64w(MARIO + 0x40, f2u(y)); n64w(MARIO + 0x44, f2u(z));
    return window.marioPos();
  };
  // An object: activeFlags the upper half of +0x74, oPosX/Y/Z +0xa0-0xa8, oAction +0x14c, oSubAction +0x150, oTimer
  // +0x154, oHealth +0x184, its behavior +0x20c (240 of them, 0x260 bytes each).
  window.findObj = (behavior) => {
    const b = n64r(n64r(MARIO_OBJ) + 0x20c) - 0x2ec0 + behavior;
    for (let i = 0; i < 240; i++) {
      const o = POOL + i * 0x260;
      if (((n64r(o + 0x74) >>> 16) & 1) && n64r(o + 0x20c) === b) return o;
    }
    return 0;
  };
  window.toObj = (behavior, dy = 0) => {
    const o = window.findObj(behavior);
    return o ? window.tp(f32(o + 0xa0), f32(o + 0xa4) + dy, f32(o + 0xa8)) : false;
  };
  window.killBowser = () => {
    const o = window.findObj(0x1850);
    if (!o) return false;
    n64w(o + 0x184, 0); n64w(o + 0x14c, 4); n64w(o + 0x150, 0); n64w(o + 0x154, 0);
    return 'bowser ' + o.toString(16);
  };
  window.nearBowser = (home) => {
    const o = window.findObj(0x1850);
    if (!o) return false;
    // Mario home (marioPos()'s "x,y,z", if given), Bowser 400 units to his -x, Mario facing -x.
    if (home) window.tp(...home.split(',').map(Number));
    wh(MARIO + 0x2e, 0xc000);
    n64w(o + 0xa0, f2u(f32(MARIO + 0x3c) - 400)); n64w(o + 0xa4, n64r(MARIO + 0x40)); n64w(o + 0xa8, n64r(MARIO + 0x44));
    return 'bowser at ' + [0xa0, 0xa4, 0xa8].map((f) => f32(o + f).toFixed(0)).join(',');
  };
  window.setLives = (n) => { wb(MARIO + 0xad, n); return 'lives ' + rb(MARIO + 0xad); };
  window.die = (lives) => { n64w(s + 28, 0); wh(MARIO + 0xae, 0xff); return window.setLives(lives); };
  return s ? 'survey at ' + s.toString(16) : 'no survey struct: a page built without patches/shader_survey.c';
})()
