#pragma once
#include "AddonUIData.h"
#include "CDataFile.h"
#include "ShaderManager.h"
#include "ToggleGroup.h"
#include <imgui.h>
#include <ranges>
#include <reshade.hpp>
#include <unordered_map>

static const std::unordered_set<std::string> varExclusionSet({ "frametime",
                                                               "framecount",
                                                               "random",
                                                               "pingpong",
                                                               "date",
                                                               "timer",
                                                               "key",
                                                               "mousepoint",
                                                               "mousedelta",
                                                               "mousebutton",
                                                               "mousewheel",
                                                               "ui_open",
                                                               "overlay_open",
                                                               "ui_active",
                                                               "overlay_active",
                                                               "ui_hovered",
                                                               "overlay_hovered" });

static void DisplayConstantSettings(ShaderToggler::ToggleGroup* group) {
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Slot");
    ImGui::TableNextColumn();
    DrawTableStepper("cb_slot",
        [&]() { return group->getCBSlotIndex(); },
        [&](uint32_t val) { group->setCBSlotIndex(val); });

    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Binding");
    ImGui::TableNextColumn();
    {
        char valBuf[16];
        uint32_t val = group->getCBDescriptorIndex();
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
        if (ImGui::Button("-##cb_desc_dec", ImVec2(btnW, 0))) {
            group->dispatchCBCycle(ShaderToggler::CYCLE_DOWN);
        }
        if (val == 0) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("+##cb_desc_inc", ImVec2(btnW, 0))) {
            group->dispatchCBCycle(ShaderToggler::CYCLE_UP);
        }
    }
}

