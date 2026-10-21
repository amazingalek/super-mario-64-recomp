// The game thread's waits on the sound thread.
//
// wait_for_audio_frames (src/audio/external.c) spins until the sound thread has made the given number of audio
// frames. On the console the sound thread, of higher priority, preempts it; ultramodern runs one game thread at a
// time and preempts none, so the loop would spin forever. This one yields to ultramodern on every pass (recomp_yield),
// which also makes each read of gAudioFrameCount a fresh one.

#include "patches.h"

extern volatile s32 gAudioFrameCount;

RECOMP_PATCH void wait_for_audio_frames(s32 frames) {
    gAudioFrameCount = 0;
    while (gAudioFrameCount < frames) {
        recomp_yield();
    }
}
