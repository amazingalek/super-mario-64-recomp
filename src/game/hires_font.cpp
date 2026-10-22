// Hi-res Font (Graphics): the HUD, the dialogs' and menus' letters and the power meter, upscaled from the game's own
// textures. Each is a little picture (a HUD glyph is 16x16 texels, a dialog letter 16x8) drawn texel by texel, so at 4K
// a texel is a block of about 9x9 pixels. RT64 can replace a texture with a bigger one from a texture pack, keyed by a
// hash of what the texture loads into TMEM, but the textures come from the user's ROM, so no pack can ship in this
// repo. The game makes its own instead, as Rush 2049: Recompiled does for its fonts: as each frame's graphics task is
// built (create_gfx_task_structure, patches/interpolation.c), its display list is walked with the TMEM loads RT64
// makes, and every texture drawn between the game's own text and HUD set-up lists (text_lists below) is hashed the way
// RT64 hashes it. A hash not seen before is upscaled 4x with xBRZ (lib/xbrz) and written to hires_font/ in the app folder, which is registered with
// RT64 as a built-in pack below any texture packs the user enables (runtime-patches/RecompFrontend-0001). Only the
// first frame that draws a new letter shows the original; after that the pack already has it, across restarts too.
//
// The glyphs are hard-edged pixel art with a one-bit alpha (the HUD's, drawn in copy mode with an alpha compare) or one
// of eight levels (the dialog font), not anti-aliased like Rush's, so the upscale is a pixel-art scaler. Against a
// bicubic with a contrast curve, xBRZ keeps the HUD digits' dark outline and colour bands and draws the font's strokes
// and curves as lines, where the bicubic blurred both and left the edges stepped. A texel past the texture's edge is
// the edge texel repeated, so the power meter's two halves, drawn side by side, stay joined. Each new letter adds a
// texture and reloads the pack, once per new letter, the first time a save plays through the dialogs and menus.
//
// Which textures: every texture drawn (a texture rectangle, or a triangle while texturing is on) from a call of one
// of text_lists' begin lists up to a call of its end list. The game brackets all of its 2D text and icons with these:
// the HUD's counters and camera icon (dl_hud_img_begin), the menus' HUD-font titles (dl_rgba16_text_begin), the dialog
// font of the dialogs, the pause screen and the act select (dl_ia_text_begin), the file select's small font
// (dl_menu_ia8_text_begin) and the power meter (dl_power_meter_base up to its health segments' end). The display list is
// the game's: the lists are called by their segmented addresses, as syms/sm64.us.datasyms.toml has them.
//
// RT64 samples a replaced texture with a linear filter, and the game's text tiles wrap (the dialog font's mirror as
// well, which RT64 then draws as a wrap): at a letter's edge the filter reaches past it and blends in the opposite edge,
// a faint line a pixel wide down the side of every letter whose far edge has ink. So while Hi-res Font is on, the walk
// also sets every render tile the text lists set up to clamp, in the game's own lists in RDRAM. A letter is drawn
// texel by texel within its texture, where a clamp reads what a wrap does, so nothing else changes, and RT64's hash is
// the same either way (it samples the smaller of the tile and its mask, the same size for every text tile). The words it
// changed are put back once the option is Original.

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "librecomp/helpers.hpp"
#include "recompui/renderer.h"
#include "util/file.h"
#include "sm64.h"

#include "xxHash/xxh3.h"
#include "shared/rt64_f3d_defines.h"
#include "common/rt64_load_types.h"
#include "common/rt64_tmem_hasher.h"
#include "../lib/rt64/include/rt64_extended_gbi.h"
#include "../lib/xbrz/xbrz.h"

