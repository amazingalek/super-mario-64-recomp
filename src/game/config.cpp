// The settings: RecompFrontend's General, Controls, Graphics, Sound and Mods tabs, with its default bindings, and the
// game's own enhancements on the Graphics tab. Each enhancement has an Original value that plays as the console does
// (README, Modern enhancements). RecompFrontend's own Graphics options carry the rest: Resolution and MS Anti-Aliasing
// (high resolution), Framerate (interpolation to the display's rate), Aspect Ratio and HUD Placement (widescreen,
// patches/widescreen.c). Hi-res Font is src/game/hires_font.cpp, the one enhancement that defaults to Original (the
// game's own pixel art), with On on the menu.

#include <atomic>
#include <filesystem>

#include "recompui/config.h"
#include "recompui/renderer.h"
#include "util/file.h"
#include "sm64.h"

namespace {
    enum class InputLag {
        Reduced,
        Original,
        OptionCount
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> input_lag_options = {
        {InputLag::Reduced, "Reduced", "Reduced"},
        {InputLag::Original, "Original", "Original"},
    };
    const std::string input_lag_option = "input_lag";

    enum class LevelOfDetail {
        Original,
        High,
        Highest,
        OptionCount
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> level_of_detail_options = {
        {LevelOfDetail::Original, "Original", "Original"},
        {LevelOfDetail::High, "High", "High"},
        {LevelOfDetail::Highest, "Highest", "Highest"},
    };
    const std::string level_of_detail_option = "level_of_detail";

    enum class HiresFont {
        On,
        Original,
        OptionCount
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> hires_font_options = {
        {HiresFont::On, "On", "On"},
        {HiresFont::Original, "Original", "Original"},
    };
    const std::string hires_font_option = "hires_font";

    enum class CopyFramesToRam {
        Off,
        Original,
        OptionCount
    };
    const std::vector<recomp::config::ConfigOptionEnumOption> copy_frames_to_ram_options = {
        {CopyFramesToRam::Off, "Off", "Off"},
        {CopyFramesToRam::Original, "Original", "Original"},
    };
    const std::string copy_frames_to_ram_option = "copy_frames_to_ram";

    // Read on the game thread every frame (src/game/recomp_api.cpp).
    std::atomic<bool> reduced_input_lag_setting{ true };
    std::atomic<uint32_t> level_of_detail_setting{ static_cast<uint32_t>(LevelOfDetail::High) };
}

bool sm64::reduced_input_lag() {
    return reduced_input_lag_setting.load(std::memory_order_relaxed);
}

uint32_t sm64::level_of_detail_divisor() {
    // The distance a level-of-detail switch compares is divided by this: High picks each model at 4 times the game's
    // distance, Highest always the nearest one (a distance of 0 is inside every closest range the game draws).
    switch (static_cast<LevelOfDetail>(level_of_detail_setting.load(std::memory_order_relaxed))) {
        case LevelOfDetail::High:
            return 4;
        case LevelOfDetail::Highest:
            return 0;
        default:
            return 1;
    }
}

void sm64::init_config() {
    std::filesystem::path app_dir = recompui::file::get_app_folder_path();
    if (!app_dir.empty()) {
        std::filesystem::create_directories(app_dir);
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = true;
    general_options.has_mouse_sensitivity = true;
    recompui::config::create_general_tab(general_options);
    recompui::config::create_controls_tab();

    recomp::config::Config& graphics = recompui::config::create_graphics_tab();
    graphics.add_enum_option(
        input_lag_option,
        "Input Lag",
        "<b>Reduced:</b> each frame is shown at the first refresh after the game has drawn it, so what you press shows "
        "on screen one frame (33 ms) sooner."
        "<br/>"
        "<b>Original:</b> each frame is shown a frame later, as on the console, which keeps one frame drawn ahead.",
        input_lag_options,
        InputLag::Reduced
    );
    // Called on load too, and while the menu previews a change.
    graphics.add_option_change_callback(input_lag_option,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            reduced_input_lag_setting = static_cast<InputLag>(std::get<uint32_t>(cur_value)) == InputLag::Reduced;
        });

    graphics.add_enum_option(
        level_of_detail_option,
        "Level of Detail",
        "How far away Mario and the other models switch to their less detailed versions."
        "<br/>"
        "<b>High:</b> 4 times as far as the game's own distances. <b>Highest:</b> always the most detailed. "
        "<b>Original:</b> the game's own distances.",
        level_of_detail_options,
        LevelOfDetail::High
    );
    graphics.add_option_change_callback(level_of_detail_option,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            level_of_detail_setting = std::get<uint32_t>(cur_value);
        });

    sm64::init_hires_font();
    graphics.add_enum_option(
        hires_font_option,
        "Hi-res Font",
        "The HUD, the power meter and the letters of the dialogs and menus, smoothed from the game's own pixel art "
        "(each upscaled four times when it is first drawn, and kept in the app folder)."
        "<br/>"
        "<b>On:</b> smooth at any resolution. <b>Original:</b> the game's own texels, as blocks at high resolution.",
        hires_font_options,
        HiresFont::Original
    );
    graphics.add_option_change_callback(hires_font_option,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            sm64::set_hires_font(static_cast<HiresFont>(std::get<uint32_t>(cur_value)) == HiresFont::On);
        });

    // The game never reads a frame back from memory (no screen samples a framebuffer, nothing writes into one), so
    // the copy RT64 makes of every frame into RDRAM, a second draw of it at 320x240, is work nothing uses. Still, it
    // is an option, in case a mod or an unknown path needs it. The render context reads it when it is made, at
    // startup after this loads, so a change takes effect on the next start. Its default is set here first: a first
    // start has no graphics.json, and then loading calls no callback, which would leave RT64's own default (on).
    recompui::renderer::set_render_to_ram(false);
    graphics.add_enum_option(
        copy_frames_to_ram_option,
        "Copy Frames to RAM",
        "Whether every frame is also copied back into the console's memory, as the N64 leaves it there for the game "
        "to read. The copy is a second draw of each frame at 320x240."
        "<br/>"
        "<b>Off:</b> no copy. The game doesn't read its frames back. "
        "<b>Original:</b> every frame is copied, as on the console."
        "<br/>"
        "Takes effect the next time the game is started.",
        copy_frames_to_ram_options,
        CopyFramesToRam::Off
    );
    graphics.add_option_change_callback(copy_frames_to_ram_option,
        [](recomp::config::ConfigValueVariant cur_value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
            recompui::renderer::set_render_to_ram(
                static_cast<CopyFramesToRam>(std::get<uint32_t>(cur_value)) == CopyFramesToRam::Original);
        });

    recompui::config::create_sound_tab();
    recompui::config::create_mods_tab();
    recompui::config::finalize();
}
