/*  RAVE patch set for RetroArch - Spout sender for the core's frame (D3D11 video driver).
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 *  Spout2 (rave/spout) is BSD licensed - see rave/spout/LICENSE-Spout2.txt.
 */

/* The game's screen receives the core's raw frame (no letterbox) - or, when a RetroArch shader preset is loaded, the
 * picture after its shader passes (rave_spout_send_d3d11_region) - as a Spout sender named
 * "RAVE <name>" ("EmuVR <name>" for --emuvr). The frame texture is in the core's format (RGB565 / BGRX / RGBA...),
 * so I draw it into my own BGRA texture first - in my own D3D11 state block, swapped in and out, so RetroArch's
 * pipeline state is untouched - then SpoutDX shares that texture. */

/* C++ headers before the Windows ones: their SAL macros (__out...) break libstdc++. */
#include <string>
#include <vector>
#include <stdio.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>

#include "spout/SpoutDX.h"
#include "spout/rave_dxgi6_shim.h"

extern "C" {
#include "rave_spout.h"
#include "../verbosity.h"
}

namespace
{
const char *kShader =
   "Texture2D t : register(t0); SamplerState s : register(s0);\n"
   "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
   "V vs(uint id : SV_VertexID) { V o; o.uv = float2((id << 1) & 2, id & 2);\n"
   "  o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
   "float4 ps(V i) : SV_Target { return float4(t.Sample(s, i.uv).rgb, 1); }\n";

struct Sender
{
   ID3D11Device             *device  = nullptr;
   ID3D11Device1            *device1 = nullptr;
   ID3D11DeviceContext1     *ctx1    = nullptr;
   ID3DDeviceContextState   *state   = nullptr;
   ID3D11VertexShader       *vs      = nullptr;
   ID3D11PixelShader        *ps      = nullptr;
   ID3D11SamplerState       *sampler = nullptr;
   ID3D11Texture2D          *rt      = nullptr;
   ID3D11RenderTargetView   *rtv     = nullptr;
   /* the shaded picture, copied out of the back buffer (same format as it) */
   ID3D11Texture2D          *copy    = nullptr;
   ID3D11ShaderResourceView *copySrv = nullptr;
   unsigned                  copyW   = 0, copyH = 0;
   DXGI_FORMAT               copyFmt = DXGI_FORMAT_UNKNOWN;
   unsigned                  width   = 0, height = 0;
   spoutDX                  *spout   = nullptr;
   std::string               name;
   bool                      failed  = false;
};
Sender g;

template <class T> void SafeRelease(T *&p) { if (p) { p->Release(); p = nullptr; } }

void ReleaseAll()
{
   if (g.spout) { g.spout->ReleaseSender(); g.spout->CloseDirectX11(); delete g.spout; g.spout = nullptr; }
   SafeRelease(g.copySrv); SafeRelease(g.copy); g.copyW = g.copyH = 0; g.copyFmt = DXGI_FORMAT_UNKNOWN;
   SafeRelease(g.rtv); SafeRelease(g.rt); SafeRelease(g.sampler); SafeRelease(g.ps); SafeRelease(g.vs);
   SafeRelease(g.state); SafeRelease(g.ctx1); SafeRelease(g.device1);
   g.device = nullptr; g.width = g.height = 0;
}

bool Compile(const char *entry, const char *target, ID3DBlob **out)
{
   ID3DBlob *err = nullptr;
   HRESULT hr = D3DCompile(kShader, strlen(kShader), "rave_spout", nullptr, nullptr, entry, target, 0, 0, out, &err);
   if (FAILED(hr))
      RARCH_ERR("[RAVE] Spout shader %s: %s\n", entry, err ? (const char*)err->GetBufferPointer() : "?");
   SafeRelease(err);
   return SUCCEEDED(hr);
}

bool Init(ID3D11Device *device)
{
   ID3DBlob *vsb = nullptr, *psb = nullptr;
   ID3D11DeviceContext *imm = nullptr;
   D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
   D3D11_SAMPLER_DESC sd = {};
   g.device = device;
   if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), (void**)&g.device1)))
      return false;
   device->GetImmediateContext(&imm);
   imm->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&g.ctx1);
   imm->Release();
   if (!g.ctx1)
      return false;
   if (FAILED(g.device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, &g.state)))
      return false;
   if (!Compile("vs", "vs_4_0", &vsb) || !Compile("ps", "ps_4_0", &psb))
      return false;
   device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g.vs);
   device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g.ps);
   vsb->Release(); psb->Release();
   sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
   sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
   device->CreateSamplerState(&sd, &g.sampler);
   g.spout = new spoutDX();
   if (!g.spout->OpenDirectX11(device))
      return false;
   g.spout->SetSenderName(g.name.c_str());
   RARCH_LOG("[RAVE] Spout sender \"%s\" on RetroArch's D3D11 device.\n", g.name.c_str());
   return g.vs && g.ps && g.sampler;
}

