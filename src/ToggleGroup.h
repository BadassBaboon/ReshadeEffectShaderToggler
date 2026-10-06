///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for Reshade 5+ which allows you
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

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <imgui.h>
#include "CDataFile.h"
#include "EffectData.h"
#include "GlobalResourceView.h"
#include "reshade.hpp"

namespace ShaderToggler {
enum DescriptorCycle { CYCLE_NONE, CYCLE_UP, CYCLE_DOWN };

enum SwapChainMatchMode : uint32_t {
    SWAPCHAIN_MATCH_MODE_RESOLUTION = 0,
    SWAPCHAIN_MATCH_MODE_ASPECT_RATIO = 1,
    SWAPCHAIN_MATCH_MODE_EXTENDED_ASPECT_RATIO = 2,
    SWAPCHAIN_MATCH_MODE_NONE = 3
};

constexpr bool IsAutoSceneColourSupported(reshade::api::device_api api) {
    return api == reshade::api::device_api::d3d10 ||
           api == reshade::api::device_api::d3d11 ||
           api == reshade::api::device_api::d3d12 ||
           api == reshade::api::device_api::vulkan;
}

static_assert(IsAutoSceneColourSupported(reshade::api::device_api::d3d10));
static_assert(IsAutoSceneColourSupported(reshade::api::device_api::d3d11));
static_assert(IsAutoSceneColourSupported(reshade::api::device_api::d3d12));
static_assert(IsAutoSceneColourSupported(reshade::api::device_api::vulkan));
static_assert(!IsAutoSceneColourSupported(reshade::api::device_api::d3d9));

// ReShade resource handles are explicitly 64-bit on both Win32 and x64.
// Keep this invariant visible so 32-bit builds cannot silently narrow them.
static_assert(sizeof(reshade::api::resource) == sizeof(uint64_t));
static_assert(sizeof(reshade::api::resource_view) == sizeof(uint64_t));
static_assert(sizeof(uintptr_t) == sizeof(void*));

enum class GroupResourceType : uint32_t { RESOURCE_ALPHA = 0, RESOURCE_BINDING = 1, RESOURCE_CONSTANTS_COPY = 2, RESOURCE_NATIVE_STAGING = 3 };

enum class GroupResourceState : uint32_t {
    RESOURCE_VALID = 1,
    RESOURCE_INVALID = 2,
    RESOURCE_RECREATED = 4,
    RESOURCE_CLEARED = 8,
};

constexpr uint32_t GroupResourceTypeCount = 4;

struct AutoDiagnosticEntry {
    uint64_t candidateId = 0;
    uint32_t shaderHash = 0;
    std::string status;
    uint64_t target = 0;
    uint32_t sceneWidth = 0;
    uint32_t sceneHeight = 0;
    std::string format;
    std::string boundary;
    uint64_t successfulRenders = 0;
};

struct __declspec(novtable) GroupResource final {
    reshade::api::resource res;
    reshade::api::format view_format;
    reshade::api::resource_view rtv;
    reshade::api::resource_view rtv_srgb;
    reshade::api::resource_view srv;
    std::shared_ptr<Rendering::GlobalResourceView> g_res;
    reshade::api::resource_desc target_description;
    std::function<bool()> enabled;
    std::function<bool()> clear_on_miss;
    GroupResourceState state;
    bool owning;
};

class ToggleGroup {
  public:
    ToggleGroup(std::string name, int Id);
    ToggleGroup();
    ToggleGroup(const ToggleGroup& other);
    ToggleGroup cloneForNewId(int newId) const;
    std::string configurationSignature() const;

    static int getNewGroupId();

    void setToggleKey(uint32_t keybind) { if (_keybind != keybind) { _keybind = keybind; markConfigDirty(); } }
    void setName(std::string newName);
    /// <summary>
    /// Writes the shader hashes, name and toggle key to the ini file specified, using a Group + groupCounter section.
    /// </summary>
    /// <param name="iniFile"></param>
    /// <param name="groupCounter"></param>
    void saveState(CDataFile& iniFile, int groupCounter) const;
    /// <summary>
    /// Loads the shader hashes, name and toggle key from the ini file specified, using a Group + groupCounter section.
    /// </summary>
    /// <param name="iniFile"></param>
    /// <param name="groupCounter">if -1, the ini file is in the pre-1.0 format</param>
    void loadState(CDataFile& iniFile, int groupCounter);
    void storeCollectedHashes(const std::unordered_set<uint32_t> pixelShaderHashes,
                              const std::unordered_set<uint32_t> vertexShaderHashes,
                              const std::unordered_set<uint32_t> computeShaderHashes);
    bool isBlockedVertexShader(uint32_t shaderHash) const;
    bool isBlockedPixelShader(uint32_t shaderHash) const;
    bool isBlockedComputeShader(uint32_t shaderHash) const;
    void clearHashes();

