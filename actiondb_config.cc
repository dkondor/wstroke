/*
 * Copyright (c) 2008-2009, Thomas Jaeger <ThJaeger@gmail.com>
 * Copyright (c) 2020-2023, Daniel Kondor <kondor.dani@gmail.com>
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

#include "actiondb.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_set.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/unique_ptr.hpp>


template<class Archive> void StrokeInfo::save(Archive & ar, G_GNUC_UNUSED const unsigned int version) const {
	ar & stroke;
	ar & action;
	ar & name;
}

template<class Archive> void ActionDB::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	ar & root;
	ar & exclude_apps;
	ar & stroke_order;
	ar & stroke_map;
}

void ActionDB::write(const std::string& config_file_name) const {
	if(!next_id) throw std::runtime_error("ActionDB::write(): missing information!\n");
	std::string tmp = config_file_name + ".tmp";
	std::ofstream ofs(tmp.c_str());
	boost::archive::text_oarchive oa(ofs);
	oa << *this;
	ofs.close();
	if (rename(tmp.c_str(), config_file_name.c_str()))
		throw std::runtime_error(_("rename() failed"));
	printf("Saved actions.\n");
}

template<>
void ActionListDiff<false>::remove(unique_t id, bool really, ActionListDiff<false>* skip) {
	if(!really) deleted.insert(id);
	else deleted.erase(id);
	added.erase(id);
	for(auto& c : children) if(&c != skip) c.remove(id, true);
}

template<>
bool ActionListDiff<false>::has_stroke(unique_t id) const {
	auto it = added.find(id);
	if(it != added.end() && !it->second.stroke.trivial()) return true;
	return false;
}

template<>
std::set<stroke_id> ActionListDiff<false>::get_ids(bool include_deleted) const {
	std::set<stroke_id> ids = parent ? parent->get_ids(false) : std::set<stroke_id>();
	if(!include_deleted) for(const auto& x : deleted) ids.erase(x);
	for(const auto& x : added) ids.insert(x.first);
	return ids;
}

template<>
StrokeRow ActionListDiff<false>::get_info(stroke_id id, bool need_attr) const {
	StrokeRow si = parent ? parent->get_info(id, false) : StrokeRow();
	if(need_attr) si.deleted = this->deleted.count(id);
	
	auto i = added.find(id);
	if(i == added.end()) return si;
	if(!parent || i->second.name != "") {
		si.name = &(i->second.name);
		if(need_attr) si.name_overwrite = (parent != nullptr);
	}
	if(!parent || !i->second.stroke.trivial()) {
		si.stroke = &i->second.stroke;
		if(need_attr) si.stroke_overwrite = (parent != nullptr);
	}
	if(i->second.action) {
		si.action = i->second.action.get();
		if(need_attr) si.action_overwrite = (parent != nullptr);
	}
	return si;
}

template<>
int ActionListDiff<false>::size_rec() const {
	int size = added.size();
	for (auto i = children.begin(); i != children.end(); i++)
		size += i->size_rec();
	return size;
}

template<>
void ActionListDiff<false>::reset(unique_t id) {
	if(!parent) return;
	added.erase(id);
	deleted.erase(id);
}


template<class iter>
void ActionDB::remove_strokes_helper(iter&& begin, iter&& end) {
	std::sort(begin, end, [this](stroke_id x, stroke_id y) {
		return stroke_map.at(x).first < stroke_map.at(y).first;
	});
	auto it = stroke_order.begin();
	for(; begin != end; ++begin) {
		stroke_id x = *begin;
		while(true) {
			if(it == stroke_order.end()) throw std::runtime_error("ActionDB::remove_strokes_from_order(): missing stroke!\n");
			if(*it == x) break;
			++it;
		}
		it = stroke_order.erase(it);
	}
}

template<class iter>
void ActionDB::remove_strokes_from_order(iter begin, iter end, bool readonly) {
	if(begin == end) return;
	if(end - begin > 20) {
		if(readonly) {
			std::vector<stroke_id> tmp;
			tmp.insert(tmp.end(), begin, end);
			remove_strokes_helper(tmp.begin(), tmp.end());
		}
		else remove_strokes_helper(begin, end);
	}
	else for(auto it = stroke_order.begin(); it != stroke_order.end(); ) 
		if(std::find(begin, end, *it) != end)
			it = stroke_order.erase(it);
		else ++it;
}

void ActionDB::remove_app_r(ActionListDiff<false>* app) {
	/* 1. Remove all stroke_ids that are owned by app */
	for(auto it = stroke_order.begin(); it != stroke_order.end(); ) {
		auto owner = stroke_map.at(*it).second;
		if(owner == app) {
			/* Remove this ID */
			free_id(*it);
			stroke_map.erase(*it);
			it = stroke_order.erase(it);
		}
		else ++it;
	}
	/* 2. Remove from the list of apps */
	if(app->app) apps.erase(app->name);
	/* 3. Remove all child apps */
	for(auto& c : app->children) remove_app(&c);
}

