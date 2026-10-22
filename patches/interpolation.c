#include "patches.h"
#include "rt64_extended_gbi.h"

#include "dialog_ids.h"
#include "buffers/buffers.h"
#include "buffers/gfx_output_buffer.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/geo_misc.h"
#include "game/ingame_menu.h"
#include "game/memory.h"
#include "game/object_list_processor.h"
#include "game/rendering_graph_node.h"
#include "game/save_file.h"
#include "game/screen_transition.h"
#include "game/segment2.h"

// RT64's interpolation to the display's rate (its Framerate Display). The game draws 30 frames a second; RT64 draws the
// frames between two of them by pairing each matrix of the newer frame with one of the older and blending the two. Left
// to itself RT64 guesses the pairs, which smears what spawns, warps or is cut to. Here every matrix the game draws is
// tagged with an id that says what it is, so RT64 pairs a thing only with itself, and the id changes where a thing must
// not be blended: then the newer frame is drawn as it is.
//
// - Display lists (geo_append_display_list, geo_process_master_list_sub): every display list the scene graph draws is
//   tagged by the graph node that drew it and the object it was drawn for (the held object when there is one), so Mario,
//   each limb, each object and the level's own geometry are each their own group. The camera is part of every one of
//   these matrices (SM64 multiplies it into the modelview, geo_process_camera), so a camera turn is blended per matrix.
// - Objects that teleport (geo_process_node_and_siblings): an object of the pool that moves further than
//   TELEPORT_DISTANCE in one frame gets a new id. One that spawns needs nothing: the game frees an object's slot only
//   after it was last drawn and spawns into it in a later frame, so a new object never pairs with the old.
// - Camera cuts (geo_process_camera): a cut changes every id of the frame (sCutCount). A cut is a new camera node (a new
//   area or level), a frame the camera didn't draw, its position or focus moving further than CUT_DISTANCE, or its view
//   turning further than CUT_ANGLE_COS in one frame. The game's smooth moves stay well below those: Mario out of a
//   cannon moves the camera about 140 units a frame, a C-button turn about 11 degrees.
// - Projections: the perspective (and the camera's roll) is one group per camera node, the skybox's orthographic
//   projection one per projection node, so a field of view change and the sky's scroll are blended too.
// - The skybox (create_skybox_facing_camera): the sky scrolls by an orthographic projection over a 3x3 grid of tiles that
//   just covers the screen, so a blended scroll would uncover the grid's edge; it draws a ring of tiles more (outside the
//   screen, so nothing changes at the game's own rate), and as many columns more as a window wider than 4:3 shows
//   (widescreen.c). Its scroll wraps once per turn of the camera, so the scroll is unwrapped (sSkyWraps) to blend
//   across the wrap.
// - The HUD (render_dl_power_meter): the power meter slides in and out by a matrix of its own. The rest of the HUD is
//   text and rectangles at fixed places.
// - Dialogs and menus (render_menus_and_dialogs): a matrix per letter, all one group paired in drawing order, its id
//   changed by whatever changes that order. Untagged, RT64 paired a letter with another drawn with the same texture.
// - Warp transitions (render_screen_transition): the shrinking or growing star or circle of a door, a painting or a pipe
//   is vertices the game moves each frame, so its group blends vertices too.
// - The display list pool (select_gfx_pool): each tagged list costs three commands more, so the pool is 4 times the
//   game's, in the patches' memory. Every frame's list starts with gEXEnable, which the commands above need.

#define TELEPORT_DISTANCE 500.0f
#define CUT_DISTANCE 500.0f
#define CUT_ANGLE_COS 0.7071f // 45 degrees

// Components each group blends. Matrices are decomposed (position, rotation and scale blended apart), and paired in the
// order they are drawn within a group, so a display list that loads matrices of its own (a painting, a shadow) pairs
// them one by one.
#define gEXMatrixGroupTagged(cmd, id, proj)                                                                         \
    gEXMatrixGroupDecomposed(cmd, id, G_EX_PUSH, proj, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,     \
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, \
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,         \
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO)

