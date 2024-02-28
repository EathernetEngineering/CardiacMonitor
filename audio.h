#ifndef CEE_AUDIO_H_
#define CEE_AUDIO_H_

#include <alloca.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _ceeAudioPlayer ceeAudioPlayer;
typedef struct _ceeAudioStream ceeAudioInStream;
typedef struct _ceeAudioStream ceeAudioOutStream;

ceeAudioPlayer* ceeAudioAllocatePlayer();
ceeAudioInStream* ceeAudioAllocateInStream();
ceeAudioOutStream* ceeAudioAllocateOutStream();
void ceeAudioFreePlayer(ceeAudioPlayer* player);
void ceeAudioFreeInStream(ceeAudioInStream* stream);
void ceeAudioFreeOutStream(ceeAudioOutStream* stream);

void ceeAudioInitialize(ceeAudioPlayer* player);
void ceeAudioShutdown(ceeAudioPlayer* player);

void ceeAudioOpenWav(ceeAudioPlayer* player, ceeAudioOutStream* stream, const char* filename);

void ceeAudioPlay(ceeAudioPlayer* player, ceeAudioOutStream* stream);

#if defined(__cplusplus)
}
#endif

#endif

