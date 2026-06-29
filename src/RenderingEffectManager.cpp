#include "RenderingEffectManager.h"
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
    RuntimeDataContainer& runtimeData = *runtime->get_private_data<RuntimeDataContainer>();
    DeviceDataContainer& deviceData = *device->get_private_data<DeviceDataContainer>();
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

bool RenderingEffectManager::_RenderEffects(command_list* cmd_list,
                                            DeviceDataContainer& deviceData,
                                            RuntimeDataContainer& runtimeData,
                                            const effect_queue& techniquesToRender,
                                            vector<EffectData*>& removalList,
                                            const unordered_set<EffectData*>& toRenderNames) {
    bool rendered = false;
    CommandListDataContainer& cmdData = *cmd_list->get_private_data<CommandListDataContainer>();
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

        if (active_resource.resource == 0) {
            continue;
        }

        resource_view view_non_srgb = {};
        resource_view view_srgb = {};
        resource_view group_view = {};
        resource_desc desc = cmd_list->get_device()->get_resource_desc(active_resource.resource);

        // render_technique requires the target to match the effect/screenshot resolution. Marking a
        // shader that draws into an off-size buffer (half-res bloom, shadow map, etc.) violates that
        // and can corrupt the frame or hang on DX12. LOG-ONLY for now (still renders) so we can
        // confirm real full-screen targets never trip this before we make it actually skip. Throttled
        // to once per distinct (group,size).
        {
            uint32_t eff_w = 0, eff_h = 0;
            runtime->get_screenshot_width_and_height(&eff_w, &eff_h);
            if (eff_w != 0 && eff_h != 0 && (desc.texture.width != eff_w || desc.texture.height != eff_h)) {
                static std::unordered_set<uint64_t> s_logged_size_mismatch;
                const uint64_t key = (static_cast<uint64_t>(group->getId()) << 40) ^
                                     (static_cast<uint64_t>(desc.texture.width) << 16) ^ desc.texture.height;
                if (s_logged_size_mismatch.insert(key).second) {
                    reshade::log::message(reshade::log::level::warning,
                        ("[REST] RT size mismatch (effect would render into an off-size target): target=" +
                         std::to_string(desc.texture.width) + "x" + std::to_string(desc.texture.height) +
                         " effect=" + std::to_string(eff_w) + "x" + std::to_string(eff_h) +
                         " - rendering anyway (log-only guard). If this is a target you marked, it's not full-screen.").c_str());
                }
            }
        }

        GroupResource& groupResource = group->GetGroupResource(GroupResourceType::RESOURCE_ALPHA);
        const shared_ptr<GlobalResourceView>& view = resourceManager.GetResourceView(runtime->get_device(), active_resource);
        bool copyPreserveAlpha = false;

        if (view == nullptr) {
            continue;
        }

        if (group->getPreserveAlpha()) {
            resource group_res = {};
            groupResourceManager.SetGroupBufferHandles(group, GroupResourceType::RESOURCE_ALPHA, &group_res, &view_non_srgb, &view_srgb, &group_view);
            // group_res can be 0 if the group render target creation failed (e.g. HDR/FP16 targets
            // whose create_resource fails). Copying into a null resource hangs the GPU, so only take
            // the preserve-alpha path when the buffer is both format-compatible AND actually created.
            const bool compatible =
                groupResourceManager.IsCompatibleWithGroupFormat(runtime->get_device(), GroupResourceType::RESOURCE_ALPHA, active_resource.resource, group);
            // Also require a single-sampled 2D source: copy_resource of an MSAA / non-2D target
            // hangs the device on DX12. (HDR sources are already handled - their group_res fails to
            // create, so group_res == 0 above.)
            const bool source_copyable = desc.texture.samples <= 1 && desc.type == resource_type::texture_2d;
            if (group_res != 0 && compatible && source_copyable) {
                cmd_list->copy_resource(active_resource.resource, group_res);
                copyPreserveAlpha = true;
            } else {
                view_non_srgb = view->rtv;
                view_srgb = view->rtv_srgb;

                // Only request a (re)created group buffer when the format is incompatible. If it's
                // compatible but group_res is still 0, creation already failed for this target
                // (e.g. HDR) - don't loop trying to recreate it every frame; just render directly.
                if (!compatible) {
                    groupResource.state = GroupResourceState::RESOURCE_INVALID;
                    groupResource.target_description = desc;
                    groupResource.view_format = active_resource.format;
                }
            }
        } else {
            view_non_srgb = view->rtv;
            view_srgb = view->rtv_srgb;
        }

        if (view_non_srgb == 0) {
            continue;
        }

        if (group->getFlipBuffer() && runtimeData.specialEffects[REST_FLIP].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_FLIP].technique, cmd_list, view_non_srgb, view_srgb);
        }

        if (group->getToneMap() && runtimeData.specialEffects[REST_TONEMAP_TO_SDR].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_TONEMAP_TO_SDR].technique, cmd_list, view_non_srgb, view_srgb);
        }

        for (const auto& effectTech : effectList) {
            runtime->render_technique(effectTech->technique, cmd_list, view_non_srgb, view_srgb);

            effectTech->rendered = true;

            removalList.push_back(effectTech);

            rendered = true;
        }

        if (group->getToneMap() && runtimeData.specialEffects[REST_TONEMAP_TO_HDR].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_TONEMAP_TO_HDR].technique, cmd_list, view_non_srgb, view_srgb);
        }

        if (group->getFlipBuffer() && runtimeData.specialEffects[REST_FLIP].technique != 0) {
            runtime->render_technique(runtimeData.specialEffects[REST_FLIP].technique, cmd_list, view_non_srgb, view_srgb);
        }

        if (copyPreserveAlpha) {
            resource_view target_view_non_srgb = view->rtv;
            resource_view target_view_srgb = view->rtv_srgb;

            if (target_view_non_srgb != 0)
                shaderManager.CopyResourceMaskAlpha(cmd_list, group_view, target_view_non_srgb, desc.texture.width, desc.texture.height);
        }
    }

    return rendered;
}

