/*
 *  audio.c
 *  dEngine
 *
 *  Clip and music playback on top of the KallistiOS sound API.
 *
 *  Clips go through the sound effect manager: the whole WAV is uploaded to
 *  the audio processor memory once and every playback is just a command sent
 *  to the AICA. Music goes through the sound stream driver: only a small
 *  buffer is kept in memory and refilled from the disc every frame from
 *  Audio_Update().
 */

#include "audio.h"
#include "filesystem.h"

#ifdef DREAMCAST

#include <malloc.h>

#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/stream.h>

/* Number of channels the AICA offers. */
#define AUDIO_MAX_VOICES            64

/* Size of one channel of the music buffer. The sound stream driver asks for
   at most that many bytes per channel every time it needs to be fed. */
#define AUDIO_MUSIC_BUFFER_SIZE     (16 * 1024)

/* Sound effects are limited to that many samples by the AICA. */
#define AUDIO_MAX_CLIP_SAMPLES      65534

/* How many frames to keep streaming silence before considering that a non
   looping track has been played all the way through. */
#define AUDIO_MUSIC_DRAIN_FRAMES    30

/* WAV sample formats we know how to feed to the audio processor. */
#define WAV_FORMAT_PCM                  0x0001
#define WAV_FORMAT_YAMAHA_ADPCM_G723    0x0014
#define WAV_FORMAT_YAMAHA_ADPCM         0x0020

typedef struct audioWavInfo_t {
    uint    rate;
    uint    dataStart;      /* offset of the first sample byte in the file */
    uint    dataLength;     /* length of the sample data, in bytes */
    uchar   channels;
    uchar   bits;           /* 16 or 8 for PCM, 4 for Yamaha ADPCM */
} audioWavInfo_t;

typedef struct audioClipSlot_t {
    char        path[MAX_GAMEPATH];
    sfxhnd_t    handle;
    uint        rate;
    uint        samples;
    uchar       channels;
    uchar       loaded;
} audioClipSlot_t;

typedef struct audioMusic_t {
    snd_stream_hnd_t    stream;
    FILE*               file;
    uchar*              buffer;
    uint                bufferSize;

    uint                dataStart;
    uint                dataLength;
    uint                dataRead;

    uint                rate;
    uchar               channels;
    uchar               bits;

    uchar               loop;
    uchar               playing;
    uchar               paused;
    uchar               finished;
    uint                drainFrames;
} audioMusic_t;

static audioClipSlot_t  clips[AUDIO_MAX_CLIPS];
static audioClip_t      voiceClips[AUDIO_MAX_VOICES];
static audioMusic_t     music;

static uchar    audioInitialized = 0;
static uchar    musicEnabled = 0;
static int      clipVolume = AUDIO_VOLUME_MAX;
static int      musicVolume = AUDIO_VOLUME_MAX;

static ushort Audio_ReadU16(const uchar* bytes)
{
    return (ushort)(bytes[0] | (bytes[1] << 8));
}

static uint Audio_ReadU32(const uchar* bytes)
{
    return (uint)bytes[0] | ((uint)bytes[1] << 8) | ((uint)bytes[2] << 16) | ((uint)bytes[3] << 24);
}

static void Audio_BuildPath(const char* filename, char* fullPath)
{
    snprintf(fullPath, MAX_OSPATH, "%s/%s", FS_Gamedir(), filename);
}

static int Audio_ClampVolume(int volume)
{
    if (volume < 0)
        return 0;

    if (volume > AUDIO_VOLUME_MAX)
        return AUDIO_VOLUME_MAX;

    return volume;
}

static int Audio_ClampPan(int pan)
{
    if (pan < AUDIO_PAN_LEFT)
        return AUDIO_PAN_LEFT;

    if (pan > AUDIO_PAN_RIGHT)
        return AUDIO_PAN_RIGHT;

    return pan;
}

