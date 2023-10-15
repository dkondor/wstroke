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

#ifdef ACTIONDB_CONVERT_CODES
#include "convert_keycodes.h"

static inline uint32_t convert_modifier(uint32_t mod) {
	return KeyCodes::convert_modifier(mod);
}

static inline uint32_t convert_keysym(uint32_t key) {
	return KeyCodes::convert_keysym(key);
}

#else

static inline uint32_t convert_modifier(G_GNUC_UNUSED uint32_t mod) {
	throw std::runtime_error("unsupported action DB version!\nrun the wstroke-config program first to convert it to the new format\n");
}

static inline uint32_t convert_keysym(G_GNUC_UNUSED uint32_t key) {
	throw std::runtime_error("unsupported action DB version!\nrun the wstroke-config program first to convert it to the new format\n");
}

#endif


BOOST_CLASS_EXPORT(Action)
BOOST_CLASS_EXPORT(Command)
BOOST_CLASS_EXPORT(ModAction)
BOOST_CLASS_EXPORT(SendKey)
BOOST_CLASS_EXPORT(SendText)
BOOST_CLASS_EXPORT(Scroll)
BOOST_CLASS_EXPORT(Ignore)
BOOST_CLASS_EXPORT(Button)
BOOST_CLASS_EXPORT(Misc)
BOOST_CLASS_EXPORT(Global)
BOOST_CLASS_EXPORT(View)
BOOST_CLASS_EXPORT(Plugin)


template<class Archive> void Action::serialize(G_GNUC_UNUSED Archive & ar, G_GNUC_UNUSED unsigned int version) {}

template<class Archive> void Command::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & cmd;
}

template<class Archive> void Plugin::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & cmd;
}

template<class Archive> void ModAction::load(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & mods;
	if (version < 1) mods = convert_modifier(mods);
}

template<class Archive> void ModAction::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	ar & boost::serialization::base_object<Action>(*this);
	ar & mods;
}

template<class Archive> void SendKey::load(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & key;
	if (version < 2) {
		uint32_t code;
		ar & code;
		if (version < 1) {
			bool xtest;
			ar & xtest;
		}
		key = convert_keysym(key);
	}
}

template<class Archive> void SendKey::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & key;
}

template<class Archive> void SendText::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & text;
}

template<class Archive> void Scroll::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
}

template<class Archive> void Ignore::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
}

template<class Archive> void Button::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & button;
}

template<class Archive> void Misc::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & type;
}

std::unique_ptr<Action> Misc::convert() const {
	switch(type) {
		case SHOWHIDE:
			return Global::create(Global::Type::SHOW_CONFIG);
		case NONE:
		case DISABLE:
		case UNMINIMIZE:
		default:
			return Global::create(Global::Type::NONE);
	}
}

template<class Archive> void Global::load(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & type;
	/* allow later extensions to add more types that might not be supported in older versions */
	if((uint32_t)type >= n_actions) type = Type::NONE;
}

template<class Archive> void Global::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	ar & boost::serialization::base_object<Action>(*this);
	ar & type;
}

template<class Archive> void View::load(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & type;
	/* allow later extensions to add more types that might not be supported in older versions */
	if((uint32_t)type >= n_actions) type = Type::NONE;
}

template<class Archive> void View::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	ar & boost::serialization::base_object<Action>(*this);
	ar & type;
}

class StrokeSet : public std::set<boost::shared_ptr<Stroke>> {
	friend class boost::serialization::access;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
};
BOOST_CLASS_EXPORT(StrokeSet)

template<class Archive> void StrokeSet::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<std::set<boost::shared_ptr<Stroke> > >(*this);
}

template<class Archive> void StrokeInfo::load(Archive & ar, const unsigned int version) {
	if (version >= 4) {
		ar & stroke;
		ar & action;
	}
	else {
		StrokeSet strokes;
		ar & strokes;
		
		if(strokes.size() && *strokes.begin()) stroke = std::move(**strokes.begin());
		
		boost::shared_ptr<Action> action2;
		ar & action2;
		if(version < 2) {
			/* convert Misc actions to new types */
			Misc* misc = dynamic_cast<Misc*>(action2.get());
			if(misc) action = misc->convert();
		}
		if(!action && version < 3) {
			/* convert Scroll and Text actions to Global / None -- they are not supported */
			Scroll* scroll = dynamic_cast<Scroll*>(action2.get());
			if(scroll) action = Global::create(Global::Type::NONE);
			else {
				SendText* text = dynamic_cast<SendText*>(action2.get());
				if(text) action = Global::create(Global::Type::NONE);
			}
		}
		if(!action) action = action2->clone();
	}
	if (version == 0) return;
	ar & name;
}

class Unique {
	friend class boost::serialization::access;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
public:
	int level; /* not used */ 
	int i;     /* (not saved in the archive) */
};

template<class Archive> void Unique::serialize(G_GNUC_UNUSED Archive & ar, G_GNUC_UNUSED unsigned int version) {}


template<>
ActionListDiff<false>* ActionListDiff<false>::add_child(std::string name, bool app) {
	children.emplace_back();
	ActionListDiff *child = &(children.back());
	child->name = name;
	child->app = app;
	child->parent = this;
	child->level = level + 1;
	return child;
}

