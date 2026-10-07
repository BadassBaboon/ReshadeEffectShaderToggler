# ReshadeEffectShaderToggler [![MSBuild](https://github.com/BadassBaboon/ReshadeEffectShaderToggler/actions/workflows/msbuild.yml/badge.svg)](https://github.com/BadassBaboon/ReshadeEffectShaderToggler/actions/workflows/msbuild.yml) [![Release](https://github.com/BadassBaboon/ReshadeEffectShaderToggler/actions/workflows/release.yml/badge.svg)](https://github.com/BadassBaboon/ReshadeEffectShaderToggler/actions/workflows/release.yml)
Reshade 6.8+ addon to apply Reshade effects to render targets bound before specific, user-configurable, groups of shaders are 
encountered within a game's rendering pipeline.

## Fork notes
This fork extends **ReshadeEffectShaderToggler (REST)** with support for **Direct3D 12**, **Vulkan**, **Direct3D 9 (32-bit `addon32`)**, updated **ReShade 6.x API** compatibility, and quality-of-life workflow improvements, while maintaining the Direct3D 10/11 compatibility.
### Key Features & Improvements
* **ReShade 6.x API Upgrade:**
  * Built against the ReShade 6.8.0 SDK (add-on API 20 / ImGui 1.92.5). Requires ReShade 6.8.0 or newer with add-on support.
  * Updated private data access (`get_private_data<T>()`) to support pointer-return semantics (`T*`), ensuring compatibility across modern ReShade 6.x builds.
* **Quality-of-Life (QoL) Enhancements:**
  * **Working Group Hotkeys:** Fixed per-group shortcut key binding evaluation (`CheckHotkeys`), enabling functional real-time keybind toggling per group.
  * **Global Toggle-All Keybind:** Added a configurable global shortcut key to toggle all active groups on or off simultaneously.
  * **Group Duplication:** Added a "Duplicate Group" button to copy existing shader groups (including marked shader hashes, assigned effects, and settings).
  * **INI Hot-Reload:** Added a "Reload from INI" button that tears down existing GPU resources safely and re-reads `ReshadeEffectShaderToggler.ini` without restarting the game.
  * **Clean Keybind UI:** Replaced raw enum strings in the keybind selector with human-readable key labels.
* **Direct3D 12 & Vulkan Stability:**
  * **Format-Matched PSOs:** Dynamic Pipeline State Object (PSO) compilation per render target format. Eliminates `DXGI_ERROR_DEVICE_HUNG` crashes caused by baked PSO format mismatches in D3D12/Vulkan.
  * **Thread-Safe Command Recording:** Thread-safe PSO map management using `std::shared_mutex` with double-checked locking to support asynchronous multi-threaded command list recording.
  * **Alpha Preservation Fix:** Fixed GPU hangs when hunting or rendering with "Preserve target alpha channel" enabled in D3D12/Vulkan. Restricts alpha-preserving quad copies on explicit-barrier APIs to standard LDR typeless formats (`R8G8B8A8` / `B8G8R8A8`), bypassing unsafe copies on HDR/FP16 render targets while preserving full UI alpha masking.
  * **Log Gating:** Throttled unsafe preview capture and render target size mismatch warnings to log only on distinct format/dimension changes, preventing `ReShade.log` file bloat.
* **Direct3D 9 (`addon32`) Support:**
  * **Surface Backbuffers:** Added `resource_type::surface` support in `GlobalResourceView`. In D3D9, ReShade exposes swap-chain backbuffers as standalone surfaces, allowing REST to bind and mask UI/effects in 32-bit D3D9 games (e.g., Battlefield 2, GTA SA, NFS Most Wanted).
  * **Device Reset Safety:** Implemented D3D9 pre-reset teardown in `onDestroyEffectRuntime`. Releases default-pool resources, group buffers, and binding views before native `IDirect3DDevice9::Reset()` calls, preventing `D3DERR_INVALIDCALL` crashes when alt-tabbing, toggling windowed/fullscreen mode, or changing resolution.
  * **Resource Destruction Order:** Reordered view teardown to ensure RTVs and SRVs are destroyed prior to parent resources, preventing resource handle leakage on D3D9 device resets.
* **Multi-Stage Pipeline Rendering:**
  * Resolved a C++ short-circuit evaluation bug in `_RenderEffects()` where simultaneous Pixel, Vertex, or Compute shader toggles at the same draw call invocation could cause secondary stages to be skipped. Active stages now evaluate independently.

## How to use
Place the `ReshadeEffectShaderToggler.addon` in the same folder as where the game exe is located. This is in most cases the same folder as where the Reshade 6.8+ dll
is located. For Unreal Engine powered games there might be two
game exe's: one in the game's installation folder, and one in a folder deeper into that folder, e.g. 
`GameName\Binaries\Win64\GameName-Win64-Shipping.exe`; the shader toggler addon has to be in that second folder, in our example:
`GameName\Binaries\Win64`. Reshade has to be placed in that folder as well.