/* Number of samples held in a block of WAV data. */
static uint Audio_CountSamples(uint dataLength, uchar channels, uchar bits)
{
    if (!channels)
        return 0;

    if (bits >= 8)
        return dataLength / ((bits / 8) * channels);

    /* Yamaha ADPCM packs two samples per byte. */
    return (dataLength * 2) / channels;
}

/*
 * Walks the chunks of an opened WAV file and collects everything needed to
 * play it: sample format and where the sample data lives in the file.
 */
static uchar Audio_ReadWavInfo(FILE* file, audioWavInfo_t* info)
{
    uchar chunk[16];
    uint fileSize;
    uint chunkSize;
    ushort format;
    uchar gotFormat = 0;

    memset(info, 0, sizeof(audioWavInfo_t));

    if (fseek(file, 0, SEEK_END))
        return 0;

    fileSize = (uint)ftell(file);
    rewind(file);

    if (fread(chunk, 1, 12, file) != 12)
        return 0;

    if (memcmp(chunk, "RIFF", 4) || memcmp(chunk + 8, "WAVE", 4))
    {
        printf("[Audio] Not a RIFF WAVE file.\n");
        return 0;
    }

    while (fread(chunk, 1, 8, file) == 8)
    {
        chunkSize = Audio_ReadU32(chunk + 4);

        if (!memcmp(chunk, "fmt ", 4))
        {
            if (chunkSize < 16 || fread(chunk, 1, 16, file) != 16)
                return 0;

            format = Audio_ReadU16(chunk);
            info->channels = (uchar)Audio_ReadU16(chunk + 2);
            info->rate = Audio_ReadU32(chunk + 4);

            if (format == WAV_FORMAT_PCM)
                info->bits = (uchar)Audio_ReadU16(chunk + 14);
            else if (format == WAV_FORMAT_YAMAHA_ADPCM || format == WAV_FORMAT_YAMAHA_ADPCM_G723)
                info->bits = 4;
            else
            {
                printf("[Audio] Unsupported WAV sample format 0x%04x.\n", format);
                return 0;
            }

            gotFormat = 1;

            /* Skip whatever the fmt chunk holds beyond the fields we read. */
            if (chunkSize > 16 && fseek(file, chunkSize - 16, SEEK_CUR))
                return 0;
        }
        else if (!memcmp(chunk, "data", 4))
        {
            info->dataStart = (uint)ftell(file);
            info->dataLength = chunkSize;
            break;
        }
        else
        {
            /* Chunks are word aligned. */
            if (fseek(file, chunkSize + (chunkSize & 1), SEEK_CUR))
                return 0;
        }
    }

    if (!gotFormat || !info->dataLength)
    {
        printf("[Audio] WAV file is missing a fmt or a data chunk.\n");
        return 0;
    }

    /* Do not trust the announced size more than the file itself. */
    if (info->dataStart >= fileSize)
        return 0;

    if (info->dataLength > fileSize - info->dataStart)
        info->dataLength = fileSize - info->dataStart;

    if (info->channels < 1 || info->channels > 2)
    {
        printf("[Audio] Only mono and stereo WAV files are supported (%d channels).\n", info->channels);
        return 0;
    }

    if (info->bits != 4 && info->bits != 8 && info->bits != 16)
    {
        printf("[Audio] Only 4, 8 and 16 bits WAV files are supported (%d bits).\n", info->bits);
        return 0;
    }

    return 1;
}

/*
 * Clips.
 */

static audioClipSlot_t* Audio_GetClipSlot(audioClip_t clip)
{
    if (clip < 0 || clip >= AUDIO_MAX_CLIPS)
        return NULL;

    if (!clips[clip].loaded)
        return NULL;

    return &clips[clip];
}

audioClip_t Audio_FindClip(const char* filename)
{
    int i;

    if (!filename)
        return AUDIO_INVALID_CLIP;

    for (i = 0; i < AUDIO_MAX_CLIPS; i++)
    {
        if (clips[i].loaded && !strcmp(clips[i].path, filename))
            return i;
    }

    return AUDIO_INVALID_CLIP;
}