namespace {

// Bump when the upscale changes, so textures made by an older version are thrown away.
const char* const pack_version = "v1";
constexpr int scale = 4;
// Texels of the edge repeated around a texture before it is scaled: xBRZ reads two past a texel.
constexpr int pad = 2;

constexpr uint32_t tmem_bytes = 4096;
constexpr uint32_t rdram_bytes = (uint32_t)recomp::mem_size;
constexpr uint32_t fmt_rgba = 0, fmt_ia = 3, fmt_i = 4;
constexpr uint32_t siz_4b = 0, siz_8b = 1, siz_16b = 2, siz_32b = 3;

// The game's lists that open and close its 2D text and icons, by the segmented address the game calls them at.
struct TextList {
    uint32_t begin, end;
};
constexpr TextList text_lists[] = {
    { 0x02011AC0, 0x02011B28 },  // dl_hud_img_begin, dl_hud_img_end
    { 0x02011B60, 0x02011BC8 },  // dl_rgba16_text_begin, dl_rgba16_text_end
    { 0x02011CC8, 0x02011D50 },  // dl_ia_text_begin, dl_ia_text_end
    { 0x0700BC40, 0x0700BC98 },  // dl_menu_ia8_text_begin, dl_menu_ia8_text_end
    { 0x03029480, 0x030295A0 },  // dl_power_meter_base, dl_power_meter_health_segments_end
};

// Off by default, as the option (Original): its change callback doesn't run on a first launch, before there is a saved config.
std::atomic<bool> enabled{ false };
std::filesystem::path pack_dir;
// Every hash already handled: in the pack, or drawn but not wanted (empty, or a format left alone).
std::unordered_set<uint64_t> seen;
std::vector<uint64_t> in_pack;

// A texture to make once the walk is done: its texels as RGBA8.
struct Pending {
    uint32_t width, height;
    std::vector<uint32_t> argb;
};

// F3D's commands (the game's microcode, Fast3D), where they differ from the RDP's.
constexpr uint8_t g_dl = 0x06, g_tri1 = 0xBF, g_moveword = 0xBC, g_texture = 0xBB, g_setothermode_h = 0xBA,
                  g_enddl = 0xB8;
constexpr uint32_t g_mw_segment = 0x06;

struct Rdp {
    uint8_t* rdram;
    uint32_t segments[16]{};
    struct {
        uint32_t address = 0, siz = 0, width = 0;
    } timg;
    RT64::LoadTile tiles[8]{};
    uint8_t tmem[tmem_bytes]{};
    // Which TMEM bytes hold a known texture: written by a load block. Other loads (tile, TLUT) aren't emulated, so they
    // mark what they may have overwritten as unknown, and draws from there are skipped.
    bool tmem_known[tmem_bytes]{};
    // G_TEXTURE: the tile triangles draw with, and whether they are textured.
    uint32_t texture_tile = 0;
    bool texture_on = false;
    // The high half of the other modes, for the TLUT type, which RT64 hashes with every texture.
    uint32_t othermode_h = 0;
    // Inside the game's text and HUD lists (text_lists).
    bool in_text = false;
    // Commands left to read this frame: a list that isn't one (a bad pointer) ends the walk rather than the game.
    uint32_t budget = 0;
    std::unordered_map<uint64_t, Pending> pending;
    // The text lists' G_SETTILE words set to clamp (see the header), by their RDRAM address, with the word the game had.
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> clamped;

    // RT64's RSP::fromSegmented and maskPhysicalAddress (without gEXSetRDRAMExtended, which the game doesn't use).
    uint32_t phys(uint32_t addr) const {
        return (segments[(addr >> 24) & 0xF] + (addr & 0x00FFFFFF)) & 0x00FFFFFF;
    }
    uint32_t rd32(uint32_t p) const { return *reinterpret_cast<uint32_t*>(rdram + (p & (rdram_bytes - 4))); }
    void wr32(uint32_t p, uint32_t v) { *reinterpret_cast<uint32_t*>(rdram + (p & (rdram_bytes - 4))) = v; }
    uint8_t rd8(uint32_t p) const { return rdram[(p & (rdram_bytes - 1)) ^ 3]; }
    static uint32_t byteswap32(uint32_t v) {
        return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
    }

