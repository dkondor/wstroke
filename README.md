# wstroke

Port of [Easystroke mouse gestures](https://github.com/thjaeger/easystroke) as a plugin for [Wayfire](https://github.com/WayfireWM/wayfire). Mouse gestures are shapes drawn on the screen while holding down one of the buttons (typically the right or middle button). This plugin allows associating such gestures with various actions. See the [Wiki](https://github.com/dkondor/wstroke/wiki) for more explanations and examples.

Note: this branch requires a recent version of Wayfire and wlroots (see below). For older versions, use the [wayfire-0.7 branch](https://github.com/dkondor/wstroke/tree/wayfire-0.7).

### Dependencies

 - [Wayfire](https://github.com/WayfireWM/wayfire) a recent git version from the 0.8.0 branch, at least commit [3ac0284](https://github.com/WayfireWM/wayfire/commit/3ac028406cc3697dd40c128721fb6e681b00c337) (see below for compiling for older Wayfire versions)
 - [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) at least version [0.16](https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3347) (tested with [0.16.2](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/0.16.2)).
 - Development libraries for GTK, GDK, glib, gtkmm, gdkmm and boost-serialization (Ubuntu packages: `libglib2.0-dev, libgtk-3-dev, libgtkmm-3.0-dev, libboost-serialization-dev`)
 - `glib-compile-resources` (Ubuntu package: `libglib2.0-dev-bin`)
 - [nlohmann_json](https://github.com/nlohmann/json/), recommended to use the same version that Wayfire uses (currently version 3.9.1)
 - Optional, but highly recommended: [WCM](https://github.com/WayfireWM/wcm) for basic configuration
 - Optionally [libinput](https://www.freedesktop.org/wiki/Software/libinput/) version [1.70](https://lists.freedesktop.org/archives/wayland-devel/2021-February/041733.html) or higher for improved touchpad support (to allow tap-and-drag for the right and middle buttons, required for drawing gestures without physical buttons)

### Building and installing

```
meson build
ninja -C build
sudo ninja -C build install
```

If you get build errors, your Wayfire version might be too old (or too new). For older Wayfire versions, try the following:
 - For version 0.7.0, use the [wayfire-0.7 branch](https://github.com/dkondor/wstroke/tree/wayfire-0.7) (run `git checkout wayfire-0.7` before building).
 - For older Wayfire versions of the 0.8.0 series (between commits [3cca6c9](https://github.com/WayfireWM/wayfire/commit/3cca6c9fee35ea8671da2b1c3f56ca61045ea693) and [d1f33e5](https://github.com/WayfireWM/wayfire/commit/d1f33e58326175f6437d0345ac78b0bb9f03b889)), use [this state](https://github.com/dkondor/wstroke/tree/4f2e8f00e4c734ac6fc3698bc4cfc504fe47a311) (run `git checkout 4f2e8f0` before building).
 - For moderately old versions of Wayfire (between commits [d1f33e5](https://github.com/WayfireWM/wayfire/commit/d1f33e58326175f6437d0345ac78b0bb9f03b889) and
 [3ac0284](https://github.com/WayfireWM/wayfire/commit/3ac028406cc3697dd40c128721fb6e681b00c337)), use [this state](https://github.com/dkondor/wstroke/tree/0401b4f608c7d265a10fa2e7f4ce2dafb9caca4b)  (run `git checkout 0401b4f` before building).
 - For recent versions of Wayfire, use this branch (and report issues for build failures).


### Running

If correctly installed, it will show up as "Mouse Gestures" plugin in WCM and can be enabled from there, or with adding `wstroke` to the list of plugins (in `[core]`) in `~/.config/wayfire.ini`.

### Configuration

Basic options such as the button used for gestures, or the target of gestures can be changed with WCM as the "Mouse Gestures" plugin, or by manually editing the `[wstroke]` section in `~/.config/wayfire.ini`. Note: trying to set a button will likely make WCM to show a warning (e.g. "Attempting to bind `BTN_RIGHT` without modifier"); it is safe to ignore this, since wstroke will forward button clicks when needed.

Gestures can be configured by the standalone program `wstroke-config`. Recommended ways to obtain an initial configuration:
 - If you have Easystroke installed, `wstroke-config` will attempt to import gestures from it, by looking for any of the `actions*` files under `~/.easystroke`. Even without Easystroke installed, copying the content of this directory from a previous installation can be used to import gestures. It is recommended to check that importing is done correctly.
 - An example configuration file is under [example/actions-wstroke](example/actions-wstroke). You can copy this file to `~/.config/wstroke` to get started as well. Note that the plugin will still need to be enabled in WCM to work.

Gestures are stored under `~/.config/wstroke/actions-wstroke`. It is recommended not to edit this file manually, but it can be copied between different computers, or backed up and restored manually.

#### Focus settings ####
For a better experience, it is recommended to diable the "click-to-focus" feature in Wayfire for the mouse button used for gestures. This will allow wstroke to manage focus when using this button and set the target of the gesture as requested by the user.

To do this, under the "Core" tab of WCM, change the option "Mouse button to focus views" to *not* include the button used for gestures. E.g. if the right button is used, the setting here should not contain `BTN_RIGHT`, so it might look like `BTN_LEFT | BTN_MIDDLE` (note: it is best to set this by clicking on the edit button on the right of the setting and manually editing the text that corresponds to this setting). Don't be alarmed by the warning that appears ("Attempting to bind `BTN_LEFT | BTN_MIDDLE` without modifier"); this is exactly the intended behavior in this case.

The same can be achieved by editing the option `focus_buttons` in the `[core]` section of `~/.config/wayfire.ini`.

### What works

 - Importing saved strokes from "actions" files created with Easystroke (just run `wstroke-config`).
 - Drawing and recognizing stokes.
 - Actions on the active view: close, minimize, (un)maximize, move, resize (select "WM Action" and the appropriate action).
 - Actions to activate another Wayfire plugin (typical desktop interactions are under "Global Action"; "Custom Plugin" can be used with giving the plugin activator name directly), only supported for some plugins, see [here](https://github.com/WayfireWM/wayfire/issues/1811).
 - Generating keypresses ("Key" action).
 - Generating mouse clicks ("Button" action).
 - Generating modifiers ("Ignore" action -- only works in combination with mouse clicks, not the keyboard).
 - Running commands as a gesture action.
 - Getting keybindings and mouse button bindings in the configuration for actions.
 - Recording strokes (slight change: these have to be recorded on a "canvas", cannot be drawn anywhere like with Easystroke; also, recording strokes requires using a different mouse button).
 - Identifying views and using application specific gestures or excluding certain apps completely; setting these in `wstroke-config` by interactively grabbing the app-id of an open view.
 - Option to target either the view under the mouse when starting the gesture (original Easystroke behavior) or the currently active one.
 - Option to change focus to the view under the mouse after a gesture.
 - Basic timeouts (move the mouse after clicking to have a gesture / end the gesture if not moving within a timeout).
 
### What does not work

 - Scroll action (removed from settings, will be converted to Global)
 - SendText action (removed from settings, will be converted to Global)
 - Ignore in combination with keyboard keypresses.
 - Individual settings (which button, timeout) for each pointing device
 - Advanced gestures
 - Touchscreen and pen / stylus support

