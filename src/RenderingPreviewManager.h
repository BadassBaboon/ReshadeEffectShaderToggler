#pragma once

#include "RenderingManager.h"
#include "RenderingShaderManager.h"

namespace Rendering {
class __declspec(novtable) RenderingPreviewManager final {
  public:
    RenderingPreviewManager(AddonImGui::AddonUIData& data, ResourceManager& rManager, RenderingShaderManager& shManager);
    ~RenderingPreviewManager();

    void UpdatePreview(reshade::api::command_list* cmd_list, uint64_t callLocation, uint64_t invocation);
    void RecordVulkanHuntedTarget(reshade::api::command_list* cmd_list, uint32_t stageIndex, uint32_t shaderHash);
    void CaptureDeferredVulkanPreview(reshade::api::command_list* cmd_list);

  private:
    AddonImGui::AddonUIData& uiData;
    ResourceManager& resourceManager;
    RenderingShaderManager& shaderManager;
};
}