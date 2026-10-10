/*  RAVE patch set for RetroArch - the game <-> RetroArch pipe (drop-in for EmuVR's 1.7.5 fork).
 *  Protocol: F:\WORK\RAVE Standalone\retroarch_rave_protocol.md
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 */
#ifndef RAVE_H__
#define RAVE_H__

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

#define RAVE_MAX_PORTS      4
/* Pad buttons as the game numbers them (RetroArch's XInput order): 0 A .. 10 Guide, 11-14 D-pad
 * (right, left, down, up), 15 D-pad modifier, 16/17 triggers as buttons. */
#define RAVE_PAD_BUTTONS    18
#define RAVE_BTN_DPAD_RIGHT 11
#define RAVE_BTN_DPAD_LEFT  12
#define RAVE_BTN_DPAD_DOWN  13
#define RAVE_BTN_DPAD_UP    14
/* Half axes as the game sends them: 0/1 LX-/+, 2/3 LY up/down, 4/5 RX-/+, 6/7 RY up/down, 8 LT, 9 RT. */
#define RAVE_PAD_HALF_AXES  10
#define RAVE_KEYS           512

typedef struct rave_pad
{
   uint32_t buttons;
   int16_t  half_axes[RAVE_PAD_HALF_AXES];
} rave_pad_t;

typedef struct rave_state
{
   bool active;          /* --rave / --emuvr given */
   bool legacy_name;     /* --emuvr: Spout sender "EmuVR <name>" */
   char name[256];
   bool input_enabled;   /* --enable-input / SETINPUT */
   bool show_window;     /* --show-window */
   bool headless;        /* --headless */
   bool lightgun;        /* --lightgun */
   bool game_focus;      /* --game-focus */
   unsigned ttl;         /* --ttl N seconds (0 = off) */
   int64_t  last_ttl_us;
   bool window_hidden;

   rave_pad_t pads[RAVE_MAX_PORTS];
   bool keys[RAVE_KEYS]; /* retro_key (= Unity KeyCode) */

   /* Lightguns per port: screen UV 0..1 from the game (-2 = off screen). */
   float gun_u[RAVE_MAX_PORTS], gun_v[RAVE_MAX_PORTS];
   bool  gun_trigger[RAVE_MAX_PORTS];
   bool  gun_offscreen[RAVE_MAX_PORTS];

   /* PAN l1 l2 r1 r2 (speaker 1/2 -> left, speaker 1/2 -> right). */
   float pan[4];
   bool  pan_set;

   /* --appendoptions "a.cfg|b.cfg": core options on top of RetroArch's own, later files win. */
   char  append_options[4096];
   bool  options_applied;
} rave_state_t;

rave_state_t *rave_get_state(void);

/* Removes RAVE's switches from argv (before RetroArch's own option parser) and remembers them. */
void rave_parse_args(int *argc, char **argv);

/* One stdin line without the "VRCMD " prefix. */
void rave_handle_vrcmd(const char *cmd);

/* Once per frame from the run loop: keep-alive, window hiding. */
void rave_frame(void);

/* True when RetroArch runs inside RAVE without --show-window: its window must never be shown or take the focus
 * (win32_set_window skips ShowWindow / SetForegroundWindow) - the game window keeps the keyboard and the pads. */
bool rave_hide_window(void);

/* Writes "VRCMD <text>" to stdout for the game. */
void rave_send(const char *text);

/* True when the frontend should send the core's frames by Spout: a --rave / --emuvr name, and not the capture core
 * (WindowCast sends its own Spout under the same name). */
bool rave_spout_wanted(void);

/* Combined XInput-style axis for port (0 LX, 1 LY up+, 2 RX, 3 RY up+, 4 LT, 5 RT). */
int16_t rave_pad_axis(unsigned port, unsigned axis);

/* Core audio (interleaved stereo, int16 or float) through the PAN matrix: returns a mixed copy, or data itself
 * until the game sent a PAN. */
const void *rave_pan_audio(const void *data, size_t samples, bool is_float);

struct core_option_manager;

/* Applies the --appendoptions files to a new core options manager; returns opt. */
struct core_option_manager *rave_apply_core_options(struct core_option_manager *opt);

/* True when the game's option files were applied: don't save them into RetroArch's own options file. */
bool rave_keep_core_options(void);

RETRO_END_DECLS

#endif
