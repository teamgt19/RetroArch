/*  RAVE patch set for RetroArch - keyboard + lightgun fed by the game's pipe (VRCMD KEYINPUT / GUN).
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 */

/* Inside RAVE, RetroArch runs hidden and gets NO system input: every key, button and gun shot comes over the pipe
 * (rave/rave.c keeps the state). I answer like a desktop input driver would: keyboard binds, the keyboard device,
 * and the gun as lightgun / mouse / pointer. Pads come from the "rave" joypad driver. */

#include <stdlib.h>

#include <libretro.h>

#include "../input_driver.h"
#include "../input_keymaps.h"
#include "../../configuration.h"
#include "../../rave/rave.h"

/* The game sends UV with v = 0 at the BOTTOM (Unity); libretro's gun Y is -0x7fff at the TOP.
 * VERIFY with a lightgun game against the old fork. */
#define RAVE_GUN_FLIP_Y 1

static bool rave_key_down(unsigned key)
{
   rave_state_t *st = rave_get_state();
   return st->input_enabled && key > 0 && key < RAVE_KEYS && key < RETROK_LAST && st->keys[key];
}

static bool rave_mouse_button(unsigned port, unsigned id)
{
   rave_state_t *st = rave_get_state();
   if (!st->input_enabled || port >= RAVE_MAX_PORTS)
      return false;
   /* The gun trigger is the left button (the fork's config binds gun_trigger_mbtn = 1). */
   return id == RETRO_DEVICE_ID_MOUSE_LEFT && st->gun_trigger[port];
}

static int16_t rave_gun_coord(unsigned port, bool y)
{
   rave_state_t *st = rave_get_state();
   float t = y ? st->gun_v[port] : st->gun_u[port];
#if RAVE_GUN_FLIP_Y
   if (y)
      t = 1.0f - t;
#endif
   if (t < 0.0f) t = 0.0f;
   if (t > 1.0f) t = 1.0f;
   return (int16_t)((t * 2.0f - 1.0f) * 0x7fff);
}

static bool rave_bind_pressed(const input_device_driver_t *joypad, rarch_joypad_info_t *joypad_info,
      const retro_keybind_set *binds, bool keyboard_mapping_blocked, unsigned port, unsigned id)
{
   const struct retro_keybind *bind = &binds[port][id];
   if (!RETRO_KEYBIND_VALID(bind))
      return false;
   if (!keyboard_mapping_blocked || id == RARCH_GAME_FOCUS_TOGGLE)
   {
      unsigned key = RETRO_KEYBIND_KEY(bind);
      if (key && rave_key_down(key))
         return true;
   }
   if (rave_mouse_button(port, bind->mbutton))
      return true;
   return false;
}

static void *rave_input_init(const char *joypad_driver)
{
   return (void*)-1;
}

static void rave_input_poll(void *data) { }

