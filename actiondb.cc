/*
 * Copyright (c) 2008-2009, Thomas Jaeger <ThJaeger@gmail.com>
 * Copyright (c) 2020, Daniel Kondor <kondor.dani@gmail.com>
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
#include <string>
#include <filesystem>
#include <glibmm/i18n.h>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/unordered_set.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/shared_ptr.hpp>

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


BOOST_CLASS_EXPORT(StrokeSet)

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

template<class Archive> void Unique::serialize(G_GNUC_UNUSED Archive & ar, G_GNUC_UNUSED unsigned int version) {}

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

RAction Misc::convert() const {
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

template<class Archive> void StrokeSet::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<std::set<RStroke> >(*this);
}

template<class Archive> void StrokeInfo::serialize(Archive & ar, const unsigned int version) {
	ar & strokes;
	ar & action;
	if (version < 2) {
		/* convert Misc actions to new types */
		Misc* misc = dynamic_cast<Misc*>(action.get());
		if(misc) {
			RAction new_action = misc->convert();
			action = new_action;
		}
	}
	if (version < 3) {
		/* convert Scroll and Text actions to Global / None -- they are not supported */
		Scroll* scroll = dynamic_cast<Scroll*>(action.get());
		if(scroll) {
			RAction new_action = Global::create(Global::Type::NONE);
			action = new_action;
		}
		else {
			SendText* text = dynamic_cast<SendText*>(action.get());
			if(text) {
				RAction new_action = Global::create(Global::Type::NONE);
				action = new_action;
			}
		}
	}
	if (version == 0) return;
	ar & name;
}


template<class Archive> void ActionListDiff::serialize(Archive & ar, const unsigned int version) {
	ar & deleted;
	ar & added;
	ar & name;
	ar & children;
	ar & app;
	if (version == 0)
		return;
	ar & order;
}

ActionDB::ActionDB() {
	root.name = _("Default");
}

template<class Archive> void ActionDB::load(Archive & ar, const unsigned int version) {
	if (version >= 2) {
		ar & root;
		if (version >= 4) {
			ar & exclude_apps;
		}
	}
	if (version == 1) {
		std::map<int, StrokeInfo> strokes;
		ar & strokes;
		for (std::map<int, StrokeInfo>::iterator i = strokes.begin(); i != strokes.end(); ++i)
			root.add(i->second);
	}
	if (version == 0) {
		std::map<std::string, StrokeInfo> strokes;
		ar & strokes;
		for (std::map<std::string, StrokeInfo>::iterator i = strokes.begin(); i != strokes.end(); ++i) {
			i->second.name = i->first;
			root.add(i->second);
		}
	}

	root.fix_tree(version == 2);
	root.add_apps(apps);
	root.name = _("Default");
}

template<class Archive> void ActionDB::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	ar & root;
	ar & exclude_apps;
}


static char const * const actions_versions[] = { "-wstroke", "-0.5.6", "-0.4.1", "-0.4.0", "", nullptr };

bool ActionDB::read(const std::string& config_dir) {
	std::string filename = config_dir+"actions";
	for (const char * const *v = actions_versions; *v; v++) {
		if (std::filesystem::exists(filename + *v)) {
			filename += *v;
			std::ifstream ifs(filename.c_str(), std::ios::binary);
			if (!ifs.fail()) {
				boost::archive::text_iarchive ia(ifs);
				ia >> *this;
			}
			return true;
		}
	}
	return false;
}




void ActionDB::write(const std::string& config_dir) {
	std::string filename = config_dir+"actions"+actions_versions[0];
	std::string tmp = filename + ".tmp";
	std::ofstream ofs(tmp.c_str());
	boost::archive::text_oarchive oa(ofs);
	oa << (const ActionDB &)(*this);
	ofs.close();
	if (rename(tmp.c_str(), filename.c_str()))
		throw std::runtime_error(_("rename() failed"));
	printf("Saved actions.\n");
}



RStrokeInfo ActionListDiff::get_info(Unique *id, bool *deleted, bool *stroke, bool *name, bool *action) const {
	if (deleted)
		*deleted = this->deleted.count(id);
	if (stroke)
		*stroke = false;
	if (name)
		*name = false;
	if (action)
		*action = false;
	RStrokeInfo si = parent ? parent->get_info(id) : RStrokeInfo(new StrokeInfo);
	std::map<Unique *, StrokeInfo>::const_iterator i = added.find(id);
	for (i = added.begin(); i != added.end(); i++) {
		if (i->first == id)
			break;
	}
	if (i == added.end()) {
		return si;
	}
	if (i->second.name != "") {
		si->name = i->second.name;
		if (name)
			*name = parent;
	}
	if (i->second.strokes.size()) {
		si->strokes = i->second.strokes;
		if (stroke)
			*stroke = parent;
	}
	if (i->second.action) {
		si->action = i->second.action;
		if (action)
			*action = parent;
	}
	return si;
}

