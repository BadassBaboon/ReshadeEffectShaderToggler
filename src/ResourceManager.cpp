#include "ResourceManager.h"
#include <format>

using namespace Rendering;
using namespace reshade::api;
using namespace Shim::Resources;
using namespace std;

ResourceShimType ResourceManager::ResolveResourceShimType(const string& stype) {
    if (stype == "none")
        return ResourceShimType::Resource_Shim_None;
    else if (stype == "srgb")
        return ResourceShimType::Resource_Shim_SRGB;
    else if (stype == "ffxiv")
        return ResourceShimType::Resource_Shim_FFXIV;

    return ResourceShimType::Resource_Shim_None;
}

void ResourceManager::Init() {

    switch (_shimType) {
        case Resource_Shim_None:
            rShim = nullptr;
            break;
        case Resource_Shim_SRGB: {
            static ResourceShimSRGB srgbShim;
            rShim = &srgbShim;
        } break;
        case Resource_Shim_FFXIV: {
            static ResourceShimFFXIV ffxivShim;
            rShim = &ffxivShim;
        } break;
        default:
            rShim = nullptr;
            break;
    }

    if (rShim != nullptr && rShim->Init()) {
        reshade::log::message(reshade::log::level::info, std::format("Resource shim initialized").c_str());
    } else {
        reshade::log::message(reshade::log::level::info, std::format("No resource shim initialized").c_str());
    }
}

void ResourceManager::InitBackbuffer(swapchain* runtime) {
    // Create backbuffer resource views
    device* dev = runtime->get_device();
    DeviceDataContainer& data = (*dev->get_private_data<DeviceDataContainer>());

    resource_desc desc = dev->get_resource_desc(runtime->get_back_buffer(0));

    // A Vulkan swapchain image can report an unknown heap. A private texture needs its own allocation.
    const resource_desc dummy_desc(1, 1, 1, 1, desc.texture.format, 1, memory_heap::default_,
        resource_usage::render_target | resource_usage::copy_source);

    bool resCreated = runtime->get_device()->create_resource(dummy_desc, nullptr, resource_usage::render_target, &data.resourceManagerData.dummy_res);

    if (resCreated) {
        runtime->get_device()->create_resource_view(
          data.resourceManagerData.dummy_res, resource_usage::render_target, resource_view_desc{ desc.texture.format }, &data.resourceManagerData.dummy_rtv);
    }
}

void ResourceManager::ClearBackbuffer(reshade::api::swapchain* runtime) {
    device* dev = runtime->get_device();
    DeviceDataContainer& data = (*dev->get_private_data<DeviceDataContainer>());

    uint32_t count = runtime->get_back_buffer_count();

    if (data.resourceManagerData.dummy_rtv != 0) {
        runtime->get_device()->destroy_resource_view(data.resourceManagerData.dummy_rtv);
        data.resourceManagerData.dummy_rtv = { 0 };
    }

    if (data.resourceManagerData.dummy_res != 0) {
        runtime->get_device()->destroy_resource(data.resourceManagerData.dummy_res);
        data.resourceManagerData.dummy_res = { 0 };
    }
}

bool ResourceManager::OnCreateSwapchain(reshade::api::swapchain_desc& desc, void* hwnd) {
    return false;
}

void ResourceManager::OnInitSwapchain(reshade::api::swapchain* swapchain) {
    InitBackbuffer(swapchain);
}

void ResourceManager::OnDestroySwapchain(reshade::api::swapchain* swapchain) {
    OnDestroyDevice(swapchain->get_device(), true);
    ClearBackbuffer(swapchain);
}

bool ResourceManager::OnCreateResource(device* device, resource_desc& desc, subresource_data* initial_data, resource_usage initial_state) {
    bool ret = false;

    if (static_cast<uint32_t>(desc.usage & resource_usage::render_target) && !static_cast<uint32_t>(desc.usage & resource_usage::shader_resource) &&
        desc.type == resource_type::texture_2d) {
        desc.usage |= resource_usage::shader_resource;
        ret = true;
    }

    if (rShim != nullptr) {
        ret |= rShim->OnCreateResource(device, desc, initial_data, initial_state);
    }

    return ret;
}

void ResourceManager::OnInitResource(device* device,
                                     const resource_desc& desc,
                                     const subresource_data* initData,
                                     resource_usage usage,
                                     reshade::api::resource handle) {
    auto& data = (*device->get_private_data<DeviceDataContainer>());

    if (rShim != nullptr) {
        rShim->OnInitResource(device, desc, initData, usage, handle);
    }
}

void ResourceManager::OnDestroyResource(device* device, resource res) {
    if (rShim != nullptr) {
        rShim->OnDestroyResource(device, res);
    }

    auto& cache = device->get_private_data<DeviceDataContainer>()->resourceManagerData;
    std::unique_lock lock(cache.viewMutex);
    const auto it = cache.views.find(res.handle);
    if (it != cache.views.end())
        it->second->state = GlobalResourceState::RESOURCE_INVALID;
}

