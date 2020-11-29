/*
 * toplevel-grabber.c -- library using the wlr-foreign-toplevel
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include <toplevel-grabber.h>

typedef struct zwlr_foreign_toplevel_handle_v1 wfthandle;

/* struct to hold information for one instance of the grabber */
struct tl_grabber {
	struct zwlr_foreign_toplevel_manager_v1* manager;
	struct wl_list toplevels;
	void (*callback)(void* data, struct tl_grabber* gr);
	void* data;
	struct toplevel* active;
	int init_done;
};

/* struct to hold information about one toplevel */
struct toplevel {
	char* app_id;
	wfthandle* handle;
	wfthandle* parent;
	struct tl_grabber* gr;
	int init_done;
	struct wl_list link;
};


#ifndef G_GNUC_UNUSED
#define G_GNUC_UNUSED __attribute__((unused))
#endif

/* callbacks */

static void title_cb(G_GNUC_UNUSED void* data, G_GNUC_UNUSED wfthandle* handle,
		G_GNUC_UNUSED const char* title) {
	/* don't care */
}

static void appid_cb(void* data, G_GNUC_UNUSED wfthandle* handle, const char* app_id) {
	if(!(app_id && data)) return;
	struct toplevel* tl = (struct toplevel*)data;
	if(tl->app_id) free(tl->app_id);
	tl->app_id = strdup(app_id);
}

void output_enter_cb(G_GNUC_UNUSED void* data, G_GNUC_UNUSED wfthandle* handle,
		G_GNUC_UNUSED struct wl_output* output) {
	/* don't care */
}
void output_leave_cb(G_GNUC_UNUSED void* data, G_GNUC_UNUSED wfthandle* handle,
		G_GNUC_UNUSED struct wl_output* output) {
	/* don't care */
}

void state_cb(void* data, G_GNUC_UNUSED wfthandle* handle, struct wl_array* state) {
	if(!(data && state)) return;
	struct toplevel* tl = (struct toplevel*)data;
	struct tl_grabber* gr = tl->gr;
	if(!gr) return;
	int activated = 0;
	int i;
	uint32_t* stdata = (uint32_t*)state->data;
	for(i = 0; i*sizeof(uint32_t) < state->size; i++) {
		if(stdata[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
            activated = 1;
            break;
		}
	}
	if(activated) {
		int orig_init_done = tl->init_done;
		struct toplevel* new_active = NULL;
		while(tl) {
			new_active = tl;
			if(!tl->parent) break;
			tl = zwlr_foreign_toplevel_handle_v1_get_user_data(tl->parent);
		}
		if(new_active != gr->active) {
			gr->active = new_active;
			if(orig_init_done && gr->callback) gr->callback(gr->data, gr);
		}
	}
}

void done_cb(void* data, G_GNUC_UNUSED wfthandle* handle) {
	if(!data) return;
	struct toplevel* tl = (struct toplevel*)data;
	tl->init_done = 1;
}

void closed_cb(void* data, G_GNUC_UNUSED wfthandle* handle) {
	if(!data) return;
	struct toplevel* tl = (struct toplevel*)data;
	/* note: we can assume that this toplevel is not set as the parent
	 * of any existing toplevels at this point */
	wl_list_remove(&(tl->link));
	zwlr_foreign_toplevel_handle_v1_destroy(tl->handle);
	if(tl->app_id) free(tl->app_id);
	free(tl);
}

void parent_cb(void* data, G_GNUC_UNUSED wfthandle* handle, wfthandle* parent) {
	if(!data) return;
	struct toplevel* tl = (struct toplevel*)data;
	tl->parent = parent;
}

struct zwlr_foreign_toplevel_handle_v1_listener toplevel_handle_interface = {
    .title        = title_cb,
    .app_id       = appid_cb,
    .output_enter = output_enter_cb,
    .output_leave = output_leave_cb,
    .state        = state_cb,
    .done         = done_cb,
    .closed       = closed_cb,
    .parent       = parent_cb
};

/* register new toplevel */
static void new_toplevel(void *data, G_GNUC_UNUSED struct zwlr_foreign_toplevel_manager_v1 *manager,
		wfthandle *handle) {
	if(!handle) return;
	if(!data) {
		/* if we unset the user data pointer, then we don't care anymore */
		zwlr_foreign_toplevel_handle_v1_destroy(handle);
		return;
	}
	struct tl_grabber* gr = (struct tl_grabber*)data;
	struct toplevel* tl = (struct toplevel*)malloc(sizeof(struct toplevel));
	if(!tl) {
		/* TODO: error message */
		return;
	}
	tl->app_id = NULL;
	tl->handle = handle;
	tl->parent = NULL;
	tl->init_done = 0;
	tl->gr = gr;
	wl_list_insert(&(gr->toplevels), &(tl->link));
	
	/* note: we cannot do anything as long as we get app_id */
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_handle_interface, tl);
}

