// SPDX-License-Identifier: GPL-2.0-only
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include "directinput/g25_ff_driver.h"
#include <windows.h>
#include <dinput.h>
#include <dinputd.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected DLL path");
        const auto module = LoadLibraryA(argv[1]);
        check(module != nullptr, "LoadLibraryA failed");
        const auto unload = reinterpret_cast<HRESULT(__stdcall*)()>(GetProcAddress(module, "DllCanUnloadNow"));
        const auto get_class = reinterpret_cast<HRESULT(__stdcall*)(REFCLSID, REFIID, void**)>(
            GetProcAddress(module, "DllGetClassObject"));
        check(unload && get_class, "COM exports missing");
        check(unload() == S_OK, "fresh DLL must be unloadable");

        IClassFactory* factory{};
        check(get_class(g25::directinput::class_id, IID_IClassFactory,
                        reinterpret_cast<void**>(&factory)) == S_OK && factory,
              "class factory creation");
        check(unload() == S_FALSE, "factory must keep DLL live");
        IDirectInputEffectDriver* driver{};
        const IID driver_iid{0x02538130, 0x898f, 0x11d0,
            {0x9a, 0xd0, 0x00, 0xa0, 0xc9, 0xa0, 0x6e, 0x35}};
        check(factory->CreateInstance(nullptr, driver_iid, reinterpret_cast<void**>(&driver)) == S_OK && driver,
              "effect driver creation");
        DIDRIVERVERSIONS versions{}; versions.dwSize = sizeof(versions);
        check(driver->GetVersions(&versions) == S_OK && versions.dwFFDriverVersion == 0x00010000,
              "driver version");
        DIDEVICESTATE state{}; state.dwSize = sizeof(state);
        check(FAILED(driver->GetForceFeedbackState(123, &state)), "uninitialized device rejected");
        driver->Release();
        factory->Release();
        check(unload() == S_OK, "released objects must permit unload");
        FreeLibrary(module);
        std::cout << "DirectInput COM loading checks passed (no hardware I/O)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
