# ReshadeEffectShaderToggler (REST)

[![Latest release](https://img.shields.io/github/v/release/BadassBaboon/ReshadeEffectShaderToggler?label=release)](https://github.com/BadassBaboon/ReshadeEffectShaderToggler/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/BadassBaboon/ReshadeEffectShaderToggler/total)](https://github.com/BadassBaboon/ReshadeEffectShaderToggler/releases)
[![ReShade](https://img.shields.io/badge/ReShade-6.8%2B-blue)](https://reshade.me)

A ReShade 6.8+ add-on that renders ReShade effects at a chosen point in a game's frame. You group the game shaders that mark that point, and REST applies your effects right before those shaders draw. The usual use is putting effects such as ambient occlusion or color grading under the game's HUD instead of on top of it.

This fork adds Direct3D 12, Vulkan and Direct3D 9 (32-bit) support, a rebuilt interface, controller shortcuts and the features from REST Enhanced. Direct3D 10 and 11 work as before.

## Fork features

### Interface
- REST has its own **REST** tab in the ReShade overlay instead of a section under Add-ons. The tab is split into Toggle Groups, Keybindings and Options, and shows when you have unsaved changes.
- Settings use on/off switches, and the layout adapts to narrow docked windows and large font scaling without clipping text.
- Group settings open in a two-pane window: the shader list on the left, and tabs for render targets, techniques, constant bindings and texture bindings on the right.
- Previews of render targets and texture bindings scale to the target's aspect ratio, up to 540 pixels tall.
- The hunting buttons show their assigned shortcut in the tooltip, and every keyboard and gamepad shortcut has a Clear button.

### Groups
- Groups can be cloned, copied to the clipboard as text, and imported from the clipboard. Cloned and imported groups start inactive with no hotkey.
- Groups can be moved up and down. REST checks them from the top of the list down.
- Per-group hotkeys work, and one shortcut can toggle every group at once.
- **Reload INI** re-reads `ReshadeEffectShaderToggler.ini` without restarting the game.
- Saving writes to a temporary file first and keeps the previous config as `ReshadeEffectShaderToggler.ini.bak`.

### Effects
- Each group has three technique modes: apply every technique enabled in ReShade, apply only the ones you tick, or apply everything except the ones you tick.
- The technique list has search, ON/OFF badges showing ReShade's enabled state, and Select All / Untick All.
- **Auto scene colour** (from REST Enhanced) handles games that render below output resolution with DLSS or dynamic resolution. REST copies the live scene to a full-resolution buffer, runs the effects there, and copies the result back. It works on Direct3D 10, 11, 12 and Vulkan, and has a **Copy diagnostics** button for bug reports.

### Hiding game elements
- **Suppress draw calls** and **Hide marked shaders** stop a group's shaders from drawing while the group is active, which removes things like the HUD for screenshots. The group's effects still render.
- Index count, vertex count and instance count filters separate draws that share a shader, for example a HUD element and a 3D model that use the same pixel shader.

### Shader hunting
- The shader list has search, an All / Marked / Unmarked filter, and buttons for Previous, Next, Previous Marked, Next Marked, Mark / Unmark, Copy Hash and Clear Marked. **Rescan** collects shaders again without losing your marks.
- Marked shaders that weren't seen in the latest collection are listed in red so you can unmark them.
- **Observed draws** lists the vertex/index and instance counts the selected shader draws with. **Set Filter** turns one of them into a geometry filter.
- **Hide hunted shader in 3D scene** hides the selected shader so you can see what it draws. It's off by default, so effects stay visible on the selected shader while you browse.
- The render target preview has RGB, R, G and B views.

### Controller support
- XInput gamepad shortcuts with button combinations such as LB + D-Pad Down, recorded by pressing them.
- Per-group gamepad shortcuts and a gamepad shortcut to toggle all groups.

### Graphics APIs
- Direct3D 12 and Vulkan support, including multithreaded command recording.
- Direct3D 9 support in the 32-bit add-on, based on [CrazyDaifu's fork](https://github.com/CrazyDaifu/ReshadeEffectShaderToggler). Alt-tabbing, switching between windowed and fullscreen, and changing resolution no longer crash Direct3D 9 games.
- Vulkan hunting and previews from REST Enhanced. The hunted draw is hidden and the preview is copied between render passes.
- On Direct3D 12 the live render target preview is off by default, because copying a game's target mid-frame froze Red Dead Redemption. Turn it on with **Live preview (DX12)** in the preview panel or in Options. Hunting and marking work either way.
- **Preserve target alpha channel** works on Direct3D 12 and Vulkan for 8-bit targets. On 16-bit float targets REST skips the alpha copy, which used to hang the GPU.

### Troubleshooting
- **Diagnostic logs** in Options (or `DiagnosticLogs=True` in the ini) writes a per-second summary to `ReShade.log`: frame rate, how many times effects rendered, and CPU time spent in REST. It's off by default.

## Requirements

- ReShade 6.8.0 or newer, installed with add-on support.
- `ReshadeEffectShaderToggler.addon64` for 64-bit games, or `ReshadeEffectShaderToggler.addon32` for 32-bit games.

## Installation

Put the add-on next to the game executable that ReShade is installed for, the same folder as ReShade's dll.

Unreal Engine games often have two executables: one in the install folder and one deeper in, such as `GameName\Binaries\Win64\GameName-Win64-Shipping.exe`. REST and ReShade both go in the deeper folder, `GameName\Binaries\Win64` in this example.

Start the game and open the ReShade overlay. You should see a **REST** tab.

## Creating a group

1. In the **REST** tab, click **New Group**. It's called `Default` and has no hotkey.
2. Click **Edit** to rename it and set a keyboard or gamepad shortcut. Names and shortcuts don't have to be unique, and keyboard shortcuts can use Ctrl, Alt and Shift.
3. Click **Settings** to pick the shaders that belong to the group (see [Marking shaders](#marking-shaders)).
4. Pick the effects in the **Techniques** tab (see [Configuring effects](#configuring-effects)).
5. Click **Done**, test the group with its switch or hotkey, then click **Save Changes**.

The configuration is saved to `ReshadeEffectShaderToggler.ini` next to the add-on.

## Configuring effects

Open a group's **Settings** and go to the **Techniques** tab. Techniques are listed by the technique name inside each effect file, not the file name, so check the `.fx` file if a name is unclear.

There are three routing modes:

- **All active techniques**: the group renders every technique enabled in ReShade that hasn't already been rendered this frame.
- **Inclusion list**: the group renders only the techniques you tick.
- **Exclusion list**: the group renders every enabled technique except the ones you tick.

A few rules apply in every mode:

- Only techniques enabled in ReShade's own list render, whatever the group says.
- A technique assigned to several active groups renders once, in the first group reached during the frame.
- Within a group, techniques run in ReShade's global order.

Once at least one group is active, REST only renders the techniques those groups use. Something enabled in ReShade can therefore stop showing up because no active group includes it.

## Marking shaders

Before you start, make sure the thing you want to put effects under, such as the HUD or a menu, is on screen. An effect with a debug view, like ambient occlusion, makes it easier to see what draws on top of it.

Click **Settings** on the group. REST collects the shaders active over the next few frames (the count is set in Options), so it only lists shaders the game is using right now. After that you can browse them.

The selected shader gets the group's effects applied right before it draws. If your test effect now shows under the HUD, the selected shader is drawing the HUD: mark it. Use the buttons in the shader list, double-click a hash, press Enter, or use the keyboard shortcuts:

| Action | Pixel shaders | Vertex shaders |
|---|---|---|
| Previous / next shader | `Numpad 1` / `Numpad 2` | `Numpad 4` / `Numpad 5` |
| Mark or unmark | `Numpad 3` | `Numpad 6` |
| Previous / next marked shader | `Ctrl + Numpad 1` / `Ctrl + Numpad 2` | `Ctrl + Numpad 4` / `Ctrl + Numpad 5` |
| Mark, then previous / next | `Shift + Numpad 1` / `Shift + Numpad 2` | `Shift + Numpad 4` / `Shift + Numpad 5` |

All of these can be changed in **Keybindings**. Compute shaders can be hunted too, but have no default keys.

Click **Done** to save the marked shaders to the group, then **Save Changes** to write them to the ini.

## Credits

- [Frans Bouma](https://github.com/FransBouma): original ShaderToggler.
- [alex / 4lex4nder](https://github.com/4lex4nder): ReshadeEffectShaderToggler, which this fork is based on.
- **DeViLhoOD** ([REST Enhanced](https://github.com/Pav-Osmolski/ReshadeEffectShaderToggler)): Auto scene colour, Vulkan injection and previews, shader hunting fixes, and the group management and technique features ported into this fork.
- [CrazyDaifu](https://github.com/CrazyDaifu/ReshadeEffectShaderToggler): Direct3D 9 swapchain support.
- [Sinom](https://github.com/sinomsinom): contributor.
- [crosire](https://github.com/crosire): ReShade, and example code for effect rendering.
- [Marty McFly](https://github.com/martymcmodding): the constant buffer extraction idea.
- [darkarchan](https://github.com/darkarchan): testing and contributions.

## License

This project keeps the license and notices of the upstream project. See [LICENSE](LICENSE).
