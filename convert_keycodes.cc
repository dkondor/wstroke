/*
 * convert_keycodes.cc
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


#include "convert_keycodes.h"
#include <array>
extern "C"
{
#include <wlr/interfaces/wlr_keyboard.h>
}


GdkKeymap* KeyCodes::keymap = nullptr;
unsigned int KeyCodes::keycode_errors = 0;

void KeyCodes::init() {
	if(keymap) return;
	GdkDisplay* dpy = gdk_display_get_default();
	keymap = gdk_keymap_get_for_display(dpy);
}

static constexpr std::array<std::pair<uint32_t, enum wlr_keyboard_modifier>, 10> modifier_match = {
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_SHIFT_MASK, WLR_MODIFIER_SHIFT),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_LOCK_MASK, WLR_MODIFIER_CAPS),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_CONTROL_MASK, WLR_MODIFIER_CTRL),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_MOD1_MASK, WLR_MODIFIER_ALT),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_MOD2_MASK, WLR_MODIFIER_MOD2),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_MOD3_MASK, WLR_MODIFIER_MOD3),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_MOD4_MASK, WLR_MODIFIER_LOGO),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_MOD5_MASK, WLR_MODIFIER_MOD5),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_META_MASK, WLR_MODIFIER_ALT),
	std::pair<uint32_t, enum wlr_keyboard_modifier>(GDK_SUPER_MASK, WLR_MODIFIER_LOGO)
};

uint32_t KeyCodes::convert_modifier(uint32_t mod) {
	uint32_t ret = 0;
	for(auto p : modifier_match) if(mod & p.first) ret |= p.second;
	return ret;
}

uint32_t KeyCodes::convert_keysym(uint32_t key) {
	uint32_t ret = 0;
	GdkKeymapKey* keys = nullptr;
	gint n_keys = 0;
	if(gdk_keymap_get_entries_for_keyval(keymap, key, &keys, &n_keys) && n_keys && keys) {
		for(gint i = 0; i < n_keys; i++) {
			if(keys[i].group == 0 || keys[i].level == 0) {
				ret = keys[i].keycode;
				break;
			}
		}
	}
	if(keys) g_free(keys);
	if(!ret) {
		keycode_errors++;
		fprintf(stderr, "KeyCodes::convert_keysym(): could not convert %u\n", key);
	}
	return ret;
}


