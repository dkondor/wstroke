/*
 * input_events.hpp -- interface to generate input events in Wayfire
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


#ifndef INPUT_EVENTS_HPP
#define INPUT_EVENTS_HPP

extern "C" {
#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>
#include <wayland-server-protocol.h>
}

class input_headless {
	public:
		/* init internals, create headless wlroots backend with fake
		 * pointer and add it to the backends managed by Wayfire */
		void init();
		/* remove the headless backend created by this class and
		 * delete it */
		void fini();
		/* emit a mouse button event */
		void pointer_button(uint32_t time_msec, uint32_t button, enum wlr_button_state state);
		/* emit a keyboard event */
		void keyboard_key(uint32_t time_msec, uint32_t key, enum wl_keyboard_key_state state);
		/* modify the modifier state of the keyboard */
		void keyboard_mods(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked);
		
		~input_headless() { fini(); }
	
	protected:
		void start_backend();
		struct wlr_backend* headless_backend = nullptr;
		struct wlr_input_device* input_pointer = nullptr;
		struct wlr_input_device* input_keyboard = nullptr;
};

#endif

