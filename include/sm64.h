#ifndef __SM64_H__
#define __SM64_H__

#include <string_view>

namespace sm64 {
    inline constexpr std::u8string_view program_id = u8"SuperMario64Recompiled";
    inline constexpr std::string_view program_name = "Super Mario 64: Recompiled";

    // The game's code sections (register_overlays.cpp) and the patches over it (register_patches.cpp).
    void register_overlays();
    void register_patches();

    // The settings (config.cpp): RecompFrontend's tabs as they come, with nothing of the game's own yet.
    void init_config();
}

#endif
