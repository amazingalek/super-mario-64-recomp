#include "patches.h"
#include "rt64_extended_gbi.h"
#include "sm64.h"

#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/geo_misc.h"
#include "game/level_update.h"
#include "game/hud.h"
#include "game/ingame_menu.h"
#include "game/memory.h"
#include "game/print.h"
#include "game/rendering_graph_node.h"
#include "game/screen_transition.h"
#include "game/segment2.h"

// Widescreen: the game drawn to the window's aspect ratio when RT64's Aspect Ratio is Expand, and as the console draws
// it (4:3) when it is Original.
//
// RT64 widens the 3D by itself: under Expand it scales every projection drawn on the full screen's viewport (the
// perspective and the orthographic ones the game draws its 2D with) so that the game's 320 units of width stay 4:3 in
// the middle of the window and the rest of the window shows more to each side ("hor+"). A fill or texture rectangle
// as wide as the scissor is stretched to the window's width, and one narrower stays at its 4:3 place. What is left is
// the game's own, at the places the decomp marks with its GFX_DIMENSIONS macros (include/gfx_dimensions.h, for a
// widescreen build), here with the aspect ratio read at run time:
// - Culling (obj_is_in_view): the horizontal check widened to the screen's edge, so objects at the sides don't pop.
// - The HUD's lives, stars, timer and camera icon (texture rectangles) aligned to the window's left or right edge with
//   gEXSetRectAlign, so RT64's HUD Placement option says how far out they go; the coins and the power meter stay in
//   the middle, as the game centres them.
// - The fades, the pause screen's shade and the cannon's circle widened to cover the window, the star, circle, Mario
//   and Bowser wipes starting and ending at a radius that covers it, and the pause screen's red coins at the HUD's edge.
// - The skybox: interpolation.c draws as many columns of its tiles as the width needs (sky_columns below).
// - The title screen's and the game over screen's tiled backdrop, carried out to the window's edges with more columns.
//
// At 4:3, or with Aspect Ratio Original, every one of these draws as the game does: the aspect is 4:3 and each check
// below falls through to the game's own code.

#define ASPECT_4_3 (4.0f / 3.0f)

// The game's own functions and data this file uses that its headers leave out (static in their files), by their
// names in syms/.
struct TextLabel {
    u32 x;
    u32 y;
    s16 length;
    char buffer[50];
};
extern struct TextLabel *sTextLabels[52];
extern s16 sTextLabelsCount;
s8 char_to_glyph_index(char c);
void add_glyph_texture(s8 glyphIndex);
void render_textrect(s32 x, s32 y, s32 pos);
void render_hud_tex_lut(s32 x, s32 y, u8 *texture);
void render_hud_small_tex_lut(s32 x, s32 y, u8 *texture);
extern s16 sCameraHUDStatus;
extern u8 gWarpTransRed, gWarpTransGreen, gWarpTransBlue;
void set_warp_transition_rgb(u8 red, u8 green, u8 blue);
void print_animated_red_coin(s16 x, s16 y);
extern s8 gRedCoinsCollected;
extern void *sTextureTransitionID[];
void create_dl_scale_matrix(s8 pushOp, f32 x, f32 y, f32 z);

// The window's aspect ratio when Aspect Ratio is Expand, else 4:3 (src/game/recomp_api.cpp).
f32 widescreen_aspect(void) {
    return recomp_get_aspect_ratio(ASPECT_4_3);
}

// The skybox's columns: its tiles are 160 units wide and scrolled by an orthographic projection RT64 widens about the
// screen's middle, so the window shows 240 * aspect of them. interpolation.c adds a ring of one tile each side.
s32 widescreen_sky_columns(f32 scrollX, s32 *firstCol) {
    f32 half = SCREEN_HEIGHT / 2 * widescreen_aspect();
    f32 left = (scrollX + SCREEN_WIDTH / 2 - half) / 160.0f;
    f32 right = (scrollX + SCREEN_WIDTH / 2 + half) / 160.0f;
    s32 first = (s32) left - (left < 0.0f ? 1 : 0);
    s32 last = (s32) right - (right < 0.0f ? 1 : 0);

    *firstCol = first - 1;
    return last - first + 3;
}