    void toggleActive() { _isActive = !_isActive; markConfigDirty(); }
    void setActive(bool isActive) { if (_isActive != isActive) { _isActive = isActive; markConfigDirty(); } }
    void setEditing(bool isEditing) { _isEditing = isEditing; }

    uint32_t getToggleKey() const { return _keybind; }
    const std::string& getName() const { return _name; }
    bool isActive() const { return _isActive; }
    bool isEditing() const { return _isEditing; }
    /// <summary>
    /// True once the group has been removed in the UI. Retired groups are kept alive (see
    /// AddonUIData::_retiredToggleGroups) until device teardown, but must not be treated as
    /// live groups by the render threads that may still hold a stale pointer to them.
    /// </summary>
    bool isRetired() const { return _isRetired; }
    void setRetired(bool isRetired = true) { _isRetired = isRetired; }
    bool isEmpty() const { return _vertexShaderHashes.size() <= 0 && _pixelShaderHashes.size() <= 0; }
    int getId() const { return _id; }
    void setId(int id) { _id = id; }
    const std::unordered_set<std::string>& preferredTechniques() const { return _preferredTechniques; }
    void setPreferredTechniques(const std::unordered_set<std::string>& techniques) { if (_preferredTechniques != techniques) { _preferredTechniques = techniques; markConfigDirty(); } }
    const std::unordered_set<uint32_t>& getPixelShaderHashes() const { return _pixelShaderHashes; }
    const std::unordered_set<uint32_t>& getVertexShaderHashes() const { return _vertexShaderHashes; }
    const std::unordered_set<uint32_t>& getComputeShaderHashes() const { return _computeShaderHashes; }
    size_t getPixelShaderHashCount() const { return _pixelShaderHashes.size(); }
    size_t getVertexShaderHashCount() const { return _vertexShaderHashes.size(); }
    size_t getComputeShaderHashCount() const { return _computeShaderHashes.size(); }
    void setInvocationLocation(uint32_t location) { if (_invocationLocation != location) { _invocationLocation = location; markConfigDirty(); } }
    uint32_t getInvocationLocation() const { return _invocationLocation; }
    void setBindingInvocationLocation(uint32_t location) { if (_bindingInvocationLocation != location) { _bindingInvocationLocation = location; markConfigDirty(); } }
    uint32_t getBindingInvocationLocation() const { return _bindingInvocationLocation; }
    void setCBSlotIndex(uint32_t index) { if (_cbSlotIndex != index) { _cbSlotIndex = index; markConfigDirty(); } }
    uint32_t getCBSlotIndex() const { return _cbSlotIndex; }
    void setCBDescriptorIndex(uint32_t index) { if (_cbDescIndex != index) { _cbDescIndex = index; markConfigDirty(); } }
    uint32_t getCBDescriptorIndex() const { return _cbDescIndex; }
    bool getCBIsPushMode() const { return _cbModePush; }
    void setCBIsPushMode(bool isPushMode) { if (_cbModePush != isPushMode) { _cbModePush = isPushMode; markConfigDirty(); } }
    void setRenderTargetIndex(uint32_t index) { if (_rtIndex != index) { _rtIndex = index; markConfigDirty(); } }
    uint32_t getRenderTargetIndex() const { return _rtIndex; }
    bool isProvidingTextureBinding() const { return _isProvidingTextureBinding; }
    void setProvidingTextureBinding(bool value) { if (_isProvidingTextureBinding != value) { _isProvidingTextureBinding = value; markConfigDirty(); } }
    const std::string& getTextureBindingName() const { return _textureBindingName; }
    void setTextureBindingName(std::string value) { if (_textureBindingName != value) { _textureBindingName = std::move(value); markConfigDirty(); } }
    bool getClearBindings() { return _clearBindings; }
    void setClearBindings(bool clear) { if (_clearBindings != clear) { _clearBindings = clear; markConfigDirty(); } }
    bool getAllowAllTechniques() const { return _allowAllTechniques; }
    void setAllowAllTechniques(bool value) { if (_allowAllTechniques != value) { _allowAllTechniques = value; markConfigDirty(); } }
    bool getExtractConstants() const { return _extractConstants; }
    void setExtractConstant(bool extract) { if (_extractConstants != extract) { _extractConstants = extract; markConfigDirty(); } }
    uint32_t getCBShaderStage() const { return _cbShaderStage; }
    void setCBShaderStage(uint32_t value) { if (_cbShaderStage != value) { _cbShaderStage = value; markConfigDirty(); } }
    bool getExtractResourceViews() const { return _extractResourceViews; }
    void setExtractResourceViews(bool extract) { if (_extractResourceViews != extract) { _extractResourceViews = extract; markConfigDirty(); } }
    bool getRenderToResourceViews() const { return _renderToResourceViews; }
    void setRenderToResourceViews(bool render) { if (_renderToResourceViews != render) { _renderToResourceViews = render; markConfigDirty(); } }
    bool getAutoRenderSRV() const { return _autoRenderSRV; }
    void setAutoRenderSRV(bool value) { if (_autoRenderSRV != value) { _autoRenderSRV = value; markConfigDirty(); } }
    bool getHideMarkedShaders() const { return _hideMarkedShaders; }
    void setHideMarkedShaders(bool value) { if (_hideMarkedShaders != value) { _hideMarkedShaders = value; markConfigDirty(); } }
    bool isAutoSceneColourActive(reshade::api::device_api api) const {
        return _autoRenderSRV && IsAutoSceneColourSupported(api);
    }

