// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <dinputd.h>

namespace g25::directinput {
// {D7A3D8CB-8B3C-4C35-A552-73A4BEB529E0}
inline constexpr CLSID class_id{0xd7a3d8cb, 0x8b3c, 0x4c35,
    {0xa5, 0x52, 0x73, 0xa4, 0xbe, 0xb5, 0x29, 0xe0}};
inline constexpr DWORD constant_effect_id = 0;
inline constexpr DWORD ramp_effect_id = 1;
inline constexpr DWORD square_effect_id = 2;
inline constexpr DWORD sine_effect_id = 3;
inline constexpr DWORD triangle_effect_id = 4;
inline constexpr DWORD sawtooth_up_effect_id = 5;
inline constexpr DWORD sawtooth_down_effect_id = 6;
inline constexpr DWORD spring_effect_id = 7;
inline constexpr DWORD damper_effect_id = 8;
inline constexpr DWORD inertia_effect_id = 9;
inline constexpr DWORD friction_effect_id = 10;
inline constexpr DWORD custom_effect_id = 11;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** object);
extern "C" HRESULT __stdcall DllCanUnloadNow();
