#ifndef __SM64_H__
#define __SM64_H__

#include <cstdint>
#include <string_view>

namespace sm64 {
    inline constexpr std::u8string_view program_id = u8"SuperMario64Recompiled";
    inline constexpr std::string_view program_name = "Super Mario 64: Recompiled";

    // The game's code sections (register_overlays.cpp) and the patches over it (register_patches.cpp).
    void register_overlays();
    void register_patches();

    // The settings (config.cpp): RecompFrontend's tabs, with the game's enhancements on the Graphics tab.
    void init_config();

    // The enhancements' settings the natives read (src/game/recomp_api.cpp).
    bool reduced_input_lag();
    uint32_t level_of_detail_divisor();

    // Hi-res Font (hires_font.cpp): set up the generated texture pack in the app folder, then turn it on or off.
    void init_hires_font();
    void set_hires_font(bool on);
}

#endif