    uint64_t getDebugEffectRenderCalls() const { return _debugEffectRenderCalls; }
    uint32_t getDebugLastRenderedTechniqueCount() const { return _debugLastRenderedTechniqueCount; }
    uint64_t getDebugLastRenderTarget() const { return _debugLastRenderTarget; }
    uint32_t getDebugSceneWidth() const { return _debugSceneWidth; }
    uint32_t getDebugSceneHeight() const { return _debugSceneHeight; }
    uint32_t getDebugEffectWidth() const { return _debugEffectWidth; }
    uint32_t getDebugEffectHeight() const { return _debugEffectHeight; }
    bool getDebugNativeStaging() const { return _debugNativeStaging; }
    const std::string& getDebugLastTechniqueOrder() const { return _debugLastTechniqueOrder; }
    const std::string& getDebugLastVulkanBoundary() const { return _debugLastVulkanBoundary; }
    void setDebugLastVulkanBoundary(const std::string& boundary) {
        _debugLastVulkanBoundary = boundary;
        updateDebugCandidate(_debugAutoStatus, boundary);
    }

    const std::string& getDebugAutoStatus() const { return _debugAutoStatus; }
    void setDebugAutoStatus(const std::string& status) {
        _debugAutoStatus = status;
        updateDebugCandidate(status);
    }
    uint64_t getDebugCurrentTarget() const { return _debugCurrentTarget; }
    uint32_t getDebugCurrentSceneWidth() const { return _debugCurrentSceneWidth; }
    uint32_t getDebugCurrentSceneHeight() const { return _debugCurrentSceneHeight; }
    const std::string& getDebugCurrentFormat() const { return _debugCurrentFormat; }
    void setDebugCurrentShaderHash(uint32_t hash) { _debugCurrentShaderHash = hash; }
    std::vector<AutoDiagnosticEntry> getDebugAutoHistory() const {
        std::lock_guard lock(*_debugHistoryMutex);
        return { _debugAutoHistory.begin(), _debugAutoHistory.end() };
    }

    void recordDebugAutoTarget(uint64_t targetHandle,
                               uint32_t sceneWidth,
                               uint32_t sceneHeight,
                               const std::string& formatName) {
        _debugCurrentTarget = targetHandle;
        _debugCurrentSceneWidth = sceneWidth;
        _debugCurrentSceneHeight = sceneHeight;
        _debugCurrentFormat = formatName;
    }

