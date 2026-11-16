/*
 *  audio_jukebox.c
 *  dEngine example: sound effects and music
 *
 *  A pad driven sound board on top of the tech demo. Every call in audio.h
 *  that a game would use shows up here.
 *
 *  Shows:
 *    - preloading clips into sound RAM, and looking them up again by path
 *    - one shots, loops, and voices with their own volume and pan
 *    - stopping one voice, or every clip at once
 *    - the master clip volume
 *    - CDDA music (free: the GD-ROM decodes it) versus streamed music (a
 *      little CPU, but works from /pc), and how starting one stops the other
 *    - pausing, resuming and the music volume
 *    - surviving missing files: every call takes an invalid handle quietly
 *
 *  The repository ships no sounds. Drop your own WAVs (8/16 bit PCM or 4 bit
 *  Yamaha ADPCM; clips at most 65534 samples) at the paths below, under data/.
 *  CMake copies .wav files to disc/ like the other assets. For CDDA, pack the
 *  disc with one -c per track; see the README.
 *
 *  Controls (port 0):
 *    A        play the blip once, centered
 *    B        start / stop the looping hum
 *    X        fire the blip panned to wherever the D-pad left/right points
 *    Y        stop every clip
 *    UP/DOWN  clip master volume
 *    L        switch music: CDDA track 1 -> CDDA track 2 -> streamed -> off
 *    R        pause / resume the music (hold R and press UP/DOWN for its volume)
 *    START    print the state to the log
 *
 *  The free fly camera is turned off so the D-pad and triggers are free.
 *
 *  Build:  ./dc-build.sh --example audio_jukebox
 */

#include <dgame.h>
#include <audio.h>

#define SOUND_BLIP   "data/sounds/blip.wav"
#define SOUND_HUM    "data/sounds/hum.wav"
#define MUSIC_STREAM "data/music/theme.wav"

#define VOLUME_STEP 32

typedef enum music_mode_t {
	MUSIC_OFF = 0,
	MUSIC_CDDA_1,
	MUSIC_CDDA_2,
	MUSIC_STREAM_FILE,
	MUSIC_MODE_COUNT
} music_mode_t;

static const char* g_musicNames[MUSIC_MODE_COUNT] = {
	"off", "CDDA track 1", "CDDA track 2", "streamed WAV"
};

static audioClip_t  g_blip = AUDIO_INVALID_CLIP;
static audioClip_t  g_hum  = AUDIO_INVALID_CLIP;
static audioVoice_t g_humVoice = AUDIO_INVALID_VOICE;

static music_mode_t g_musicMode;
static int          g_musicPaused;
static int          g_volumeChangedWhileR;   /* R was a modifier, not a press */

static dg_action_t g_actBlip;
static dg_action_t g_actHum;
static dg_action_t g_actPanned;
static dg_action_t g_actStopAll;
static dg_action_t g_actPan;
static dg_action_t g_actVolUp;
static dg_action_t g_actVolDown;
static dg_action_t g_actMusic;
static dg_action_t g_actPause;
static dg_action_t g_actReport;

/* -------------------------------------------------------------------------
 *  Helpers
 * ------------------------------------------------------------------------- */

static int Clamp(int value, int low, int high)
{
	return value < low ? low : (value > high ? high : value);
}

static void StartMusic(music_mode_t mode)
{
	uchar started = 1;

	g_musicMode = mode;
	g_musicPaused = 0;

	/* No explicit stop needed between modes: starting a track stops whatever
	   was playing, whichever kind it was. */
	switch (mode)
	{
		case MUSIC_CDDA_1:      started = Audio_PlayCDDATrack(1, 1);             break;
		case MUSIC_CDDA_2:      started = Audio_PlayCDDATrack(2, 1);             break;
		case MUSIC_STREAM_FILE: started = Audio_PlayMusic(MUSIC_STREAM, 1);      break;
		default:                Audio_StopMusic();                               break;
	}

	if (!started)
		Game_Log("[jukebox] could not start %s (no such track or file?)\n",
		         g_musicNames[mode]);
	else
		Game_Log("[jukebox] music: %s\n", g_musicNames[mode]);
}

