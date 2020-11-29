/*
 * toplevel-grabber-test.c -- test selecting a toplevel view based on
 * 	user interaction
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


#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <toplevel-grabber.h>

static int is_done = 0;

static void tl_cb(void* data, struct tl_grabber* gr) {
	char* app_id = toplevel_grabber_get_app_id(gr);
	printf("Activated app: %s\n", app_id ? app_id : "(null)");
	if(app_id) free(app_id);
	toplevel_grabber_free(gr);
	is_done = 1;
}


int main() {
	struct wl_display* dpy = wl_display_connect(NULL);
	if(!dpy) {
		fprintf(stderr, "Cannot connect to display!\n");
		return 1;
	}
	
	struct tl_grabber* gr = toplevel_grabber_new(dpy, NULL, NULL);
	if(!gr) {
		fprintf(stderr, "Cannot create grabber interface!\n");
		return 1;
	}
	printf("Starting grabber, click to select a toplevel view\n");
	toplevel_grabber_set_callback(gr, tl_cb, NULL);
	
	while(!(is_done || wl_display_dispatch(dpy) == -1));
	if(!is_done) toplevel_grabber_free(gr);
	
	return 0;
}

