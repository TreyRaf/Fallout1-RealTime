Fallout The Unofficial Remaster
===================

A real-time, controller-first version of Fallout 1, built on Fallout 1 CE.

What you need
-------------
- Windows 10 or 11 (64-bit).
- Your own copy of Fallout 1, installed from GOG or Steam. This package
  contains no Fallout 1 game files.
- An Xbox-style controller (recommended; the game is designed for one).

How to play
-----------
1. Unzip this folder anywhere you can write to (for example Documents or
   Desktop; not inside Program Files).
2. Double-click fallout-ce.exe.

The first launch finds your Fallout 1 automatically (GOG or Steam). If it
can't, it asks you to pick the folder Fallout 1 is installed in (the one
with MASTER.DAT and CRITTER.DAT). It then copies Fallout 1's small loose
data files (about 80 MB) into this folder's "data" folder and reads the big
MASTER.DAT and CRITTER.DAT from your install. Your Fallout 1 install is
never changed, and its saves are not copied.

After that, double-clicking fallout-ce.exe just starts the game.

Saves
-----
Saves go to data\SAVEGAME in this folder. The game autosaves every 10
minutes of real time into slot 10 ("Autosave"); it never overwrites a save
you made there.

Settings
--------
fallout.cfg, [realtime] section (autosave_minutes, timing_*, and others).

Moving or reinstalling Fallout 1
--------------------------------
If Fallout 1 moves, delete the master_dat and critter_dat lines' values in
fallout.cfg (or the whole fallout.cfg) and start the game: it looks again.