static void ToggleHum(void)
{
	if (g_humVoice != AUDIO_INVALID_VOICE)
	{
		Audio_StopVoice(g_humVoice);
		g_humVoice = AUDIO_INVALID_VOICE;
		return;
	}

	/* A loop keeps its voice until stopped, so hang on to the handle. */
	g_humVoice = Audio_LoopClip(g_hum);
}

static void PlayPannedBlip(void)
{
	/* -1 .. 1 from the D-pad pair, onto 0 .. 255. */
	float side = Input_Value(g_actPan);
	int pan = AUDIO_PAN_CENTER + (int)(side * (AUDIO_PAN_RIGHT - AUDIO_PAN_CENTER));

	/* A one shot: the voice frees itself when it ends, so the handle is not
	   worth keeping. Pan is ignored for stereo clips. */
	Audio_PlayClipEx(g_blip, 200, Clamp(pan, AUDIO_PAN_LEFT, AUDIO_PAN_RIGHT), 0);
}

static void ChangeVolume(int delta)
{
	/* With R held, UP/DOWN steers the music instead of the clips. */
	if (Input_Held(g_actPause))
	{
		g_volumeChangedWhileR = 1;
		Audio_SetMusicVolume(Clamp(Audio_GetMusicVolume() + delta, 0, AUDIO_VOLUME_MAX));
		return;
	}

	/* Only applies to clips played from now on. */
	Audio_SetClipVolume(Clamp(Audio_GetClipVolume() + delta, 0, AUDIO_VOLUME_MAX));
}

/* -------------------------------------------------------------------------
 *  Hooks
 * ------------------------------------------------------------------------- */

void Game_Configure(dg_config_t* cfg)
{
	cfg->bootScene = "data/scenes/techDemo.scene";
	cfg->debugCamera = 0;
}

void Game_ConfigureInput(void)
{
	Input_ClearBindings();

	g_actBlip    = Input_DefineAction("blip");
	g_actHum     = Input_DefineAction("hum");
	g_actPanned  = Input_DefineAction("panned_blip");
	g_actStopAll = Input_DefineAction("stop_all");
	g_actPan     = Input_DefineAction("pan");
	g_actVolUp   = Input_DefineAction("volume_up");
	g_actVolDown = Input_DefineAction("volume_down");
	g_actMusic   = Input_DefineAction("next_music");
	g_actPause   = Input_DefineAction("pause_music");
	g_actReport  = Input_DefineAction("report");

	Input_BindButton(g_actBlip,    0, DG_BTN_A);
	Input_BindButton(g_actHum,     0, DG_BTN_B);
	Input_BindButton(g_actPanned,  0, DG_BTN_X);
	Input_BindButton(g_actStopAll, 0, DG_BTN_Y);
	Input_BindButtonPair(g_actPan, 0, DG_BTN_LEFT, DG_BTN_RIGHT);
	Input_BindAxis(g_actPan,       0, DG_AXIS_LX, 1.0f);
	Input_BindButton(g_actVolUp,   0, DG_BTN_UP);
	Input_BindButton(g_actVolDown, 0, DG_BTN_DOWN);
	Input_BindButton(g_actMusic,   0, DG_BTN_LTRIG);
	Input_BindButton(g_actPause,   0, DG_BTN_RTRIG);
	Input_BindButton(g_actReport,  0, DG_BTN_START);
}

