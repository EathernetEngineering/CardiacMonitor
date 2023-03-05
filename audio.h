#ifndef _AUDIO_H
#define _AUDIO_H

#include <alloca.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _ceeAudioState ceeAudioState;

ceeAudioState* ceeAudioMallocState();
void ceeAudioFreeState(ceeAudioState* state);

void ceeAudioInitialize(ceeAudioState* state);
void ceeAudioShutdown(ceeAudioState* state);

void ceeAudioOpenWav(ceeAudioState* state, const char* filename);

void ceeAudioPlay(ceeAudioState* state);

#if defined(__cplusplus)
}
#endif

#endif

