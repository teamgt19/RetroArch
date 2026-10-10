# RAVE's RetroArch configs

The files RAVE passes to this RetroArch on every launch. The live copies are in the game's `Retroarch` folder
(`F:\WORK\RAVE Standalone\Builds\RAVE_Dev\Retroarch`, shared by RAVE_HD and RAVE_URP); these are the tracked copies.
Copy them here again after changing the live ones.

- `rave_override.cfg` - forced on every launch (`--appendconfig`): d3d11, VRCMD input, 1x native picture, EmuVR's
  player 1 keyboard / save / netplay settings.
- `modes\lightgun_<core>.cfg / .opt / .netplay.cfg` - added only when a game's Lightgun is on (all 13 old EmuVR
  "(Lightguns)" folders' settings); `modes.json` = the gun model per core.
- `modes\vertical.cfg` (`video_rotation = "3"`), `vertical_<core>.opt` - added for vertical games.