void ActionDB::convert_actionlist(ActionListDiff<false>& dst, ActionListDiff<true>& src,
		std::unordered_map<Unique*, stroke_id>& mapping, std::unordered_set<Unique*>& extra_unique) {
	for(Unique* x : src.order) {
		if(mapping.count(x)) throw std::runtime_error("Unique added multiple times!\n");
		stroke_id z = get_next_id();
		stroke_order.push_back(z);
		stroke_map[z] = std::pair(z, &dst);
		dst.order.push_back(z);
		mapping[x] = z;
	}
	
	for(Unique* x : src.deleted) {
		auto it = mapping.find(x);
		/* Note: due to how deletions are handled in earlier versions,
		 * a Unique can end up staying in the deleted list of a child
		 * even after it was deleted from the parent (this happens if it
		 * is first deleted from the child and then the parent). In this
		 * case, it is safe to just ignore it. */
		if(it != mapping.end()) dst.deleted.insert(it->second);
		else extra_unique.insert(x);
	}
	
	for(auto& x : src.added) {
		auto it = mapping.find(x.first);
		if(it == mapping.end()) throw std::runtime_error("Unique not found!\n");
		dst.added.insert(std::make_pair(it->second, std::move(x.second)));
	}
	
	for(auto& x : src.children) {
		ActionListDiff<false>* y = dst.add_child(x.name, x.app);
		convert_actionlist(*y, x, mapping, extra_unique);
	}
}

template<class Archive> void ActionDB::load(Archive & ar, const unsigned int version) {
	if (version > 5) throw std::runtime_error("ActionDB::load(): unsupported archive version, maybe it was created with a newer version of WStroke?\n");
	if (version == 5) {
		ar & root;
		ar & exclude_apps;
		if(next_id) {
			// read the order of strokes -- only matters for the GUI
			ar & stroke_order;
			ar & stroke_map;
			// recreate IDs mapping
			for(stroke_id x : stroke_order) if(x + 1 > next_id) next_id = x + 1;
			for(stroke_id x = 1; x < next_id; x++) if(!stroke_map.count(x)) available_ids.push_back(x);
		}
	}
	else if (version >= 2) {
		ActionListDiff<true> root_tmp;
		ar & root_tmp;
		std::unordered_map<Unique*, stroke_id> mapping;
		std::unordered_set<Unique*> extra_unique;
		convert_actionlist(root, root_tmp, mapping, extra_unique);
		if (version >= 4) ar & exclude_apps;
		
		for(auto& x : mapping) delete x.first;
		for(Unique* x : extra_unique) delete x;
	}
	if (version == 1) {
		std::map<int, StrokeInfo> strokes;
		ar & strokes;
		for (std::map<int, StrokeInfo>::iterator i = strokes.begin(); i != strokes.end(); ++i)
			add_stroke(&root, std::move(i->second));
	}
	if (version == 0) {
		std::map<std::string, StrokeInfo> strokes;
		ar & strokes;
		for (std::map<std::string, StrokeInfo>::iterator i = strokes.begin(); i != strokes.end(); ++i) {
			i->second.name = i->first;
			add_stroke(&root, std::move(i->second));
		}
	}

	root.add_apps(apps);
	root.name = _("Default");
	read_version = version;
}

char const * const ActionDB::wstroke_actions_versions[3] = { "actions-wstroke-2", "actions-wstroke", nullptr };
char const * const ActionDB::easystroke_actions_versions[5] = { "actions-0.5.6", "actions-0.4.1", "actions-0.4.0", "actions", nullptr };

bool ActionDB::read(const std::string& config_file_name, bool readonly) {
	clear();
	next_id = readonly ? 0 : 1;
	if(!std::filesystem::exists(config_file_name)) return false;
	if(!std::filesystem::is_regular_file(config_file_name)) return false;
	std::ifstream ifs(config_file_name.c_str(), std::ios::binary);
	if(ifs.fail()) return false;
	boost::archive::text_iarchive ia(ifs);
	ia >> *this;
	return true;
}

stroke_id ActionDB::add_stroke(ActionListDiff<false>* parent, StrokeInfo&& si, stroke_id before) {
	stroke_id new_id = get_next_id();
	parent->added.emplace(new_id, std::move(si));
	unsigned int order = 0;
	
	auto it = stroke_order.end();
	if(before) for(auto it = stroke_order.begin(); it != stroke_order.end(); ++it)
		if(*it == before) break;
	
	if(it != stroke_order.end()) order = stroke_map.at(*it).first;
	else if(!stroke_order.empty()) order = stroke_map.at(stroke_order.back()).first + 1;
	
	stroke_map[new_id] = std::pair(order, parent);
	it = stroke_order.insert(it, new_id);
	/* if the new element was inserted in the middle, the sort order
	 * for all later elements needs to be updated */
	for(++it, ++order; it != stroke_order.end(); ++it, ++order) {
		/* optimization: if the sort order is already large enough, we
		 * do not need to update anymore (this can happen if strokes were
		 * removed before */
		auto& x = stroke_map.at(*it);
		if(x.first >= order) break;
		x.first = order;
	}
	
	return new_id;
}

