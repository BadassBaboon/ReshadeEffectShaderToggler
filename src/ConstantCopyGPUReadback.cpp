#include "ConstantCopyGPUReadback.h"
#include "PipelinePrivateData.h"
#include <cstring>

using namespace Shim::Constants;
using namespace reshade::api;
using namespace ShaderToggler;
using namespace std;

bool ConstantCopyGPUReadback::GetHostConstantBuffer(reshade::api::command_list* cmd_list,
                                                    ShaderToggler::ToggleGroup* group,
                                                    vector<uint8_t>& dest,
                                                    size_t size,
                                                    uint64_t resourceHandle, uint64_t offset) {
    resource src = resource{ resourceHandle };
    auto* dev = cmd_list->get_device();
    // Mapping a staging buffer cannot wait for a copy that has only been recorded.
    if (dev->get_api() == device_api::vulkan)
        return false;
    if (dev->get_api() == device_api::d3d12) {
        const auto heap = dev->get_resource_desc(src).heap;
        void* data = nullptr;
        if ((heap != memory_heap::upload && heap != memory_heap::gpu_upload) ||
            !dev->map_buffer_region(src, offset, size, map_access::read_only, &data))
            return false;
        memcpy(dest.data(), data, size);
        dev->unmap_buffer_region(src);
        return true;
    }
    auto* runtime = dev->get_private_data<DeviceDataContainer>()->current_runtime;
    if (runtime == nullptr || cmd_list != runtime->get_command_queue()->get_immediate_command_list())
        return false;
    ShaderToggler::GroupResource& dst = group->GetGroupResource(ShaderToggler::GroupResourceType::RESOURCE_CONSTANTS_COPY);
    if (groupResourceManager.IsCompatibleWithGroupFormat(cmd_list->get_device(), ShaderToggler::GroupResourceType::RESOURCE_CONSTANTS_COPY, src, group)) {
        void* data = nullptr;
        cmd_list->copy_resource(src, dst.res);
        if (cmd_list->get_device()->map_buffer_region(dst.res, offset, size, map_access::read_only, &data)) {
            memcpy(dest.data(), data, size);
            cmd_list->get_device()->unmap_buffer_region(dst.res);
            return true;
        }
    } else {
        dst.state = ShaderToggler::GroupResourceState::RESOURCE_INVALID;
        dst.target_description = cmd_list->get_device()->get_resource_desc(src);
    }
    return false;
}
