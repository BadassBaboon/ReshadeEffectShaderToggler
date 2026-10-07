///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for ReShade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
//
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////

#pragma once
#include "ConstantManager.h"
#include "GamepadMonitor.h"
#include "KeyData.h"
#include "RenderingManager.h"
#include "ResourceManager.h"
#include "version.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <format>
#include <imgui.h>
#include <ranges>
#include <reshade.hpp>
#include <string>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

#define MAX_DESCRIPTOR_INDEX 10

static const char* const REST_DISCORD_URL = "https://discord.gg/qRdVSkUW6n";

static ImVec4 rfx_col(ImGuiCol idx) { return ImGui::GetStyle().Colors[idx]; }
static ImU32 rfx_u32(ImVec4 c, float alpha_mul = 1.0f) {
    auto b = [](float v) { return static_cast<ImU32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (b(c.w * alpha_mul * ImGui::GetStyle().Alpha) << 24);
}
static ImU32 rfx_u32(ImGuiCol idx, float alpha_mul = 1.0f) { return rfx_u32(rfx_col(idx), alpha_mul); }
static bool rfx_light_theme() {
    const ImVec4 c = rfx_col(ImGuiCol_WindowBg);
    return c.x * 0.299f + c.y * 0.587f + c.z * 0.114f > 0.5f;
}
static ImVec4 rfx_accent() { return rfx_col(ImGuiCol_CheckMark); }
static ImVec4 rfx_ok()   { return rfx_light_theme() ? ImVec4(0.10f, 0.48f, 0.22f, 1.0f) : ImVec4(0.35f, 0.85f, 0.50f, 1.0f); }
static ImVec4 rfx_warn() { return rfx_light_theme() ? ImVec4(0.66f, 0.40f, 0.00f, 1.0f) : ImVec4(1.00f, 0.75f, 0.35f, 1.0f); }
static ImVec4 rfx_bad()  { return rfx_light_theme() ? ImVec4(0.75f, 0.12f, 0.12f, 1.0f) : ImVec4(1.00f, 0.40f, 0.40f, 1.0f); }

struct RfxThemeScope {
    int color_count = 0;
    int var_count = 0;

    RfxThemeScope() {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,     5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,      6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,     6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,      ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,       ImVec2(8.0f, 6.0f));
        var_count = 5;
    }

    ~RfxThemeScope() {
        ImGui::PopStyleVar(var_count);
        ImGui::PopStyleColor(color_count);
    }
};

static constexpr float RFX_CARD_PAD = 16.0f;

static bool BeginCard(const char* str_id, const char* title = nullptr) {
    ImGui::PushID(str_id);
    ImGui::Spacing();

    ImVec2 p_start = ImGui::GetCursorScreenPos();
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGuiID height_id = ImGui::GetID("##card_h");
    float prev_h = ImGui::GetStateStorage()->GetFloat(height_id, 0.0f);

    if (prev_h > 0.0f) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p_end(p_start.x + avail_w, p_start.y + prev_h);
        dl->AddRectFilled(p_start, p_end, rfx_u32(ImGuiCol_FrameBg, 0.5f), 6.0f);
        dl->AddRect(p_start, p_end, rfx_u32(ImGuiCol_Border), 6.0f, 0, 1.0f);
    }

    ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##card_top_y"), p_start.y);

    ImGui::Dummy(ImVec2(0, 8.0f));
    // Wrap all plain text inside the card at the card's inner right edge so long
    // labels and status lines never run past the border.
    const float wrap_local_x = ImGui::GetCursorPosX() + avail_w - RFX_CARD_PAD;
    ImGui::Indent(RFX_CARD_PAD);
    ImGui::PushTextWrapPos(wrap_local_x);
    if (title != nullptr && title[0] != '\0') {
        ImGui::TextColored(rfx_accent(), "%s", title);
        ImGui::Dummy(ImVec2(0, 4.0f));
    }
    return true;
}

static void EndCard() {
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 8.0f));
    ImGui::Unindent(RFX_CARD_PAD);

    float top_y = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##card_top_y"), 0.0f);
    float current_y = ImGui::GetCursorScreenPos().y;
    float card_h = current_y - top_y;
    if (card_h > 0.0f) {
        ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##card_h"), card_h);
    }

    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 4.0f));
}

static bool DrawToggleSwitch(const char* id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float height = ImGui::GetFrameHeight() * 0.70f;
    float width = height * 1.85f;
    float radius = height * 0.5f;

    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool clicked = ImGui::IsItemClicked();
    if (clicked) *v = !(*v);

    float t = *v ? 1.0f : 0.0f;
    ImU32 col_bg;
    if (*v) {
        ImVec4 on = rfx_accent();
        if (ImGui::IsItemHovered()) on.w *= 0.85f;
        col_bg = rfx_u32(on);
    } else {
        col_bg = rfx_u32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    }

    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, radius);
    float circle_x = p.x + radius + t * (width - 2.0f * radius);
    draw_list->AddCircleFilled(ImVec2(circle_x, p.y + radius), radius - 1.5f,
                               rfx_u32(*v ? ImGuiCol_WindowBg : ImGuiCol_TextDisabled));
    return clicked;
}

static bool DrawTableToggleSwitch(const char* id, bool* v, float rightPad = 8.0f) {
    float avail = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight() * 0.70f;
    float switch_w = height * 1.85f;
    if (avail > switch_w + rightPad) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - switch_w - rightPad);
    }
    return DrawToggleSwitch(id, v);
}

template <typename Getter, typename Setter>
static void DrawTableStepper(const char* id_suffix, Getter getVal, Setter setVal, uint32_t minVal = 0, float rightPad = 8.0f) {
    char valBuf[16];
    uint32_t val = getVal();
    snprintf(valBuf, sizeof(valBuf), "%u", val);
    const float textW = ImGui::CalcTextSize(valBuf).x;
    const float btnW = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float totalW = textW + btnW * 2.0f + spacing * 2.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > totalW + rightPad) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - totalW - rightPad);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(valBuf);
    ImGui::SameLine();
    if (val <= minVal) ImGui::BeginDisabled();
    char decId[32];
    snprintf(decId, sizeof(decId), "-##dec_%s", id_suffix);
    if (ImGui::Button(decId, ImVec2(btnW, 0))) {
        setVal(val - 1);
    }
    if (val <= minVal) ImGui::EndDisabled();
    ImGui::SameLine();
    char incId[32];
    snprintf(incId, sizeof(incId), "+##inc_%s", id_suffix);
    if (ImGui::Button(incId, ImVec2(btnW, 0))) {
        setVal(val + 1);
    }
}

// Places the next control at local x `ctrl_x` on the same line as the previous item when it
// clears that item; otherwise lets it flow onto the next line instead of overlapping.
static void SameLineIfFits(float ctrl_x) {
    const float prev_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX() +
                           ImGui::GetStyle().ItemSpacing.x;
    if (ctrl_x >= prev_end) {
        ImGui::SameLine(ctrl_x);
    }
}

static bool DrawToggleRow(const char* label, bool* v, const char* tooltip = nullptr, const char* subtext = nullptr, float rightPad = RFX_CARD_PAD) {
    ImGui::PushID(label);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float switch_w = ImGui::GetFrameHeight() * 0.70f * 1.85f;
    const float switch_x = ImGui::GetCursorPosX() + avail - switch_w - rightPad;
    // Keep the label column clear of the switch so long labels wrap instead of overlapping it.
    const float label_wrap_x = switch_x - ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const bool wrap_label = label_wrap_x > ImGui::GetCursorPosX() + 60.0f;
    if (wrap_label) ImGui::PushTextWrapPos(label_wrap_x);
    ImGui::BeginGroup();
    ImGui::Text("%s", label);
    if (subtext != nullptr && subtext[0] != '\0') {
        ImGui::TextDisabled("%s", subtext);
    }
    ImGui::EndGroup();
    if (wrap_label) ImGui::PopTextWrapPos();

    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);

    if (switch_x > ImGui::GetCursorPosX()) {
        ImGui::SameLine(switch_x);
    } else {
        ImGui::SameLine();
    }
    bool changed = DrawToggleSwitch("##sw", v);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    ImGui::Spacing();
    return changed;
}

#include "AddonUIConstants.h"

enum RestCategory {
    CAT_GROUPS = 0,
    CAT_KEYBINDINGS,
    CAT_OPTIONS,
    CAT_COUNT
};

static const char* const s_restCategoryNames[CAT_COUNT] = {
    "Toggle Groups",
    "Keybindings",
    "Options"
};

static bool DrawSidebarCategoryButton(int category_id, const char* label, bool selected) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeight() + 10.0f;

    ImGui::PushStyleColor(ImGuiCol_Button, selected ? rfx_col(ImGuiCol_Header) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rfx_col(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rfx_col(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);

    ImGui::PushID(category_id);
    bool clicked = ImGui::Button("##cat", ImVec2(width, height));
    ImGui::PopID();

    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    float cy = p.y + height * 0.5f;
    float cx = p.x + 18.0f;
    ImU32 icon_col = rfx_u32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);

    switch (category_id) {
    case CAT_GROUPS: {
        draw->AddRect(ImVec2(cx - 6, cy - 5), ImVec2(cx + 3, cy + 1), icon_col, 1.0f, 0, 1.5f);
        draw->AddRect(ImVec2(cx - 2, cy - 1), ImVec2(cx + 6, cy + 5), icon_col, 1.0f, 0, 1.5f);
        break;
    }
    case CAT_KEYBINDINGS: {
        draw->AddRect(ImVec2(cx - 6, cy - 4), ImVec2(cx + 6, cy + 4), icon_col, 1.5f, 0, 1.5f);
        draw->AddLine(ImVec2(cx - 3, cy - 1), ImVec2(cx - 3, cy + 1), icon_col, 1.5f);
        draw->AddLine(ImVec2(cx + 3, cy - 1), ImVec2(cx + 3, cy + 1), icon_col, 1.5f);
        break;
    }
    case CAT_OPTIONS: {
        draw->AddLine(ImVec2(cx - 6, cy - 3), ImVec2(cx + 6, cy - 3), icon_col, 1.5f);
        draw->AddCircleFilled(ImVec2(cx - 2, cy - 3), 2.2f, icon_col);
        draw->AddLine(ImVec2(cx - 6, cy + 3), ImVec2(cx + 6, cy + 3), icon_col, 1.5f);
        draw->AddCircleFilled(ImVec2(cx + 2, cy + 3), 2.2f, icon_col);
        break;
    }
    default:
        break;
    }

    {
        const ImU32 text_col = rfx_u32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        draw->AddText(ImVec2(p.x + 34.0f, cy - ImGui::GetTextLineHeight() * 0.5f), text_col, label);
    }

    if (selected) {
        float dot_r = 3.5f;
        ImVec2 dot_pos(p.x + width - 14.0f, cy);
        draw->AddCircleFilled(dot_pos, dot_r, rfx_u32(rfx_accent()));
    }

    return clicked;
}

// From ReShade, see https://github.com/crosire/reshade/blob/main/source/imgui_widgets.cpp
static bool key_input_box(const char* name, uint32_t* keys, const reshade::api::effect_runtime* runtime) {
    char buf[48];
    buf[0] = '\0';
    if (*keys)
        buf[ShaderToggler::reshade_key_name(*keys).copy(buf, sizeof(buf) - 1)] = '\0';

    ImGui::InputTextWithHint(name,
                             "Click to set keyboard shortcut",
                             buf,
                             sizeof(buf),
                             ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_NoHorizontalScroll);

    if (ImGui::IsItemActive()) {
        const uint32_t last_key_pressed = ShaderToggler::reshade_last_key_pressed(runtime);
        if (last_key_pressed != 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                *keys = 0;

            } else if (last_key_pressed < 0x10 || last_key_pressed > 0x12) // Exclude modifier keys
            {
                *keys = last_key_pressed;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x11)) << 8;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x10)) << 16;
                *keys |= static_cast<uint32_t>(runtime->is_key_down(0x12)) << 24;
            }

            return true;
        }
    } else if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click in the field and press any key to change the shortcut to that key.");
    }

    return false;
}

static bool gamepad_input_box(const char* name, uint32_t* shortcut) {
    auto& gpMonitor = ShaderToggler::GamepadMonitor::getInstance();
    const bool isConnected = gpMonitor.isConnected();

    static ImGuiID s_activeGamepadWidget = 0;
    static uint32_t s_accumulatedCombo = 0;

    const ImGuiID currentWidgetId = ImGui::GetID(name);
    char buf[64];
    buf[0] = '\0';
    if (s_activeGamepadWidget == currentWidgetId && s_accumulatedCombo != 0) {
        std::string str = ShaderToggler::GamepadMonitor::buttonsToString(s_accumulatedCombo);
        strncpy_s(buf, sizeof(buf), str.c_str(), sizeof(buf) - 1);
    } else if (*shortcut != 0) {
        std::string str = ShaderToggler::GamepadMonitor::buttonsToString(*shortcut);
        strncpy_s(buf, sizeof(buf), str.c_str(), sizeof(buf) - 1);
    }

    const char* hint = isConnected ? ((s_activeGamepadWidget == currentWidgetId) ? "Press buttons..." : "Click to set controller combo")
                                   : "No controller connected";

    ImGui::InputTextWithHint(name,
                             hint,
                             buf,
                             sizeof(buf),
                             ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_NoHorizontalScroll);

    bool valueChanged = false;

    if (ImGui::IsItemActive()) {
        if (s_activeGamepadWidget != currentWidgetId) {
            s_activeGamepadWidget = currentWidgetId;
            s_accumulatedCombo = 0;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            *shortcut = 0;
            s_accumulatedCombo = 0;
            valueChanged = true;
        } else if (isConnected) {
            const uint32_t cur = gpMonitor.getCurrentButtons();
            if (cur != 0) {
                s_accumulatedCombo |= cur;
            } else if (s_accumulatedCombo != 0) {
                *shortcut = s_accumulatedCombo;
                s_accumulatedCombo = 0;
                valueChanged = true;
            }
        }
    } else {
        if (s_activeGamepadWidget == currentWidgetId) {
            if (s_accumulatedCombo != 0) {
                *shortcut = s_accumulatedCombo;
                s_accumulatedCombo = 0;
                valueChanged = true;
            }
            s_activeGamepadWidget = 0;
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to focus, hold your controller button combination (e.g. LB + D-Pad Down), then release.\nPress Backspace or Delete to clear.\nSupported: D-Pad, A/B/X/Y, LB/RB, LT/RT, Start, Back, Thumb clicks.");
    }

    return valueChanged;
}

static constexpr const char* invocationDescription[] = { "BEFORE DRAW", "AFTER DRAW", "ON RENDER TARGET CHANGE" };

static void DisplayIsPartOfToggleGroup() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
    ImGui::SameLine();
    ImGui::Text(" Shader is part of this toggle group.");
    ImGui::PopStyleColor();
}

