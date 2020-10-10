/*
 * stroke_drawing_area.h -- Gtk.DrawingArea adapted to record new strokes
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


#ifndef STROKE_DRAWING_AREA_H
#define STROKE_DRAWING_AREA_H

#include <gtkmm.h>
#include "gesture.h"

class SRArea : public Gtk::DrawingArea {
	public:
		SRArea();
		void clear();
		RStroke get_stroke() { return stroke; }
		sigc::signal<void, RStroke> stroke_recorded;
		virtual ~SRArea() { }
	protected:
		bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
		bool on_button_press_event(GdkEventButton* event) override;
		bool on_button_release_event(GdkEventButton* event) override;
		bool on_motion_notify_event(GdkEventMotion* event) override;
		bool on_configure_event(GdkEventConfigure* event) override;
		
		void draw_line(gdouble x, gdouble y, guint32 t);
		
		Cairo::RefPtr<Cairo::Surface> surface;
		guint current_button = 0;
		gdouble last_x;
		gdouble last_y;
		
		PreStroke ps;
		RStroke stroke;
};


#endif

