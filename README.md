# BALLAST

A top-down 2D mine shooter written in C and SDL3, drawn in glowing vector
graphics with a warping spring grid, particle explosions and bloom. The ship,
the robots and missiles leave rippling wakes in the grid, and through it you
look down into open space in parallax depths: stars, the zone's planets
and drifting asteroids. While the camera moves, its eye trails a little off
straight overhead: the walls lean with the motion and show their sides, and
space below slides the other way. At rest the view is flat top-down again.

**Greed has mass.** Every shard of salvage you pick up and every miner you
rescue rides in your hold and makes the ship heavier: slower to get going,
longer to stop, harder when it rams. The hold is only banked at the exit, and a
destroyed ship spills it where it fell. Dump half of it as a bomb that grows
with the salvage packed inside when greed gets you cornered.

A run is five procedurally built sectors and the Overseer's foundry. Every
mine ends in a fight with its reactor core, a boss that throws bullet patterns
and rotating lasers at you in phases. Between sectors you draft ship modules in
the hangar and pick the next mine on the sector chart; challenges in the pilot
record unlock new ships, modules and sector types.

All graphics, sound effects and music are generated in code, so the game needs
no asset files.

## Building

Requirements: a C11 compiler and SDL 3.2 or newer.

```sh
# macOS (Homebrew) / Linux with pkg-config
make
./ballast

# or with CMake
cmake -B build-cmake && cmake --build build-cmake
./build-cmake/ballast
```

On macOS you can install SDL3 with `brew install sdl3`.

## Controls

| Action                   | Keyboard + mouse            | Gamepad          |
|--------------------------|-----------------------------|------------------|
| Fly                      | WASD / arrow keys           | Left stick       |
| Aim                      | Mouse                       | Right stick      |
| Fire primary             | Left button / Ctrl          | Right trigger    |
| Fire secondary           | Right button / Space        | Left trigger     |
| Afterburner              | Shift                       | A / LB           |
| Laser or special weapon  | 1 / 2, Q, mouse wheel       | Y                |
| Swap for the weapon under the ship | E                 | RB               |
| Jettison cargo as a bomb | F                           | X / B            |
| Automap                  | Tab / M                     | Back             |
| Turn the automap         | Drag, Q / E                 | Right stick      |
| Pause                    | Esc / P                     | Start            |
| Fullscreen               | F11 / Alt+Enter             |                  |

The Options menu can switch the flight model from twin-stick (WASD moves along
the screen axes) to ship-relative (W thrusts toward the cursor).

## How the game works

- **Intro and ending**: the game opens on a wireframe intro (the signal
  reaching the Consortium's mines, the robots waking, the Wraith-7 diving into
  a shaft, the logo flying together); it replays when the title screen sits
  idle. Winning the run plays an ending cinematic before the final card.
  Fire skips either.
- **The holo briefing**: before each mine a holo-sim acts out the objectives
  in wireframe while the orders type out, highlighting the line it shows: a
  scan of the real generated mine with the route through its keys, core and
  exit, then short demos of keys and doors, rescuing miners, salvage mass and
  the cargo bomb, the core fight, vaults and the escape. Left / right steps
  through the demos; the known threats turn on holo turntables.
- **Mines**: every mine is generated from big rooms (pillared halls,
  colonnades, caverns, crosses, rings around a rock island, split and twin
  halls, L-shapes, octagons) and ends in an arena two or four rooms big. Blue,
  yellow and red doors need matching keys; the HUD always shows your current
  objective and the automap (Tab) the parts of the mine you have explored.
- **The automap**: a holo model of the explored mine in 3D. It opens looking
  straight down and tilts over while a scan sweeps out from the ship and the
  walls rise behind it as glass panels; doors stand as coloured slabs, and
  keys, miners, the reactor and the ship float over the floor on tethers.
  Drag the mouse or hold Q / E to turn it, WASD pans and the wheel zooms.
- **The reactor core**: the arena holds the reactor, or the Overseer in the
  finale. The core wakes when you come close and fights in three or four
  phases of bullet patterns - spirals, rings, aimed fans, curving flowers,
  rotating laser beams, seeker mines, flak novas and walls of accelerating
  shards; from the third phase on two run at once. Lasers stop at walls, so
  the arena's pillars are cover. When a phase breaks the core vents (it wipes
  every bullet, pays for each and throws you back), then raises shield pylons
  that must be destroyed before it can be hurt again, or calls in guards. The
  last phase is a meltdown. Destroying it starts the self-destruct countdown,
  longer the further the exit; green arrows lead you out.
