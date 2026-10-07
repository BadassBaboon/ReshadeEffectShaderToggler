#include "RenderingEffectManager.h"
#include "DiagnosticLog.h"
#include "StateTracking.h"
#include "Util.h"

using namespace Rendering;
using namespace ShaderToggler;
using namespace reshade::api;
using namespace std;

RenderingEffectManager::RenderingEffectManager(AddonImGui::AddonUIData& data,
                                               ResourceManager& rManager,
                                               RenderingShaderManager& shManager,
                                               ToggleGroupResourceManager& tgrManager)
  : uiData(data)
  , resourceManager(rManager)
  , shaderManager(shManager)
  , groupResourceManager(tgrManager) {}

RenderingEffectManager::~RenderingEffectManager() {}

bool RenderingEffectManager::RenderRemainingEffects(effect_runtime* runtime) {
    if (runtime == nullptr || runtime->get_device() == nullptr) {
        return false;
    }

    command_list* cmd_list = runtime->get_command_queue()->get_immediate_command_list();
    device* device = runtime->get_device();
    RuntimeDataContainer& runtimeData = (*runtime->get_private_data<RuntimeDataContainer>());
    DeviceDataContainer& deviceData = (*device->get_private_data<DeviceDataContainer>());
    bool rendered = false;

    resource res = runtime->get_current_back_buffer();

    const std::shared_ptr<GlobalResourceView>& view = resourceManager.GetResourceView(device, res.handle);

    if (view == nullptr || view->rtv == 0 || !deviceData.rendered_effects) {
        return false;
    }

    resource_view active_rtv = view->rtv;
    resource_view active_rtv_srgb = view->rtv_srgb;

    for (auto& eff : runtimeData.allSortedTechniques) {
        if (eff->enabled && !eff->rendered) {
            runtime->render_technique(eff->technique, cmd_list, active_rtv, active_rtv_srgb);

            eff->rendered = true;
            rendered = true;
        }
    }

    return rendered;
}

static reshade::api::format GetBackBufferTypelessFormat(effect_runtime* runtime) {
    const resource backBuffer = runtime->get_current_back_buffer();
    if (backBuffer == 0)
        return reshade::api::format::unknown;
    return format_to_typeless(runtime->get_device()->get_resource_desc(backBuffer).texture.format);
}

