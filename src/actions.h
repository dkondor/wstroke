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
#ifndef __ACTIONS_H__
#define __ACTIONS_H__

#include <gtkmm.h>
#include <memory>
#include <glibmm/main.h>
#include "actiondb.h"


class TreeViewMulti : public Gtk::TreeView {
	bool pending;
	Gtk::TreePath path;
	virtual bool on_button_press_event(GdkEventButton* event);
	virtual bool on_button_release_event(GdkEventButton* event);
	virtual void on_drag_begin(const Glib::RefPtr<Gdk::DragContext> &context);
public:
	TreeViewMulti();
};

class Actions {
	public:
		Actions(const std::string& config_dir_, Glib::RefPtr<Gtk::Builder>& widgets_) : widgets(widgets_), config_dir(config_dir_) { }
		void startup(Gtk::Application* app, Gtk::Dialog* message_dialog = nullptr);
	private:
		void on_button_delete();
		void on_button_new();
		void on_selection_changed();
		void on_name_edited(const Glib::ustring& path, const Glib::ustring& new_text);
		void on_type_edited(const Glib::ustring& path, const Glib::ustring& new_text);
		void on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column);
		void on_cell_data_name(Gtk::CellRenderer* cell, const Gtk::TreeModel::iterator& iter);
		void on_cell_data_type(Gtk::CellRenderer* cell, const Gtk::TreeModel::iterator& iter);
		void save_actions();
		void update_actions() { actions_changed = true; }
	public:
		void on_accel_edited(const gchar *path_string, guint accel_key, GdkModifierType accel_mods);
		void on_combo_edited(const gchar *path_string, guint item);
		void on_arg_editing_started(GtkCellEditable *editable, const gchar *path);
		void on_text_edited(const gchar *path, const gchar *new_text);
		void on_cell_data_arg(GtkCellRenderer *cell, gchar *path);
		
		Gtk::Window* get_main_win() { return main_win.get(); }
		void exit() { exiting = true; save_actions(); }
		
		ActionDB actions;
		
	private:
		int compare_ids(const Gtk::TreeModel::iterator &a, const Gtk::TreeModel::iterator &b);
		class OnStroke;

		void focus(stroke_id id, int col, bool edit);

		void on_add_app();
		void on_add_group();
		void on_apps_selection_changed();
		void load_app_list(const Gtk::TreeNodeChildren &ch, ActionListDiff<false> *actions);
		void update_action_list();
		void update_row(const Gtk::TreeRow &row);
		void update_counts();
		void on_remove_app();
		
		bool select_exclude_row(const Gtk::TreeModel::Path& path, const Gtk::TreeModel::iterator& iter, const std::string& name);
		void on_add_exclude();
		void on_remove_exclude();
		
		class ModelColumns : public Gtk::TreeModel::ColumnRecord {
			public:
				ModelColumns() {
					add(stroke); add(name); add(type); add(arg); add(cmd_save); add(id);
					add(name_bold); add(action_bold); add(deactivated);
				}
				Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf> > stroke;
				Gtk::TreeModelColumn<Glib::ustring> name, type, arg, cmd_save, plugin_action_save;
				Gtk::TreeModelColumn<stroke_id> id;
				Gtk::TreeModelColumn<bool> name_bold, action_bold;
				Gtk::TreeModelColumn<bool> deactivated;
		};
		class Store : public Gtk::ListStore {
			Actions *parent;
			public:
				Store(const Gtk::TreeModelColumnRecord &columns, Actions *p) : Gtk::ListStore(columns), parent(p) {}
				static Glib::RefPtr<Store> create(const Gtk::TreeModelColumnRecord &columns, Actions *parent) {
					return Glib::RefPtr<Store>(new Store(columns, parent));
				}
			protected:
				bool row_draggable_vfunc(const Gtk::TreeModel::Path &path) const;
				bool row_drop_possible_vfunc(const Gtk::TreeModel::Path &dest, const Gtk::SelectionData &selection) const;
				bool drag_data_received_vfunc(const Gtk::TreeModel::Path &dest, const Gtk::SelectionData& selection);
		};
		class AppsStore : public Gtk::TreeStore {
			Actions *parent;
			public:
				AppsStore(const Gtk::TreeModelColumnRecord &columns, Actions *p) : Gtk::TreeStore(columns), parent(p) {}
				static Glib::RefPtr<AppsStore> create(const Gtk::TreeModelColumnRecord &columns, Actions *parent) {
					return Glib::RefPtr<AppsStore>(new AppsStore(columns, parent));
				}
			protected:
				bool row_drop_possible_vfunc(const Gtk::TreeModel::Path &dest, const Gtk::SelectionData &selection) const;
				bool drag_data_received_vfunc(const Gtk::TreeModel::Path &dest, const Gtk::SelectionData& selection);
		};
		ModelColumns cols;
		TreeViewMulti tv;
		Glib::RefPtr<Store> tm;

		Gtk::TreeView *apps_view = nullptr;
		Glib::RefPtr<AppsStore> apps_model;
		/* helper to find a given app in apps_view / apps_model */
		bool get_action_item(const ActionListDiff<false>* x, Gtk::TreeIter& it);

		class Single : public Gtk::TreeModel::ColumnRecord {
			public:
				Single() { add(type); }
				Gtk::TreeModelColumn<Glib::ustring> type;
		};
		Single type;

		class Apps : public Gtk::TreeModel::ColumnRecord {
			public:
				Apps() { add(app); add(actions); add(count); }
				Gtk::TreeModelColumn<Glib::ustring> app;
				Gtk::TreeModelColumn<ActionListDiff<false>*> actions;
				Gtk::TreeModelColumn<int> count;
		};
		Apps ca;
		
		/* exception list */
		Single exclude_cols;
		Glib::RefPtr<Gtk::ListStore> exclude_tm;
		Gtk::TreeView* exclude_tv;

		struct Focus;

		Glib::RefPtr<Gtk::ListStore> type_store;

		Gtk::Button *button_record, *button_delete, *button_remove_app, *button_reset_actions;
		Gtk::CheckButton *check_show_deleted;
		Gtk::Expander *expander_apps;
		Gtk::VPaned *vpaned_apps;

		int vpaned_position;
		bool editing_new = false;
		bool editing = false;

		ActionListDiff<false>* action_list;
		Glib::RefPtr<Gtk::Builder> widgets;
		
		/* import / export */
		Gtk::Window* import_dialog;
		Gtk::Button* button_import_cancel;
		Gtk::Button* button_import_import;
		Gtk::FileChooserButton* import_file_chooser;
		Gtk::RadioButton* import_add;
		Gtk::InfoBar* import_info;
		Gtk::Label* import_info_label;
		void try_import();
		void try_export();
		
		/* main window */
		std::unique_ptr<Gtk::Window> main_win;
		const std::string config_dir;
		Glib::RefPtr<Glib::TimeoutSource> timeout; /* timeout for saving changes */
		bool actions_changed = false;
		bool exiting = false;
		bool save_error = false;
};

#endif

