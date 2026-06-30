#include "RenderingShaderManager.h"
#include "StateTracking.h"
#include "resource.h"
#include <d3d12.h>

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

RenderingShaderManager::RenderingShaderManager(AddonImGui::AddonUIData& data, ResourceManager& rManager)
  : uiData(data)
  , resourceManager(rManager) {}

RenderingShaderManager::~RenderingShaderManager() {}

void RenderingShaderManager::InitShader(reshade::api::device* device,
                                        uint16_t ps_resource_id,
                                        uint16_t vs_resource_id,
                                        reshade::api::pipeline& sh_pipeline,
                                        reshade::api::pipeline_layout& sh_layout,
                                        reshade::api::sampler& sh_sampler,
                                        resource& quad,
                                        uint8_t write_mask) {
    if (sh_pipeline == 0 && (device->get_api() == device_api::d3d9 || device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 ||
                             device->get_api() == device_api::d3d12)) {
        sampler_desc sampler_desc = {};
        sampler_desc.filter = filter_mode::min_mag_mip_point;
        sampler_desc.address_u = texture_address_mode::clamp;
        sampler_desc.address_v = texture_address_mode::clamp;
        sampler_desc.address_w = texture_address_mode::clamp;

        pipeline_layout_param layout_params[2];
        layout_params[0] = descriptor_range{ 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::sampler };
        layout_params[1] = descriptor_range{ 0, 0, 0, 1, shader_stage::all, 1, descriptor_type::shader_resource_view };

        const EmbeddedResourceData vs = resourceManager.GetResourceData(vs_resource_id);
        const EmbeddedResourceData ps = resourceManager.GetResourceData(ps_resource_id);

        shader_desc vs_desc = { vs.data, vs.size };
        shader_desc ps_desc = { ps.data, ps.size };

        std::vector<pipeline_subobject> subobjects;
        subobjects.push_back({ pipeline_subobject_type::vertex_shader, 1, &vs_desc });
        subobjects.push_back({ pipeline_subobject_type::pixel_shader, 1, &ps_desc });

        if (device->get_api() == device_api::d3d9) {
            static input_element input_layout[1] = {
                { 0, "TEXCOORD", 0, format::r32g32_float, 0, offsetof(vert_input, uv), sizeof(vert_input), 0 },
            };

            subobjects.push_back({ pipeline_subobject_type::input_layout, 1, reinterpret_cast<void*>(input_layout) });
        }

        reshade::api::primitive_topology topology = reshade::api::primitive_topology::triangle_list;
        subobjects.push_back({ reshade::api::pipeline_subobject_type::primitive_topology, 1, &topology });

        reshade::api::blend_desc blend_state;
        blend_state.render_target_write_mask[0] = write_mask;
        subobjects.push_back({ reshade::api::pipeline_subobject_type::blend_state, 1, &blend_state });

        reshade::api::format render_target_format = reshade::api::format_to_default_typed(reshade::api::format::r8g8b8a8_typeless, 0);
        subobjects.push_back({ reshade::api::pipeline_subobject_type::render_target_formats, 1, &render_target_format });

        if (!device->create_pipeline_layout(2, layout_params, &sh_layout) ||
            !device->create_pipeline(sh_layout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &sh_pipeline) ||
            !device->create_sampler(sampler_desc, &sh_sampler)) {

            sh_pipeline = { 0 };
            sh_layout = { 0 };
            sh_sampler = { 0 };
            reshade::log::message(reshade::log::level::warning, "Unable to create pipeline");
        }

        if (quad == 0 && device->get_api() == device_api::d3d9) {
            const uint32_t num_vertices = 4;

            if (!device->create_resource(resource_desc(num_vertices * sizeof(vert_input), memory_heap::cpu_to_gpu, resource_usage::vertex_buffer),
                                         nullptr,
                                         resource_usage::cpu_access,
                                         &quad)) {
                reshade::log::message(reshade::log::level::warning, "Unable to create preview copy pipeline vertex buffer");
            } else {
                const vert_input vertices[num_vertices] = { vert_input{ vert_uv{ 0.0f, 0.0f } },
                                                            vert_input{ vert_uv{ 0.0f, 1.0f } },
                                                            vert_input{ vert_uv{ 1.0f, 0.0f } },
                                                            vert_input{ vert_uv{ 1.0f, 1.0f } } };

                void* host_memory;

                if (device->map_buffer_region(quad, 0, UINT64_MAX, map_access::write_only, &host_memory)) {
                    memcpy(host_memory, vertices, num_vertices * sizeof(vert_input));
                    device->unmap_buffer_region(quad);
                }
            }
        }
    }
}

