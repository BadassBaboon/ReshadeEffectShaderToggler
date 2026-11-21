#include "ConstantCopyNierReplicant.h"
#include <cstring>

using namespace Shim::Constants;
using namespace reshade::api;

sig_nier_replicant_cbload* ConstantCopyNierReplicant::org_nier_replicant_cbload = nullptr;
void* ConstantCopyNierReplicant::Origin = nullptr;
size_t ConstantCopyNierReplicant::Size = 0;

ConstantCopyNierReplicant::ConstantCopyNierReplicant() {}

ConstantCopyNierReplicant::~ConstantCopyNierReplicant() {}

bool ConstantCopyNierReplicant::Init() {
    return Shim::GameHookT<sig_nier_replicant_cbload>::Hook(&org_nier_replicant_cbload, detour_nier_replicant_cbload, nier_replicant_cbload);
}

bool ConstantCopyNierReplicant::UnInit() {
    return MH_Uninitialize() == MH_OK;
}

void ConstantCopyNierReplicant::OnMapBufferRegion(device* device, resource resource, uint64_t offset, uint64_t size, map_access access, void** data) {
    if (Origin != nullptr && (access == map_access::write_discard || access == map_access::write_only)) {
        std::unique_lock<std::shared_mutex> lock(deviceHostMutex);
        const auto& it = deviceToHostConstantBuffer.find(resource.handle);

        if (it != deviceToHostConstantBuffer.end()) {
            auto& [_, buf] = *it;
            if (offset < buf.size()) {
                const size_t bytes = static_cast<size_t>(std::min<uint64_t>(size, buf.size() - offset));
                memmove(buf.data() + offset, Origin, std::min(Size, bytes));
            }
        }
    }
}

void ConstantCopyNierReplicant::OnUnmapBufferRegion(device* device, resource resource) {}

void __fastcall ConstantCopyNierReplicant::detour_nier_replicant_cbload(intptr_t p1, intptr_t* p2, uintptr_t p3) {
    ConstantCopyNierReplicant::Origin =
      reinterpret_cast<uint8_t*>((p3 & 0xffffffff) * *reinterpret_cast<uintptr_t*>(p1 + 0xd0) + *reinterpret_cast<intptr_t*>(p1 + 0x88));
    ConstantCopyNierReplicant::Size = *reinterpret_cast<uintptr_t*>(p1 + 0xd0);

    org_nier_replicant_cbload(p1, p2, p3);

    ConstantCopyNierReplicant::Origin = nullptr;
    ConstantCopyNierReplicant::Size = 0;
}