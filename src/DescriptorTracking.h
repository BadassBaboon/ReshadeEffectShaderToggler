/*
 * Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#pragma once

#include "reshade.hpp"
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

/// <summary>
/// An instance of this is automatically created for all devices and can be queried with <c>device->get_private_data&lt;descriptor_tracking&gt;()</c> (assuming
/// descriptor tracking was registered via <see cref="descriptor_tracking::register_events"/>).
/// </summary>
class __declspec(uuid("33319e83-387c-448e-881c-7e68fc2e52c4")) descriptor_tracking {
  public:
    enum class descriptor_data_type : uint32_t {
        sampler = 0,
        sampler_with_resource_view = 1,
        shader_resource_view = 2,
        unordered_access_view = 3,
        constant_buffer = 6,
        shader_storage_buffer = 7,
        push_constant = 8
    };

    struct descriptor_data {
        reshade::api::descriptor_type type;
        reshade::api::sampler_with_resource_view sampler_and_view;
        reshade::api::sampler sampler;
        reshade::api::resource_view view;
        reshade::api::buffer_range constant;
    };

    /// <summary>
    /// Registers all the necessary add-on events for descriptor tracking to work.
    /// </summary>
    static void register_events(bool track_descriptors = true);
    /// <summary>
    /// Unregisters all the necessary add-on events for descriptor tracking to work.
    /// </summary>
    static void unregister_events(bool track_descriptors = true);

    descriptor_data lookup(reshade::api::device* device, reshade::api::pipeline_layout layout, uint32_t slot,
                           reshade::api::descriptor_table table, uint32_t index, const std::vector<uint32_t>& dynamic_offsets) const;
    uint32_t table_size(reshade::api::pipeline_layout layout, uint32_t slot) const;
    uint32_t dynamic_offset_count(reshade::api::pipeline_layout layout, uint32_t slot) const;

  private:
    struct range_info {
        reshade::api::descriptor_range range;
        uint32_t first;
        uint32_t first_dynamic_offset;
    };
    struct table_info {
        std::vector<range_info> ranges;
        uint32_t size = 0;
        uint32_t dynamic_count = 0;
    };
    const table_info* find_table(reshade::api::pipeline_layout layout, uint32_t slot) const;

    static void on_init_pipeline_layout(reshade::api::device* device,
                                        uint32_t count,
                                        const reshade::api::pipeline_layout_param* params,
                                        reshade::api::pipeline_layout layout);
    static void on_destroy_pipeline_layout(reshade::api::device* device, reshade::api::pipeline_layout layout);

    static bool on_copy_descriptor_tables(reshade::api::device* device, uint32_t count, const reshade::api::descriptor_table_copy* copies);
    static bool on_update_descriptor_tables(reshade::api::device* device, uint32_t count, const reshade::api::descriptor_table_update* updates);

    mutable std::shared_mutex mutex;
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, descriptor_data>> heaps;
    std::unordered_map<uint64_t, std::vector<table_info>> layouts;
};
