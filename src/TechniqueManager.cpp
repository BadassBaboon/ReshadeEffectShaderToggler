#include "TechniqueManager.h"

using namespace reshade::api;
using namespace ShaderToggler;
using namespace std;

size_t TechniqueManager::charBufferSize = CHAR_BUFFER_SIZE;
char TechniqueManager::charBuffer[CHAR_BUFFER_SIZE];

TechniqueManager::TechniqueManager(KeyMonitor& kMonitor)
  : keyMonitor(kMonitor) {}

void TechniqueManager::AddEffectsReloadingCallback(std::function<void(reshade::api::effect_runtime*)> callback) {
    effectsReloadingCallback.push_back(callback);
}

void TechniqueManager::AddEffectsReloadedCallback(std::function<void(reshade::api::effect_runtime*)> callback) {
    effectsReloadedCallback.push_back(callback);
}

void TechniqueManager::SignalEffectsReloading(reshade::api::effect_runtime* runtime) {
    for (auto& func : effectsReloadingCallback) {
        func(runtime);
    }
}

void TechniqueManager::SignalEffectsReloaded(reshade::api::effect_runtime* runtime) {
    for (auto& func : effectsReloadedCallback) {
        func(runtime);
    }
}

void TechniqueManager::OnReshadeReloadedEffects(reshade::api::effect_runtime* runtime) {
    RuntimeDataContainer& data = (*runtime->get_private_data<RuntimeDataContainer>());
    unique_lock<shared_mutex> lock(data.technique_mutex);

    data.allEnabledTechniques.clear();
    data.allTechniques.clear();
    data.allSortedTechniques.clear();
    data.techniqueUiCache.clear();
    for (auto& builtin : data.specialEffects)
        builtin.technique = {};

    Rendering::RenderingManager::EnumerateTechniques(
      runtime, [&data, this](effect_runtime* runtime, effect_technique technique, string& name, string& eff_name) {
          bool enabled = runtime->get_technique_state(technique);

          // Assign technique handles to REST effects
          bool builtin = false;
          for (uint32_t j = 0; j < REST_EFFECTS_COUNT; j++) {
              if (name == data.specialEffects[j].name) {
                  data.specialEffects[j].technique = technique;
                  builtin = true;
                  break;
              }
          }

          if (builtin) {
              return;
          }

          const auto& it = data.allTechniques.emplace(name + " [" + eff_name + "]", EffectData{ technique, runtime, enabled });
          it.first->second.name = &it.first->first;
          data.allSortedTechniques.push_back(&it.first->second);

          if (enabled) {
              data.allEnabledTechniques.emplace(&it.first->second);
          }
      });

    data.techniqueUiCache.reserve(data.allTechniques.size());
    for (auto& [name, effect] : data.allTechniques) {
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        data.techniqueUiCache.push_back({ name, std::move(upper), &effect });
    }
    std::sort(data.techniqueUiCache.begin(), data.techniqueUiCache.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });

    if (data.allTechniques.empty() && std::none_of(std::begin(data.specialEffects), std::end(data.specialEffects),
        [](const auto& builtin) { return builtin.technique != 0; }))
        SignalEffectsReloading(runtime);
    else
        SignalEffectsReloaded(runtime);
}

bool TechniqueManager::OnReshadeSetTechniqueState(reshade::api::effect_runtime* runtime, reshade::api::effect_technique technique, bool enabled) {
    RuntimeDataContainer& data = (*runtime->get_private_data<RuntimeDataContainer>());
    unique_lock<shared_mutex> lock(data.technique_mutex);

    charBufferSize = CHAR_BUFFER_SIZE;
    runtime->get_technique_name(technique, charBuffer, &charBufferSize);
    string techName(charBuffer);

    charBufferSize = CHAR_BUFFER_SIZE;
    runtime->get_technique_effect_name(technique, charBuffer, &charBufferSize);
    string effName(charBuffer);

    std::string effKey = techName + " [" + effName + "]";

    // Prevent REST techniques from being manually enabled
    for (uint32_t j = 0; j < REST_EFFECTS_COUNT; j++) {
        if (techName == data.specialEffects[j].name) {
            return true;
        }
    }

    const auto& it = data.allTechniques.find(effKey);

    if (it == data.allTechniques.end()) {
        return false;
    }

    if (enabled && !it->second.enabled)
        it->second.timeout_start = std::chrono::steady_clock::now();
    it->second.enabled = enabled;

    if (!enabled) {
        if (data.allEnabledTechniques.contains(&it->second)) {
            data.allEnabledTechniques.erase(&it->second);
        }
    } else {
        if (data.allEnabledTechniques.find(&it->second) == data.allEnabledTechniques.end()) {
            data.allEnabledTechniques.emplace(&it->second);
        }
    }

    return false;
}

bool TechniqueManager::OnReshadeReorderTechniques(reshade::api::effect_runtime* runtime, size_t count, reshade::api::effect_technique* techniques) {
    RuntimeDataContainer& data = (*runtime->get_private_data<RuntimeDataContainer>());
    unique_lock<shared_mutex> lock(data.technique_mutex);

    std::unordered_map<uint64_t, EffectData*> byHandle;
    for (auto& [name, effect] : data.allTechniques)
        byHandle.emplace(effect.technique.handle, &effect);
    data.allSortedTechniques.clear();
    for (size_t i = 0; i < count; ++i) {
        const auto it = byHandle.find(techniques[i].handle);
        if (it != byHandle.end())
            data.allSortedTechniques.push_back(it->second);
    }

    return false;
}

void TechniqueManager::OnReshadePresent(reshade::api::effect_runtime* runtime) {
    RuntimeDataContainer& deviceData = (*runtime->get_private_data<RuntimeDataContainer>());
    unique_lock<shared_mutex> lock(deviceData.technique_mutex);

    // Only enabled techniques get their per-frame rendered marker cleared (below). The REST
    // Enhanced port cleared it for every known technique, which re-armed disabled ones as
    // well and dropped RDR1 (D3D12) from 144 to ~16 fps whenever a group rendered anything.

    std::vector<effect_technique> expired;
    for (auto* eff : deviceData.allEnabledTechniques) {
        if (eff->timeout >= 0 && eff->technique != 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - eff->timeout_start).count() >= eff->timeout) {
            expired.push_back(eff->technique);
            continue;
        }

        // Prevent effects that are not supposed to be in screenshots from being rendered when ReShade is taking a screenshot
        if (!eff->enabled_in_screenshot && keyMonitor.GetKeyState(KeyMonitor::KEY_SCREEN_SHOT) == KeyState::KET_STATE_PRESSED) {
            eff->rendered = true;
        } else {
            eff->rendered = false;
        }
    }
    // ReShade calls our technique-state callback synchronously.
    lock.unlock();
    for (auto technique : expired)
        runtime->set_technique_state(technique, false);
}