# wstroke

Port of [Easystroke mouse gestures](https://github.com/thjaeger/easystroke) as a plugin for [Wayfire](https://github.com/WayfireWM/wayfire). Mouse gestures are shapes drawn on the screen while holding down one of the buttons (typically the right or middle button). This plugin allows associating such gestures with various actions. See the [Wiki](https://github.com/dkondor/wstroke/wiki) for more explanations and examples.

Note: this branch requires a recent version of Wayfire and wlroots (see below). For older versions, use the [wayfire-0.7 branch](https://github.com/dkondor/wstroke/tree/wayfire-0.7).

### Dependencies

 - [Wayfire](https://github.com/WayfireWM/wayfire) a recent git version from the 0.8.0 branch, at least commit [48c3048](https://github.com/WayfireWM/wayfire/pull/1864/commits/48c30481afe47c8235885d2a2c7378091e6293f2)) and [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) at least version [0.16](https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3347) (tested with [0.16.2](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/0.16.2)).
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

