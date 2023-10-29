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
#ifndef __STROKEDB_H__
#define __STROKEDB_H__
#include <map>
#include <set>
#include <list>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <type_traits>
#include <glibmm.h>
#include <glibmm/i18n.h>
#include <boost/serialization/access.hpp>
#include <boost/serialization/version.hpp>
#include <boost/serialization/split_member.hpp>

#include "gesture.h"

class Action;
class Command;
class SendKey;
class SendText;
class Scroll;
class Ignore;
class Button;
class Misc;
class Global;
class View;
class Plugin;
class Ranking;
class Touchpad;


class ActionVisitor {
	public:
		virtual ~ActionVisitor() { }
		virtual void visit(const Command* action) = 0;
		virtual void visit(const SendKey* action) = 0;
		virtual void visit(const SendText* action) = 0;
		virtual void visit(const Scroll* action) = 0;
		virtual void visit(const Ignore* action) = 0;
		virtual void visit(const Button* action) = 0;
		virtual void visit(const Global* action) = 0;
		virtual void visit(const View* action) = 0;
		virtual void visit(const Plugin* action) = 0;
		virtual void visit(const Touchpad* action) = 0;
};

class Action {
	friend class boost::serialization::access;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
public:
	virtual void visit(ActionVisitor* visitor) const = 0;
	virtual std::string get_type() const = 0;
	virtual ~Action() {}
	virtual std::unique_ptr<Action> clone() const = 0;
};

class Command : public Action {
	friend class boost::serialization::access;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
	Command(const std::string &c) : cmd(c) {}
	Command(const Command&) = default;
public:
	std::string cmd;
	Command() {}
	static std::unique_ptr<Action> create(const std::string &c) { return std::unique_ptr<Action>(new Command(c)); }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::string get_type() const override { return "Command"; }
	const std::string& get_cmd() const { return cmd; }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Command(*this)); }
};

class ModAction : public Action {
	friend class boost::serialization::access;
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	template<class Archive> void load(Archive & ar, const unsigned int version);
	template<class Archive> void save(Archive & ar, const unsigned int version) const;
protected:
	ModAction() {}
	uint32_t mods = 0;
	ModAction(uint32_t mods_) : mods(mods_) {}
	ModAction(const ModAction&) = default;
public:
	uint32_t get_mods() const { return mods; }
};
BOOST_CLASS_VERSION(ModAction, 1)
/* version 1: save modifiers as enum wlr_keyboard_modifier
 * this notably does not support Gdk's "virtual" modifiers */


class SendKey : public ModAction {
	friend class boost::serialization::access;
	uint32_t key;
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	template<class Archive> void load(Archive & ar, const unsigned int version);
	template<class Archive> void save(Archive & ar, const unsigned int version) const;
	SendKey(uint32_t key_, uint32_t mods) :
		ModAction(mods), key(key_) {}
	SendKey(const SendKey&) = default;
public:
	SendKey() {}
	static std::unique_ptr<Action> create(uint32_t key, uint32_t mods) {
		return std::unique_ptr<Action>(new SendKey(key, mods));
	}

	std::string get_type() const override { return "SendKey"; }
	uint32_t get_key() const { return key; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new SendKey(*this)); }
};
BOOST_CLASS_VERSION(SendKey, 2)
/* version 2: save hardware keycode in key, omit separate code variable */

class SendText : public Action {
	friend class boost::serialization::access;
	std::string text;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
	SendText(Glib::ustring text_) : text(text_) {}
	SendText(const SendText&) = default;
public:
	SendText() {}
	static std::unique_ptr<Action> create(Glib::ustring text) { return std::unique_ptr<Action>(new SendText(text)); }

	std::string get_type() const override { return "SendText"; }
	const Glib::ustring get_text() const { return text; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new SendText(*this)); }
};

class Scroll : public ModAction {
	friend class boost::serialization::access;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
	Scroll(uint32_t mods) : ModAction(mods) {}
	Scroll(const Scroll&) = default;
public:
	Scroll() {}
	static std::unique_ptr<Action> create(uint32_t mods) { return std::unique_ptr<Action>(new Scroll(mods)); }
	std::string get_type() const override { return "Scroll"; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Scroll(*this)); }
};