static void DisplayConstantTab(AddonImGui::AddonUIData& instance, ShaderToggler::ToggleGroup* group, reshade::api::device* dev) {
    if (instance.GetConstantHandler() == nullptr) {
        return;
    }

    std::shared_lock<std::shared_mutex> lock(instance.GetConstantHandler()->GetBufferMutex());

    const uint32_t columns = 4;
    const char* typeItems[] = { "byte", "float", "int", "uint" };
    const char* cbModeItems[] = { "BUFFER", "PUSH" };
    const uint32_t typeSizes[] = { 1, 4, 4, 4 };
    static int typeSelectionIndex = 1;
    static const char* typeSelectedItem = typeItems[1];
    int cbModeSelectionIndex = group->getCBIsPushMode() ? 1 : 0;
    const char* cbModeSelection = cbModeItems[cbModeSelectionIndex];

    const uint8_t* bufferContent = instance.GetConstantHandler()->GetConstantBuffer(group);
    const size_t bufferSize = instance.GetConstantHandler()->GetConstantBufferSize(group);
    auto& varMap = group->GetVarOffsetMapping();
    const size_t offsetInputBufSize = 32;
    static char offsetInputBuf[offsetInputBufSize] = { "000" };

    static const char* stageItems[] = { "PIXEL", "VERTEX", "COMPUTE" };
    uint32_t selectedStageIndex = group->getCBShaderStage();
    const char* selectedStage = stageItems[selectedStageIndex];

    bool extractionEnabled = group->getExtractConstants();

    if (BeginCard("##cb_config_card", "CONSTANT BUFFER EXTRACTION")) {
        if (!instance.GetTrackDescriptors()) {
            ImGui::BeginDisabled();
        }

        const float labelColWidth = std::max(200.0f, ImGui::CalcTextSize("Extract constant buffer   ").x);
        if (ImGui::BeginTable("ConstantBufferSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody, ImVec2(-RFX_CARD_PAD, 0.0f))) {
            ImGui::TableSetupColumn("##CBcolumnsetup", ImGuiTableColumnFlags_WidthFixed, labelColWidth);
            ImGui::TableSetupColumn("##CBcontrols", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Extract constant buffer");
            ImGui::TableNextColumn();
            DrawTableToggleSwitch("##Extractconstantbuffer", &extractionEnabled);

            if (!extractionEnabled) {
                instance.GetConstantHandler()->RemoveGroup(group, dev);
                ImGui::BeginDisabled();
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("View mode");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##Viewmode", typeSelectedItem, ImGuiComboFlags_None)) {
                for (int n = 0; n < IM_ARRAYSIZE(typeItems); n++) {
                    bool is_selected = (typeSelectedItem == typeItems[n]);
                    if (ImGui::Selectable(typeItems[n], is_selected)) {
                        typeSelectionIndex = n;
                        typeSelectedItem = typeItems[n];
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
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

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Constant mode");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##CBmode", cbModeSelection, ImGuiComboFlags_None)) {
                for (int n = 0; n < IM_ARRAYSIZE(cbModeItems); n++) {
                    bool is_selected = (cbModeSelection == cbModeItems[n]);
                    if (ImGui::Selectable(cbModeItems[n], is_selected)) {
                        cbModeSelectionIndex = n;
                        cbModeSelection = cbModeItems[n];
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TableNextRow();

            DisplayConstantSettings(group);

            if (!extractionEnabled) {
                ImGui::EndDisabled();
            }

            if (!instance.GetTrackDescriptors()) {
                ImGui::EndDisabled();
            }

            ImGui::EndTable();
        }
        group->setExtractConstant(extractionEnabled);
        group->setCBIsPushMode(cbModeSelectionIndex == 1);
        group->setCBShaderStage(selectedStageIndex);

        EndCard();
    }

    ImGui::Spacing();

    if (BeginCard("##cb_data_card", "CONSTANT BUFFER DATA")) {
        if (bufferContent != nullptr && bufferSize > 0) {
            const float gridHeight = 200.0f;
            if (ImGui::BeginTable("Buffer View Grid##table",
                                  columns + 1,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                                    ImGuiTableFlags_RowBg,
                                  ImVec2(-RFX_CARD_PAD, gridHeight))) {
                size_t elements = bufferSize / typeSizes[typeSelectionIndex];

                ImGui::TableSetupScrollFreeze(0, 1);
                for (int i = 0; i < columns + 1; i++) {
                    if (i == 0) {
                        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
                    } else {
                        ImGui::TableSetupColumn(std::format("{:#04x}", (i - 1) * typeSizes[typeSelectionIndex]).c_str(), ImGuiTableColumnFlags_None);
                    }
                }

                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;

                double clipElements =
                  (static_cast<double>(elements) + static_cast<double>(elements) / static_cast<double>(columns)) / static_cast<double>(columns + 1);
                clipper.Begin(static_cast<int>(std::ceil(clipElements)));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                        for (int i = row * (columns + 1); i < row * static_cast<ptrdiff_t>(columns + 1) + static_cast<ptrdiff_t>(columns + 1); i++) {
                            if (i % (columns + 1) == 0) {
                                ImGui::TableNextColumn();
                                ImGui::TableHeader(std::format("{:#05x}", i / (columns + 1) * typeSizes[typeSelectionIndex] * columns).c_str());
                                continue;
                            }

                            std::stringstream sContent;

                            if (typeSelectionIndex == 0) {
                                sContent << std::format("{:02X}", bufferContent[i - i / (columns + 1) - 1]) << std::endl;
                            } else {
                                uint32_t bufferOffset = (i - i / (columns + 1) - 1) * typeSizes[typeSelectionIndex];

                                switch (typeSelectionIndex) {
                                    case 1:
                                        sContent << std::format("{:.8f}", *(reinterpret_cast<const float*>(&bufferContent[bufferOffset]))) << std::endl;
                                        break;
                                    case 2:
                                        sContent << *(reinterpret_cast<const int32_t*>(&bufferContent[bufferOffset])) << std::endl;
                                        break;
                                    case 3:
                                        sContent << *(reinterpret_cast<const uint32_t*>(&bufferContent[bufferOffset])) << std::endl;
                                        break;
                                }
                            }

                            ImGui::TableNextColumn();
                            const auto& txt = sContent.str();
                            ImGui::Text(txt.c_str());
                        }
                    }
                }
                clipper.End();

                ImGui::EndTable();
            }
        } else {
            ImGui::Spacing();
            ImGui::TextDisabled(extractionEnabled ? "No constant buffer data intercepted yet for this group." : "Enable constant buffer extraction above to view buffer data.");
        }

        EndCard();
    }

    ImGui::Spacing();

    if (BeginCard("##cb_vars_card", "VARIABLE MAPPINGS")) {
        const float addBtnW = ImGui::CalcTextSize("Add Variable Binding").x + ImGui::GetStyle().FramePadding.x * 2.0f + 14.0f;
        if (!extractionEnabled) {
            instance.GetConstantHandler()->RemoveGroup(group, dev);
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Add Variable Binding", ImVec2(addBtnW, 0))) {
            ImGui::OpenPopup("Add###const_variables");
        }

        if (!extractionEnabled) {
            ImGui::EndDisabled();
        }

        ImGui::Separator();

        if (ImGui::BeginPopupModal("Add###const_variables", nullptr, ImGuiWindowFlags_AlwaysAutoResize) && instance.GetRESTVariables()->size() > 0) {
            ImGui::Text("Add constant buffer offset to variable binding:");

            static int varSelectionIndex = 0;
            std::vector<std::string> varNames;
            std::transform(
              instance.GetRESTVariables()->begin(),
              instance.GetRESTVariables()->end(),
              std::back_inserter(varNames),
              [](const std::pair<std::string, std::tuple<Shim::Constants::constant_type, std::vector<reshade::api::effect_uniform_variable>>>& kV) {
                  return kV.first;
              });
            std::vector<std::string> filteredVars;
            std::copy_if(varNames.begin(), varNames.end(), std::back_inserter(filteredVars), [](const std::string& s) { return !varExclusionSet.contains(s); });

            static std::string varSelectedItem = filteredVars.size() > 0 ? filteredVars[0] : "";

            if (ImGui::BeginCombo("Variable", varSelectedItem.c_str(), ImGuiComboFlags_None)) {
                for (auto& v : filteredVars) {
                    bool is_selected = (varSelectedItem == v);
                    if (ImGui::Selectable(v.c_str(), is_selected)) {
                        varSelectedItem = v;
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Text("0x");
            ImGui::SameLine();
            ImGui::InputText("Offset", offsetInputBuf, offsetInputBufSize, ImGuiInputTextFlags_CharsHexadecimal);

            static bool prevValue = false;
            ImGui::Spacing();
            DrawToggleRow("Use previous value", &prevValue, "Sample constant value from previous frame", nullptr, 4.0f);
            ImGui::Spacing();

            ImGui::Separator();

            ImGui::SetCursorPosX(ImGui::GetWindowWidth() / 2 - 120 - ImGui::GetStyle().ItemSpacing.x / 2 - ImGui::GetStyle().FramePadding.x / 2);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                if (varSelectedItem.size() > 0) {
                    group->SetVarMapping(std::stoul(std::string(offsetInputBuf), nullptr, 16), varSelectedItem, prevValue);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        const char* varColumns[] = { "Variable", "Offset", "Type", "Use Previous Value" };
        std::vector<std::string> removal;

        if (varMap.size() > 0 &&
            ImGui::BeginTable("Buffer View Grid##vartable",
                              IM_ARRAYSIZE(varColumns) + 1,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoBordersInBody,
                              ImVec2(-RFX_CARD_PAD, 150.0f))) {
            for (int i = 0; i < IM_ARRAYSIZE(varColumns); i++) {
                ImGui::TableSetupColumn(varColumns[i], ImGuiTableColumnFlags_None);
            }

            ImGui::TableHeadersRow();

            for (const auto& [varName, varData] : varMap) {
                if (!instance.GetRESTVariables()->contains(varName))
                    continue;
                const auto& [varOffset, varEnabled] = varData;
                ImGui::TableNextColumn();
                ImGui::Text(varName.c_str());
                ImGui::TableNextColumn();
                ImGui::Text(std::format("{:#05x}", varOffset).c_str());
                ImGui::TableNextColumn();
                ImGui::Text(Shim::Constants::type_desc[static_cast<uint32_t>(std::get<0>(instance.GetRESTVariables()->at(varName)))]);
                ImGui::TableNextColumn();
                ImGui::Text(std::format("{}", varEnabled).c_str());
                ImGui::TableNextColumn();
                if (ImGui::Button(std::format("Remove##{}", varName).c_str())) {
                    removal.push_back(varName);
                }
            }

            ImGui::EndTable();
        } else if (varMap.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No variable bindings configured for this group.");
        }

        std::for_each(removal.begin(), removal.end(), [&group](std::string& e) { group->RemoveVarMapping(e); });

        EndCard();
    }
}