audioClip_t Audio_LoadClip(const char* filename)
{
    char fullPath[MAX_OSPATH];
    audioWavInfo_t info;
    audioClipSlot_t* slot;
    audioClip_t clip;
    sfxhnd_t handle;
    FILE* file;
    uint samples;
    int i;

    if (!audioInitialized || !filename || !filename[0])
        return AUDIO_INVALID_CLIP;

    /* Already in the audio processor memory ? */
    clip = Audio_FindClip(filename);

    if (clip != AUDIO_INVALID_CLIP)
        return clip;

    for (i = 0; i < AUDIO_MAX_CLIPS; i++)
    {
        if (!clips[i].loaded)
        {
            clip = i;
            break;
        }
    }

    if (clip == AUDIO_INVALID_CLIP)
    {
        printf("[Audio] Clip library is full (%d clips), '%s' was not loaded.\n", AUDIO_MAX_CLIPS, filename);
        return AUDIO_INVALID_CLIP;
    }

    Audio_BuildPath(filename, fullPath);

    /* Read the header ourselves: the sound effect manager does not tell us
       anything about the clip once it is loaded, and we need to know whether
       it is stereo in order to stop it later. */
    file = fopen(fullPath, "rb");

    if (!file)
    {
        printf("[Audio] Clip '%s' cannot be opened.\n", fullPath);
        return AUDIO_INVALID_CLIP;
    }

    if (!Audio_ReadWavInfo(file, &info))
    {
        printf("[Audio] Clip '%s' cannot be decoded.\n", fullPath);
        fclose(file);
        return AUDIO_INVALID_CLIP;
    }

    fclose(file);

    samples = Audio_CountSamples(info.dataLength, info.channels, info.bits);

    if (samples > AUDIO_MAX_CLIP_SAMPLES)
    {
        printf("[Audio] Clip '%s' is %u samples long, it will be cut down to %d samples. Stream it as music instead.\n",
               filename, samples, AUDIO_MAX_CLIP_SAMPLES);
        samples = AUDIO_MAX_CLIP_SAMPLES;
    }

    handle = snd_sfx_load(fullPath);

    if (handle == SFXHND_INVALID)
    {
        printf("[Audio] Clip '%s' cannot be uploaded to the audio processor.\n", fullPath);
        return AUDIO_INVALID_CLIP;
    }

    slot = &clips[clip];

    strncpy(slot->path, filename, MAX_GAMEPATH - 1);
    slot->path[MAX_GAMEPATH - 1] = '\0';

    slot->handle = handle;
    slot->rate = info.rate;
    slot->samples = samples;
    slot->channels = info.channels;
    slot->loaded = 1;

    printf("[Audio] Loaded clip '%s' (%u Hz, %s, %d bits, %u samples).\n",
           filename, info.rate, info.channels == 1 ? "mono" : "stereo", info.bits, samples);

    return clip;
}

void Audio_UnloadClip(audioClip_t clip)
{
    audioClipSlot_t* slot;
    int i;

    slot = Audio_GetClipSlot(clip);

    if (!slot)
        return;

    /* Whatever is still playing this clip is about to reference audio
       processor memory that does not belong to us anymore. */
    for (i = 0; i < AUDIO_MAX_VOICES; i++)
    {
        if (voiceClips[i] == clip)
            Audio_StopVoice(i);
    }

    snd_sfx_unload(slot->handle);

    memset(slot, 0, sizeof(audioClipSlot_t));
}

void Audio_UnloadAllClips(void)
{
    int i;

    if (!audioInitialized)
        return;

    Audio_StopAllClips();

    for (i = 0; i < AUDIO_MAX_CLIPS; i++)
    {
        if (clips[i].loaded)
            snd_sfx_unload(clips[i].handle);
    }

    memset(clips, 0, sizeof(clips));
}

