/*  RAVE patch set for RetroArch - the game <-> RetroArch pipe (drop-in for EmuVR's 1.7.5 fork).
 *  Protocol: F:\WORK\RAVE Standalone\retroarch_rave_protocol.md
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <features/features_cpu.h>
#include <string/stdstring.h>
#include <compat/strl.h>

#include "rave.h"
#include "../command.h"
#include "../verbosity.h"
#include "../input/input_driver.h"
#include "../runloop.h"
#include "rave_spout.h"
#include <dynamic/dylib.h>
#include <file/config_file.h>
#include "../core_option_manager.h"
#ifdef HAVE_NETWORKING
#include "../network/netplay/netplay.h"
#endif

#if defined(_WIN32) && !defined(_XBOX) && !defined(__WINRT__)
#include <windows.h>
#include "../gfx/common/win32_common.h"
#define RAVE_HAVE_WIN32_WINDOW 1
#endif

static rave_state_t rave_st;

/* The video core's RAVE functions (our ffmpeg core exports them), looked up once per loaded core. */
typedef void    (*rave_video_seek_t)(double seconds, int absolute);
typedef int64_t (*rave_video_time_ms_t)(void);
static dylib_t              rave_video_lib;
static rave_video_seek_t    rave_video_seek_fn;
static rave_video_time_ms_t rave_video_time_fn;
static int64_t              rave_last_time_send_us;

#ifdef HAVE_FFMPEG
/* The built-in media player (builtin_mediaplayer_enable) is the same ffmpeg core, linked in with prefixed names. */
void    libretro_ffmpeg_rave_video_seek(double seconds, int absolute);
int64_t libretro_ffmpeg_rave_video_time_ms(void);
#endif

static void rave_video_lookup(void)
{
   dylib_t lib = runloop_state_get_ptr()->lib_handle;
#ifdef HAVE_FFMPEG
   if (runloop_state_get_ptr()->current_core_type == CORE_TYPE_FFMPEG)
   {
      rave_video_seek_fn = libretro_ffmpeg_rave_video_seek;
      rave_video_time_fn = libretro_ffmpeg_rave_video_time_ms;
      rave_video_lib     = NULL;
      return;
   }
#endif
   if (lib == rave_video_lib && (lib || !rave_video_time_fn))
      return;
   rave_video_lib     = lib;
   rave_video_seek_fn = lib ? (rave_video_seek_t)dylib_proc(lib, "rave_video_seek") : NULL;
   rave_video_time_fn = lib ? (rave_video_time_ms_t)dylib_proc(lib, "rave_video_time_ms") : NULL;
   RARCH_LOG("[RAVE] core %p: video functions %s.\n", (void*)lib, rave_video_time_fn ? "found" : "not found");
}

/* VIDEO PAUSE 0|1, VIDEO SEEK <seconds> (absolute - netplay clients follow the host), VIDEO SEEK_FRAMES <n>
 * (relative; n is in SECONDS - the game sends +-10 / +-60 for the stick, like the old ffmpeg core's seek buttons;
 * VERIFY against the fork). */
static void rave_video(const char *args)
{
   if (string_starts_with(args, "PAUSE "))
   {
      command_event(atoi(args + 6) ? CMD_EVENT_PAUSE : CMD_EVENT_UNPAUSE, NULL);
      return;
   }
   rave_video_lookup();
   if (!rave_video_seek_fn)
   {
      RARCH_LOG("[RAVE] VIDEO %s: the loaded core has no RAVE video functions.\n", args);
      return;
   }
   if (string_starts_with(args, "SEEK_FRAMES "))
      rave_video_seek_fn(atof(args + 12), 0);
   else if (string_starts_with(args, "SEEK "))
      rave_video_seek_fn(atof(args + 5), 1);
}

/* NETPLAY HOST|STOP, RESYNC. A host resyncs by sending its state to everyone; a client can't ask for that upstream,
 * so it reconnects (the game restarts a client RetroArch on VRCMD RESTART) and gets the host's full state. */
