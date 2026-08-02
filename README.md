dEngine
=======

dEngine is an OpenGL ES 2.0 renderer authored by Fabien Sanglard.

Summary :
=========

dEngine is material based. Each material result in the compilation and caching of an unique uber shader. The renderer supports:
- Shadow mapping.
- Specual mapping.
- Bump-mapping. 

Audio :
=======

The audio API (`src/audio.h`) runs on the KallistiOS sound support and handles
two kinds of sounds.

**Clips** are short sound effects preloaded in the audio processor memory, and
played as one shots or looped:

```c
audioClip_t door = Audio_LoadClip("data/sounds/door.wav");

Audio_PlayClip(door);                                    /* one shot         */
Audio_LoopClip(door);                                    /* until stopped    */
voice = Audio_PlayClipEx(door, 200, AUDIO_PAN_LEFT, 0);  /* volume and pan   */
Audio_StopVoice(voice);
```

**Music** is a single track streamed straight from the disc, so the whole track
never has to fit in memory:

```c
Audio_SetMusicVolume(200);
Audio_PlayMusic("data/music/theme.wav", 1 /* loop */);
```

Both read WAV files holding 8 or 16 bits PCM, or 4 bits Yamaha ADPCM samples,
mono or stereo. Clips additionally cannot be longer than 65534 samples, which
is an audio processor limitation: anything longer should be streamed as music.
`Audio_Update()` feeds the streaming music and is called once per frame by
`dEngine_HostFrame()`.

A scene declares its own audio, the clips being loaded before the scene starts
and the music being started right away:

```
music
{
    filename : data/music/theme.wav
    volume   : 255
    loop     : 1
}

sounds
{
    data/sounds/door.wav
    data/sounds/hit.wav
}
```

Clips declared that way are retrieved with `Audio_FindClip("data/sounds/door.wav")`.
Opening a scene stops the music of the previous one and unloads its clips.

Details :
=========

For details about optimizations and technical choices, check the page of the author:

http://fabiensanglard.net/dEngine/index.php

http://fabiensanglard.net/dEngineSourceCodeRelease/index.php

Enjoy,

Fabien Sanglard