class Touchpad : public ModAction {
	friend class boost::serialization::access;
public:
	enum Type { NONE, SCROLL, SWIPE, PINCH };
	Type type = NONE;
	uint32_t fingers = 0;
private:
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	template<class Archive> void load(Archive & ar, const unsigned int version);
	template<class Archive> void save(Archive & ar, const unsigned int version) const;
	Touchpad(uint32_t mods, uint32_t fingers_, Type t) : ModAction(mods), type(t), fingers(fingers_) {}
	Touchpad(const Touchpad&) = default;
public:
	Touchpad() {}
	static constexpr uint32_t n_actions = static_cast<uint32_t>(Type::PINCH) + 1;
	static std::unique_ptr<Action> create(Type t, uint32_t fingers, uint32_t mods) { return std::unique_ptr<Action>(new Touchpad(mods, fingers, t)); }
	std::string get_type() const override { return "Touchpad"; }
	Type get_action_type() const { return type; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Touchpad(*this)); }
};

class Ignore : public ModAction {
	friend class boost::serialization::access;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
	Ignore(uint32_t mods) : ModAction(mods) {}
	Ignore(const Ignore&) = default;
public:
	Ignore() {}
	static std::unique_ptr<Action> create(uint32_t mods) { return std::unique_ptr<Action>(new Ignore(mods)); }
	std::string get_type() const override { return "Ignore"; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Ignore(*this)); }
};

class Button : public ModAction {
	friend class boost::serialization::access;
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
	Button(uint32_t mods, uint32_t button_) : ModAction(mods), button(button_) {}
	Button(const Button&) = default;
	uint32_t button = 0;
public:
	Button() {}
	static std::unique_ptr<Action> create(uint32_t mods, uint32_t button_) { return std::unique_ptr<Action>(new Button(mods, button_)); }
	std::string get_type() const override { return "Button"; }
	uint32_t get_button() const { return button; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Button(*this)); }
};

/* Misc action -- not used anymore, kept only for compatibility with old Easystroke config files */
class Misc : public Action {
	friend class boost::serialization::access;
public:
	enum Type { NONE, UNMINIMIZE, SHOWHIDE, DISABLE };
	Type type;
private:
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
	Misc(Type t) : type(t) {}
public:
	Misc() {}
	std::string get_type() const override { return "Misc"; }
	static std::unique_ptr<Action> create(Type t) { return std::unique_ptr<Action>(new Misc(t)); }
	/* does nothing */
	void visit(G_GNUC_UNUSED ActionVisitor* visitor) const override { return; }
	/* convert old Misc actions to new representation */
	std::unique_ptr<Action> convert() const;
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Misc(*this)); }
};

/* new version: instead of Misc, we have Global Actions, View Actions, and Custom Plugin */
class Global : public Action {
	friend class boost::serialization::access;
public:
	enum class Type : uint32_t { NONE, EXPO, SCALE, SCALE_ALL, SHOW_CONFIG, SHOW_DESKTOP };
protected:
	Type type;
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	template<class Archive> void load(Archive & ar, const unsigned int version);
	template<class Archive> void save(Archive & ar, const unsigned int version) const;
	Global(Type t): type(t) { }
	Global(): type(Type::NONE) { }
	Global(const Global&) = default;
public:
	static constexpr uint32_t n_actions = static_cast<uint32_t>(Type::SHOW_DESKTOP) + 1;
	static const char* types[n_actions];
	static const char* get_type_str(Type type);
	std::string get_type() const override { return "Global Action"; }
	static std::unique_ptr<Action> create(Type t) { return std::unique_ptr<Action>(new Global(t)); }
	Type get_action_type() const { return type; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Global(*this)); }
};