bool EnsureTarget(unsigned w, unsigned h)
{
   D3D11_TEXTURE2D_DESC td = {};
   if (g.rt && g.width == w && g.height == h)
      return true;
   SafeRelease(g.rtv); SafeRelease(g.rt);
   td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
   td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
   td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
   if (FAILED(g.device->CreateTexture2D(&td, nullptr, &g.rt)) || FAILED(g.device->CreateRenderTargetView(g.rt, nullptr, &g.rtv)))
      return false;
   g.width = w; g.height = h;
   return true;
}
}

extern "C" void rave_spout_set_name(const char *name)
{
   g.name = name ? name : "";
}

extern "C" void rave_spout_send_d3d11(void *device, void *frame_srv, unsigned width, unsigned height)
{
   ID3D11Device *dev = (ID3D11Device*)device;
   ID3D11ShaderResourceView *srv = (ID3D11ShaderResourceView*)frame_srv;
   ID3DDeviceContextState *previous = nullptr;
   D3D11_VIEWPORT vp = {};
   if (g.name.empty() || !dev || !srv || !width || !height || g.failed)
      return;
   if (g.device != dev)
   {
      ReleaseAll();
      if (!Init(dev))
      {
         RARCH_ERR("[RAVE] Spout sender could not start - the game will get no picture.\n");
         ReleaseAll();
         g.failed = true;
         return;
      }
   }
   if (!EnsureTarget(width, height))
      return;

   /* My own state block: nothing RetroArch set is touched. */
   g.ctx1->SwapDeviceContextState(g.state, &previous);
   vp.Width = (float)width; vp.Height = (float)height; vp.MaxDepth = 1.0f;
   g.ctx1->OMSetRenderTargets(1, &g.rtv, nullptr);
   g.ctx1->RSSetViewports(1, &vp);
   g.ctx1->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   g.ctx1->VSSetShader(g.vs, nullptr, 0);
   g.ctx1->PSSetShader(g.ps, nullptr, 0);
   g.ctx1->PSSetShaderResources(0, 1, &srv);
   g.ctx1->PSSetSamplers(0, 1, &g.sampler);
   g.ctx1->Draw(3, 0);
   {
      ID3D11ShaderResourceView *none = nullptr;
      g.ctx1->PSSetShaderResources(0, 1, &none);
      g.ctx1->OMSetRenderTargets(0, nullptr, nullptr);
   }
   g.ctx1->SwapDeviceContextState(previous, nullptr);
   if (previous)
      previous->Release();

   g.spout->SendTexture(g.rt);
}

/* The picture after RetroArch's shader passes: the game's rectangle of the back buffer, copied into a texture of my
 * own (a render target can't be read while it is bound, and the back buffer also holds the letterbox bars), then sent
 * the same way as the raw frame. */
extern "C" void rave_spout_send_d3d11_region(void *device, void *texture2d, unsigned x, unsigned y, unsigned width, unsigned height)
{
   ID3D11Device *dev = (ID3D11Device*)device;
   ID3D11Resource *res = (ID3D11Resource*)texture2d;
   ID3D11Texture2D *source = nullptr;
   D3D11_TEXTURE2D_DESC sd = {};
   D3D11_BOX box = {};
   if (g.name.empty() || !dev || !res || !width || !height || g.failed)
      return;
   if (g.device != dev)
   {
      ReleaseAll();
      if (!Init(dev))
      {
         RARCH_ERR("[RAVE] Spout sender could not start - the game will get no picture.\n");
         ReleaseAll();
         g.failed = true;
         return;
      }
   }
   if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&source)) || !source)
      return;
   source->GetDesc(&sd);
   /* the viewport can hang over the edge of the window */
   if (x >= sd.Width || y >= sd.Height || sd.SampleDesc.Count != 1)
   {
      source->Release();
      return;
   }
   if (x + width > sd.Width)
      width = sd.Width - x;
   if (y + height > sd.Height)
      height = sd.Height - y;
   if (!g.copy || g.copyW != width || g.copyH != height || g.copyFmt != sd.Format)
   {
      D3D11_TEXTURE2D_DESC td = {};
      SafeRelease(g.copySrv); SafeRelease(g.copy);
      td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
      td.Format = sd.Format; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
      td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      if (FAILED(dev->CreateTexture2D(&td, nullptr, &g.copy)) || FAILED(dev->CreateShaderResourceView(g.copy, nullptr, &g.copySrv)))
      {
         SafeRelease(g.copySrv); SafeRelease(g.copy);
         source->Release();
         return;
      }
      g.copyW = width; g.copyH = height; g.copyFmt = sd.Format;
      RARCH_LOG("[RAVE] Sending the SHADED picture: %u x %u (RetroArch shader preset on).\n", width, height);
   }
   box.left = x; box.top = y; box.front = 0; box.right = x + width; box.bottom = y + height; box.back = 1;
   g.ctx1->CopySubresourceRegion(g.copy, 0, 0, 0, 0, source, 0, &box);
   source->Release();
   rave_spout_send_d3d11(device, g.copySrv, width, height);
}

