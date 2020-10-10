/*
 * stroke_drawing_area.cpp -- Gtk.DrawingArea adapted to record new strokes
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


#include "stroke_drawing_area.h"
#include <cmath>

SRArea::SRArea() {
//	signal_configure_event().connect(sigc::mem_fun(*this, &SRArea::configure_event));
	add_events(Gdk::EventMask::BUTTON_PRESS_MASK | Gdk::EventMask::BUTTON_RELEASE_MASK | Gdk::EventMask::BUTTON_MOTION_MASK);
}

bool SRArea::on_configure_event(GdkEventConfigure* event) {
	auto win = get_window();
	surface = win->create_similar_surface(Cairo::Content::CONTENT_COLOR, event->width, event->height);
	clear();
	return true;
}

bool SRArea::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
	if(surface) {
		cr->set_source(surface, 0, 0);
		cr->paint();
	}
	return true;
}

bool SRArea::on_button_press_event(GdkEventButton* event) {
	if(current_button) return true;
	current_button = event->button;
	last_x = event->x;
	last_y = event->y;
	ps.clear();
	stroke.reset();
	ps.add(Triple {(float)last_x, (float)last_y, event->time});
	return true;
}

bool SRArea::on_button_release_event(GdkEventButton* event) {
	if(event->button != current_button) return true;
	draw_line(event->x, event->y, event->time);
	current_button = 0;
	stroke = Stroke::create(ps, 0, 0, AnyModifier, 0);
	ps.clear();
	stroke_recorded.emit(stroke);
	return true;
}

bool SRArea::on_motion_notify_event(GdkEventMotion* event) {
	if(current_button) draw_line(event->x, event->y, event->time);
	return true;
}

void SRArea::draw_line(gdouble x, gdouble y, guint32 t) {
	if(surface && (x != last_x || y != last_y)) {
		auto cr = Cairo::Context::create(surface);
		cr->set_source_rgb(0.8, 0, 0);
		cr->move_to(last_x, last_y);
		cr->line_to(x, y);
		cr->stroke();
		
		int x1 = std::floor(std::min(x, last_x)) - 2;
		int y1 = std::floor(std::min(y, last_y)) - 2;
		int w = std::ceil(std::abs(x - last_x)) + 4;
		int h = std::ceil(std::abs(y - last_y)) + 4;
		queue_draw_area(x1, y1, w, h);
		
		ps.add(Triple {(float)last_x, (float)last_y, t});
		
		last_x = x;
		last_y = y;
	}
}

void SRArea::clear() {
	if(surface) {
		auto cr = Cairo::Context::create(surface);
		cr->set_source_rgb(1, 1, 1);
		cr->paint();
	}
	ps.clear();
	stroke.reset();
}