/* actions performed on the active view (either directly or via another plugin) */
class View : public Action {
	friend class boost::serialization::access;
public:
	enum class Type : uint32_t { NONE, CLOSE, MAXIMIZE, MOVE, RESIZE, MINIMIZE, FULLSCREEN, SEND_TO_BACK, ALWAYS_ON_TOP, STICKY };
protected:
	Type type;
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	template<class Archive> void load(Archive & ar, const unsigned int version);
	template<class Archive> void save(Archive & ar, const unsigned int version) const;
	View(Type t): type(t) { }
	View(): type(Type::NONE) { }
	View(const View&) = default;
public:
	static constexpr uint32_t n_actions = static_cast<uint32_t>(Type::STICKY) + 1;
	static const char* types[n_actions];
	static const char* get_type_str(Type type);
	std::string get_type() const override { return "View Action"; }
	static std::unique_ptr<Action> create(Type t) { return std::unique_ptr<Action>(new View(t)); }
	Type get_action_type() const { return type; }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new View(*this)); }
};

/* custom plugin activator */
class Plugin : public Action {
	friend class boost::serialization::access;
protected:
	template<class Archive> void serialize(Archive & ar, const unsigned int version);
	std::string cmd;
	Plugin() {}
	Plugin(const std::string &c) : cmd(c) {}
	Plugin(const Plugin&) = default;
public:
	static std::unique_ptr<Action> create(const std::string &c) { return std::unique_ptr<Action>(new Plugin(c)); }
	void visit(ActionVisitor* visitor) const override { visitor->visit(this); }
	std::string get_type() const override { return "Custom Plugin Action"; }
	const std::string& get_action() const { return cmd; }
	std::unique_ptr<Action> clone() const override { return std::unique_ptr<Action>(new Plugin(*this)); }
};

class StrokeInfo {
private:
	friend class ActionDB;
	template<bool uptr> friend class ActionListDiff;
	friend class boost::serialization::access;
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	template<class Archive> void load(Archive & ar, const unsigned int version);
	template<class Archive> void save(Archive & ar, const unsigned int version) const;
	
public:
	StrokeInfo(std::unique_ptr<Action>&& a) : action(std::move(a)) { }
	StrokeInfo() {}
	
	std::unique_ptr<Action> action;
	Stroke stroke;
	std::string name;
};
BOOST_CLASS_VERSION(StrokeInfo, 4)

struct StrokeRow {
	const Stroke* stroke = nullptr;
	const std::string* name = nullptr;
	const Action* action = nullptr;
	bool deleted = false;
	bool stroke_overwrite = false;
	bool name_overwrite = false;
	bool action_overwrite = false;
};

class Ranking {
	int x, y;
public:
	const Stroke *stroke, *best_stroke;
	Action* action;
	double score;
	std::string name;
	std::multimap<double, std::pair<std::string, const Stroke*> > r;
};


typedef uint32_t stroke_id;
class Unique;
class ActionDB;

template<bool uptr>
class ActionListDiff {
	private:
	friend class boost::serialization::access;
	friend class ActionDB;
	using unique_t = typename std::conditional<uptr, Unique*, stroke_id>::type;
	
	template<class Archive> void serialize(Archive & ar, const unsigned int version) {
		ar & deleted;
		ar & added;
		ar & name;
		ar & children;
		ar & app;
		if constexpr (!uptr) {
			ar & parent;
			return;
		}
		if (version == 0) return;
		ar & order;
	}

	ActionListDiff *parent = nullptr;
	std::set<unique_t> deleted;
	std::map<unique_t, StrokeInfo> added;
	std::list<unique_t> order; // only for old version (uptr == true)
	std::list<ActionListDiff> children;

	void remove(unique_t id, bool really, ActionListDiff* skip = nullptr);
public:
	int level = 0;
	bool app = false;
	std::string name;
	
	typedef typename std::list<ActionListDiff>::iterator iterator;
	iterator begin() { return children.begin(); }
	iterator end() { return children.end(); }
	ActionListDiff *get_parent() { return parent; }

	StrokeRow get_info(unique_t id, bool need_attr = true) const;
	const std::string& get_stroke_name(unique_t id) const;
	Action* get_stroke_action(unique_t id) const {
		auto it = added.find(id);
		if(it != added.end() && it->second.action) return it->second.action.get();
		//!! TODO: check for non-null parent ??
		return parent->get_stroke_action(id);
	}
	bool has_stroke(unique_t id) const {
		auto it = added.find(id);
		if(it != added.end() && !it->second.stroke.trivial()) return true;
		return false;
	}