    // RT64's RDP::loadBlockOperation / loadToTMEMCommon for everything but 32-bit RGBA.
    void load_block(uint32_t t, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t dxt) {
        RT64::LoadTile& tile = tiles[t];
        tile.uls = uls;
        tile.ult = ult;
        tile.lrs = lrs;
        tile.lrt = dxt;
        if (uls > lrs || lrs >= 0x800) {
            return;
        }
        if (tile.siz == siz_32b && tile.fmt == fmt_rgba) {
            std::fill(std::begin(tmem_known), std::end(tmem_known), false);
            return;
        }
        uint32_t bytes_per_row = timg.width << timg.siz >> 1;
        uint32_t src = timg.address + (uls << timg.siz >> 1) + bytes_per_row * ult;
        uint32_t words = ((lrs - uls) >> (4 - tile.siz)) + 1;
        uint32_t dst = (tile.tmem << 3) & (tmem_bytes - 1);
        uint32_t stride = tile.line << 3;
        uint32_t xor_mask = 0, dxt_counter = 0;
        bool aligned = (src & 3) == 0;
        for (uint32_t w = 0; w < words && w < tmem_bytes / 8; w++) {
            if (aligned) {
                uint32_t hi = byteswap32(rd32(src)), lo = byteswap32(rd32(src + 4));
                memcpy(&tmem[dst ^ xor_mask], &hi, 4);
                memcpy(&tmem[dst ^ xor_mask ^ 4], &lo, 4);
                memset(&tmem_known[dst], 1, 8);
            }
            else {
                for (uint32_t i = 0; i < 8; i++) {
                    uint32_t a = ((dst + i) ^ xor_mask) & (tmem_bytes - 1);
                    tmem[a] = rd8(src + i);
                    tmem_known[a] = true;
                }
            }
            dxt_counter += dxt;
            while (dxt_counter >= 0x800) {
                dst = (dst + stride) & (tmem_bytes - 1);
                dxt_counter -= 0x800;
                xor_mask ^= 4;
            }
            src += 8;
            dst = (dst + 8) & (tmem_bytes - 1);
        }
    }