/* sent when toplevel management is no longer available -- this will happen after stopping */
static void toplevel_manager_finished(G_GNUC_UNUSED void *data,
		struct zwlr_foreign_toplevel_manager_v1 *manager) {
    zwlr_foreign_toplevel_manager_v1_destroy(manager);
}

static struct zwlr_foreign_toplevel_manager_v1_listener toplevel_manager_interface = {
    .toplevel = new_toplevel,
    .finished = toplevel_manager_finished,
};

static void registry_global_add_cb(void *data, struct wl_registry *registry,
		uint32_t id, const char *interface, uint32_t version) {
	struct tl_grabber* gr = (struct tl_grabber*)data;
	if(!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
		uint32_t v = zwlr_foreign_toplevel_manager_v1_interface.version;
		if(version < v) v = version;
		gr->manager = wl_registry_bind(registry, id, &zwlr_foreign_toplevel_manager_v1_interface, v);
		if(gr->manager)
			zwlr_foreign_toplevel_manager_v1_add_listener(gr->manager, &toplevel_manager_interface, gr);
		else { /* TODO: handle error */ }
	}
	gr->init_done = 0;
}

static void registry_global_remove_cb(G_GNUC_UNUSED void *data,
		G_GNUC_UNUSED struct wl_registry *registry, G_GNUC_UNUSED uint32_t id) {
	/* don't care */
}

static const struct wl_registry_listener registry_listener = {
	registry_global_add_cb,
	registry_global_remove_cb
};

struct tl_grabber* toplevel_grabber_new(struct wl_display* dpy,
		void (*callback)(void* data, struct tl_grabber* gr), void* data) {
	if(!dpy) {
		/* TODO: get display! */
		return NULL;
	}
	
	struct tl_grabber* gr = (struct tl_grabber*)malloc(sizeof(struct tl_grabber));
	if(!gr) return NULL;
	
	gr->manager = NULL;
	wl_list_init(&(gr->toplevels));
	gr->callback = callback;
	gr->data = data;
	gr->active = NULL;
	
	struct wl_registry* registry = wl_display_get_registry(dpy);
	wl_registry_add_listener(registry, &registry_listener, gr);
	do {
		gr->init_done = 1;
		wl_display_roundtrip(dpy);
	}
	while(!gr->init_done);
	return gr;
}

char* toplevel_grabber_get_app_id(struct tl_grabber* gr) {
	char* ret = NULL;
	if(gr && gr->active && gr->active->app_id) ret = strdup(gr->active->app_id);
	return ret;
}

/*
void toplevel_grabber_reset(struct tl_grabber* gr) {
	if(gr) gr->active = NULL;
}
*/

void toplevel_grabber_set_callback(struct tl_grabber* gr,
		void (*callback)(void* data, struct tl_grabber* gr), void* data) {
	if(gr) {
		gr->callback = callback;
		gr->data = data;
	}
}

int toplevel_grabber_activate_app(struct tl_grabber* gr,
		const char* app_id, struct wl_seat* wl_seat, int parent) {
	if(!(gr && app_id)) return -1;
	struct toplevel* tl;
	wl_list_for_each(tl, &(gr->toplevels), link) {
		if(!strcmp(app_id, tl->app_id)) {
			if(parent) while(tl->parent) {
				struct toplevel* tmp = zwlr_foreign_toplevel_handle_v1_get_user_data(tl->parent);
				if(!tmp) break;
				tl = tmp;
			}
			zwlr_foreign_toplevel_handle_v1_activate (tl->handle, wl_seat);
			return 0;
		}
	}
	return -1;
}

void toplevel_grabber_free(struct tl_grabber* gr) {
	if(!gr) return;
	/* stop listening and also free all existing toplevels */
	/* set user data to null -- this will stop adding newly reported toplevels */
	zwlr_foreign_toplevel_manager_v1_set_user_data(gr->manager, NULL);
	/* this will send the finished signal and result in destroying manager later */
	zwlr_foreign_toplevel_manager_v1_stop(gr->manager);
	gr->manager = NULL;
	/* destroy all existing toplevel handles */
	struct toplevel* tl;
	struct toplevel* tmp;
	wl_list_for_each_safe(tl, tmp, &(gr->toplevels), link) {
		wl_list_remove(&(tl->link));
		zwlr_foreign_toplevel_handle_v1_destroy(tl->handle);
		if(tl->app_id) free(tl->app_id);
		free(tl);
	}
}


