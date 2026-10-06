#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ShaderToggler {

enum GamepadButtons : uint32_t {
    GAMEPAD_NONE           = 0,
    GAMEPAD_DPAD_UP        = 0x0001,
    GAMEPAD_DPAD_DOWN      = 0x0002,
    GAMEPAD_DPAD_LEFT      = 0x0004,
    GAMEPAD_DPAD_RIGHT     = 0x0008,
    GAMEPAD_START          = 0x0010,
    GAMEPAD_BACK           = 0x0020, // View / Select
    GAMEPAD_LEFT_THUMB     = 0x0040, // L3
    GAMEPAD_RIGHT_THUMB    = 0x0080, // R3
    GAMEPAD_LEFT_SHOULDER  = 0x0100, // LB
    GAMEPAD_RIGHT_SHOULDER = 0x0200, // RB
    GAMEPAD_LEFT_TRIGGER   = 0x0400, // LT
    GAMEPAD_RIGHT_TRIGGER  = 0x0800, // RT
    GAMEPAD_A              = 0x1000,
    GAMEPAD_B              = 0x2000,
    GAMEPAD_X              = 0x4000,
    GAMEPAD_Y              = 0x8000,
};

class GamepadMonitor {
  public:
    static GamepadMonitor& getInstance() {
        static GamepadMonitor instance;
        return instance;
    }

    void update();

    bool isConnected() const { return _connected; }
    int getActiveUserIndex() const { return _activeUserIndex; }
    uint32_t getCurrentButtons() const { return _currentButtons; }
    uint32_t getPressedButtons() const { return _pressedButtons; }

    bool isComboTriggered(uint32_t combo) const;

    static std::string buttonsToString(uint32_t buttons);

  private:
    GamepadMonitor();
    ~GamepadMonitor();

    GamepadMonitor(const GamepadMonitor&) = delete;
    GamepadMonitor& operator=(const GamepadMonitor&) = delete;

    void initXInput();

    struct XINPUT_GAMEPAD_LOCAL {
        WORD  wButtons;
        BYTE  bLeftTrigger;
        BYTE  bRightTrigger;
        SHORT sThumbLX;
        SHORT sThumbLY;
        SHORT sThumbRX;
        SHORT sThumbRY;
    };

    struct XINPUT_STATE_LOCAL {
        DWORD                dwPacketNumber;
        XINPUT_GAMEPAD_LOCAL Gamepad;
    };

    typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE_LOCAL* pState);

    HMODULE _xinputModule = nullptr;
    PFN_XInputGetState _xinputGetState = nullptr;

    bool _initialized = false;
    bool _connected = false;
    int _activeUserIndex = -1;

    uint32_t _currentButtons = 0;
    uint32_t _previousButtons = 0;
    uint32_t _pressedButtons = 0;
};

}