bool RenderingEffectManager::_RenderEffects(command_list* cmd_list,
                                            DeviceDataContainer& deviceData,
                                            RuntimeDataContainer& runtimeData,
                                            const effect_queue& techniquesToRender,
                                            vector<EffectData*>& removalList,
                                            const unordered_set<EffectData*>& toRenderNames,
                                            bool vulkanSafeBoundary,
                                            const unordered_map<uint64_t, resource_usage>* allowedVulkanTargets) {
    bool rendered = false;
    CommandListDataContainer& cmdData = (*cmd_list->get_private_data<CommandListDataContainer>());
    effect_runtime* runtime = deviceData.current_runtime;

    unordered_map<ToggleGroup*, pair<vector<EffectData*>, ResourceRenderData>> groupTechMap;

    for (const auto& aTech : runtimeData.allSortedTechniques) {
        const auto& sTech = techniquesToRender.find(aTech);

        if (sTech == techniquesToRender.end()) {
            continue;
        }

        if (sTech->first->enabled && !sTech->first->rendered && toRenderNames.contains(sTech->first)) {
            const auto& [techName, techData] = *sTech;

            auto& [gEffects, gResource] = groupTechMap[techData.group];

            gEffects.push_back(sTech->first);
            gResource = sTech->second;
        }
    }

    for (const auto& tech : groupTechMap) {
        const auto& group = tech.first;
        const auto& [effectList, active_resource] = tech.second;

        // A group can be retired (removed in the UI) while its entry is still queued here. It is kept
        // alive for exactly this reason, but must not render anymore.
        if (group->isRetired()) {
            continue;
        }

        if (cmdData.hasDrawGeometry && !group->matchesDrawGeometry(cmdData.isIndexedDraw, cmdData.currentDrawCount, cmdData.currentInstanceCount)) {
            continue;
        }

        if (active_resource.resource == 0) {
            continue;
        }

        resource_view view_non_srgb = {};
        resource_view view_srgb = {};
        resource_view group_view = {};
        resource_desc desc = cmd_list->get_device()->get_resource_desc(active_resource.resource);
        GroupResource& groupResource = group->GetGroupResource(GroupResourceType::RESOURCE_ALPHA);
        const shared_ptr<GlobalResourceView>& view = resourceManager.GetResourceView(runtime->get_device(), active_resource);
        bool copyPreserveAlpha = false;

        if (view == nullptr) {
            continue;
        }

        uint32_t runtimeWidth = 0, runtimeHeight = 0;
        runtime->get_screenshot_width_and_height(&runtimeWidth, &runtimeHeight);

        const device_api deviceApi = cmd_list->get_device()->get_api();
        const bool autoSceneColour = group->isAutoSceneColourActive(deviceApi);
        const bool preserveTargetAlpha = group->getPreserveAlpha() && !autoSceneColour;

        // Only Auto Scene Colour stages off-size targets (its pre-upscale scene buffer). Manual groups
        // render straight into the matched target, as before the REST Enhanced port: ReShade 6.x keeps
        // a cached effect permutation per target size/format, and staging a manual target by hand
        // (assuming it is in render-target state) hung the GPU on RDR1 (D3D12) with an off-size target.
        const bool offSizeTarget =
          runtimeWidth > 0 && runtimeHeight > 0 &&
          (desc.texture.width != runtimeWidth || desc.texture.height != runtimeHeight);
        const reshade::api::format backBufferFormat = GetBackBufferTypelessFormat(runtime);
        const bool formatMismatch =
          backBufferFormat != reshade::api::format::unknown && format_to_typeless(desc.texture.format) != backBufferFormat;
        const bool wantsNativeStaging = autoSceneColour && offSizeTarget;
        const bool vulkanAutoSceneColour = autoSceneColour && deviceApi == device_api::vulkan;
        const bool vulkanNativeStaging = vulkanAutoSceneColour && wantsNativeStaging;
        resource_usage vulkanTargetUsage = resource_usage::render_target;

        if (vulkanAutoSceneColour) {
            // ReShade's Vulkan render_technique path always copies the supplied colour
            // target into its effect-colour texture before the first pass, so the live
            // image must already have transfer-source usage. Native staging additionally
            // needs transfer-destination usage for the processed result to be blitted back.
            const bool hasCopySource = static_cast<uint32_t>(desc.usage & resource_usage::copy_source) != 0;
            const bool hasCopyDest = static_cast<uint32_t>(desc.usage & resource_usage::copy_dest) != 0;
            if (!hasCopySource) {
                group->setDebugAutoStatus("Unsupported target: missing transfer source");
                continue;
            }
            if (wantsNativeStaging && !hasCopyDest) {
                group->setDebugAutoStatus("Unsupported target: missing transfer destination");
                continue;
            }

            if (!runtime->get_device()->check_format_support(desc.texture.format, resource_usage::copy_source)) {
                group->setDebugAutoStatus("Unsupported format: transfer source");
                continue;
            }
            if (wantsNativeStaging && !runtime->get_device()->check_format_support(desc.texture.format, resource_usage::copy_dest)) {
                group->setDebugAutoStatus("Unsupported format: transfer destination");
                continue;
            }
        }

        // Vulkan draw callbacks execute inside the game's active render pass.
        // ReShade effect rendering and transfer/blit commands would be invalid there,
        // so defer Auto Scene Colour until a later begin_render_pass callback, which
        // ReShade invokes before the Vulkan render pass actually begins.
        if (vulkanAutoSceneColour) {
            if (!vulkanSafeBoundary) {
                // Move Vulkan Auto work out of the current command-list queue. A later
                // continuation pass may be recorded on a different command buffer, so
                // pending work is tracked at device scope. Preserve the first applicable
                // group/target for a technique, matching REST's first-render-wins model.
                group->setDebugAutoStatus("Waiting for safe Vulkan continuation or target transition");
                if (!effectList.empty())
                    deviceData.vulkanAutoWorkPending.store(true, std::memory_order_release);
                for (EffectData* effect : effectList) {
                    deviceData.vulkanAutoPendingEffects.try_emplace(effect, active_resource);
                    removalList.push_back(effect);
                }
                continue;
            }

            if (allowedVulkanTargets != nullptr) {
                const auto targetIt = allowedVulkanTargets->find(active_resource.resource.handle);
                if (targetIt == allowedVulkanTargets->end())
                    continue;
                vulkanTargetUsage = targetIt->second;
            }
        }

        bool useNativeStaging = false;
        resource nativeStageRes = {};
        resource_view nativeStageRTV = {};
        resource_view nativeStageRTVSRGB = {};
        resource_view nativeStageSRV = {};

        if (wantsNativeStaging) {
            if (vulkanNativeStaging) {
                // Vulkan image blits require single-sample transfer-capable images.
                // The live render target was opted into transfer usage at resource creation.
                if (desc.texture.samples != 1) {
                    group->setDebugAutoStatus("Unsupported target: multisampled");
                    continue;
                }
                if (!runtime->get_device()->check_capability(device_caps::blit)) {
                    group->setDebugAutoStatus("Unsupported device: image blit unavailable");
                    continue;
                }
            }

            GroupResource& staging = group->GetGroupResource(GroupResourceType::RESOURCE_NATIVE_STAGING);

            resource_desc desired = desc;
            desired.texture.width = runtimeWidth;
            desired.texture.height = runtimeHeight;
            desired.texture.depth_or_layers = 1;
            desired.texture.levels = 1;
            desired.texture.samples = 1;
            desired.texture.format = format_to_typeless(active_resource.format);

            bool stagingCompatible = false;
            if (staging.res != 0) {
                const resource_desc current = runtime->get_device()->get_resource_desc(staging.res);
                stagingCompatible =
                  current.texture.width == desired.texture.width &&
                  current.texture.height == desired.texture.height &&
                  format_to_typeless(current.texture.format) == format_to_typeless(desired.texture.format);
            }

            if (!stagingCompatible) {
                staging.target_description = desired;
                staging.view_format = active_resource.format;
                RestDiag::Log("group '{}' requests a staging buffer {}x{} {} (game target {}x{} {})",
                              group->getName(), desired.texture.width, desired.texture.height,
                              RenderingManager::FormatName(desired.texture.format),
                              desc.texture.width, desc.texture.height, RenderingManager::FormatName(desc.texture.format));
                RestDiag::Count(RestDiag::g_counters.skippedRenders);
                staging.state = GroupResourceState::RESOURCE_INVALID;
                continue;
            }

            groupResourceManager.SetGroupBufferHandles(group,
                                                       GroupResourceType::RESOURCE_NATIVE_STAGING,
                                                       &nativeStageRes,
                                                       &nativeStageRTV,
                                                       &nativeStageRTVSRGB,
                                                       &nativeStageSRV);

            if (nativeStageRes == 0 || nativeStageRTV == 0 || view->rtv == 0) {
                RestDiag::Detail("group '{}' skipped: staging handles missing (res={} rtv={} target rtv={})",
                                 group->getName(), nativeStageRes.handle, nativeStageRTV.handle, view->rtv.handle);
                RestDiag::Count(RestDiag::g_counters.skippedRenders);
                continue;
            }

            if (vulkanNativeStaging) {
                // Vulkan uses the generic ReShade blit API rather than REST's embedded
                // DXBC fullscreen-copy pipeline. Keep the live image in copy-source state
                // until the processed native-size result is blitted back.
                if (vulkanTargetUsage != resource_usage::copy_source)
                    cmd_list->barrier(active_resource.resource, vulkanTargetUsage, resource_usage::copy_source);
                cmd_list->barrier(nativeStageRes, resource_usage::render_target, resource_usage::copy_dest);
                cmd_list->copy_texture_region(active_resource.resource,
                                              0,
                                              nullptr,
                                              nativeStageRes,
                                              0,
                                              nullptr,
                                              filter_mode::min_mag_mip_point);
                cmd_list->barrier(nativeStageRes, resource_usage::copy_dest, resource_usage::render_target);
            } else {
                if (nativeStageSRV == 0 || view->srv == 0) {
                    RestDiag::Detail("group '{}' skipped: staging srv={} target srv={}",
                                     group->getName(), nativeStageSRV.handle, view->srv.handle);
                    RestDiag::Count(RestDiag::g_counters.skippedRenders);
                    continue;
                }

                // The selected resource is the live RTV bound for the matched draw.
                // Temporarily transition it to shader-resource state so the fullscreen copy
                // shader can sample it into the native-size staging surface.
                cmd_list->barrier(active_resource.resource, resource_usage::render_target, resource_usage::shader_resource);

                // Upscale the game's pre-DLSS scene into a native-size scratch surface.
                // This keeps ReShade's effect-created intermediate textures at their normal
                // runtime dimensions, avoiding mixed-resolution shared-resource permutations.
                RestDiag::ScopedCpuTimer stagingTimer(RestDiag::g_counters.stagingCpuMicros);
                shaderManager.CopyResource(cmd_list, view->srv, nativeStageRTV, runtimeWidth, runtimeHeight);
            }

            view_non_srgb = nativeStageRTV;
            view_srgb = nativeStageRTVSRGB != 0 ? nativeStageRTVSRGB : nativeStageRTV;
            useNativeStaging = true;
            staging.state = GroupResourceState::RESOURCE_VALID;
        }

        const bool transitionManualSRV =
          !autoSceneColour && group->getRenderToResourceViews() &&
          cmd_list->get_device()->get_api() == device_api::d3d12 &&
          !useNativeStaging;

        // Manual SRV rendering temporarily turns a shader-resource view into a
        // render target, then restores its original state before the game resumes.
        if (transitionManualSRV) {
            cmd_list->barrier(active_resource.resource, resource_usage::shader_resource, resource_usage::render_target);
        }

        if (!useNativeStaging && preserveTargetAlpha) {
            resource group_res = {};
            groupResourceManager.SetGroupBufferHandles(group, GroupResourceType::RESOURCE_ALPHA, &group_res, &view_non_srgb, &view_srgb, &group_view);

            const bool strict_copy = (runtime->get_device()->get_api() == reshade::api::device_api::d3d12 || runtime->get_device()->get_api() == reshade::api::device_api::vulkan);
            const reshade::api::format copy_typeless = format_to_typeless(desc.texture.format);
            const bool format_copyable = !strict_copy ||
                                         copy_typeless == reshade::api::format::r8g8b8a8_typeless ||
                                         copy_typeless == reshade::api::format::b8g8r8a8_typeless;

            const bool compatible =
                groupResourceManager.IsCompatibleWithGroupFormat(runtime->get_device(), GroupResourceType::RESOURCE_ALPHA, active_resource.resource, group);
            const bool source_copyable = desc.texture.samples <= 1 && desc.type == resource_type::texture_2d && format_copyable;
            if (group_res != 0 && compatible && source_copyable) {
                std::vector<reshade::api::resource_view> bound_rtvs;
                reshade::api::resource_view bound_dsv = { 0 };

                if (strict_copy) {
                    bound_rtvs = (*cmd_list->get_private_data<state_tracking>()).render_targets;
                    bound_dsv = (*cmd_list->get_private_data<state_tracking>()).depth_stencil;
                    // Unbind render targets so we can safely transition them without DX12 validation errors
                    cmd_list->bind_render_targets_and_depth_stencil(0, nullptr, { 0 });
                }

                cmd_list->copy_resource(active_resource.resource, group_res);

                if (strict_copy) {
                    // Rebind targets. ReShade's wrapper will automatically transition them back to RENDER_TARGET.
                    cmd_list->bind_render_targets_and_depth_stencil(static_cast<uint32_t>(bound_rtvs.size()), bound_rtvs.data(), bound_dsv);
                }

                copyPreserveAlpha = true;
            } else {
                view_non_srgb = view->rtv;
                view_srgb = view->rtv_srgb;

                if (!compatible) {
                    groupResource.state = GroupResourceState::RESOURCE_INVALID;
                    groupResource.target_description = desc;
                    groupResource.view_format = active_resource.format;
                }
            }
        } else if (!useNativeStaging) {
            view_non_srgb = view->rtv;
            view_srgb = view->rtv_srgb;
        }

        if (view_non_srgb == 0) {
            RestDiag::Detail("group '{}' skipped: no render target view", group->getName());
            RestDiag::Count(RestDiag::g_counters.skippedRenders);
            continue;
        }

        if (RestDiag::Enabled()) {
            // What render_technique will actually receive. Any mismatch with the swapchain here makes
            // ReShade rebuild its back-buffer copy (and possibly reload effects) on this call.
            device* dev = runtime->get_device();
            const resource effectRes = dev->get_resource_from_view(view_non_srgb);
            const resource_desc effectDesc = dev->get_resource_desc(effectRes);
            const resource_view_desc effectViewDesc = dev->get_resource_view_desc(view_non_srgb);
            const bool effectMismatch =
              effectDesc.texture.width != runtimeWidth || effectDesc.texture.height != runtimeHeight ||
              format_to_typeless(effectDesc.texture.format) != backBufferFormat;

            RestDiag::Count(useNativeStaging ? RestDiag::g_counters.stagedRenders : RestDiag::g_counters.directRenders);
            if (effectMismatch)
                RestDiag::Count(RestDiag::g_counters.mismatchedDirectRenders);

            RestDiag::Detail("render group '{}' ({} technique(s)) | game target {}x{} {} view {} | swapchain {}x{} {} | "
                             "offSize={} formatMismatch={} staging={} preserveAlpha={} auto={} renderToSRV={} | "
                             "effect target {}x{} {} view {}{}",
                             group->getName(), effectList.size(),
                             desc.texture.width, desc.texture.height, RenderingManager::FormatName(desc.texture.format),
                             RenderingManager::FormatName(active_resource.format),
                             runtimeWidth, runtimeHeight, RenderingManager::FormatName(backBufferFormat),
                             offSizeTarget, formatMismatch, useNativeStaging, preserveTargetAlpha, autoSceneColour,
                             group->getRenderToResourceViews(),
                             effectDesc.texture.width, effectDesc.texture.height, RenderingManager::FormatName(effectDesc.texture.format),
                             RenderingManager::FormatName(effectViewDesc.format),
                             effectMismatch ? " | MISMATCH: ReShade will rebuild its back-buffer copy" : "");
        }

        const bool restoreVulkanDirectTarget =
          vulkanAutoSceneColour && vulkanSafeBoundary && !useNativeStaging &&
          vulkanTargetUsage != resource_usage::render_target;
        if (restoreVulkanDirectTarget)
            cmd_list->barrier(active_resource.resource, vulkanTargetUsage, resource_usage::render_target);

        if (group->getFlipBuffer() && runtimeData.specialEffects[REST_FLIP].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_FLIP].technique, cmd_list, view_non_srgb, view_srgb);
        }

        if (group->getToneMap() && runtimeData.specialEffects[REST_TONEMAP_TO_SDR].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_TONEMAP_TO_SDR].technique, cmd_list, view_non_srgb, view_srgb);
        }

        uint32_t renderedTechniqueCount = 0;
        std::string renderedTechniqueOrder;
        for (const auto& effectTech : effectList) {
            if (autoSceneColour) {
                char techniqueName[256] = {};
                size_t techniqueNameSize = sizeof(techniqueName);
                runtime->get_technique_name(effectTech->technique, techniqueName, &techniqueNameSize);

                if (!renderedTechniqueOrder.empty())
                    renderedTechniqueOrder += " -> ";
                renderedTechniqueOrder += techniqueName;
            }

            {
                RestDiag::ScopedCpuTimer techniqueTimer(RestDiag::g_counters.techniqueCpuMicros, &RestDiag::g_counters.techniqueCpuMaxMicros);
                runtime->render_technique(effectTech->technique, cmd_list, view_non_srgb, view_srgb);
                if (RestDiag::DetailEnabled()) {
                    char diagName[256] = {};
                    size_t diagNameSize = sizeof(diagName);
                    runtime->get_technique_name(effectTech->technique, diagName, &diagNameSize);
                    RestDiag::Detail("  render_technique '{}' for group '{}': {} us CPU", diagName, group->getName(), techniqueTimer.ElapsedMicros());
                }
            }

            effectTech->rendered = true;
            ++renderedTechniqueCount;
            RestDiag::Count(RestDiag::g_counters.techniquesRendered);

            removalList.push_back(effectTech);

            rendered = true;
        }

        if (renderedTechniqueCount > 0 && autoSceneColour) {
            const uint32_t effectWidth = useNativeStaging ? runtimeWidth : desc.texture.width;
            const uint32_t effectHeight = useNativeStaging ? runtimeHeight : desc.texture.height;
            group->recordDebugEffectRender(renderedTechniqueCount,
                                           active_resource.resource.handle,
                                           renderedTechniqueOrder,
                                           desc.texture.width,
                                           desc.texture.height,
                                           effectWidth,
                                           effectHeight,
                                           useNativeStaging);
        }

        if (group->getToneMap() && runtimeData.specialEffects[REST_TONEMAP_TO_HDR].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_TONEMAP_TO_HDR].technique, cmd_list, view_non_srgb, view_srgb);
        }

        if (group->getFlipBuffer() && runtimeData.specialEffects[REST_FLIP].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_FLIP].technique, cmd_list, view_non_srgb, view_srgb);
        }

        if (copyPreserveAlpha) {
            resource_view target_view_non_srgb = view->rtv;

            if (target_view_non_srgb != 0)
                shaderManager.CopyResourceMaskAlpha(cmd_list, group_view, target_view_non_srgb, desc.texture.width, desc.texture.height);
        }

        if (useNativeStaging) {
            // Downscale the completed native-size effect result back into the live
            // scene target. Leave the game resource in render-target state so the
            // matched draw can execute normally after REST returns.
            if (vulkanNativeStaging) {
                cmd_list->barrier(nativeStageRes, resource_usage::render_target, resource_usage::copy_source);
                cmd_list->barrier(active_resource.resource, resource_usage::copy_source, resource_usage::copy_dest);
                cmd_list->copy_texture_region(nativeStageRes,
                                              0,
                                              nullptr,
                                              active_resource.resource,
                                              0,
                                              nullptr,
                                              filter_mode::min_mag_mip_point);
                cmd_list->barrier(nativeStageRes, resource_usage::copy_source, resource_usage::render_target);
                if (vulkanTargetUsage != resource_usage::copy_dest)
                    cmd_list->barrier(active_resource.resource, resource_usage::copy_dest, vulkanTargetUsage);
            } else {
                cmd_list->barrier(nativeStageRes, resource_usage::render_target, resource_usage::shader_resource);
                cmd_list->barrier(active_resource.resource, resource_usage::shader_resource, resource_usage::render_target);

                RestDiag::ScopedCpuTimer stagingBackTimer(RestDiag::g_counters.stagingCpuMicros);
                if (view->rtv != 0) {
                    shaderManager.CopyResource(cmd_list, nativeStageSRV, view->rtv, desc.texture.width, desc.texture.height);
                }

                cmd_list->barrier(nativeStageRes, resource_usage::shader_resource, resource_usage::render_target);
            }
        } else if (transitionManualSRV) {
            cmd_list->barrier(active_resource.resource, resource_usage::render_target, resource_usage::shader_resource);
        } else if (restoreVulkanDirectTarget) {
            cmd_list->barrier(active_resource.resource, resource_usage::render_target, vulkanTargetUsage);
        }
    }

    return rendered;
}

