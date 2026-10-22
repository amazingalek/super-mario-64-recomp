#include "patches.h"
#include "sm64.h"

#include "engine/graph_node.h"
#include "game/game_init.h"
#include "game/main.h"
#include "game/profiler.h"

// The enhancements other than widescreen (widescreen.c), each read from its Graphics option (src/game/config.cpp)
// and each drawing as the game does at its Original value.

// The game's own functions and data this file uses that its headers leave out, by their names in syms/.
extern s16 gMatStackIndex;
extern Mtx *gMatStackFixed[32];
extern u16 sRenderedFramebuffer;
void geo_process_node_and_siblings(struct GraphNode *firstNode);

// Input Lag (game_init.c, the game's). The game triple-buffers: each frame it hands the graphics task of the frame it
// just worked out to the RCP, and at the next retrace shows the framebuffer of the frame before (sRenderedFramebuffer),
// which keeps one frame drawn ahead of the screen for an RCP that can take longer than a frame. The runtime's RCP
// draws a frame in the order it was handed over, before any later swap is shown, so Reduced shows the frame just
// handed over instead (sRenderingFramebuffer): one game frame, 33 ms, sooner. The next frame is drawn into the third
// buffer, never the one on screen.
RECOMP_PATCH void display_and_vsync(void) {
    u16 shown = recomp_get_reduced_input_lag() ? sRenderingFramebuffer : sRenderedFramebuffer;

    profiler_log_thread5_time(BEFORE_DISPLAY_LISTS);
    osRecvMesg(&gGfxVblankQueue, &gMainReceivedMesg, OS_MESG_BLOCK);
    if (gGoddardVblankCallback != NULL) {
        gGoddardVblankCallback();
        gGoddardVblankCallback = NULL;
    }
    exec_display_list(&gGfxPool->spTask);
    profiler_log_thread5_time(AFTER_DISPLAY_LISTS);
    osRecvMesg(&gGameVblankQueue, &gMainReceivedMesg, OS_MESG_BLOCK);
    osViSwapBuffer((void *) PHYSICAL_TO_VIRTUAL(gPhysicalFramebuffers[shown]));
    profiler_log_thread5_time(THREAD5_END);
    osRecvMesg(&gGameVblankQueue, &gMainReceivedMesg, OS_MESG_BLOCK);
    if (++sRenderedFramebuffer == 3) {
        sRenderedFramebuffer = 0;
    }
    if (++sRenderingFramebuffer == 3) {
        sRenderingFramebuffer = 0;
    }
    gGlobalTimer++;
}

// Level of Detail (rendering_graph_node.c, the game's). A level-of-detail node draws its children only when the
// camera's distance to it is in its range: Mario's three models switch at 600 and 1600 units, and other models and
// parts of levels have ranges of their own. The distance is divided by the option's divisor, so High keeps each model
// to 4 times its distance and Highest always picks the nearest range (0 is in every nearest range the game has,
// including Mario's from -2048). Every one of Mario's models has the same skeleton and the same held-object node, so
// only what is drawn changes.
RECOMP_PATCH void geo_process_level_of_detail(struct GraphNodeLevelOfDetail *node) {
    Mtx *mtx = gMatStackFixed[gMatStackIndex];
    s16 distanceFromCam = -GET_HIGH_S16_OF_32(mtx->m[1][3]);
    s32 divisor = recomp_get_lod_divisor();

    if (divisor == 0) {
        distanceFromCam = 0;
    } else if (divisor != 1) {
        distanceFromCam /= divisor;
    }

    if (node->minDistance <= distanceFromCam && distanceFromCam < node->maxDistance) {
        if (node->node.children != 0) {
            geo_process_node_and_siblings(node->node.children);
        }
    }
}
