/*
 * input_inhibitor.c -- inhibitor for grabbing key combinations
 * 
 * Copyright 2020 Daniel Kondor <kondor.dani@gmail.com>
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 */


#include <input_inhibitor.h>
#include <wlr-input-inhibitor-unstable-v1-client-protocol.h>
#include <gdk/gdkwayland.h>

static struct zwlr_input_inhibitor_v1* grab = NULL;
static struct zwlr_input_inhibit_manager_v1* inhibitor = NULL;


static void _add(G_GNUC_UNUSED void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, G_GNUC_UNUSED uint32_t version) {
	if(strcmp(interface, zwlr_input_inhibit_manager_v1_interface.name) == 0)
		inhibitor = (struct zwlr_input_inhibit_manager_v1*)wl_registry_bind(registry, name, &zwlr_input_inhibit_manager_v1_interface, 1u);
}

static void _remove(G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_registry *registry, G_GNUC_UNUSED uint32_t name) { }

static struct wl_registry_listener listener = { &_add, &_remove };


gboolean input_inhibitor_init() {
	struct wl_display* display = gdk_wayland_display_get_wl_display(gdk_display_get_default());
	if(!display) return FALSE;
	
	struct wl_registry* registry = wl_display_get_registry(display);
	if(!registry) return FALSE;

	wl_registry_add_listener(registry, &listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	if (!inhibitor) return FALSE;
	return TRUE;
}

gboolean input_inhibitor_grab() {
	if(!inhibitor) return FALSE;
	if(grab) return TRUE;
	grab = zwlr_input_inhibit_manager_v1_get_inhibitor(inhibitor);
	if(!grab) return FALSE;
	return TRUE;
}

void input_inhibitor_ungrab() {
	if(grab) {
		zwlr_input_inhibitor_v1_destroy(grab);
		wl_display_flush(gdk_wayland_display_get_wl_display(gdk_display_get_default()));
		grab = NULL;
	}
}


