/*
 * input_events.hpp -- interface to generate input events in Wayfire
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


#ifndef INPUT_EVENTS_HPP
#define INPUT_EVENTS_HPP

extern "C" {
#include <wlr/version.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wayland-server-protocol.h>
}

#if (WLR_VERSION_NUM >= 4608)
#define WLR_BUTTON_RELEASED        WL_POINTER_BUTTON_STATE_RELEASED
#define WLR_BUTTON_PRESSED         WL_POINTER_BUTTON_STATE_PRESSED
#define WSTROKE_AXIS_HORIZONTAL    wl_pointer_axis::WL_POINTER_AXIS_HORIZONTAL_SCROLL
#define WSTROKE_AXIS_VERTICAL      wl_pointer_axis::WL_POINTER_AXIS_VERTICAL_SCROLL
#define WLR_AXIS_SOURCE_CONTINUOUS WL_POINTER_AXIS_SOURCE_CONTINUOUS
#define WSTROKE_BUTTON_STATE       wl_pointer_button_state
#define WSTROKE_AXIS_ORIENTATION   wl_pointer_axis
#define WSTROKE_WLR_VERSION_018
#else
#define WSTROKE_AXIS_HORIZONTAL    wlr_axis_orientation::WLR_AXIS_ORIENTATION_HORIZONTAL
#define WSTROKE_AXIS_VERTICAL      wlr_axis_orientation::WLR_AXIS_ORIENTATION_VERTICAL
#define WSTROKE_BUTTON_STATE       wlr_button_state
#define WSTROKE_AXIS_ORIENTATION   wlr_axis_orientation
#endif

class input_headless {
	public:
		/* init internals, create headless wlroots backend with fake
		 * pointer and add it to the backends managed by Wayfire */
		void init();
		/* remove the headless backend created by this class and
		 * delete it */
		void fini();
		/* emit a mouse button event */
		void pointer_button(uint32_t time_msec, uint32_t button, enum WSTROKE_BUTTON_STATE state);
		/* emit a pointer scroll event */
		void pointer_scroll(uint32_t time_msec, double delta, enum WSTROKE_AXIS_ORIENTATION o);
		/* emit a sequence of swipe events */
		void pointer_start_swipe(uint32_t time_msec, uint32_t fingers);
		void pointer_update_swipe(uint32_t time_msec, uint32_t fingers, double dx, double dy);
		void pointer_end_swipe(uint32_t time_msec, bool cancelled);
		/* emit a sequence of pinch events */
		void pointer_start_pinch(uint32_t time_msec, uint32_t fingers);
		void pointer_update_pinch(uint32_t time_msec, uint32_t fingers, double dx, double dy, double scale, double rotation);
		void pointer_end_pinch(uint32_t time_msec, bool cancelled);
		/* emit a keyboard event */
		void keyboard_key(uint32_t time_msec, uint32_t key, enum wl_keyboard_key_state state);
		/* modify the modifier state of the keyboard */
		void keyboard_mods(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked);
		/* return if a pointer event was generated by us */
		bool is_own_event_btn(const wlr_pointer_button_event* ev) const { return ev && (ev->pointer == input_pointer); }
		
		~input_headless() { fini(); }
	
	protected:
		void start_backend();
		struct wlr_backend* headless_backend = nullptr;
		struct wlr_pointer* input_pointer = nullptr;
		struct wlr_keyboard* input_keyboard = nullptr;
};

#endif

