#include "ConstantHandlerBase.h"
#include "PipelinePrivateData.h"
#include "ShaderManager.h"
#include "StateTracking.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <set>
#ifdef NDEBUG
#error These regression checks require assertions.
#endif

using namespace ShaderToggler;

static void testGroupSettings() {
    CDataFile config;
    assert(config.LoadFromString("[Group0]\nName=HUD:=\nConstantPushMode=True\nInvocationLocation=2\n"
                                 "BindingInvocationLocation=9\nAllowAllTechniques=False\nTechniques=Fog [Fog.fx]\n"
                                 "[Group0_ComputeShaders]\nAmountHashes=1\nShaderHash0=123\n"));
    ToggleGroup group;
    group.loadState(config, 0);
    assert(!group.isEmpty() && group.getComputeShaderHashes().contains(123));
    assert(group.getName() == "HUD:=" && group.getCBIsPushMode());
    assert(group.getInvocationLocation() == 2 && group.getBindingInvocationLocation() == 2);
    assert(group.acceptsTechnique("Fog [Fog.fx]") && !group.acceptsTechnique("Bloom [Bloom.fx]"));
    group.setAllowAllTechniques(true);
    group.setHasTechniqueExceptions(true);
    assert(!group.acceptsTechnique("Fog [Fog.fx]") && group.acceptsTechnique("Bloom [Bloom.fx]"));

    CDataFile saved, imported;
    group.saveState(saved, 0);
    assert(imported.LoadFromString(saved.Serialize()));
    ToggleGroup copy;
    copy.loadState(imported, 0);
    assert(copy.configurationSignature() == group.configurationSignature());
    assert(!imported.LoadFromString("[Group0]\nName=Replacement\n[unfinished"));
    assert(imported.GetString("Name", "Group0") == "HUD:=");
    std::ofstream("broken.ini") << "[unfinished";
    assert(!imported.Load("broken.ini"));
    assert(imported.GetString("Name", "Group0") == "HUD:=");
}

static void testTransientPipelines() {
    ShaderManager shaders;
    shaders.startHuntingMode({20});
    shaders.addHashHandlePair(10, 1);
    shaders.addHashHandlePair(20, 2);
    shaders.addHashHandlePair(20, 3);
    for (uint64_t handle : {1, 2, 3})
        shaders.addActivePipelineHandle(handle);
    shaders.removeHandle(2);
    shaders.huntNextShader(true);
    assert(shaders.getActiveHuntedShaderHash() == 20);
    assert(shaders.getShaderHash(2) == 0 && shaders.getShaderHash(3) == 20);
    shaders.removeHandle(3);
    assert((shaders.getCollectedShaderHashesOrdered() == std::vector<uint32_t>{10, 20}));
    shaders.setHideHuntedShader(true);
    assert(shaders.isBlockedShader(20) && !shaders.isBlockedShader(10));
    shaders.stopHuntingMode();
    assert(!shaders.isBlockedShader(20));
}

static void testConstantsAndReload() {
    state_tracking state{};
    constexpr uint32_t stage = 2, slot = 0, bufferIndex = 17;
    state.constant_buffer[stage].resize(bufferIndex + 1);
    const std::vector<uint32_t> words{0x12345678, 0x87654321, 0xffffffff};
    state.constant_buffer[stage][bufferIndex] = words;
    state.root_tables[stage].second.emplace_back(StateTracking::root_entry_type::push_constants, bufferIndex, reshade::api::descriptor_table{});
    assert(state.get_root_table_entry_size_at(stage, slot) == words.size());
    ToggleGroup group;
    Shim::Constants::ConstantHandlerBase constants;
    constants.SetConstants(&group, *state.get_constants_at(stage, slot));
    assert(constants.GetConstantBufferSize(&group) == words.size() * sizeof(uint32_t));
    assert(std::memcmp(constants.GetConstantBuffer(&group), words.data(), words.size() * sizeof(uint32_t)) == 0);

    CommandListDataContainer list;
    EffectData effect;
    list.ps.techniquesToRender[&effect] = {};
    list.cs.bindingsToUpdate[&group] = {};
    list.SyncEffectGeneration(0);
    assert(list.ps.techniquesToRender.size() == 1);
    list.SyncEffectGeneration(1);
    assert(list.ps.techniquesToRender.empty() && list.cs.bindingsToUpdate.size() == 1);
    list.Reset();
    assert(!list.ps.activeShaderHash && list.cs.bindingsToUpdate.empty());
}

// A minimal host for checking the compiled DLL's actual registration and teardown.
static bool registered;
static std::set<std::pair<reshade::addon_event, void *>> callbacks;
static std::set<std::string> overlays;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t version) {
    return registered = version == RESHADE_API_VERSION;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) { registered = false; }
extern "C" __declspec(dllexport) void ReShadeRegisterEvent(reshade::addon_event event, void* callback) {
    assert(callbacks.emplace(event, callback).second);
}
extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(reshade::addon_event event, void* callback) {
    assert(callbacks.erase({event, callback}) == 1);
}
extern "C" __declspec(dllexport) void ReShadeRegisterOverlay(const char* title, void*) { overlays.emplace(title ? title : ""); }
extern "C" __declspec(dllexport) void ReShadeUnregisterOverlay(const char* title, void*) { assert(overlays.erase(title ? title : "") == 1); }
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
extern "C" __declspec(dllexport) const imgui_function_table* ReShadeGetImGuiFunctionTable(uint32_t version) {
    static const imgui_function_table table{};
    return version == IMGUI_VERSION_NUM ? &table : nullptr;
}

int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    testGroupSettings();
    testTransientPipelines();
    testConstantsAndReload();
    const std::string layout = "[OVERLAY]\nWindow=[Window][###home],DockId=0x00000007,,0,[Window][REST],Pos=0,,0,[Window][Other],DockId=0x00000003,,1\n";
    std::ofstream("ReShade.ini", std::ios::binary) << layout;
    for (int pass = 0; pass < 2; ++pass) {
        std::ofstream("ReshadeEffectShaderToggler.ini") << "[General]\nAmountGroups=0\nTrackDescriptors=" << (pass ? "False" : "True") << '\n';
        HMODULE addon = LoadLibraryW(argv[1]);
        assert(addon && registered && overlays.contains("REST"));
        for (auto event : {reshade::addon_event::update_descriptor_tables, reshade::addon_event::copy_descriptor_tables}) {
            assert(std::any_of(callbacks.begin(), callbacks.end(), [event](const auto& entry) { return entry.first == event; }) == (pass == 0));
        }
        for (auto event : {reshade::addon_event::bind_pipeline, reshade::addon_event::draw, reshade::addon_event::draw_indexed,
                           reshade::addon_event::dispatch, reshade::addon_event::reshade_reloaded_effects}) {
            assert(std::any_of(callbacks.begin(), callbacks.end(), [event](const auto& entry) { return entry.first == event; }));
        }
        assert(FreeLibrary(addon));
        assert(!registered && callbacks.empty() && overlays.empty());
    }
    std::ifstream ini("ReShade.ini", std::ios::binary);
    assert(std::string(std::istreambuf_iterator<char>(ini), {}) == layout);
    std::cout << "Group settings, shader hunting, push constants, queue invalidation and add-on lifecycle passed.\n";
}
