# wstroke

Experimental port of [Easystroke mouse gestures](https://github.com/thjaeger/easystroke) as a plugin for [Wayfire](https://github.com/WayfireWM/wayfire).

### Dependencies

 - [Wayfire](https://github.com/WayfireWM/wayfire). Needs a patch to propagate mouse clicks, either from [this branch](https://github.com/dkondor/wayfire/tree/fake_mouse_button) or just use the `fake_mouse_button.patch` file in this repository
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

If correctly installed, it will show up as "Easystroke mouse gestures" plugin in WCM and can be enabled from there. Also, the mouse button for gestures can be changed there. Gesture actions can be configured by running `wstroke-config`. Configuration files are stored under `~/.config/wstroke`, in a format compatible with original Easystroke. `wstroke-config` will read and copy any configuration from under `~/.easystroke` if this directory is empty.


### What works

 - Reading saved strokes from "actions" files created with Easystroke.
 - Drawing and recognizing stokes (will log output with matches).
 - Closing and minimizing the active view (select "WM Actions" and the appropriate action).
 - Getting keybindings and mouse button bindings in the configuration for actions.
 
### What does not work

 - Any other action (generating key or button press, scroll, running commands, etc.)
 - Recording new strokes / editing strokes.
 - Identifying applications / adding new applications.