void RenderingShaderManager::InitShaders(reshade::api::device* device) {
    DeviceDataContainer& shader = device->get_private_data<DeviceDataContainer>();

    if (device->get_api() == device_api::d3d9) {
        InitShader(device,
                   SHADER_PREVIEW_COPY_PS_3_0,
                   SHADER_FULLSCREEN_VS_3_0,
                   shader.customShader.copyPipeline.pipeline,
                   shader.customShader.copyPipeline.pipelineLayout,
                   shader.customShader.copyPipeline.pipelineSampler,
                   shader.customShader.fullscreenQuadVertexBuffer,
                   0xF);
        InitShader(device,
                   SHADER_PREVIEW_COPY_PS_3_0,
                   SHADER_FULLSCREEN_VS_3_0,
                   shader.customShader.alphaPreservingCopyPipeline.pipeline,
                   shader.customShader.alphaPreservingCopyPipeline.pipelineLayout,
                   shader.customShader.alphaPreservingCopyPipeline.pipelineSampler,
                   shader.customShader.fullscreenQuadVertexBuffer,
                   0x7);
    } else {
        InitShader(device,
                   SHADER_PREVIEW_COPY_PS_4_0,
                   SHADER_FULLSCREEN_VS_4_0,
                   shader.customShader.copyPipeline.pipeline,
                   shader.customShader.copyPipeline.pipelineLayout,
                   shader.customShader.copyPipeline.pipelineSampler,
                   shader.customShader.fullscreenQuadVertexBuffer,
                   0xF);
        InitShader(device,
                   SHADER_PREVIEW_COPY_PS_4_0,
                   SHADER_FULLSCREEN_VS_4_0,
                   shader.customShader.alphaPreservingCopyPipeline.pipeline,
                   shader.customShader.alphaPreservingCopyPipeline.pipelineLayout,
                   shader.customShader.alphaPreservingCopyPipeline.pipelineSampler,
                   shader.customShader.fullscreenQuadVertexBuffer,
                   0x7);
    }
}

void RenderingShaderManager::DestroyShaders(reshade::api::device* device) {
    DeviceDataContainer& shader = device->get_private_data<DeviceDataContainer>();

    if (shader.customShader.fullscreenQuadVertexBuffer != 0) {
        device->destroy_resource(shader.customShader.fullscreenQuadVertexBuffer);
        shader.customShader.fullscreenQuadVertexBuffer = { 0 };
    }

    if (shader.customShader.copyPipeline.pipeline != 0) {
        device->destroy_pipeline(shader.customShader.copyPipeline.pipeline);
        shader.customShader.copyPipeline.pipeline = { 0 };
    }

    if (shader.customShader.alphaPreservingCopyPipeline.pipeline != 0) {
        device->destroy_pipeline(shader.customShader.alphaPreservingCopyPipeline.pipeline);
        shader.customShader.alphaPreservingCopyPipeline.pipeline = { 0 };
    }

    if (shader.customShader.copyPipeline.pipelineLayout != 0) {
        device->destroy_pipeline_layout(shader.customShader.copyPipeline.pipelineLayout);
        shader.customShader.copyPipeline.pipelineLayout = { 0 };
    }

    if (shader.customShader.alphaPreservingCopyPipeline.pipelineLayout != 0) {
        device->destroy_pipeline_layout(shader.customShader.alphaPreservingCopyPipeline.pipelineLayout);
        shader.customShader.alphaPreservingCopyPipeline.pipelineLayout = { 0 };
    }

    if (shader.customShader.copyPipeline.pipelineSampler != 0) {
        device->destroy_sampler(shader.customShader.copyPipeline.pipelineSampler);
        shader.customShader.copyPipeline.pipelineSampler = { 0 };
    }

    if (shader.customShader.alphaPreservingCopyPipeline.pipelineSampler != 0) {
        device->destroy_sampler(shader.customShader.alphaPreservingCopyPipeline.pipelineSampler);
        shader.customShader.alphaPreservingCopyPipeline.pipelineSampler = { 0 };
    }
}