static void rave_netplay(const char *word, const char *args)
{
#ifdef HAVE_NETWORKING
   if (string_is_equal(word, "NETPLAY"))
   {
      if (string_starts_with(args, "HOST"))
         command_event(CMD_EVENT_NETPLAY_ENABLE_HOST, NULL);
      else if (string_starts_with(args, "STOP"))
         command_event(CMD_EVENT_NETPLAY_DISCONNECT, NULL);
      return;
   }
   /* RESYNC */
   if (netplay_driver_ctl(RARCH_NETPLAY_CTL_IS_SERVER, NULL))
      netplay_force_send_savestate();
   else if (netplay_driver_ctl(RARCH_NETPLAY_CTL_IS_CONNECTED, NULL))
      rave_send("RESTART");
#else
   RARCH_WARN("[RAVE] %s: built without networking.\n", word);
#endif
}

rave_state_t *rave_get_state(void)
{
   return &rave_st;
}

/* I take my switches out of argv so RetroArch's own parser never sees them. */
void rave_parse_args(int *argc, char **argv)
{
   static bool parsed;
   int i, out = 0;
   /* Netplay host / content reloads run the startup again: keep the first launch's switches. */
   if (parsed)
      return;
   parsed      = true;
   rave_st.ttl = 6; /* the game's default */
   for (i = 0; i < *argc; i++)
   {
      const char *a   = argv[i];
      const char *val = (i + 1 < *argc) ? argv[i + 1] : NULL;
      if ((string_is_equal(a, "--rave") || string_is_equal(a, "--emuvr")) && val)
      {
         rave_st.active      = true;
         rave_st.legacy_name = string_is_equal(a, "--emuvr");
         strlcpy(rave_st.name, val, sizeof(rave_st.name));
         i++;
         continue;
      }
      if (string_is_equal(a, "--ttl") && val)
      {
         rave_st.ttl = (unsigned)strtoul(val, NULL, 10);
         i++;
         continue;
      }
      if (string_is_equal(a, "--appendoptions") && val)
      {
         strlcpy(rave_st.append_options, val, sizeof(rave_st.append_options));
         i++;
         continue;
      }
      if (string_is_equal(a, "--oculus-audio") && val)
      {
         /* Rift headphones as the audio device: not done yet (logged). */
         RARCH_WARN("[RAVE] --oculus-audio %s: audio device pick not implemented yet.\n", val);
         i++;
         continue;
      }
      if (string_is_equal(a, "--enable-input")) { rave_st.input_enabled = true; continue; }
      if (string_is_equal(a, "--show-window"))  { rave_st.show_window   = true; continue; }
      if (string_is_equal(a, "--headless"))     { rave_st.headless      = true; continue; }
      if (string_is_equal(a, "--lightgun"))     { rave_st.lightgun      = true; continue; }
      if (string_is_equal(a, "--game-focus"))   { rave_st.game_focus    = true; continue; }
      if (string_is_equal(a, "--cpu"))
      {
         RARCH_WARN("[RAVE] --cpu: not implemented yet (ignored).\n");
         continue;
      }
      argv[out++] = argv[i];
   }
   argv[out] = NULL;
   *argc     = out;
   if (rave_st.active)
   {
      char sender[300];
      snprintf(sender, sizeof(sender), "%s %s", rave_st.legacy_name ? "EmuVR" : "RAVE", rave_st.name);
      rave_spout_set_name(sender);
   }
   rave_st.last_ttl_us = cpu_features_get_time_usec();
}

struct core_option_manager *rave_apply_core_options(struct core_option_manager *opt)
{
   char list[sizeof(rave_st.append_options)];
   char *path, *next;
   if (!opt || !*rave_st.append_options)
      return opt;
   strlcpy(list, rave_st.append_options, sizeof(list));
   for (path = list; path; path = next)
   {
      struct config_entry_list *e;
      unsigned set    = 0;
      config_file_t *conf;
      if ((next = strchr(path, '|')))
         *next++ = '\0';
      if (!*path)
         continue;
      if (!(conf = config_file_new_from_path_to_string(path)))
      {
         RARCH_WARN("[RAVE] Core options file not found: \"%s\".\n", path);
         continue;
      }
      /* Keys for other cores and values a core doesn't offer are skipped. */
      for (e = conf->entries; e; e = e->next)
      {
         size_t idx, val_idx;
         if (     e->key && e->value
               && core_option_manager_get_idx(opt, e->key, &idx)
               && core_option_manager_get_val_idx(opt, idx, e->value, &val_idx))
         {
            opt->opts[idx].index = val_idx;
            set++;
         }
      }
      config_file_free(conf);
      RARCH_LOG("[RAVE] Core options from \"%s\": %u set.\n", path, set);
   }
   opt->updated            = true;
   rave_st.options_applied = true;
   return opt;
}