void RenderingEffectManager::RenderDeferredVulkanAutoEffects(command_list* cmd_list,
                                                             uint32_t renderTargetCount,
                                                             const render_pass_render_target_desc* renderTargets) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr ||
        cmd_list->get_device()->get_api() != device_api::vulkan || renderTargetCount == 0 || renderTargets == nullptr) {
        return;
    }

    unordered_map<uint64_t, resource_usage> safeTargets;
    safeTargets.reserve(renderTargetCount);
    for (uint32_t i = 0; i < renderTargetCount; ++i) {
        if (renderTargets[i].view == 0 || renderTargets[i].load_op != render_pass_load_op::load)
            continue;

        // Only inject before a continuation pass that LOADs the existing target.
        // CLEAR/DISCARD would immediately overwrite the injected result.
        const resource target = cmd_list->get_device()->get_resource_from_view(renderTargets[i].view);
        if (target != 0)
            safeTargets.try_emplace(target.handle, resource_usage::render_target);
    }

    if (!safeTargets.empty())
        _RenderDeferredVulkanAutoEffects(cmd_list, safeTargets, "same-target LOAD pass");
}

void RenderingEffectManager::RenderDeferredVulkanAutoEffectsAfterBarrier(command_list* cmd_list,
                                                                        uint32_t barrierCount,
                                                                        const resource* resources,
                                                                        const resource_usage* oldStates,
                                                                        const resource_usage* newStates) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr ||
        cmd_list->get_device()->get_api() != device_api::vulkan ||
        barrierCount == 0 || resources == nullptr || oldStates == nullptr || newStates == nullptr) {
        return;
    }

    CommandListDataContainer& commandListData = (*cmd_list->get_private_data<CommandListDataContainer>());
    if (commandListData.vulkanAutoInjectionActive || commandListData.vulkanInsideRenderPass)
        return;

    unordered_map<uint64_t, resource_usage> safeTargets;
    safeTargets.reserve(barrierCount);

    for (uint32_t i = 0; i < barrierCount; ++i) {
        if (resources[i] == 0 || oldStates[i] == resource_usage::undefined ||
            newStates[i] == resource_usage::undefined) {
            continue;
        }

        const uint32_t oldUsage = static_cast<uint32_t>(oldStates[i]);
        const uint32_t newUsage = static_cast<uint32_t>(newStates[i]);
        const uint32_t renderTargetUsage = static_cast<uint32_t>(resource_usage::render_target);

        // Only use the fallback after the render pass has ended and the exact scene
        // target is explicitly leaving render-target usage. This avoids firing on
        // unrelated or in-pass barriers, which can cause intermittent double/early
        // application and visible flicker.
        if ((oldUsage & renderTargetUsage) == 0 || (newUsage & renderTargetUsage) != 0)
            continue;

        safeTargets.insert_or_assign(resources[i].handle, newStates[i]);
    }

    if (!safeTargets.empty())
        _RenderDeferredVulkanAutoEffects(cmd_list, safeTargets, "post-pass RT transition");
}

