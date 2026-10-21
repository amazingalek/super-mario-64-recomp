// Goddard's load_dynlist (src/goddard/renderer.c) without the TLB, which the runtime doesn't have.
//
// The title screen's Mario head is built from dynlists in their own ROM segment, linked at the virtual address
// 0x04000000 and full of pointers into itself. The game DMAs the segment into a temporary buffer, maps 0x04000000 onto
// that buffer with osMapTLB, runs the lists (proc_dynlist, which copies what it keeps), then frees the buffer. Here the
// buffer is relocated instead: every word in it that points into the segment is moved to point into the buffer. In
// US 1.0's segment every aligned word in that range is a pointer the linker put there (checked against the decomp's
// relocations: 118 of them, no animation value or vertex among them), so this is exact for it.

#include "patches.h"

struct DynList;
struct GdObj;

struct GdObj* proc_dynlist(struct DynList* dylist);
void* gd_malloc_temp(u32 size);
void gd_free(void* ptr);
void gd_block_dma(u32 romAddr, void* vAddr, s32 size);
void fatal_printf(const char* fmt, ...);

extern u8 _gd_dynlistsSegmentStart[];
extern u8 _gd_dynlistsSegmentEnd[];
extern u8 _gd_dynlistsSegmentRomStart[];
extern u8 _gd_dynlistsSegmentRomEnd[];

RECOMP_PATCH struct GdObj* load_dynlist(struct DynList* dynlist) {
    u32 segStart = (u32) _gd_dynlistsSegmentStart;
    u32 segEnd = (u32) _gd_dynlistsSegmentEnd;
    u32 segSize = _gd_dynlistsSegmentRomEnd - _gd_dynlistsSegmentRomStart;
    u8* seg = gd_malloc_temp(segSize);
    u32* word;
    struct GdObj* loadedList;

    if (seg == NULL) {
        fatal_printf("Not enough DRAM for DATA segment \n");
    }

    gd_block_dma((u32) _gd_dynlistsSegmentRomStart, seg, segSize);

    // @recomp Relocate the segment to where it was loaded, in place of mapping it at its own address.
    for (word = (u32*) seg; word < (u32*) (seg + segSize); word++) {
        if (*word >= segStart && *word < segEnd) {
            *word += (u32) seg - segStart;
        }
    }

    loadedList = proc_dynlist((struct DynList*) (seg + ((u32) dynlist - segStart)));

    gd_free(seg);

    return loadedList;
}
