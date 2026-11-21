#include "ConstantCopyMemcpySingular.h"

using namespace Shim::Constants;
using namespace reshade::api;
using namespace std;

thread_local BufferCopy ConstantCopyMemcpySingular::_bufferCopy;

ConstantCopyMemcpySingular::ConstantCopyMemcpySingular() {
    _instance = this;
}

ConstantCopyMemcpySingular::~ConstantCopyMemcpySingular() {}

void ConstantCopyMemcpySingular::OnMapBufferRegion(device* device, resource resource, uint64_t offset, uint64_t size, map_access access, void** data) {
    if (access == map_access::write_discard || access == map_access::write_only) {
        resource_desc desc = device->get_resource_desc(resource);
        if (desc.type == resource_type::buffer && desc.heap == memory_heap::upload && (desc.usage & resource_usage::constant_buffer) != resource_usage::undefined)
            _bufferCopy = BufferCopy{resource.handle, *data, offset, size, desc.buffer.size};
    }
}

void ConstantCopyMemcpySingular::OnUnmapBufferRegion(device* device, resource resource) {
    if (_bufferCopy.resource == resource.handle)
        _bufferCopy = {};
}

void ConstantCopyMemcpySingular::OnMemcpy(void* dest, void* src, size_t size) {
    uint64_t offset;
    if (_bufferCopy.contains(dest, size, offset))
        SetHostConstantBuffer(_bufferCopy.resource, src, size, offset, _bufferCopy.bufferSize);
}
