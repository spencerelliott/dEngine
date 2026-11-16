# examples/

Small, self contained games, each one showing off one of the engine's systems.
Each directory is a complete `game/` replacement: it defines its own hooks, so
only one example is built at a time, and never together with `game/`.

| Example | System | What it does |
|---|---|---|
| [`physics_playground`](physics_playground/physics_playground.c) | `physics.h` | Hell knights drop, stack, get shoved by a moving platform, raycast pushes and a radius blast. |
| [`character_controller`](character_controller/character_controller.c) | `physics.h` + camera | A first person walker: `PHYS_MoveAndSlide`, gravity, jumping, a trigger volume with enter and exit events, a raycast probe. |
| [`audio_jukebox`](audio_jukebox/audio_jukebox.c) | `audio.h` | A pad driven sound board: one shots, loops, pan and volume, CDDA and streamed music, pause and resume. |
| [`behaviors`](behaviors/behaviors.c) | `dgame.h` objects | A ring of knights built out of small stacked behaviors (orbit, bob, pulse, blink) that are added, tuned and removed at runtime. |
| [`input_actions`](input_actions/input_actions.c) | `dgame.h` input | Actions on buttons, axes and pairs, two control schemes swapped at runtime, deadzones, raw pad readout, a second player, scene swaps. |

Each file starts with a comment listing what it shows and its controls.

## Building one

```sh
./dc-build.sh --example physics_playground
./dc-build.sh Debug --example audio_jukebox --cdi
```

`--example <name>` sets the `DENGINE_EXAMPLE` CMake cache variable, which
compiles `examples/<name>/*.c` in place of `game/`. Leave it out (or pass an
empty value) to go back to `game/`. With plain CMake:

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=toolchain/Dreamcast.cmake -DDENGINE_EXAMPLE=behaviors
```

To start your own game from one, copy its file into `game/`. Remove
`game/example_spinner.c` first: both define `Game_Update` and friends, so the
link would fail with duplicate symbols.

## Assets

All the examples run on the tech demo scene and the hell knight that ships
in `data/`. The exception is `audio_jukebox`, because the repository ships no
sounds. It looks for:

| Path | Used as |
|---|---|
| `data/sounds/blip.wav` | a short one shot |
| `data/sounds/hum.wav` | a short loop |
| `data/music/theme.wav` | streamed music |
| CDDA tracks 1 and 2 | disc music, added with `mkdcdisc -c` |

A missing file does not crash anything. It is reported on the log and that
button stays silent. Clips must be 8 or 16 bit PCM, or 4 bit Yamaha ADPCM, and
at most 65534 samples long. See the Audio section of the top level `README.md`
for how to press CDDA tracks onto the disc.

## Things the examples point out

These are easy to trip over:

- **Physics bodies outlive scenes.** Game objects die on a scene swap but the
  physics world does not. The physics examples call `PHYS_Reset()` from
  `Game_SceneUnloading`.
- **Physics runs between `Game_Update` and `Game_LateUpdate`.** Push input into
  bodies in the first one. Copy positions back out to objects in the second.
- **The knight mesh is authored Z up.** Spawned by hand, it needs a pitch of
  -90 degrees (`GameObject_SetRotation(obj, 0, -90 * DEG_TO_RAD, 0)`) to stand.
  The `.map` loader bakes this into its matrix.
- **`HUD_DrawText` coordinates** run from -640 to 640 across and -480 to 480 up,
  with the origin at the centre, and `x` is the **right** edge of the text.
- **`Input_ClearBindings` deletes the actions too,** along with their
  previous frame state. Re-fetch every `dg_action_t` afterwards. A button held
  through a rebind also reads as pressed again on the next frame, which is why
  `input_actions` reads its scheme toggle raw.
- **Kinematic bodies ignore gravity.** A character moved with
  `PHYS_MoveAndSlide` integrates its own vertical speed.
- **The tech demo has no collision geometry.** The physics examples add
  invisible boxes that stand in for the floor and walls.
