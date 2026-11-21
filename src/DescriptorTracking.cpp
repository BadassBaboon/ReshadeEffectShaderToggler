/*
 * Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "DescriptorTracking.h"
#include "reshade.hpp"
#include <algorithm>

using namespace reshade::api;

const descriptor_tracking::table_info* descriptor_tracking::find_table(pipeline_layout layout, uint32_t slot) const {
    const auto it = layouts.find(layout.handle);
    return it != layouts.end() && slot < it->second.size() ? &it->second[slot] : nullptr;
}

uint32_t descriptor_tracking::table_size(pipeline_layout layout, uint32_t slot) const {
    std::shared_lock lock(mutex);
    const auto* table = find_table(layout, slot);
    return table ? table->size : 0;
}

uint32_t descriptor_tracking::dynamic_offset_count(pipeline_layout layout, uint32_t slot) const {
    std::shared_lock lock(mutex);
    const auto* table = find_table(layout, slot);
    return table ? table->dynamic_count : 0;
}

static bool is_dynamic(descriptor_type type) {
    return type == descriptor_type::constant_buffer_with_dynamic_offset || type == descriptor_type::shader_storage_buffer_with_dynamic_offset;
}

descriptor_tracking::descriptor_data descriptor_tracking::lookup(device* device, pipeline_layout layout, uint32_t slot,
    descriptor_table table, uint32_t index, const std::vector<uint32_t>& dynamic_offsets) const {
    std::shared_lock lock(mutex);
    const auto* info = find_table(layout, slot);
    if (info == nullptr || table == 0)
        return {};
    for (const auto& entry : info->ranges) {
        if (index < entry.first || index - entry.first >= entry.range.count)
            continue;
        descriptor_heap heap{};
        uint32_t base = 0;
        device->get_descriptor_heap_offset(table, entry.range.binding, 0, &heap, &base);
        const auto pool = heaps.find(heap.handle);
        if (pool == heaps.end())
            break;
        const uint32_t element = index - entry.first;
        const auto it = pool->second.find(base + element);
        if (it == pool->second.end())
            break;
        auto result = it->second;
        if (is_dynamic(entry.range.type) && entry.first_dynamic_offset + element < dynamic_offsets.size())
            result.constant.offset += dynamic_offsets[entry.first_dynamic_offset + element];
        return result;
    }
    return {};
}

static void on_init_device(device* device) {
    device->create_private_data<descriptor_tracking>();
}
static void on_destroy_device(device* device) {
    device->destroy_private_data<descriptor_tracking>();
}

void descriptor_tracking::on_init_pipeline_layout(device* device, uint32_t count, const pipeline_layout_param* params, pipeline_layout layout) {
    descriptor_tracking& ctx = (*device->get_private_data<descriptor_tracking>());
    std::vector<table_info> tables(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& param = params[i];
        auto& info = tables[i];
        if (param.type == pipeline_layout_param_type::descriptor_table) {
            for (uint32_t j = 0; j < param.descriptor_table.count; ++j)
                info.ranges.push_back({param.descriptor_table.ranges[j]});
        } else if (param.type == pipeline_layout_param_type::descriptor_table_with_flags) {
            for (uint32_t j = 0; j < param.descriptor_table_with_flags.count; ++j)
                info.ranges.push_back({param.descriptor_table_with_flags.ranges[j]});
        }
        std::sort(info.ranges.begin(), info.ranges.end(), [](const range_info& a, const range_info& b) { return a.range.binding < b.range.binding; });
        for (auto& entry : info.ranges) {
            entry.first = std::max(info.size, entry.range.binding);
            entry.first_dynamic_offset = info.dynamic_count;
            if (entry.range.count == UINT32_MAX || entry.range.count > INT32_MAX - std::min<uint32_t>(entry.first, INT32_MAX)) {
                entry.range.count = 0;
                continue;
            }
            info.size = entry.first + entry.range.count;
            if (is_dynamic(entry.range.type))
                info.dynamic_count += entry.range.count;
        }
    }
    std::unique_lock lock(ctx.mutex);
    ctx.layouts.insert_or_assign(layout.handle, std::move(tables));
}
void descriptor_tracking::on_destroy_pipeline_layout(device* device, pipeline_layout layout) {
    descriptor_tracking& ctx = (*device->get_private_data<descriptor_tracking>());
    std::unique_lock lock(ctx.mutex);
    ctx.layouts.erase(layout.handle);
}

bool descriptor_tracking::on_copy_descriptor_tables(device* device, uint32_t count, const descriptor_table_copy* copies) {
    descriptor_tracking& ctx = (*device->get_private_data<descriptor_tracking>());

    for (uint32_t i = 0; i < count; ++i) {
        const descriptor_table_copy& copy = copies[i];

        uint32_t src_offset;
        descriptor_heap src_heap;
        device->get_descriptor_heap_offset(copy.source_table, copy.source_binding, copy.source_array_offset, &src_heap, &src_offset);

        uint32_t dst_offset;
        descriptor_heap dst_heap;
        device->get_descriptor_heap_offset(copy.dest_table, copy.dest_binding, copy.dest_array_offset, &dst_heap, &dst_offset);

        if (src_heap == 0 || dst_heap == 0)
            continue;
        std::unique_lock lock(ctx.mutex);
        auto& destination = ctx.heaps[dst_heap.handle];
        const auto source = ctx.heaps.find(src_heap.handle);
        for (uint32_t k = 0; k < copy.count; ++k) {
            if (source != ctx.heaps.end()) {
                const auto descriptor = source->second.find(src_offset + k);
                if (descriptor != source->second.end()) {
                    auto value = descriptor->second;
                    destination[dst_offset + k] = value;
                    continue;
                }
            }
            destination.erase(dst_offset + k);
        }
    }

    return false;
}

bool descriptor_tracking::on_update_descriptor_tables(device* device, uint32_t count, const descriptor_table_update* updates) {
    descriptor_tracking& ctx = (*device->get_private_data<descriptor_tracking>());

    for (uint32_t i = 0; i < count; ++i) {
        const descriptor_table_update& update = updates[i];

        uint32_t offset;
        descriptor_heap heap;
        device->get_descriptor_heap_offset(update.table, update.binding, update.array_offset, &heap, &offset);

        if (heap == 0)
            continue;
        std::unique_lock lock(ctx.mutex);
        auto& descriptors = ctx.heaps[heap.handle];
        for (uint32_t k = 0; k < update.count; ++k) {
            auto& descriptor = descriptors[offset + k];
            descriptor = {};
            descriptor.type = update.type;

            switch (update.type) {
                case descriptor_type::sampler:
                    descriptor.sampler = static_cast<const sampler*>(update.descriptors)[k];
                    break;
                case descriptor_type::sampler_with_resource_view:
                    descriptor.sampler_and_view = static_cast<const sampler_with_resource_view*>(update.descriptors)[k];
                    descriptor.view = descriptor.sampler_and_view.view;
                    descriptor.sampler = descriptor.sampler_and_view.sampler;
                    break;
                case descriptor_type::shader_resource_view:
                case descriptor_type::unordered_access_view:
                    descriptor.view = static_cast<const resource_view*>(update.descriptors)[k];
                    break;
                case descriptor_type::constant_buffer:
                case descriptor_type::constant_buffer_with_dynamic_offset:
                case descriptor_type::shader_storage_buffer_with_dynamic_offset:
                case descriptor_type::shader_storage_buffer:
                    descriptor.constant = static_cast<const buffer_range*>(update.descriptors)[k];
            }
        }
    }

    return false;
}

void descriptor_tracking::register_events(bool track_descriptors) {
    reshade::register_event<reshade::addon_event::init_device>(on_init_device);
    reshade::register_event<reshade::addon_event::destroy_device>(on_destroy_device);
    reshade::register_event<reshade::addon_event::init_pipeline_layout>(on_init_pipeline_layout);
    reshade::register_event<reshade::addon_event::destroy_pipeline_layout>(on_destroy_pipeline_layout);

    if (track_descriptors) {
        reshade::register_event<reshade::addon_event::copy_descriptor_tables>(on_copy_descriptor_tables);
        reshade::register_event<reshade::addon_event::update_descriptor_tables>(on_update_descriptor_tables);
    }
}
void descriptor_tracking::unregister_events(bool track_descriptors) {
    reshade::unregister_event<reshade::addon_event::init_device>(on_init_device);
    reshade::unregister_event<reshade::addon_event::destroy_device>(on_destroy_device);
    reshade::unregister_event<reshade::addon_event::init_pipeline_layout>(on_init_pipeline_layout);
    reshade::unregister_event<reshade::addon_event::destroy_pipeline_layout>(on_destroy_pipeline_layout);

    if (track_descriptors) {
        reshade::unregister_event<reshade::addon_event::copy_descriptor_tables>(on_copy_descriptor_tables);
        reshade::unregister_event<reshade::addon_event::update_descriptor_tables>(on_update_descriptor_tables);
    }
}