- **Grazes**: enemy bullets that brush past the ship recharge its energy and
  pay a little score. The ship's hull is smaller than its glow.
- **Traps**: plasma vents that erupt in a rhythm (deeper ones spit pellets),
  laser gates that switch on and off across the corridors, sweepers with
  rotating laser arms, gravity wells that drag the ship in - a full hold makes
  them hard to escape - and proximity mine fields. Robots are immune.
- **Robots**: besides the drones, lifters, hulks, spiders, gophers, drillers,
  super hulks and cloakers there are wasps that circle and slash past with
  shotgun bursts, pulsar nodes that spin bullet spirals, lancer sentinels
  whose charged beam is telegraphed by an aiming line, flak bombers whose
  shells burst into rings of pellets, and brood carriers that launch swarms of
  exploding mites. Deeper sectors teach the old robots new tricks, and elite
  robots (a gold halo) are tougher, fire faster, pay double and burst into
  pellets when they die.
- **Cargo and mass**: salvage from wrecks, caches, cracked walls and the
  reactor goes into the hold, and so do rescued miners. Mass lowers your
  acceleration and top speed and makes you drift, but a heavier ship rams
  harder. Everything in the hold is banked at the exit; die and it spills
  where you fell, together with your weapons, so you can fly back for it.
  **F** dumps half the hold as a cargo bomb: the more salvage in it, the
  bigger the blast (it can blow open cracked walls too).
- **Vaults**: sealed vault doors open when the reactor blows. The treasure
  inside is worth more the further the vault lies from the exit, so every
  vault is a bet against the countdown.
- **Weapons**: the laser (4 levels, quad upgrade) plus one special weapon slot
  (Vulcan, Spreadfire, Plasma or Fusion) and one secondary slot (Concussion,
  Homing, Proximity bombs, Smart or Mega missiles). All primaries run on
  energy, which refills at yellow energy centers. A different weapon lying
  on the floor is a choice: **E** swaps it for the one in your slot.
- **The run**: five generated sectors (Tycho, Io, Ceres) and the Overseer's
  foundry on Ceres. On the sector chart every node pays a bonus: an R&D crate
  with a free module in the deepest vault, a rich vein, a rescue beacon, an
  armory or a derelict whose draft offers rare modules. Some also carry a
  hazard (short fuse, infested, high gravity, overclocked generators,
  blackout, drained, armoured robots, booby-trapped) that pays extra salvage.
- **The sector chart**: a holo table in 3D. The sectors float over a glowing
  floor as wireframe moons (Tycho, Io) and tumbling rocks (Ceres), the routes
  arc between them and the Overseer waits at the far end; the camera leans with
  the mouse, a sector off your route shows what it holds when you point at it,
  and picking one flies the ship along the route and dives it into the mine.
- **The hangar**: between sectors you take one module for free from a draft
  of three, and spend banked salvage in the shop: two more modules, a draft
  reroll, hull repair, missiles and spare ships. Modules fill the ship's 5-6
  slots and many rank up when drafted again; with every slot full, a new one
  replaces a module you scrap. Their tags (passive, on kill, on hit, on burn,
  cargo, on pickup, countdown) show how they combine, for example Ram Prow and
  Gravity Anchor with a heavy hold, or Detonator Rounds with Bounty Hunter.
- **Ships**: the Wraith-7, and three to unlock: the Mule hauler (cargo weighs
  half, ram prow), the Kestrel interceptor (fast, phase shift, heavy cargo)
  and the Warden gunship (guardian drone, big energy cells).
- **The pilot record**: twelve challenges (escape with less than 3 seconds
  left, bank 400 salvage from one mine, destroy 3 robots with one cargo bomb,
  win a run...) unlock ships, modules for the draft pool, sector types and
  the threat protocols.
- **Skill and heat**: Trainee, Rookie or Hotshot, plus optional threat
  protocols (veteran and elite robots, short fuse, armoured, swarm, heavy
  cargo, fragile hull, scarcity, last stand). Each adds heat: 10% more score
  and 5% more salvage per point.
- **Seeds and the daily challenge**: the seed decides the chart, the mines and
  the drafts; type one in the run setup to replay or share it. The daily
  challenge gives every pilot the same seed, ship and protocols for the day.
- **The run report**: at the end of a run a report card with your build, the
  closest escape, the biggest chain and cargo bomb and what killed you is
  shown and saved as an image next to the save files (PNG with SDL 3.4 or
  newer, BMP before).
- **Score**: killing robots in quick succession builds a chain multiplier up
  to x8. The tally adds shield, hostage, full-rescue, skill and heat bonuses
  and shows how close your escape was. You get an extra life every 50,000
  points.
