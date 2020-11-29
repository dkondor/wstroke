/*
 * toplevel-grabber.h -- library using the wlr-foreign-toplevel
 * 	interface to get the ID of an activated toplevel view
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


#ifndef TOPLEVEL_GRABBER_H
#define TOPLEVEL_GRABBER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <wayland-client.h>

struct tl_grabber;

/*
 * Create a new grabber and start listening to events about toplevels.
 * Parameters:
 * 	dpy -- Wayland display connectoin to use (must be given by the caller)
 *  callback -- function to be called when a new toplevel is activated (can be NULL)
 *  data -- user data to pass to the callback function
 */
struct tl_grabber* toplevel_grabber_new(struct wl_display* dpy,
		void (*callback)(void* data, struct tl_grabber* gr), void* data);

/*
 * Get the last activated app ID (if any). Returns a copy of the app ID,
 * or NULL if no app was activated or no app ID can be determined. If the
 * return value is non-NULL, the caller must free() it.
 */
char* toplevel_grabber_get_app_id(struct tl_grabber* gr);

/* 
 * Reset the currently active app, i.e. toplevel_grabber_get_app_id()
 * will return NULL until a new toplevel is activated again.
 * void toplevel_grabber_reset(struct tl_grabber* gr);
 */

/* 
 * Set the callback function to be called when a new toplevel is activated.
 */
void toplevel_grabber_set_callback(struct tl_grabber* gr,
		void (*callback)(void* data, struct tl_grabber* gr), void* data);

/* 
 * Activate any toplevel view with a matching app_id on the given seat.
 * If parent is true, it selects the topmost parent if multiple views
 * in a hierarchy have the same app ID.
 * This is useful to re-show the caller's view after the user has
 * selected another app by activating it.
 * Returns 0 on success or -1 if the given app ID was not found.
 */
int toplevel_grabber_activate_app(struct tl_grabber* gr,
		const char* app_id, struct wl_seat* wl_seat, int parent);

/* Stop listening to toplevel events and free all resources associated
 * with this instance.
 */
void toplevel_grabber_free(struct tl_grabber* gr);

#ifdef __cplusplus
}
#endif

#endif