void ResourceManager::OnDestroyDevice(device* device, bool validDevice) {
    auto& cache = device->get_private_data<DeviceDataContainer>()->resourceManagerData;
    decltype(cache.views) views;
    {
        std::unique_lock lock(cache.viewMutex);
        views.swap(cache.views);
    }
    for (auto& [handle, view] : views)
        view->Dispose(validDevice);
    DisposePreview(device);
}

bool ResourceManager::OnCreateResourceView(device* device, resource resource, resource_usage usage_type, resource_view_desc& desc) {
    if (rShim != nullptr) {
        return rShim->OnCreateResourceView(device, resource, usage_type, desc);
    }

    return false;
}

void ResourceManager::OnInitResourceView(device* device, resource resource, resource_usage usage_type, const resource_view_desc& desc, resource_view view) {}

void ResourceManager::OnDestroyResourceView(device* device, resource_view view) {}

std::shared_ptr<GlobalResourceView> ResourceManager::GetResourceView(device* device, const ResourceRenderData& data) {
    return GetResourceView(device, data.resource.handle, data.format);
}

std::shared_ptr<GlobalResourceView> ResourceManager::GetResourceView(device* device, uint64_t handle, reshade::api::format format) {
    if (handle == 0) {
        return {};
    }

    auto& cache = device->get_private_data<DeviceDataContainer>()->resourceManagerData;
    std::unique_lock lock(cache.viewMutex);

    auto& res = cache.views[handle];

    if (res == nullptr) {
        res = std::make_shared<GlobalResourceView>(device, resource{ handle }, format);
    }

    if (res->state == GlobalResourceState::RESOURCE_INVALID) {
        return {};
    }

    if (res->state == GlobalResourceState::RESOURCE_VALID)
        res->state = GlobalResourceState::RESOURCE_USED;

    return res;
}

void ResourceManager::DisposePreview(reshade::api::device* device) {
    if (device == nullptr) {
        return;
    }

    DeviceDataContainer& data = (*device->get_private_data<DeviceDataContainer>());

    if (data.resourceManagerData.preview_res[0] == 0 && data.resourceManagerData.preview_res[1] == 0)
        return;

    for (uint32_t i = 0; i < 2; i++) {
        if (data.resourceManagerData.preview_srv[i] != 0) {
            device->destroy_resource_view(data.resourceManagerData.preview_srv[i]);
        }

        if (data.resourceManagerData.preview_rtv[i] != 0) {
            device->destroy_resource_view(data.resourceManagerData.preview_rtv[i]);
        }

        if (data.resourceManagerData.preview_res[i] != 0) {
            device->destroy_resource(data.resourceManagerData.preview_res[i]);
        }

        data.resourceManagerData.preview_res[i] = resource{ 0 };
        data.resourceManagerData.preview_srv[i] = resource_view{ 0 };
        data.resourceManagerData.preview_rtv[i] = resource_view{ 0 };
    }
}

void ResourceManager::OnEffectsReloading(effect_runtime* runtime) {
    effects_reloading = true;
}

void ResourceManager::OnEffectsReloaded(effect_runtime* runtime) {
    effects_reloading = false;
}

void ResourceManager::CheckResourceViews(reshade::api::effect_runtime* runtime) {
    if (!effects_reloading) {
        auto& cache = runtime->get_device()->get_private_data<DeviceDataContainer>()->resourceManagerData;
        std::vector<std::shared_ptr<GlobalResourceView>> expired;
        std::unique_lock lock(cache.viewMutex);
        for (auto view = cache.views.begin(); view != cache.views.end();) {
            bool referenced = view->second.use_count() > 1;

            // valid but not used or just invalid, dispose
            if (view->second->state != GlobalResourceState::RESOURCE_USED && !referenced) {
                expired.push_back(std::move(view->second));
                view = cache.views.erase(view);
                continue;
            }

            if (view->second->state == GlobalResourceState::RESOURCE_USED) {
                view->second->state = GlobalResourceState::RESOURCE_VALID;
            }

            view++;
        }
    }
}