- **Continue**: the run is saved in the hangar, on the chart and before each
  mine. Progress inside a mine is not saved: aborting the mission or quitting
  resumes from the briefing before it.

Settings, high scores, the pilot record (`profile.txt`), the run in progress
(`run.txt`) and the run reports are saved to the SDL preferences folder (on
macOS `~/Library/Application Support/Ballast/Ballast/`).

## Source layout

| File              | Contents                                                         |
|-------------------|------------------------------------------------------------------|
| `src/main.c`      | Entry point, state machine, menus and screens (run setup, sector chart, hangar, run report, pilot record), input, save files |
| `src/game.c`      | Sector setup, the ship (cargo mass, weapon slots, jettison), weapons, projectiles, powerups, doors, countdown, world rendering |
| `src/robots.c`    | Robot definitions and AI, elite robots, robot generators, the boss |
| `src/reactor.c`   | The reactor core fight: phases, patterns, shield pylons, vents   |
| `src/barrage.c`   | Bullet patterns and laser beams shared by the reactor, the boss and the pattern robots |
| `src/traps.c`     | Plasma vents, laser gates, sweepers and gravity wells            |
| `src/hud.c`       | HUD, messages, hints, powerup icons                              |
| `src/automap.c`   | The automap as a 3D holo model of the explored mine              |
| `src/level.c`     | Tile map to wall geometry, collision, raycasts, flow-field pathfinding |
| `src/zones.c`     | The three zones (colours, music, rosters) and the map legend     |
| `src/mapgen.c`    | Procedural mines: room archetypes, arenas, lock-and-key layout, vaults, traps, robots and loot |
| `src/run.c`       | The run: sector chart, bonuses and hazards, ships, threat protocols, drafts, seeds, the daily challenge |
| `src/modules.c`   | The ship modules and the gameplay modifiers they provide         |
| `src/profile.c`   | The pilot record: challenges and what they unlock                |
| `src/fx.c`        | Warping grid and the wakes that ripple it, particles, debris, shock rings, score popups |
| `src/backdrop.c`  | Space under the mine floor in parallax depths: nebula, stars, the zone's planets, asteroids |
| `src/wire.c`      | Wireframe 3D: perspective glow lines, line models (extruded shapes, globes, rocks), draw-on and blow-apart effects |
| `src/cinema.c`    | The holo briefing demos, the intro and the ending cinematics; their line models for the other holo screens |
| `src/chart.c`     | The sector chart as a 3D holo table: the sectors, routes and camera, the launch flight |
| `src/ui.c`        | Menu animation: eased hovers, springing cursors and presses kept by id between frames |
| `src/render.c`    | Batched glow line renderer, bloom post-process, vector font, group transforms that slide and fade the UI |
| `src/audio.c`     | Software mixer, synthesized sound effects                        |
| `src/music.c`     | House music sequencer and synthesizer; the songs as text patterns |

The map legend is documented at the top of `src/zones.c`; the room archetypes of
the generator, and the markers they use, are in `src/mapgen.c`.

## Test and debug flags

```
--level N        start directly in sector N (0-5) of a throwaway run
--seed HEX       the run seed, e.g. --seed 1234ABCD
--ship N         0 Wraith-7, 1 Mule, 2 Kestrel, 3 Warden
--difficulty N   0 = Trainee ... 2 = Hotshot; 3 and 4 add the veteran and elite protocols
--heat MASK      threat protocols as a bit mask
--mods a,b,...   install modules at their highest rank (keys as in modules.c)
--cargo N        start every mine with N salvage in the hold
--unlockall      treat every challenge as done
--daily          open the run setup on the daily challenge
--god            invulnerable ship
--allweapons     a special weapon, missiles, the best laser and all keys
--tp X Y         teleport to tile X,Y
--arena          start in the arena next to the reactor or the Overseer
                 (with --bot: circle it and shoot it)
--spawn LETTERS  spawn robots by their map letters around the ship (e.g. PkBqa)
--elite          the spawned robots are elite
--destroy        destroy the reactor/boss at start (tests the escape)
--bot            simple autopilot that fights robots and flies to the exit
--autorun        click through every screen of a whole run (with --bot)
--automap --reveal
--state NAME     open a screen: intro, title, setup, map, briefing, tally, victory, summary, record, help, options, scores, hangar, ...
                 (a normal launch opens on the intro; test runs skip it)
--salvage N      add N banked salvage to the run
--prefdir DIR    keep the save files in DIR; test runs then write them there
                 (otherwise --frames, --level and --state runs never write any save file)
--frames N --shot file.bmp   run N frames as fast as possible, save a screenshot and quit
```