    // RT64's sample size (State's draw call tiles) and hash for a draw with tile `t`.
    void draw(uint32_t t) {
        const RT64::LoadTile& tile = tiles[t & 7];
        if (!in_text || tile.line == 0 || (tile.fmt != fmt_rgba && tile.fmt != fmt_ia && tile.fmt != fmt_i) ||
            tile.siz == siz_32b) {
            return;
        }
        bool clamp_s = tile.masks == 0 || (tile.cms & 2);
        bool clamp_t = tile.maskt == 0 || (tile.cmt & 2);
        int tile_w = clamp_s ? std::max((tile.lrs - tile.uls + 4) / 4, 1) : 0xFFFF;
        int tile_h = clamp_t ? std::max((tile.lrt - tile.ult + 4) / 4, 1) : 0xFFFF;
        int mask_w = tile.masks > 0 ? 1 << tile.masks : 0xFFFF;
        int mask_h = tile.maskt > 0 ? 1 << tile.maskt : 0xFFFF;
        uint16_t width = (uint16_t)std::min(tile_w, mask_w);
        uint16_t height = (uint16_t)std::min(tile_h, mask_h);
        if (width > 256 || height > 256 || RT64::TMEMHasher::requiresRawTMEM(tile, width, height, 0)) {
            return;
        }
        uint32_t row_bytes = tile.line << 3;
        uint32_t start = tile.tmem << 3;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < (uint32_t)((width << tile.siz) + 1) / 2; x++) {
                if (!tmem_known[(start + y * row_bytes + x) & (tmem_bytes - 1)]) {
                    return;
                }
            }
        }
        uint32_t tlut = othermode_h & (3u << 14);  // G_MDSFT_TEXTLUT, as RT64's OtherMode::textLUT
        uint64_t hash = RT64::TMEMHasher::hash(tmem, tile, width, height, tlut, RT64::TMEMHasher::CurrentHashVersion);
        if (seen.insert(hash).second) {
            pending[hash] = { width, height, read_texels(tile, width, height) };
        }
    }

    // The texels as ARGB, read back the way the RDP samples them: odd rows have their 32-bit halves swapped in TMEM.
    std::vector<uint32_t> read_texels(const RT64::LoadTile& tile, uint32_t width, uint32_t height) const {
        std::vector<uint32_t> argb((size_t)width * height);
        uint32_t row_bytes = tile.line << 3;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint32_t a = (tile.tmem << 3) + y * row_bytes + ((x << tile.siz) >> 1);
                if (y & 1) {
                    a ^= 4;
                }
                uint8_t b0 = tmem[a & (tmem_bytes - 1)], b1 = tmem[(a + 1) & (tmem_bytes - 1)];
                uint32_t r, g, b, alpha;
                if (tile.siz == siz_16b) {
                    uint32_t c = (b0 << 8) | b1;
                    if (tile.fmt == fmt_rgba) {
                        r = ((c >> 11) & 31) * 255 / 31;
                        g = ((c >> 6) & 31) * 255 / 31;
                        b = ((c >> 1) & 31) * 255 / 31;
                        alpha = (c & 1) ? 255 : 0;
                    } else {  // IA16; I16 doesn't exist, and RT64 reads it as IA16
                        r = g = b = b0;
                        alpha = b1;
                    }
                } else if (tile.siz == siz_8b) {
                    if (tile.fmt == fmt_ia) {
                        r = g = b = (b0 >> 4) * 17;
                        alpha = (b0 & 15) * 17;
                    } else {
                        r = g = b = alpha = b0;
                    }
                } else {
                    uint32_t n = (x & 1) ? (b0 & 15) : (b0 >> 4);
                    if (tile.fmt == fmt_ia) {
                        r = g = b = (n >> 1) * 255 / 7;
                        alpha = (n & 1) ? 255 : 0;
                    } else {
                        r = g = b = alpha = n * 17;
                    }
                }
                argb[(size_t)y * width + x] = (alpha << 24) | (r << 16) | (g << 8) | b;
            }
        }
        return argb;
    }

    void walk(uint32_t addr, int depth) {
        if (depth > 12) {
            return;
        }
        uint32_t p = phys(addr);
        for (; budget > 0; budget--, p += 8) {
            uint32_t w0 = rd32(p), w1 = rd32(p + 4);
            switch (w0 >> 24) {
            case g_enddl:
                return;
            case g_dl:
                for (const TextList& list : text_lists) {
                    if (w1 == list.begin) {
                        in_text = true;
                    } else if (w1 == list.end) {
                        in_text = false;
                    }
                }
                if (w0 & 0x00010000) {  // branch
                    p = phys(w1) - 8;
                } else {
                    walk(w1, depth + 1);
                }
                break;
            case g_moveword:
                // F3D's: the offset in w0's middle bytes and the index in its low byte (F3DEX2's are the other way).
                if ((w0 & 0xFF) == g_mw_segment) {
                    segments[(((w0 >> 8) & 0xFFFF) / 4) & 0xF] = w1 & 0x00FFFFFF;
                }
                break;
            case g_texture:
                texture_tile = (w0 >> 8) & 7;
                texture_on = (w0 & 0xFF) != 0;
                break;
            case g_setothermode_h: {  // F3D's: the shift and length in w0's low bytes
                uint32_t shift = (w0 >> 8) & 0xFF, len = w0 & 0xFF;
                uint32_t mask = (uint32_t)(((1ull << len) - 1) << shift);
                othermode_h = (othermode_h & ~mask) | (w1 & mask);
                break;
            }
            case 0xEF:  // G_RDPSETOTHERMODE
                othermode_h = w0 & 0x00FFFFFF;
                break;
            case g_tri1:
                if (texture_on) {
                    draw(texture_tile);
                }
                break;
            case 0xFD:  // G_SETTIMG
                timg.siz = (w0 >> 19) & 3;
                timg.width = (w0 & 0xFFF) + 1;
                timg.address = phys(w1);
                break;
            case 0xF5: {  // G_SETTILE
                if (in_text && ((w1 >> 24) & 7) != 7) {  // a render tile, not G_TX_LOADTILE's
                    uint32_t clamp = (w1 & ~((3u << 18) | (3u << 8))) | (2u << 18) | (2u << 8);  // cmt, cms
                    if (clamp != w1) {
                        clamped.emplace(p + 4, std::make_pair(w1, clamp));
                        wr32(p + 4, clamp);
                        w1 = clamp;
                    }
                }
                RT64::LoadTile& tile = tiles[(w1 >> 24) & 7];
                tile.fmt = (w0 >> 21) & 7;
                tile.siz = (w0 >> 19) & 3;
                tile.line = (w0 >> 9) & 0x1FF;
                tile.tmem = w0 & 0x1FF;
                tile.palette = (w1 >> 20) & 0xF;
                tile.cmt = (w1 >> 18) & 3;
                tile.maskt = (w1 >> 14) & 0xF;
                tile.shiftt = (w1 >> 10) & 0xF;
                tile.cms = (w1 >> 8) & 3;
                tile.masks = (w1 >> 4) & 0xF;
                tile.shifts = w1 & 0xF;
                break;
            }
            case 0xF2: {  // G_SETTILESIZE
                RT64::LoadTile& tile = tiles[(w1 >> 24) & 7];
                tile.uls = (w0 >> 12) & 0xFFF;
                tile.ult = w0 & 0xFFF;
                tile.lrs = (w1 >> 12) & 0xFFF;
                tile.lrt = w1 & 0xFFF;
                break;
            }
            case 0xF3:  // G_LOADBLOCK
                load_block((w1 >> 24) & 7, (w0 >> 12) & 0xFFF, w0 & 0xFFF, (w1 >> 12) & 0xFFF, w1 & 0xFFF);
                break;
            case 0xF4:  // G_LOADTILE
            case 0xF0:  // G_LOADTLUT
                std::fill(std::begin(tmem_known), std::end(tmem_known), false);
                break;
            case 0xE4:  // G_TEXRECT, then G_RDPHALF_2 and G_RDPHALF_CONT
            case 0xE5:  // G_TEXRECTFLIP
                draw((w1 >> 24) & 7);
                p += 16;
                break;
            case RT64_EXTENDED_OPCODE:
                switch (w0 & 0xFFFFFF) {
                case G_EX_TEXRECT_V1:
                    draw(w1 & 7);
                    p += 16;
                    break;
                case G_EX_SETSCISSORALIGN_V1:
                    p += 16;
                    break;
                case G_EX_FILLRECT_V1:
                case G_EX_SETVIEWPORT_V1:
                case G_EX_SETSCISSOR_V1:
                case G_EX_SETRECTALIGN_V1:
                case G_EX_SETVIEWPORTALIGN_V1:
                case G_EX_MATRIXGROUP_V1:
                case G_EX_EDITGROUPBYADDRESS_V1:
                case G_EX_VERTEX_V1:
                case G_EX_MATRIX_FLOAT_V1:
                case G_EX_SETVERTEXSEGMENT_V1:
                    p += 8;
                    break;
                }
                break;
            }
        }
    }
};