// The game's own functions this file calls that its headers leave out (rendering_graph_node.c's are static), by their
// names in syms/.
extern s16 gMatStackIndex;
extern Mat4 gMatStack[32];
extern Mtx *gMatStackFixed[32];
extern struct AllocOnlyPool *gDisplayListHeap;
s32 calculate_skybox_scaled_x(s8 player, f32 fov);
s32 calculate_skybox_scaled_y(s8 player, f32 fov);
void geo_try_process_children(struct GraphNode *node);
void geo_process_held_object(struct GraphNodeHeldObject *node);
void geo_process_ortho_projection(struct GraphNodeOrthoProjection *node);
void geo_process_perspective(struct GraphNodePerspective *node);
void geo_process_master_list(struct GraphNodeMasterList *node);
void geo_process_level_of_detail(struct GraphNodeLevelOfDetail *node);
void geo_process_switch(struct GraphNodeSwitchCase *node);
void geo_process_camera(struct GraphNodeCamera *node);
void geo_process_translation_rotation(struct GraphNodeTranslationRotation *node);
void geo_process_translation(struct GraphNodeTranslation *node);
void geo_process_rotation(struct GraphNodeRotation *node);
void geo_process_object(struct Object *node);
void geo_process_animated_part(struct GraphNodeAnimatedPart *node);
void geo_process_billboard(struct GraphNodeBillboard *node);
void geo_process_display_list(struct GraphNodeDisplayList *node);
void geo_process_scale(struct GraphNodeScale *node);
void geo_process_shadow(struct GraphNodeShadow *node);
void geo_process_object_parent(struct GraphNodeObjectParent *node);
void geo_process_generated_list(struct GraphNodeGenerated *node);
void geo_process_background(struct GraphNodeBackground *node);
void geo_process_master_list_sub(struct GraphNodeMasterList *node);

// The game's DisplayListNode with the id after it: only the two functions here make and read these.
struct TaggedDisplayListNode {
    Mtx *transform;
    void *displayList;
    struct TaggedDisplayListNode *next;
    u32 id;
};

static u32 sCutCount;

// The node geo_process_node_and_siblings is processing, so geo_append_display_list knows who draws.
static struct GraphNode *sTagNode;

// The projection of the master list being built (a camera or an orthographic projection node), or NULL.
static struct GraphNode *sMasterProjNode;

// Each pool object's teleport count, its position when last drawn and the frame it was drawn in.
static u8 sObjTeleports[OBJECT_POOL_CAPACITY];
static Vec3f sObjLastPos[OBJECT_POOL_CAPACITY];
static u32 sObjLastFrame[OBJECT_POOL_CAPACITY];

static struct GraphNodeCamera *sLastCamNode;
static Vec3f sLastCamPos;
static Vec3f sLastCamFocus;
static u32 sLastCamFrame;

// murmur3's finalizer, so that ids made from nearby pointers spread over all 32 bits.
static u32 mix_id(u32 h) {
    h ^= h >> 16;
    h *= 0x85EBCA6B;
    h ^= h >> 13;
    h *= 0xC2B2AE35;
    h ^= h >> 16;
    return h;
}

// An id is never G_EX_ID_IGNORE (0) or G_EX_ID_AUTO (0xFFFFFFFF).
static u32 make_id(u32 a, u32 b, u32 c) {
    u32 h = mix_id(mix_id(mix_id(a) ^ b) ^ c);
    return (h == G_EX_ID_IGNORE || h == G_EX_ID_AUTO) ? 1 : h;
}

static s32 pool_index(struct GraphNodeObject *obj) {
    u32 offset = (u32) obj - (u32) gObjectPool;
    if (offset < OBJECT_POOL_CAPACITY * sizeof(struct Object)) {
        return offset / sizeof(struct Object);
    }
    return -1;
}

