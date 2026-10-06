#pragma once

#include "RenderingManager.h"
#include "RenderingShaderManager.h"
#include "ToggleGroupResourceManager.h"

namespace Rendering {
class __declspec(novtable) RenderingEffectManager final {
  public:
    RenderingEffectManager(AddonImGui::AddonUIData& data, ResourceManager& rManager, RenderingShaderManager& shManager, ToggleGroupResourceManager& tgrManager);
    ~RenderingEffectManager();

    void RenderEffects(reshade::api::command_list* cmd_list, uint64_t callLocation = CALL_DRAW, uint64_t invocation = MATCH_NONE);
    void RenderDeferredVulkanAutoEffects(reshade::api::command_list* cmd_list,
                                         uint32_t renderTargetCount,
                                         const reshade::api::render_pass_render_target_desc* renderTargets);
    void RenderDeferredVulkanAutoEffectsAfterBarrier(reshade::api::command_list* cmd_list,
                                                     uint32_t barrierCount,
                                                     const reshade::api::resource* resources,
                                                     const reshade::api::resource_usage* oldStates,
                                                     const reshade::api::resource_usage* newStates);
    bool RenderRemainingEffects(reshade::api::effect_runtime* runtime);
    void PreventRuntimeReload(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list);

  private:
    AddonImGui::AddonUIData& uiData;
    ResourceManager& resourceManager;
    RenderingShaderManager& shaderManager;
    ToggleGroupResourceManager& groupResourceManager;

    bool _RenderEffects(reshade::api::command_list* cmd_list,
                        DeviceDataContainer& deviceData,
                        RuntimeDataContainer& runtimeData,
                        const effect_queue& techniquesToRender,
                        std::vector<EffectData*>& removalList,
                        const std::unordered_set<EffectData*>& toRenderNames,
                        bool vulkanSafeBoundary = false,
                        const std::unordered_map<uint64_t, reshade::api::resource_usage>* allowedVulkanTargets = nullptr);

    void _RenderDeferredVulkanAutoEffects(
      reshade::api::command_list* cmd_list,
      const std::unordered_map<uint64_t, reshade::api::resource_usage>& safeTargets,
      const char* boundaryName);
};
}