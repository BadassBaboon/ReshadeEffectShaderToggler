#include "RenderingPreviewManager.h"
#include "RenderingManager.h"
#include "StateTracking.h"
#include "Util.h"
#include "resource.h"
#include <d3d12.h>
#include <string>

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

RenderingPreviewManager::RenderingPreviewManager(AddonImGui::AddonUIData& data, ResourceManager& rManager, RenderingShaderManager& shManager)
  : uiData(data)
  , resourceManager(rManager)
  , shaderManager(shManager) {}

RenderingPreviewManager::~RenderingPreviewManager() {}

void RenderingPreviewManager::UpdatePreview(command_list* cmd_list, uint64_t callLocation, uint64_t invocation) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr) {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    if (deviceData.current_runtime == nullptr || uiData.GetToggleGroupIdShaderEditing() < 0) {
        return;
    }

    RuntimeDataContainer& runtimeData = deviceData.current_runtime->get_private_data<RuntimeDataContainer>();

    ToggleGroup& group = uiData.GetToggleGroups().at(uiData.GetToggleGroupIdShaderEditing());

    // Set views during draw call since we can be sure the correct ones are bound at that point
    if (!callLocation && deviceData.huntPreview.target == 0) {
        ResourceViewData active_target;

        if (invocation & MATCH_PREVIEW_PS) {
            active_target = RenderingManager::GetCurrentResourceView(cmd_list, deviceData, &group, commandListData, 0, invocation & MATCH_PREVIEW_PS);
        } else if (invocation & MATCH_PREVIEW_VS) {
            active_target = RenderingManager::GetCurrentResourceView(cmd_list, deviceData, &group, commandListData, 1, invocation & MATCH_PREVIEW_VS);
        } else if (invocation & MATCH_PREVIEW_CS) {
            active_target = RenderingManager::GetCurrentResourceView(cmd_list, deviceData, &group, commandListData, 2, invocation & MATCH_PREVIEW_CS);
        }

        if (active_target.resource != 0) {
            resource_desc desc = device->get_resource_desc(active_target.resource);

            // Bail BEFORE engaging the preview system for targets we can't safely copy (HDR/float
            // back/scene buffers, MSAA, non-2D). Skipping only the copy wasn't enough - capturing
            // the target and recreating preview buffers for it also hangs the GPU. Don't even set
            // huntPreview.target, so nothing downstream touches the resource. The shader can still
            // be cycled to and marked; only the thumbnail is unavailable.
            // The HDR/FP16 live-buffer TDR hang is a DX12/Vulkan problem; DX9/10/11 copy those fine.
            // So only restrict the FORMAT on explicit-barrier APIs; the samples/non-2D restriction is
            // universal (an MSAA / non-2D copy is invalid everywhere). This keeps HDR previews working
            // on GTA5 Legacy (DX11) while preventing the hang on RDR1 / GTA5 Enhanced (DX12).
            const bool strict_copy = (device->get_api() == device_api::d3d12 || device->get_api() == device_api::vulkan);
            const reshade::api::format cap_typeless = format_to_typeless(desc.texture.format);
            const bool cap_copyable = !strict_copy ||
                                      cap_typeless == reshade::api::format::r8g8b8a8_typeless ||
                                      cap_typeless == reshade::api::format::b8g8r8a8_typeless;
            if (desc.texture.samples > 1 || desc.type != resource_type::texture_2d || !cap_copyable) {
                // This capture attempt runs every frame while hunting; throttle the log to once per
                // distinct format so an HDR shader you're parked on doesn't flood ReShade.log.
                static reshade::api::format s_last_skip_format = reshade::api::format::unknown;
                if (desc.texture.format != s_last_skip_format) {
                    s_last_skip_format = desc.texture.format;
                    reshade::log::message(reshade::log::level::warning,
                        ("[REST] Preview not engaged (unsafe target): samples=" + std::to_string(desc.texture.samples) +
                         " type=" + std::to_string(static_cast<uint32_t>(desc.type)) +
                         " format=" + std::to_string(static_cast<uint32_t>(desc.texture.format)) +
                         " - shader can still be marked.").c_str());
                }
                return;
            }
            // Seed barrier tracking with the resource's state at this draw call (it's bound as a
            // render target here). on_barrier() then follows any transitions the game makes before
            // we copy it, so we know its REAL state at copy time (e.g. HDR buffers get moved to
            // shader_resource for post-processing - assuming render_target there hangs the device).
            cmd_list->get_private_data<state_tracking>().start_resource_barrier_tracking(active_target.resource, resource_usage::render_target);

            deviceData.huntPreview.target = active_target.resource;
            deviceData.huntPreview.target_desc = desc;
            deviceData.huntPreview.format = desc.texture.format;
            deviceData.huntPreview.view_format = active_target.format;
            deviceData.huntPreview.width = desc.texture.width;
            deviceData.huntPreview.height = desc.texture.height;

            // Log each distinct captured target once (this runs every frame while hunting).
            static uint64_t s_last_captured_handle = 0;
            if (active_target.resource.handle != s_last_captured_handle) {
                s_last_captured_handle = active_target.resource.handle;
                reshade::log::message(reshade::log::level::debug,
                    ("[REST] Preview target captured: " + std::to_string(desc.texture.width) + "x" + std::to_string(desc.texture.height) +
                     " samples=" + std::to_string(desc.texture.samples) +
                     " type=" + std::to_string(static_cast<uint32_t>(desc.type)) +
                     " format=" + std::to_string(static_cast<uint32_t>(desc.texture.format))).c_str());
            }
        } else {
            return;
        }
    }

    if (deviceData.huntPreview.target == 0 ||
        !(!callLocation && !deviceData.huntPreview.target_invocation_location || callLocation & deviceData.huntPreview.target_invocation_location)) {
        return;
    }

    if (group.getId() == uiData.GetToggleGroupIdShaderEditing() && !deviceData.huntPreview.matched) {
        resource rs = deviceData.huntPreview.target;
        // Real current state of the source on this command list. If unknown (capture happened on a
        // different command list, or no barrier was tracked), we can't safely transition+copy it,
        // so skip the preview rather than guess a state and hang the device.
        const resource_usage rs_usage = cmd_list->get_private_data<state_tracking>().stop_resource_barrier_tracking(rs);
        if (rs_usage == resource_usage::undefined) {
            reshade::log::message(reshade::log::level::warning,
                "[REST] Preview skipped: source state unknown on this command list (can't safely copy).");
            deviceData.huntPreview.matched = true;
            return;
        }

        if (!resourceManager.IsCompatibleWithPreviewFormat(device, rs, deviceData.huntPreview.view_format)) {
            deviceData.huntPreview.recreate_preview = true;
        } else {
            bool supportsAlphaClear = device->get_api() == device_api::d3d9 || device->get_api() == device_api::d3d10 ||
                                      device->get_api() == device_api::d3d11 || device->get_api() == device_api::d3d12;

            // Whether this API needs explicit resource barriers around the copy (DX12 / Vulkan).
            // DX9/10/11 do implicit transitions, so we copy without barriers there (matches upstream).
            const bool strict_copy = (device->get_api() == device_api::d3d12 || device->get_api() == device_api::vulkan);

            resource previewResPing = resource{ 0 };
            resource previewResPong = resource{ 0 };
            resource_view preview_pong_rtv = resource_view{ 0 };
            resource_view preview_ping_srv = resource_view{ 0 };

            resourceManager.SetPingPreviewHandles(device, &previewResPing, nullptr, &preview_ping_srv);
            resourceManager.SetPongPreviewHandles(device, &previewResPong, &preview_pong_rtv, nullptr);

            // The source is transitioned from its REAL current state (rs_usage, from barrier
            // tracking) to copy_source and back; the preview resources are in shader_resource state.
            const resource_usage RT = resource_usage::render_target;
            const resource_usage SR = resource_usage::shader_resource;
            const resource_usage CS = resource_usage::copy_source;
            const resource_usage CD = resource_usage::copy_dest;

            // Both paths: copy source -> ping (matched source format), then render ping -> pong
            // (always R8G8B8A8) through the fixed-format copy shader. This keeps the bound RTV's
            // format equal to the pipeline's declared format for ANY source format (incl. HDR),
            // which is what stops the format=10 device removal. ImGui displays pong.
            if (previewResPing != 0 && previewResPong != 0 && preview_ping_srv != 0 && preview_pong_rtv != 0) {
                // source -> ping. On DX12/Vulkan, barrier from the source's real state (rs_usage) to
                // copy_source and back; on DX9/10/11 the runtime does these implicitly, so we omit them.
                const resource res2[2] = { rs, previewResPing };
                const resource_usage before[2] = { rs_usage, SR };
                const resource_usage during[2] = { CS, CD };
                
                std::vector<reshade::api::resource_view> bound_rtvs;
                reshade::api::resource_view bound_dsv = {0};

                if (strict_copy) {
                    bound_rtvs = cmd_list->get_private_data<state_tracking>().render_targets;
                    bound_dsv = cmd_list->get_private_data<state_tracking>().depth_stencil;
                    // Unbind render targets so we can safely transition them without DX12 validation errors
                    cmd_list->bind_render_targets_and_depth_stencil(0, nullptr, { 0 });
                    cmd_list->barrier(2, res2, before, during);
                }
                
                cmd_list->copy_resource(rs, previewResPing);
                
                if (strict_copy) {
                    cmd_list->barrier(2, res2, during, before); // src back to rs_usage, ping back to SR
                    // Rebind
                    cmd_list->bind_render_targets_and_depth_stencil(static_cast<uint32_t>(bound_rtvs.size()), bound_rtvs.data(), bound_dsv);
                }

                // pong: shader_resource -> render_target for the copy-shader output, then back.
                resource_usage pong_from = SR, pong_to = RT;
                if (strict_copy) cmd_list->barrier(1, &previewResPong, &pong_from, &pong_to);
                if (group.getClearPreviewAlpha() && supportsAlphaClear) {
                    shaderManager.CopyResourceMaskAlpha(cmd_list, preview_ping_srv, preview_pong_rtv, deviceData.huntPreview.width, deviceData.huntPreview.height);
                } else {
                    shaderManager.CopyResource(cmd_list, preview_ping_srv, preview_pong_rtv, deviceData.huntPreview.width, deviceData.huntPreview.height);
                }
                if (strict_copy) cmd_list->barrier(1, &previewResPong, &pong_to, &pong_from);
            }

            if (group.getFlipBuffer() && runtimeData.specialEffects[REST_FLIP].technique != 0) {
                deviceData.current_runtime->render_technique(runtimeData.specialEffects[REST_FLIP].technique, cmd_list, preview_pong_rtv, preview_pong_rtv);
            }

            if (group.getToneMap() && runtimeData.specialEffects[REST_TONEMAP_TO_SDR].technique != 0) {
                deviceData.current_runtime->render_technique(
                  runtimeData.specialEffects[REST_TONEMAP_TO_SDR].technique, cmd_list, preview_pong_rtv, preview_pong_rtv);
            }
        }

        deviceData.huntPreview.matched = true;
    }
}