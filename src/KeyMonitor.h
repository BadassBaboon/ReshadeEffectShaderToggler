#pragma once

#include "KeyData.h"
#include "reshade_api.hpp"
#include <charconv>
#include <unordered_map>

namespace ShaderToggler {
enum class KeyState : uint32_t { KEY_STATE_NONE = 0, KET_STATE_PRESSED = 1 };

class __declspec(novtable) KeyMonitor final {
  public:
    static const uint32_t KEY_SCREEN_SHOT = 0;

    void SetKeyMonitor(uint32_t id, uint32_t keys) {
        keyMap[id] = keys;
        stateMap[id] = KeyState::KEY_STATE_NONE;
    }

    void PollKeyStates(reshade::api::effect_runtime* runtime) {
        for (const auto& keys : keyMap) {
            auto& currentState = stateMap[keys.first];

            if (areKeysPressed(keys.second, runtime)) {
                currentState = KeyState::KET_STATE_PRESSED;
            } else {
                currentState = KeyState::KEY_STATE_NONE;
            }
        }
    }

    KeyState GetKeyState(uint32_t id) {
        const auto& state = stateMap.find(id);
        if (state != stateMap.end()) {
            return state->second;
        }

        return KeyState::KEY_STATE_NONE;
    }

    void Init(reshade::api::effect_runtime* runtime) {
        if (initialized)
            return;

        for (const auto& [id, config] : reshadeConfigMapping) {
            const auto& [section, name] = config;
            char values[64] = {};
            size_t size = sizeof(values);
            if (!reshade::get_config_value(runtime, section.c_str(), name.c_str(), values, &size))
                continue;

            // ReShade returns INI list elements as consecutive NUL-terminated strings.
            uint32_t combo = 0;
            const char* value = values;
            for (uint32_t i = 0; i < 4 && value < values + size && *value; ++i) {
                const char* end = value + std::strlen(value);
                uint32_t key = 0;
                const auto parsed = std::from_chars(value, end, key);
                if (parsed.ec != std::errc{} || parsed.ptr != end || key > 255)
                    break;
                combo |= key << (8 * i);
                value = end + 1;
            }
            SetKeyMonitor(id, combo);
        }

        initialized = true;
    }

  private:
    bool initialized = false;
    std::unordered_map<uint32_t, uint32_t> keyMap;
    std::unordered_map<uint32_t, KeyState> stateMap;

    const std::unordered_map<uint32_t, std::tuple<std::string, std::string>> reshadeConfigMapping = {
        { KEY_SCREEN_SHOT, { "INPUT", "KeyScreenshot" } },
    };
};
}