// Uncompressed RGBA8 DDS with a full mip chain, so RT64 can filter the text when it is drawn smaller. Colour is
// averaged weighted by alpha, so a smaller level doesn't pull black from transparent texels into the edges.
bool write_dds(const std::filesystem::path& path, std::vector<uint8_t> rgba, uint32_t width, uint32_t height) {
    uint32_t levels = 1;
    while ((width >> levels) > 0 || (height >> levels) > 0) {
        levels++;
    }
    uint32_t header[32]{};
    header[0] = 0x20534444;  // "DDS "
    header[1] = 124;
    header[2] = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000;  // caps, height, width, pitch, pixel format, mip count
    header[3] = height;
    header[4] = width;
    header[5] = width * 4;
    header[7] = levels;
    header[19] = 32;
    header[20] = 0x1 | 0x40;  // alpha, RGB
    header[22] = 32;
    header[23] = 0x000000FF;
    header[24] = 0x0000FF00;
    header[25] = 0x00FF0000;
    header[26] = 0xFF000000;
    header[27] = 0x1000 | 0x400000 | 0x8;  // texture, mipmap, complex
    // Written aside and moved into place: RT64 may be reading the pack.
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    FILE* f = fopen(tmp.string().c_str(), "wb");
    if (f == nullptr) {
        return false;
    }
    fwrite(header, sizeof(header), 1, f);
    uint32_t w = width, h = height;
    for (uint32_t level = 0; level < levels; level++) {
        fwrite(rgba.data(), 1, (size_t)w * h * 4, f);
        uint32_t nw = std::max(w / 2, 1u), nh = std::max(h / 2, 1u);
        std::vector<uint8_t> next((size_t)nw * nh * 4);
        for (uint32_t y = 0; y < nh; y++) {
            for (uint32_t x = 0; x < nw; x++) {
                uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
                uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
                const uint8_t* in[4] = { &rgba[(y0 * w + x0) * 4], &rgba[(y0 * w + x1) * 4], &rgba[(y1 * w + x0) * 4],
                                         &rgba[(y1 * w + x1) * 4] };
                uint8_t* o = &next[(y * nw + x) * 4];
                uint32_t alpha = in[0][3] + in[1][3] + in[2][3] + in[3][3];
                for (uint32_t c = 0; c < 3; c++) {
                    uint32_t sum = in[0][c] * in[0][3] + in[1][c] * in[1][3] + in[2][c] * in[2][3] + in[3][c] * in[3][3];
                    o[c] = alpha == 0 ? 0 : (uint8_t)((sum + alpha / 2) / alpha);
                }
                o[3] = (uint8_t)((alpha + 2) / 4);
            }
        }
        rgba = std::move(next);
        w = nw;
        h = nh;
    }
    bool ok = ferror(f) == 0;
    fclose(f);
    std::error_code ec;
    if (ok) {
        std::filesystem::rename(tmp, path, ec);
    }
    if (!ok || ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

void write_database() {
    std::string json = "{\n  \"configuration\": { \"autoPath\": \"rt64\", \"configurationVersion\": 3, \"hashVersion\": " +
                       std::to_string(RT64::TMEMHasher::CurrentHashVersion) +
                       ", \"defaultOperation\": \"preload\", \"defaultShift\": \"none\" },\n  \"textures\": [";
    for (size_t i = 0; i < in_pack.size(); i++) {
        char entry[96];
        snprintf(entry, sizeof(entry), "%s\n    { \"hashes\": { \"rt64\": \"%016" PRIx64 "\" }, \"path\": \"\" }",
                 i == 0 ? "" : ",", in_pack[i]);
        json += entry;
    }
    json += "\n  ]\n}\n";
    std::filesystem::path tmp = pack_dir / "rt64.json.tmp";
    FILE* f = fopen(tmp.string().c_str(), "wb");
    if (f == nullptr) {
        return;
    }
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    std::error_code ec;
    std::filesystem::rename(tmp, pack_dir / "rt64.json", ec);
}

// The texture upscaled into the pack, or false if it is empty.
bool make_texture(uint64_t hash, const Pending& texture) {
    if (std::none_of(texture.argb.begin(), texture.argb.end(), [](uint32_t c) { return (c >> 24) != 0; })) {
        return false;
    }
    int w = (int)texture.width + 2 * pad, h = (int)texture.height + 2 * pad;
    std::vector<uint32_t> src((size_t)w * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sx = std::clamp(x - pad, 0, (int)texture.width - 1), sy = std::clamp(y - pad, 0, (int)texture.height - 1);
            src[(size_t)y * w + x] = texture.argb[(size_t)sy * texture.width + sx];
        }
    }
    std::vector<uint32_t> big((size_t)w * h * scale * scale);
    xbrz::scale(scale, src.data(), big.data(), w, h, xbrz::ColorFormat::argbUnbuffered);

    uint32_t out_w = texture.width * scale, out_h = texture.height * scale;
    std::vector<uint8_t> rgba((size_t)out_w * out_h * 4);
    for (uint32_t y = 0; y < out_h; y++) {
        for (uint32_t x = 0; x < out_w; x++) {
            uint32_t c = big[(size_t)(y + pad * scale) * w * scale + x + pad * scale];
            uint8_t* o = &rgba[((size_t)y * out_w + x) * 4];
            o[0] = (uint8_t)(c >> 16);
            o[1] = (uint8_t)(c >> 8);
            o[2] = (uint8_t)c;
            o[3] = (uint8_t)(c >> 24);
        }
    }
    char name[32];
    snprintf(name, sizeof(name), "%016" PRIx64 ".dds", hash);
    if (!write_dds(pack_dir / name, std::move(rgba), out_w, out_h)) {
        fprintf(stderr, "[hires font] can't write %s\n", (pack_dir / name).string().c_str());
        return false;
    }
    in_pack.push_back(hash);
    return true;
}

void register_pack() {
    std::vector<std::filesystem::path> packs;
    if (enabled) {
        packs.push_back(pack_dir);
    }
    recompui::renderer::set_builtin_texture_packs(packs);
}

} // namespace