    void resetDebugAutoDiagnostics() {
        _debugAutoStatus.clear();
        _debugCurrentTarget = 0;
        _debugCurrentSceneWidth = 0;
        _debugCurrentSceneHeight = 0;
        _debugCurrentFormat.clear();
        _debugCurrentShaderHash = 0;
        {
            std::lock_guard lock(*_debugHistoryMutex);
            _debugAutoHistory.clear();
            _debugHistorySequence = 0;
        }

        _debugEffectRenderCalls = 0;
        _debugLastRenderedTechniqueCount = 0;
        _debugLastRenderTarget = 0;
        _debugLastTechniqueOrder.clear();
        _debugLastVulkanBoundary.clear();
        _debugSceneWidth = 0;
        _debugSceneHeight = 0;
        _debugEffectWidth = 0;
        _debugEffectHeight = 0;
        _debugNativeStaging = false;
    }
    void recordDebugEffectRender(uint32_t techniqueCount,
                                 uint64_t targetHandle,
                                 const std::string& techniqueOrder,
                                 uint32_t sceneWidth,
                                 uint32_t sceneHeight,
                                 uint32_t effectWidth,
                                 uint32_t effectHeight,
                                 bool nativeStaging) {
        ++_debugEffectRenderCalls;
        _debugLastRenderedTechniqueCount = techniqueCount;
        _debugLastRenderTarget = targetHandle;
        _debugLastTechniqueOrder = techniqueOrder;
        _debugSceneWidth = sceneWidth;
        _debugSceneHeight = sceneHeight;
        _debugEffectWidth = effectWidth;
        _debugEffectHeight = effectHeight;
        _debugNativeStaging = nativeStaging;
        _debugAutoStatus = "Successful";
        updateDebugCandidate("Successful", {}, true);
    }

    void setBindingSRVSlotIndex(uint32_t index) { if (_bindingSrvSlotIndex != index) { _bindingSrvSlotIndex = index; markConfigDirty(); } }
    uint32_t getBindingSRVSlotIndex() const { return _bindingSrvSlotIndex; }
    void setRenderSRVSlotIndex(uint32_t index) { if (_renderSrvSlotIndex != index) { _renderSrvSlotIndex = index; markConfigDirty(); } }
    uint32_t getRenderSRVSlotIndex() const { return _renderSrvSlotIndex; }
    void setBindingSRVDescriptorIndex(uint32_t index) { if (_bindingSrvDescIndex != index) { _bindingSrvDescIndex = index; markConfigDirty(); } }
    uint32_t getBindingSRVDescriptorIndex() const { return _bindingSrvDescIndex; }
    void setRenderSRVDescriptorIndex(uint32_t index) { if (_renderSrvDescIndex != index) { _renderSrvDescIndex = index; markConfigDirty(); } }
    uint32_t getRenderSRVDescriptorIndex() const { return _renderSrvDescIndex; }
    uint32_t getSRVShaderStage() const { return _bindingSrvShaderStage; }
    void setSRVShaderStage(uint32_t shaderStage) { if (_bindingSrvShaderStage != shaderStage) { _bindingSrvShaderStage = shaderStage; markConfigDirty(); } }
    uint32_t getRenderSRVShaderStage() const { return _renderSrvShaderStage; }
    void setRenderSRVShaderStage(uint32_t shaderStage) { if (_renderSrvShaderStage != shaderStage) { _renderSrvShaderStage = shaderStage; markConfigDirty(); } }
    void setBindingRenderTargetIndex(uint32_t index) { if (_bindingRTIndex != index) { _bindingRTIndex = index; markConfigDirty(); } }
    uint32_t getBindingRenderTargetIndex() const { return _bindingRTIndex; }
    bool getHasTechniqueExceptions() const { return _hasTechniqueExceptions; }
    void setHasTechniqueExceptions(bool exceptions) { if (_hasTechniqueExceptions != exceptions) { _hasTechniqueExceptions = exceptions; markConfigDirty(); } }
    uint32_t getMatchSwapchainResolution() const { return _matchSwapchainResolution; }
    void setMatchSwapchainResolution(uint32_t match) { if (_matchSwapchainResolution != match) { _matchSwapchainResolution = match; markConfigDirty(); } }
    uint32_t getBindingMatchSwapchainResolution() const { return _bindingMatchSwapchainResolution; }
    void setBindingMatchSwapchainResolution(uint32_t match) { if (_bindingMatchSwapchainResolution != match) { _bindingMatchSwapchainResolution = match; markConfigDirty(); } }
    bool getRequeueAfterRTMatchingFailure() const { return _requeueAfterRTMatchingFailure; }
    void setRequeueAfterRTMatchingFailure(bool requeue) { if (_requeueAfterRTMatchingFailure != requeue) { _requeueAfterRTMatchingFailure = requeue; markConfigDirty(); } }
    bool getCopyTextureBinding() const { return _copyTextureBinding; }
    void setCopyTextureBinding(bool copy) { if (_copyTextureBinding != copy) { _copyTextureBinding = copy; markConfigDirty(); } }
    const std::unordered_map<std::string, std::tuple<uintptr_t, bool>>& GetVarOffsetMapping() const { return _varOffsetMapping; }
    bool SetVarMapping(uintptr_t, std::string&, bool);
    bool RemoveVarMapping(std::string&);
    bool getClearPreviewAlpha() const { return _previewClearAlpha; }
    void setClearPreviewAlpha(bool previewClearAlpha) { if (_previewClearAlpha != previewClearAlpha) { _previewClearAlpha = previewClearAlpha; markConfigDirty(); } }
    bool getToneMap() const { return _tonemapHDRtoSDRtoHDR; }
    void setToneMap(bool tonemap) { if (_tonemapHDRtoSDRtoHDR != tonemap) { _tonemapHDRtoSDRtoHDR = tonemap; markConfigDirty(); } }
    bool getPreserveAlpha() const { return _preserveAlpha; }
    void setPreserveAlpha(bool alpha) { if (_preserveAlpha != alpha) { _preserveAlpha = alpha; markConfigDirty(); } }
    bool getFlipBuffer() const { return _flipBuffer; }
    void setFlipBuffer(bool flip) { if (_flipBuffer != flip) { _flipBuffer = flip; markConfigDirty(); } }
    bool getFlipBufferBinding() const { return _flipBufferBinding; }
    void setFlipBufferBinding(bool flip) { if (_flipBufferBinding != flip) { _flipBufferBinding = flip; markConfigDirty(); } }
    bool getSuppressDrawCall() const { return _suppressDrawCall; }
    void setSuppressDrawCall(bool suppress) { if (_suppressDrawCall != suppress) { _suppressDrawCall = suppress; markConfigDirty(); } }
    uint32_t getGamepadShortcut() const { return _gamepadShortcut; }
    void setGamepadShortcut(uint32_t shortcut) { if (_gamepadShortcut != shortcut) { _gamepadShortcut = shortcut; markConfigDirty(); } }

