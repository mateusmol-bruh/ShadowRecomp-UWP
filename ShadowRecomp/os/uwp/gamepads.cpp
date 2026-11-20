#include "platform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <SDL3/SDL.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

namespace
{
    using winrt::Windows::Gaming::Input::Gamepad;
    using winrt::Windows::Gaming::Input::GamepadButtons;

    constexpr std::array buttons = {
        GamepadButtons::A, GamepadButtons::B, GamepadButtons::X, GamepadButtons::Y,
        GamepadButtons::View, GamepadButtons::None, GamepadButtons::Menu,
        GamepadButtons::LeftThumbstick, GamepadButtons::RightThumbstick,
        GamepadButtons::LeftShoulder, GamepadButtons::RightShoulder,
        GamepadButtons::DPadUp, GamepadButtons::DPadDown,
        GamepadButtons::DPadLeft, GamepadButtons::DPadRight
    };
    static_assert(buttons.size() == SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1);

    struct GamepadSlot
    {
        Gamepad gamepad{ nullptr };
        SDL_JoystickID id{};
        SDL_Joystick* joystick{};
        bool firstReading = true;
    };

    std::array<GamepadSlot, 4> slots;

    bool SDLCALL Rumble(void* userdata, Uint16 low, Uint16 high)
    {
        const auto& slot = *static_cast<GamepadSlot*>(userdata);
        try
        {
            auto vibration = slot.gamepad.Vibration();
            vibration.LeftMotor = low / 65535.0;
            vibration.RightMotor = high / 65535.0;
            slot.gamepad.Vibration(vibration);
            return true;
        }
        catch (const winrt::hresult_error& error)
        {
            return SDL_SetError("Xbox gamepad vibration failed: %s", winrt::to_string(error.message()).c_str());
        }
    }

    void Disconnect(GamepadSlot& slot)
    {
        SDL_DetachVirtualJoystick(slot.id);
        if (slot.joystick)
            SDL_CloseJoystick(slot.joystick);
        slot = {};
    }

    void Connect(GamepadSlot& slot, const Gamepad& gamepad)
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = static_cast<Uint16>(buttons.size());
        desc.name = "Xbox Controller";
        desc.userdata = &slot;
        desc.Rumble = Rumble;

        slot.gamepad = gamepad;
        slot.id = SDL_AttachVirtualJoystick(&desc);
        if (slot.id)
            slot.joystick = SDL_OpenJoystick(slot.id);
        if (slot.joystick)
        {
            Uwp::LogStartup("Input: Xbox controller connected.");
            return;
        }

        Uwp::LogStartup(std::string("Input: SDL could not register the Xbox controller: ") + SDL_GetError());
        if (slot.id)
            Disconnect(slot);
        else
            slot = {};
    }

    Sint16 StickValue(double value)
    {
        if (!std::isfinite(value))
            return 0;
        return static_cast<Sint16>(std::clamp(value, -1.0, 1.0) * (value < 0.0 ? 32768.0 : 32767.0));
    }

    Sint16 TriggerValue(double value)
    {
        if (!std::isfinite(value))
            return -32768;
        return static_cast<Sint16>(std::clamp(value, 0.0, 1.0) * 65535.0 - 32768.0);
    }

    void PublishGamepadReading(GamepadSlot& slot)
    {
        if (slot.firstReading)
            Uwp::LogStartup("Input: requesting the first native controller reading.");
        const auto reading = slot.gamepad.GetCurrentReading();
        if (slot.firstReading)
            Uwp::LogStartup("Input: native controller reading received. Updating SDL.");

        for (size_t index = 0; index < buttons.size(); ++index)
        {
            SDL_SetJoystickVirtualButton(slot.joystick, static_cast<int>(index),
                (reading.Buttons & buttons[index]) != GamepadButtons::None);
        }

        SDL_SetJoystickVirtualAxis(slot.joystick, SDL_GAMEPAD_AXIS_LEFTX, StickValue(reading.LeftThumbstickX));
        SDL_SetJoystickVirtualAxis(slot.joystick, SDL_GAMEPAD_AXIS_LEFTY, StickValue(-reading.LeftThumbstickY));
        SDL_SetJoystickVirtualAxis(slot.joystick, SDL_GAMEPAD_AXIS_RIGHTX, StickValue(reading.RightThumbstickX));
        SDL_SetJoystickVirtualAxis(slot.joystick, SDL_GAMEPAD_AXIS_RIGHTY, StickValue(-reading.RightThumbstickY));
        SDL_SetJoystickVirtualAxis(slot.joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, TriggerValue(reading.LeftTrigger));
        SDL_SetJoystickVirtualAxis(slot.joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, TriggerValue(reading.RightTrigger));

        if (std::exchange(slot.firstReading, false))
            Uwp::LogStartup("Input: first controller reading queued for SDL.");
    }
}

void Uwp::UpdateGamepads()
{
    static Uint64 nextTopologyCheck = 0;
    const Uint64 now = SDL_GetTicks();
    if (now >= nextTopologyCheck)
    {
        nextTopologyCheck = now + 500;
        try
        {
            const auto connected = Gamepad::Gamepads();
            for (auto& slot : slots)
            {
                uint32_t index = 0;
                if (slot.gamepad && !connected.IndexOf(slot.gamepad, index))
                    Disconnect(slot);
            }

            for (const auto& gamepad : connected)
            {
                if (std::ranges::any_of(slots, [&](const GamepadSlot& slot) { return slot.gamepad == gamepad; }))
                    continue;

                const auto freeSlot = std::ranges::find_if(slots, [](const GamepadSlot& slot) { return !slot.gamepad; });
                if (freeSlot == slots.end())
                    break;

                Connect(*freeSlot, gamepad);
            }
        }
        catch (const winrt::hresult_error& error)
        {
            nextTopologyCheck = now + 5000;
            LogStartup("Input: Windows.Gaming.Input could not list controllers: " + winrt::to_string(error.message()));
        }
    }

    for (auto& slot : slots)
    {
        if (!slot.joystick)
            continue;

        try
        {
            PublishGamepadReading(slot);
        }
        catch (const winrt::hresult_error& error)
        {
            LogStartup("Input: Windows.Gaming.Input stopped reading a controller: " + winrt::to_string(error.message()));
            Disconnect(slot);
        }
    }
}