void RenderingEffectManager::_RenderDeferredVulkanAutoEffects(
  command_list* cmd_list,
  const unordered_map<uint64_t, resource_usage>& safeTargets,
  const char* boundaryName) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr || safeTargets.empty())
        return;

    DeviceDataContainer& deviceData = (*cmd_list->get_device()->get_private_data<DeviceDataContainer>());
    if (deviceData.current_runtime == nullptr)
        return;

    unique_lock<shared_mutex> renderLock(deviceData.render_mutex);
    if (deviceData.vulkanAutoPendingEffects.empty())
        return;

    RuntimeDataContainer& runtimeData = (*deviceData.current_runtime->get_private_data<RuntimeDataContainer>());
    unordered_set<EffectData*> toRenderNames;

    for (const auto& [effect, data] : deviceData.vulkanAutoPendingEffects) {
        if (effect == nullptr || data.group == nullptr || data.resource == 0)
            continue;
        if (!data.group->isAutoSceneColourActive(device_api::vulkan))
            continue;
        if (!safeTargets.contains(data.resource.handle))
            continue;
        if (!effect->enabled || effect->rendered)
            continue;

        toRenderNames.insert(effect);
    }

    if (toRenderNames.empty())
        return;

    CommandListDataContainer& commandListData = (*cmd_list->get_private_data<CommandListDataContainer>());
    commandListData.vulkanAutoInjectionActive = true;

    if (!deviceData.rendered_effects) {
        deviceData.current_runtime->render_effects(cmd_list, resource_view{ 0 }, resource_view{ 0 });
        deviceData.rendered_effects = true;
    }

    vector<EffectData*> removalList;

    shared_lock<shared_mutex> techLock(runtimeData.technique_mutex);
    _RenderEffects(cmd_list,
                   deviceData,
                   runtimeData,
                   deviceData.vulkanAutoPendingEffects,
                   removalList,
                   toRenderNames,
                   true,
                   &safeTargets);
    techLock.unlock();

    for (auto* effect : removalList) {
        const auto pendingIt = deviceData.vulkanAutoPendingEffects.find(effect);
        if (pendingIt != deviceData.vulkanAutoPendingEffects.end() &&
            pendingIt->second.group != nullptr && boundaryName != nullptr) {
            pendingIt->second.group->setDebugLastVulkanBoundary(boundaryName);
        }
        deviceData.vulkanAutoPendingEffects.erase(effect);
    }

    if (deviceData.vulkanAutoPendingEffects.empty())
        deviceData.vulkanAutoWorkPending.store(false, std::memory_order_release);

    commandListData.vulkanAutoInjectionActive = false;
}