// The screen's left and right edges in the game's 320 units, for full-screen geometry.
#define LEFT_EDGE(aspect) (SCREEN_WIDTH / 2 - SCREEN_HEIGHT / 2 * (aspect))
#define RIGHT_EDGE(aspect) (SCREEN_WIDTH / 2 + SCREEN_HEIGHT / 2 * (aspect))

// rendering_graph_node.c: the game's, with the screen's half width widened by the aspect ratio as the decomp's
// widescreen build does. The game's own check uses the vertical field of view as the horizontal one (its @bug), so
// at 4:3 objects already pop in a little inside the screen's edge; past 4:3 the whole check scales with the width.
RECOMP_PATCH s32 obj_is_in_view(struct GraphNodeObject *node, Mat4 matrix) {
    s16 cullingRadius;
    s16 halfFov;
    struct GraphNode *geo;
    f32 hScreenEdge;
    f32 aspect = widescreen_aspect();

    if (node->node.flags & GRAPH_RENDER_INVISIBLE) {
        return FALSE;
    }

    geo = node->sharedChild;

    halfFov = (gCurGraphNodeCamFrustum->fov / 2.0f + 1.0f) * 32768.0f / 180.0f + 0.5f;

    hScreenEdge = -matrix[3][2] * sins(halfFov) / coss(halfFov);
    if (aspect > ASPECT_4_3) {
        hScreenEdge *= aspect;
    }

    if (geo != NULL && geo->type == GRAPH_NODE_TYPE_CULLING_RADIUS) {
        cullingRadius = (f32) ((struct GraphNodeCullingRadius *) geo)->cullingRadius;
    } else {
        cullingRadius = 300;
    }

    if (matrix[3][2] > -100.0f + cullingRadius) {
        return FALSE;
    }

    if (matrix[3][2] < -20000.0f - cullingRadius) {
        return FALSE;
    }

    if (matrix[3][0] > hScreenEdge + cullingRadius) {
        return FALSE;
    }
    if (matrix[3][0] < -hScreenEdge - cullingRadius) {
        return FALSE;
    }
    return TRUE;
}

// The HUD. Its text is queued by print_text and drawn by render_text_labels at the end of the frame, so each label
// keeps the edge it was printed for. An origin aligns a rectangle to that edge of the window: its x is then counted
// from the edge (from the right one, 320 to the left of it), so the game's own numbers stay.
static u32 sLabelOrigin[52] = { [0 ... 51] = G_EX_ORIGIN_NONE };

static void align_labels(s32 first, u32 origin) {
    s32 i;

    for (i = first; i < sTextLabelsCount && i < 52; i++) {
        sLabelOrigin[i] = origin;
    }
}

// A rectangle aligned to an edge past 4:3 is still clipped by the game's scissor, which RT64 keeps on the 4:3 middle,
// so while the HUD draws aligned the scissor is the whole window: set_rect_origin from NONE pushes it, back to NONE pops.
static u32 sRectOrigin = G_EX_ORIGIN_NONE;

static void set_rect_origin(u32 origin) {
    if (origin == sRectOrigin) {
        return;
    }
    if (sRectOrigin == G_EX_ORIGIN_NONE) {
        gEXPushScissor(gDisplayListHead++);
        gEXSetScissor(gDisplayListHead++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0,
                      SCREEN_HEIGHT);
    }
    if (origin == G_EX_ORIGIN_RIGHT) {
        gEXSetRectAlign(gDisplayListHead++, G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -SCREEN_WIDTH * 4, 0,
                        -SCREEN_WIDTH * 4, 0);
    } else {
        gEXSetRectAlign(gDisplayListHead++, origin, origin, 0, 0, 0, 0);
    }
    if (origin == G_EX_ORIGIN_NONE) {
        gEXPopScissor(gDisplayListHead++);
    }
    sRectOrigin = origin;
}

// hud.c, the game's, with its labels aligned.
#define HUD_TOP_Y 209
#define HUD_STARS_X 78

RECOMP_PATCH void render_hud_mario_lives(void) {
    s32 first = sTextLabelsCount;

    print_text(22, HUD_TOP_Y, ",");
    print_text(38, HUD_TOP_Y, "*");
    print_text_fmt_int(54, HUD_TOP_Y, "%d", gHudDisplay.lives);
    align_labels(first, G_EX_ORIGIN_LEFT);
}

