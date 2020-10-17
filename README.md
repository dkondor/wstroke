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


### What works

 - Importing saved strokes from "actions" files created with Easystroke (just run `wstroke-config`).
 - Drawing and recognizing stokes (will log output with matches).
 - Actions on the active view: close, minimize, (un)maximize, move, resize (select "WM Actions" and the appropriate action).
 - Generating keypresses ("Key" action).
 - Getting keybindings and mouse button bindings in the configuration for actions.
 - Recording strokes (slight change: these have to be recorded on a "canvas", cannot be drawn anywhere like with Easystroke; also, recording strokes requires using a different mouse button).
 
### What does not work

 - Any other action (ignore, button press, scroll, running commands, etc.)
 - Identifying applications / adding new applications.
 - Targeting the view under the mouse instead of a current active one.
 - Reloading the configuration if edited while Wayfire is running (has to reload the plugin to apply any changes).