void RenderingEffectManager::RenderEffects(command_list* cmd_list, uint64_t callLocation, uint64_t invocation) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr) {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = (*cmd_list->get_private_data<CommandListDataContainer>());
    DeviceDataContainer& deviceData = (*device->get_private_data<DeviceDataContainer>());

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    RestDiag::ScopedCpuTimer renderEffectsTimer(RestDiag::g_counters.renderEffectsCpuMicros);
    unique_lock<shared_mutex> renderLock(deviceData.render_mutex);

    if (deviceData.current_runtime == nullptr || (commandListData.ps.techniquesToRender.size() == 0 && commandListData.vs.techniquesToRender.size() == 0 &&
                                                  commandListData.cs.techniquesToRender.size() == 0)) {
        return;
    }

    RuntimeDataContainer& runtimeData = (*deviceData.current_runtime->get_private_data<RuntimeDataContainer>());
    bool toRender = false;
    unordered_set<EffectData*> psToRenderNames;
    unordered_set<EffectData*> vsToRenderNames;
    unordered_set<EffectData*> csToRenderNames;

    if (invocation & MATCH_EFFECT_PS) {
        RenderingManager::QueueOrDequeue(
          cmd_list, deviceData, commandListData, commandListData.ps.techniquesToRender, psToRenderNames, callLocation, 0, MATCH_EFFECT_PS);
    }

    if (invocation & MATCH_EFFECT_VS) {
        RenderingManager::QueueOrDequeue(
          cmd_list, deviceData, commandListData, commandListData.vs.techniquesToRender, vsToRenderNames, callLocation, 1, MATCH_EFFECT_VS);
    }

    if (invocation & MATCH_EFFECT_CS) {
        RenderingManager::QueueOrDequeue(
          cmd_list, deviceData, commandListData, commandListData.cs.techniquesToRender, csToRenderNames, callLocation, 2, MATCH_EFFECT_CS);
    }

    bool rendered = false;
    vector<EffectData*> psRemovalList;
    vector<EffectData*> vsRemovalList;
    vector<EffectData*> csRemovalList;

    if (psToRenderNames.empty() && vsToRenderNames.empty() && csToRenderNames.empty()) {
        return;
    }

    if (!deviceData.rendered_effects) {
        deviceData.current_runtime->render_effects(cmd_list, resource_view{ 0 }, resource_view{ 0 });
        deviceData.rendered_effects = true;
    }

    shared_lock<shared_mutex> techLock(runtimeData.technique_mutex);
    bool psRendered = false;
    if (psToRenderNames.size() > 0) {
        psRendered = _RenderEffects(cmd_list, deviceData, runtimeData, commandListData.ps.techniquesToRender, psRemovalList, psToRenderNames);
    }
    bool vsRendered = false;
    if (vsToRenderNames.size() > 0) {
        vsRendered = _RenderEffects(cmd_list, deviceData, runtimeData, commandListData.vs.techniquesToRender, vsRemovalList, vsToRenderNames);
    }
    bool csRendered = false;
    if (csToRenderNames.size() > 0) {
        csRendered = _RenderEffects(cmd_list, deviceData, runtimeData, commandListData.cs.techniquesToRender, csRemovalList, csToRenderNames);
    }
    rendered = psRendered || vsRendered || csRendered;
    techLock.unlock();

    for (auto& g : psRemovalList) {
        commandListData.ps.techniquesToRender.erase(g);
    }

    for (auto& g : vsRemovalList) {
        commandListData.vs.techniquesToRender.erase(g);
    }

    for (auto& g : csRemovalList) {
        commandListData.cs.techniquesToRender.erase(g);
    }

    if (rendered) {
        RestDiag::Count(RestDiag::g_counters.renderEffectsCalls);
        (*cmd_list->get_private_data<state_tracking>()).apply(cmd_list);
    }
}

void RenderingEffectManager::PreventRuntimeReload(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list) {
    if (runtime == nullptr)
        return;

    RuntimeDataContainer& runtimeData = (*runtime->get_private_data<RuntimeDataContainer>());
    DeviceDataContainer& deviceData = (*runtime->get_device()->get_private_data<DeviceDataContainer>());

    // cringe
    if (runtimeData.specialEffects[REST_NOOP].technique != 0) {
        resource res = runtime->get_current_back_buffer();
        const std::shared_ptr<GlobalResourceView>& view = resourceManager.GetResourceView(runtime->get_device(), res.handle);

        if (view == nullptr || view->rtv == 0)
            return;

        resource_view active_rtv = view->rtv;
        resource_view active_rtv_srgb = view->rtv_srgb;

        if (deviceData.resourceManagerData.dummy_rtv != 0)
            runtime->render_technique(
              runtimeData.specialEffects[REST_NOOP].technique, cmd_list, deviceData.resourceManagerData.dummy_rtv, deviceData.resourceManagerData.dummy_rtv);

        runtime->render_technique(runtimeData.specialEffects[REST_NOOP].technique, cmd_list, active_rtv, active_rtv_srgb);
    }
}