static int16_t rave_input_state(
      void *data,
      const input_device_driver_t *joypad,
      const input_device_driver_t *sec_joypad,
      rarch_joypad_info_t *joypad_info,
      const retro_keybind_set *binds,
      bool keyboard_mapping_blocked,
      unsigned port,
      unsigned device,
      unsigned idx,
      unsigned id)
{
   rave_state_t *st = rave_get_state();
   if (port >= MAX_USERS)
      return 0;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
         {
            unsigned i;
            int16_t ret = 0;
            for (i = 0; i < RARCH_FIRST_CUSTOM_BIND; i++)
               if (rave_bind_pressed(joypad, joypad_info, binds, keyboard_mapping_blocked, port, i))
                  ret |= (1 << i);
            return ret;
         }
         if (id < RARCH_BIND_LIST_END)
            return rave_bind_pressed(joypad, joypad_info, binds, keyboard_mapping_blocked, port, id);
         break;

      case RETRO_DEVICE_KEYBOARD:
         return rave_key_down(id);

      case RETRO_DEVICE_LIGHTGUN:
         if (port >= RAVE_MAX_PORTS)
            break;
         switch (id)
         {
            case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:
               return rave_gun_coord(port, false);
            case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:
               return rave_gun_coord(port, true);
            case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
               return st->gun_offscreen[port];
            case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:
               return st->input_enabled && st->gun_trigger[port] && !st->gun_offscreen[port];
            case RETRO_DEVICE_ID_LIGHTGUN_RELOAD:
               return st->input_enabled && st->gun_trigger[port] && st->gun_offscreen[port];
            case RETRO_DEVICE_ID_LIGHTGUN_AUX_A:
            case RETRO_DEVICE_ID_LIGHTGUN_AUX_B:
            case RETRO_DEVICE_ID_LIGHTGUN_AUX_C:
            case RETRO_DEVICE_ID_LIGHTGUN_START:
            case RETRO_DEVICE_ID_LIGHTGUN_SELECT:
            case RETRO_DEVICE_ID_LIGHTGUN_DPAD_UP:
            case RETRO_DEVICE_ID_LIGHTGUN_DPAD_DOWN:
            case RETRO_DEVICE_ID_LIGHTGUN_DPAD_LEFT:
            case RETRO_DEVICE_ID_LIGHTGUN_DPAD_RIGHT:
            case RETRO_DEVICE_ID_LIGHTGUN_PAUSE:
               {
                  /* Gun buttons through the normal gun binds: pad buttons (rave joypad) or keys. */
                  unsigned new_id                = input_driver_lightgun_id_convert(id);
                  const uint64_t bind_joykey     = input_config_binds[port][new_id].joykey;
                  const uint64_t autobind_joykey = input_autoconf_binds[port][new_id].joykey;
                  const uint64_t joykey          = (bind_joykey != NO_BTN) ? bind_joykey : autobind_joykey;
                  if (joypad && (uint16_t)joykey != NO_BTN && joypad->button(joypad_info->joy_idx, (uint16_t)joykey))
                     return 1;
                  return rave_bind_pressed(joypad, joypad_info, binds, keyboard_mapping_blocked, port, new_id);
               }
            default:
               break;
         }
         break;

      case RETRO_DEVICE_MOUSE:
      case RARCH_DEVICE_MOUSE_SCREEN:
         if (port >= RAVE_MAX_PORTS)
            break;
         switch (id)
         {
            case RETRO_DEVICE_ID_MOUSE_LEFT:
               return rave_mouse_button(port, id);
            default:
               break; /* no relative motion: the gun is absolute */
         }
         break;

      case RETRO_DEVICE_POINTER:
      case RARCH_DEVICE_POINTER_SCREEN:
         if (port >= RAVE_MAX_PORTS || idx != 0)
            break;
         switch (id)
         {
            case RETRO_DEVICE_ID_POINTER_X:
               return rave_gun_coord(port, false);
            case RETRO_DEVICE_ID_POINTER_Y:
               return rave_gun_coord(port, true);
            case RETRO_DEVICE_ID_POINTER_PRESSED:
               return st->input_enabled && st->gun_trigger[port] && !st->gun_offscreen[port];
            case RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN:
               return st->gun_offscreen[port];
            default:
               break;
         }
         break;
   }
   return 0;
}

static void rave_input_free(void *data) { }

static uint64_t rave_input_get_capabilities(void *data)
{
   return (1 << RETRO_DEVICE_JOYPAD)
        | (1 << RETRO_DEVICE_ANALOG)
        | (1 << RETRO_DEVICE_KEYBOARD)
        | (1 << RETRO_DEVICE_MOUSE)
        | (1 << RETRO_DEVICE_LIGHTGUN)
        | (1 << RETRO_DEVICE_POINTER);
}

input_driver_t input_rave = {
   rave_input_init,
   rave_input_poll,
   rave_input_state,
   rave_input_free,
   NULL, /* set_sensor_state */
   NULL, /* get_sensor_input */
   rave_input_get_capabilities,
   "rave",
   NULL, /* grab_mouse */
   NULL, /* grab_stdin */
   NULL  /* keypress_vibrate */
};