bool rave_keep_core_options(void)
{
   return rave_st.options_applied;
}

void rave_send(const char *text)
{
   if (!rave_st.active)
      return;
   fprintf(stdout, "VRCMD %s\n", text);
   fflush(stdout);
}

static void rave_key(bool down, unsigned key)
{
   if (key == 0 || key >= RAVE_KEYS)
      return;
   if (rave_st.keys[key] == down)
      return;
   rave_st.keys[key] = down;
   /* Cores with a keyboard callback get the event too (the character only for plain ASCII keys). */
   input_keyboard_event(down, key, (key < 128) ? key : 0, 0, RETRO_DEVICE_KEYBOARD);
}

/* Optional player prefix right after the command word: "P1".."P8" (no prefix = player 1, the old game's form).
 * Multiplayer: the player number a stream / cabinet assigns is the port the game's commands carry. */
static unsigned rave_take_port(const char **args)
{
   const char *a = *args;
   if ((a[0] == 'P' || a[0] == 'p') && a[1] >= '1' && a[1] <= '9' && (a[2] == ' ' || a[2] == '\0'))
   {
      unsigned port = (unsigned)(a[1] - '1');
      a += 2;
      while (*a == ' ')
         a++;
      *args = a;
      return port < RAVE_MAX_PORTS ? port : RAVE_MAX_PORTS;
   }
   return 0;
}

static void rave_gun(unsigned port, const char *args)
{
   float u, v;
   if (port >= RAVE_MAX_PORTS)
      return;
   if (string_starts_with(args, "UP"))
   {
      rave_st.gun_trigger[port] = false;
      return;
   }
   if (string_starts_with(args, "AIM "))
   {
      if (sscanf(args + 4, "%f %f", &u, &v) == 2)
      {
         rave_st.gun_u[port]         = u;
         rave_st.gun_v[port]         = v;
         rave_st.gun_offscreen[port] = false;
      }
      return;
   }
   if (sscanf(args, "%f %f", &u, &v) == 2)
   {
      /* GUN x y = trigger down at x,y; -2 -2 = off-screen shot (reload). */
      rave_st.gun_offscreen[port] = (u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f);
      if (!rave_st.gun_offscreen[port])
      {
         rave_st.gun_u[port] = u;
         rave_st.gun_v[port] = v;
      }
      rave_st.gun_trigger[port] = true;
   }
}

