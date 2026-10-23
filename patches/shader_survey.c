// The survey: lets a script warp Mario into any level by itself and keep his health up there, for the page's shader
// list (src/rt64_shaders.txt, recorded by tools/shader_plan.json) and its play plans (tools/play_plan.json), without
// anyone walking the castle to a painting. It does nothing unless a script asks.
//
// The script (tools/shader_survey.js) finds this struct in RDRAM by its two magic words. It writes a level (LEVEL_*),
// an area and a warp node into `level`, `area` and `node`, then `request`. display_and_vsync (patches/enhancements.c)
// calls shader_survey_tick once a frame on the game's thread, and the first frame Mario is in a level that plays
// normally, played and not the title's demo, it warps there as a painting or a door does: initiate_warp, then the fade
// out of fade_into_special_warp. A course's warp ends at its act select, as a painting's does, and a request made at
// the title or the file select waits until Mario is in the castle grounds. `warps` counts the requests taken.
// `invincible` keeps Mario's health full and takes away any damage he is about to take, so a plan's minute in a course
// doesn't end at a death. Every frame the struct also carries the level and area playing, the play mode (sCurrPlayMode,
// 0 normal), Mario's action (0 with no Mario) and the game's frame count (gGlobalTimer, 30 a second of the game's own
// time), which the plans hold their waits on.
//
// For tools/play_plan_full.json, which plays the whole game: `star`, an index plus 1 (an act's star is its act's index,
// 0-5), puts a star of the course playing (bhvStar with that index in its parameters, as a course's own script places
// one) where Mario stands, the first frame he is in a course that plays normally, so he collects it as a player does:
// its dance, the course's end and the save. Most acts' stars only appear once Mario has done what the act asks.

#include "patches.h"
#include "sm64.h"
#include "course_table.h"
#include "model_ids.h"
#include "object_fields.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/level_update.h"
#include "game/object_helpers.h"

void initiate_warp(s16 destLevel, s16 destArea, s16 destWarpNode, s32 arg3);

// level_update.c's, which its header leaves out.
#define PLAY_MODE_NORMAL 0

// bhvStar, by its address in the behaviors' segment (0x13), which spawn_object resolves.
#define BHV_STAR ((const BehaviorScript *) 0x13003E3C)

struct ShaderSurvey {
    u32 magic[2];
    volatile s32 request;
    volatile s32 level;
    volatile s32 area;
    volatile s32 node;
    volatile u32 warps;
    volatile s32 invincible;
    volatile s32 curr_level;
    volatile s32 curr_area;
    volatile s32 play_mode;
    volatile u32 action;
    volatile u32 frames;
    volatile s32 star;
};

struct ShaderSurvey shader_survey = { { 0x534D3634, 0x53525659 }, 0, 0, 0, 0, 0, 0, -1, -1, -1, 0, 0, 0 }; // "SM64" "SRVY"

void shader_survey_tick(void) {
    s32 in_level = gCurrentArea != NULL && gMarioState->marioObj != NULL;

    // A demo's level plays normally too, but a warp from it keeps the demo's input and its "PRESS START".
    if (shader_survey.request && in_level && sCurrPlayMode == PLAY_MODE_NORMAL && gCurrDemoInput == NULL) {
        shader_survey.request = 0;
        initiate_warp(shader_survey.level, shader_survey.area, shader_survey.node, 0);
        fade_into_special_warp(0, 0);
        shader_survey.warps++;
    }

    if (shader_survey.star && in_level && sCurrPlayMode == PLAY_MODE_NORMAL && gCurrCourseNum != COURSE_NONE) {
        struct Object *star = spawn_object(gMarioState->marioObj, MODEL_STAR, BHV_STAR);
        star->oBhvParams = (shader_survey.star - 1) << 24;
        shader_survey.star = 0;
    }

    if (shader_survey.invincible && in_level) {
        gMarioState->health = 0x880;
        gMarioState->hurtCounter = 0;
    }

    shader_survey.curr_level = gCurrLevelNum;
    shader_survey.curr_area = gCurrentArea != NULL ? gCurrentArea->index : -1;
    shader_survey.play_mode = sCurrPlayMode;
    shader_survey.action = in_level ? gMarioState->action : 0;
    shader_survey.frames = gGlobalTimer;
}
