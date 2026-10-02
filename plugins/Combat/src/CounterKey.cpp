#include "Common.h"
#include "CounterKey.h"
#include "CombatConfig.h"

#include <array>
#include <format>

namespace SLCombat::CounterKey
{
    namespace
    {
        using KB = RE::BSKeyboardDevice::Keys;

        struct KeyName
        {
            std::uint32_t key;
            const char*   name;
        };

        // Readable names for the keyboard keys a player would pick; others show their code
        constexpr std::array kKeyboardNames{
            KeyName{ KB::kQ, "Q" }, KeyName{ KB::kW, "W" }, KeyName{ KB::kE, "E" }, KeyName{ KB::kR, "R" },
            KeyName{ KB::kT, "T" }, KeyName{ KB::kY, "Y" }, KeyName{ KB::kU, "U" }, KeyName{ KB::kI, "I" },
            KeyName{ KB::kO, "O" }, KeyName{ KB::kP, "P" }, KeyName{ KB::kA, "A" }, KeyName{ KB::kS, "S" },
            KeyName{ KB::kD, "D" }, KeyName{ KB::kF, "F" }, KeyName{ KB::kG, "G" }, KeyName{ KB::kH, "H" },
            KeyName{ KB::kJ, "J" }, KeyName{ KB::kK, "K" }, KeyName{ KB::kL, "L" }, KeyName{ KB::kZ, "Z" },
            KeyName{ KB::kX, "X" }, KeyName{ KB::kC, "C" }, KeyName{ KB::kV, "V" }, KeyName{ KB::kB, "B" },
            KeyName{ KB::kN, "N" }, KeyName{ KB::kM, "M" },
            KeyName{ KB::kNum1, "1" }, KeyName{ KB::kNum2, "2" }, KeyName{ KB::kNum3, "3" }, KeyName{ KB::kNum4, "4" },
            KeyName{ KB::kNum5, "5" }, KeyName{ KB::kNum6, "6" }, KeyName{ KB::kNum7, "7" }, KeyName{ KB::kNum8, "8" },
            KeyName{ KB::kNum9, "9" }, KeyName{ KB::kNum0, "0" },
            KeyName{ KB::kF1, "F1" }, KeyName{ KB::kF2, "F2" }, KeyName{ KB::kF3, "F3" }, KeyName{ KB::kF4, "F4" },
            KeyName{ KB::kF5, "F5" }, KeyName{ KB::kF6, "F6" }, KeyName{ KB::kF7, "F7" }, KeyName{ KB::kF8, "F8" },
            KeyName{ KB::kF9, "F9" }, KeyName{ KB::kF10, "F10" }, KeyName{ KB::kF11, "F11" }, KeyName{ KB::kF12, "F12" },
            KeyName{ KB::kSpacebar, "Space" }, KeyName{ KB::kTab, "Tab" }, KeyName{ KB::kCapsLock, "Caps Lock" },
            KeyName{ KB::kLeftShift, "Left Shift" }, KeyName{ KB::kRightShift, "Right Shift" },
            KeyName{ KB::kLeftControl, "Left Ctrl" }, KeyName{ KB::kRightControl, "Right Ctrl" },
            KeyName{ KB::kLeftAlt, "Left Alt" }, KeyName{ KB::kRightAlt, "Right Alt" },
            KeyName{ KB::kTilde, "~" }, KeyName{ KB::kEnter, "Enter" }, KeyName{ KB::kBackspace, "Backspace" },
        };

        constexpr std::array kMouseNames{
            "Left Mouse", "Right Mouse", "Middle Mouse", "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8"
        };

        std::uint32_t BlockBinding(RE::INPUT_DEVICE a_device)
        {
            auto* controls = RE::ControlMap::GetSingleton();
            auto* userEvents = RE::UserEvents::GetSingleton();
            if (!controls || !userEvents) {
                return RE::ControlMap::kInvalid;
            }
            return controls->GetMappedKey(userEvents->leftAttack.c_str(), a_device);
        }

        int CodeOf(RE::INPUT_DEVICE a_device, std::uint32_t a_key)
        {
            if (a_key == RE::ControlMap::kInvalid) {
                return kGameBlockKey;
            }
            return a_device == RE::INPUT_DEVICE::kMouse ? kMouseOffset + static_cast<int>(a_key) : static_cast<int>(a_key);
        }
    }

    bool Valid(int a_code)
    {
        return a_code == kGameBlockKey || (a_code > 0 && a_code <= kMaxCode);
    }

    std::vector<std::pair<RE::INPUT_DEVICE, std::uint32_t>> PromptKeys()
    {
        std::vector<std::pair<RE::INPUT_DEVICE, std::uint32_t>> keys;
        auto add = [&](RE::INPUT_DEVICE a_device, std::uint32_t a_key) {
            if (a_key != RE::ControlMap::kInvalid) {
                keys.emplace_back(a_device, a_key);
            }
        };
        int code = CombatConfig::Get().counterKey;
        if (Valid(code) && code != kGameBlockKey) {
            if (code >= kMouseOffset) {
                add(RE::INPUT_DEVICE::kMouse, static_cast<std::uint32_t>(code - kMouseOffset));
            } else {
                add(RE::INPUT_DEVICE::kKeyboard, static_cast<std::uint32_t>(code));
            }
        } else {
            add(RE::INPUT_DEVICE::kKeyboard, BlockBinding(RE::INPUT_DEVICE::kKeyboard));
            add(RE::INPUT_DEVICE::kMouse, BlockBinding(RE::INPUT_DEVICE::kMouse));
        }
        add(RE::INPUT_DEVICE::kGamepad, BlockBinding(RE::INPUT_DEVICE::kGamepad));
        return keys;
    }

    bool Matches(const RE::ButtonEvent* a_button)
    {
        if (!a_button) {
            return false;
        }
        auto device = a_button->GetDevice();
        int code = CombatConfig::Get().counterKey;
        bool custom = Valid(code) && code != kGameBlockKey;
        if (!custom || device == RE::INPUT_DEVICE::kGamepad) {
            auto* userEvents = RE::UserEvents::GetSingleton();
            return userEvents && a_button->QUserEvent() == userEvents->leftAttack;
        }
        return CodeOf(device, a_button->GetIDCode()) == code;
    }

    std::string Name(int a_code)
    {
        if (a_code == kGameBlockKey) {
            // The block binding's own name: keyboard first, else the mouse button
            int bound = CodeOf(RE::INPUT_DEVICE::kKeyboard, BlockBinding(RE::INPUT_DEVICE::kKeyboard));
            if (bound == kGameBlockKey) {
                bound = CodeOf(RE::INPUT_DEVICE::kMouse, BlockBinding(RE::INPUT_DEVICE::kMouse));
            }
            return bound == kGameBlockKey ? std::string("Block key") : std::format("Block key ({})", Name(bound));
        }
        if (!Valid(a_code)) {
            return std::format("invalid ({})", a_code);
        }
        if (a_code >= kMouseOffset) {
            return kMouseNames[static_cast<std::size_t>(a_code - kMouseOffset)];
        }
        for (const auto& entry : kKeyboardNames) {
            if (entry.key == static_cast<std::uint32_t>(a_code)) {
                return entry.name;
            }
        }
        return std::format("key 0x{:02X}", a_code);
    }
}