void RenderingEffectManager::RenderEffects(command_list* cmd_list, uint64_t callLocation, uint64_t invocation) {
    if (cmd_list == nullptr || cmd_list->get_device() == nullptr) {
        return;
    }

    device* device = cmd_list->get_device();
    CommandListDataContainer& commandListData = *cmd_list->get_private_data<CommandListDataContainer>();
    DeviceDataContainer& deviceData = *device->get_private_data<DeviceDataContainer>();

    // Remove call location from queue
    commandListData.commandQueue &= ~(invocation << (callLocation * MATCH_DELIMITER));

    unique_lock<shared_mutex> renderLock(deviceData.render_mutex);

    if (deviceData.current_runtime == nullptr || (commandListData.ps.techniquesToRender.size() == 0 && commandListData.vs.techniquesToRender.size() == 0 &&
                                                  commandListData.cs.techniquesToRender.size() == 0)) {
        return;
    }

    RuntimeDataContainer& runtimeData = *deviceData.current_runtime->get_private_data<RuntimeDataContainer>();
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

    if (psToRenderNames.size() == 0 && vsToRenderNames.size() == 0) {
        return;
    }

    if (!deviceData.rendered_effects) {
        deviceData.current_runtime->render_effects(cmd_list, resource_view{ 0 }, resource_view{ 0 });
        deviceData.rendered_effects = true;
    }

    shared_lock<shared_mutex> techLock(runtimeData.technique_mutex);
    rendered =
      (psToRenderNames.size() > 0) &&
        _RenderEffects(cmd_list, deviceData, runtimeData, commandListData.ps.techniquesToRender, psRemovalList, psToRenderNames) ||
      (vsToRenderNames.size() > 0) &&
        _RenderEffects(cmd_list, deviceData, runtimeData, commandListData.vs.techniquesToRender, vsRemovalList, vsToRenderNames) ||
      (csToRenderNames.size() > 0) && _RenderEffects(cmd_list, deviceData, runtimeData, commandListData.cs.techniquesToRender, csRemovalList, csToRenderNames);
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
        cmd_list->get_private_data<state_tracking>()->apply(cmd_list);
    }
}

void RenderingEffectManager::PreventRuntimeReload(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list) {
    if (runtime == nullptr)
        return;

    RuntimeDataContainer& runtimeData = *runtime->get_private_data<RuntimeDataContainer>();
    DeviceDataContainer& deviceData = *runtime->get_device()->get_private_data<DeviceDataContainer>();

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