# wstroke

Experimental port of [Easystroke mouse gestures](https://github.com/thjaeger/easystroke) as a plugin for [Wayfire](https://github.com/WayfireWM/wayfire).

### Dependencies

 - [Wayfire](https://github.com/WayfireWM/wayfire).
 - Development libraries for GTK, GDK, glib, gtkmm, gdkmm and boost-serialization (Ubuntu packages: `libglib2.0-dev, libgtk-3-dev, libgtkmm-3.0-dev, libboost-serialization-dev`
 - `glib-compile-resources` (Ubuntu package: `libglib2.0-dev-bin`)
 - Potentially [wcm](https://github.com/WayfireWM/wcm) to easily enable

### Building and installing

```
meson build
ninja -C build
sudo ninja -C build install
```

### Running

If correctly installed, it will show up as "Easystroke mouse gestures" plugin in WCM and can be enabled from there. Also, the mouse button for gestures can be changed there. Gesture actions can be configured by running `wstroke-config`. Configuration files are stored under `~/.config/wstroke`, in a format slightly updated from the original Easystroke. `wstroke-config` will read and convert any configuration from under `~/.easystroke` if this directory is empty.

An example configuration file is under [example/actions-wstroke](example/actions-wstroke). You can copy this file to `~/.config/wstroke` to get started as well. Note that the plugin will still need to be enabled in WCM to work.

### What works

 - Importing saved strokes from "actions" files created with Easystroke (just run `wstroke-config`).
 - Drawing and recognizing stokes (will log output with matches).
 - Actions on the active view: close, minimize, (un)maximize, move, resize (select "WM Action" and the appropriate action).
 - Actions to activate another Wayfire plugin (typical desktop interactions are under "Global Action" or "Custom Plugin" can be used with giving the plugin activator name directly).
 - Generating keypresses ("Key" action).
 - Running commands as a gesture action.
 - Getting keybindings and mouse button bindings in the configuration for actions.
 - Recording strokes (slight change: these have to be recorded on a "canvas", cannot be drawn anywhere like with Easystroke; also, recording strokes requires using a different mouse button).
 - Identifying views and using application specific gestures or excluding certain apps completely.
 - Option to target either the view under the mouse when starting the gesture (original Easystroke behavior) or the currently active one.
 - Interactively identifying running applications in `wstroke-config` (e.g. to add application specific gestures or exclude them).
 
### What does not work

 - Any other action (ignore, button press, scroll, etc.)

