/*
 * The zones of a run and the map legend of the generated mines.
 *
 * Map legend
 *   #  rock              .  open floor         w  cracked (breakable) rock
 *   -  door              b  blue door          y  yellow door
 *   r  red door          x  exit door          X  vault door
 *   Z  exit tunnel (goal)
 *   S  player start      E  energy center      G  robot generator (matcen)
 *   C  reactor core      W  boss               H  hostage
 *   1  blue key          2  yellow key         3  red key
 * Robots
 *   d drone  l lifter  t turret  h hulk  s spider  g gopher
 *   v vulcan driller  u super hulk  c cloaked lifter
 *   a wasp  P pulsar  k lancer  B bomber  q carrier
 * Traps
 *   ~  plasma vent       =  laser gate (spans the corridor it sits in)
 *   O  sweeper: rotating laser arms              Y  gravity well
 *   n  proximity mine
 * Powerups
 *   + shield  * energy  L laser upgrade  Q quad lasers  V vulcan  A vulcan ammo
 *   N spreadfire  J plasma  F fusion  m concussion x4  o homing x4
 *   p proximity x4  i smart missile  M mega missile  I invulnerability
 *   K cloak  U extra life  $ salvage cache  @ vault treasure  R R&D crate
 */
#include "common.h"

const LevelDef ZONES[3] = {
    {"TYCHO EXCAVATION", "LUNAR MINING COLONY - EARTH'S MOON", NULL, 0,
     {0.20f, 0.85f, 1.00f, 1}, {0.10f, 0.22f, 0.65f, 1}, {0.040f, 0.060f, 0.125f, 1}, {0.40f, 1.00f, 0.90f, 1},
     SONG_L1, "",
     {RB_DRONE, RB_LIFTER, RB_WASP, RB_TURRET, RB_GOPHER}, 5,
     {RB_DRONE, RB_LIFTER, RB_WASP}, 3},
    {"IO SULFUR REFINERY", "VOLCANIC PROCESSING PLANT - JUPITER SYSTEM", NULL, 0,
     {1.00f, 0.58f, 0.15f, 1}, {0.50f, 0.14f, 0.34f, 1}, {0.100f, 0.050f, 0.035f, 1}, {1.00f, 0.85f, 0.30f, 1},
     SONG_L2, "",
     {RB_HULK, RB_SPIDER, RB_PULSAR, RB_BOMBER, RB_LANCER}, 5,
     {RB_DRONE, RB_SPIDER, RB_GOPHER, RB_WASP}, 4},
    {"CERES DEEP FOUNDRY", "ASTEROID BELT - CORE OF THE INFECTION", NULL, 0,
     {1.00f, 0.25f, 0.75f, 1}, {0.32f, 0.10f, 0.58f, 1}, {0.080f, 0.028f, 0.095f, 1}, {1.00f, 0.55f, 1.00f, 1},
     SONG_L3, "",
     {RB_SUPERHULK, RB_CLOAKER, RB_CARRIER, RB_LANCER, RB_PULSAR}, 5,
     {RB_DRILLER, RB_LIFTER, RB_SPIDER, RB_CLOAKER}, 4},
};