/* Any other video driver (the OpenGL fallback when NVIDIA Smooth Motion hooks D3D11): the core's CPU frame, converted
 * to BGRA on the CPU and shared from a small D3D11 device of my own - no GL/DX interop. */
namespace
{
spoutDX                    *cpu_spout = nullptr;
std::vector<unsigned char>  cpu_buf;
bool                        cpu_failed = false;
}

extern "C" void rave_spout_send_pixels(const void *data, unsigned width, unsigned height, size_t pitch, int format)
{
   unsigned x, y;
   if (g.name.empty() || !data || !width || !height || cpu_failed)
      return;
   if (!cpu_spout)
   {
      cpu_spout = new spoutDX();
      if (!cpu_spout->OpenDirectX11())
      {
         RARCH_ERR("[RAVE] Spout (CPU) sender could not start - the game will get no picture.\n");
         delete cpu_spout;
         cpu_spout  = nullptr;
         cpu_failed = true;
         return;
      }
      cpu_spout->SetSenderName(g.name.c_str());
      RARCH_LOG("[RAVE] Spout sender \"%s\" from CPU frames (non-D3D11 video driver).\n", g.name.c_str());
   }
   cpu_buf.resize((size_t)width * height * 4);
   for (y = 0; y < height; y++)
   {
      const unsigned char *row = (const unsigned char*)data + y * pitch;
      unsigned char *out       = &cpu_buf[(size_t)y * width * 4];
      switch (format)
      {
         case RAVE_PIXFMT_XRGB8888: /* memory B G R X */
            for (x = 0; x < width; x++)
            {
               out[x * 4 + 0] = row[x * 4 + 0];
               out[x * 4 + 1] = row[x * 4 + 1];
               out[x * 4 + 2] = row[x * 4 + 2];
               out[x * 4 + 3] = 255;
            }
            break;
         case RAVE_PIXFMT_RGB565:
            for (x = 0; x < width; x++)
            {
               unsigned p = ((const uint16_t*)row)[x];
               unsigned r = (p >> 11) & 31, gg = (p >> 5) & 63, b = p & 31;
               out[x * 4 + 0] = (unsigned char)((b << 3) | (b >> 2));
               out[x * 4 + 1] = (unsigned char)((gg << 2) | (gg >> 4));
               out[x * 4 + 2] = (unsigned char)((r << 3) | (r >> 2));
               out[x * 4 + 3] = 255;
            }
            break;
         default: /* 0RGB1555 */
            for (x = 0; x < width; x++)
            {
               unsigned p = ((const uint16_t*)row)[x];
               unsigned r = (p >> 10) & 31, gg = (p >> 5) & 31, b = p & 31;
               out[x * 4 + 0] = (unsigned char)((b << 3) | (b >> 2));
               out[x * 4 + 1] = (unsigned char)((gg << 3) | (gg >> 2));
               out[x * 4 + 2] = (unsigned char)((r << 3) | (r >> 2));
               out[x * 4 + 3] = 255;
            }
            break;
      }
   }
   cpu_spout->SendImage(cpu_buf.data(), width, height);
}

extern "C" void rave_spout_release(void)
{
   if (cpu_spout)
   {
      cpu_spout->ReleaseSender();
      cpu_spout->CloseDirectX11();
      delete cpu_spout;
      cpu_spout = nullptr;
   }
   ReleaseAll();
}
