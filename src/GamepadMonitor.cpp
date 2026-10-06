#include "GamepadMonitor.h"

namespace ShaderToggler {

GamepadMonitor::GamepadMonitor() {
    initXInput();
}

GamepadMonitor::~GamepadMonitor() {
    if (_xinputModule != nullptr) {
        FreeLibrary(_xinputModule);
        _xinputModule = nullptr;
    }
}

void GamepadMonitor::initXInput() {
    if (_initialized) {
        return;
    }
    _initialized = true;

    // Try standard XInput DLLs in descending order of modern feature support
    static const wchar_t* dllNames[] = {
        L"xinput1_4.dll",
        L"xinput1_3.dll",
        L"xinput9_1_0.dll"
    };

    for (const wchar_t* dllName : dllNames) {
        _xinputModule = LoadLibraryW(dllName);
        if (_xinputModule != nullptr) {
            _xinputGetState = reinterpret_cast<PFN_XInputGetState>(GetProcAddress(_xinputModule, "XInputGetState"));
            if (_xinputGetState != nullptr) {
                break;
            }
            FreeLibrary(_xinputModule);
            _xinputModule = nullptr;
        }
    }
}

void GamepadMonitor::update() {
    _previousButtons = _currentButtons;
    _currentButtons = 0;
    _pressedButtons = 0;
    _connected = false;
    _activeUserIndex = -1;

    if (_xinputGetState == nullptr) {
        return;
    }

    // Poll connected controllers (user indices 0..3)
    XINPUT_STATE_LOCAL state = {};
    for (DWORD i = 0; i < 4; i++) {
        DWORD result = _xinputGetState(i, &state);
        if (result == ERROR_SUCCESS) {
            _connected = true;
            _activeUserIndex = static_cast<int>(i);

            // Convert raw wButtons to our GamepadButtons mask
            uint32_t buttons = 0;
            if (state.Gamepad.wButtons & 0x0001) buttons |= GAMEPAD_DPAD_UP;
            if (state.Gamepad.wButtons & 0x0002) buttons |= GAMEPAD_DPAD_DOWN;
            if (state.Gamepad.wButtons & 0x0004) buttons |= GAMEPAD_DPAD_LEFT;
            if (state.Gamepad.wButtons & 0x0008) buttons |= GAMEPAD_DPAD_RIGHT;
            if (state.Gamepad.wButtons & 0x0010) buttons |= GAMEPAD_START;
            if (state.Gamepad.wButtons & 0x0020) buttons |= GAMEPAD_BACK;
            if (state.Gamepad.wButtons & 0x0040) buttons |= GAMEPAD_LEFT_THUMB;
            if (state.Gamepad.wButtons & 0x0080) buttons |= GAMEPAD_RIGHT_THUMB;
            if (state.Gamepad.wButtons & 0x0100) buttons |= GAMEPAD_LEFT_SHOULDER;
            if (state.Gamepad.wButtons & 0x0200) buttons |= GAMEPAD_RIGHT_SHOULDER;
            if (state.Gamepad.wButtons & 0x1000) buttons |= GAMEPAD_A;
            if (state.Gamepad.wButtons & 0x2000) buttons |= GAMEPAD_B;
            if (state.Gamepad.wButtons & 0x4000) buttons |= GAMEPAD_X;
            if (state.Gamepad.wButtons & 0x8000) buttons |= GAMEPAD_Y;

            // Trigger thresholds (threshold > 50 avoids accidental trigger drag)
            if (state.Gamepad.bLeftTrigger > 50)  buttons |= GAMEPAD_LEFT_TRIGGER;
            if (state.Gamepad.bRightTrigger > 50) buttons |= GAMEPAD_RIGHT_TRIGGER;

            _currentButtons = buttons;
            break; // Found active controller
        }
    }

    if (_connected) {
        _pressedButtons = (_currentButtons ^ _previousButtons) & _currentButtons;
    }
}

bool GamepadMonitor::isComboTriggered(uint32_t combo) const {
    if (combo == 0 || !_connected) {
        return false;
    }

    // All buttons in the combo must be currently held down
    if ((_currentButtons & combo) != combo) {
        return false;
    }

    // At least one button in the combo must have transitioned to pressed this frame
    if ((_pressedButtons & combo) == 0) {
        return false;
    }

    // Exact combo match: ignore if extraneous buttons are being held simultaneously
    if (_currentButtons != combo) {
        return false;
    }

    return true;
}

std::string GamepadMonitor::buttonsToString(uint32_t buttons) {
    if (buttons == 0) {
        return "None";
    }

    std::vector<std::string> parts;
    if (buttons & GAMEPAD_BACK)           parts.push_back("Back");
    if (buttons & GAMEPAD_START)          parts.push_back("Start");
    if (buttons & GAMEPAD_LEFT_SHOULDER)  parts.push_back("LB");
    if (buttons & GAMEPAD_RIGHT_SHOULDER) parts.push_back("RB");
    if (buttons & GAMEPAD_LEFT_TRIGGER)   parts.push_back("LT");
    if (buttons & GAMEPAD_RIGHT_TRIGGER)  parts.push_back("RT");
    if (buttons & GAMEPAD_LEFT_THUMB)     parts.push_back("LS Click");
    if (buttons & GAMEPAD_RIGHT_THUMB)    parts.push_back("RS Click");
    if (buttons & GAMEPAD_DPAD_UP)        parts.push_back("D-Pad Up");
    if (buttons & GAMEPAD_DPAD_DOWN)      parts.push_back("D-Pad Down");
    if (buttons & GAMEPAD_DPAD_LEFT)      parts.push_back("D-Pad Left");
    if (buttons & GAMEPAD_DPAD_RIGHT)     parts.push_back("D-Pad Right");
    if (buttons & GAMEPAD_A)              parts.push_back("A");
    if (buttons & GAMEPAD_B)              parts.push_back("B");
    if (buttons & GAMEPAD_X)              parts.push_back("X");
    if (buttons & GAMEPAD_Y)              parts.push_back("Y");

    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) {
            result += " + ";
        }
        result += parts[i];
    }
    return result.empty() ? "None" : result;
}

}
