/* RAVE: RetroArch's bundled gfx/include/dxsdk/dxgi1_6.h (found first on the include path) predates IDXGIFactory6 /
 * DXGI_GPU_PREFERENCE, which SpoutDirectX uses for the GPU preference. Declared here only when missing. */
#ifndef RAVE_DXGI6_SHIM_H
#define RAVE_DXGI6_SHIM_H

#ifdef __cplusplus
/* libstdc++ first: the Windows SAL macros (__out...) the D3D headers bring in break it. */
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#endif

#if defined(__cplusplus) && !defined(__IDXGIFactory6_INTERFACE_DEFINED__) && !defined(DXGI_GPU_PREFERENCE_DEFINED_RAVE)
#define DXGI_GPU_PREFERENCE_DEFINED_RAVE
#include <dxgi1_5.h>

typedef enum DXGI_GPU_PREFERENCE
{
   DXGI_GPU_PREFERENCE_UNSPECIFIED      = 0,
   DXGI_GPU_PREFERENCE_MINIMUM_POWER    = 1,
   DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE = 2
} DXGI_GPU_PREFERENCE;

MIDL_INTERFACE("c1b6694f-ff09-44a9-b03c-77900a0a1d17")
IDXGIFactory6 : public IDXGIFactory5
{
public:
   virtual HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(
         UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IDXGIFactory6, 0xc1b6694f, 0xff09, 0x44a9, 0xb0, 0x3c, 0x77, 0x90, 0x0a, 0x0a, 0x1d, 0x17)
#endif
#endif

/* The bundled d3d11 / dxgi headers also don't register interface ids for MinGW's C++ __uuidof. */
#if defined(__cplusplus) && defined(__CRT_UUID_DECL) && !defined(RAVE_DXSDK_UUIDS)
#define RAVE_DXSDK_UUIDS
#include <d3d11_1.h>
__CRT_UUID_DECL(ID3D11Device,         0xdb6f6ddb, 0xac77, 0x4e88, 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40)
__CRT_UUID_DECL(ID3D11Device1,        0xa04bfb29, 0x08ef, 0x43d6, 0xa4, 0x9c, 0xa9, 0xbd, 0xbd, 0xcb, 0xe6, 0x86)
__CRT_UUID_DECL(ID3D11DeviceContext1, 0xbb2c6faa, 0xb5fb, 0x4082, 0x8e, 0x6b, 0x38, 0x8b, 0x8c, 0xfa, 0x90, 0xe1)
__CRT_UUID_DECL(ID3D11Texture2D,      0x6f15aaf2, 0xd208, 0x4e89, 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c)
__CRT_UUID_DECL(IDXGIAdapter1,        0x29038f61, 0x3839, 0x4626, 0x91, 0xfd, 0x08, 0x68, 0x79, 0x01, 0x1a, 0x05)
__CRT_UUID_DECL(IDXGIFactory1,        0x770aae78, 0xf26f, 0x4dba, 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87)
__CRT_UUID_DECL(IDXGIFactory2,        0x50c83a1c, 0xe072, 0x4c48, 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0)
__CRT_UUID_DECL(IDXGIKeyedMutex,      0x9d8e1289, 0xd7b3, 0x465f, 0x81, 0x26, 0x25, 0x0e, 0x34, 0x9a, 0xf8, 0x5d)
__CRT_UUID_DECL(IDXGIResource,        0x035f3ab4, 0x482e, 0x4e50, 0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b)
#endif

#endif