RECOMP_PATCH void render_hud_stars(void) {
    s8 showX = 0;
    s32 first = sTextLabelsCount;

    if (gHudFlash == 1 && gGlobalTimer & 8) {
        return;
    }

    if (gHudDisplay.stars < 100) {
        showX = 1;
    }

    print_text(SCREEN_WIDTH - HUD_STARS_X, HUD_TOP_Y, "-");
    if (showX == 1) {
        print_text(SCREEN_WIDTH - HUD_STARS_X + 16, HUD_TOP_Y, "*");
    }
    print_text_fmt_int((showX * 14) + SCREEN_WIDTH - (HUD_STARS_X - 16), HUD_TOP_Y, "%d", gHudDisplay.stars);
    align_labels(first, G_EX_ORIGIN_RIGHT);
}

RECOMP_PATCH void render_hud_timer(void) {
    u8 *(*hudLUT)[58] = segmented_to_virtual(&main_hud_lut);
    u16 timerValFrames = gHudDisplay.timer;
    u16 timerMins = timerValFrames / (30 * 60);
    u16 timerSecs = (timerValFrames - (timerMins * 1800)) / 30;
    u16 timerFracSecs = ((u16) (timerValFrames - (timerMins * 1800) - (timerSecs * 30))) / 3;
    s32 first = sTextLabelsCount;

    print_text(SCREEN_WIDTH - 150, 185, "TIME");
    print_text_fmt_int(SCREEN_WIDTH - 91, 185, "%0d", timerMins);
    print_text_fmt_int(SCREEN_WIDTH - 71, 185, "%02d", timerSecs);
    print_text_fmt_int(SCREEN_WIDTH - 37, 185, "%d", timerFracSecs);
    align_labels(first, G_EX_ORIGIN_RIGHT);

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    set_rect_origin(G_EX_ORIGIN_RIGHT);
    render_hud_tex_lut(SCREEN_WIDTH - 81, 32, (*hudLUT)[GLYPH_APOSTROPHE]);
    render_hud_tex_lut(SCREEN_WIDTH - 46, 32, (*hudLUT)[GLYPH_DOUBLE_QUOTE]);
    set_rect_origin(G_EX_ORIGIN_NONE);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

RECOMP_PATCH void render_hud_camera_status(void) {
    u8 *(*cameraLUT)[6] = segmented_to_virtual(&main_hud_camera_lut);
    s32 x = SCREEN_WIDTH - 54;
    s32 y = 205;

    if (sCameraHUDStatus == CAM_STATUS_NONE) {
        return;
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    set_rect_origin(G_EX_ORIGIN_RIGHT);
    render_hud_tex_lut(x, y, (*cameraLUT)[GLYPH_CAM_CAMERA]);

    switch (sCameraHUDStatus & CAM_STATUS_MODE_GROUP) {
        case CAM_STATUS_MARIO:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_MARIO_HEAD]);
            break;
        case CAM_STATUS_LAKITU:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_LAKITU_HEAD]);
            break;
        case CAM_STATUS_FIXED:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_FIXED]);
            break;
    }

    switch (sCameraHUDStatus & CAM_STATUS_C_MODE_GROUP) {
        case CAM_STATUS_C_DOWN:
            render_hud_small_tex_lut(x + 4, y + 16, (*cameraLUT)[GLYPH_CAM_ARROW_DOWN]);
            break;
        case CAM_STATUS_C_UP:
            render_hud_small_tex_lut(x + 4, y - 8, (*cameraLUT)[GLYPH_CAM_ARROW_UP]);
            break;
    }

    set_rect_origin(G_EX_ORIGIN_NONE);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