    bool getMatchVertexCount() const { return _matchVertexCount; }
    void setMatchVertexCount(bool match) { if (_matchVertexCount != match) { _matchVertexCount = match; markConfigDirty(); } }
    uint32_t getVertexCountMin() const { return _vertexCountMin; }
    void setVertexCountMin(uint32_t min) { if (_vertexCountMin != min) { _vertexCountMin = min; markConfigDirty(); } }
    uint32_t getVertexCountMax() const { return _vertexCountMax; }
    void setVertexCountMax(uint32_t max) { if (_vertexCountMax != max) { _vertexCountMax = max; markConfigDirty(); } }

    bool getMatchIndexCount() const { return _matchIndexCount; }
    void setMatchIndexCount(bool match) { if (_matchIndexCount != match) { _matchIndexCount = match; markConfigDirty(); } }
    uint32_t getIndexCountMin() const { return _indexCountMin; }
    void setIndexCountMin(uint32_t min) { if (_indexCountMin != min) { _indexCountMin = min; markConfigDirty(); } }
    uint32_t getIndexCountMax() const { return _indexCountMax; }
    void setIndexCountMax(uint32_t max) { if (_indexCountMax != max) { _indexCountMax = max; markConfigDirty(); } }

    bool getMatchInstanceCount() const { return _matchInstanceCount; }
    void setMatchInstanceCount(bool match) { if (_matchInstanceCount != match) { _matchInstanceCount = match; markConfigDirty(); } }
    uint32_t getInstanceCountMin() const { return _instanceCountMin; }
    void setInstanceCountMin(uint32_t min) { if (_instanceCountMin != min) { _instanceCountMin = min; markConfigDirty(); } }
    uint32_t getInstanceCountMax() const { return _instanceCountMax; }
    void setInstanceCountMax(uint32_t max) { if (_instanceCountMax != max) { _instanceCountMax = max; markConfigDirty(); } }