audioVoice_t Audio_PlayClipEx(audioClip_t clip, int volume, int pan, uchar loop)
{
    audioClipSlot_t* slot;
    sfx_play_data_t playData;
    int channel;

    if (!audioInitialized)
        return AUDIO_INVALID_VOICE;

    slot = Audio_GetClipSlot(clip);

    if (!slot)
        return AUDIO_INVALID_VOICE;

    playData.chn = -1;      /* let the sound effect manager pick a channel */
    playData.idx = slot->handle;
    playData.vol = (Audio_ClampVolume(volume) * clipVolume) / AUDIO_VOLUME_MAX;
    playData.pan = Audio_ClampPan(pan);
    playData.loop = loop ? 1 : 0;
    playData.freq = 0;      /* play at the rate the clip was recorded at */
    playData.loopstart = 0;
    playData.loopend = 0;   /* loop over the whole clip */

    channel = snd_sfx_play_ex(&playData);

    if (channel < 0 || channel >= AUDIO_MAX_VOICES)
        return AUDIO_INVALID_VOICE;

    voiceClips[channel] = clip;

    return channel;
}

audioVoice_t Audio_PlayClip(audioClip_t clip)
{
    return Audio_PlayClipEx(clip, AUDIO_VOLUME_MAX, AUDIO_PAN_CENTER, 0);
}

audioVoice_t Audio_LoopClip(audioClip_t clip)
{
    return Audio_PlayClipEx(clip, AUDIO_VOLUME_MAX, AUDIO_PAN_CENTER, 1);
}

void Audio_StopVoice(audioVoice_t voice)
{
    audioClipSlot_t* slot;

    if (!audioInitialized || voice < 0 || voice >= AUDIO_MAX_VOICES)
        return;

    snd_sfx_stop(voice);

    /* Stereo clips are played on two consecutive channels. */
    slot = Audio_GetClipSlot(voiceClips[voice]);

    if (slot && slot->channels == 2 && voice + 1 < AUDIO_MAX_VOICES)
        snd_sfx_stop(voice + 1);

    voiceClips[voice] = AUDIO_INVALID_CLIP;
}

void Audio_StopAllClips(void)
{
    int i;

    if (!audioInitialized)
        return;

    snd_sfx_stop_all();

    for (i = 0; i < AUDIO_MAX_VOICES; i++)
        voiceClips[i] = AUDIO_INVALID_CLIP;
}

void Audio_SetClipVolume(int volume)
{
    clipVolume = Audio_ClampVolume(volume);
}

int Audio_GetClipVolume(void)
{
    return clipVolume;
}

/*
 * Music.
 */

/*
 * Called by the sound stream driver whenever it runs low on data. Despite
 * their name the request and the returned count are both byte counts.
 */
static void* Audio_MusicCallback(snd_stream_hnd_t stream, int requested, int* received)
{
    uint remaining;
    int rewinds = 0;
    int chunk;
    int read;
    int got = 0;

    (void)stream;

    *received = 0;

    if (!music.playing || !music.file || music.paused || music.finished)
        return NULL;

    if (requested < 0)
        return NULL;

    if ((uint)requested > music.bufferSize)
        requested = music.bufferSize;

    while (got < requested)
    {
        remaining = music.dataLength - music.dataRead;

        if (!remaining)
        {
            if (!music.loop || rewinds)
            {
                music.finished = 1;
                break;
            }

            if (fseek(music.file, music.dataStart, SEEK_SET))
            {
                music.finished = 1;
                break;
            }

            music.dataRead = 0;
            remaining = music.dataLength;
            rewinds++;
        }

        chunk = requested - got;

        if ((uint)chunk > remaining)
            chunk = (int)remaining;

        read = (int)fread(music.buffer + got, 1, chunk, music.file);

        if (read <= 0)
        {
            /* The track is shorter than its header claims. */
            music.dataRead = music.dataLength;

            if (!music.loop || rewinds)
            {
                music.finished = 1;
                break;
            }

            continue;
        }

        music.dataRead += read;
        got += read;
    }

    if (!got)
        return NULL;

    *received = got;

    return music.buffer;
}