ActionListDiff<false>* ActionDB::add_app(ActionListDiff<false>* parent, const std::string& name, bool real_app) {
	auto ret = parent->add_child(name, real_app);
	apps[name] = ret;
	return ret;
}

void ActionDB::remove_app(ActionListDiff<false>* app) {
	/* Recursively remove all stroke_ids from this subtree and from the apps map */
	remove_app_r(app);
	/* Remove the app from its parent -- this will recursively free memory */
	auto parent = app->parent; /* parent is not null as app != root */
	for(auto it = parent->children.begin(); it != parent->children.end(); ++it)
		if(&*it == app) {
			parent->children.erase(it);
			return;
		}
	throw std::runtime_error("ActionDB::remove_app(): app not found!\n");
}

unsigned int ActionDB::count_owned_strokes(const ActionListDiff<false>* parent) const {
	unsigned int ret = 0;
	for(const auto& x : parent->added) if(stroke_map.at(x.first).second == parent) ret++;
	return ret;
}

template<class it>
void ActionDB::remove_strokes(ActionListDiff<false>* parent, it&& begin, it&& end) {
	std::vector<stroke_id> to_delete;
	end = std::remove_if(begin, end, [this, parent](stroke_id id) {
		bool really = (stroke_map.at(id).second == parent);
		parent->remove(id, really);
		return !really;
	});
	remove_strokes_from_order(begin, end);
	for(; begin != end; ++begin) {
		stroke_id x = *begin;
		stroke_map.erase(x);
		free_id(x);
	}
}

template void ActionDB::remove_strokes<std::vector<stroke_id>::iterator>(ActionListDiff<false>* parent, std::vector<stroke_id>::iterator&& begin, std::vector<stroke_id>::iterator&& end);


void ActionDB::remove_stroke(ActionListDiff<false>* parent, stroke_id id) {
	std::array<stroke_id, 1> tmp = {id};
	remove_strokes(parent, tmp.begin(), tmp.end());
}

void ActionDB::move_stroke(stroke_id id, stroke_id before, bool after) {
	if(id == before) return;
	std::list<stroke_id>::iterator src = stroke_order.end();
	std::list<stroke_id>::iterator dst = stroke_order.end();
	for(auto it = stroke_order.begin(); it != stroke_order.end(); ++it) {
		if(*it == id) src = it;
		if(*it == before) dst = it;
	}
	if(after && dst != stroke_order.end()) ++dst;
	if(src == stroke_order.end()) throw std::runtime_error("ActionDB::move_stroke(): stroke ID not found!\n");
	src = stroke_order.erase(src);
	unsigned int order = 0;
	if(dst == stroke_order.end()) {
		if(!stroke_order.empty()) order = stroke_map.at(stroke_order.back()).first + 1;
	}
	else order = stroke_map.at(*dst).first;
	stroke_map.at(id).first = order;
	stroke_order.insert(dst, id);
	for(++order; dst != stroke_order.end(); ++dst, ++order) {
		auto& x = stroke_map.at(*dst);
		if(x.first >= order) break;
		x.first = order;
	}
}

template<class it>
void ActionDB::move_strokes(it&& begin, it&& end, stroke_id before, bool after) {
	remove_strokes_from_order(begin, end, true); // note: we need to keep the order
	auto dst = stroke_order.end();
	for(auto it2 = stroke_order.begin(); it2 != stroke_order.end(); ++it2)
		if(*it2 == before) {
			dst = it2;
			break;
		}
	if(after && dst != stroke_order.end()) ++dst;
	stroke_order.insert(dst, begin, end);
	
	/* recalculate the sort order for all elements */
	unsigned int order = 0;
	for(stroke_id id : stroke_order) stroke_map.at(id).first = order++;
}

