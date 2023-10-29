/*
 * convert_keycodes.h
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


#include <gdk/gdk.h>
#include <stdint.h>

#ifndef CONVERT_KEYCODES_H
#define CONVERT_KEYCODES_H

class KeyCodes {
	public:
		/* convert from a combination of Gdk modifier constants to the
		 * WLR modifier enum constants (take care of "virtual" modifiers
		 * like SUPER, ALT, etc.) */
		static uint32_t convert_modifier(uint32_t mod);
		/* add back "virtual" modifiers -- calls the corresponding GDK function */
		static uint32_t add_virtual_modifiers(uint32_t mod);
		/* try to convert a keysym to a hardware keycode; returns the
		 * keycode or zero if it was not found */
		static uint32_t convert_keysym(uint32_t key);
		/* convert a hardware keycode to a keysym (using level = 0 and
		 * group = 0) for the purpose of displaying it to the user */
		static uint32_t convert_keycode(uint32_t code);
		
		static void init();
		
		static unsigned int keycode_errors;
	protected:
		static GdkKeymap* keymap;
};


#endif

