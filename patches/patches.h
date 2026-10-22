#ifndef __PATCHES_H__
#define __PATCHES_H__

// What every patch includes: the sections N64Recomp reads a patch's role from, the natives (syms.ld) and the decomp's
// headers.

#define RECOMP_EXPORT __attribute__((section(".recomp_export")))
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH __attribute__((section(".recomp_force_patch")))
#define RECOMP_DECLARE_EVENT(func)                                                          \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"") \
        __attribute__((noinline, weak, used, section(".recomp_event"))) void func {         \
    }                                                                                       \
    _Pragma("GCC diagnostic pop")

// libultra functions the runtime reimplements are called by their _recomp names (syms.ld).
#define osRecvMesg osRecvMesg_recomp
#define osPiStartDma osPiStartDma_recomp
#define osInvalDCache osInvalDCache_recomp
#define osViSwapBuffer osViSwapBuffer_recomp

#include <ultra64.h>
#include "macros.h"
#include "types.h"

// The game's natives (src/game/recomp_api.cpp).
void recomp_load_overlays(u32 rom, void* ram, u32 size);
void recomp_yield(void);
f32 recomp_get_aspect_ratio(f32 original);
f32 recomp_get_hud_aspect_ratio(f32 original);
s32 recomp_get_reduced_input_lag(void);
s32 recomp_get_lod_divisor(void);

// shader_survey.c, called once a frame from display_and_vsync.
void shader_survey_tick(void);

// widescreen.c, for the other patches.
f32 widescreen_aspect(void);
s32 widescreen_sky_columns(f32 scrollX, s32 *firstCol);

#endif