static f32 dist_sq(Vec3f a, Vec3f b) {
    f32 dx = a[0] - b[0];
    f32 dy = a[1] - b[1];
    f32 dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

static void check_teleport(struct Object *obj) {
    s32 i = pool_index(&obj->header.gfx);
    if (i < 0 || sObjLastFrame[i] == gGlobalTimer) {
        return;
    }
    if (dist_sq(obj->header.gfx.pos, sObjLastPos[i]) > TELEPORT_DISTANCE * TELEPORT_DISTANCE) {
        sObjTeleports[i]++;
    }
    vec3f_copy(sObjLastPos[i], obj->header.gfx.pos);
    sObjLastFrame[i] = gGlobalTimer;
}

static u32 display_list_id(void) {
    struct GraphNodeObject *obj = gCurGraphNodeObject;
    s32 i;
    if (gCurGraphNodeHeldObject != NULL && gCurGraphNodeHeldObject->objNode != NULL) {
        obj = &gCurGraphNodeHeldObject->objNode->header.gfx;
    }
    i = pool_index(obj);
    return make_id((u32) sTagNode, (u32) obj + (i >= 0 ? sObjTeleports[i] : 0), sCutCount);
}

RECOMP_PATCH void geo_append_display_list(void *displayList, s16 layer) {
    if (gCurGraphNodeMasterList != 0) {
        struct TaggedDisplayListNode *listNode =
            alloc_only_pool_alloc(gDisplayListHeap, sizeof(struct TaggedDisplayListNode));

        listNode->transform = gMatStackFixed[gMatStackIndex];
        listNode->displayList = displayList;
        listNode->next = 0;
        listNode->id = display_list_id();
        if (gCurGraphNodeMasterList->listHeads[layer] == 0) {
            gCurGraphNodeMasterList->listHeads[layer] = (struct DisplayListNode *) listNode;
        } else {
            ((struct TaggedDisplayListNode *) gCurGraphNodeMasterList->listTails[layer])->next = listNode;
        }
        gCurGraphNodeMasterList->listTails[layer] = (struct DisplayListNode *) listNode;
    }
}

extern struct RenderModeContainer {
    u32 modes[8];
} renderModeTable_1Cycle[2], renderModeTable_2Cycle[2];

RECOMP_PATCH void geo_process_master_list_sub(struct GraphNodeMasterList *node) {
    struct TaggedDisplayListNode *currList;
    s32 i;
    s32 enableZBuffer = (node->node.flags & GRAPH_RENDER_Z_BUFFER) != 0;
    struct RenderModeContainer *modeList = &renderModeTable_1Cycle[enableZBuffer];
    struct RenderModeContainer *mode2List = &renderModeTable_2Cycle[enableZBuffer];
    struct GraphNode *projNode = sMasterProjNode;

    // The projection was loaded while the graph was walked; RT64 gives it the group current at its first triangle.
    if (projNode != NULL) {
        gEXMatrixGroupTagged(gDisplayListHead++, make_id((u32) projNode, 0x50524F4A, sCutCount), G_MTX_PROJECTION);
    }

    if (enableZBuffer != 0) {
        gDPPipeSync(gDisplayListHead++);
        gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER);
    }

    for (i = 0; i < GFX_NUM_MASTER_LISTS; i++) {
        if ((currList = (struct TaggedDisplayListNode *) node->listHeads[i]) != NULL) {
            gDPSetRenderMode(gDisplayListHead++, modeList->modes[i], mode2List->modes[i]);
            while (currList != NULL) {
                gEXMatrixGroupTagged(gDisplayListHead++, currList->id, G_MTX_MODELVIEW);
                gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(currList->transform),
                          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                gSPDisplayList(gDisplayListHead++, currList->displayList);
                gEXPopMatrixGroup(gDisplayListHead++, G_MTX_MODELVIEW);
                currList = currList->next;
            }
        }
    }
    if (enableZBuffer != 0) {
        gDPPipeSync(gDisplayListHead++);
        gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER);
    }

    if (projNode != NULL) {
        gEXPopMatrixGroup(gDisplayListHead++, G_MTX_PROJECTION);
    }
}

RECOMP_PATCH void geo_process_master_list(struct GraphNodeMasterList *node) {
    s32 i;

    if (gCurGraphNodeMasterList == NULL && node->node.children != NULL) {
        gCurGraphNodeMasterList = node;
        sMasterProjNode = NULL;
        for (i = 0; i < GFX_NUM_MASTER_LISTS; i++) {
            node->listHeads[i] = NULL;
        }
        geo_process_node_and_siblings(node->node.children);
        geo_process_master_list_sub(node);
        sMasterProjNode = NULL;
        gCurGraphNodeMasterList = NULL;
    }
}

static s32 is_camera_cut(struct GraphNodeCamera *node) {
    f32 prevX, prevY, prevZ, curX, curY, curZ, dot, lenSq;

    if (node != sLastCamNode || sLastCamFrame + 1 != gGlobalTimer) {
        return TRUE;
    }
    if (dist_sq(node->pos, sLastCamPos) > CUT_DISTANCE * CUT_DISTANCE
        || dist_sq(node->focus, sLastCamFocus) > CUT_DISTANCE * CUT_DISTANCE) {
        return TRUE;
    }
    prevX = sLastCamFocus[0] - sLastCamPos[0];
    prevY = sLastCamFocus[1] - sLastCamPos[1];
    prevZ = sLastCamFocus[2] - sLastCamPos[2];
    curX = node->focus[0] - node->pos[0];
    curY = node->focus[1] - node->pos[1];
    curZ = node->focus[2] - node->pos[2];
    dot = prevX * curX + prevY * curY + prevZ * curZ;
    lenSq = (prevX * prevX + prevY * prevY + prevZ * prevZ) * (curX * curX + curY * curY + curZ * curZ);
    // cos(angle) < CUT_ANGLE_COS, without a square root: the dot's sign and its square against the lengths'.
    return dot < 0.0f || dot * dot < CUT_ANGLE_COS * CUT_ANGLE_COS * lenSq;
}