Be sure to use the Reshade version which supports Addons (so the unsigned version). When you start your game, the `Addons` tab in 
the Reshade gui should show the ReshadeEffectShaderToggler information and controls.

To create a toggle for a set of shaders, open the reshade gui and go to the addon's tab -> Reshade Effect Shader Toggler area. Then click 
the `New` button to create a new *Toggle group*. By default the new toggle group has as name `Default` and no toggle key. 
To change these, click the `Edit` button of the Toggle Group. You can then change the name of the toggle group
and also add a keyboard shortcut. The keyboard shortcut nor the name have to be unique. The keyboard shortcut can be 
any key on your keyboard, optionally in combination of using `Alt`, `Control`, and `Shift`. 

A toggle group needs shaders assigned to it, that's done with marking shaders below.

## Configuring Effects
Each shader group can be assigned a subset of all Reshade effects. These are rendered in the order the shader groups are encountered while 
rendering the frame, though the order of rendering within a shader groups corresponds to the global order you've set up in Reshade.
To configure effects, click the `Change Effects` button in the shader group. A window should pop up with a table showing all effects. The naming
is based on the technique names specified within the effects themselves instead of the actual effect names, in doubt open the 
effect shader file and double check.

Shader group effect rendering is based on the following principles:
* A single shader group that has no effects assigned, if active, will render all effects enabled in Reshade that have not yet been rendered by previous shader groups
* If an effect is assigned to multiple shader groups, it will only be rendered by the first shader group encountered.
* Only effects enabled in the global Reshade effect list will be rendered, no matter what is assigned to the shader groups.

    To finish editing either press the window's `x` button on the top right or press the `Done` button in the
    shader group.

    Note that this may lead to discrepancies between what is enabled in Reshade and what is actually rendered. If at least one shader group is enabled, 
    it will render only effects applicable to those shader groups, i.e., potentially not all globally enabled effects will be rendered.

## Marking Shaders
To successfully be able to mark a set of shaders to toggle, be sure the elements you want to toggle, so the elements
using the shaders, are visible, e.g. a menu or a hud or a certain effect. If possible, enable something like an ambient occlusion
shader in it's debug mode to better see what is rendered on top of it. In case you have multiple shader groups with different assigned
effects, it might make sense to assign this shader to shader group you're editing. After you've made sure the elements to toggle are
visible, click the `Change Shaders` button of the toggle group. This will start the 'Shader hunting' phase for the particular 
toggle group. 

You should see the shader overlay in the top left corner with the information you need. By default it waits a certain amount
of frames (which you can configure in the reshade overlay) to see which shaders are currently active. This is to avoid having
to walk through potentially thousands of shaders which aren't currently used. 

After the frames have been collected, it has enough information to allow you to browse the shaders. 

To walk the available pixel shaders, use the `Numpad 1` and `Numpad 2` keys. If an element is rendered on top of your test effect, 
the shader that's currently 'active' is rendering these elements, so if you want to render effects before these elements, 
press `Numpad 3`. The shader is then marked, and part of the toggle group. Press `Numpad 3` again to remove it from the group.

To walk all shaders you already marked in the current group, you can hold down `Ctrl` and press `Numpad 1` and `Numpad 2` to quickly
move back/forth through the shaders in a group, e.g. when you made a mistake and you want to unmark a shader. 

To walk the available vertex shaders, instead use the `Numpad 4` and `Numpad 5` keys. To mark a vertex shader to be 
part of the toggle group, press `Numpad 6`.

Same for vertex shaders: to walk all shaders you already marked in the current group, you can hold down `Ctrl` and press `Numpad 4` and `Numpad 5` 
to quickly move back/forth through the shaders in a group. 

To test your current group, press the toggle key you assigned to the group or toggle the `Active` checkbox. When you're done, click the 'Done' button in the 
reshade overlay for the particular toggle group. 

To re-use this information the next time you run the game, click the Save toggle group button. This will write an ini file 
(`ReshadeEffectShaderToggler.ini`) with the information to create the set of shaders to toggle next time you start the game. This file is
located in the same folder as `ReshadeEffectShaderToggler.addon`.

## Credits
* [Sinom](https://github.com/sinomsinom)<br/>
    Contributor
* [Frans Bouma](https://github.com/FransBouma)<br/>
    Original ShaderToggler
* [crosire](https://github.com/crosire)<br/>
    Reshade and example code of effect rendering
* [Marty McFly](https://github.com/martymcmodding)<br/>
    Constant buffer extraction idea
* [darkarchan](https://github.com/darkarchan)<br/>
    Testing, contribution