    bool hasGeometryFilter() const {
        return _matchVertexCount || _matchIndexCount || _matchInstanceCount;
    }

    bool matchesDrawGeometry(bool isIndexed, uint32_t count, uint32_t instanceCount = 1) const {
        if (!hasGeometryFilter()) {
            return true;
        }

        if (_matchInstanceCount) {
            if (_instanceCountMin > 0 && instanceCount < _instanceCountMin) return false;
            if (_instanceCountMax > 0 && instanceCount > _instanceCountMax) return false;
        }

        if (isIndexed) {
            if (_matchIndexCount) {
                if (_indexCountMin > 0 && count < _indexCountMin) return false;
                if (_indexCountMax > 0 && count > _indexCountMax) return false;
                return true;
            }
            return !_matchVertexCount;
        } else {
            if (_matchVertexCount) {
                if (_vertexCountMin > 0 && count < _vertexCountMin) return false;
                if (_vertexCountMax > 0 && count > _vertexCountMax) return false;
                return true;
            }
            return !_matchIndexCount;
        }
    }

    void dispatchCBCycle(DescriptorCycle cycle) { _cbCycle = cycle; }
    DescriptorCycle consumeCBCycle() {
        DescriptorCycle ret = _cbCycle;
        _cbCycle = CYCLE_NONE;
        return ret;
    }
    void dispatchSRVCycle(DescriptorCycle cycle) { _srvCycle = cycle; }
    DescriptorCycle consumeSRVCycle() {
        DescriptorCycle ret = _srvCycle;
        _srvCycle = CYCLE_NONE;
        return ret;
    }
    void dispatchRTCycle(DescriptorCycle cycle) { _rtCycle = cycle; }
    DescriptorCycle consumeRTCycle() {
        DescriptorCycle ret = _rtCycle;
        _rtCycle = CYCLE_NONE;
        return ret;
    }

    GroupResource& GetGroupResource(GroupResourceType type);

    bool operator==(const ToggleGroup& rhs) { return getId() == rhs.getId(); }

    void SetConfigDirtyFlag(std::atomic_bool* flag) { _configDirtyFlag = flag; }

    bool AlphaEnabled() { return _preserveAlpha; }
    bool AlphaClear() { return false; }
    bool BindingEnabled() { return _isProvidingTextureBinding && _copyTextureBinding; }
    bool BindingClear() { return _clearBindings; }
    void AssignPreferredTechniqueData(std::unordered_map<std::string, EffectData>& allTechniques);
    const std::unordered_set<EffectData*>& GetPreferredTechniqueData();

  private:
    void markConfigDirty() const {
        if (_configDirtyFlag != nullptr)
            _configDirtyFlag->store(true, std::memory_order_release);
    }
    void updateDebugCandidate(const std::string& status,
                              const std::string& boundary = {},
                              bool successfulRender = false) {
        std::lock_guard lock(*_debugHistoryMutex);

        auto it = std::find_if(_debugAutoHistory.begin(), _debugAutoHistory.end(), [this](const AutoDiagnosticEntry& entry) {
            return entry.shaderHash == _debugCurrentShaderHash &&
                   entry.target == _debugCurrentTarget &&
                   entry.sceneWidth == _debugCurrentSceneWidth &&
                   entry.sceneHeight == _debugCurrentSceneHeight &&
                   entry.format == _debugCurrentFormat;
        });

        if (it != _debugAutoHistory.end()) {
            AutoDiagnosticEntry updated = *it;
            updated.status = status;
            if (!boundary.empty())
                updated.boundary = boundary;
            if (successfulRender)
                ++updated.successfulRenders;

            _debugAutoHistory.erase(it);
            _debugAutoHistory.push_back(std::move(updated));
            return;
        }

        AutoDiagnosticEntry entry;
        entry.candidateId = ++_debugHistorySequence;
        entry.shaderHash = _debugCurrentShaderHash;
        entry.status = status;
        entry.target = _debugCurrentTarget;
        entry.sceneWidth = _debugCurrentSceneWidth;
        entry.sceneHeight = _debugCurrentSceneHeight;
        entry.format = _debugCurrentFormat;
        entry.boundary = boundary;
        entry.successfulRenders = successfulRender ? 1 : 0;
        _debugAutoHistory.push_back(std::move(entry));

        while (_debugAutoHistory.size() > 8)
            _debugAutoHistory.pop_front();
    }