uchar Audio_PlayMusic(const char* filename, uchar loop)
{
    char fullPath[MAX_OSPATH];
    audioWavInfo_t info;

    if (!audioInitialized || !musicEnabled || !filename || !filename[0])
        return 0;

    Audio_StopMusic();

    Audio_BuildPath(filename, fullPath);

    music.file = fopen(fullPath, "rb");

    if (!music.file)
    {
        printf("[Audio] Music '%s' cannot be opened.\n", fullPath);
        return 0;
    }

    if (!Audio_ReadWavInfo(music.file, &info))
    {
        printf("[Audio] Music '%s' cannot be decoded.\n", fullPath);
        Audio_StopMusic();
        return 0;
    }

    /* Feeding the audio processor whole 32 bytes blocks keeps every transfer
       aligned, the handful of samples dropped at the end of the track are
       not audible. */
    info.dataLength &= ~31u;

    if (!info.dataLength)
    {
        printf("[Audio] Music '%s' is too short to be streamed.\n", fullPath);
        Audio_StopMusic();
        return 0;
    }

    music.dataStart = info.dataStart;
    music.dataLength = info.dataLength;
    music.dataRead = 0;
    music.rate = info.rate;
    music.channels = info.channels;
    music.bits = info.bits;
    music.loop = loop ? 1 : 0;
    music.finished = 0;
    music.paused = 0;
    music.drainFrames = 0;

    if (fseek(music.file, music.dataStart, SEEK_SET))
    {
        printf("[Audio] Music '%s' cannot be rewound to its sample data.\n", fullPath);
        Audio_StopMusic();
        return 0;
    }

    music.bufferSize = AUDIO_MUSIC_BUFFER_SIZE * music.channels;
    music.buffer = (uchar*)memalign(32, music.bufferSize);

    if (!music.buffer)
    {
        printf("[Audio] Not enough memory to stream '%s'.\n", fullPath);
        Audio_StopMusic();
        return 0;
    }

    music.stream = snd_stream_alloc(Audio_MusicCallback, AUDIO_MUSIC_BUFFER_SIZE);

    if (music.stream == SND_STREAM_INVALID)
    {
        printf("[Audio] No sound stream available to play '%s'.\n", fullPath);
        Audio_StopMusic();
        return 0;
    }

    snd_stream_volume(music.stream, musicVolume);

    /* The stream is prefilled on start, everything it needs must be set. */
    music.playing = 1;

    if (music.bits == 16)
        snd_stream_start(music.stream, music.rate, music.channels == 2);
    else if (music.bits == 8)
        snd_stream_start_pcm8(music.stream, music.rate, music.channels == 2);
    else
        snd_stream_start_adpcm(music.stream, music.rate, music.channels == 2);

    printf("[Audio] Streaming music '%s' (%u Hz, %s, %d bits, %s).\n",
           filename, music.rate, music.channels == 1 ? "mono" : "stereo", music.bits,
           music.loop ? "looping" : "one shot");

    return 1;
}

void Audio_StopMusic(void)
{
    if (!audioInitialized)
        return;

    if (music.stream != SND_STREAM_INVALID)
    {
        snd_stream_stop(music.stream);
        snd_stream_destroy(music.stream);
    }

    if (music.file)
        fclose(music.file);

    if (music.buffer)
        free(music.buffer);

    memset(&music, 0, sizeof(audioMusic_t));
    music.stream = SND_STREAM_INVALID;
}

void Audio_PauseMusic(void)
{
    /* The stream keeps running and is fed silence, so that resuming picks up
       exactly where the track was left. */
    music.paused = 1;
}

void Audio_ResumeMusic(void)
{
    music.paused = 0;
}

uchar Audio_IsMusicPlaying(void)
{
    return music.playing && !music.finished;
}