void sm64::init_hires_font() {
    std::filesystem::path root = recompui::file::get_app_folder_path() / "hires_font";
    pack_dir = root / pack_version;
    std::error_code ec;
    // Textures from an older upscale.
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.path().filename() != pack_version) {
            std::filesystem::remove_all(entry.path(), ec);
        }
    }
    std::filesystem::create_directories(pack_dir, ec);
    for (const auto& entry : std::filesystem::directory_iterator(pack_dir, ec)) {
        if (entry.path().extension() == ".dds") {
            uint64_t hash = std::strtoull(entry.path().stem().string().c_str(), nullptr, 16);
            seen.insert(hash);
            in_pack.push_back(hash);
        }
    }
    write_database();
    register_pack();
}

void sm64::set_hires_font(bool on) {
    enabled = on;
    register_pack();
}

// recomp_hires_font_scan(list): the frame's display list is complete (patches/interpolation.c, create_gfx_task_structure).
extern "C" void recomp_hires_font_scan(uint8_t* rdram, recomp_context* ctx) {
    static Rdp rdp;
    rdp.rdram = rdram;
    if (!enabled.load(std::memory_order_relaxed) || pack_dir.empty()) {
        // The game's own tiles back, where the list is still the one clamped (a menu's is loaded again with its level).
        for (const auto& [address, words] : rdp.clamped) {
            if (rdp.rd32(address) == words.second) {
                rdp.wr32(address, words.first);
            }
        }
        rdp.clamped.clear();
        return;
    }
    rdp.pending.clear();
    rdp.in_text = false;
    rdp.texture_on = false;
    rdp.budget = 1000000;
    std::fill(std::begin(rdp.tmem_known), std::end(rdp.tmem_known), false);
    rdp.walk(_arg<0, PTR(void)>(rdram, ctx), 0);
    size_t made = 0;
    for (const auto& [hash, texture] : rdp.pending) {
        made += make_texture(hash, texture);
    }
    if (made > 0) {
        write_database();
        register_pack();
        fprintf(stderr, "[hires font] %zu texture(s) made, %zu in the pack\n", made, in_pack.size());
    }
}