    std::atomic_bool* _configDirtyFlag = nullptr;
    int _id;
    std::string _name;
    uint32_t _keybind;
    std::unordered_set<uint32_t> _vertexShaderHashes;
    std::unordered_set<uint32_t> _pixelShaderHashes;
    std::unordered_set<uint32_t> _computeShaderHashes;
    uint32_t _invocationLocation = 0;
    uint32_t _rtIndex = 0;
    uint32_t _cbSlotIndex = 2;
    uint32_t _cbDescIndex = 0;
    uint32_t _cbShaderStage = 0;
    uint32_t _bindingInvocationLocation = 0;
    uint32_t _bindingRTIndex = 0;
    uint32_t _bindingSrvSlotIndex = 1;
    uint32_t _renderSrvSlotIndex = 1;
    uint32_t _bindingSrvDescIndex = 0;
    uint32_t _renderSrvDescIndex = 0;
    uint32_t _bindingSrvShaderStage = 0;
    uint32_t _renderSrvShaderStage = 0;
    bool _isActive;           // true means the group is actively toggled (so the hashes have to be hidden.
    bool _isEditing;          // true means the group is actively edited (name, key)
    bool _isRetired = false;  // true means the group was removed and only lives on so stale render-thread pointers stay valid
    bool _allowAllTechniques; // true means all techniques are allowed, regardless of preferred techniques.
    volatile bool _isProvidingTextureBinding;
    volatile bool _copyTextureBinding;
    bool _renderToResourceViews;
    bool _autoRenderSRV = false;
    bool _hideMarkedShaders = false;
    uint64_t _debugEffectRenderCalls = 0;
    uint32_t _debugLastRenderedTechniqueCount = 0;
    uint64_t _debugLastRenderTarget = 0;
    uint32_t _debugSceneWidth = 0;
    uint32_t _debugSceneHeight = 0;
    uint32_t _debugEffectWidth = 0;
    uint32_t _debugEffectHeight = 0;
    bool _debugNativeStaging = false;
    std::string _debugLastTechniqueOrder;
    std::string _debugLastVulkanBoundary;

    std::string _debugAutoStatus;
    uint64_t _debugCurrentTarget = 0;
    uint32_t _debugCurrentSceneWidth = 0;
    uint32_t _debugCurrentSceneHeight = 0;
    std::string _debugCurrentFormat;
    uint32_t _debugCurrentShaderHash = 0;
    mutable std::shared_ptr<std::mutex> _debugHistoryMutex = std::make_shared<std::mutex>();
    std::deque<AutoDiagnosticEntry> _debugAutoHistory;
    uint64_t _debugHistorySequence = 0;
    bool _extractConstants;
    bool _extractResourceViews;
    volatile bool _clearBindings;
    bool _previewClearAlpha = true;
    bool _hasTechniqueExceptions; // _preferredTechniques are handled as exception to _allowAllTechniques
    bool _tonemapHDRtoSDRtoHDR = false;
    volatile bool _preserveAlpha = false;
    bool _flipBuffer = false;
    bool _flipBufferBinding = false;
    bool _suppressDrawCall = false;
    bool _matchVertexCount = false;
    uint32_t _vertexCountMin = 0;
    uint32_t _vertexCountMax = 0;
    bool _matchIndexCount = false;
    uint32_t _indexCountMin = 0;
    uint32_t _indexCountMax = 0;
    bool _matchInstanceCount = false;
    uint32_t _instanceCountMin = 0;
    uint32_t _instanceCountMax = 0;
    uint32_t _gamepadShortcut = 0;
    uint32_t _matchSwapchainResolution = SWAPCHAIN_MATCH_MODE_RESOLUTION;
    uint32_t _bindingMatchSwapchainResolution = SWAPCHAIN_MATCH_MODE_RESOLUTION;
    bool _requeueAfterRTMatchingFailure;
    bool _cbModePush = false;
    std::string _textureBindingName;
    std::unordered_set<std::string> _preferredTechniques;
    std::unordered_set<EffectData*> _preferredTechniqueData;
    std::unordered_map<std::string, std::tuple<uintptr_t, bool>> _varOffsetMapping;
    DescriptorCycle _cbCycle;
    DescriptorCycle _srvCycle;
    DescriptorCycle _rtCycle;

    std::array<GroupResource, 4> _group_buffers;
};
}
