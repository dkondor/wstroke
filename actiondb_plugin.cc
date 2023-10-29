/*
 * Copyright (c) 2008-2009, Thomas Jaeger <ThJaeger@gmail.com>
 * Copyright (c) 2023, Daniel Kondor <kondor.dani@gmail.com>
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
 */

#include "gesture.h"
#include "actiondb.h"

template<>
const std::string& ActionListDiff<false>::get_stroke_name(unique_t id) const {
	auto it = added.find(id);
	if(it != added.end() && it->second.name != "") return it->second.name;
	//!! TODO: check for non-null parent ??
	return parent->get_stroke_name(id);
}

template<>
std::map<stroke_id, const Stroke*> ActionListDiff<false>::get_strokes() const {
	std::map<stroke_id, const Stroke*> strokes = parent ? parent->get_strokes() : std::map<stroke_id, const Stroke*>();
	for(const auto& x : deleted) strokes.erase(x);
	for(const auto& x : added) if(!x.second.stroke.trivial()) strokes[x.first] = &x.second.stroke;
	return strokes;
}

template<>
Action* ActionListDiff<false>::handle(const Stroke& s, Ranking* r) const {
	double best_score = 0.0;
	Action* ret = nullptr;
	if(r) r->stroke = &s;
	const auto strokes = get_strokes();
	for(const auto& x : strokes) {
		const Stroke& y = *x.second;
		double score;
		int match = Stroke::compare(s, y, score);
		if (match < 0)
			continue;
		bool new_best = false;
		if(score > best_score) {
			new_best = true;
			best_score = score;
			ret = get_stroke_action(x.first);
			if(r) r->best_stroke = &y;
		}
		if(r) {
			const std::string& name = get_stroke_name(x.first);
			r->r.insert(std::pair<double, std::pair<std::string, const Stroke*> >
				(score, std::pair<std::string, const Stroke*>(name, &y)));
			if(new_best) r->name = name;
		}
	}
	
	if(r) {
		r->score = best_score;
		r->action = ret;
	}
	return ret;
}
