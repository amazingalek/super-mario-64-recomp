// The natives the patches call (patches/syms.ld gives each a dummy address that N64Recomp turns into a call by name),
// and the libultra functions the game calls that N64Recomp leaves out and the runtime doesn't have.

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "librecomp/helpers.hpp"
#include "ultramodern/config.hpp"
#include "recompui/recompui.h"
#include "sm64.h"

#include <algorithm>

// recomp_load_overlays(rom, ram, size): the code sections in that stretch of ROM are now at ram (patches/overlays.c).
extern "C" void recomp_load_overlays(uint8_t* rdram, recomp_context* ctx) {
    u32 rom = _arg<0, u32>(rdram, ctx);
    PTR(void) ram = _arg<1, PTR(void)>(rdram, ctx);
    u32 size = _arg<2, u32>(rdram, ctx);

    load_overlays(rom, ram, size);
}

// The runtime has no TLB. The game's one real mapping, goddard's dynlists, is replaced by patches/dynlist.c, which
// leaves only the boot's (src/game/main.c, a mapping it unmaps again at once).
extern "C" void osMapTLB_recomp(uint8_t* rdram, recomp_context* ctx) {
}

extern "C" void yield_self_1ms(uint8_t* rdram);

// recomp_yield(): for a patch that waits on another game thread. ultramodern runs one game thread at a time, so this
// hands the CPU to a ready thread of higher priority, after waiting up to 1 ms for an event (a VI, an RCP task) to
// make one ready.
extern "C" void recomp_yield(uint8_t* rdram, recomp_context* ctx) {
    yield_self_1ms(rdram);
}

// The aspect ratio the game is drawn at (patches/widescreen.c): the window's when Aspect Ratio is Expand, where RT64
// widens the view to it, and the game's own (the argument, 4:3) when it is Original or the window is narrower.
static float window_aspect_ratio(float original) {
    int width, height;
    recompui::get_window_size(width, height);
    if (ultramodern::renderer::get_graphics_config().ar_option != ultramodern::renderer::AspectRatio::Expand ||
        width <= 0 || height <= 0) {
        return original;
    }
    return std::max(static_cast<float>(width) / height, original);
}

// recomp_get_aspect_ratio(original): the aspect ratio of the screen the game draws to.
extern "C" void recomp_get_aspect_ratio(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, window_aspect_ratio(_arg<0, float>(rdram, ctx)));
}

// recomp_get_hud_aspect_ratio(original): the aspect ratio of the area HUD Placement puts the HUD in, the one RT64
// aligns the HUD's rectangles to: the game's own with Original, at most 16:9 with 16:9, the screen with Expand.
extern "C" void recomp_get_hud_aspect_ratio(uint8_t* rdram, recomp_context* ctx) {
    float original = _arg<0, float>(rdram, ctx);
    float aspect = window_aspect_ratio(original);
    switch (ultramodern::renderer::get_graphics_config().hr_option) {
        case ultramodern::renderer::HUDRatioMode::Original:
            aspect = original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            aspect = std::max(std::min(aspect, 16.0f / 9.0f), original);
            break;
        default:
            break;
    }
    _return(ctx, aspect);
}

// recomp_get_reduced_input_lag(): whether Input Lag is Reduced (patches/enhancements.c).
extern "C" void recomp_get_reduced_input_lag(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, sm64::reduced_input_lag() ? 1 : 0);
}

// recomp_get_lod_divisor(): what Level of Detail divides a level-of-detail switch's distance by, 0 for always the
// nearest model (patches/enhancements.c).
extern "C" void recomp_get_lod_divisor(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, static_cast<s32>(sm64::level_of_detail_divisor()));
}
