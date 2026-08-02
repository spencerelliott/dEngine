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

**Music** is a single background track, and comes from either of two places.
A **CDDA track** pressed on the disc is decoded by the GD-ROM drive and mixed
into the output by the audio processor, so it costs no CPU time at all:

```c
Audio_SetMusicVolume(200);
Audio_PlayCDDATrack(1, 1 /* loop */);
```

A **streamed file** costs a little CPU but does not need the track to be laid
out on the disc, which makes it the one to use when running from `/pc`. Only a
small buffer is kept in memory, never the whole track:

```c
Audio_PlayMusic("data/music/theme.wav", 1 /* loop */);
```

Only one plays at a time: starting one stops the other, and
`Audio_StopMusic()`, `Audio_PauseMusic()`, `Audio_ResumeMusic()`,
`Audio_SetMusicVolume()` and `Audio_IsMusicPlaying()` drive whichever is
current.

Clips and streamed music read WAV files holding 8 or 16 bits PCM, or 4 bits
Yamaha ADPCM samples, mono or stereo. Clips additionally cannot be longer than
65534 samples, which is an audio processor limitation: anything longer should
be streamed as music or pressed as a CDDA track. `Audio_Update()` feeds the
streaming music and is called once per frame by `dEngine_HostFrame()`; a CDDA
track needs nothing of the sort.

CDDA tracks are laid down when the disc is built, with one `-c` per track in
the order they should appear on the disc:

```
mkdcdisc -e dEngine.elf -d disc/ -c data/music/theme.wav -c data/music/action.wav -o dEngine.cdi
```

They occupy the first session of the disc, so the first `-c` file is track 1,
the second is track 2, and so on. Note that only `mkdcdisc`'s default
Audio/Data layout carries audio tracks.

A scene declares its own audio, the clips being loaded before the scene starts
and the music being started right away:

```
music
{
    track  : 1
    volume : 255
    loop   : 1
}

sounds
{
    data/sounds/door.wav
    data/sounds/hit.wav
}
```

A `music` block naming a `filename` instead of a `track` streams that file. If
it gives both, the CDDA track wins.

Clips declared that way are retrieved with `Audio_FindClip("data/sounds/door.wav")`.
Opening a scene stops the music of the previous one and unloads its clips.

Details :
=========

For details about optimizations and technical choices, check the page of the author:

http://fabiensanglard.net/dEngine/index.php

http://fabiensanglard.net/dEngineSourceCodeRelease/index.php

Enjoy,

Fabien Sanglard