void Audio_SetMusicVolume(int volume)
{
    musicVolume = Audio_ClampVolume(volume);

    if (music.playing)
        snd_stream_volume(music.stream, musicVolume);
}

int Audio_GetMusicVolume(void)
{
    return musicVolume;
}

/*
 * Lifecycle.
 */

void Audio_Init(void)
{
    int i;

    if (audioInitialized)
        return;

    memset(clips, 0, sizeof(clips));
    memset(&music, 0, sizeof(audioMusic_t));

    music.stream = SND_STREAM_INVALID;

    for (i = 0; i < AUDIO_MAX_VOICES; i++)
        voiceClips[i] = AUDIO_INVALID_CLIP;

    /* This brings up the sound system as a whole, streams included. */
    if (snd_stream_init() < 0)
    {
        printf("[Audio] Sound stream driver unavailable, music is disabled.\n");

        if (snd_init() < 0)
        {
            printf("[Audio] Audio processor unavailable, audio is disabled.\n");
            return;
        }
    }
    else
        musicEnabled = 1;

    audioInitialized = 1;

    printf("[Audio] Audio processor initialized.\n");
}

void Audio_Shutdown(void)
{
    if (!audioInitialized)
        return;

    Audio_StopMusic();
    Audio_UnloadAllClips();

    if (musicEnabled)
        snd_stream_shutdown();

    snd_shutdown();

    audioInitialized = 0;
    musicEnabled = 0;
}

void Audio_Update(void)
{
    if (!audioInitialized || !music.playing)
        return;

    /* -3 only means the callback had nothing to hand over, which is what
       happens while paused or once the track is over. */
    if (snd_stream_poll(music.stream) == -1)
    {
        printf("[Audio] Music stream lost, stopping it.\n");
        Audio_StopMusic();
        return;
    }

    if (music.finished && ++music.drainFrames > AUDIO_MUSIC_DRAIN_FRAMES)
        Audio_StopMusic();
}

#else /* !DREAMCAST */

/*
 * The audio API is backed by the audio processor of the Dreamcast, there is
 * nothing to play sounds with on the other platforms yet.
 */

void Audio_Init(void) {}
void Audio_Shutdown(void) {}
void Audio_Update(void) {}

audioClip_t Audio_LoadClip(const char* filename) { (void)filename; return AUDIO_INVALID_CLIP; }
audioClip_t Audio_FindClip(const char* filename) { (void)filename; return AUDIO_INVALID_CLIP; }
void Audio_UnloadClip(audioClip_t clip) { (void)clip; }
void Audio_UnloadAllClips(void) {}

audioVoice_t Audio_PlayClip(audioClip_t clip) { (void)clip; return AUDIO_INVALID_VOICE; }
audioVoice_t Audio_LoopClip(audioClip_t clip) { (void)clip; return AUDIO_INVALID_VOICE; }

audioVoice_t Audio_PlayClipEx(audioClip_t clip, int volume, int pan, uchar loop)
{
    (void)clip;
    (void)volume;
    (void)pan;
    (void)loop;
    return AUDIO_INVALID_VOICE;
}

void Audio_StopVoice(audioVoice_t voice) { (void)voice; }
void Audio_StopAllClips(void) {}
void Audio_SetClipVolume(int volume) { (void)volume; }
int  Audio_GetClipVolume(void) { return AUDIO_VOLUME_MAX; }

uchar Audio_PlayMusic(const char* filename, uchar loop) { (void)filename; (void)loop; return 0; }
void  Audio_StopMusic(void) {}
void  Audio_PauseMusic(void) {}
void  Audio_ResumeMusic(void) {}
uchar Audio_IsMusicPlaying(void) { return 0; }
void  Audio_SetMusicVolume(int volume) { (void)volume; }
int   Audio_GetMusicVolume(void) { return AUDIO_VOLUME_MAX; }

#endif /* DREAMCAST */
