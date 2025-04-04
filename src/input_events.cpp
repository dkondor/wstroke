/*
 * input_events.cpp -- interface to generate input events in Wayfire
 * 
 * Copyright 2020-2024 Daniel Kondor <kondor.dani@gmail.com>
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

#include "input_events.hpp"

#include <wayland-server-core.h>

extern "C" {
#define static
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#undef static
}

#include <wayfire/util/log.hpp>
#include <wayfire/core.hpp>

static const struct wlr_pointer_impl ws_headless_pointer_impl = {
	.name = "wstroke-pointer",
};

static const struct wlr_keyboard_impl ws_headless_keyboard_impl = {
	.name = "wstroke-keyboard",
	.led_update = nullptr
};


void input_headless::init() {
	auto& core = wf::compositor_core_t::get();
	/* 1. create headless backend */
	headless_backend = wlr_headless_backend_create(core.ev_loop);
	if(!headless_backend) {
		LOGE("Cannot create headless wlroots backend!");
		return;
	}
	/* 2. add to the core backend */
	if(!wlr_multi_backend_add(core.backend, headless_backend)) {
		LOGE("Cannot add headless wlroots backend!");
		wlr_backend_destroy(headless_backend);
		headless_backend = nullptr;
		return;
	}
	/* 3. start the new headless backend */
	start_backend();
	
	/* 4. create the new input device */
	input_pointer = (struct wlr_pointer*)calloc(1, sizeof(struct wlr_pointer));
	if(!input_pointer) {
		LOGE("Cannot create pointer device!");
		fini();
		return;
	}
	wlr_pointer_init(input_pointer, &ws_headless_pointer_impl, ws_headless_pointer_impl.name);
	
	
	input_keyboard = (struct wlr_keyboard*)calloc(1, sizeof(struct wlr_keyboard));
	if(!input_keyboard) {
		LOGE("Cannot create keyboard device!");
		fini();
		return;
	}
	wlr_keyboard_init(input_keyboard, &ws_headless_keyboard_impl, ws_headless_keyboard_impl.name);
	
	wl_signal_emit_mutable(&headless_backend->events.new_input, input_keyboard);
	wl_signal_emit_mutable(&headless_backend->events.new_input, input_pointer);
}

void input_headless::start_backend() {
	if(!wlr_backend_start(headless_backend)) {
		LOGE("Cannot start headless wlroots backend!");
		fini();
	}
}

void input_headless::fini() {
	if(input_pointer) {
		wlr_pointer_finish(input_pointer);
		free(input_pointer);
		input_pointer = nullptr;
	}
	if(input_keyboard) {
		wlr_keyboard_finish(input_keyboard);
		free(input_keyboard);
		input_keyboard = nullptr;
	}
	if(headless_backend) {
		auto& core = wf::compositor_core_t::get();
		wlr_multi_backend_remove(core.backend, headless_backend);
		wlr_backend_destroy(headless_backend);
		headless_backend = nullptr;
	}
}

void input_headless::pointer_button(uint32_t time_msec, uint32_t button, enum WSTROKE_BUTTON_STATE state) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer button event");
	wlr_pointer_button_event ev;
    ev.pointer = input_pointer;
    ev.button = button;
    ev.state = state;
    ev.time_msec = time_msec;
    wl_signal_emit(&(input_pointer->events.button), &ev);
}

void input_headless::pointer_scroll(uint32_t time_msec, double delta, enum WSTROKE_AXIS_ORIENTATION o) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer scroll event");
	wlr_pointer_axis_event ev;
	ev.pointer = input_pointer;
	ev.time_msec = time_msec;
	ev.source = WL_POINTER_AXIS_SOURCE_CONTINUOUS;
	ev.orientation = o;
	ev.delta = delta;
	ev.delta_discrete = delta * WLR_POINTER_AXIS_DISCRETE_STEP;;
	wl_signal_emit(&(input_pointer->events.axis), &ev);
}

void input_headless::pointer_start_swipe(uint32_t time_msec, uint32_t fingers) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer swipe begin event");
	wlr_pointer_swipe_begin_event ev;
	ev.pointer = input_pointer;
	ev.time_msec = time_msec;
	ev.fingers = fingers;
	wl_signal_emit(&(input_pointer->events.swipe_begin), &ev);
}

void input_headless::pointer_update_swipe(uint32_t time_msec, uint32_t fingers, double dx, double dy) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer swipe update event");
	wlr_pointer_swipe_update_event ev;
	ev.pointer = input_pointer;
	ev.time_msec = time_msec;
	ev.fingers = fingers;
	ev.dx = dx;
	ev.dy = dy;
	wl_signal_emit(&(input_pointer->events.swipe_update), &ev);
}

void input_headless::pointer_end_swipe(uint32_t time_msec, bool cancelled) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer swipe end event");
	wlr_pointer_swipe_end_event ev;
	ev.pointer = input_pointer;
	ev.time_msec = time_msec;
	ev.cancelled = cancelled; //!! note: conversion from C++ bool to C99/C23 bool !!
	wl_signal_emit(&(input_pointer->events.swipe_end), &ev);
}

void input_headless::pointer_start_pinch(uint32_t time_msec, uint32_t fingers) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer pinch begin event");
	wlr_pointer_pinch_begin_event ev;
	ev.pointer = input_pointer;
	ev.time_msec = time_msec;
	ev.fingers = fingers;
	wl_signal_emit(&(input_pointer->events.pinch_begin), &ev);
}

void input_headless::pointer_update_pinch(uint32_t time_msec, uint32_t fingers, double dx, double dy, double scale, double rotation) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer pinch update event");
	wlr_pointer_pinch_update_event ev;
	ev.pointer = input_pointer;
	ev.time_msec = time_msec;
	ev.fingers = fingers;
	ev.dx = dx;
	ev.dy = dy;
	ev.scale = scale;
	ev.rotation = rotation;
	wl_signal_emit(&(input_pointer->events.pinch_update), &ev);
}

void input_headless::pointer_end_pinch(uint32_t time_msec, bool cancelled) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting pointer pinch end event");
	wlr_pointer_pinch_end_event ev;
	ev.pointer = input_pointer;
	ev.time_msec = time_msec;
	ev.cancelled = cancelled; //!! note: conversion from C++ bool to C99/C23 bool !!
	wl_signal_emit(&(input_pointer->events.pinch_end), &ev);
}

void input_headless::keyboard_key(uint32_t time_msec, uint32_t key, enum wl_keyboard_key_state state) {
	if(!(input_keyboard && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Emitting keyboard event ", key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? ", pressed" : ", released");
	wlr_keyboard_key_event ev;
	ev.keycode = key;
	ev.state = (decltype(ev.state))state;
	ev.update_state = true;
	ev.time_msec = time_msec;
	wl_signal_emit(&(input_keyboard->events.key), &ev);
}

void input_headless::keyboard_mods(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked) {
	if(!(input_keyboard && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGD("Changing keyboard modifiers");
	wlr_keyboard_notify_modifiers(input_keyboard, mods_depressed, mods_latched, mods_locked, 0);
/*	struct wlr_seat* seat = wf::get_core().get_current_seat(); -- does not work: combining with the "real" keyboard
	struct wlr_keyboard_modifiers modifiers;
	modifiers.depressed = mods_depressed;
	modifiers.latched = mods_latched;
	modifiers.locked = mods_locked;
	modifiers.group = 0; // ??
	wlr_seat_keyboard_notify_modifiers(seat, &modifiers); */
}