RECOMP_PATCH void geo_process_camera(struct GraphNodeCamera *node) {
    Mat4 cameraTransform;
    Mtx *rollMtx = alloc_display_list(sizeof(*rollMtx));
    Mtx *mtx = alloc_display_list(sizeof(*mtx));

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }

    // The camera has moved for this frame (its function above): a cut changes every id drawn from here on.
    if (is_camera_cut(node)) {
        sCutCount++;
    }
    sLastCamNode = node;
    vec3f_copy(sLastCamPos, node->pos);
    vec3f_copy(sLastCamFocus, node->focus);
    sLastCamFrame = gGlobalTimer;
    sMasterProjNode = &node->fnNode.node;

    mtxf_rotate_xy(rollMtx, node->rollScreen);

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(rollMtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);

    mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
    mtxf_mul(gMatStack[gMatStackIndex + 1], cameraTransform, gMatStack[gMatStackIndex]);
    gMatStackIndex++;
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
    gMatStackFixed[gMatStackIndex] = mtx;
    if (node->fnNode.node.children != 0) {
        gCurGraphNodeCamera = node;
        node->matrixPtr = &gMatStack[gMatStackIndex];
        geo_process_node_and_siblings(node->fnNode.node.children);
        gCurGraphNodeCamera = NULL;
    }
    gMatStackIndex--;
}

