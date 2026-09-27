# NEON DESCENT

A top-down 2D mine shooter written in C and SDL3. The gameplay is modelled on
the MS-DOS classic **Descent** (1995) and the visuals on **Geometry Wars**:
glowing neon vector graphics, a warping spring grid, particle explosions and
bloom.

Fly into three infected mines, find the coloured access keys, destroy the
reactor core (or the Overseer boss), rescue the trapped miners and reach the
exit tunnel before the mine self-destructs.

All graphics, sound effects and music are generated in code, so the game needs
no asset files.

## Building

Requirements: a C11 compiler and SDL 3.2 or newer.

```sh
# macOS (Homebrew) / Linux with pkg-config
make
./neon_descent

# or with CMake
cmake -B build-cmake && cmake --build build-cmake
./build-cmake/neon_descent
```

On macOS you can install SDL3 with `brew install sdl3`.

## Controls

| Action              | Keyboard + mouse            | Gamepad          |
|---------------------|-----------------------------|------------------|
| Fly                 | WASD / arrow keys           | Left stick       |
| Aim                 | Mouse                       | Right stick      |
| Fire primary        | Left button / Ctrl          | Right trigger    |
| Fire secondary      | Right button / Space        | Left trigger     |
| Afterburner         | Shift                       | A / LB           |
| Select primary      | 1-5, Q, mouse wheel         | Y                |
| Select secondary    | 6-0, E                      | RB               |
| Drop proximity bomb | F                           | X / B            |
| Automap             | Tab / M                     | Back             |
| Pause               | Esc / P                     | Start            |
| Fullscreen          | F11 / Alt+Enter             |                  |

The Options menu can switch the flight model from twin-stick (WASD moves along
the screen axes) to ship-relative (W thrusts toward the cursor, as in Descent).

## How the game works

- **Mines**: every level has a reactor core, or the Overseer in the last mine.
  Destroying it starts a self-destruct countdown (25 to 60 seconds depending on
  the mine and difficulty). Green arrows lead you to the exit tunnel.
- **Keys**: blue, yellow and red doors need matching keys. The HUD always shows
  your current objective. The automap (Tab) shows the parts of the mine you
  have already explored.
- **Hostages**: fly into trapped miners (marked SOS) to pick them up. They
  only count once you escape. If your ship is destroyed, the hostages on board
  die and all your weapons are dropped where you died, so you can fly back
  and collect them.
- **Shield and energy**: the shield is your health. Energy powers your
  weapons and refills at yellow energy centers. The Vulcan cannon uses ammo
  instead of energy.
- **Weapons**: 5 primary weapons (Laser with 4 upgrade levels and Quad
  upgrade, Vulcan, Spreadfire, Plasma, Fusion) and 5 secondary weapons
  (Concussion, Homing, Proximity bombs, Smart and Mega missiles). Hold fire
  with the Fusion cannon to charge it, but don't overcharge.
- **Robots**: 11 robot types with different AI: rushers, melee lifters,
  turrets, missile hulks, spiders that split into babies, mine-laying
  gophers, ambushing vulcan drillers, homing-missile super hulks, cloaked
  lifters, and the teleporting Overseer boss. Robot generators (pink pads) keep
  producing robots, especially after the reactor goes down. Robots only fire
  at you while they are on screen, and arrows at the screen edge show where
  alerted robots are.
- **Score**: killing robots in quick succession builds a chain multiplier up
  to x8. The end-of-level tally adds shield, energy, hostage, full-rescue and
  skill bonuses. You get an extra life every 50,000 points.
- **Secrets**: cracked walls (dashed outlines) can be shot open and hide rare
  equipment.
- **Difficulty**: Trainee, Rookie, Hotshot, Ace or Insane. Higher levels make
  robots more accurate and aggressive and give you less time to escape.

Settings and high scores are saved to the SDL preferences folder
(on macOS `~/Library/Application Support/NeonDescent/NeonDescent/`).

## Source layout

| File              | Contents                                                         |
|-------------------|------------------------------------------------------------------|
| `src/main.c`      | Entry point, state machine, menus and screens, input, save files |
| `src/game.c`      | Level setup, player, weapons, projectiles, powerups, doors, countdown, world rendering |
| `src/robots.c`    | Robot definitions and AI, the reactor, robot generators, the boss |
| `src/hud.c`       | HUD, messages, hints, automap, powerup icons                     |
| `src/level.c`     | Tile map to wall geometry, collision, raycasts, flow-field pathfinding |
| `src/levels.c`    | The three mine layouts (ASCII maps) and their metadata           |
| `src/fx.c`        | Warping grid, particles, debris, shock rings, score popups       |
| `src/render.c`    | Batched neon line renderer, bloom post-process, vector font      |
| `src/audio.c`     | Software mixer, synthesized sound effects, procedural music      |

The map legend is documented at the top of `src/levels.c`, so you can edit the
mines or add new ones there.

## Test and debug flags

```
--level N        start directly in mine N (0-2)
--difficulty N   0 = Trainee ... 4 = Insane
--god            invulnerable ship
--allweapons     all weapons, keys and upgrades
--tp X Y         teleport to tile X,Y
--destroy        destroy the reactor/boss at start (tests the escape)
--bot            simple autopilot that fights robots
--automap --reveal
--state NAME     open a screen: title, briefing, tally, help, options, scores, ...
--frames N --shot file.bmp   run N frames, save a screenshot and quit
```
