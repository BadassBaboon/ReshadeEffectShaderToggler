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

#include "CDataFile.h"
#include "ToggleGroup.h"
#include <atomic>
#include <map>
#include <reshade_api_device.hpp>
#include <reshade_api_pipeline.hpp>
#include <shared_mutex>
#include <tsl/robin_map.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ShaderToggler {
struct DrawGeometryStats {
    bool isIndexed = false;
    uint32_t count = 0;
    uint32_t instanceCount = 0;
    uint32_t invocations = 0;

    bool operator==(const DrawGeometryStats& other) const {
        return isIndexed == other.isIndexed && count == other.count && instanceCount == other.instanceCount;
    }
};

/// <summary>
/// Class which manages a set of shaders for a given type (pixel, vertex...)
/// </summary>
class ShaderManager {
  public:
    ShaderManager();

    void addHashHandlePair(uint32_t shaderHash, uint64_t pipelineHandle);
    void removeHandle(uint64_t handle);
    /// <summary>
    /// Switches on the hunting mode for the shader manager. It will copy the passed in hashes to the set of marked hashes. Hunting mode is the mode
    ///	where the user can step through collected active shaders to mark them for assignment to the current edited group.
    /// </summary>
    /// <param name="currentMarkedHashes"></param>
    void startHuntingMode(const std::unordered_set<uint32_t> currentMarkedHashes);
    void stopHuntingMode();
    /// <summary>
    /// Moves to the next shader. If control is pressed as well, it'll step to the next marked shader (if any). If there aren't any shaders in that
    ///	situation, it'll stay on the current shader.
    /// </summary>
    /// <param name="ctrlPressed">If control is pressed as well, it'll step to the next marked shader (if any). If there aren't any shaders in that
    ///	situation, it'll stay on the current shader.</param>
    void huntNextShader(bool ctrlPressed);
    /// <summary>
    /// Moves to the previous shader. If control is pressed as well, it'll step to the previous marked shader (if any). If there aren't any shaders in that
    ///	situation, it'll stay on the current shader.
    /// </summary>
    /// <param name="ctrlPressed">If control is pressed as well, it'll step to the previous marked shader (if any). If there aren't any shaders in that
    ///	situation, it'll stay on the current shader.</param>
    void huntPreviousShader(bool ctrlPressed);
    /// <summary>
    /// Returns true if the shader hash passed in is the currently hunted shader or it's part of the marked shader hashes
    /// </summary>
    /// <param name="shaderHash"></param>
    /// <returns></returns>
    bool isBlockedShader(uint32_t shaderHash);
    /// <summary>
    /// Returns the shader hash for the passed in pipeline handle, if found. 0 otherwise.
    /// </summary>
    /// <param name="handle"></param>
    /// <returns></returns>
    uint32_t getShaderHash(uint64_t handle);
    void addActivePipelineHandle(uint64_t handle);
    void toggleMarkOnHuntedShader();
    void clearMarkedShaderHashes() {
        std::unique_lock lock(_markedShaderHashMutex);
        _markedShaderHashes.clear();
    }
    bool removeMarkedShaderHash(uint32_t hash) {
        std::unique_lock lock(_markedShaderHashMutex);
        return _markedShaderHashes.erase(hash) > 0;
    }
    void resetActiveHuntedShader();

    size_t getPipelineCount() { return _handleToShaderHash.size(); }
    size_t getShaderCount() { return _shaderHashes.size(); }
    std::unordered_set<uint32_t> getCollectedShaderHashes() const {
        std::shared_lock lock(_collectedActiveHandlesMutex);
        return _collectedActiveShaderHashes;
    }
    std::vector<uint32_t> getCollectedShaderHashesOrdered() const {
        std::shared_lock lock(_collectedActiveHandlesMutex);
        return _collectedActiveShaderOrder;
    }
    void setActivedHuntedShaderIndex(uint32_t index);
    bool setActiveHuntedShaderHash(uint32_t hash);
    size_t getAmountShaderHashesCollected() const {
        std::shared_lock lock(_collectedActiveHandlesMutex);
        return _collectedActiveShaderHashes.size();
    }
    bool isInHuntingMode() const { return _isInHuntingMode.load(std::memory_order_acquire); }
    uint32_t getActiveHuntedShaderHash() const { return _activeHuntedShaderHash.load(std::memory_order_acquire); }
    int getActiveHuntedShaderIndex() const { return _activeHuntedShaderIndex; }
    void toggleHideMarkedShaders() { _hideMarkedShaders = !_hideMarkedShaders; }
    void setHideMarkedShaders(bool hide) { _hideMarkedShaders = hide; }

    bool isHideHuntedShader() const { return _hideHuntedShader; }
    void setHideHuntedShader(bool hide) { _hideHuntedShader = hide; }

    bool isHuntedShaderMarked() {
        std::shared_lock lock(_markedShaderHashMutex);
        return _markedShaderHashes.contains(_activeHuntedShaderHash.load(std::memory_order_acquire));
    }

    bool isHuntedShaderMarked(uint32_t hash) {
        std::shared_lock lock(_markedShaderHashMutex);
        return _markedShaderHashes.contains(hash);
    }

    std::unordered_set<uint32_t> getMarkedShaderHashes() {
        std::shared_lock lock(_markedShaderHashMutex);
        return _markedShaderHashes;
    }

    uint32_t getCollectedShaderHash(uint32_t index) const {
        std::shared_lock lock(_collectedActiveHandlesMutex);
        return index < _collectedActiveShaderOrder.size() ? _collectedActiveShaderOrder[index] : 0;
    }

    size_t getMarkedShaderCount() {
        std::shared_lock lock(_markedShaderHashMutex);
        return _markedShaderHashes.size();
    }

    bool isKnownHandle(uint64_t pipelineHandle) {
        std::shared_lock lock(_hashHandlesMutex);
        return _handleToShaderHash.contains(pipelineHandle);
    }

    inline uint32_t safeGetShaderHash(uint64_t pipelineHandle) {
        std::shared_lock lock(_hashHandlesMutex);
        const auto& it = _handleToShaderHash.find(pipelineHandle);

        return it == _handleToShaderHash.end() ? 0 : it->second;
    }

    void recordDrawGeometry(uint32_t shaderHash, bool isIndexed, uint32_t count, uint32_t instanceCount);
    std::vector<DrawGeometryStats> getObservedDrawGeometries() const;
    void clearObservedDrawGeometries();

  private:
    void setActiveHuntedShaderHandle();

    std::unordered_set<uint32_t> _shaderHashes; // all shader hashes added through init pipeline
    tsl::robin_map<uint64_t, uint32_t> _handleToShaderHash;
    std::unordered_set<uint32_t> _collectedActiveShaderHashes; // membership lookup for shaders collected during the active collection phase
    std::vector<uint32_t> _collectedActiveShaderOrder;          // stable first-seen order used by hunting navigation and UI
    std::unordered_set<uint32_t> _markedShaderHashes;          // the hashes for shaders which are currently marked.

    std::atomic_bool _isInHuntingMode{ false };
    int32_t _activeHuntedShaderIndex = -1;
    std::atomic_uint32_t _activeHuntedShaderHash{ 0 };
    mutable std::shared_mutex _collectedActiveHandlesMutex;
    std::shared_mutex _hashHandlesMutex;
    std::shared_mutex _markedShaderHashMutex;
    bool _hideMarkedShaders = false;
    bool _hideHuntedShader = false;

    std::unordered_map<uint32_t, std::vector<DrawGeometryStats>> _observedDrawGeometries;
    mutable std::shared_mutex _drawGeometryMutex;
};
}
