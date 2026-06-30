#pragma once

#include "RenderingManager.h"
#include <unordered_map>
#include <shared_mutex>

namespace Rendering {
class __declspec(novtable) RenderingShaderManager final {
  public:
    RenderingShaderManager(AddonImGui::AddonUIData& data, ResourceManager& rManager);
    ~RenderingShaderManager();

    void InitShaders(reshade::api::device* device);
    void DestroyShaders(reshade::api::device* device);

    void CopyResource(reshade::api::command_list* cmd_list,
                      reshade::api::resource_view srv_src,
                      reshade::api::resource_view rtv_dst,
                      uint32_t width,
                      uint32_t height);
    void CopyResourceMaskAlpha(reshade::api::command_list* cmd_list,
                               reshade::api::resource_view srv_src,
                               reshade::api::resource_view rtv_dst,
                               uint32_t width,
                               uint32_t height);

  private:
    struct vert_uv {
        float x, y;
    };

    struct vert_input {
        vert_uv uv;
    };

    void InitShader(reshade::api::device* device,
                    uint16_t ps_resource_id,
                    uint16_t vs_resource_id,
                    reshade::api::pipeline& sh_pipeline,
                    reshade::api::pipeline_layout& sh_layout,
                    reshade::api::sampler& sh_sampler,
                    reshade::api::resource& quad,
                    uint8_t write_mask = 0xF);
    void ApplyShader(reshade::api::command_list* cmd_list,
                     reshade::api::resource_view srv_src,
                     reshade::api::resource_view rtv_dst,
                     reshade::api::pipeline& sh_pipeline,
                     reshade::api::pipeline_layout& sh_layout,
                     reshade::api::sampler& sh_sampler,
                     reshade::api::resource& quad,
                     uint32_t width,
                     uint32_t height);
                     
    reshade::api::pipeline GetOrCompilePipeline(reshade::api::device* device,
                                                uint16_t ps_resource_id,
                                                uint16_t vs_resource_id,
                                                reshade::api::pipeline_layout sh_layout,
                                                uint8_t write_mask,
                                                reshade::api::format target_format,
                                                std::unordered_map<reshade::api::format, reshade::api::pipeline>& cache,
                                                std::shared_mutex& mutex);

    AddonImGui::AddonUIData& uiData;
    ResourceManager& resourceManager;
};
}
