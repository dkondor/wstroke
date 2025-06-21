# wstroke

Port of [Easystroke mouse gestures](https://github.com/thjaeger/easystroke) as a plugin for [Wayfire](https://github.com/WayfireWM/wayfire). Mouse gestures are shapes drawn on the screen while holding down one of the buttons (typically the right or middle button). This plugin allows associating such gestures with various actions. See the [Wiki](https://github.com/dkondor/wstroke/wiki) for more explanations and examples.

Packages are available for:
 - Ubuntu 24.04: https://launchpad.net/~kondor-dani/+archive/ubuntu/ppa-wstroke
 - Debian (testing and unstable): in the official [repository](https://packages.debian.org/testing/wstroke).

### Dependencies

 - [Wayfire](https://github.com/WayfireWM/wayfire), the current development version, i.e. 0.10.0, after commit [544427d](https://github.com/WayfireWM/wayfire/pull/2613) (see below for compiling on older Wayfire versions)
 - [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) version [0.18](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/0.18?ref_type=heads).
 - Development libraries for GTK, GDK, glib, cairo, pixman, gtkmm, gdkmm and boost-serialization (Ubuntu packages: `libglib2.0-dev, libgtk-3-dev, libcairo2-dev, libpixman-1-dev, libgtkmm-3.0-dev, libboost-serialization-dev`)
 - `glib-compile-resources` (Ubuntu package: `libglib2.0-dev-bin`)
 - [Vala](https://vala.dev/) compiler (for building, Ubuntu package: `valac`; or use the [no_vala](https://github.com/dkondor/wstroke/tree/no_vala) branch instead)
 - Optional, but highly recommended: [WCM](https://github.com/WayfireWM/wcm) for basic configuration
 - Optionally [libinput](https://www.freedesktop.org/wiki/Software/libinput/) version [1.17](https://lists.freedesktop.org/archives/wayland-devel/2021-February/041733.html) or higher for improved touchpad support (to allow tap-and-drag for the right and middle buttons, required for drawing gestures without physical buttons)

### Building and installing

```
meson build
ninja -C build
sudo ninja -C build install
```

If you get build errors, your Wayfire version might be too old (or too new). For older Wayfire versions, try the following:
 - For version 0.7.0, use the [wayfire-0.7 branch](https://github.com/dkondor/wstroke/tree/wayfire-0.7) (run `git checkout wayfire-0.7` before building).
 - For older Wayfire versions of the 0.8.0 series (between commits [3cca6c9](https://github.com/WayfireWM/wayfire/commit/3cca6c9fee35ea8671da2b1c3f56ca61045ea693) and [d1f33e5](https://github.com/WayfireWM/wayfire/commit/d1f33e58326175f6437d0345ac78b0bb9f03b889)), use [this state](https://github.com/dkondor/wstroke/tree/4f2e8f00e4c734ac6fc3698bc4cfc504fe47a311) (run `git checkout 4f2e8f0` before building). If using multiple monitors, you can separately apply the fix to [issue #5](https://github.com/dkondor/wstroke/issues/5): `git cherry-pick 1c02905a4e`
 - For moderately old versions of Wayfire (between commits [d1f33e5](https://github.com/WayfireWM/wayfire/commit/d1f33e58326175f6437d0345ac78b0bb9f03b889) and
 [3ac0284](https://github.com/WayfireWM/wayfire/commit/3ac028406cc3697dd40c128721fb6e681b00c337)), use [this state](https://github.com/dkondor/wstroke/tree/0401b4f608c7d265a10fa2e7f4ce2dafb9caca4b)  (run `git checkout 0401b4f` before building). If using multiple monitors, you can separately apply the fix to [issue #5](https://github.com/dkondor/wstroke/issues/5): `git cherry-pick 1c02905a4e`
 - For version [0.8.0](https://github.com/WayfireWM/wayfire/tree/v0.8.0),  [0.9.0](https://github.com/WayfireWM/wayfire/tree/v0.9.0), or the development version up to commit [448ce8b](https://github.com/WayfireWM/wayfire/commit/448ce8b5be341f91b9f3b10ee9d4f3ea8cd57819), use releases of the 2.2 series, the latest available one is [2.2.1](https://github.com/dkondor/wstroke/tree/v2.2.1) (run `git checkout v2.2.1` before building). This version supports building against wlroots versions 0.16-0.18. However, the version of wlroots should be the same that was used for building Wayfire (this should be detected during compilation).
 - For older versions in the 0.10.0 series (between commits [fe33cfa](https://github.com/WayfireWM/wayfire/commit/fe33cfa5b03f11cb9749694929cc33583464bda0) and [544427d](https://github.com/WayfireWM/wayfire/commit/544427d617c9f643af2e2f3dbcbff5513c7d55f1), use releases of the 2.3 series, the latest available one is [2.3.0](https://github.com/dkondor/wstroke/tree/v2.3.0) (run `git checkout v2.3.0` before building). This requires wlroots version 0.18.
 - For recent versions of Wayfire (0.10.0 or newer, after commit [544427d](https://github.com/WayfireWM/wayfire/commit/544427d617c9f643af2e2f3dbcbff5513c7d55f1), use this branch (and report issues for build failures).


### Running

If correctly installed, it will show up as "Mouse Gestures" plugin in WCM and can be enabled from there, or with adding `wstroke` to the list of plugins (in `[core]`) in `~/.config/wayfire.ini`.

### Configuration

Basic options such as the button used for gestures, or the target of gestures can be changed with WCM as the "Mouse Gestures" plugin, or by manually editing the `[wstroke]` section in `~/.config/wayfire.ini`. Note: trying to set a button will likely make WCM to show a warning (e.g. "Attempting to bind `BTN_RIGHT` without modifier"); it is safe to ignore this, since wstroke will forward button clicks when needed.

Gestures can be configured by the standalone program `wstroke-config`. Recommended ways to obtain an initial configuration:
 - If you have Easystroke installed, `wstroke-config` will attempt to import gestures from it, by looking for any of the `actions*` files under `~/.easystroke`. Even without Easystroke installed, copying the content of this directory from a previous installation can be used to import gestures. It is recommended to check that importing is done correctly.
 - An example configuration file is under [example/actions-wstroke-2](example/actions-wstroke-2). This is installed automatically and will be used by `wstroke-config` as default if no other configuration exists. You can copy this file to `~/.config/wstroke` manually as well.

Gestures are stored under `wstroke/actions-wstroke-2` in the directory given by the `XDG_CONFIG_HOME` environment variable (`~/.config` by default). It is recommended not to edit this file manually, but it can be copied between different computers, or backed up and restored manually.

#### Focus settings ####
For a better experience, it is recommended to disable the "click-to-focus" feature in Wayfire for the mouse button used for gestures. This will allow wstroke to manage focus when using this button and set the target of the gesture as requested by the user.

To do this, under the "Core" tab of WCM, change the option "Mouse button to focus views" to *not* include the button used for gestures. E.g. if the right button is used, the setting here should not contain `BTN_RIGHT`, so it might look like `BTN_LEFT | BTN_MIDDLE` (note: it is best to set this by clicking on the edit button on the right of the setting and manually editing the text that corresponds to this setting). Don't be alarmed by the warning that appears ("Attempting to bind `BTN_LEFT | BTN_MIDDLE` without modifier"); this is exactly the intended behavior in this case.

The same can be achieved by editing the option `focus_buttons` in the `[core]` section of `~/.config/wayfire.ini`.

### What works

 - Importing saved strokes from "actions" files created with Easystroke (just run `wstroke-config`).
 - Drawing and recognizing strokes.
 - Drawing strokes with all supported renderer backends (EGL, Vulkan and pixman).
 - Actions on the active view: close, minimize, (un)maximize, move, resize (select "WM Action" and the appropriate action).
 - Actions to activate another Wayfire plugin (typical desktop interactions are under "Global Action"; "Custom Plugin" can be used with giving the plugin activator name directly), only supported for some plugins, see [here](https://github.com/WayfireWM/wayfire/issues/1811).
 - Generating keypresses ("Key" action).
 - Generating mouse clicks ("Button" action).
 - Generating modifiers ("Ignore" action -- only works in combination with mouse clicks, not the keyboard).
 - Emulating touchpad "gestures" with mouse movement, such as scrolling or pinch zoom in apps that support it ("Touchpad Gesture" action; "Scroll" action from Easystroke will be converted to this).
 - Running commands as a gesture action.
 - Getting keybindings and mouse button bindings in the configuration for actions.
 - Recording strokes (slight change: these have to be recorded on a "canvas", cannot be drawn anywhere like with Easystroke; also, recording strokes requires using a different mouse button).
 - Identifying views and using application specific gestures or excluding certain apps completely; setting these in `wstroke-config` by interactively grabbing the app-id of an open view.
 - Option to target either the view under the mouse when starting the gesture (original Easystroke behavior) or the currently active one.
 - Option to change focus to the view under the mouse after a gesture.
 - Basic timeouts (move the mouse after clicking to have a gesture / end the gesture if not moving within a timeout).
 
### What does not work

 - SendText action (removed from settings, will be converted to Global)
 - Ignore action in combination with keyboard keypresses.
 - Individual settings (which button, timeout) for each pointing device
 - Advanced gestures
 - Touchscreen and pen / stylus support
 - Drawing strokes with the Vulkan renderer (`WLR_RENDERER=vulkan`) is inefficient (suggestions for improvement are welcome).