	int size_rec() const {
		int size = added.size();
		for (auto i = children.begin(); i != children.end(); i++)
			size += i->size_rec();
		return size;
	}
	bool resettable(unique_t id) const { return parent && (added.count(id) || deleted.count(id)) && parent->contains(id); }

	void set_action(unique_t id, std::unique_ptr<Action>&& action) { added[id].action = std::move(action); }
	void set_stroke(unique_t id, Stroke&& stroke) { added[id].stroke = std::move(stroke); }
	void set_name(unique_t id, std::string name) { added[id].name = std::move(name); }
	bool contains(unique_t id) const {
		if (deleted.count(id))
			return false;
		if (added.count(id))
			return true;
		return parent && parent->contains(id);
	}
	void reset(unique_t id);
	void add_apps(std::map<std::string, ActionListDiff *> &apps) {
		if (app) apps[name] = this;
		for (auto& x : children) x.add_apps(apps);
	}
	ActionListDiff *add_child(std::string name, bool app);

	std::map<unique_t, const Stroke*> get_strokes() const;
	std::set<unique_t> get_ids(bool include_deleted) const;
 	int count_actions() const {
		if(parent) return get_ids(false).size();
		else return added.size();
	}
	Action* handle(const Stroke& s, Ranking* r) const;
};
BOOST_CLASS_VERSION(ActionListDiff<true>, 1)
BOOST_CLASS_VERSION(ActionListDiff<false>, 1)


class ActionDB {
private:
	/* input / output via boost */
	friend class boost::serialization::access;
	friend class ActionListDiff<false>;
	template<class Archive> void load(Archive & ar, const unsigned int version);
	template<class Archive> void save(Archive & ar, const unsigned int version) const;
	BOOST_SERIALIZATION_SPLIT_MEMBER()
	
	/* Recursively convert an ActionListDiff tree from an older version.
	 * This can be called from the load() function above. */
	void convert_actionlist(ActionListDiff<false>& dst, ActionListDiff<true>& src,
		std::unordered_map<Unique*, stroke_id>& mapping, std::unordered_set<Unique*>& extra_unique);
	unsigned int read_version = 0;

	/* Main storage */
	std::map<std::string, ActionListDiff<false>*> apps;
	ActionListDiff<false> root;
	std::unordered_set<std::string> exclude_apps;
	
	/* Storage of stroke_ids.
	 * We store all stroke_ids in the order they should appear in the
	 * gesture list along with a mapping from stroke_id to their sort order. */
	std::list<stroke_id> stroke_order;
	/* Each stroke_id has an "owner", that is the ActionListDiff where it was added. */
	std::unordered_map<stroke_id, std::pair<unsigned int, ActionListDiff<false>*>> stroke_map;
	/* Next available ID. Setting this to 0 means no strokes can be added. */
	stroke_id next_id = 1;
	/* Available (previously deleted) stroke IDs that can be reused. */
	std::vector<stroke_id> available_ids;
	
	/* Get a new stroke ID (for adding a new stroke). */
	stroke_id get_next_id() {
		if(!next_id) throw std::runtime_error("ActionDB: read-only database!\n");
		stroke_id ret = next_id;
		if(available_ids.size()) {
			ret = available_ids.back();
			available_ids.pop_back();
		}
		else next_id++;
		return ret;
	}
	/* Remove a stroke ID that is no longer used. */
	void free_id(stroke_id id) {
		if(id >= next_id) throw std::runtime_error("ActionDB: too large ID to remove!\n");
		if(id + 1 == next_id) next_id--;
		else available_ids.push_back(id);
	}
	
	/* Remove a set of strokes from the stroke_order list. Uses a sort
	 * if the number of strokes is large to avoid quadratic runtime. */
	template<class iter> void remove_strokes_helper(iter&& begin, iter&& end);
	template<class iter> void remove_strokes_from_order(iter begin, iter end, bool readonly = false);
	
	/* Helper to remove an app. */
	void remove_app_r(ActionListDiff<false>* app);
	
