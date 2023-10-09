/*
 * stroke_draw.h
 * 
 * Copyright (c) 2008-2009, Thomas Jaeger <ThJaeger@gmail.com>
 * Copyright (c) 2020-2023 Daniel Kondor <kondor.dani@gmail.com>
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


#ifndef STROKE_DRAW_H
#define STROKE_DRAW_H

#include "gesture.h"
#include <gdkmm.h>

class StrokeDrawer {
	protected:
		static Glib::RefPtr<Gdk::Pixbuf> drawEmpty_(int);
		static Glib::RefPtr<Gdk::Pixbuf> pbEmpty;
	
	public:
		static Glib::RefPtr<Gdk::Pixbuf> draw(const Stroke* stroke, int size, double width = 2.0);
		static void draw(const Stroke* stroke, Cairo::RefPtr<Cairo::Surface> surface, int x, int y, int w, int h, double width = 2.0);
		static void draw_svg(const Stroke* stroke, std::string filename);
		static Glib::RefPtr<Gdk::Pixbuf> drawEmpty(int);
};

#endif

