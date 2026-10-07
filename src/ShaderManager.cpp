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

#include "ShaderManager.h"
#include <algorithm>

using namespace reshade::api;
using namespace std;

namespace ShaderToggler {
ShaderManager::ShaderManager() {}

void ShaderManager::addHashHandlePair(uint32_t shaderHash, uint64_t pipelineHandle) {
    if (pipelineHandle > 0 && shaderHash > 0) {
        unique_lock lock(_hashHandlesMutex);
        _handleToShaderHash[pipelineHandle] = shaderHash;
        _shaderHashes.emplace(shaderHash);
    }
}

void ShaderManager::removeHandle(uint64_t handle) {
    uint32_t shaderHash = 0;
    {
        unique_lock ulock(_hashHandlesMutex);
        const auto it = _handleToShaderHash.find(handle);
        if (it == _handleToShaderHash.end())
            return;

        shaderHash = it->second;
        _handleToShaderHash.erase(it);
        _shaderHashes.erase(shaderHash);
    }

    {
        unique_lock lock(_collectedActiveHandlesMutex);
        if (_collectedActiveShaderHashes.erase(shaderHash) > 0) {
            const auto orderIt = std::find(_collectedActiveShaderOrder.begin(), _collectedActiveShaderOrder.end(), shaderHash);
            if (orderIt != _collectedActiveShaderOrder.end())
                _collectedActiveShaderOrder.erase(orderIt);
        }
    }
}

void ShaderManager::startHuntingMode(const unordered_set<uint32_t> currentMarkedHashes) {
    // copy the currently marked hashes (from the active group) to the set of marked hashes.
    {
        unique_lock lock(_markedShaderHashMutex);
        _markedShaderHashes.clear();
        for (const auto hash : currentMarkedHashes) {
            _markedShaderHashes.emplace(hash);
        }
    }

    // switch on hunting mode
    _isInHuntingMode.store(true, memory_order_release);
    _activeHuntedShaderIndex = -1;
    _activeHuntedShaderHash.store(0, memory_order_release);
    clearObservedDrawGeometries();
    {
        unique_lock lock(_collectedActiveHandlesMutex);
        _collectedActiveShaderHashes.clear(); // clear it so we start with a clean slate
        _collectedActiveShaderOrder.clear();
    }
}

void ShaderManager::resetActiveHuntedShader() {
    _activeHuntedShaderIndex = -1;
    _activeHuntedShaderHash.store(0, memory_order_release);
    clearObservedDrawGeometries();
}

void ShaderManager::stopHuntingMode() {
    _isInHuntingMode.store(false, memory_order_release);
    _activeHuntedShaderIndex = -1;
    _activeHuntedShaderHash.store(0, memory_order_release);
    clearObservedDrawGeometries();
    {
        unique_lock lock(_markedShaderHashMutex);
        _markedShaderHashes.clear();
    }
}

void ShaderManager::setActiveHuntedShaderHandle() {
    if (_activeHuntedShaderIndex < 0) {
        _activeHuntedShaderHash.store(0, memory_order_release);
        return;
    }

    _activeHuntedShaderHash.store(
      getCollectedShaderHash(static_cast<uint32_t>(_activeHuntedShaderIndex)),
      memory_order_release);
}

void ShaderManager::huntNextShader(bool ctrlPressed) {
    if (!_isInHuntingMode.load(memory_order_acquire))
        return;

    const int32_t count = static_cast<int32_t>(getAmountShaderHashesCollected());
    if (count <= 0)
        return;

    if (ctrlPressed) {
        std::shared_lock lock(_markedShaderHashMutex);
        if (_markedShaderHashes.empty())
            return;

        const int32_t start = _activeHuntedShaderIndex;
        for (int32_t step = 1; step <= count; ++step) {
            const int32_t index = (start + step + count) % count;
            const uint32_t hash = getCollectedShaderHash(static_cast<uint32_t>(index));
            if (_markedShaderHashes.contains(hash)) {
                _activeHuntedShaderIndex = index;
                _activeHuntedShaderHash.store(hash, memory_order_release);
                return;
            }
        }
        return;
    }

    if (_activeHuntedShaderIndex < 0 || _activeHuntedShaderIndex >= count - 1)
        _activeHuntedShaderIndex = 0;
    else
        ++_activeHuntedShaderIndex;

    setActiveHuntedShaderHandle();
}

void ShaderManager::huntPreviousShader(bool ctrlPressed) {
    if (!_isInHuntingMode.load(memory_order_acquire))
        return;

    const int32_t count = static_cast<int32_t>(getAmountShaderHashesCollected());
    if (count <= 0)
        return;

    if (ctrlPressed) {
        std::shared_lock lock(_markedShaderHashMutex);
        if (_markedShaderHashes.empty())
            return;

        const int32_t start = _activeHuntedShaderIndex < 0 ? 0 : _activeHuntedShaderIndex;
        for (int32_t step = 1; step <= count; ++step) {
            const int32_t index = (start - step + count * 2) % count;
            const uint32_t hash = getCollectedShaderHash(static_cast<uint32_t>(index));
            if (_markedShaderHashes.contains(hash)) {
                _activeHuntedShaderIndex = index;
                _activeHuntedShaderHash.store(hash, memory_order_release);
                return;
            }
        }
        return;
    }

    if (_activeHuntedShaderIndex <= 0)
        _activeHuntedShaderIndex = count - 1;
    else
        --_activeHuntedShaderIndex;

    setActiveHuntedShaderHandle();
}

void ShaderManager::setActivedHuntedShaderIndex(uint32_t index) {
    if (!_isInHuntingMode.load(memory_order_acquire)) {
        return;
    }

    const size_t count = getAmountShaderHashesCollected();
    if (count == 0) {
        return;
    }

    if (index >= count) {
        _activeHuntedShaderIndex = 0;
    } else {
        _activeHuntedShaderIndex = index;
    }

    setActiveHuntedShaderHandle();
}

bool ShaderManager::setActiveHuntedShaderHash(uint32_t hash) {
    if (!_isInHuntingMode.load(memory_order_acquire) || hash == 0)
        return false;

    std::shared_lock lock(_collectedActiveHandlesMutex);
    const auto it = std::find(_collectedActiveShaderOrder.begin(), _collectedActiveShaderOrder.end(), hash);
    if (it == _collectedActiveShaderOrder.end())
        return false;

    _activeHuntedShaderIndex = static_cast<int32_t>(std::distance(_collectedActiveShaderOrder.begin(), it));
    _activeHuntedShaderHash.store(hash, memory_order_release);
    return true;
}

bool ShaderManager::isBlockedShader(uint32_t shaderHash) {
    if (shaderHash == 0) {
        return false;
    }

    bool toReturn = false;
    if (_isInHuntingMode.load(memory_order_acquire) && _hideHuntedShader) {
        const uint32_t activeHash = _activeHuntedShaderHash.load(memory_order_acquire);
        toReturn |= (activeHash == shaderHash);
    }
    if (_hideMarkedShaders) {
        shared_lock lock(_markedShaderHashMutex);
        toReturn |= _markedShaderHashes.contains(shaderHash);
    }

    return toReturn;
}

void ShaderManager::addActivePipelineHandle(uint64_t handle) {
    // get the shader hash bound to this pipeline handle
    const auto shaderHash = getShaderHash(handle);
    if (shaderHash > 0) {
        unique_lock lock(_collectedActiveHandlesMutex);
        const auto [_, inserted] = _collectedActiveShaderHashes.emplace(shaderHash);
        if (inserted)
            _collectedActiveShaderOrder.push_back(shaderHash);
    }
}

void ShaderManager::toggleMarkOnHuntedShader() {
    const uint32_t activeHash = _activeHuntedShaderHash.load(memory_order_acquire);
    if (activeHash == 0)
        return;

    unique_lock lock(_markedShaderHashMutex);
    if (_markedShaderHashes.contains(activeHash))
        _markedShaderHashes.erase(activeHash);
    else
        _markedShaderHashes.emplace(activeHash);
}

uint32_t ShaderManager::getShaderHash(uint64_t handle) {
    shared_lock lock(_hashHandlesMutex);
    const auto it = _handleToShaderHash.find(handle);
    return it == _handleToShaderHash.end() ? 0 : it->second;
}

void ShaderManager::recordDrawGeometry(uint32_t shaderHash, bool isIndexed, uint32_t count, uint32_t instanceCount) {
    if (!_isInHuntingMode.load(memory_order_acquire) || shaderHash == 0 || shaderHash != _activeHuntedShaderHash.load(memory_order_acquire)) {
        return;
    }

    std::unique_lock lock(_drawGeometryMutex);
    auto& statsList = _observedDrawGeometries[shaderHash];
    for (auto& stat : statsList) {
        if (stat.isIndexed == isIndexed && stat.count == count && stat.instanceCount == instanceCount) {
            stat.invocations++;
            return;
        }
    }

    if (statsList.size() < 32) {
        statsList.push_back({ isIndexed, count, instanceCount, 1 });
    }
}

std::vector<DrawGeometryStats> ShaderManager::getObservedDrawGeometries() const {
    const uint32_t activeHash = _activeHuntedShaderHash.load(memory_order_acquire);
    if (activeHash == 0) {
        return {};
    }
    std::shared_lock lock(_drawGeometryMutex);
    const auto it = _observedDrawGeometries.find(activeHash);
    if (it != _observedDrawGeometries.end()) {
        return it->second;
    }
    return {};
}

void ShaderManager::clearObservedDrawGeometries() {
    std::unique_lock lock(_drawGeometryMutex);
    _observedDrawGeometries.clear();
}
}