// print.c, the game's (US), each label drawn at its edge.
RECOMP_PATCH void render_text_labels(void) {
    s32 i;
    s32 j;
    s8 glyphIndex;
    Mtx *mtx;

    if (sTextLabelsCount == 0) {
        return;
    }

    mtx = alloc_display_list(sizeof(*mtx));

    if (mtx == NULL) {
        for (i = 0; i < 52; i++) {
            sLabelOrigin[i] = G_EX_ORIGIN_NONE;
        }
        sTextLabelsCount = 0;
        return;
    }

    guOrtho(mtx, 0.0f, SCREEN_WIDTH, 0.0f, SCREEN_HEIGHT, -10.0f, 10.0f, 1.0f);
    gSPPerspNormalize((Gfx *) (gDisplayListHead++), 0xFFFF);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);

    for (i = 0; i < sTextLabelsCount; i++) {
        set_rect_origin(i < 52 ? sLabelOrigin[i] : G_EX_ORIGIN_NONE);
        for (j = 0; j < sTextLabels[i]->length; j++) {
            glyphIndex = char_to_glyph_index(sTextLabels[i]->buffer[j]);

            if (glyphIndex != GLYPH_SPACE) {
                add_glyph_texture(glyphIndex);
                render_textrect(sTextLabels[i]->x, sTextLabels[i]->y, j);
            }
        }

        mem_pool_free(gEffectsMemoryPool, sTextLabels[i]);
        if (i < 52) {
            sLabelOrigin[i] = G_EX_ORIGIN_NONE;
        }
    }
    set_rect_origin(G_EX_ORIGIN_NONE);

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    sTextLabelsCount = 0;
}

// screen_transition.c: the colour fades, as wide as the screen.
RECOMP_PATCH Vtx *vertex_transition_color(struct WarpTransitionData *transData, u8 alpha) {
    Vtx *verts = alloc_display_list(4 * sizeof(*verts));
    u8 r = transData->red;
    u8 g = transData->green;
    u8 b = transData->blue;
    f32 aspect = widescreen_aspect();
    s16 left = aspect > ASPECT_4_3 ? (s16) (LEFT_EDGE(aspect) - 1.0f) : 0;
    s16 right = aspect > ASPECT_4_3 ? (s16) (RIGHT_EDGE(aspect) + 1.0f) : SCREEN_WIDTH;

    if (verts != NULL) {
        make_vertex(verts, 0, left, 0, -1, 0, 0, r, g, b, alpha);
        make_vertex(verts, 1, right, 0, -1, 0, 0, r, g, b, alpha);
        make_vertex(verts, 2, right, SCREEN_HEIGHT, -1, 0, 0, r, g, b, alpha);
        make_vertex(verts, 3, left, SCREEN_HEIGHT, -1, 0, 0, r, g, b, alpha);
    }
    return verts;
}

// area.c, the game's: a wipe starts or ends at a radius that covers the whole screen (SCREEN_HEIGHT * aspect, the
// game's GFX_DIMENSIONS_FULL_RADIUS, 320 at 4:3).
RECOMP_PATCH void play_transition(s16 transType, s16 time, u8 red, u8 green, u8 blue) {
    s16 fullRadius = SCREEN_HEIGHT * widescreen_aspect();

    if (fullRadius < SCREEN_WIDTH) {
        fullRadius = SCREEN_WIDTH;
    }

    gWarpTransition.isActive = TRUE;
    gWarpTransition.type = transType;
    gWarpTransition.time = time;
    gWarpTransition.pauseRendering = FALSE;

    if (transType & 1) {
        set_warp_transition_rgb(red, green, blue);
    } else {
        red = gWarpTransRed, green = gWarpTransGreen, blue = gWarpTransBlue;
    }

    gWarpTransition.data.red = red;
    gWarpTransition.data.green = green;
    gWarpTransition.data.blue = blue;
    if (transType >= 8) {
        gWarpTransition.data.startTexX = SCREEN_WIDTH / 2;
        gWarpTransition.data.startTexY = SCREEN_HEIGHT / 2;
        gWarpTransition.data.endTexX = SCREEN_WIDTH / 2;
        gWarpTransition.data.endTexY = SCREEN_HEIGHT / 2;

        gWarpTransition.data.texTimer = 0;

        if (transType & 1) {
            gWarpTransition.data.startTexRadius = fullRadius;
            if (transType >= 0x0F) {
                gWarpTransition.data.endTexRadius = 16;
            } else {
                gWarpTransition.data.endTexRadius = 0;
            }
        } else {
            if (transType >= 0x0E) {
                gWarpTransition.data.startTexRadius = 16;
            } else {
                gWarpTransition.data.startTexRadius = 0;
            }
            gWarpTransition.data.endTexRadius = fullRadius;
        }
    }
}