void Game_SceneLoaded(const char* scene)
{
	(void)scene;

	/* Opening a scene unloads the previous scene's clips and stops its music,
	   so load per scene. Loading a clip twice hands back the same handle.

	   A scene can also declare these itself:

	       sounds
	       {
	           data/sounds/blip.wav
	           data/sounds/hum.wav
	       }

	   and the game then fetches them with Audio_FindClip(path). Trying that
	   first means the example works either way. */
	g_blip = Audio_FindClip(SOUND_BLIP);
	if (g_blip == AUDIO_INVALID_CLIP)
		g_blip = Audio_LoadClip(SOUND_BLIP);

	g_hum = Audio_FindClip(SOUND_HUM);
	if (g_hum == AUDIO_INVALID_CLIP)
		g_hum = Audio_LoadClip(SOUND_HUM);

	if (g_blip == AUDIO_INVALID_CLIP || g_hum == AUDIO_INVALID_CLIP)
		Game_Log("[jukebox] missing %s or %s; those buttons will stay silent\n",
		         SOUND_BLIP, SOUND_HUM);

	g_humVoice = AUDIO_INVALID_VOICE;

	Audio_SetClipVolume(AUDIO_VOLUME_MAX);
	Audio_SetMusicVolume(192);

	/* Start on CDDA, which costs nothing. When there is no track 1 (running
	   from /pc, say) fall back to streaming. */
	StartMusic(MUSIC_CDDA_1);
	if (!Audio_IsMusicPlaying())
		StartMusic(MUSIC_STREAM_FILE);
}

void Game_Update(float dt)
{
	(void)dt;

	if (Input_Pressed(g_actBlip))
		Audio_PlayClip(g_blip);

	if (Input_Pressed(g_actHum))
		ToggleHum();

	if (Input_Pressed(g_actPanned))
		PlayPannedBlip();

	if (Input_Pressed(g_actStopAll))
	{
		Audio_StopAllClips();
		g_humVoice = AUDIO_INVALID_VOICE;
	}

	if (Input_Pressed(g_actVolUp))
		ChangeVolume(VOLUME_STEP);

	if (Input_Pressed(g_actVolDown))
		ChangeVolume(-VOLUME_STEP);

	if (Input_Pressed(g_actMusic))
		StartMusic((music_mode_t)((g_musicMode + 1) % MUSIC_MODE_COUNT));

	/* On release rather than press: if R was only held as a modifier for the
	   music volume, letting go of it should not also pause. */
	if (Input_Released(g_actPause))
	{
		if (!g_volumeChangedWhileR && g_musicMode != MUSIC_OFF)
		{
			if (g_musicPaused)
				Audio_ResumeMusic();
			else
				Audio_PauseMusic();

			g_musicPaused = !g_musicPaused;
		}

		g_volumeChangedWhileR = 0;
	}

	if (Input_Pressed(g_actReport))
	{
		Game_Log("[jukebox] music=%s playing=%d cdda=%d musicVol=%d clipVol=%d hum=%d\n",
		         g_musicNames[g_musicMode], Audio_IsMusicPlaying(), Audio_GetCDDATrack(),
		         Audio_GetMusicVolume(), Audio_GetClipVolume(), g_humVoice);
	}

	/* No Audio_Update() here: dEngine_HostFrame already feeds the stream once
	   a frame. */
}

void Game_RenderHUD(void)
{
	char line[64];

	sprintf(line, "music: %s%s", g_musicNames[g_musicMode],
	        g_musicPaused ? " (paused)" : "");
	HUD_DrawText(620, 400, line);

	sprintf(line, "music vol %d  clip vol %d", Audio_GetMusicVolume(), Audio_GetClipVolume());
	HUD_DrawText(620, 360, line);

	sprintf(line, "hum %s  pan %+.2f", g_humVoice != AUDIO_INVALID_VOICE ? "on" : "off",
	        Input_Value(g_actPan));
	HUD_DrawText(620, 320, line);

	HUD_DrawText(620, -320, "A blip  B hum  X panned  Y stop");
	HUD_DrawText(620, -360, "UP/DN vol  L music  R pause");
	HUD_DrawText(620, -400, "R+UP/DN music vol");
}

void Game_SceneUnloading(const char* scene)
{
	(void)scene;

	/* The scene loader would do this too; done here so it is explicit. */
	Audio_StopAllClips();
	Audio_StopMusic();
}
