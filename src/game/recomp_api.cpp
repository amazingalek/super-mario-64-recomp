// The natives the patches call (patches/syms.ld gives each a dummy address that N64Recomp turns into a call by name),
// and the libultra functions the game calls that N64Recomp leaves out and the runtime doesn't have.

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "librecomp/helpers.hpp"

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
