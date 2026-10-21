/*  RAVE patch set for RetroArch - virtual pads fed by the game's pipe (VRCMD BINPUT / AINPUT).
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 */

/* I look exactly like RetroArch's XInput pad (buttons 0 A .. 10 Guide, D-pad as hat 0, axes 0 LX, 1 LY, 2 RX, 3 RY,
 * 4 LT, 5 RT), so the game's numbering and the "RAVE Pad" autoconfig profile (a copy of the XInput one) line up.
 * Eight pads (RAVE_MAX_PORTS), one per player: the game's commands carry the player number ("BINPUT P2 ..."; none = player 1). */

#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#include "../input_driver.h"
#include "../../tasks/tasks_internal.h"
#include "../../rave/rave.h"

#define RAVE_PAD_NAME "RAVE Pad"

static bool rave_joypad_pressed(unsigned port, uint16_t joykey)
{
   rave_state_t *st  = rave_get_state();
   unsigned hat_dir  = GET_HAT_DIR(joykey);
   uint32_t buttons;
   if (port >= RAVE_MAX_PORTS || !st->input_enabled)
      return false;
   buttons = st->pads[port].buttons;
   if (hat_dir)
   {
      switch (hat_dir)
      {
         case HAT_UP_MASK:    return (buttons >> RAVE_BTN_DPAD_UP)    & 1;
         case HAT_DOWN_MASK:  return (buttons >> RAVE_BTN_DPAD_DOWN)  & 1;
         case HAT_LEFT_MASK:  return (buttons >> RAVE_BTN_DPAD_LEFT)  & 1;
         case HAT_RIGHT_MASK: return (buttons >> RAVE_BTN_DPAD_RIGHT) & 1;
         default:             return false;
      }
   }
   /* 0-10 like XInput; 15-17 (D-pad modifier, triggers as buttons) reachable for custom binds. */
   if (joykey < RAVE_PAD_BUTTONS && (joykey <= 10 || joykey >= 15))
      return (buttons >> joykey) & 1;
   return false;
}

static int16_t rave_joypad_axis_value(unsigned port, uint32_t joyaxis)
{
   int16_t val;
   if (AXIS_NEG_GET(joyaxis) <= 3)
   {
      val = rave_pad_axis(port, AXIS_NEG_GET(joyaxis));
      return val < 0 ? val : 0;
   }
   if (AXIS_POS_GET(joyaxis) <= 5)
   {
      val = rave_pad_axis(port, AXIS_POS_GET(joyaxis));
      return val > 0 ? val : 0;
   }
   return 0;
}

static void *rave_joypad_init(void *data)
{
   unsigned i;
   for (i = 0; i < RAVE_MAX_PORTS; i++)
      input_autoconfigure_connect(RAVE_PAD_NAME, NULL, NULL, "rave", i, 0, 0);
   return (void*)-1;
}

static bool rave_joypad_query_pad(unsigned pad)
{
   return pad < RAVE_MAX_PORTS;
}

static void rave_joypad_destroy(void) { }

static int32_t rave_joypad_button(unsigned port, uint16_t joykey)
{
   return rave_joypad_pressed(port, joykey);
}

static int16_t rave_joypad_axis(unsigned port, uint32_t joyaxis)
{
   return rave_joypad_axis_value(port, joyaxis);
}

static int16_t rave_joypad_state(
      rarch_joypad_info_t *joypad_info,
      const struct retro_keybind *binds,
      unsigned port)
{
   unsigned i;
   int16_t ret       = 0;
   uint16_t port_idx = joypad_info->joy_idx;

   for (i = 0; i < RARCH_FIRST_CUSTOM_BIND; i++)
   {
      /* Auto-binds are per joypad, not per user. */
      const uint64_t joykey  = (binds[i].joykey != NO_BTN)
         ? binds[i].joykey  : joypad_info->auto_binds[i].joykey;
      const uint32_t joyaxis = (binds[i].joyaxis != AXIS_NONE)
         ? binds[i].joyaxis : joypad_info->auto_binds[i].joyaxis;
      if ((uint16_t)joykey != NO_BTN && rave_joypad_pressed(port_idx, (uint16_t)joykey))
         ret |= (1 << i);
      else if (joyaxis != AXIS_NONE &&
            ((float)abs(rave_joypad_axis_value(port_idx, joyaxis)) / 0x8000) > joypad_info->axis_threshold)
         ret |= (1 << i);
   }
   return ret;
}

static void rave_joypad_poll(void) { }

static const char *rave_joypad_name(unsigned pad)
{
   return pad < RAVE_MAX_PORTS ? RAVE_PAD_NAME : NULL;
}

input_device_driver_t rave_joypad = {
   rave_joypad_init,
   rave_joypad_query_pad,
   rave_joypad_destroy,
   rave_joypad_button,
   rave_joypad_state,
   NULL, /* get_buttons */
   rave_joypad_axis,
   rave_joypad_poll,
   NULL, /* set_rumble */
   NULL, /* set_rumble_gain */
   NULL, /* set_sensor_state */
   NULL, /* get_sensor_input */
   rave_joypad_name,
   "rave",
};
