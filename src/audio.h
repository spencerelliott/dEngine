/*
 *  audio.h
 *  dEngine
 *
 *  Simple audio API built on top of the KallistiOS sound support.
 *
 *  Two kinds of sounds are handled:
 *
 *  - Clips: short sound effects that are preloaded in the audio processor
 *    (SPU) RAM once, and can then be triggered as many times as needed,
 *    either as a one shot or as a looping sound. Clips are loaded from WAV
 *    files (8/16 bit PCM or 4 bit Yamaha ADPCM) and, because of an AICA
 *    limitation, cannot be longer than 65534 samples.
 *
 *  - Music: a single background track streamed straight from the disc, so
 *    the whole track never has to fit in memory. Music is loaded from a WAV
 *    file as well (same formats as clips) and can be looped forever.
 *
 *  Audio_Update() must be called once per frame: this is what feeds the
 *  streaming music to the SPU.
 */

#ifndef DE_AUDIO
#define DE_AUDIO

#include "globals.h"

#define AUDIO_MAX_CLIPS         32

#define AUDIO_INVALID_CLIP      (-1)
#define AUDIO_INVALID_VOICE     (-1)

#define AUDIO_VOLUME_MAX        255
#define AUDIO_PAN_LEFT          0
#define AUDIO_PAN_CENTER        128
#define AUDIO_PAN_RIGHT         255

/* Handle on a clip preloaded in the audio processor memory. */
typedef int audioClip_t;

/* Handle on a playing sound (an audio processor channel). */
typedef int audioVoice_t;

void Audio_Init(void);
void Audio_Shutdown(void);

/* Feeds the streaming music, must be called once per frame. */
void Audio_Update(void);

/*
 * Clips.
 */

/* Preloads a WAV file in the audio processor memory. Loading a clip that is
   already loaded does not load it twice, the existing handle is returned. */
audioClip_t Audio_LoadClip(const char* filename);

/* Returns the handle of an already loaded clip, AUDIO_INVALID_CLIP if the
   clip was never loaded. */
audioClip_t Audio_FindClip(const char* filename);

void Audio_UnloadClip(audioClip_t clip);
void Audio_UnloadAllClips(void);

/* Plays a preloaded clip once, at full volume, centered. */
audioVoice_t Audio_PlayClip(audioClip_t clip);

/* Plays a preloaded clip over and over until Audio_StopVoice() is called. */
audioVoice_t Audio_LoopClip(audioClip_t clip);

/* volume: 0 to AUDIO_VOLUME_MAX, pan: AUDIO_PAN_LEFT to AUDIO_PAN_RIGHT
   (pan is ignored for stereo clips), loop: 0 for a one shot. */
audioVoice_t Audio_PlayClipEx(audioClip_t clip, int volume, int pan, uchar loop);

void Audio_StopVoice(audioVoice_t voice);
void Audio_StopAllClips(void);

/* Master volume applied to every clip played afterwards. */
void Audio_SetClipVolume(int volume);
int  Audio_GetClipVolume(void);

/*
 * Music.
 */

/* Streams a WAV file from the disc. Any music already playing is stopped.
   Returns 1 if the track could be started. */
uchar Audio_PlayMusic(const char* filename, uchar loop);

void  Audio_StopMusic(void);
void  Audio_PauseMusic(void);
void  Audio_ResumeMusic(void);
uchar Audio_IsMusicPlaying(void);

void Audio_SetMusicVolume(int volume);
int  Audio_GetMusicVolume(void);

#endif