// screen_transition.c, the game's: the view from inside a cannon is a circle in black, and past 4:3 the black is
// carried out to the window's edges.
RECOMP_PATCH Gfx *render_cannon_circle_base(void) {
    Vtx *verts = alloc_display_list(8 * sizeof(*verts));
    Gfx *dlist = alloc_display_list(20 * sizeof(*dlist));
    Gfx *g = dlist;
    f32 aspect = widescreen_aspect();

    if (verts == NULL || dlist == NULL) {
        return NULL;
    }

    make_vertex(verts, 0, 0, 0, -1, -1152, 1824, 0, 0, 0, 255);
    make_vertex(verts, 1, SCREEN_WIDTH, 0, -1, 1152, 1824, 0, 0, 0, 255);
    make_vertex(verts, 2, SCREEN_WIDTH, SCREEN_HEIGHT, -1, 1152, 192, 0, 0, 0, 255);
    make_vertex(verts, 3, 0, SCREEN_HEIGHT, -1, -1152, 192, 0, 0, 0, 255);
    make_vertex(verts, 4, LEFT_EDGE(aspect) - 1.0f, 0, -1, 0, 0, 0, 0, 0, 255);
    make_vertex(verts, 5, RIGHT_EDGE(aspect) + 1.0f, 0, -1, 0, 0, 0, 0, 0, 255);
    make_vertex(verts, 6, RIGHT_EDGE(aspect) + 1.0f, SCREEN_HEIGHT, -1, 0, 0, 0, 0, 0, 255);
    make_vertex(verts, 7, LEFT_EDGE(aspect) - 1.0f, SCREEN_HEIGHT, -1, 0, 0, 0, 0, 0, 255);

    gSPDisplayList(g++, dl_proj_mtx_fullscreen);
    gDPSetCombineMode(g++, G_CC_MODULATEIDECALA, G_CC_MODULATEIDECALA);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPLoadTextureBlock(g++, sTextureTransitionID[TEX_TRANS_CIRCLE], G_IM_FMT_IA, G_IM_SIZ_8b, 32, 64, 0,
                        G_TX_WRAP | G_TX_MIRROR, G_TX_WRAP | G_TX_MIRROR, 5, 6, G_TX_NOLOD, G_TX_NOLOD);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPVertex(g++, VIRTUAL_TO_PHYSICAL(verts), 4, 0);
    gSPDisplayList(g++, dl_draw_quad_verts_0123);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    if (aspect > ASPECT_4_3) {
        gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
        gSPVertex(g++, VIRTUAL_TO_PHYSICAL(verts + 4), 4, 4);
        gSP2Triangles(g++, 4, 0, 3, 0, 4, 3, 7, 0);
        gSP2Triangles(g++, 1, 5, 6, 0, 1, 6, 2, 0);
    }
    gSPDisplayList(g++, dl_screen_transition_end);
    gSPEndDisplayList(g);
    return dlist;
}

