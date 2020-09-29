/*
 * input_events.cpp -- interface to generate input events in Wayfire
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

#include "input_events.hpp"

#include <wayland-server-core.h>

extern "C" {
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
}

#include <wayfire/util/log.hpp>
#include <wayfire/core.hpp>

void input_headless::init() {
	auto& core = wf::compositor_core_t::get();
	/* 1. create backend */
	headless_backend = wlr_headless_backend_create_with_renderer(core.display, core.renderer);
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
	if(!wlr_backend_start(headless_backend)) {
		LOGE("Cannot start headless wlroots backend!");
		wlr_multi_backend_remove(core.backend, headless_backend);
		wlr_backend_destroy(headless_backend);
		headless_backend = nullptr;
		return;
	}
	/* 4. create the new input device */
	input_pointer = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_POINTER);
	if(!input_pointer) {
		LOGE("Cannot create pointer device!");
		fini();
		return;
	}
	input_keyboard = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_KEYBOARD);
	if(!input_keyboard) {
		LOGE("Cannot create keyboard device!");
		fini();
		return;
	}
}

void input_headless::fini() {
	if(headless_backend) {
		auto& core = wf::compositor_core_t::get();
		wlr_multi_backend_remove(core.backend, headless_backend);
		wlr_backend_destroy(headless_backend);
		headless_backend = nullptr;
		input_pointer = nullptr;
		input_keyboard = nullptr;
	}
}

void input_headless::pointer_button(uint32_t time_msec, uint32_t button, enum wlr_button_state state) {
	if(!(input_pointer && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGI("Emitting pointer button event");
	wlr_event_pointer_button ev;
    ev.device = input_pointer;
    ev.button = button;
    ev.state = state;
    ev.time_msec = time_msec;
    wl_signal_emit(&(input_pointer->pointer->events.button), &ev);
}

void input_headless::keyboard_key(uint32_t time_msec, uint32_t key, enum wlr_key_state state) {
	if(!(input_keyboard && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGI("Emitting keyboard event ", key, state == WLR_KEY_PRESSED ? ", pressed" : ", released");
	wlr_event_keyboard_key ev;
	ev.keycode = key;
	ev.state = state;
	ev.update_state = false;
	ev.time_msec = time_msec;
	wl_signal_emit(&(input_keyboard->keyboard->events.key), &ev);
}

void input_headless::keyboard_mods(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked) {
	if(!(input_keyboard && headless_backend)) {
		LOGW("No input device created!");
		return;
	}
	LOGI("Changing keyboard modifiers");
	wlr_keyboard_notify_modifiers(input_keyboard->keyboard, mods_depressed, mods_latched, mods_locked, 0);
}