	/* Helper for merging two ActionDBs. */
	void merge_actions_r(ActionListDiff<false>* dst, ActionListDiff<false>* src, std::unordered_map<stroke_id, stroke_id>& id_map);
	
	/* Needed for clear(). */
	ActionDB(ActionDB&&) = default;
	ActionDB& operator = (ActionDB&&) = default;
	
public:
	/******************************************************************
	 * Input / output                                                 */
	
	/* Try to read actions from the given config file. Returns false if
	 * no config file found, throws an exception on other errors.
	 * Note: this will clear any existing actions first. */
	bool read(const std::string& config_file_name, bool readonly = false);
	/* Try to save actions to the config file; throws exception on failure. */
	void write(const std::string& config_file_name) const;
	/* During read(), the version of the archive is stored. It can be retrieved here
	 * and used to decide if a conversion from an older took place during loading. */
	unsigned int get_read_version() const { return read_version; }
	/* Merge or replace the contents of this ActionDB with the given other one. */
	void merge_actions(ActionDB&& other);
	void overwrite_actions(ActionDB&& other);
	
	
	/******************************************************************
	 * Handling apps and groups of apps                               */

	const ActionListDiff<false> *get_action_list(const std::string& wm_class) const {
		auto i = apps.find(wm_class);
		return i == apps.end() ? nullptr : i->second;
	}
	
	const ActionListDiff<false> *get_root() const { return &root; }
	ActionListDiff<false> *get_root() { return &root; }
	
	/* Add a new ActionListDiff corresponding to the given name with
	 * the given parent.
	 * Preconditions: parent must belong to this ActionDB instance and
	 * name must not have previously been added. */
	ActionListDiff<false>* add_app(ActionListDiff<false>* parent, const std::string& name, bool real_app);
	
	/* Remove an app or group that belongs to this ActionDB and is not the root. */
	void remove_app(ActionListDiff<false>* app);
	
	/* Manage apps that are excluded. */
	const std::unordered_set<std::string>& get_exclude_apps() const { return exclude_apps; }
	bool exclude_app(const std::string& cl) const { return exclude_apps.count(cl); }
	bool add_exclude_app(const std::string& cl) { return exclude_apps.insert(cl).second; }
	bool remove_exclude_app(const std::string& cl) { return exclude_apps.erase(cl); }
	
	
	/******************************************************************
	 * Manage strokes.                                                */
	const ActionListDiff<false>* get_stroke_owner(stroke_id id) const { return stroke_map.at(id).second; }
	unsigned int get_stroke_order(stroke_id id) const { return stroke_map.at(id).first; }
	unsigned int count_owned_strokes(const ActionListDiff<false>* parent) const;
	
	/* Add a new stroke owned by the ActionList given as parent. */
	stroke_id add_stroke(ActionListDiff<false>* parent, StrokeInfo&& si, stroke_id before = 0);
	
	/* Remove a set of strokes together (avoiding quadratic runtime). */
	template<class it>
	void remove_strokes(ActionListDiff<false>* parent, it&& begin, it&& end);
	
	/* Remove or disable the given stroke from the ActionList given.
	 * If the stroke is owned by this ActionList, it is deleted recursively;
	 * otherwise, it is only disabled. */
	void remove_stroke(ActionListDiff<false>* parent, stroke_id id);
	
	/* move one stroke, changing the ordering of strokes */
	void move_stroke(stroke_id id, stroke_id before, bool after);
	
	/* move a set of strokes from their position to be before dst */
	template<class it>
	void move_strokes(it&& begin, it&& end, stroke_id before, bool after);
	
	/* Move or copy strokes between apps / groups. Returns if the stroke
	 * was removed from src. */
	bool move_stroke_to_app(ActionListDiff<false>* src, ActionListDiff<false>* dst, stroke_id id);
	
	void clear() { *this = ActionDB(); }
	
	ActionDB() { root.name = _("Default"); }
	
	/* Config file names */
	static char const * const wstroke_actions_versions[3];
	static char const * const easystroke_actions_versions[5];
};
BOOST_CLASS_VERSION(ActionDB, 5)

#endif