static void DisplayTechniqueSelection(reshade::api::effect_runtime* runtime,
                                      AddonImGui::AddonUIData& instance,
                                      ShaderToggler::ToggleGroup* group) {
    if (group == nullptr)
        return;

    RuntimeDataContainer& runtimeData = runtime->get_private_data<RuntimeDataContainer>();
    static char searchBuf[256] = "\0";

    bool allowAll = group->getAllowAllTechniques();
    bool exceptions = group->getHasTechniqueExceptions();
    const bool oldAllowAll = allowAll;
    const bool oldExceptions = exceptions;
    bool selectionChanged = false;

    size_t availableCount = 0;
    {
        std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
        availableCount = runtimeData.techniqueUiCache.size();
    }

    if (BeginCard("##tech_mode_card", "TECHNIQUE ROUTING MODE")) {
        const float labelColWidth = std::max(220.0f, ImGui::CalcTextSize("Except for selected techniques   ").x);
        if (ImGui::BeginTable("TechniqueModeTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody, ImVec2(-RFX_CARD_PAD, 0.0f))) {
            ImGui::TableSetupColumn("##col_label", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
            ImGui::TableSetupColumn("##col_ctrl", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Apply all enabled techniques");
            ImGui::TableNextColumn();
            if (DrawTableToggleSwitch("##ApplyAllTechniques", &allowAll)) {
                selectionChanged = true;
            }

            if (allowAll) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Except for selected techniques");
                ImGui::TableNextColumn();
                if (DrawTableToggleSwitch("##TechExceptions", &exceptions)) {
                    selectionChanged = true;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Current Routing Mode");
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const char* modeDesc = nullptr;
            const char* modeName = nullptr;
            ImVec4 modeCol;
            if (!allowAll) {
                modeName = "INCLUSION LIST";
                modeCol = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
                modeDesc = "Only checked techniques trigger for this group.";
            } else if (exceptions) {
                modeName = "EXCLUSION LIST";
                modeCol = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
                modeDesc = "All active techniques trigger EXCEPT checked ones.";
            } else {
                modeName = "ALL ACTIVE TECHNIQUES (Passive)";
                modeCol = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
                modeDesc = "All globally enabled ReShade techniques apply.";
            }
            // Right-align with the switches above (same 8px pad as DrawTableToggleSwitch).
            const float modeW = ImGui::CalcTextSize(modeName).x;
            const float modeAvail = ImGui::GetContentRegionAvail().x;
            if (modeAvail > modeW + 8.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + modeAvail - modeW - 8.0f);
            }
            ImGui::TextColored(modeCol, "%s", modeName);

            ImGui::EndTable();
            // Description on its own full-width line so it wraps at the card edge instead of
            // being squeezed into the leftover space beside the mode name.
            ImGui::TextDisabled("%s", modeDesc);
        }
        EndCard();
    }

    ImGui::Spacing();

    if (BeginCard("##tech_list_card", "AVAILABLE TECHNIQUES")) {
        // Search bar
        ImGui::SetNextItemWidth(-RFX_CARD_PAD);
        ImGui::InputTextWithHint("##techniqueSearch", "Search techniques by name...", searchBuf, IM_ARRAYSIZE(searchBuf));

        ImGui::Spacing();

        const bool canEditCheckboxes = !allowAll || exceptions;
        if (!canEditCheckboxes) {
            ImGui::BeginDisabled();
        }

        const float selectAllW = ImGui::CalcTextSize("Select All").x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
        const float untickAllW = ImGui::CalcTextSize("Untick All").x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
        if (ImGui::Button("Select All", ImVec2(selectAllW, 0))) {
            std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
            auto updated = group->preferredTechniques();
            std::string searchUpper(searchBuf);
            std::transform(searchUpper.begin(), searchUpper.end(), searchUpper.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            for (const auto& entry : runtimeData.techniqueUiCache) {
                if (searchUpper.empty() || entry.upperName.find(searchUpper) != std::string::npos) {
                    updated.insert(entry.name);
                }
            }
            group->setPreferredTechniques(updated);
            selectionChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Untick All", ImVec2(untickAllW, 0))) {
            if (searchBuf[0] == '\0') {
                const std::unordered_set<std::string> empty;
                group->setPreferredTechniques(empty);
            } else {
                std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
                auto updated = group->preferredTechniques();
                std::string searchUpper(searchBuf);
                std::transform(searchUpper.begin(), searchUpper.end(), searchUpper.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                for (const auto& entry : runtimeData.techniqueUiCache) {
                    if (entry.upperName.find(searchUpper) != std::string::npos) {
                        updated.erase(entry.name);
                    }
                }
                group->setPreferredTechniques(updated);
            }
            selectionChanged = true;
        }

        char techCountBuf[64];
        snprintf(techCountBuf, sizeof(techCountBuf), "%zu selected / %zu available", group->preferredTechniques().size(), availableCount);
        // Only share the button row when the whole counter fits; otherwise give it its own line.
        const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - RFX_CARD_PAD;
        const float prevEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX() + ImGui::GetStyle().ItemSpacing.x;
        if (prevEnd + ImGui::CalcTextSize(techCountBuf).x <= rowRight) {
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", techCountBuf);

        ImGui::Separator();

        const float tableH = std::max(120.0f, ImGui::GetContentRegionAvail().y - 8.0f);
        const int numCols = 1;

        std::string searchUpper(searchBuf);
        std::transform(searchUpper.begin(), searchUpper.end(), searchUpper.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

        {
            std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
            if (runtimeData.techniqueUiCache.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No ReShade techniques detected. Make sure your effects are loaded.");
            } else {
                if (ImGui::BeginTable("TechniqueSelectionTable", numCols,
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV,
                                      ImVec2(-RFX_CARD_PAD, tableH))) {
                    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);

                    int matchCount = 0;
                    for (const auto& entry : runtimeData.techniqueUiCache) {
                        if (!searchUpper.empty() && entry.upperName.find(searchUpper) == std::string::npos)
                            continue;

                        matchCount++;
                        ImGui::TableNextColumn();
                        bool enabled = group->preferredTechniques().contains(entry.name);
                        ImGui::PushID(entry.name.c_str());
                        if (ImGui::Checkbox(entry.name.c_str(), &enabled)) {
                            auto updated = group->preferredTechniques();
                            if (enabled)
                                updated.insert(entry.name);
                            else
                                updated.erase(entry.name);
                            group->setPreferredTechniques(updated);
                            selectionChanged = true;
                        }

                        if (entry.effect != nullptr && !entry.effect->enabled) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(off in ReShade)");
                        }
                        ImGui::PopID();
                    }

                    if (matchCount == 0 && !searchUpper.empty()) {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("No techniques matching \"%s\"", searchBuf);
                    }

                    ImGui::EndTable();
                }
            }
        }

        if (!canEditCheckboxes) {
            ImGui::EndDisabled();
        }

        EndCard();
    }

    if (allowAll != oldAllowAll || exceptions != oldExceptions) {
        group->setHasTechniqueExceptions(exceptions);
        group->setAllowAllTechniques(allowAll);
        selectionChanged = true;
    }

    if (selectionChanged) {
        std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
        instance.AssignPreferredGroupTechniques(runtimeData.allTechniques);
    }
}

static void DrawPreview(unsigned long long textureId,
                        uint32_t srcWidth,
                        uint32_t srcHeight,
                        ImVec4 tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const ImVec2 origin = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width = std::max(1.0f, avail.x);
    const float height = std::max(1.0f, avail.y);

    // Unknown source size (0) would make the ratio NaN and the image invisible; fill the pane instead.
    float new_width = srcWidth > 0 ? static_cast<float>(srcWidth) : width;
    float new_height = srcHeight > 0 ? static_cast<float>(srcHeight) : height;

    const float ratio = std::min(width / new_width, height / new_height);
    new_width = std::floor(new_width * ratio);
    new_height = std::floor(new_height * ratio);

    ImGui::SetCursorPos(ImVec2(origin.x + (width - new_width) * 0.5f, origin.y + (height - new_height) * 0.5f));

    ImGui::Image(textureId, ImVec2(new_width, new_height), ImVec2(0, 0), ImVec2(1, 1), tint);

    ImGui::PopStyleVar();
}

static void DisplayPreview(AddonImGui::AddonUIData& instance,
                           Rendering::ResourceManager& resManager,
                           reshade::api::effect_runtime* runtime,
                           ShaderToggler::ToggleGroup* group,
                           float width = 0) {
    DeviceDataContainer& deviceData = runtime->get_device()->get_private_data<DeviceDataContainer>();
    reshade::api::resource_view srv = reshade::api::resource_view{ 0 };
    resManager.SetPongPreviewHandles(runtime->get_device(), nullptr, nullptr, &srv);
    bool clearAlpha = group->getClearPreviewAlpha();
    const bool vulkan = runtime->get_device()->get_api() == reshade::api::device_api::vulkan;

    if (vulkan)
        ImGui::BeginDisabled();
    DrawToggleRow("Clear alpha channel", &clearAlpha,
                  vulkan ? "Vulkan safe preview currently copies the source image directly." : "Clear alpha channel before sampling preview texture",
                  nullptr, RFX_CARD_PAD);
    if (vulkan)
        ImGui::EndDisabled();

    if (!deviceData.huntPreview.status.empty()) {
        ImGui::TextDisabled("%s", deviceData.huntPreview.status.c_str());
    }

    if (deviceData.huntPreview.target != 0) {
        const char* stageName = deviceData.huntPreview.hunted_stage == 0 ? "PS" :
                                deviceData.huntPreview.hunted_stage == 1 ? "VS" : "CS";
        ImGui::Text("Shader: 0x%08x (%s)   Target: %ux%u", deviceData.huntPreview.hunted_shader_hash, stageName,
                    deviceData.huntPreview.width, deviceData.huntPreview.height);
        ImGui::Text("Format: %s   Address: 0x%llx", Rendering::RenderingManager::FormatName(deviceData.huntPreview.format).c_str(),
                    static_cast<unsigned long long>(deviceData.huntPreview.target.handle));
        ImGui::Separator();
    }

    int& previewChannel = instance.GetHuntingUIState().previewChannel;
    if (srv != 0 && deviceData.huntPreview.matched) {
        ImGui::TextDisabled("View:");
        ImGui::SameLine();
        ImGui::RadioButton("RGB", &previewChannel, 0);
        ImGui::SameLine();
        ImGui::RadioButton("R", &previewChannel, 1);
        ImGui::SameLine();
        ImGui::RadioButton("G", &previewChannel, 2);
        ImGui::SameLine();
        ImGui::RadioButton("B", &previewChannel, 3);

        const ImVec4 previewTint =
          previewChannel == 1 ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) :
          previewChannel == 2 ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) :
          previewChannel == 3 ? ImVec4(0.0f, 0.0f, 1.0f, 1.0f) :
                                ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

        const float availPreviewW = std::max(200.0f, ImGui::GetContentRegionAvail().x - RFX_CARD_PAD);
        float desiredPreviewH = 340.0f;
        if (deviceData.huntPreview.width > 0 && deviceData.huntPreview.height > 0) {
            const float aspect = static_cast<float>(deviceData.huntPreview.height) / static_cast<float>(deviceData.huntPreview.width);
            desiredPreviewH = std::clamp(availPreviewW * aspect, 280.0f, 540.0f);
        }

        if (ImGui::BeginChild("RTPreview##preview", ImVec2(-RFX_CARD_PAD, desiredPreviewH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            DrawPreview(srv.handle, deviceData.huntPreview.width, deviceData.huntPreview.height, previewTint);
        }
        ImGui::EndChild();
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled(vulkan ?
            (deviceData.huntPreview.status.empty() ? "Select a shader while hunting to capture a Vulkan preview." : deviceData.huntPreview.status.c_str()) :
            "Select a shader in the hunting list to capture a live render target preview.");
    }

    group->setClearPreviewAlpha(clearAlpha);
}

static void DisplayBindingPreview(AddonImGui::AddonUIData& instance,
                                  Rendering::ResourceManager& resManager,
                                  reshade::api::effect_runtime* runtime,
                                  ShaderToggler::ToggleGroup* group) {
    ShaderToggler::GroupResource& groupResource = group->GetGroupResource(ShaderToggler::GroupResourceType::RESOURCE_BINDING);

    reshade::api::resource_view res_view = { 0 };
    if (groupResource.owning) {
        res_view = groupResource.srv;
    } else if (groupResource.g_res != nullptr) {
        res_view = groupResource.g_res->srv;
    }

    if (res_view != 0) {
        ImGui::Text("Format: %u | Width: %u | Height: %u",
                    static_cast<uint32_t>(groupResource.target_description.texture.format),
                    groupResource.target_description.texture.width,
                    groupResource.target_description.texture.height);
        ImGui::Separator();

        const float availPreviewW = std::max(200.0f, ImGui::GetContentRegionAvail().x - RFX_CARD_PAD);
        float desiredPreviewH = 340.0f;
        if (groupResource.target_description.texture.width > 0 && groupResource.target_description.texture.height > 0) {
            const float aspect = static_cast<float>(groupResource.target_description.texture.height) / static_cast<float>(groupResource.target_description.texture.width);
            desiredPreviewH = std::clamp(availPreviewW * aspect, 280.0f, 540.0f);
        }

        if (ImGui::BeginChild("BindingPreview##preview", ImVec2(-RFX_CARD_PAD, desiredPreviewH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            DrawPreview(res_view.handle, groupResource.target_description.texture.width, groupResource.target_description.texture.height);
        }
        ImGui::EndChild();
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("No texture is currently captured or bound for this group.");
    }
}

static void DisplayRenderTargets(AddonImGui::AddonUIData& instance,
                                 Rendering::ResourceManager& resManager,
                                 reshade::api::effect_runtime* runtime,
                                 ShaderToggler::ToggleGroup* group) {
    const char* typeSelectedItem = invocationDescription[group->getInvocationLocation()];
    uint32_t selectedIndex = group->getInvocationLocation();

    const char* typeDestItems[] = { "Render target", "Shader Resource View" };
    uint32_t selectedDestIndex = group->getRenderToResourceViews() ? 1 : 0;
    const char* typeSelectedDestItem = typeDestItems[selectedDestIndex];

    static const char* stageItems[] = { "PIXEL", "VERTEX", "COMPUTE" };
    uint32_t selectedStageIndex = group->getRenderSRVShaderStage();
    const char* selectedStage = stageItems[selectedStageIndex];

    bool retry = group->getRequeueAfterRTMatchingFailure();
    bool tonemap = group->getToneMap();
    bool preserveAlpha = group->getPreserveAlpha();
    bool flipbuffer = group->getFlipBuffer();
    bool autoSceneColour = group->getAutoRenderSRV();

    static const char* swapchainMatchOptions[] = { "RESOLUTION", "ASPECT RATIO", "EXTENDED ASPECT RATIO", "NONE" };
    uint32_t selectedSwapchainMatchMode = group->getMatchSwapchainResolution();
    const char* typesSelectedSwapchainMatchMode = swapchainMatchOptions[selectedSwapchainMatchMode];

    const reshade::api::device_api deviceApi = runtime->get_device()->get_api();
    const bool autoSceneColourSupported = ShaderToggler::IsAutoSceneColourSupported(deviceApi);
    const bool supportsSRVwrite = deviceApi < reshade::api::device_api::d3d12;

    const bool autoSceneColourActive = autoSceneColour && autoSceneColourSupported;
    const bool pendingVulkanShaderEdits =
      deviceApi == reshade::api::device_api::vulkan &&
      instance.GetToggleGroupIdShaderEditing().load() == group->getId();

    if (BeginCard("##rt_config_card", "RENDER TARGET CONFIGURATION")) {
        const float labelColWidth = std::max(220.0f, ImGui::CalcTextSize("Preserve target alpha channel   ").x);
        if (ImGui::BeginTable("RenderTargetsSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody, ImVec2(-RFX_CARD_PAD, 0.0f))) {
            ImGui::TableSetupColumn("##RTcolumnsetup", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
            ImGui::TableSetupColumn("##RTcontrols", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Auto scene colour");
            ImGui::TableNextColumn();
            if (!autoSceneColourSupported)
                ImGui::BeginDisabled();
            DrawTableToggleSwitch("##AutoSceneColour", &autoSceneColour);
            if (!autoSceneColourSupported)
                ImGui::EndDisabled();
            if (!autoSceneColourSupported) {
                ImGui::TextDisabled("(D3D10/D3D11/D3D12/Vulkan only)");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Auto Scene Colour supports D3D10, D3D11, D3D12 and Vulkan. Vulkan native staging uses ReShade\'s generic image-blit path.");
            }

            ImGui::TableNextRow();

            if (autoSceneColourActive) {
                ImGui::TableNextColumn();
                ImGui::Text("Target");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Live render target (matched draw)");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Graphics API");
                ImGui::TableNextColumn();
                if (deviceApi == reshade::api::device_api::d3d10)
                    ImGui::TextUnformatted("D3D10");
                else if (deviceApi == reshade::api::device_api::d3d11)
                    ImGui::TextUnformatted("D3D11");
                else if (deviceApi == reshade::api::device_api::d3d12)
                    ImGui::TextUnformatted("D3D12");
                else
                    ImGui::TextUnformatted("Vulkan");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Current attempt");
                ImGui::TableNextColumn();
                if (pendingVulkanShaderEdits)
                    ImGui::TextUnformatted("Finish shader hunting (Done) to apply marks");
                else if (!group->getDebugAutoStatus().empty())
                    ImGui::TextUnformatted(group->getDebugAutoStatus().c_str());
                else
                    ImGui::TextUnformatted("Waiting for matching render target...");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Current target");
                ImGui::TableNextColumn();
                if (group->getDebugCurrentSceneWidth() > 0 && group->getDebugCurrentSceneHeight() > 0) {
                    ImGui::Text("%ux%u | %s | 0x%llx",
                                group->getDebugCurrentSceneWidth(),
                                group->getDebugCurrentSceneHeight(),
                                group->getDebugCurrentFormat().empty() ? "(format unknown)" : group->getDebugCurrentFormat().c_str(),
                                static_cast<unsigned long long>(group->getDebugCurrentTarget()));
                } else {
                    ImGui::TextUnformatted("(none)");
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Last successful injection");
                ImGui::TableNextColumn();
                if (group->getDebugEffectRenderCalls() > 0) {
                    ImGui::Text("%ux%u -> %ux%u",
                                group->getDebugSceneWidth(),
                                group->getDebugSceneHeight(),
                                group->getDebugEffectWidth(),
                                group->getDebugEffectHeight());
                } else {
                    ImGui::TextUnformatted("(none yet)");
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Last successful staging");
                ImGui::TableNextColumn();
                if (group->getDebugEffectRenderCalls() == 0) {
                    ImGui::TextUnformatted("(none)");
                } else if (group->getDebugNativeStaging() && deviceApi == reshade::api::device_api::vulkan) {
                    ImGui::TextUnformatted("Vulkan image blit");
                } else if (group->getDebugNativeStaging()) {
                    ImGui::TextUnformatted("Fullscreen shader copy");
                } else {
                    ImGui::TextUnformatted("Direct");
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Last successful techniques");
                ImGui::TableNextColumn();
                if (group->getDebugEffectRenderCalls() > 0)
                    ImGui::Text("%u | %s",
                                group->getDebugLastRenderedTechniqueCount(),
                                group->getDebugLastTechniqueOrder().empty() ? "(none)" : group->getDebugLastTechniqueOrder().c_str());
                else
                    ImGui::TextUnformatted("(none)");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Successful renders");
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(group->getDebugEffectRenderCalls()));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Diagnostics");
                ImGui::TableNextColumn();
                const float copyBtnW = ImGui::CalcTextSize("Copy diagnostics").x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
                if (ImGui::GetContentRegionAvail().x > copyBtnW)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - copyBtnW);
                if (ImGui::Button("Copy diagnostics", ImVec2(copyBtnW, 0))) {
                    const char* apiName = deviceApi == reshade::api::device_api::d3d10 ? "D3D10" :
                                          deviceApi == reshade::api::device_api::d3d11 ? "D3D11" :
                                          deviceApi == reshade::api::device_api::d3d12 ? "D3D12" : "Vulkan";
                    const std::string diagnostics = std::format(
                      "REST {}\nGroup: {}\nAPI: {}\nAuto Scene Colour: active\n\nCurrent candidate / latest attempt\nStatus: {}\nTarget: {}x{} | {} | 0x{:x}\n\nLast successful injection\nScene -> effect: {}x{} -> {}x{}\nStaging path: {}\nVulkan boundary: {}\nTechniques: {}\nTechnique order: {}\nSuccessful renders: {}\nLast successful target: 0x{:x}",
                      REST_VERSION_STRING,
                      group->getName(),
                      apiName,
                      group->getDebugAutoStatus().empty() ? "(none)" : group->getDebugAutoStatus(),
                      group->getDebugCurrentSceneWidth(),
                      group->getDebugCurrentSceneHeight(),
                      group->getDebugCurrentFormat().empty() ? "(unknown)" : group->getDebugCurrentFormat(),
                      static_cast<unsigned long long>(group->getDebugCurrentTarget()),
                      group->getDebugSceneWidth(),
                      group->getDebugSceneHeight(),
                      group->getDebugEffectWidth(),
                      group->getDebugEffectHeight(),
                      group->getDebugEffectRenderCalls() == 0 ? "(none)" :
                        (group->getDebugNativeStaging() ?
                          (deviceApi == reshade::api::device_api::vulkan ? "Vulkan image blit" : "fullscreen shader copy") :
                          "direct"),
                      deviceApi == reshade::api::device_api::vulkan ?
                        (group->getDebugLastVulkanBoundary().empty() ? "(none yet)" : group->getDebugLastVulkanBoundary()) :
                        "not applicable",
                      group->getDebugLastRenderedTechniqueCount(),
                      group->getDebugLastTechniqueOrder().empty() ? "(none)" : group->getDebugLastTechniqueOrder(),
                      static_cast<unsigned long long>(group->getDebugEffectRenderCalls()),
                      static_cast<unsigned long long>(group->getDebugLastRenderTarget()));
                    ImGui::SetClipboardText(diagnostics.c_str());
                }

                const auto recentCandidates = group->getDebugAutoHistory();
                if (!recentCandidates.empty() && ImGui::TreeNode("Recent candidates")) {
                    for (auto it = recentCandidates.rbegin(); it != recentCandidates.rend(); ++it) {
                        const auto& entry = *it;
                        if (entry.successfulRenders > 0) {
                            ImGui::Text("#%llu 0x%08x | %ux%u | %s | %llu renders",
                                        static_cast<unsigned long long>(entry.candidateId),
                                        entry.shaderHash,
                                        entry.sceneWidth,
                                        entry.sceneHeight,
                                        entry.status.empty() ? "(no status)" : entry.status.c_str(),
                                        static_cast<unsigned long long>(entry.successfulRenders));
                        } else {
                            ImGui::Text("#%llu 0x%08x | %ux%u | %s",
                                        static_cast<unsigned long long>(entry.candidateId),
                                        entry.shaderHash,
                                        entry.sceneWidth,
                                        entry.sceneHeight,
                                        entry.status.empty() ? "(no status)" : entry.status.c_str());
                        }

                        if (entry.target != 0) {
                            ImGui::TextDisabled("Target 0x%llx | %s%s%s",
                                                static_cast<unsigned long long>(entry.target),
                                                entry.format.empty() ? "(format unknown)" : entry.format.c_str(),
                                                entry.boundary.empty() ? "" : " | ",
                                                entry.boundary.empty() ? "" : entry.boundary.c_str());
                        }
                    }
                    ImGui::TreePop();
                }
            } else {
                if (supportsSRVwrite) {
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Render destination");
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##Renderdestination", typeSelectedDestItem, ImGuiComboFlags_None)) {
                        for (int n = 0; n < IM_ARRAYSIZE(typeDestItems); n++) {
                            const bool is_selected = (typeSelectedDestItem == typeDestItems[n]);
                            if (ImGui::Selectable(typeDestItems[n], is_selected)) {
                                typeSelectedDestItem = typeDestItems[n];
                                selectedDestIndex = n;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TableNextRow();
                } else {
                    selectedDestIndex = 0;
                }

                if (supportsSRVwrite && selectedDestIndex == 1) {
                    if (!instance.GetTrackDescriptors()) {
                        ImGui::BeginDisabled();
                        group->setRenderToResourceViews(false);
                    } else {
                        group->setRenderToResourceViews(true);
                    }

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Shader Stage");
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##RenderShaderStage", selectedStage, ImGuiComboFlags_None)) {
                        for (int n = 0; n < IM_ARRAYSIZE(stageItems); n++) {
                            const bool is_selected = (selectedStage == stageItems[n]);
                            if (ImGui::Selectable(stageItems[n], is_selected)) {
                                selectedStageIndex = n;
                                selectedStage = stageItems[n];
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    group->setRenderSRVShaderStage(selectedStageIndex);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Slot");
                    ImGui::TableNextColumn();
                    DrawTableStepper("srv_slot",
                        [&]() { return group->getRenderSRVSlotIndex(); },
                        [&](uint32_t val) { group->setRenderSRVSlotIndex(val); });

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Binding");
                    ImGui::TableNextColumn();
                    DrawTableStepper("srv_bind",
                        [&]() { return group->getRenderSRVDescriptorIndex(); },
                        [&](uint32_t val) { group->setRenderSRVDescriptorIndex(val); });

                    if (!instance.GetTrackDescriptors())
                        ImGui::EndDisabled();
                } else {
                    group->setRenderToResourceViews(false);

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Render target index");
                    ImGui::TableNextColumn();
                    DrawTableStepper("rt_idx",
                        [&]() { return group->getRenderTargetIndex(); },
                        [&](uint32_t val) { group->setRenderTargetIndex(val); });

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Invocation location");
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##Invocationlocation", typeSelectedItem, ImGuiComboFlags_None)) {
                        for (int n = 0; n < IM_ARRAYSIZE(invocationDescription); n++) {
                            const bool is_selected = (typeSelectedItem == invocationDescription[n]);
                            if (ImGui::Selectable(invocationDescription[n], is_selected)) {
                                typeSelectedItem = invocationDescription[n];
                                selectedIndex = n;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Retry RT assignment");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##RetryRTassignment", &retry);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Apply tone map clamping");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##tonemap", &tonemap);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Flip render target");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##flipbuffer", &flipbuffer);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Preserve target alpha channel");
            ImGui::TableNextColumn();
            if (autoSceneColourActive)
                ImGui::BeginDisabled();
            DrawTableToggleSwitch("##preserveAlpha", &preserveAlpha);
            if (autoSceneColourActive) {
                ImGui::EndDisabled();
                ImGui::TextDisabled("(ignored while Auto scene colour is active)");
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Match swapchain");
            ImGui::TableNextColumn();
            if (autoSceneColourActive) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("ASPECT RATIO (automatic)");
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##effSwapChainMatchMode", typesSelectedSwapchainMatchMode, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(swapchainMatchOptions); n++) {
                        const bool is_selected = (typesSelectedSwapchainMatchMode == swapchainMatchOptions[n]);
                        if (ImGui::Selectable(swapchainMatchOptions[n], is_selected)) {
                            typesSelectedSwapchainMatchMode = swapchainMatchOptions[n];
                            selectedSwapchainMatchMode = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::EndTable();
        }

        group->setRequeueAfterRTMatchingFailure(retry);
        group->setMatchSwapchainResolution(selectedSwapchainMatchMode);
        group->setAutoRenderSRV(autoSceneColour);
        group->setInvocationLocation(selectedIndex);
        group->setToneMap(tonemap);
        group->setPreserveAlpha(preserveAlpha);
        group->setFlipBuffer(flipbuffer);

        EndCard();
    }

    ImGui::Spacing();

    if (BeginCard("##rt_preview_card", "RENDER TARGET PREVIEW")) {
        DisplayPreview(instance, resManager, runtime, group);
        EndCard();
    }
}

static void DisplayTextureBindings(AddonImGui::AddonUIData& instance,
                                   ShaderToggler::ToggleGroup* group,
                                   reshade::api::effect_runtime* runtime,
                                   Rendering::ResourceManager& resManager) {
    const char* typeItems[] = { "Render target", "Shader Resource View" };
    uint32_t selectedIndex = group->getExtractResourceViews() ? 1 : 0;
    const char* typeSelectedItem = typeItems[selectedIndex];

    static const char* swapchainMatchOptions[] = { "RESOLUTION", "ASPECT RATIO", "EXTENDED ASPECT RATIO", "NONE" };
    uint32_t selectedSwapchainMatchMode = group->getBindingMatchSwapchainResolution();
    const char* typesSelectedSwapchainMatchMode = swapchainMatchOptions[selectedSwapchainMatchMode];

    static const char* stageItems[] = { "PIXEL", "VERTEX", "COMPUTE" };
    uint32_t selectedStageIndex = group->getSRVShaderStage();
    const char* selectedStage = stageItems[selectedStageIndex];

    char tmpBuffer[150] = {};
    bool isBindingEnabled = group->isProvidingTextureBinding();
    const std::string& bindingName = group->getTextureBindingName();
    strncpy_s(tmpBuffer, 150, bindingName.c_str(), bindingName.size());

    bool copyBinding = group->getCopyTextureBinding();
    bool clearBinding = group->getClearBindings();
    bool flipBinding = group->getFlipBufferBinding();

    if (BeginCard("##texture_config_card", "TEXTURE BINDING CONFIGURATION")) {
        const float labelColWidth = std::max(220.0f, ImGui::CalcTextSize("Create texture copy for binding   ").x);
        if (ImGui::BeginTable("Bindingsettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody, ImVec2(-RFX_CARD_PAD, 0.0f))) {
            ImGui::TableSetupColumn("##BindingColumnSetup", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
            ImGui::TableSetupColumn("##BindingControls", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Texture binding enabled");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##Texturebindingenabled", &isBindingEnabled);

            if (!isBindingEnabled) {
                ImGui::BeginDisabled();
                group->setProvidingTextureBinding(false);
            } else {
                group->setProvidingTextureBinding(true);
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Texture semantic");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##BindingName", tmpBuffer, 149);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Texture source");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##Bindingsource", typeSelectedItem, ImGuiComboFlags_None)) {
                for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++) {
                    bool is_selected = (typeSelectedItem == typeItems[n]);
                    if (ImGui::Selectable(typeItems[n], is_selected)) {
                        typeSelectedItem = typeItems[n];
                        selectedIndex = n;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Create texture copy for binding");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##Copybinding", &copyBinding);

            ImGui::TableNextRow();

            ImGui::BeginDisabled(!copyBinding);
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Flip binding texture");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##flipbinding", &flipBinding);
            ImGui::EndDisabled();

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Clear binding on hash miss");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##Clearbinding", &clearBinding);

            if (selectedIndex == 1) {
                if (!instance.GetTrackDescriptors()) {
                    ImGui::BeginDisabled();
                    group->setExtractResourceViews(false);
                } else {
                    group->setExtractResourceViews(true);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Shader Stage");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##ShaderStage", selectedStage, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(stageItems); n++) {
                        bool is_selected = (selectedStage == stageItems[n]);
                        if (ImGui::Selectable(stageItems[n], is_selected)) {
                            selectedStageIndex = n;
                            selectedStage = stageItems[n];
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                group->setSRVShaderStage(selectedStageIndex);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Slot");
                ImGui::TableNextColumn();
                DrawTableStepper("bind_srv_slot",
                    [&]() { return group->getBindingSRVSlotIndex(); },
                    [&](uint32_t val) { group->setBindingSRVSlotIndex(val); });

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Binding");
                ImGui::TableNextColumn();
                {
                    char valBuf[16];
                    uint32_t val = group->getBindingSRVDescriptorIndex();
                    snprintf(valBuf, sizeof(valBuf), "%u", val);
                    const float textW = ImGui::CalcTextSize(valBuf).x;
                    const float btnW = ImGui::GetFrameHeight();
                    const float spacing = ImGui::GetStyle().ItemSpacing.x;
                    const float totalW = textW + btnW * 2.0f + spacing * 2.0f;
                    const float avail = ImGui::GetContentRegionAvail().x;
                    if (avail > totalW + 8.0f) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - totalW - 8.0f);
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(valBuf);
                    ImGui::SameLine();
                    if (val == 0) ImGui::BeginDisabled();
                    if (ImGui::Button("-##bind_srv_desc_dec", ImVec2(btnW, 0))) {
                        group->dispatchSRVCycle(ShaderToggler::CYCLE_DOWN);
                    }
                    if (val == 0) ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("+##bind_srv_desc_inc", ImVec2(btnW, 0))) {
                        group->dispatchSRVCycle(ShaderToggler::CYCLE_UP);
                    }
                }

                if (!instance.GetTrackDescriptors()) {
                    ImGui::EndDisabled();
                }
            } else {
                group->setExtractResourceViews(false);

                const char* rtTypeSelectedItem = invocationDescription[group->getBindingInvocationLocation()];
                uint32_t rtSelectedIndex = group->getBindingInvocationLocation();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Render target index");
                ImGui::TableNextColumn();
                DrawTableStepper("bind_rt_idx",
                    [&]() { return group->getBindingRenderTargetIndex(); },
                    [&](uint32_t val) { group->setBindingRenderTargetIndex(val); });

                ImGui::TableNextRow();
                if (!copyBinding) {
                    ImGui::BeginDisabled();
                }

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Invocation location");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##Invocationlocation", rtTypeSelectedItem, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(invocationDescription); n++) {
                        bool is_selected = (rtTypeSelectedItem == invocationDescription[n]);
                        if (ImGui::Selectable(invocationDescription[n], is_selected)) {
                            rtTypeSelectedItem = invocationDescription[n];
                            rtSelectedIndex = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (!copyBinding) {
                    ImGui::EndDisabled();
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Match swapchain");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##swapChainMatchMode", typesSelectedSwapchainMatchMode, ImGuiComboFlags_None)) {
                    for (int n = 0; n < IM_ARRAYSIZE(swapchainMatchOptions); n++) {
                        bool is_selected = (typesSelectedSwapchainMatchMode == swapchainMatchOptions[n]);
                        if (ImGui::Selectable(swapchainMatchOptions[n], is_selected)) {
                            typesSelectedSwapchainMatchMode = swapchainMatchOptions[n];
                            selectedSwapchainMatchMode = n;
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                group->setBindingInvocationLocation(rtSelectedIndex);
                group->setBindingMatchSwapchainResolution(selectedSwapchainMatchMode);
            }

            if (!isBindingEnabled) {
                ImGui::EndDisabled();
            }

            ImGui::EndTable();
        }

        group->setTextureBindingName(tmpBuffer);
        group->setCopyTextureBinding(copyBinding);
        group->setClearBindings(clearBinding);
        group->setFlipBufferBinding(flipBinding);

        EndCard();
    }

    ImGui::Spacing();

    if (BeginCard("##texture_preview_card", "TEXTURE PREVIEW")) {
        DisplayBindingPreview(instance, resManager, runtime, group);
        EndCard();
    }
}

static void DisplayGroupView(AddonImGui::AddonUIData& instance,
                             Rendering::ResourceManager& resManager,
                             reshade::api::effect_runtime* runtime,
                             ShaderToggler::ToggleGroup* group,
                             ShaderToggler::ShaderManager* shaderManager) {
    if (*instance.ActiveCollectorFrameCounter() > 0) {
        ImGui::Text("Collecting active shaders... %u frames remaining", instance.ActiveCollectorFrameCounter()->load());
        ImGui::TextDisabled("Keep the relevant scene visible until collection finishes.");
        return;
    }

    auto& huntingUI = instance.GetHuntingUIState();
    char* shaderSearch = huntingUI.shaderSearch;
    int& filterMode = huntingUI.filterMode;
    const char* filterItems[] = { "All", "Marked", "Unmarked" };

    const float filterColW = std::max(100.0f, ImGui::CalcTextSize("Unmarked").x + ImGui::GetStyle().FramePadding.x * 2.0f + 28.0f);
    if (ImGui::BeginTable("ShaderHuntSearch", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Filter", ImGuiTableColumnFlags_WidthFixed, filterColW);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##shaderSearch", "Search shader hash...", shaderSearch, 64);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##shaderFilter", &filterMode, filterItems, IM_ARRAYSIZE(filterItems));

        ImGui::EndTable();
    }

    auto repeatButton = [](const char* label) {
        ImGui::PushButtonRepeat(true);
        const bool pressed = ImGui::Button(label, ImVec2(-1.0f, 0));
        ImGui::PopButtonRepeat();
        return pressed;
    };

    bool navigationChanged = false;

    const size_t markedCount = shaderManager->getMarkedShaderCount();
    const uint32_t activeHuntedHash = shaderManager->getActiveHuntedShaderHash();

    const std::vector<uint32_t> hashes = shaderManager->getCollectedShaderHashesOrdered();
    const std::unordered_set<uint32_t> collectedSet(hashes.begin(), hashes.end());
    const std::unordered_set<uint32_t> markedHashes = shaderManager->getMarkedShaderHashes();

    std::vector<uint32_t> missingMarkedHashes;
    missingMarkedHashes.reserve(markedHashes.size());
    for (const uint32_t hash : markedHashes) {
        if (!collectedSet.contains(hash))
            missingMarkedHashes.push_back(hash);
    }
    std::sort(missingMarkedHashes.begin(), missingMarkedHashes.end());

    // Wrap at the pane edge: this pane is user-resizable and can get narrow.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%zu collected | %zu marked | %zu not seen",
                        hashes.size(),
                        markedCount,
                        missingMarkedHashes.size());
    if (!missingMarkedHashes.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
        ImGui::TextUnformatted("Red = marked but not observed during latest pass.");
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::TextWrapped("The selected shader previews live. Marked shaders are saved to the group when you click Done.");
    ImGui::Separator();

    const uint32_t selectedHash = shaderManager->getActiveHuntedShaderHash();

    std::string needle(shaderSearch);
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    struct ShaderListEntry {
        uint32_t hash;
        bool collected;
    };

    std::vector<ShaderListEntry> visibleHashes;
    visibleHashes.reserve(hashes.size() + missingMarkedHashes.size());

    auto addIfVisible = [&](uint32_t hash, bool collected) {
        const bool marked = shaderManager->isHuntedShaderMarked(hash);
        const std::string hashText = std::format("{:#08x}", hash);

        bool visible = filterMode == 0 || (filterMode == 1 && marked) || (filterMode == 2 && collected && !marked);
        if (visible && !needle.empty()) {
            std::string haystack = hashText;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            visible = haystack.find(needle) != std::string::npos;
        }

        if (visible)
            visibleHashes.push_back({ hash, collected });
    };

    for (const uint32_t hash : hashes)
        addIfVisible(hash, true);
    for (const uint32_t hash : missingMarkedHashes)
        addIfVisible(hash, false);

    const float bottomControlsHeight = ImGui::GetFrameHeightWithSpacing() * 4.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f + 6.0f;
    const float listHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y - bottomControlsHeight);

    if (ImGui::BeginTable("ShaderHashView",
                          1,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_NoBordersInBody,
                          ImVec2(0, listHeight))) {
        auto drawHash = [&](const ShaderListEntry& entry) {
            const uint32_t hash = entry.hash;
            const bool marked = shaderManager->isHuntedShaderMarked(hash);
            const bool missing = marked && !entry.collected;
            const std::string hashText = std::format("{:#08x}", hash);

            if (missing)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
            else if (marked)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

            const bool clicked =
              ImGui::Selectable(hashText.c_str(), entry.collected && selectedHash == hash, ImGuiSelectableFlags_AllowDoubleClick);

            if (entry.collected) {
                // Follow keyboard focus only on arrow-key navigation. A focused row otherwise kept
                // re-selecting itself and undid Next/Previous from hotkeys and the nav buttons.
                const bool focusNavigated = ImGui::IsItemFocused() && selectedHash != hash &&
                                            (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow));
                if ((clicked || focusNavigated) && shaderManager->setActiveHuntedShaderHash(hash)) {
                    instance.UpdateToggleGroupsForShaderHashes();
                }

                if ((clicked && ImGui::IsMouseDoubleClicked(0)) || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
                    shaderManager->toggleMarkOnHuntedShader();
                    instance.UpdateToggleGroupsForShaderHashes();
                }
            } else {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Marked shader not observed during the latest collection pass.\nDouble-click to unmark it.");
                if (clicked && ImGui::IsMouseDoubleClicked(0) && shaderManager->removeMarkedShaderHash(hash))
                    instance.UpdateToggleGroupsForShaderHashes();
            }

            if (missing || marked)
                ImGui::PopStyleColor();
        };

        for (const ShaderListEntry& entry : visibleHashes) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawHash(entry);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();

    const uint32_t stageOffset = std::min<uint32_t>(huntingUI.selectedShaderType, 2) * 7;
    const auto kbPrev = static_cast<AddonImGui::Keybind>(stageOffset + AddonImGui::PIXEL_SHADER_DOWN);
    const auto kbNext = static_cast<AddonImGui::Keybind>(stageOffset + AddonImGui::PIXEL_SHADER_UP);
    const auto kbMark = static_cast<AddonImGui::Keybind>(stageOffset + AddonImGui::PIXEL_SHADER_MARK);
    const auto kbPrevMarked = static_cast<AddonImGui::Keybind>(stageOffset + AddonImGui::PIXEL_SHADER_MARKED_DOWN);
    const auto kbNextMarked = static_cast<AddonImGui::Keybind>(stageOffset + AddonImGui::PIXEL_SHADER_MARKED_UP);

    auto showNavTooltip = [&](const char* description, AddonImGui::Keybind kb) {
        if (!ImGui::IsItemHovered())
            return;
        const uint32_t key = instance.GetKeybinding(kb);
        if (key != 0) {
            const std::string keyName = ShaderToggler::reshade_key_name(key);
            if (!keyName.empty()) {
                ImGui::SetTooltip("%s\nShortcut: %s", description, keyName.c_str());
                return;
            }
        }
        ImGui::SetTooltip("%s\nShortcut: None (assign in Keybindings tab)", description);
    };

    // Bottom Navigation Row 1: Previous / Next
    if (ImGui::BeginTable("ShaderHuntNavPrimary", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableNextColumn();
        if (repeatButton("< Previous")) {
            shaderManager->huntPreviousShader(false);
            navigationChanged = true;
        }
        showNavTooltip("Select previous collected shader", kbPrev);

        ImGui::TableNextColumn();
        if (repeatButton("Next >")) {
            shaderManager->huntNextShader(false);
            navigationChanged = true;
        }
        showNavTooltip("Select next collected shader", kbNext);

        ImGui::EndTable();
    }

    // Bottom Navigation Row 2: Marked Previous / Next
    if (ImGui::BeginTable("ShaderHuntNavMarked", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableNextColumn();
        if (repeatButton("< Prev Marked")) {
            shaderManager->huntPreviousShader(true);
            navigationChanged = true;
        }
        showNavTooltip("Select previous marked shader", kbPrevMarked);

        ImGui::TableNextColumn();
        if (repeatButton("Next Marked >")) {
            shaderManager->huntNextShader(true);
            navigationChanged = true;
        }
        showNavTooltip("Select next marked shader", kbNextMarked);

        ImGui::EndTable();
    }

    // Bottom Action Row 3: Mark / Unmark (Primary hunting action, prominent full-width)
    if (ImGui::Button("Mark / Unmark", ImVec2(-1.0f, 0))) {
        shaderManager->toggleMarkOnHuntedShader();
        navigationChanged = true;
    }
    if (ImGui::IsItemHovered()) {
        const uint32_t key = instance.GetKeybinding(kbMark);
        const uint32_t keyPrev = instance.GetKeybinding(static_cast<AddonImGui::Keybind>(stageOffset + AddonImGui::PIXEL_SHADER_MARK_PREV));
        const uint32_t keyNext = instance.GetKeybinding(static_cast<AddonImGui::Keybind>(stageOffset + AddonImGui::PIXEL_SHADER_MARK_NEXT));

        std::string shortcutStr = (key != 0) ? ShaderToggler::reshade_key_name(key) : "None (assign in Keybindings tab)";
        std::string extraStr = "";
        if (keyPrev != 0)
            extraStr += "\nMark + Prev: " + ShaderToggler::reshade_key_name(keyPrev);
        if (keyNext != 0)
            extraStr += "\nMark + Next: " + ShaderToggler::reshade_key_name(keyNext);

        ImGui::SetTooltip("Toggle mark on active shader (or double-click hash in list / press Enter)\nShortcut: %s%s", shortcutStr.c_str(), extraStr.c_str());
    }

    // Bottom Actions Row 4: Copy Hash | Clear Marked (2 equal columns with plenty of width)
    if (ImGui::BeginTable("ShaderHuntActionsSecondary", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableNextColumn();
        if (activeHuntedHash == 0)
            ImGui::BeginDisabled();
        if (ImGui::Button("Copy Hash", ImVec2(-1.0f, 0))) {
            const std::string hashText = std::format("0x{:08x}", activeHuntedHash);
            ImGui::SetClipboardText(hashText.c_str());
        }
        if (activeHuntedHash == 0)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy selected shader hash to clipboard");

        ImGui::TableNextColumn();
        if (markedCount == 0)
            ImGui::BeginDisabled();
        if (ImGui::Button("Clear Marked", ImVec2(-1.0f, 0))) {
            shaderManager->clearMarkedShaderHashes();
            navigationChanged = true;
        }
        if (markedCount == 0)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear all marked shaders in this stage");

        ImGui::EndTable();
    }

    if (navigationChanged)
        instance.UpdateToggleGroupsForShaderHashes();
}

static std::atomic<bool> s_imguiWantTextInput = false;

static void DisplayOverlay(AddonImGui::AddonUIData& instance, Rendering::ResourceManager& resManager, reshade::api::effect_runtime* runtime) {
    s_imguiWantTextInput.store(ImGui::GetIO().WantTextInput);
    if (instance.GetToggleGroupIdShaderEditing() >= 0) {
        std::string editingGroupName = "";
        const int idx = instance.GetToggleGroupIdShaderEditing();
        ShaderToggler::ToggleGroup* group = nullptr;
        if (instance.GetToggleGroups().find(idx) != instance.GetToggleGroups().end()) {
            editingGroupName = instance.GetToggleGroups()[idx].getName();
            group = &instance.GetToggleGroups()[idx];
        }

        if (group == nullptr)
            return;

        RfxThemeScope theme;
        ImGui::SetNextWindowBgAlpha(1.0);
        ImGui::SetNextWindowSize({ 1280, 800 }, ImGuiCond_Once);
        bool wndOpen = true;

        auto& huntingUIState = instance.GetHuntingUIState();
        const char* typeItems[] = { "Pixel shader", "Vertex shader", "Compute Shader" };
        uint32_t& selectedIndex = huntingUIState.selectedShaderType;
        selectedIndex = std::min<uint32_t>(selectedIndex, 2);
        const char* typeSelectedItem = typeItems[selectedIndex];

        ShaderToggler::ShaderManager* selectedShaderManager =
          selectedIndex == 0 ? instance.GetPixelShaderManager() : (selectedIndex == 1 ? instance.GetVertexShaderManager() : instance.GetComputeShaderManager());

        if (ImGui::Begin(std::format("Group settings ({})", editingGroupName).c_str(), &wndOpen)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            const float splitterWidth = 8.0f;
            const float horizontalSpacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float maxShaderPaneWidth = std::max(300.0f, availableWidth - 420.0f - splitterWidth - horizontalSpacing);
            huntingUIState.shaderPaneWidth = std::clamp(huntingUIState.shaderPaneWidth, 300.0f, maxShaderPaneWidth);

            if (ImGui::BeginChild("GroupView", { huntingUIState.shaderPaneWidth, 0 }, true, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

                const float rescanBtnW = std::max(76.0f, ImGui::CalcTextSize("Rescan").x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f);
                if (ImGui::BeginTable("##shaderTypeRow", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Rescan", ImGuiTableColumnFlags_WidthFixed, rescanBtnW);

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##shaderType", typeSelectedItem, ImGuiComboFlags_None)) {
                        for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++) {
                            bool is_selected = (typeSelectedItem == typeItems[n]);
                            if (ImGui::Selectable(typeItems[n], is_selected)) {
                                if (n != selectedIndex) {
                                    switch (n) {
                                        case 0: {
                                            instance.GetVertexShaderManager()->resetActiveHuntedShader();
                                            instance.GetComputeShaderManager()->resetActiveHuntedShader();
                                        } break;
                                        case 1: {
                                            instance.GetPixelShaderManager()->resetActiveHuntedShader();
                                            instance.GetComputeShaderManager()->resetActiveHuntedShader();
                                        } break;
                                        case 2: {
                                            instance.GetPixelShaderManager()->resetActiveHuntedShader();
                                            instance.GetVertexShaderManager()->resetActiveHuntedShader();
                                        } break;
                                        default:
                                            break;
                                    }

                                    instance.UpdateToggleGroupsForShaderHashes();
                                }

                                typeSelectedItem = typeItems[n];
                                selectedIndex = n;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::TableNextColumn();
                    if (ImGui::Button("Rescan", ImVec2(-1.0f, 0))) {
                        auto* pixelManager = instance.GetPixelShaderManager();
                        auto* vertexManager = instance.GetVertexShaderManager();
                        auto* computeManager = instance.GetComputeShaderManager();
                        pixelManager->startHuntingMode(pixelManager->getMarkedShaderHashes());
                        vertexManager->startHuntingMode(vertexManager->getMarkedShaderHashes());
                        computeManager->startHuntingMode(computeManager->getMarkedShaderHashes());
                        *instance.ActiveCollectorFrameCounter() = *instance.StartValueFramecountCollectionPhase();
                        instance.UpdateToggleGroupsForShaderHashes();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Recollect active shaders for the configured number of frames.");

                    ImGui::EndTable();
                }

                if (group->hasGeometryFilter()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Group Filter: ");
                    ImGui::SameLine();
                    if (group->getMatchIndexCount()) {
                        if (group->getIndexCountMin() == group->getIndexCountMax())
                            ImGui::Text("Index = %u", group->getIndexCountMin());
                        else
                            ImGui::Text("Index [%u - %u]", group->getIndexCountMin(), group->getIndexCountMax());
                    } else if (group->getMatchVertexCount()) {
                        if (group->getVertexCountMin() == group->getVertexCountMax())
                            ImGui::Text("Vertex = %u", group->getVertexCountMin());
                        else
                            ImGui::Text("Vertex [%u - %u]", group->getVertexCountMin(), group->getVertexCountMax());
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear Filter##Group")) {
                        group->setMatchIndexCount(false);
                        group->setMatchVertexCount(false);
                        group->setMatchInstanceCount(false);
                    }
                }

                DisplayGroupView(instance, resManager, runtime, group, selectedShaderManager);

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::PushID(0);
            ImGui::Button("", ImVec2(splitterWidth, -1));
            ImGui::PopID();
            if (ImGui::IsItemActive()) {
                huntingUIState.shaderPaneWidth =
                  std::clamp(huntingUIState.shaderPaneWidth + ImGui::GetIO().MouseDelta.x, 300.0f, maxShaderPaneWidth);
            }

            ImGui::SameLine();

            // The bool-border overload takes ImGuiWindowFlags: ImGuiChildFlags_AlwaysAutoResize aliased
            // ImGuiWindowFlags_AlwaysAutoResize there and let this pane grow past the window edge.
            if (ImGui::BeginChild("GroupSettings", { 0, 0 }, true, ImGuiWindowFlags_None)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));

                // Off by default: the hunted shader stays visible so the group's effects and the
                // render target preview update live while you step through shaders.
                const bool vulkanHunting = runtime->get_device()->get_api() == reshade::api::device_api::vulkan;
                bool hideHunted = selectedShaderManager->isHideHuntedShader();
                if (vulkanHunting) ImGui::BeginDisabled();
                if (DrawToggleRow("Hide hunted shader in 3D scene", &hideHunted,
                                  "Skip draws of the currently selected shader so you can see what it renders.\nWhile hidden, effects still trigger on it and the preview still captures it.",
                                  vulkanHunting ? "Vulkan always hides the hunted shader (preview is captured at a safe render-pass boundary)." : nullptr,
                                  4.0f)) {
                    instance.GetPixelShaderManager()->setHideHuntedShader(hideHunted);
                    instance.GetVertexShaderManager()->setHideHuntedShader(hideHunted);
                    instance.GetComputeShaderManager()->setHideHuntedShader(hideHunted);
                }
                if (vulkanHunting) ImGui::EndDisabled();

                bool hideMarkedShaders = group->getHideMarkedShaders();
                if (DrawToggleRow("Hide marked shaders", &hideMarkedShaders,
                                  "Suppress graphics draws matching this group's marked pixel or vertex shaders while the group is active.\nUseful for HUD-free screenshots, shader identification and configuration testing.\nCompute dispatches are not suppressed.",
                                  hideMarkedShaders ? "Matching graphics draws are suppressed while this group is active." : nullptr,
                                  4.0f)) {
                    group->setHideMarkedShaders(hideMarkedShaders);
                }

                bool suppressDraw = group->getSuppressDrawCall();
                if (DrawToggleRow("Suppress draw calls", &suppressDraw,
                                  "When enabled and this group is active, draw calls using this group's shaders are suppressed directly on the GPU.",
                                  nullptr,
                                  4.0f)) {
                    group->setSuppressDrawCall(suppressDraw);
                }

                if (ImGui::TreeNode("Geometry Refine Filters")) {
                    bool matchIdx = group->getMatchIndexCount();
                    if (DrawToggleRow("Filter by index count", &matchIdx, "Only trigger for draw calls with index count in specified range.", nullptr, 4.0f)) {
                        group->setMatchIndexCount(matchIdx);
                    }
                    if (matchIdx) {
                        ImGui::Indent(16.0f);
                        int idxMin = static_cast<int>(group->getIndexCountMin());
                        int idxMax = static_cast<int>(group->getIndexCountMax());
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Min:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputInt("##IdxMinOverlay", &idxMin, 0))
                            group->setIndexCountMin(std::max(0, idxMin));
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Max:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputInt("##IdxMaxOverlay", &idxMax, 0))
                            group->setIndexCountMax(std::max(0, idxMax));
                        ImGui::Unindent(16.0f);
                    }

                    bool matchVtx = group->getMatchVertexCount();
                    if (DrawToggleRow("Filter by vertex count", &matchVtx, "Only trigger for draw calls with vertex count in specified range.", nullptr, 4.0f)) {
                        group->setMatchVertexCount(matchVtx);
                    }
                    if (matchVtx) {
                        ImGui::Indent(16.0f);
                        int vtxMin = static_cast<int>(group->getVertexCountMin());
                        int vtxMax = static_cast<int>(group->getVertexCountMax());
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Min:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputInt("##VtxMinOverlay", &vtxMin, 0))
                            group->setVertexCountMin(std::max(0, vtxMin));
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Max:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputInt("##VtxMaxOverlay", &vtxMax, 0))
                            group->setVertexCountMax(std::max(0, vtxMax));
                        ImGui::Unindent(16.0f);
                    }

                    bool matchInst = group->getMatchInstanceCount();
                    if (DrawToggleRow("Filter by instance count", &matchInst, "Only trigger for draw calls with instance count in specified range.", nullptr, 4.0f)) {
                        group->setMatchInstanceCount(matchInst);
                    }
                    if (matchInst) {
                        ImGui::Indent(16.0f);
                        int instMin = static_cast<int>(group->getInstanceCountMin());
                        int instMax = static_cast<int>(group->getInstanceCountMax());
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Min:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputInt("##InstMinOverlay", &instMin, 0))
                            group->setInstanceCountMin(std::max(0, instMin));
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Max:");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputInt("##InstMaxOverlay", &instMax, 0))
                            group->setInstanceCountMax(std::max(0, instMax));
                        ImGui::Unindent(16.0f);
                    }

                    if (group->hasGeometryFilter()) {
                        ImGui::Spacing();
                        if (ImGui::Button("Clear Geometry Filters##Overlay")) {
                            group->setMatchIndexCount(false);
                            group->setMatchVertexCount(false);
                            group->setMatchInstanceCount(false);
                            group->setIndexCountMin(0);
                            group->setIndexCountMax(0);
                            group->setVertexCountMin(0);
                            group->setVertexCountMax(0);
                            group->setInstanceCountMin(0);
                            group->setInstanceCountMax(0);
                        }
                    }
                    ImGui::TreePop();
                }

                if (instance.GetShowObservedDraws()) {
                    const auto observedDraws = selectedShaderManager->getObservedDrawGeometries();
                    const uint32_t activeHash = selectedShaderManager->getActiveHuntedShaderHash();
                    if (BeginCard("##observed_draws_pane", "OBSERVED DRAWS FOR ACTIVE SHADER")) {
                        if (activeHash == 0) {
                            ImGui::TextDisabled("No active shader selected in list.");
                        } else if (observedDraws.empty()) {
                            ImGui::TextDisabled("No draw calls observed for 0x%08X yet.", activeHash);
                        } else {
                            ImGui::TextDisabled("%zu draw variation(s) observed for 0x%08X:", observedDraws.size(), activeHash);
                        }

                        const float boxHeight = 85.0f;
                        if (ImGui::BeginChild("##observedDrawsScroll", ImVec2(0, boxHeight), true)) {
                            if (activeHash == 0) {
                                ImGui::TextDisabled("Select a shader from the left hunting list.");
                            } else if (observedDraws.empty()) {
                                ImGui::TextDisabled("Draw geometry stats will appear here when this shader renders.");
                            } else {
                                for (size_t di = 0; di < observedDraws.size(); di++) {
                                    const auto& d = observedDraws[di];
                                    ImGui::PushID(static_cast<int>(di));
                                    ImGui::BulletText("%s: %u %s (inst: %u, x%u)",
                                        d.isIndexed ? "Indexed" : "Non-idx",
                                        d.count,
                                        d.isIndexed ? "idx" : "vtx",
                                        d.instanceCount,
                                        d.invocations);
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Set Filter")) {
                                        if (d.isIndexed) {
                                            group->setMatchIndexCount(true);
                                            group->setIndexCountMin(d.count);
                                            group->setIndexCountMax(d.count);
                                            group->setMatchVertexCount(false);
                                        } else {
                                            group->setMatchVertexCount(true);
                                            group->setVertexCountMin(d.count);
                                            group->setVertexCountMax(d.count);
                                            group->setMatchIndexCount(false);
                                        }
                                    }
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip("Lock group geometry filter to exactly %u %s.", d.count, d.isIndexed ? "indices" : "vertices");
                                    }
                                    ImGui::PopID();
                                }
                            }
                        }
                        ImGui::EndChild();
                        EndCard();
                    }
                }

                ImGui::Separator();

                ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
                if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags)) {
                    if (ImGui::BeginTabItem("Render targets")) {
                        instance.SetCurrentTabType(AddonImGui::TAB_RENDER_TARGET);
                        DisplayRenderTargets(instance, resManager, runtime, group);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Techniques")) {
                        instance.SetCurrentTabType(AddonImGui::TAB_TECHNIQUE);
                        DisplayTechniqueSelection(runtime, instance, group);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Constant bindings")) {
                        instance.SetCurrentTabType(AddonImGui::TAB_CONSTANT_BUFFER);
                        DisplayConstantTab(instance, group, runtime->get_device());
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Texture bindings")) {
                        instance.SetCurrentTabType(AddonImGui::TAB_TEXTURE_BINDING);
                        DisplayTextureBindings(instance, group, runtime, resManager);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();

            ImGui::PopStyleVar();
        }
        ImGui::End();

        if (!wndOpen) {
            instance.SetCurrentTabType(AddonImGui::TAB_NONE);
            instance.CloseGroupSettings(true, *group);
        }
    } else {
        instance.SetCurrentTabType(AddonImGui::TAB_NONE);
    }
}

static void CheckHotkeys(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    auto& gpMonitor = ShaderToggler::GamepadMonitor::getInstance();
    gpMonitor.update();

    if (*instance.ActiveCollectorFrameCounter() > 0)
        --(*instance.ActiveCollectorFrameCounter());

    auto pressedKeys = [&](uint32_t keys) {
        if (keys == 0 || !ShaderToggler::areKeysPressed(keys, runtime))
            return false;

        const bool wantsCtrl = ((keys >> 8) & 0xFF) != 0;
        const bool wantsShift = ((keys >> 16) & 0xFF) != 0;
        const bool wantsAlt = ((keys >> 24) & 0xFF) != 0;

        return wantsCtrl == runtime->is_key_down(VK_CONTROL) &&
               wantsShift == runtime->is_key_down(VK_SHIFT) &&
               wantsAlt == runtime->is_key_down(VK_MENU);
    };

    auto pressed = [&](AddonImGui::Keybind binding) {
        return pressedKeys(instance.GetKeybinding(binding));
    };

    auto handleHunting = [&](ShaderToggler::ShaderManager* manager,
                             AddonImGui::Keybind previous,
                             AddonImGui::Keybind next,
                             AddonImGui::Keybind mark,
                             AddonImGui::Keybind previousMarked,
                             AddonImGui::Keybind nextMarked,
                             AddonImGui::Keybind markPrevious,
                             AddonImGui::Keybind markNext) {
        bool changed = false;

        if (pressed(markPrevious)) {
            manager->toggleMarkOnHuntedShader();
            manager->huntPreviousShader(false);
            return true;
        }
        if (pressed(markNext)) {
            manager->toggleMarkOnHuntedShader();
            manager->huntNextShader(false);
            return true;
        }

        if (pressed(previousMarked)) {
            manager->huntPreviousShader(true);
            changed = true;
        } else if (pressed(nextMarked)) {
            manager->huntNextShader(true);
            changed = true;
        } else if (pressed(previous)) {
            manager->huntPreviousShader(false);
            changed = true;
        } else if (pressed(next)) {
            manager->huntNextShader(false);
            changed = true;
        }

        if (pressed(mark)) {
            manager->toggleMarkOnHuntedShader();
            changed = true;
        }

        return changed;
    };

    if (instance.GetToggleGroupIdShaderEditing() >= 0) {
        if (!s_imguiWantTextInput.load()) {
            bool changed = false;
            changed |= handleHunting(instance.GetPixelShaderManager(),
                                     AddonImGui::PIXEL_SHADER_DOWN,
                                     AddonImGui::PIXEL_SHADER_UP,
                                     AddonImGui::PIXEL_SHADER_MARK,
                                     AddonImGui::PIXEL_SHADER_MARKED_DOWN,
                                     AddonImGui::PIXEL_SHADER_MARKED_UP,
                                     AddonImGui::PIXEL_SHADER_MARK_PREV,
                                     AddonImGui::PIXEL_SHADER_MARK_NEXT);
            changed |= handleHunting(instance.GetVertexShaderManager(),
                                     AddonImGui::VERTEX_SHADER_DOWN,
                                     AddonImGui::VERTEX_SHADER_UP,
                                     AddonImGui::VERTEX_SHADER_MARK,
                                     AddonImGui::VERTEX_SHADER_MARKED_DOWN,
                                     AddonImGui::VERTEX_SHADER_MARKED_UP,
                                     AddonImGui::VERTEX_SHADER_MARK_PREV,
                                     AddonImGui::VERTEX_SHADER_MARK_NEXT);
            changed |= handleHunting(instance.GetComputeShaderManager(),
                                     AddonImGui::COMPUTE_SHADER_DOWN,
                                     AddonImGui::COMPUTE_SHADER_UP,
                                     AddonImGui::COMPUTE_SHADER_MARK,
                                     AddonImGui::COMPUTE_SHADER_MARKED_DOWN,
                                     AddonImGui::COMPUTE_SHADER_MARKED_UP,
                                     AddonImGui::COMPUTE_SHADER_MARK_PREV,
                                     AddonImGui::COMPUTE_SHADER_MARK_NEXT);

            if (changed)
                instance.UpdateToggleGroupsForShaderHashes();
        }
        return;
    }

    auto& groups = instance.GetToggleGroups();

    // Global "toggle all groups": flips every group to the opposite of the
    // current majority state so a single press reliably hides/shows everything.
    const uint32_t toggleAllKey = instance.GetKeybinding(AddonImGui::Keybind::TOGGLE_ALL_GROUPS);
    const uint32_t toggleAllGamepad = instance.GetGamepadToggleAll();
    bool triggerToggleAll = false;
    if (toggleAllKey != 0 && pressedKeys(toggleAllKey)) {
        triggerToggleAll = true;
    }
    if (toggleAllGamepad != 0 && gpMonitor.isComboTriggered(toggleAllGamepad)) {
        triggerToggleAll = true;
    }

    if (triggerToggleAll) {
        size_t activeCount = 0;
        for (const auto& [_, group] : groups) {
            if (group.isActive())
                ++activeCount;
        }
        const bool targetActive = activeCount <= (groups.size() / 2);
        for (auto& [_, group] : groups) {
            if (group.isActive() != targetActive) {
                group.toggleActive();
                if (!targetActive && instance.GetConstantHandler() != nullptr) {
                    instance.GetConstantHandler()->RemoveGroup(&group, runtime->get_device());
                }
            }
        }
    }

    // Per-group toggle keys and controller shortcuts.
    for (auto& [_, group] : groups) {
        bool triggered = false;
        const uint32_t key = group.getToggleKey();
        if (key != 0 && pressedKeys(key)) {
            triggered = true;
        }
        const uint32_t gpShortcut = group.getGamepadShortcut();
        if (gpShortcut != 0 && gpMonitor.isComboTriggered(gpShortcut)) {
            triggered = true;
        }

        if (triggered) {
            const bool wasActive = group.isActive();
            group.toggleActive();
            if (wasActive && instance.GetConstantHandler() != nullptr) {
                instance.GetConstantHandler()->RemoveGroup(&group, runtime->get_device());
            }
        }
    }
}

static void ShowHelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(450.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void DrawCategoryGroups(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    static std::string groupClipboardStatus;

    if (ImGui::Button("New Group")) {
        instance.AddDefaultGroup();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create a new toggle group with default settings.");

    ImGui::SameLine();
    if (ImGui::Button("Import Group")) {
        const char* clipboard = ImGui::GetClipboardText();
        if (clipboard != nullptr && instance.ImportToggleGroup(clipboard) != nullptr) {
            RuntimeDataContainer& runtimeData = runtime->get_private_data<RuntimeDataContainer>();
            std::shared_lock<std::shared_mutex> techLock(runtimeData.technique_mutex);
            instance.AssignPreferredGroupTechniques(runtimeData.allTechniques);
            groupClipboardStatus = "Group imported from clipboard.";
        } else {
            groupClipboardStatus = "Clipboard does not contain a valid REST group.";
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Import a REST group serialized string from the Windows clipboard.");

    ImGui::SameLine();
    if (instance.IsConfigDirty()) {
        ImGui::TextColored(rfx_warn(), "Unsaved changes");
    } else {
        ImGui::TextColored(rfx_ok(), "All changes saved");
    }

    if (!groupClipboardStatus.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", groupClipboardStatus.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<ShaderToggler::ToggleGroup*> toRemove;
    std::vector<int> toClone;
    int moveUpId = -1;
    int moveDownId = -1;

    const auto& groupOrder = instance.GetToggleGroupOrder();
    for (size_t orderIdx = 0; orderIdx < groupOrder.size(); ++orderIdx) {
        const int groupId = groupOrder[orderIdx];
        auto it = instance.GetToggleGroups().find(groupId);
        if (it == instance.GetToggleGroups().end())
            continue;
        auto& group = it->second;

        ImGui::PushID(group.getId());

        std::string cardId = std::format("##group_card_{}", group.getId());
        if (BeginCard(cardId.c_str())) {
            // First row: Up/Down Arrows + Active switch + Name + Badges + Action Buttons
            const bool isFirst = (orderIdx == 0);
            const bool isLast = (orderIdx + 1 >= groupOrder.size());

            if (isFirst) ImGui::BeginDisabled();
            if (ImGui::ArrowButton(std::format("##up_{}", group.getId()).c_str(), ImGuiDir_Up)) {
                moveUpId = group.getId();
            }
            if (isFirst) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Move group up (higher priority)");

            ImGui::SameLine();
            if (isLast) ImGui::BeginDisabled();
            if (ImGui::ArrowButton(std::format("##dn_{}", group.getId()).c_str(), ImGuiDir_Down)) {
                moveDownId = group.getId();
            }
            if (isLast) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Move group down (lower priority)");

            ImGui::SameLine();
            bool groupActive = group.isActive();
            if (DrawToggleSwitch("##active_sw", &groupActive)) {
                group.toggleActive();
                if (!groupActive && instance.GetConstantHandler() != nullptr) {
                    instance.GetConstantHandler()->RemoveGroup(&group, runtime->get_device());
                }
            }

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(rfx_col(ImGuiCol_Text), "%s", group.getName().c_str());

            const std::string keyStr = group.getToggleKey() > 0 ? ShaderToggler::reshade_key_name(group.getToggleKey()) : "";
            const std::string padStr = group.getGamepadShortcut() > 0 ? ShaderToggler::GamepadMonitor::buttonsToString(group.getGamepadShortcut()) : "";

            if (!keyStr.empty() || !padStr.empty()) {
                ImGui::SameLine();
                if (!keyStr.empty() && !padStr.empty()) {
                    ImGui::TextColored(rfx_accent(), "[%s | Pad: %s]", keyStr.c_str(), padStr.c_str());
                } else if (!keyStr.empty()) {
                    ImGui::TextColored(rfx_accent(), "[%s]", keyStr.c_str());
                } else {
                    ImGui::TextColored(rfx_accent(), "[Pad: %s]", padStr.c_str());
                }
            }

            // Action buttons on the right side of the card: only Settings and Edit!
            const bool isEditingShaders = instance.GetToggleGroupIdShaderEditing() == group.getId();
            const char* settingsLabel = isEditingShaders ? "Done" : "Settings";
            const char* editLabel = group.isEditing() ? "Close" : "Edit";

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float padX = ImGui::GetStyle().FramePadding.x * 2.0f;
            const float totalBtnsW = (ImGui::CalcTextSize(settingsLabel).x + padX) +
                                     (ImGui::CalcTextSize(editLabel).x + padX) +
                                     spacing + RFX_CARD_PAD;

            const float avail = ImGui::GetContentRegionAvail().x;
            SameLineIfFits(ImGui::GetCursorPosX() + avail - totalBtnsW);

            if (isEditingShaders) {
                ImGui::PushStyleColor(ImGuiCol_Button, rfx_col(ImGuiCol_ButtonActive));
                if (ImGui::Button("Done")) {
                    instance.EndShaderEditing(true, group);
                }
                ImGui::PopStyleColor();
            } else if (instance.GetToggleGroupIdShaderEditing() >= 0) {
                ImGui::BeginDisabled();
                ImGui::Button("Settings");
                ImGui::EndDisabled();
            } else {
                if (ImGui::Button("Settings")) {
                    instance.StartShaderEditing(group);
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the shader hunting & binding settings window for this group.");

            ImGui::SameLine();
            if (ImGui::Button(editLabel)) {
                group.setEditing(!group.isEditing());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle inline configuration panel (rename, keybind, pad combo, filters).");

            // Summary text row
            const bool shaderEditingThisGroup = instance.GetToggleGroupIdShaderEditing().load() == group.getId();
            const size_t psCount = shaderEditingThisGroup ? instance.GetPixelShaderManager()->getMarkedShaderCount() : group.getPixelShaderHashCount();
            const size_t vsCount = shaderEditingThisGroup ? instance.GetVertexShaderManager()->getMarkedShaderCount() : group.getVertexShaderHashCount();
            const size_t csCount = shaderEditingThisGroup ? instance.GetComputeShaderManager()->getMarkedShaderCount() : group.getComputeShaderHashCount();
            const size_t fxCount = group.preferredTechniques().size();

            ImGui::Spacing();
            ImGui::TextDisabled("Shaders: PS %zu | VS %zu | CS %zu | Effects: %zu%s%s%s%s",
                                psCount,
                                vsCount,
                                csCount,
                                fxCount,
                                group.getAutoRenderSRV() ? " | Auto Scene Colour" : "",
                                group.getHideMarkedShaders() ? " | Hide Shaders" : "",
                                group.getSuppressDrawCall() ? " | Suppress Draws" : "",
                                shaderEditingThisGroup ? " | Hunting Active" : "");

            if (group.hasGeometryFilter()) {
                ImGui::SameLine();
                if (group.getMatchIndexCount()) {
                    if (group.getIndexCountMin() == group.getIndexCountMax())
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "| Index: %u", group.getIndexCountMin());
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "| Index: [%u-%u]", group.getIndexCountMin(), group.getIndexCountMax());
                } else if (group.getMatchVertexCount()) {
                    if (group.getVertexCountMin() == group.getVertexCountMax())
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "| Vertex: %u", group.getVertexCountMin());
                    else
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "| Vertex: [%u-%u]", group.getVertexCountMin(), group.getVertexCountMax());
                }
            }

            // Conflict warnings
            if (group.getToggleKey() != 0) {
                bool conflictShown = false;
                for (const auto& [otherId, otherGroup] : instance.GetToggleGroups()) {
                    if (otherId != group.getId() && otherGroup.getToggleKey() == group.getToggleKey()) {
                        ImGui::TextColored(rfx_warn(), "Warning: group shortcut conflicts with '%s'.", otherGroup.getName().c_str());
                        conflictShown = true;
                        break;
                    }
                }

                if (!conflictShown) {
                    constexpr uint32_t activeKeybindCount = static_cast<uint32_t>(AddonImGui::INVOCATION_DOWN);
                    for (uint32_t i = 0; i < activeKeybindCount; ++i) {
                        if (instance.GetKeybinding(static_cast<AddonImGui::Keybind>(i)) == group.getToggleKey()) {
                            ImGui::TextColored(rfx_warn(), "Warning: shortcut conflicts with REST action '%s'.", AddonImGui::KeybindDisplayNames[i]);
                            break;
                        }
                    }
                }
            }

            // Inline edit section
            if (group.isEditing()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(rfx_accent(), "Configure Group #%d", group.getId());
                ImGui::Spacing();

                const float clearBtnW = std::max(56.0f, ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f);
                const float inputW = 200.0f;
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float totalFieldW = inputW + spacing + clearBtnW;

                if (ImGui::BeginTable("##group_edit_fields", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody, ImVec2(-RFX_CARD_PAD, 0.0f))) {
                    ImGui::TableSetupColumn("##field_lbl", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##field_val", ImGuiTableColumnFlags_WidthFixed, totalFieldW);

                    // Name field
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Name:");
                    ImGui::TableNextColumn();
                    char tmpBuffer[150] = {};
                    const std::string name = group.getName();
                    strncpy_s(tmpBuffer, 150, name.c_str(), name.size());
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##Name", tmpBuffer, 149)) {
                        group.setName(tmpBuffer);
                        instance.MarkConfigDirty();
                    }

                    // Keyboard Shortcut
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Keyboard Shortcut:");
                    ImGui::TableNextColumn();
                    uint32_t keys = group.getToggleKey();
                    ImGui::SetNextItemWidth(inputW);
                    if (key_input_box("##KbShortcut", &keys, runtime)) {
                        group.setToggleKey(keys);
                        instance.MarkConfigDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##ClearKbGroup", ImVec2(clearBtnW, 0))) {
                        group.setToggleKey(0);
                        instance.MarkConfigDirty();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear keyboard shortcut");

                    // Gamepad Shortcut
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Gamepad Shortcut:");
                    ImGui::TableNextColumn();
                    uint32_t gpShortcut = group.getGamepadShortcut();
                    ImGui::SetNextItemWidth(inputW);
                    if (gamepad_input_box("##PadShortcut", &gpShortcut)) {
                        group.setGamepadShortcut(gpShortcut);
                        instance.MarkConfigDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##ClearPadGroup", ImVec2(clearBtnW, 0))) {
                        group.setGamepadShortcut(0);
                        instance.MarkConfigDirty();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear gamepad shortcut");

                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Toggle switches for Draw Calls & Filters
                bool suppress = group.getSuppressDrawCall();
                if (DrawToggleRow("Suppress Draw Calls", &suppress,
                                  "When enabled and this group is active, draw calls using this group's shaders are suppressed directly on the GPU.")) {
                    group.setSuppressDrawCall(suppress);
                    instance.MarkConfigDirty();
                }

                bool matchIdx = group.getMatchIndexCount();
                if (DrawToggleRow("Filter by Index Count", &matchIdx,
                                  "Only match indexed draw calls within the configured index count range.\nSet Min == Max for exact match, or Max = 0 for unlimited.")) {
                    group.setMatchIndexCount(matchIdx);
                    instance.MarkConfigDirty();
                }
                if (matchIdx) {
                    ImGui::Indent(16.0f);
                    int idxMin = static_cast<int>(group.getIndexCountMin());
                    int idxMax = static_cast<int>(group.getIndexCountMax());
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Min:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::InputInt("##IdxMin", &idxMin, 0)) {
                        group.setIndexCountMin(std::max(0, idxMin));
                        instance.MarkConfigDirty();
                    }
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Max:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::InputInt("##IdxMax", &idxMax, 0)) {
                        group.setIndexCountMax(std::max(0, idxMax));
                        instance.MarkConfigDirty();
                    }
                    ImGui::Unindent(16.0f);
                }

                bool matchVtx = group.getMatchVertexCount();
                if (DrawToggleRow("Filter by Vertex Count", &matchVtx,
                                  "Only match non-indexed draw calls within the configured vertex count range.\nSet Min == Max for exact match, or Max = 0 for unlimited.")) {
                    group.setMatchVertexCount(matchVtx);
                    instance.MarkConfigDirty();
                }
                if (matchVtx) {
                    ImGui::Indent(16.0f);
                    int vtxMin = static_cast<int>(group.getVertexCountMin());
                    int vtxMax = static_cast<int>(group.getVertexCountMax());
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Min:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::InputInt("##VtxMin", &vtxMin, 0)) {
                        group.setVertexCountMin(std::max(0, vtxMin));
                        instance.MarkConfigDirty();
                    }
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Max:");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::InputInt("##VtxMax", &vtxMax, 0)) {
                        group.setVertexCountMax(std::max(0, vtxMax));
                        instance.MarkConfigDirty();
                    }
                    ImGui::Unindent(16.0f);
                }

                if (group.hasGeometryFilter()) {
                    ImGui::Spacing();
                    if (ImGui::Button("Clear Geometry Filters##Edit")) {
                        group.setMatchIndexCount(false);
                        group.setMatchVertexCount(false);
                        group.setMatchInstanceCount(false);
                        group.setIndexCountMin(0);
                        group.setIndexCountMax(0);
                        group.setVertexCountMin(0);
                        group.setVertexCountMax(0);
                        instance.MarkConfigDirty();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Done Editing##OK")) {
                    group.setEditing(false);
                }

                // Action buttons on the right side of the edit panel
                const float cloneW = ImGui::CalcTextSize("Clone").x + padX;
                const float copyW = ImGui::CalcTextSize("Copy").x + padX;
                const float deleteW = ImGui::CalcTextSize("Delete").x + padX;
                const float rightActionsW = cloneW + copyW + deleteW + spacing * 2.0f + RFX_CARD_PAD;

                const float availRight = ImGui::GetContentRegionAvail().x;
                SameLineIfFits(ImGui::GetCursorPosX() + availRight - rightActionsW);

                if (ImGui::Button("Clone")) {
                    toClone.push_back(group.getId());
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate this group and its assigned shaders.");

                ImGui::SameLine();
                if (ImGui::Button("Copy")) {
                    const std::string serialized = instance.ExportToggleGroup(group);
                    ImGui::SetClipboardText(serialized.c_str());
                    groupClipboardStatus = "Group copied to clipboard.";
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy serialized group to Windows clipboard.");

                ImGui::SameLine();
                if (ImGui::Button("Delete")) {
                    ImGui::OpenPopup("Delete group?");
                }

                if (ImGui::BeginPopupModal("Delete group?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Delete '%s'?", group.getName().c_str());
                    ImGui::TextDisabled("This takes effect immediately. Save changes to write to disk.");
                    ImGui::Spacing();
                    if (ImGui::Button("Delete")) {
                        toRemove.push_back(&group);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }

            EndCard();
        }

        ImGui::PopID();
    }

    if (moveUpId >= 0) {
        instance.MoveGroupUp(moveUpId);
    }
    if (moveDownId >= 0) {
        instance.MoveGroupDown(moveDownId);
    }

    for (const int sourceId : toClone)
        instance.CloneToggleGroup(sourceId);

    if (!toRemove.empty()) {
        instance.GetToggleGroupIdEffectEditing() = -1;
        instance.GetToggleGroupIdSettingsOpen() = -1;
        instance.GetToggleGroupIdShaderEditing() = -1;
        instance.GetToggleGroupIdConstantEditing() = -1;
        instance.StopHuntingMode();
    }

    for (const auto* group : toRemove) {
        instance.SignalToggleGroupRemoved(runtime, const_cast<ShaderToggler::ToggleGroup*>(group));
        instance.RetireToggleGroup(group->getId());
    }

    if (!toRemove.empty() || !toClone.empty()) {
        instance.UpdateToggleGroupsForShaderHashes();
        instance.MarkConfigDirty();
    }
}

static void DrawCategoryKeybindings(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    if (BeginCard("##cat_pad", "GAMEPAD CONTROLLER")) {
        auto& gp = ShaderToggler::GamepadMonitor::getInstance();
        if (gp.isConnected()) {
            ImGui::TextColored(rfx_ok(), "Controller: Connected (User %d)", gp.getActiveUserIndex());
        } else {
            ImGui::TextDisabled("Controller: Not connected (connect any XInput gamepad)");
        }
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Toggle ALL groups on/off (Pad):");

        const float avail = ImGui::GetContentRegionAvail().x;
        const float clearBtnW = std::max(56.0f, ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f);
        const float inputW = 200.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float totalW = inputW + spacing + clearBtnW;
        SameLineIfFits(ImGui::GetCursorPosX() + avail - totalW - RFX_CARD_PAD);

        ImGui::SetNextItemWidth(inputW);
        uint32_t gpToggleAll = instance.GetGamepadToggleAll();
        if (gamepad_input_box("##ToggleAllGp", &gpToggleAll)) {
            instance.SetGamepadToggleAll(gpToggleAll);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear##ClearGpToggleAll", ImVec2(clearBtnW, 0))) {
            instance.SetGamepadToggleAll(0);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear gamepad shortcut");
        EndCard();
    }

    if (BeginCard("##cat_keys", "KEYBOARD HUNTING & SHORTCUTS")) {
        bool duplicateBinding = false;
        constexpr uint32_t totalKeybindCount = IM_ARRAYSIZE(AddonImGui::KeybindNames);

        if (ImGui::BeginTable("##keybinds_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp, ImVec2(-RFX_CARD_PAD, 0.0f))) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 240.0f);
            ImGui::TableHeadersRow();

            for (uint32_t i = 0; i < totalKeybindCount; i++) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(AddonImGui::KeybindDisplayNames[i]);

                ImGui::TableNextColumn();
                uint32_t keys = instance.GetKeybinding(static_cast<AddonImGui::Keybind>(i));
                ImGui::SetNextItemWidth(-1.0f);
                if (key_input_box(std::format("##kb_{}", i).c_str(), &keys, runtime))
                    instance.SetKeybinding(static_cast<AddonImGui::Keybind>(i), keys);

                if (keys != 0) {
                    for (uint32_t j = 0; j < i; ++j) {
                        if (instance.GetKeybinding(static_cast<AddonImGui::Keybind>(j)) == keys) {
                            duplicateBinding = true;
                            break;
                        }
                    }
                }
            }
            ImGui::EndTable();
        }

        if (duplicateBinding) {
            ImGui::Spacing();
            ImGui::TextColored(rfx_warn(), "Warning: two or more REST actions use the same shortcut.");
        }

        EndCard();
    }
}

static void DrawCategoryOptions(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    auto DrawRightAlignedRow = [](const char* label, const char* tooltip, auto drawControl, float ctrlW = 220.0f) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);

        const float avail = ImGui::GetContentRegionAvail().x;
        SameLineIfFits(ImGui::GetCursorPosX() + avail - ctrlW - RFX_CARD_PAD);
        ImGui::SetNextItemWidth(ctrlW);
        drawControl();
        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
        ImGui::Spacing();
    };

    if (BeginCard("##opt_hunting", "SHADER HUNTING & SELECTION")) {
        DrawRightAlignedRow("Overlay opacity:", "Opacity of the shader hunting overlay window.", [&]() {
            if (ImGui::SliderFloat("##OverlayOpacity", instance.OverlayOpacity(), 0.0f, 1.0f))
                instance.MarkConfigDirty();
        });

        DrawRightAlignedRow("# of frames to collect:", "The number of frames the addon will collect active shaders. Set this to a high number if the shader you want to mark is only used occasionally.", [&]() {
            if (ImGui::SliderInt("##FramesToCollect", instance.StartValueFramecountCollectionPhase(), 10, 1000))
                instance.MarkConfigDirty();
        });

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool showDraws = instance.GetShowObservedDraws();
        if (DrawToggleRow("Show observed draw calls during hunting", &showDraws,
                          "Displays the observed draw calls and geometry count (vertices / indices) for the currently selected hunted shader in the settings window.",
                          "Default: On")) {
            instance.SetShowObservedDraws(showDraws);
        }

        EndCard();
    }

    if (BeginCard("##opt_pipeline", "PIPELINE & RESOURCE COMPATIBILITY")) {
        std::string varSelectedItem = instance.GetResourceShim();
        DrawRightAlignedRow("Resource Shim:", "GPU resource shim to use for target capture.", [&]() {
            if (ImGui::BeginCombo("##ResourceShim", varSelectedItem.c_str(), ImGuiComboFlags_None)) {
                for (auto& v : Rendering::ResourceShimNames) {
                    bool is_selected = (varSelectedItem == v);
                    if (ImGui::Selectable(v.c_str(), is_selected)) {
                        varSelectedItem = v;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });
        instance.SetResourceShim(varSelectedItem);

        std::string varSelectedCopyMethod = instance.GetConstHookCopyType();
        DrawRightAlignedRow("Constant Buffer Copy Method:", "Intercept method for capturing constant buffer data.", [&]() {
            if (ImGui::BeginCombo("##ConstCopyType", varSelectedCopyMethod.c_str(), ImGuiComboFlags_None)) {
                for (auto& v : Shim::Constants::ConstantCopyTypeNames) {
                    bool is_selected = (varSelectedCopyMethod == v);
                    if (ImGui::Selectable(v.c_str(), is_selected)) {
                        varSelectedCopyMethod = v;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });
        instance.SetConstHookCopyType(varSelectedCopyMethod);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool trackDescriptors = instance.GetTrackDescriptors();
        if (DrawToggleRow("Track descriptors", &trackDescriptors,
                          "Enables descriptor set tracking for advanced shader binding matching.")) {
            instance.SetTrackDescriptors(trackDescriptors);
        }

        bool runtimeReload = instance.GetPreventRuntimeReload();
        if (DrawToggleRow("Prevent runtime reload", &runtimeReload,
                          "Prevents ReShade from reloading effects during runtime if the game swaps pipelines.")) {
            instance.SetPreventRuntimeReload(runtimeReload);
        }

        bool diagnosticLogs = instance.GetDiagnosticLogs();
        if (DrawToggleRow("Diagnostic logs", &diagnosticLogs,
                          "Writes detailed REST rendering diagnostics to ReShade.log: a per-second summary, what each group renders into,\n"
                          "staging and buffer recreation, group toggles and ReShade effect reloads. Leave off unless you're troubleshooting.",
                          diagnosticLogs ? "Logging to ReShade.log. Turn off when done; it adds overhead." : nullptr)) {
            instance.SetDiagnosticLogs(diagnosticLogs);
        }

        EndCard();
    }

    if (BeginCard("##opt_storage", "CONFIGURATION PERSISTENCE")) {
        const bool dirty = instance.IsConfigDirty();
        if (dirty) {
            ImGui::TextColored(rfx_warn(), "Status: Changes have not been written to ReshadeEffectShaderToggler.ini.");
        } else {
            ImGui::TextColored(rfx_ok(), "Status: Configuration file is up to date.");
        }

        ImGui::Spacing();
        if (!dirty) ImGui::BeginDisabled();
        if (ImGui::Button("Save Changes##OptSave")) {
            instance.SaveShaderTogglerIniFile();
        }
        if (!dirty) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Reload from ini##OptReload")) {
            instance.GetToggleGroupIdEffectEditing() = -1;
            instance.GetToggleGroupIdSettingsOpen() = -1;
            instance.GetToggleGroupIdShaderEditing() = -1;
            instance.GetToggleGroupIdConstantEditing() = -1;
            instance.StopHuntingMode();
            for (auto& [_, group] : instance.GetToggleGroups()) {
                instance.SignalToggleGroupRemoved(runtime, &group);
            }
            instance.RetireAllToggleGroups();
            instance.LoadShaderTogglerIniFile();
            instance.UpdateToggleGroupsForShaderHashes();
        }
        EndCard();
    }
}

static int s_currentRestCategory = CAT_GROUPS;

static void DisplayRestTab(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* runtime) {
    s_imguiWantTextInput.store(ImGui::GetIO().WantTextInput);
    RfxThemeScope theme;

    // Top Header Banner (regular layout items, so nothing can overlap the panes below)
    ImGui::Spacing();
    {
        size_t activeCount = 0;
        const auto& groups = instance.GetToggleGroups();
        for (const auto& [_, g] : groups) {
            if (g.isActive()) ++activeCount;
        }
        char status_str[64];
        snprintf(status_str, sizeof(status_str), "%zu Groups (%zu Active)", groups.size(), activeCount);
        const std::string ver_text = "v" REST_VERSION_STRING;
        const ImGuiStyle& style = ImGui::GetStyle();

        // Visible right edge of the window, independent of any horizontal content overflow.
        const float visible_right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - style.WindowPadding.x -
                                    ((ImGui::GetScrollMaxY() > 0.0f) ? style.ScrollbarSize : 0.0f);

        ImGui::TextColored(rfx_accent(), "Reshade Effect Shader Toggler");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ver_text.c_str());

        const float status_w = ImGui::CalcTextSize(status_str).x;
        const float status_x = visible_right - status_w;
        if (status_x >= ImGui::GetItemRectMax().x + style.ItemSpacing.x * 3.0f) {
            ImGui::SameLine(status_x - ImGui::GetWindowPos().x + ImGui::GetScrollX());
        }
        ImGui::TextColored(activeCount > 0 ? rfx_ok() : rfx_col(ImGuiCol_TextDisabled), "%s", status_str);
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Two-pane layout: Left Sidebar + Right Content Area
    float max_label_w = 0.0f;
    for (int i = 0; i < CAT_COUNT; ++i) {
        max_label_w = std::max(max_label_w, ImGui::CalcTextSize(s_restCategoryNames[i]).x);
    }
    const float sidebar_width = std::max(175.0f, max_label_w + 34.0f + 24.0f);

    {
        ImVec4 sb = rfx_col(ImGuiCol_FrameBg);
        sb.w *= 0.35f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, sb);
    }
    ImGui::BeginChild("##rest_sidebar", ImVec2(sidebar_width, 0), false);
    {
        for (int i = 0; i < CAT_COUNT; ++i) {
            if (DrawSidebarCategoryButton(i, s_restCategoryNames[i], s_currentRestCategory == i)) {
                s_currentRestCategory = i;
            }
        }

        const float footer_h = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y + 12.0f;
        if (ImGui::GetCursorPosY() < ImGui::GetWindowHeight() - footer_h) {
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footer_h);
        }
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reload INI", ImVec2(-1, 0))) {
            instance.GetToggleGroupIdEffectEditing() = -1;
            instance.GetToggleGroupIdSettingsOpen() = -1;
            instance.GetToggleGroupIdShaderEditing() = -1;
            instance.GetToggleGroupIdConstantEditing() = -1;
            instance.StopHuntingMode();
            for (auto& [_, group] : instance.GetToggleGroups()) {
                instance.SignalToggleGroupRemoved(runtime, &group);
            }
            instance.RetireAllToggleGroups();
            instance.LoadShaderTogglerIniFile();
            instance.UpdateToggleGroupsForShaderHashes();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Discard unsaved changes and reload from ReshadeEffectShaderToggler.ini");

        const bool dirty = instance.IsConfigDirty();
        if (!dirty) ImGui::BeginDisabled();
        if (ImGui::Button("Save Changes", ImVec2(-1, 0))) {
            instance.SaveShaderTogglerIniFile();
        }
        if (!dirty) ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save toggle groups and keybindings to ReshadeEffectShaderToggler.ini");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(1);

    ImGui::SameLine();

    // Right Content Area
    ImGui::BeginChild("##rest_content", ImVec2(0, 0), false);
    {
        static const char* const cat_titles[CAT_COUNT] = {
            "Toggle Groups",
            "Keybindings & Shortcuts",
            "General & Pipeline Options"
        };

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::Indent(6.0f);
        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextUnformatted(cat_titles[s_currentRestCategory]);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Unindent(6.0f);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        switch (s_currentRestCategory) {
        case CAT_GROUPS:
            DrawCategoryGroups(instance, runtime);
            break;
        case CAT_KEYBINDINGS:
            DrawCategoryKeybindings(instance, runtime);
            break;
        case CAT_OPTIONS:
            DrawCategoryOptions(instance, runtime);
            break;
        default:
            break;
        }
    }
    ImGui::EndChild();
}

static void DisplayAddonsTab(AddonImGui::AddonUIData& instance, reshade::api::effect_runtime* /*runtime*/) {
    RfxThemeScope theme;

    ImGui::Spacing();
    ImGui::TextWrapped("REST now has its own dedicated tab ('REST') docked at the top of the ReShade overlay window.");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("All toggle groups, shader hunting, keybindings, and options are configured there.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (BeginCard("##quick_status", "QUICK OVERVIEW")) {
        const auto& groups = instance.GetToggleGroups();
        size_t activeCount = 0;
        for (const auto& [_, g] : groups) {
            if (g.isActive()) ++activeCount;
        }

        ImGui::BulletText("Toggle Groups: %zu configured (%zu active)", groups.size(), activeCount);

        const int editingId = instance.GetToggleGroupIdShaderEditing();
        if (editingId >= 0 && groups.find(editingId) != groups.end()) {
            ImGui::BulletText("Shader Hunting: Active (Editing '%s')", groups.at(editingId).getName().c_str());
        } else {
            ImGui::BulletText("Shader Hunting: Inactive");
        }

        auto& gp = ShaderToggler::GamepadMonitor::getInstance();
        if (gp.isConnected()) {
            ImGui::BulletText("Controller: Connected (User %d)", gp.getActiveUserIndex());
        } else {
            ImGui::BulletText("Controller: Not connected");
        }

        ImGui::BulletText("Configuration: %s", instance.IsConfigDirty() ? "Unsaved changes" : "Saved to disk");
        EndCard();
    }

    ImGui::Spacing();
    if (ImGui::Button("Discord", ImVec2(140.0f, 0))) {
        ShellExecuteA(NULL, "open", REST_DISCORD_URL, NULL, NULL, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Join Baboon's Workshop on Discord");
}