void ResourceManager::CheckPreview(reshade::api::command_list* cmd_list, reshade::api::device* device) {
    if (device == nullptr) {
        return;
    }

    DeviceDataContainer& deviceData = (*device->get_private_data<DeviceDataContainer>());

    std::unique_lock lock(deviceData.binding_mutex);
    if (deviceData.huntPreview.recreate_preview) {
        DisposePreview(device);
        resource_desc desc = deviceData.huntPreview.target_desc;
        // Vulkan copies the selected subresource directly. D3D draws into an RGBA8 display texture.
        const bool vulkan = device->get_api() == device_api::vulkan;
        const auto sourceFormat = vulkan ? desc.texture.format : format_to_typeless(desc.texture.format);
        const auto pongFormat = vulkan ? sourceFormat : format::r8g8b8a8_typeless;
        const auto pongViewFormat = vulkan ? deviceData.huntPreview.view_format : format::r8g8b8a8_unorm;
        resource_desc preview_desc[2] = { resource_desc(desc.texture.width,
                                                        desc.texture.height,
                                                        1,
                                                        1,
                                                        sourceFormat,
                                                        1,
                                                        memory_heap::default_,
                                                        resource_usage::copy_dest | resource_usage::copy_source | resource_usage::shader_resource |
                                                          resource_usage::render_target),
                                          resource_desc(desc.texture.width,
                                                        desc.texture.height,
                                                        1,
                                                        1,
                                                        pongFormat,
                                                        1,
                                                        memory_heap::default_,
                                                        resource_usage::copy_dest | resource_usage::shader_resource | resource_usage::render_target) };

        for (uint32_t i = 0; i < 2; i++) {
            const reshade::api::format view_typed = (i == 0) ? format_to_default_typed(deviceData.huntPreview.view_format, 0) : pongViewFormat;

            if (!device->create_resource(preview_desc[i], nullptr, resource_usage::shader_resource, &deviceData.resourceManagerData.preview_res[i])) {
                reshade::log::message(reshade::log::level::error, "Failed to create preview render target!");
            }

            if (deviceData.resourceManagerData.preview_res[i] != 0 &&
                !device->create_resource_view(deviceData.resourceManagerData.preview_res[i],
                                              resource_usage::shader_resource,
                                              resource_view_desc(view_typed),
                                              &deviceData.resourceManagerData.preview_srv[i])) {
                reshade::log::message(reshade::log::level::error, "Failed to create preview shader resource view!");
            }

            if (deviceData.resourceManagerData.preview_res[i] != 0 &&
                !device->create_resource_view(deviceData.resourceManagerData.preview_res[i],
                                              resource_usage::render_target,
                                              resource_view_desc(view_typed),
                                              &deviceData.resourceManagerData.preview_rtv[i])) {
                reshade::log::message(reshade::log::level::error, "Failed to create preview render target view!");
            }
        }
    }
}

void ResourceManager::SetPingPreviewHandles(reshade::api::device* device,
                                            reshade::api::resource* res,
                                            reshade::api::resource_view* rtv,
                                            reshade::api::resource_view* srv) {
    DeviceDataContainer& deviceData = (*device->get_private_data<DeviceDataContainer>());

    if (deviceData.resourceManagerData.preview_res[0] != 0) {
        if (res != nullptr)
            *res = deviceData.resourceManagerData.preview_res[0];
        if (rtv != nullptr)
            *rtv = deviceData.resourceManagerData.preview_rtv[0];
        if (srv != nullptr)
            *srv = deviceData.resourceManagerData.preview_srv[0];
    }
}

void ResourceManager::SetPongPreviewHandles(reshade::api::device* device,
                                            reshade::api::resource* res,
                                            reshade::api::resource_view* rtv,
                                            reshade::api::resource_view* srv) {
    DeviceDataContainer& deviceData = (*device->get_private_data<DeviceDataContainer>());

    if (deviceData.resourceManagerData.preview_res[1] != 0) {
        if (res != nullptr)
            *res = deviceData.resourceManagerData.preview_res[1];
        if (rtv != nullptr)
            *rtv = deviceData.resourceManagerData.preview_rtv[1];
        if (srv != nullptr)
            *srv = deviceData.resourceManagerData.preview_srv[1];
    }
}

bool ResourceManager::IsCompatibleWithPreviewFormat(reshade::api::device* device, reshade::api::resource res, reshade::api::format view_format) {
    DeviceDataContainer& deviceData = (*device->get_private_data<DeviceDataContainer>());

    if (deviceData.resourceManagerData.preview_res[0] == 0 || deviceData.resourceManagerData.preview_srv[0] == 0 || res == 0)
        return false;

    resource_desc res_desc = device->get_resource_desc(res);
    resource_desc preview_desc = device->get_resource_desc(deviceData.resourceManagerData.preview_res[0]);
    resource_view_desc preview_view_desc = device->get_resource_view_desc(deviceData.resourceManagerData.preview_srv[0]);

    if ((format_to_typeless(view_format) == format_to_typeless(preview_view_desc.format)) && res_desc.texture.width == preview_desc.texture.width &&
        res_desc.texture.height == preview_desc.texture.height) {
        return true;
    }

    return false;
}

EmbeddedResourceData ResourceManager::GetResourceData(uint16_t id) {
    HMODULE hModule = NULL;
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCTSTR)GetResourceData, &hModule))
        return { nullptr, 0 };

    HRSRC myResource = ::FindResource(hModule, MAKEINTRESOURCE(id), RT_RCDATA);

    if (myResource != 0) {
        DWORD myResourceSize = SizeofResource(hModule, myResource);
        HGLOBAL myResourceData = LoadResource(hModule, myResource);

        if (myResourceData != 0) {
            const char* pMyBinaryData = static_cast<const char*>(LockResource(myResourceData));
            return EmbeddedResourceData{ pMyBinaryData, myResourceSize };
        }
    }

    return EmbeddedResourceData{ nullptr, 0 };
}