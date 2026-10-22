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
  return s ? 'survey at ' + s.toString(16) : 'no survey struct: a page built without patches/shader_survey.c';
})()
