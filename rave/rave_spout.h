/*  RAVE patch set for RetroArch - Spout sender for the core's frame.
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 */
#ifndef RAVE_SPOUT_H__
#define RAVE_SPOUT_H__

#include <stddef.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

/* Empty name = no sender. */
void rave_spout_set_name(const char *name);

/* device = ID3D11Device*, frame_srv = ID3D11ShaderResourceView* of the core's frame (any format). */
void rave_spout_send_d3d11(void *device, void *frame_srv, unsigned width, unsigned height);

/* With a shader preset on: the picture AFTER RetroArch's shader passes. texture2d = ID3D11Texture2D* the last pass
 * drew into (the back buffer), x / y / width / height = the game's viewport rectangle in it. */
void rave_spout_send_d3d11_region(void *device, void *texture2d, unsigned x, unsigned y, unsigned width, unsigned height);

/* The same rectangle sent at out_width x out_height. */
void rave_spout_send_d3d11_region_scaled(void *device, void *texture2d, unsigned x, unsigned y, unsigned width, unsigned height,
      unsigned out_width, unsigned out_height);

/* Same values as enum retro_pixel_format. */
#define RAVE_PIXFMT_0RGB1555 0
#define RAVE_PIXFMT_XRGB8888 1
#define RAVE_PIXFMT_RGB565   2

/* The core's CPU frame, for any video driver but D3D11 (which sends from the GPU). */
void rave_spout_send_pixels(const void *data, unsigned width, unsigned height, size_t pitch, int format);

void rave_spout_release(void);

RETRO_END_DECLS

#endif