void rave_handle_vrcmd(const char *cmd)
{
   char word[32];
   int  n = 0;
   const char *args;

   while (*cmd == ' ')
      cmd++;
   while (cmd[n] && cmd[n] != ' ' && n < (int)sizeof(word) - 1)
   {
      word[n] = cmd[n];
      n++;
   }
   word[n] = '\0';
   args    = cmd + n;
   while (*args == ' ')
      args++;

   if (string_is_equal(word, "TTL"))
   {
      rave_st.last_ttl_us = cpu_features_get_time_usec();
      rave_send("TTL");
   }
   else if (string_is_equal(word, "BINPUT"))
   {
      /* BINPUT [P<n>] DOWN|UP <button> */
      unsigned port   = rave_take_port(&args);
      bool down       = string_starts_with(args, "DOWN");
      const char *num = strchr(args, ' ');
      unsigned b      = num ? (unsigned)strtoul(num + 1, NULL, 10) : RAVE_PAD_BUTTONS;
      if (port < RAVE_MAX_PORTS && b < RAVE_PAD_BUTTONS)
      {
         if (down)
            rave_st.pads[port].buttons |=  (1u << b);
         else
            rave_st.pads[port].buttons &= ~(1u << b);
      }
   }
   else if (string_is_equal(word, "AINPUT"))
   {
      /* AINPUT [P<n>] <half axis> <value> */
      unsigned port = rave_take_port(&args);
      unsigned axis = 0;
      int value     = 0;
      if (port < RAVE_MAX_PORTS && sscanf(args, "%u %d", &axis, &value) == 2 && axis < RAVE_PAD_HALF_AXES)
      {
         if (value >  32767) value =  32767;
         if (value < -32767) value = -32767;
         rave_st.pads[port].half_axes[axis] = (int16_t)value;
      }
   }
   else if (string_is_equal(word, "MINPUT"))
   {
      /* MINPUT [P<n>] <dx> <dy>: trackball / spinner counts, added up until the core's next poll. */
      unsigned port = rave_take_port(&args);
      int dx = 0, dy = 0;
      if (port < RAVE_MAX_PORTS && sscanf(args, "%d %d", &dx, &dy) >= 1)
      {
         int64_t x = (int64_t)rave_st.mouse_acc_x[port] + dx;
         int64_t y = (int64_t)rave_st.mouse_acc_y[port] + dy;
         if (x >  1000000) x =  1000000; else if (x < -1000000) x = -1000000;
         if (y >  1000000) y =  1000000; else if (y < -1000000) y = -1000000;
         rave_st.mouse_acc_x[port] = (int32_t)x;
         rave_st.mouse_acc_y[port] = (int32_t)y;
      }
   }
   else if (string_is_equal(word, "KEYINPUT"))
   {
      bool down = string_starts_with(args, "DOWN");
      const char *num = strchr(args, ' ');
      if (num)
         rave_key(down, (unsigned)strtoul(num + 1, NULL, 10));
   }
   else if (string_is_equal(word, "GUN"))
   {
      /* GUN [P<n>] x y | AIM x y | UP */
      unsigned port = rave_take_port(&args);
      rave_gun(port, args);
   }
   else if (string_is_equal(word, "SETINPUT"))
   {
      rave_st.input_enabled = (atoi(args) != 0);
      if (!rave_st.input_enabled)
      {
         /* Let go of everything, so nothing stays held while the player is away. */
         unsigned i;
         memset(rave_st.pads, 0, sizeof(rave_st.pads));
         for (i = 1; i < RAVE_KEYS; i++)
            rave_key(false, i);
         memset(rave_st.gun_trigger, 0, sizeof(rave_st.gun_trigger));
         memset(rave_st.mouse_acc_x, 0, sizeof(rave_st.mouse_acc_x));
         memset(rave_st.mouse_acc_y, 0, sizeof(rave_st.mouse_acc_y));
         memset(rave_st.mouse_dx, 0, sizeof(rave_st.mouse_dx));
         memset(rave_st.mouse_dy, 0, sizeof(rave_st.mouse_dy));
      }
   }
   else if (string_is_equal(word, "PAN"))
   {
      float p[4];
      if (sscanf(args, "%f %f %f %f", &p[0], &p[1], &p[2], &p[3]) == 4)
      {
         memcpy(rave_st.pan, p, sizeof(p));
         rave_st.pan_set = true;
      }
   }
   else if (string_is_equal(word, "RESET"))
      command_event(CMD_EVENT_RESET, NULL);
   else if (string_is_equal(word, "VIDEO"))
      rave_video(args);
   else if (string_is_equal(word, "NETPLAY") || string_is_equal(word, "RESYNC"))
      rave_netplay(word, args);
   else
      /* NETPLAY HOST|STOP, RESYNC, VIDEO ...: next step of the port (logged so a missing one shows up). */
      RARCH_LOG("[RAVE] VRCMD %s: not implemented yet.\n", cmd);
}

bool rave_hide_window(void)
{
   return rave_st.active && !rave_st.show_window;
}

bool rave_spout_wanted(void)
{
   const char *core;
   if (!rave_st.active)
      return false;
   core = runloop_state_get_ptr()->system.info.library_name;
   return !(core && string_is_equal(core, "WindowCast"));
}

void rave_mouse_poll(void)
{
   unsigned i;
   for (i = 0; i < RAVE_MAX_PORTS; i++)
   {
      /* A frame takes at most one int16 of counts; anything over stays for the next frame. */
      int32_t x = rave_st.mouse_acc_x[i], y = rave_st.mouse_acc_y[i];
      if (x >  32767) x =  32767; else if (x < -32767) x = -32767;
      if (y >  32767) y =  32767; else if (y < -32767) y = -32767;
      rave_st.mouse_acc_x[i] -= x;
      rave_st.mouse_acc_y[i] -= y;
      rave_st.mouse_dx[i]     = (int16_t)x;
      rave_st.mouse_dy[i]     = (int16_t)y;
   }
}

