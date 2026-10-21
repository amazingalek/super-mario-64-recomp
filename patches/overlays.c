#include "patches.h"

// The runtime registers the code inside ROM 0x1000 plus 1 MB at startup, which is main, and any other code only when
// it is told it was loaded (recomp_load_overlays). Every read from the ROM comes through dma_read, so it tells the
// runtime about the engine segment, which load_engine_code_segment() reads once at boot, and goddard (the title
// screen, file select and Mario's head), which the level scripts' FIXED_LOAD reads each time they reach the title or
// the file select. A read holding no code registers nothing.

// As src/game/memory.c defines it.
#define ALIGN16(val) (((val) + 0xF) & ~0xF)

extern OSIoMesg gDmaIoMesg;
extern OSMesg gMainReceivedMesg;
extern OSMesgQueue gDmaMesgQueue;

RECOMP_PATCH void dma_read(u8* dest, u8* srcStart, u8* srcEnd) {
    u32 size = ALIGN16(srcEnd - srcStart);

    // @recomp Register the code this brings in.
    recomp_load_overlays((u32) srcStart, dest, size);

    osInvalDCache(dest, size);
    while (size != 0) {
        u32 copySize = (size >= 0x1000) ? 0x1000 : size;

        osPiStartDma(&gDmaIoMesg, OS_MESG_PRI_NORMAL, OS_READ, (uintptr_t) srcStart, dest, copySize, &gDmaMesgQueue);
        osRecvMesg(&gDmaMesgQueue, &gMainReceivedMesg, OS_MESG_BLOCK);

        dest += copySize;
        srcStart += copySize;
        size -= copySize;
    }
}
