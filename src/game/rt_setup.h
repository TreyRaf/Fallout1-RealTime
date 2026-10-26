#ifndef FALLOUT_GAME_RT_SETUP_H_
#define FALLOUT_GAME_RT_SETUP_H_

namespace fallout {

// First launch on a new computer: when the Fallout 1 data files aren't where
// `fallout.cfg` says, the player's own Fallout 1 is looked for (GOG, Steam,
// or a folder they pick). `master_dat` and `critter_dat` then point at its
// MASTER.DAT and CRITTER.DAT, and its loose data files (not its saves) are
// copied into this game's `data` folder. The original install is only read.
//
// Called after the configuration is loaded, before the databases are opened.
// Returns false when no Fallout 1 was found (the player was told).
bool rt_setup_game_files();

} // namespace fallout

#endif /* FALLOUT_GAME_RT_SETUP_H_ */