int16_t rave_pad_axis(unsigned port, unsigned axis)
{
   const int16_t *h;
   int v = 0;
   if (port >= RAVE_MAX_PORTS || !rave_st.input_enabled)
      return 0;
   h = rave_st.pads[port].half_axes;
   switch (axis)
   {
      case 0: v = h[0] + h[1]; break;
      case 1: v = h[2] + h[3]; break;
      case 2: v = h[4] + h[5]; break;
      case 3: v = h[6] + h[7]; break;
      case 4: v = h[8] < 0 ? -h[8] : h[8]; break;
      case 5: v = h[9] < 0 ? -h[9] : h[9]; break;
      default: return 0;
   }
   if (v >  32767) v =  32767;
   if (v < -32767) v = -32767;
   return (int16_t)v;
}

const void *rave_pan_audio(const void *data, size_t samples, bool is_float)
{
   static void  *buf;
   static size_t buf_bytes;
   size_t i, frames = samples / 2;
   size_t bytes     = samples * (is_float ? sizeof(float) : sizeof(int16_t));
   float l1, l2, r1, r2;

   if (!rave_st.active || !data || frames == 0)
      return data;
   if (bytes > buf_bytes)
   {
      void *n = realloc(buf, bytes);
      if (!n)
         return data;
      buf       = n;
      buf_bytes = bytes;
   }
   /* Inside RAVE the sound only comes from the system's screen: silent until the game positions it (first PAN),
    * and always silent on a dedicated server (--headless, nobody listening). */
   if (rave_st.headless || !rave_st.pan_set)
   {
      memset(buf, 0, bytes);
      return buf;
   }
   /* left = l1*L + l2*R, right = r1*L + r2*R (speaker 1 = L, speaker 2 = R) - VERIFY the order vs the fork. */
   l1 = rave_st.pan[0]; l2 = rave_st.pan[1]; r1 = rave_st.pan[2]; r2 = rave_st.pan[3];
   if (is_float)
   {
      const float *in = (const float*)data;
      float *out      = (float*)buf;
      for (i = 0; i < frames; i++)
      {
         float l = in[i * 2], r = in[i * 2 + 1];
         out[i * 2]     = l1 * l + l2 * r;
         out[i * 2 + 1] = r1 * l + r2 * r;
      }
   }
   else
   {
      const int16_t *in = (const int16_t*)data;
      int16_t *out      = (int16_t*)buf;
      for (i = 0; i < frames; i++)
      {
         float l = in[i * 2], r = in[i * 2 + 1];
         float a = l1 * l + l2 * r, b = r1 * l + r2 * r;
         if (a > 32767.0f) a = 32767.0f; else if (a < -32768.0f) a = -32768.0f;
         if (b > 32767.0f) b = 32767.0f; else if (b < -32768.0f) b = -32768.0f;
         out[i * 2]     = (int16_t)a;
         out[i * 2 + 1] = (int16_t)b;
      }
   }
   return buf;
}

void rave_frame(void)
{
   if (!rave_st.active)
      return;

   /* Keep-alive: the game sends VRCMD TTL every second; no word for --ttl seconds = the game is gone. */
   if (rave_st.ttl > 0 && cpu_features_get_time_usec() - rave_st.last_ttl_us > (int64_t)rave_st.ttl * 1000000)
   {
      RARCH_LOG("[RAVE] no TTL from the game for %u s - quitting.\n", rave_st.ttl);
      rave_st.ttl = 0;
      command_event(CMD_EVENT_QUIT, NULL);
      return;
   }

   /* Video position for the game (VRCMD TIME <ms>), 10x a second; the game reads "paused" from it not moving. */
   if (cpu_features_get_time_usec() - rave_last_time_send_us > 100000)
   {
      rave_last_time_send_us = cpu_features_get_time_usec();
      rave_video_lookup();
      if (rave_video_time_fn)
      {
         char msg[48];
         snprintf(msg, sizeof(msg), "TIME %lld", (long long)rave_video_time_fn());
         rave_send(msg);
      }
   }

#ifdef RAVE_HAVE_WIN32_WINDOW
   /* RetroArch runs hidden inside RAVE (the picture goes out by Spout) unless --show-window. */
   if (!rave_st.show_window)
   {
      HWND hwnd = win32_get_window();
      if (hwnd && IsWindowVisible(hwnd))
      {
         ShowWindow(hwnd, SW_HIDE);
         rave_st.window_hidden = true;
      }
   }
#endif
}
