/*
 * Copyright (c) 2008-2009, Thomas Jaeger <ThJaeger@gmail.com>
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
#include <glibmm/i18n.h>
#include <gdkmm.h>
#include <gtkmm.h>

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/shared_ptr.hpp>

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

template<class Archive> void Unique::serialize(G_GNUC_UNUSED Archive & ar, G_GNUC_UNUSED unsigned int version) {}

template<class Archive> void Action::serialize(G_GNUC_UNUSED Archive & ar, G_GNUC_UNUSED unsigned int version) {}

template<class Archive> void Command::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & cmd;
}

template<class Archive> void ModAction::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & mods;
}

template<class Archive> void SendKey::load(Archive & ar, const unsigned int version) {
	guint code;
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & key;
	ar & code;
	if (version < 1) {
		bool xtest;
		ar & xtest;
	}
}

template<class Archive> void SendKey::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	guint code = 0;
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & key;
	ar & code;
}

template<class Archive> void SendText::load(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	std::string str;
	ar & str;
	text = str;
}

template<class Archive> void SendText::save(Archive & ar, G_GNUC_UNUSED unsigned int version) const {
	ar & boost::serialization::base_object<Action>(*this);
	std::string str(text);
	ar & str;
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

template<class Archive> void StrokeSet::serialize(Archive & ar, G_GNUC_UNUSED unsigned int version) {
	ar & boost::serialization::base_object<std::set<RStroke> >(*this);
}

template<class Archive> void StrokeInfo::serialize(Archive & ar, const unsigned int version) {
	ar & strokes;
	ar & action;
	if (version == 0) return;
	ar & name;
}

using namespace std;

/*
void Command::run() {
	gchar* argv[] = {(gchar*) "/bin/sh", (gchar*) "-c", NULL, NULL};
	argv[2] = (gchar *) cmd.c_str();
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}
*/

ButtonInfo Button::get_button_info() const {
	ButtonInfo bi;
	bi.button = button;
	bi.state = mods;
	return bi;
}


const Glib::ustring Button::get_label() const {
	return get_button_info().get_button_text();
}

const char *Misc::types[5] = { N_("None"), N_("Close"), N_("Minimize"), nullptr };

const Glib::ustring Misc::get_label() const { return _(types[static_cast<int>(type)]); }

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
}


static const char *actions_versions[] = { "-0.5.6", "-0.4.1", "-0.4.0", "", nullptr };

bool ActionDB::read(const std::string& config_dir) {
	std::string filename = config_dir+"actions";
	bool ret = false;
	for (const char **v = actions_versions; *v; v++) {
		if (std::filesystem::exists(filename + *v)) {
			filename += *v;
			try {
				ifstream ifs(filename.c_str(), ios::binary);
				if (!ifs.fail()) {
					boost::archive::text_iarchive ia(ifs);
					ia >> *this;
					ret = true;
				}
			} catch (exception &e) {
				
			}
			break;
		}
	}
	return ret;
}

static void error_dialog(const Glib::ustring &text) {
	Gtk::MessageDialog dialog(text, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
	dialog.show();
	dialog.run();
}

bool ActionDB::write(const std::string& config_dir) {
	std::string filename = config_dir+"actions"+actions_versions[0];
	std::string tmp = filename + ".tmp";
	bool good_state = true;
	try {
		ofstream ofs(tmp.c_str());
		boost::archive::text_oarchive oa(ofs);
		oa << (const ActionDB &)(*this);
		ofs.close();
		if (rename(tmp.c_str(), filename.c_str()))
			throw std::runtime_error(_("rename() failed"));
		printf("Saved actions.\n");
	} catch (exception &e) {
		printf(_("Error: Couldn't save action database: %s.\n"), e.what());
		good_state = false;
		error_dialog(Glib::ustring::compose(_( "Couldn't save %1.  Your changes will be lost.  "
				"Make sure that \"%2\" is a directory and that you have write access to it.  "
				"You can change the configuration directory "
				"using the -c or --config-dir command line options."), _("actions"), config_dir));
	}
	return good_state;
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
			r->r.insert(pair<double, pair<std::string, RStroke> >
					(score, pair<std::string, RStroke>(si->name, *j)));
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
			r->r.insert(pair<double, pair<std::string, RStroke> >
					(score, pair<std::string, RStroke>(si->name, *j)));
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

/* actions 
void Button::run() {
	fprintf(stderr, "Button action: %u\n", button);
}

void SendKey::run() {
	if (!key)
		return;
	fprintf(stderr, "SendKey: %u\n", key);
	// guint code = XKeysymToKeycode(dpy, key);
}

RModifiers ModAction::prepare() {
	// todo: process modifiers
	return nullptr;
}

RModifiers SendKey::prepare() {
	// todo: process modifiers
	return nullptr;
}
*/

const Glib::ustring SendKey::get_label() const {
	return Gtk::AccelGroup::get_label(key, mods);
}

const Glib::ustring ModAction::get_label() const {
	if (!mods)
		return _("No Modifiers");
	Glib::ustring label = Gtk::AccelGroup::get_label(0, mods);
	return label.substr(0,label.size()-1);
}

const Glib::ustring Scroll::get_label() const {
	if (mods)
		return ModAction::get_label() + _(" + Scroll");
	else
		return _("Scroll");
}

const Glib::ustring Ignore::get_label() const {
	if (mods)
		return ModAction::get_label();
	else
		return _("Ignore");
}

/*
void SendText::run() {
	for (Glib::ustring::iterator i = text.begin(); i != text.end(); i++)
		if (!fake_char(*i))
			fake_unicode(*i);
}

void Misc::run() {
	switch (type) {
		case SHOWHIDE:
			win->show_hide();
			return;
		case UNMINIMIZE:
			grabber->unminimize();
			return;
		case DISABLE:
			disabled.set(!disabled.get());
			return;
		default:
			return;
	}
}
*/

bool ButtonInfo::overlap(const ButtonInfo &bi) const {
	if (button != bi.button)
		return false;
	if (state == AnyModifier || bi.state == AnyModifier)
			return true;
	return !((state ^ bi.state) & ~Gdk::ModifierType::LOCK_MASK &
		~Gdk::ModifierType::MOD2_MASK);
}

Glib::ustring ButtonInfo::get_button_text() const {
	Glib::ustring str;
	if (instant)
		str += _("(Instantly) ");
	if (click_hold)
		str += _("(Click & Hold) ");
	if (state == AnyModifier)
		str += Glib::ustring() + "(" + _("Any Modifier") + " +) ";
	else
		str += Gtk::AccelGroup::get_label(0, (Gdk::ModifierType)state);
	return str + Glib::ustring::compose(_("Button %1"), button);
}

bool mods_equal(RModifiers m1, RModifiers m2) {
	return m1 && m2 && *m1 == *m2;
}