template void ActionDB::move_strokes<std::vector<stroke_id>::iterator>(std::vector<stroke_id>::iterator&& begin, std::vector<stroke_id>::iterator&& end, stroke_id before, bool after);
template void ActionDB::move_strokes<std::vector<stroke_id>::reverse_iterator>(std::vector<stroke_id>::reverse_iterator&& begin, std::vector<stroke_id>::reverse_iterator&& end, stroke_id before, bool after);


bool ActionDB::move_stroke_to_app(ActionListDiff<false>* src, ActionListDiff<false>* dst, stroke_id id) {
	if(src == dst) return false;
	if(!src->contains(id)) return false;
	
	/* Main cases:
	 *  1. src is the ancestor of dst or vice versa -> we move all information from src to dst
	 * 		(note: this might affect other descendants of src, and also potentially overwrites any
	 * 		information already at dst)
	 *  2. src and dst are "independent" -> in this case, we make a copy (potentially overwriting
	 * 		any information in dst)
	 *  In either cases, the goal is to make dst look exactly like src was.
	 */
	
	auto parent = src->parent;
	while(parent && parent != dst) parent = parent->parent;
	if(parent == dst) {
		/* dst is the parent of src */
		if(stroke_map.at(id).second == src) {
			/* simple case, just move everything */
			dst->added[id] = std::move(src->added.at(id));
			src->added.erase(id);
			stroke_map.at(id).second = dst;
		}
		else {
			/* move any properties that are overridden in src or in any of its parents */
			StrokeInfo info;
			auto tmp = src;
			bool change_owner = false;
			do {
				auto it = tmp->added.find(id);
				if(it != tmp->added.end()) {
					if(stroke_map.at(id).second == tmp) change_owner = true;
					bool erase = true;
					if(!it->second.stroke.trivial()) {
						if(info.stroke.trivial()) info.stroke = std::move(it->second.stroke);
						else erase = false;
					}
					if(it->second.name != "") {
						if(info.name == "") info.name = std::move(it->second.name);
						else erase = false;
					}
					if(it->second.action) {
						if(!info.action) info.action = std::move(it->second.action);
						else erase = false;
					}
					if(erase) tmp->added.erase(it);
				}
				tmp = tmp->parent;
			} while(tmp != dst);
			StrokeInfo& di = dst->added[id]; // this might add a new element to dst->added
			if(!info.stroke.trivial()) di.stroke = std::move(info.stroke);
			if(info.name != "") di.name = std::move(info.name);
			if(info.action) di.action = std::move(info.action);
			if(change_owner) stroke_map.at(id).second = dst;
		}
		return false;
	}
	
	parent = dst->parent;
	while(parent && parent != src) parent = parent->parent;
	if(parent == src) {
		/* src is the parent of dst */
		if(stroke_map.at(id).second == src) {
			/* simple case, just move down everything */
			dst->added[id] = std::move(src->added.at(id));
			src->remove(id, true, dst);
			stroke_map.at(id).second = dst;
			return true;
		}
		else {
			/* check if the stroke is deleted in any of dst's ancestors
			 * (if yes, we will copy this stroke) */
			bool deleted = false;
			auto tmp = dst->parent;
			while(tmp != src) if(tmp->deleted.count(id)) {
				deleted = true;
				break;
			}
			
			if(!deleted) {
				StrokeRow r = src->get_info(id, false);
				StrokeInfo& info = dst->added[id];
				auto it = src->added.find(id);
				if(it != src->added.end()) {
					info = std::move(it->second);
					src->added.erase(it);
				}
				// we need to copy all things from r that are
				// (1) not already in info; and (2) overridden between src and dst
				bool copy_stroke = false;
				bool copy_name = false;
				bool copy_action = false;
				tmp = dst->parent;
				while(tmp != src) {
					auto it2 = tmp->added.find(id);
					if(!it2->second.stroke.trivial()) copy_stroke = true;
					if(it2->second.name != "") copy_name = true;
					if(it2->second.action) copy_action = true;
				}
				if(copy_stroke && info.stroke.trivial()) info.stroke = r.stroke->clone();
				if(copy_name && info.name == "") info.name = *r.name;
				if(copy_action && !info.action) info.action = r.action->clone();
				dst->deleted.erase(id);
				return false;
			}
		}
	}
	
	/* Here, dst and src are "unrelated": we need to copy all the info or
	 * the whole stroke to dst */
	dst->deleted.erase(id);
	if(dst->contains(id)) {
		/* This ID is already present, we need to copy parts of the info which
		 * are not already the same. */
		StrokeRow rsrc = src->get_info(id, false);
		StrokeRow rdst = dst->get_info(id, false);
		StrokeInfo* info = nullptr;
		/* Note: members of rsrc and rdst are pointers to the actual objects
		 * which contain the relevant info; these can be used to compare if
		 * anything needs to be copied. */
		if(rsrc.stroke != rdst.stroke) {
			if(!info) info = &(dst->added[id]);
			info->stroke = rsrc.stroke->clone();
		}
		if(rsrc.name != rdst.name) {
			if(!info) info = &(dst->added[id]);
			info->name = *rsrc.name;
		}
		if(rsrc.action != rdst.action) {
			if(!info) info = &(dst->added[id]);
			info->action = rsrc.action->clone();
		}
	}
	else {
		// we copy the whole stroke
		StrokeRow r = src->get_info(id, false);
		StrokeInfo info;
		info.name = *r.name;
		info.action = r.action->clone();
		info.stroke = r.stroke->clone();
		add_stroke(dst, std::move(info));
	}
	return false;
}