// ingame_menu.c: the pause screen's shade, as wide as the screen (the game's is the dialog box scaled to just over
// 4:3), and its red coins at the HUD's right edge.
RECOMP_PATCH void shade_screen(void) {
    f32 aspect = widescreen_aspect();

    if (aspect > ASPECT_4_3) {
        create_dl_translation_matrix(MENU_MTX_PUSH, LEFT_EDGE(aspect) - 1.0f, SCREEN_HEIGHT, 0);
        create_dl_scale_matrix(MENU_MTX_NOPUSH, (SCREEN_HEIGHT * aspect + 2.0f) / 130.0f, 3.4f, 1.0f);
    } else {
        create_dl_translation_matrix(MENU_MTX_PUSH, 0, SCREEN_HEIGHT, 0);
        create_dl_scale_matrix(MENU_MTX_NOPUSH, 2.6f, 3.4f, 1.0f);
    }

    gDPSetEnvColor(gDisplayListHead++, 0, 0, 0, 110);
    gSPDisplayList(gDisplayListHead++, dl_draw_text_bg_box);
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

RECOMP_PATCH void render_pause_red_coins(void) {
    s8 x;
    f32 right = RIGHT_EDGE(recomp_get_hud_aspect_ratio(ASPECT_4_3));

    for (x = 0; x < gRedCoinsCollected; x++) {
        print_animated_red_coin(right - 30 - x * 20, 16);
    }
}

// intro_geo.c: the tiled backdrop behind Mario's face on the title screen and the game over screen, 4 columns of 80
// units and 3 rows. Past 4:3 more columns are drawn each side, each a copy of the edge tile of its row (on the game
// over screen they turn into the title's along with it). The game's intro_backdrop_one_image places tile <index> at
// xCoords[index], so each extra tile is drawn with that entry moved for the call.
struct GraphNodeMore {
    struct GraphNode node;
    void *todo;
    u32 unk18;
};
extern s8 *introBackgroundTables[];
extern s8 gameOverBackgroundTable[12];
extern s32 sGameOverFrameCounter;
extern s32 sGameOverTableIndex;
extern s8 flipOrder[12];
extern f32 xCoords[12];
Gfx *intro_backdrop_one_image(s32 index, s8 *backgroundTable);

#define INTRO_BACKGROUND_SUPER_MARIO 0
#define INTRO_BACKGROUND_GAME_OVER 1
#define TITLE_BG_BEGIN ((Gfx *) 0x0A000100)
#define TITLE_BG_END ((Gfx *) 0x0A000190)

static Gfx *draw_backdrop(s8 *backgroundTable) {
    f32 aspect = widescreen_aspect();
    s32 sideColumns = 0;
    Gfx *dl;
    Gfx *dlIter;
    s32 i;
    s32 c;

    if (aspect > ASPECT_4_3) {
        sideColumns = (s32) ((SCREEN_HEIGHT / 2 * aspect - SCREEN_WIDTH / 2) / 80.0f) + 1;
    }

    dl = alloc_display_list((4 + 12 + sideColumns * 6) * sizeof(*dl));
    if (dl == NULL) {
        return NULL;
    }
    dlIter = dl;
    gSPDisplayList(dlIter++, &dl_proj_mtx_fullscreen);
    gSPDisplayList(dlIter++, TITLE_BG_BEGIN);
    for (i = 0; i < 12; i++) {
        gSPDisplayList(dlIter++, intro_backdrop_one_image(i, backgroundTable));
    }
    for (c = 1; c <= sideColumns; c++) {
        for (i = 0; i < 12; i += 4) {
            f32 left = xCoords[i];
            f32 right = xCoords[i + 3];

            xCoords[i] = -80.0f * c;
            gSPDisplayList(dlIter++, intro_backdrop_one_image(i, backgroundTable));
            xCoords[i] = left;
            xCoords[i + 3] = SCREEN_WIDTH - 80.0f + 80.0f * c;
            gSPDisplayList(dlIter++, intro_backdrop_one_image(i + 3, backgroundTable));
            xCoords[i + 3] = right;
        }
    }
    gSPDisplayList(dlIter++, TITLE_BG_END);
    gSPEndDisplayList(dlIter);
    return dl;
}

// The game's, with the backdrop drawn by draw_backdrop.
RECOMP_PATCH Gfx *geo_intro_regular_backdrop(s32 state, struct GraphNode *node, UNUSED void *context) {
    struct GraphNodeMore *graphNode = (struct GraphNodeMore *) node;
    s32 index = graphNode->unk18 & 0xff;

    if (state == 1) {
        graphNode->node.flags = (graphNode->node.flags & 0xFF) | (LAYER_OPAQUE << 8);
        return draw_backdrop(introBackgroundTables[index]);
    }
    return NULL;
}

RECOMP_PATCH Gfx *geo_intro_gameover_backdrop(s32 state, struct GraphNode *node, UNUSED void *context) {
    s32 i;

    if (state != 1) {
        sGameOverFrameCounter = 0;
        sGameOverTableIndex = -2;
        for (i = 0; i < 12; i++) {
            gameOverBackgroundTable[i] = INTRO_BACKGROUND_GAME_OVER;
        }
        return NULL;
    }

    if (sGameOverTableIndex == -2) {
        if (sGameOverFrameCounter == 180) {
            sGameOverTableIndex++;
            sGameOverFrameCounter = 0;
        }
    } else {
        if (sGameOverTableIndex != 11 && !(sGameOverFrameCounter & 0x1)) {
            sGameOverTableIndex++;
            gameOverBackgroundTable[flipOrder[sGameOverTableIndex]] = INTRO_BACKGROUND_SUPER_MARIO;
        }
    }
    if (sGameOverTableIndex != 11) {
        sGameOverFrameCounter++;
    }
    node->flags = (node->flags & 0xFF) | (LAYER_OPAQUE << 8);
    return draw_backdrop(gameOverBackgroundTable);
}