boost::shared_ptr<std::map<Unique *, StrokeSet> > ActionListDiff::get_strokes() const {
	boost::shared_ptr<std::map<Unique *, StrokeSet> > strokes = parent ? parent->get_strokes() :
		boost::shared_ptr<std::map<Unique *, StrokeSet> >(new std::map<Unique *, StrokeSet>);
	for (std::set<Unique *>::const_iterator i = deleted.begin(); i != deleted.end(); i++)
		strokes->erase(*i);
	for (std::map<Unique *, StrokeInfo>::const_iterator i = added.begin(); i != added.end(); i++)
		if (i->second.strokes.size())
			(*strokes)[i->first] = i->second.strokes;
	return strokes;
}

boost::shared_ptr<std::set<Unique *> > ActionListDiff::get_ids(bool include_deleted) const {
	boost::shared_ptr<std::set<Unique *> > ids = parent ? parent->get_ids(false) :
		boost::shared_ptr<std::set<Unique *> >(new std::set<Unique *>);
	if (!include_deleted)
		for (std::set<Unique *>::const_iterator i = deleted.begin(); i != deleted.end(); i++)
			ids->erase(*i);
	for (std::map<Unique *, StrokeInfo>::const_iterator i = added.begin(); i != added.end(); i++)
		ids->insert(i->first);
	return ids;
}


void ActionListDiff::all_strokes(std::list<RStroke> &strokes) const {
	for (std::map<Unique *, StrokeInfo>::const_iterator i = added.begin(); i != added.end(); i++)
		for (std::set<RStroke>::const_iterator j = i->second.strokes.begin(); j != i->second.strokes.end(); j++)
			strokes.push_back(*j);
	for (std::list<ActionListDiff>::const_iterator i = children.begin(); i != children.end(); i++)
		i->all_strokes(strokes);
}

RAction ActionListDiff::handle(RStroke s, RRanking &r) const {
	if (!s)
		return RAction();
	r.reset(new Ranking);
	r->stroke = s;
	r->score = 0.0;
	boost::shared_ptr<std::map<Unique *, StrokeSet> > strokes = get_strokes();
	for (std::map<Unique *, StrokeSet>::const_iterator i = strokes->begin(); i!=strokes->end(); i++) {
		for (StrokeSet::iterator j = i->second.begin(); j!=i->second.end(); j++) {
			double score;
			int match = Stroke::compare(s, *j, score);
			if (match < 0)
				continue;
			RStrokeInfo si = get_info(i->first);
			r->r.insert(std::pair<double, std::pair<std::string, RStroke> >
					(score, std::pair<std::string, RStroke>(si->name, *j)));
			if (score > r->score) {
				r->score = score;
				if (match) {
					r->name = si->name;
					r->action = si->action;
					r->best_stroke = *j;
				}
			}
		}
	}
	return r->action;
}

void ActionListDiff::handle_advanced(RStroke s, std::map<guint, RAction> &as,
		std::map<guint, RRanking> &rs, int b1, int b2) const {
	if (!s)
		return;
	boost::shared_ptr<std::map<Unique *, StrokeSet> > strokes = get_strokes();
	for (std::map<Unique *, StrokeSet>::const_iterator i = strokes->begin(); i!=strokes->end(); i++) {
		for (StrokeSet::iterator j = i->second.begin(); j!=i->second.end(); j++) {
			int b = (*j)->button;
			if (!s->timeout && !b)
				continue;
			s->button = b;
			double score;
			int match = Stroke::compare(s, *j, score);
			if (match < 0)
				continue;
			Ranking *r;
			if (b == b1)
				b = b2;
			if (rs.count(b)) {
				r = rs[b].get();
			} else {
				r = new Ranking;
				rs[b].reset(r);
				r->stroke = RStroke(new Stroke(*s));
				r->score = -1;
			}
			RStrokeInfo si = get_info(i->first);
			r->r.insert(std::pair<double, std::pair<std::string, RStroke> >
					(score, std::pair<std::string, RStroke>(si->name, *j)));
			if (score > r->score) {
				r->score = score;
				if (match) {
					r->name = si->name;
					r->action = si->action;
					r->best_stroke = *j;
					as[b] = si->action;
				}
			}
		}
	}
}


ActionListDiff::~ActionListDiff() {
/*	if (app)
		actions.apps.erase(name); */
}