// The game's, with sTagNode set around each node's processing (and restored after, for a parent that appends its
// display list after its children), the orthographic projection noted for its master list and each object checked for
// a teleport.
RECOMP_PATCH void geo_process_node_and_siblings(struct GraphNode *firstNode) {
    s16 iterateChildren = TRUE;
    struct GraphNode *curGraphNode = firstNode;
    struct GraphNode *parent = curGraphNode->parent;
    struct GraphNode *prevTagNode = sTagNode;

    if (parent != NULL) {
        iterateChildren = (parent->type != GRAPH_NODE_TYPE_SWITCH_CASE);
    }

    do {
        sTagNode = curGraphNode;
        if (curGraphNode->flags & GRAPH_RENDER_ACTIVE) {
            if (curGraphNode->flags & GRAPH_RENDER_CHILDREN_FIRST) {
                geo_try_process_children(curGraphNode);
            } else {
                switch (curGraphNode->type) {
                    case GRAPH_NODE_TYPE_ORTHO_PROJECTION:
                        if (gCurGraphNodeMasterList != NULL) {
                            sMasterProjNode = curGraphNode;
                        }
                        geo_process_ortho_projection((struct GraphNodeOrthoProjection *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_PERSPECTIVE:
                        geo_process_perspective((struct GraphNodePerspective *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_MASTER_LIST:
                        geo_process_master_list((struct GraphNodeMasterList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_LEVEL_OF_DETAIL:
                        geo_process_level_of_detail((struct GraphNodeLevelOfDetail *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SWITCH_CASE:
                        geo_process_switch((struct GraphNodeSwitchCase *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_CAMERA:
                        geo_process_camera((struct GraphNodeCamera *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION_ROTATION:
                        geo_process_translation_rotation((struct GraphNodeTranslationRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION:
                        geo_process_translation((struct GraphNodeTranslation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ROTATION:
                        geo_process_rotation((struct GraphNodeRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT:
                        check_teleport((struct Object *) curGraphNode);
                        geo_process_object((struct Object *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ANIMATED_PART:
                        geo_process_animated_part((struct GraphNodeAnimatedPart *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BILLBOARD:
                        geo_process_billboard((struct GraphNodeBillboard *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_DISPLAY_LIST:
                        geo_process_display_list((struct GraphNodeDisplayList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SCALE:
                        geo_process_scale((struct GraphNodeScale *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SHADOW:
                        geo_process_shadow((struct GraphNodeShadow *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT_PARENT:
                        geo_process_object_parent((struct GraphNodeObjectParent *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_GENERATED_LIST:
                        geo_process_generated_list((struct GraphNodeGenerated *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BACKGROUND:
                        geo_process_background((struct GraphNodeBackground *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_HELD_OBJ:
                        geo_process_held_object((struct GraphNodeHeldObject *) curGraphNode);
                        break;
                    default:
                        geo_try_process_children((struct GraphNode *) curGraphNode);
                        break;
                }
            }
        } else {
            if (curGraphNode->type == GRAPH_NODE_TYPE_OBJECT) {
                ((struct GraphNodeObject *) curGraphNode)->throwMatrix = NULL;
            }
        }
        sTagNode = prevTagNode;
    } while (iterateChildren && (curGraphNode = curGraphNode->next) != firstNode);
}

// The skybox (skybox.c). SKYBOX_* are skybox.c's.
#define SKYBOX_WIDTH (4 * SCREEN_WIDTH)
#define SKYBOX_HEIGHT (4 * SCREEN_HEIGHT)
#define SKYBOX_TILE_WIDTH (SCREEN_WIDTH / 2)
#define SKYBOX_TILE_HEIGHT (SCREEN_HEIGHT / 2)
#define SKYBOX_COLS (10)
#define SKYBOX_ROWS (8)

// The game's 3 rows of tiles and a ring around them: 5. The columns are widescreen_sky_columns's, 5 at 4:3.
#define SKYBOX_GRID 5

struct Skybox {
    u16 yaw;
    s16 pitch;
    s32 scaledX;
    s32 scaledY;
    s32 upperLeftTile;
};
extern struct Skybox sSkyBoxInfo[2];

typedef const u8 *const SkyboxTexture[80];
extern SkyboxTexture *sSkyboxTextures[10];
extern u8 sSkyboxColors[][3];

// How many times the scroll has wrapped, so that it moves on from SKYBOX_WIDTH to past it rather than back to 0.
static s32 sSkyWraps;
static f32 sSkyLastX;

static Vtx *make_skybox_tile(s32 col, s32 row, s8 colorIndex) {
    Vtx *verts = alloc_display_list(4 * sizeof(*verts));
    s16 x = col * SKYBOX_TILE_WIDTH;
    s16 y = SKYBOX_HEIGHT - row * SKYBOX_TILE_HEIGHT;
    u8 r = sSkyboxColors[colorIndex][0];
    u8 g = sSkyboxColors[colorIndex][1];
    u8 b = sSkyboxColors[colorIndex][2];

    if (verts != NULL) {
        make_vertex(verts, 0, x, y, -1, 0, 0, r, g, b, 255);
        make_vertex(verts, 1, x, y - SKYBOX_TILE_HEIGHT, -1, 0, 31 << 5, r, g, b, 255);
        make_vertex(verts, 2, x + SKYBOX_TILE_WIDTH, y - SKYBOX_TILE_HEIGHT, -1, 31 << 5, 31 << 5, r, g, b, 255);
        make_vertex(verts, 3, x + SKYBOX_TILE_WIDTH, y, -1, 31 << 5, 0, r, g, b, 255);
    }
    return verts;
}

// The game's create_skybox_facing_camera, init_skybox_display_list, create_skybox_ortho_matrix and
// draw_skybox_tile_grid in one, with the wider grid and the unwrapped scroll. A tile's column is any whole number: its
// texture is the column modulo 8, as the game's 10-column tile lists repeat their first 2 columns. A row out of the
// image is left out: the scroll stops before one is on screen.
//
// player and background are s8 in the game (game/skybox.h, left out here for that reason) but taken as words and
// narrowed here. IDO narrows a char or short argument in the function that takes it, so its callers pass whatever the
// register holds: geo_skybox_main passes the background node's word, which holds the background twice
// (init_graph_node_background), and clang's code leaves the narrowing to the caller, so it would index the sky's lists
// with 0x40004 for background 4. Only background 0, the castle grounds' and Bob-omb Battlefield's sky, drew right.
RECOMP_PATCH Gfx *create_skybox_facing_camera(s32 playerWord, s32 backgroundWord, f32 fov, f32 posX, f32 posY, f32 posZ,
                                              f32 focX, f32 focY, f32 focZ) {
    s8 player = (s8) playerWord;
    s8 background = (s8) backgroundWord;
    f32 cameraFaceX = focX - posX;
    f32 cameraFaceY = focY - posY;
    f32 cameraFaceZ = focZ - posZ;
    s8 colorIndex = 1;
    s32 dlCommandCount;
    s32 columns;
    Gfx *skybox;
    Gfx *dlist;
    Mtx *ortho;
    f32 scrollX, scrollY;
    s32 firstCol, firstRow, row, col;

    if (background == 8
        && !(save_file_get_star_flags(gCurrSaveFileNum - 1, COURSE_NUM_TO_INDEX(COURSE_JRB)) & (1 << 0))) {
        colorIndex = 0;
    }

    // The game's skybox yaw and pitch, as skybox.c keeps them for player 0, which is all the game draws.
    fov = 90.0f;
    sSkyBoxInfo[player].yaw = atan2s(cameraFaceZ, cameraFaceX);
    sSkyBoxInfo[player].pitch = atan2s(sqrtf(cameraFaceX * cameraFaceX + cameraFaceZ * cameraFaceZ), cameraFaceY);
    sSkyBoxInfo[player].scaledX = calculate_skybox_scaled_x(player, fov);
    sSkyBoxInfo[player].scaledY = calculate_skybox_scaled_y(player, fov);

    scrollX = sSkyBoxInfo[player].scaledX + sSkyWraps * SKYBOX_WIDTH;
    if (scrollX - sSkyLastX > SKYBOX_WIDTH / 2) {
        sSkyWraps--;
        scrollX -= SKYBOX_WIDTH;
    } else if (sSkyLastX - scrollX > SKYBOX_WIDTH / 2) {
        sSkyWraps++;
        scrollX += SKYBOX_WIDTH;
    }
    // Far from 0 the tiles' 16-bit positions would overflow: start again at the game's own scroll, as a cut, which
    // takes one turn after another the same way, about 4.
    if (sSkyWraps > 4 || sSkyWraps < -4) {
        sSkyWraps = 0;
        scrollX = sSkyBoxInfo[player].scaledX;
        sCutCount++;
    }
    sSkyLastX = scrollX;
    scrollY = sSkyBoxInfo[player].scaledY;

    // The game's top left tile is the one at the scroll; the ring starts one before it, and a screen wider than 4:3
    // (widescreen.c) needs more columns.
    columns = widescreen_sky_columns(scrollX, &firstCol);
    dlCommandCount = 5 + (SKYBOX_GRID * columns) * 7;
    skybox = alloc_display_list(dlCommandCount * sizeof(Gfx));
    ortho = alloc_display_list(sizeof(*ortho));
    if (skybox == NULL || ortho == NULL) {
        return NULL;
    }
    dlist = skybox;
    guOrtho(ortho, scrollX, scrollX + SCREEN_WIDTH, scrollY - SCREEN_HEIGHT, scrollY, 0.0f, 3.0f, 1.0f);

    gSPDisplayList(dlist++, dl_skybox_begin);
    gSPMatrix(dlist++, VIRTUAL_TO_PHYSICAL(ortho), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPDisplayList(dlist++, dl_skybox_tile_tex_settings);

    firstRow = (SKYBOX_HEIGHT - (s32) scrollY) / SKYBOX_TILE_HEIGHT - 1;
    for (row = firstRow; row < firstRow + SKYBOX_GRID; row++) {
        if (row < 0 || row >= SKYBOX_ROWS) {
            continue;
        }
        for (col = firstCol; col < firstCol + columns; col++) {
            s32 texCol = ((col % 8) + 8) % 8;
            const u8 *const texture =
                (*(SkyboxTexture *) segmented_to_virtual(sSkyboxTextures[background]))[row * SKYBOX_COLS + texCol];
            Vtx *vertices = make_skybox_tile(col, row, colorIndex);

            gLoadBlockTexture(dlist++, 32, 32, G_IM_FMT_RGBA, texture);
            gSPVertex(dlist++, VIRTUAL_TO_PHYSICAL(vertices), 4, 0);
            gSPDisplayList(dlist++, dl_draw_quad_verts_0123);
        }
    }
    gSPDisplayList(dlist++, dl_skybox_end);
    gSPEndDisplayList(dlist);
    return skybox;
}

// The HUD's power meter (hud.c).
struct PowerMeterHUD {
    s8 animation;
    s16 x;
    s16 y;
    f32 unused;
};
extern struct PowerMeterHUD sPowerMeterHUD;
void render_power_meter_health_segment(s16 numHealthWedges);
extern Gfx dl_power_meter_base[];
extern Gfx dl_power_meter_health_segments_begin[];
extern Gfx dl_power_meter_health_segments_end[];

#define POWER_METER_ID 0x48554450 // "HUDP"

RECOMP_PATCH void render_dl_power_meter(s16 numHealthWedges) {
    Mtx *mtx = alloc_display_list(sizeof(Mtx));

    if (mtx == NULL) {
        return;
    }

    guTranslate(mtx, (f32) sPowerMeterHUD.x, (f32) sPowerMeterHUD.y, 0);

    gEXMatrixGroupTagged(gDisplayListHead++, POWER_METER_ID, G_MTX_MODELVIEW);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx++), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList(gDisplayListHead++, &dl_power_meter_base);

    if (numHealthWedges != 0) {
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_begin);
        render_power_meter_health_segment(numHealthWedges);
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_end);
    }

    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
    gEXPopMatrixGroup(gDisplayListHead++, G_MTX_MODELVIEW);
}

// The warp transitions: a star, circle, Mario or Bowser shaped hole that shrinks or grows, made of vertices the game
// moves each frame under one fixed matrix, so its group blends the vertices themselves (the same count every frame).
#define TRANSITION_ID 0x5452414E // "TRAN"

s32 render_fade_transition_from_color(s8 fadeTimer, u8 transTime, struct WarpTransitionData *transData);
s32 render_fade_transition_into_color(s8 fadeTimer, u8 transTime, struct WarpTransitionData *transData);
s32 render_textured_transition(s8 fadeTimer, s8 transTime, struct WarpTransitionData *transData, s8 texID, s8 transTexType);

RECOMP_PATCH s32 render_screen_transition(s8 fadeTimer, s8 transType, u8 transTime, struct WarpTransitionData *transData) {
    s32 done = 0;

    gEXMatrixGroupDecomposed(gDisplayListHead++, TRANSITION_ID, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_AUTO,
                             G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
    switch (transType) {
        case WARP_TRANSITION_FADE_FROM_COLOR:
            done = render_fade_transition_from_color(fadeTimer, transTime, transData);
            break;
        case WARP_TRANSITION_FADE_INTO_COLOR:
            done = render_fade_transition_into_color(fadeTimer, transTime, transData);
            break;
        case WARP_TRANSITION_FADE_FROM_STAR:
        case WARP_TRANSITION_FADE_INTO_STAR:
            done = render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_STAR, TRANS_TYPE_MIRROR);
            break;
        case WARP_TRANSITION_FADE_FROM_CIRCLE:
        case WARP_TRANSITION_FADE_INTO_CIRCLE:
            done = render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_CIRCLE, TRANS_TYPE_MIRROR);
            break;
        case WARP_TRANSITION_FADE_FROM_MARIO:
        case WARP_TRANSITION_FADE_INTO_MARIO:
            done = render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_MARIO, TRANS_TYPE_CLAMP);
            break;
        case WARP_TRANSITION_FADE_FROM_BOWSER:
        case WARP_TRANSITION_FADE_INTO_BOWSER:
            done = render_textured_transition(fadeTimer, transTime, transData, TEX_TRANS_BOWSER, TRANS_TYPE_MIRROR);
            break;
    }
    gEXPopMatrixGroup(gDisplayListHead++, G_MTX_MODELVIEW);
    return done;
}

// The dialogs and menus (ingame_menu.c): a box, then a matrix per letter, each a translation multiplied onto the last.
// Untagged, RT64 pairs a letter with any letter of the older frame drawn with the same texture, so of a line's two o's
// each could be blended toward the other and draw between the two, on another letter or out of sight. Here they are one
// group paired in the order they are drawn, and what changes that order (another dialog or menu, its state, page, scroll
// or cursor line) changes the id, so that frame is drawn as it is.
#define MENUS_ID 0x4D454E55 // "MENU"

extern s16 gMenuMode;
extern s16 gDialogID;
extern s8 gMenuState;
extern s8 gMenuLineNum;
extern s16 gDialogPageStartStrIndex;
extern s16 gDialogScrollOffsetY;
s16 render_pause_screen(void);
s16 render_course_complete_screen(void);
void print_peach_letter_message(void);
void render_dialog_entries(void);

static u32 menus_id(void) {
    return make_id(MENUS_ID, ((u32) (u16) gMenuMode << 16) | (u16) gDialogID,
                   ((u32) (u8) gMenuState << 24) ^ ((u32) (u8) gMenuLineNum << 16) ^
                       ((u32) (u16) gDialogPageStartStrIndex << 8) ^ (u16) gDialogScrollOffsetY);
}

// The game's, inside the group.
RECOMP_PATCH s16 render_menus_and_dialogs(void) {
    s16 index = MENU_OPT_NONE;

    create_dl_ortho_matrix();
    gEXMatrixGroupTagged(gDisplayListHead++, menus_id(), G_MTX_MODELVIEW);

    if (gMenuMode != MENU_MODE_NONE) {
        switch (gMenuMode) {
            case MENU_MODE_UNUSED_0:
                index = render_pause_screen();
                break;
            case MENU_MODE_RENDER_PAUSE_SCREEN:
                index = render_pause_screen();
                break;
            case MENU_MODE_RENDER_COURSE_COMPLETE_SCREEN:
                index = render_course_complete_screen();
                break;
            case MENU_MODE_UNUSED_3:
                index = render_course_complete_screen();
                break;
        }

        gMenuTextColorTransTimer = (s16) gMenuTextColorTransTimer + 0x1000;
    } else if (gDialogID != DIALOG_NONE) {
        // The Peach "Dear Mario" message needs to be repositioned separately
        if (gDialogID == DIALOG_020) {
            print_peach_letter_message();
        } else {
            render_dialog_entries();
            gMenuTextColorTransTimer = (s16) gMenuTextColorTransTimer + 0x1000;
        }
    }

    gEXPopMatrixGroup(gDisplayListHead++, G_MTX_MODELVIEW);
    return index;
}

// The display list pool: the game's two GfxPools keep their tasks, the lists go here.
#define EXT_GFX_POOL_SIZE (GFX_POOL_SIZE * 4)
static Gfx sGfxPoolsExt[2][EXT_GFX_POOL_SIZE];
static Gfx *sGfxPoolBase;

RECOMP_PATCH void select_gfx_pool(void) {
    Gfx *buffer = sGfxPoolsExt[gGlobalTimer % 2];

    gGfxPool = &gGfxPools[gGlobalTimer % 2];
    set_segment_base_addr(1, buffer);
    gGfxSPTask = &gGfxPool->spTask;
    gDisplayListHead = buffer;
    gGfxPoolEnd = (u8 *) (buffer + EXT_GFX_POOL_SIZE);
    sGfxPoolBase = buffer;
    gEXEnable(gDisplayListHead++);
}

// The game's, with the list's start and length taken from the pool above (render_init's first frame uses the game's).
RECOMP_PATCH void create_gfx_task_structure(void) {
    Gfx *base = sGfxPoolBase != NULL ? sGfxPoolBase : gGfxPool->buffer;
    s32 entries = gDisplayListHead - base;

    gGfxSPTask->msgqueue = &gGfxVblankQueue;
    gGfxSPTask->msg = (OSMesg) 2;
    gGfxSPTask->task.t.type = M_GFXTASK;
    gGfxSPTask->task.t.ucode_boot = rspF3DBootStart;
    gGfxSPTask->task.t.ucode_boot_size = ((u8 *) rspF3DBootEnd - (u8 *) rspF3DBootStart);
    gGfxSPTask->task.t.flags = 0;
    gGfxSPTask->task.t.ucode = rspF3DStart;
    gGfxSPTask->task.t.ucode_data = rspF3DDataStart;
    gGfxSPTask->task.t.ucode_size = SP_UCODE_SIZE;
    gGfxSPTask->task.t.ucode_data_size = SP_UCODE_DATA_SIZE;
    gGfxSPTask->task.t.dram_stack = (u64 *) gGfxSPTaskStack;
    gGfxSPTask->task.t.dram_stack_size = SP_DRAM_STACK_SIZE8;
    gGfxSPTask->task.t.output_buff = gGfxSPTaskOutputBuffer;
    gGfxSPTask->task.t.output_buff_size = (u64 *) ((u8 *) gGfxSPTaskOutputBuffer + sizeof(gGfxSPTaskOutputBuffer));
    gGfxSPTask->task.t.data_ptr = (u64 *) base;
    gGfxSPTask->task.t.data_size = entries * sizeof(Gfx);
    gGfxSPTask->task.t.yield_data_ptr = (u64 *) gGfxSPTaskYieldBuffer;
    gGfxSPTask->task.t.yield_data_size = OS_YIELD_DATA_SIZE;
    // The list is complete: Hi-res Font makes what it draws new (src/game/hires_font.cpp).
    recomp_hires_font_scan(base);
}