void ActionDB::merge_actions_r(ActionListDiff<false>* dst, ActionListDiff<false>* src, std::unordered_map<stroke_id, stroke_id>& id_map) {
	auto new_dst = add_app(dst, src->name, src->app);
	for(auto& x : src->added) {
		auto it = id_map.find(x.first);
		if(it != id_map.end()) {
			new_dst->added[it->second] = std::move(x.second);
		}
		else {
			// note: this stroke should be owned by src in this case
			id_map[x.first] = add_stroke(new_dst, std::move(x.second));
/*			StrokeRow r = src->get_info(x.first);
			StrokeInfo info;
			if(r.name) info.name = *r.name;
			if(r.action) info.action = r.action->clone();
			if(r.stroke) info.stroke = r.stroke->clone();
			add_stroke(action_list, std::move(info)); */
		}
	}
	for(auto x : src->deleted) new_dst->deleted.insert(id_map.at(x));
	
	for(auto& x : src->children) merge_actions_r(new_dst, &x, id_map);
}


void ActionDB::merge_actions(ActionDB&& other) {
	std::unordered_map<stroke_id, stroke_id> id_map;
	for(const auto& x : other.exclude_apps) exclude_apps.insert(x);
	for(auto& x : other.root.added) id_map[x.first] = add_stroke(&root, std::move(x.second));
	
	/* First we copy apps that exist in the current actions
	 * (we don't try to merge groups since the tree structure can differ) */
	auto other_apps = std::move(other.apps); // hack to avoid erasing while iterating (this works, since only erase() is called on other.apps)
	for(auto& x : other_apps) {
		auto it = apps.find(x.first);
		if(it == apps.end()) continue;
		auto action_list = it->second;
		
		/* Instead of only considering the apps added in this node,
		 * we consider all strokes, since some properties can be overriden
		 * on higher level */
		for(auto id : other.stroke_order) {
			if(x.second->contains(id)) {
				StrokeRow r = x.second->get_info(id);
				if(! (r.stroke || r.name || r.action) ) continue; // no info (already copied in root)
				auto it2 = id_map.find(id);
				if(it2 != id_map.end()) {
					/* note: we use clone, since this might be an inherited stroke */
					if(r.stroke) action_list->set_stroke(it2->second, r.stroke->clone());
					if(r.name) action_list->set_name(it2->second, *r.name);
					if(r.action) action_list->set_action(it2->second, r.action->clone());
				}
				else {
					/* copy this stroke */
					StrokeInfo info;
					if(r.name) info.name = *r.name;
					if(r.action) info.action = r.action->clone();
					if(r.stroke) info.stroke = r.stroke->clone();
					add_stroke(action_list, std::move(info));
				}
			}
		}
		
		other.remove_app(x.second);
	}
	
	/* copy the remaining tree structure */
	for(auto& x : other.root.children) merge_actions_r(&root, &x, id_map);
}

void ActionDB::overwrite_actions(ActionDB&& other) {
	*this = std::move(other);
	for(auto& c : root.children) c.parent = &root;
	for(auto& x : stroke_map) if(x.second.second == &other.root) x.second.second = &root;
}



