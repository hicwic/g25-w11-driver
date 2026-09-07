// SPDX-License-Identifier: GPL-2.0-only
#include "device/directinput_probe.h"
#include "device/hid_transport.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace g25 {
namespace {
template<class T> struct ComRelease { void operator()(T* ptr) const noexcept { if (ptr) ptr->Release(); } };
template<class T> using ComPtr = std::unique_ptr<T, ComRelease<T>>;
struct Context { IDirectInput8W* input; unsigned found{}; };
struct FindContext { GUID instance{}; bool found{}; };
BOOL CALLBACK find_g25(const DIDEVICEINSTANCEW* instance, void* opaque) {
    if (LOWORD(instance->guidProduct.Data1) == logitech_vid && HIWORD(instance->guidProduct.Data1) == g25_pid) {
        auto& result = *static_cast<FindContext*>(opaque);
        result.instance = instance->guidInstance;
        result.found = true;
        return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
}
struct EffectInventory { unsigned total{}; bool supported[12]{}; };
BOOL CALLBACK enumerate_effects(const DIEFFECTINFOW* info, void* opaque) {
    auto& effects = *static_cast<EffectInventory*>(opaque);
    ++effects.total;
    const GUID* standard[]{&GUID_ConstantForce, &GUID_RampForce, &GUID_Square, &GUID_Sine,
        &GUID_Triangle, &GUID_SawtoothUp, &GUID_SawtoothDown, &GUID_Spring,
        &GUID_Damper, &GUID_Inertia, &GUID_Friction, &GUID_CustomForce};
    for (unsigned index = 0; index < 12; ++index)
        if (IsEqualGUID(info->guid, *standard[index])) effects.supported[index] = true;
    return DIENUM_CONTINUE;
}
BOOL CALLBACK enumerate(const DIDEVICEINSTANCEW* instance, void* opaque) {
    // No exception may unwind through a Windows C callback.
    try {
        if (LOWORD(instance->guidProduct.Data1) != logitech_vid || !supported_pid(HIWORD(instance->guidProduct.Data1)))
            return DIENUM_CONTINUE;
        auto& context = *static_cast<Context*>(opaque); ++context.found;
        std::cout << "    " << utf8(instance->tszInstanceName);
        IDirectInputDevice8W* raw{};
        const auto hr = context.input->CreateDevice(instance->guidInstance, &raw, nullptr);
        if (FAILED(hr)) { std::cout << " CreateDevice HRESULT=" << std::hex << hr << std::dec << '\n'; return DIENUM_CONTINUE; }
        ComPtr<IDirectInputDevice8W> device(raw);
        DIDEVCAPS caps{}; caps.dwSize = sizeof(caps);
        const auto result = device->GetCapabilities(&caps);
        if (FAILED(result)) std::cout << " GetCapabilities HRESULT=" << std::hex << result << std::dec;
        else {
            std::cout << " axes=" << caps.dwAxes << " buttons=" << caps.dwButtons << " POVs=" << caps.dwPOVs
                      << " DIDC_FORCEFEEDBACK=" << ((caps.dwFlags & DIDC_FORCEFEEDBACK) ? "yes" : "no");
            if (caps.dwFlags & DIDC_FORCEFEEDBACK) {
                EffectInventory effects;
                const auto enumerated = device->EnumEffects(enumerate_effects, &effects, DIEFT_ALL);
                if (SUCCEEDED(enumerated)) {
                    const auto standard_count = static_cast<unsigned>(std::count(std::begin(effects.supported),
                                                                                std::end(effects.supported), true));
                    std::cout << " effects=" << effects.total << " standard=" << standard_count << "/12";
                }
            }
        }
        std::cout << '\n';
    } catch (const std::exception& error) { std::cerr << "DirectInput diagnostic: " << error.what() << '\n'; }
    catch (...) { std::cerr << "DirectInput diagnostic failed\n"; }
    return DIENUM_CONTINUE;
}
}

void run_directinput_test(std::string_view test_effect, HANDLE stop_event) {
    IDirectInput8W* raw_input{};
    auto result = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                     reinterpret_cast<void**>(&raw_input), nullptr);
    if (FAILED(result)) throw std::runtime_error("DirectInput8Create failed HRESULT=" + std::to_string(result));
    ComPtr<IDirectInput8W> input(raw_input);
    FindContext found;
    result = input->EnumDevices(DI8DEVCLASS_GAMECTRL, find_g25, &found, DIEDFL_ATTACHEDONLY);
    if (FAILED(result) || !found.found) throw std::runtime_error("native G25 not found through DirectInput");
    IDirectInputDevice8W* raw_device{};
    result = input->CreateDevice(found.instance, &raw_device, nullptr);
    if (FAILED(result)) throw std::runtime_error("DirectInput CreateDevice failed HRESULT=" + std::to_string(result));
    ComPtr<IDirectInputDevice8W> device(raw_device);
    result = device->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(result)) throw std::runtime_error("DirectInput SetDataFormat failed HRESULT=" + std::to_string(result));
    auto window = GetConsoleWindow();
    bool owns_window = false;
    if (!window) {
        window = CreateWindowExW(0, L"STATIC", L"g25tool DirectInput", WS_OVERLAPPED,
                                 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        owns_window = window != nullptr;
    }
    if (!window) throw std::runtime_error("could not create a DirectInput cooperative-level window");
    struct WindowGuard { HWND value; bool owned; ~WindowGuard() { if (owned) DestroyWindow(value); } } window_guard{window, owns_window};
    result = device->SetCooperativeLevel(window, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
    if (FAILED(result)) throw std::runtime_error("DirectInput SetCooperativeLevel failed HRESULT=" + std::to_string(result));
    result = device->Acquire();
    if (FAILED(result)) throw std::runtime_error("DirectInput Acquire failed HRESULT=" + std::to_string(result));
    struct Unacquire { IDirectInputDevice8W* value; ~Unacquire() { value->Unacquire(); } } unacquire{device.get()};

    DWORD axis = DIJOFS_X;
    LONG direction = DI_FFNOMINALMAX;
    DICONSTANTFORCE force{3000};
    DIRAMPFORCE ramp{-3000, 3000};
    DIPERIODIC periodic{3000, 0, 0, 250'000};
    DICONDITION condition{};
    condition.lPositiveCoefficient = DI_FFNOMINALMAX;
    condition.lNegativeCoefficient = DI_FFNOMINALMAX;
    condition.dwPositiveSaturation = 3000;
    condition.dwNegativeSaturation = 3000;
    std::array<LONG, 4> custom_samples{-3000, 0, 3000, 0};
    DICUSTOMFORCE custom{1, 62'500, static_cast<DWORD>(custom_samples.size()),
                         custom_samples.data()};
    const GUID* effect_guid = &GUID_ConstantForce;
    void* type_parameters = &force;
    DWORD parameter_size = sizeof(force);
    if (test_effect == "ramp") { effect_guid = &GUID_RampForce; type_parameters = &ramp; parameter_size = sizeof(ramp); }
    else if (test_effect == "square") { effect_guid = &GUID_Square; type_parameters = &periodic; parameter_size = sizeof(periodic); }
    else if (test_effect == "sine") { effect_guid = &GUID_Sine; type_parameters = &periodic; parameter_size = sizeof(periodic); }
    else if (test_effect == "triangle") { effect_guid = &GUID_Triangle; type_parameters = &periodic; parameter_size = sizeof(periodic); }
    else if (test_effect == "saw-up") { effect_guid = &GUID_SawtoothUp; type_parameters = &periodic; parameter_size = sizeof(periodic); }
    else if (test_effect == "saw-down") { effect_guid = &GUID_SawtoothDown; type_parameters = &periodic; parameter_size = sizeof(periodic); }
    else if (test_effect == "spring") { effect_guid = &GUID_Spring; type_parameters = &condition; parameter_size = sizeof(condition); }
    else if (test_effect == "damper") { effect_guid = &GUID_Damper; type_parameters = &condition; parameter_size = sizeof(condition); }
    else if (test_effect == "inertia") { effect_guid = &GUID_Inertia; type_parameters = &condition; parameter_size = sizeof(condition); }
    else if (test_effect == "friction") { effect_guid = &GUID_Friction; type_parameters = &condition; parameter_size = sizeof(condition); }
    else if (test_effect == "custom") { effect_guid = &GUID_CustomForce; type_parameters = &custom; parameter_size = sizeof(custom); }
    DIEFFECT definition{};
    definition.dwSize = sizeof(definition);
    definition.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    definition.dwDuration = 1'000'000;
    definition.dwSamplePeriod = 0;
    definition.dwGain = DI_FFNOMINALMAX;
    definition.dwTriggerButton = DIEB_NOTRIGGER;
    definition.cAxes = 1;
    definition.rgdwAxes = &axis;
    definition.rglDirection = &direction;
    definition.cbTypeSpecificParams = parameter_size;
    definition.lpvTypeSpecificParams = type_parameters;
    IDirectInputEffect* raw_effect{};
    result = device->CreateEffect(*effect_guid, &definition, &raw_effect, nullptr);
    if (FAILED(result)) throw std::runtime_error("DirectInput CreateEffect failed HRESULT=" + std::to_string(result));
    ComPtr<IDirectInputEffect> effect(raw_effect);
    std::cout << "DirectInput " << test_effect << " effect, bounded to 30%, for at most 1 second; Ctrl+C stops.\n" << std::flush;
    result = effect->Start(1, 0);
    if (FAILED(result)) throw std::runtime_error("DirectInput effect Start failed HRESULT=" + std::to_string(result));
    WaitForSingleObject(stop_event, 1000);
    effect->Stop();
    device->SendForceFeedbackCommand(DISFFC_STOPALL);
    std::cout << "DirectInput effect stopped.\n";
}
void print_directinput_inventory() {
    std::cout << "DirectInput inventory (read-only capability query; no effects played):\n";
    IDirectInput8W* raw{};
    const auto hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                      reinterpret_cast<void**>(&raw), nullptr);
    if (FAILED(hr)) { std::cout << "    DirectInput8Create HRESULT=" << std::hex << hr << std::dec << '\n'; return; }
    ComPtr<IDirectInput8W> input(raw);
    Context context{input.get()};
    const auto result = input->EnumDevices(DI8DEVCLASS_GAMECTRL, enumerate, &context, DIEDFL_ATTACHEDONLY);
    if (FAILED(result)) std::cout << "    EnumDevices HRESULT=" << std::hex << result << std::dec << '\n';
    if (!context.found) std::cout << "    No matching Logitech wheel exposed.\n";
}
}