void RenderingShaderManager::ApplyShader(command_list* cmd_list,
                                         resource_view srv_src,
                                         resource_view rtv_dst,
                                         pipeline& sh_pipeline,
                                         pipeline_layout& sh_layout,
                                         sampler& sh_sampler,
                                         resource& quad,
                                         uint32_t width,
                                         uint32_t height) {
    device* device = cmd_list->get_device();

    if (sh_pipeline == 0 || !(device->get_api() == device_api::d3d9 || device->get_api() == device_api::d3d10 || device->get_api() == device_api::d3d11 ||
                              device->get_api() == device_api::d3d12)) {
        return;
    }

    cmd_list->get_private_data<state_tracking>().capture(cmd_list, true);

    cmd_list->bind_render_targets_and_depth_stencil(1, &rtv_dst);

    cmd_list->bind_pipeline(pipeline_stage::all_graphics, sh_pipeline);

    cmd_list->push_descriptors(shader_stage::pixel, sh_layout, 0, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::sampler, &sh_sampler });
    cmd_list->push_descriptors(shader_stage::pixel, sh_layout, 1, descriptor_table_update{ {}, 0, 0, 1, descriptor_type::shader_resource_view, &srv_src });

    const viewport viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    cmd_list->bind_viewports(0, 1, &viewport);

    const rect scissor_rect = { 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
    cmd_list->bind_scissor_rects(0, 1, &scissor_rect);

    if (cmd_list->get_device()->get_api() == device_api::d3d9) {
        cmd_list->bind_pipeline_state(dynamic_state::primitive_topology, static_cast<uint32_t>(primitive_topology::triangle_strip));
        cmd_list->bind_vertex_buffer(0, quad, 0, sizeof(vert_input));
        cmd_list->draw(4, 1, 0, 0);
    } else {
        cmd_list->draw(3, 1, 0, 0);
    }
}

void RenderingShaderManager::CopyResource(command_list* cmd_list, resource_view srv_src, resource_view rtv_dst, uint32_t width, uint32_t height) {
    device* device = cmd_list->get_device();
    DeviceDataContainer& data = device->get_private_data<DeviceDataContainer>();

    ApplyShader(cmd_list,
                srv_src,
                rtv_dst,
                data.customShader.copyPipeline.pipeline,
                data.customShader.copyPipeline.pipelineLayout,
                data.customShader.copyPipeline.pipelineSampler,
                data.customShader.fullscreenQuadVertexBuffer,
                width,
                height);

    cmd_list->get_private_data<state_tracking>().apply(cmd_list, true);
}

void RenderingShaderManager::CopyResourceMaskAlpha(reshade::api::command_list* cmd_list,
                                                   reshade::api::resource_view srv_src,
                                                   reshade::api::resource_view rtv_dst,
                                                   uint32_t width,
                                                   uint32_t height) {
    device* device = cmd_list->get_device();
    DeviceDataContainer& data = device->get_private_data<DeviceDataContainer>();

    ApplyShader(cmd_list,
                srv_src,
                rtv_dst,
                data.customShader.alphaPreservingCopyPipeline.pipeline,
                data.customShader.alphaPreservingCopyPipeline.pipelineLayout,
                data.customShader.alphaPreservingCopyPipeline.pipelineSampler,
                data.customShader.fullscreenQuadVertexBuffer,
                width,
                height);

    cmd_list->get_private_data<state_tracking>().apply(cmd_list, true);
}
