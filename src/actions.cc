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
#include "actions.h"
#include "actiondb.h"
#include "stroke_draw.h"
#include "convert_keycodes.h"
#include "input_inhibitor.h"
#include <filesystem>
#include <toplevel-grabber.h>
#include <glibmm/i18n.h>
#include <gdkmm.h>
#include <gtkmm/filechoosernative.h>
#include <gdk/gdkwayland.h>

#include <X11/XKBlib.h>
#include "cellrenderertextish.h"
#include "stroke_drawing_area.h"
#include "config.h"
#include <typeinfo>

class SelectButton {
public:
	SelectButton(const Button& bt, Glib::RefPtr<Gtk::Builder>& widgets_);
	~SelectButton();
	bool run();
	uint32_t button = 0;
	uint32_t state = 0;
private:
	Gtk::MessageDialog *dialog;
	bool on_button_press(GdkEventButton *ev);

	Gtk::EventBox *eventbox;
	Gtk::ToggleButton *toggle_shift, *toggle_control, *toggle_alt, *toggle_super;
	Gtk::ComboBoxText *select_button;
	sigc::connection handler;
	Glib::RefPtr<Gtk::Builder> widgets;
};

class SelectTouchpad {
public:
	SelectTouchpad(const Touchpad& bt, Glib::RefPtr<Gtk::Builder>& widgets_, const Glib::ustring& action_name);
	~SelectTouchpad() { }
	bool run();
	uint32_t get_fingers() const;
	Touchpad::Type get_type() const;
	uint32_t state = 0;
private:
	Gtk::Dialog *dialog;
	Gtk::ToggleButton *toggle_shift, *toggle_control, *toggle_alt, *toggle_super;
	Gtk::RadioButton *radio_scroll, *radio_pinch, *radio_swipe;
	Gtk::SpinButton *spin_fingers;
	Gtk::Adjustment *adj_fingers;
	Glib::RefPtr<Gtk::Builder> widgets;
};

bool TreeViewMulti::on_button_press_event(GdkEventButton* event) {
	int cell_x, cell_y;
	Gtk::TreeViewColumn *column;
	pending = (get_path_at_pos(event->x, event->y, path, column, cell_x, cell_y))
		&& (get_selection()->is_selected(path))
		&& !(event->state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK));
	return Gtk::TreeView::on_button_press_event(event);
}

bool TreeViewMulti::on_button_release_event(GdkEventButton* event) {
	if (pending) {
		pending = false;
		get_selection()->unselect_all();
		get_selection()->select(path);
	}
	return Gtk::TreeView::on_button_release_event(event);
}

void TreeViewMulti::on_drag_begin(const Glib::RefPtr<Gdk::DragContext> &context) {
	pending = false;
	if (get_selection()->count_selected_rows() <= 1)
		return Gtk::TreeView::on_drag_begin(context);
	Glib::RefPtr<Gdk::Pixbuf> pb = render_icon_pixbuf(Gtk::Stock::DND_MULTIPLE, Gtk::ICON_SIZE_DND);
	context->set_icon(pb, pb->get_width(), pb->get_height());
}

TreeViewMulti::TreeViewMulti() : Gtk::TreeView(), pending(false) {
	get_selection()->set_select_function(
	[this](Glib::RefPtr<Gtk::TreeModel> const&, Gtk::TreeModel::Path const&, bool) {
           return !pending;
       });
}

enum class Type { COMMAND, KEY, TEXT, SCROLL, IGNORE, BUTTON, /* MISC, */ GLOBAL, VIEW, PLUGIN, TOUCHPAD };

struct TypeInfo {
	Type type;
	const char *name;
	const std::type_info *type_info;
	const CellRendererTextishMode mode;
};

static constexpr TypeInfo all_types[] = {
	{ Type::COMMAND, N_("Command"),         &typeid(Command),  CELL_RENDERER_TEXTISH_MODE_Popup },
	{ Type::KEY,     N_("Key"),             &typeid(SendKey),  CELL_RENDERER_TEXTISH_MODE_Key   },
//	{ Type::TEXT,    N_("Text"),            &typeid(SendText), CELL_RENDERER_TEXTISH_MODE_Text  },
//	{ Type::SCROLL,  N_("Scroll"),          &typeid(Scroll),   CELL_RENDERER_TEXTISH_MODE_Key   },
	{ Type::IGNORE,  N_("Ignore"),          &typeid(Ignore),   CELL_RENDERER_TEXTISH_MODE_Key   },
	{ Type::BUTTON,  N_("Button"),          &typeid(Button),   CELL_RENDERER_TEXTISH_MODE_Popup },
//	{ Type::MISC,    N_("WM Action (old)"), &typeid(Misc),     CELL_RENDERER_TEXTISH_MODE_Combo },
	{ Type::GLOBAL,  N_("Global Action"),   &typeid(Global),   CELL_RENDERER_TEXTISH_MODE_Combo },
	{ Type::VIEW,    N_("WM Action"),       &typeid(View),     CELL_RENDERER_TEXTISH_MODE_Combo },
	{ Type::PLUGIN,  N_("Custom Plugin"),   &typeid(Plugin),   CELL_RENDERER_TEXTISH_MODE_Text  },
	{ Type::TOUCHPAD,N_("Touchpad Gesture"),&typeid(Touchpad), CELL_RENDERER_TEXTISH_MODE_Popup },
	{ Type::COMMAND, 0,                     0,                 CELL_RENDERER_TEXTISH_MODE_Text  }
};

static Type from_name(const Glib::ustring& name) {
	for (const TypeInfo* i = all_types; i->name; i++)
		if (!i->name || _(i->name) == name)
			return i->type;
	return Type::COMMAND; /* not reached */
}

static const TypeInfo& type_info_from_name(const Glib::ustring& name) {
	for (const TypeInfo* i = all_types; i->name; i++)
		if (!i->name || _(i->name) == name)
			return *i;
	return all_types[7]; /* not reached */
}

static constexpr const char *type_info_to_name(const std::type_info *info) {
	for (const TypeInfo* i = all_types; i->name; i++)
		if (i->type_info == info)
			return _(i->name);
	return "";
}

static Glib::ustring get_action_label(const Action* action);

static void on_actions_cell_data_arg(G_GNUC_UNUSED GtkTreeViewColumn *tree_column, GtkCellRenderer *cell, GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data) {
	GtkTreePath *path = gtk_tree_model_get_path(tree_model, iter);
	gchar *path_string = gtk_tree_path_to_string(path);
	((Actions *)data)->on_cell_data_arg(cell, path_string);
	g_free(path_string);
	gtk_tree_path_free(path);
}

static void on_actions_accel_edited(CellRendererTextish *, gchar *path, GdkModifierType mods, guint code, gpointer data) {
	((Actions *)data)->on_accel_edited(path, code, mods);
}

static void on_actions_combo_edited(CellRendererTextish *, gchar *path, guint row, gpointer data) {
	((Actions *)data)->on_combo_edited(path, row);
}

static void on_actions_text_edited(GtkCellRendererText *, gchar *path, gchar *new_text, gpointer data) {
	((Actions *)data)->on_text_edited(path, new_text);
}

static void on_actions_editing_started(GtkCellRenderer *, GtkCellEditable *editable, const gchar *path, gpointer data) {
	((Actions *)data)->on_arg_editing_started(editable, path);
}

static void on_stroke_editing_started(GtkCellRenderer *, GtkCellEditable *, const gchar *path, gpointer data) {
	((Actions *)data)->on_stroke_editing(path);
}

void Actions::startup(Gtk::Application* app, Gtk::Dialog* message_dialog) {
	{
		Gtk::Window* tmp = nullptr;
		widgets->get_widget("main", tmp);
		main_win.reset(tmp);
		if(message_dialog) {
			message_dialog->set_transient_for(*main_win);
			message_dialog->set_modal(true);
			main_win->signal_show().connect([message_dialog]() {
				message_dialog->show();
				message_dialog->run();
				delete message_dialog;
			});
		}
		app->add_window(*tmp);
	}
	
	chooser.startup();
	
	timeout = Glib::TimeoutSource::create(5000);
	action_list = actions.get_root();
	
	Gtk::ScrolledWindow *sw;
	widgets->get_widget("scrolledwindow_actions", sw);
	widgets->get_widget("treeview_apps", apps_view);
	sw->add(tv);
	tv.show();
	
	Gtk::AboutDialog *about_dialog;
	widgets->get_widget("about-dialog", about_dialog);
	about_dialog->set_wrap_license(true);
	about_dialog->signal_response().connect([about_dialog](G_GNUC_UNUSED int response_id) { about_dialog->hide(); });
	
	Gtk::Button *button_add, *button_add_app, *button_add_group, *button_about;
	widgets->get_widget("button_add_action", button_add);
	widgets->get_widget("button_delete_action", button_delete);
	widgets->get_widget("button_record", button_record);
	widgets->get_widget("button_add_app", button_add_app);
	widgets->get_widget("button_add_group", button_add_group);
	widgets->get_widget("button_remove_app", button_remove_app);
	widgets->get_widget("button_reset_actions", button_reset_actions);
	widgets->get_widget("button_about", button_about);
	widgets->get_widget("check_show_deleted", check_show_deleted);
	
	Gtk::Button *button_import, *button_export;
	Gtk::LinkButton *import_easystroke, *import_default;
	widgets->get_widget("import_dialog", import_dialog);
	widgets->get_widget("button_import", button_import);
	widgets->get_widget("button_export", button_export);
	widgets->get_widget("import_import", button_import_import);
	widgets->get_widget("import_cancel", button_import_cancel);
	widgets->get_widget("import_easystroke", import_easystroke);
	widgets->get_widget("import_default", import_default);
	widgets->get_widget("import_file_chooser", import_file_chooser);
	widgets->get_widget("import_add", import_add);
	widgets->get_widget("import_info", import_info);
	widgets->get_widget("import_info_label", import_info_label);
	button_export->signal_clicked().connect([this]() {
		try_export();
	});
	button_import->signal_clicked().connect([this]() {
		import_dialog->show_all();
	});
	button_import_cancel->signal_clicked().connect([this]() {
		import_dialog->close();
	});
	button_import_import->signal_clicked().connect([this]() {
		try_import();
	});
	import_easystroke->signal_activate_link().connect([this]() {
		std::string old_config_dir = std::string(getenv("HOME")) + "/.easystroke/";
		std::error_code ec;
		bool found = false;
		if(std::filesystem::exists(old_config_dir, ec) && std::filesystem::is_directory(config_dir, ec)) {
			for(const char* const * x = ActionDB::easystroke_actions_versions; *x; ++x) {
				std::string fn = old_config_dir + *x;
				if(std::filesystem::exists(fn, ec) && std::filesystem::is_regular_file(fn, ec)) {
					import_file_chooser->set_filename(fn);
					found = true;
					break;
				}
			}
		}
		if(!found) {
			import_info_label->set_text(_("Cannot find Easystroke configuration. Make sure that Easystroke is properly installed."));
			import_info->show_all();
			gtk_info_bar_set_revealed(import_info->gobj(), TRUE);
		}
		else gtk_info_bar_set_revealed(import_info->gobj(), FALSE);
		return true;
	}, false);
	import_default->signal_activate_link().connect([this]() {
		std::string fn = std::string(DATA_DIR) + "/" + ActionDB::wstroke_actions_versions[0];
		std::error_code ec;
		if(std::filesystem::exists(fn, ec) && std::filesystem::is_regular_file(fn, ec)) {
			import_file_chooser->set_filename(fn);
			gtk_info_bar_set_revealed(import_info->gobj(), FALSE);
		}
		else {
			import_info_label->set_text(_("Cannot find the default configuration. Make sure that WStroke is properly installed."));
			import_info->show_all();
			gtk_info_bar_set_revealed(import_info->gobj(), TRUE);
		}
		return true;
	}, false);
	import_info->signal_response().connect([this] (auto) { gtk_info_bar_set_revealed(import_info->gobj(), FALSE); });
	import_file_chooser->signal_file_set().connect([this] () { gtk_info_bar_set_revealed(import_info->gobj(), FALSE); });
	
	button_record->signal_clicked().connect([this]() {
		Gtk::TreeModel::Path path;
		Gtk::TreeViewColumn *col;
		tv.get_cursor(path, col);
		Gtk::TreeRow row(*tm->get_iter(path));
		on_row_activated(row);
	});
	button_delete->signal_clicked().connect(sigc::mem_fun(*this, &Actions::on_button_delete));
	button_add->signal_clicked().connect(sigc::mem_fun(*this, &Actions::on_button_new));
	button_add_app->signal_clicked().connect(sigc::mem_fun(*this, &Actions::on_add_app));
	button_add_group->signal_clicked().connect(sigc::mem_fun(*this, &Actions::on_add_group));
	button_remove_app->signal_clicked().connect(sigc::mem_fun(*this, &Actions::on_remove_app));
	button_reset_actions->signal_clicked().connect([this]() {
		std::vector<Gtk::TreePath> paths = tv.get_selection()->get_selected_rows();
			for(const auto& x : paths) {
				Gtk::TreeRow row(*tm->get_iter(x));
				action_list->reset(row[cols.id]);
			}
			update_action_list();
			on_selection_changed();
			update_actions();
		});
	button_about->signal_clicked().connect([about_dialog](){ about_dialog->run(); });

	tv.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &Actions::on_selection_changed));

	tm = Store::create(cols, this);

	tv.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);

	int n;
	CellRendererTextish *stroke_renderer = cell_renderer_textish_new();
	stroke_renderer->mode = CELL_RENDERER_TEXTISH_MODE_Popup;
	GtkTreeViewColumn *col_stroke = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(col_stroke, GTK_CELL_RENDERER (stroke_renderer), TRUE);
	gtk_tree_view_column_add_attribute(col_stroke, GTK_CELL_RENDERER (stroke_renderer), "icon", cols.stroke.index());
	gtk_tree_view_column_set_title(col_stroke, _("Stroke"));
	gtk_tree_view_column_set_sort_column_id(col_stroke, cols.id.index());
	gtk_tree_view_append_column(tv.gobj(), col_stroke);
	g_object_set(stroke_renderer, "editable", true, nullptr);
	g_signal_connect(stroke_renderer, "editing-started", G_CALLBACK(on_stroke_editing_started), this);
	
	tm->set_sort_func(cols.id, sigc::mem_fun(*this, &Actions::compare_ids));
	tm->set_default_sort_func(sigc::mem_fun(*this, &Actions::compare_ids));
	tm->set_sort_column(Gtk::TreeSortable::DEFAULT_SORT_COLUMN_ID, Gtk::SORT_ASCENDING);

	n = tv.append_column(_("Name"), cols.name);
	Gtk::CellRendererText *name_renderer = dynamic_cast<Gtk::CellRendererText *>(tv.get_column_cell_renderer(n-1));
	name_renderer->property_editable() = true;
	name_renderer->signal_edited().connect(sigc::mem_fun(*this, &Actions::on_name_edited));
	name_renderer->signal_editing_started().connect([this]
		(G_GNUC_UNUSED Gtk::CellEditable* editable, G_GNUC_UNUSED const Glib::ustring& path) { editing = true; });
	name_renderer->signal_editing_canceled().connect([this] () { editing_new = false; });
	Gtk::TreeView::Column *col_name = tv.get_column(n-1);
	col_name->set_sort_column(cols.name);
	col_name->set_cell_data_func(*name_renderer, sigc::mem_fun(*this, &Actions::on_cell_data_name));
	col_name->set_resizable();

	type_store = Gtk::ListStore::create(type);
	for(const TypeInfo *i = all_types; i->name; i++)
		(*(type_store->append()))[type.type] = _(i->name);

	Gtk::CellRendererCombo *type_renderer = Gtk::manage(new Gtk::CellRendererCombo);
	type_renderer->property_model() = type_store;
	type_renderer->property_editable() = true;
	type_renderer->property_text_column() = 0;
	type_renderer->property_has_entry() = false;
	type_renderer->signal_edited().connect(sigc::mem_fun(*this, &Actions::on_type_edited));
	type_renderer->signal_editing_started().connect([this]
		(G_GNUC_UNUSED Gtk::CellEditable* editable, G_GNUC_UNUSED const Glib::ustring& path) { editing = true; });
	type_renderer->signal_editing_canceled().connect([this] () { editing_new = false; });

	n = tv.append_column(_("Type"), *type_renderer);
	Gtk::TreeView::Column *col_type = tv.get_column(n-1);
	col_type->add_attribute(type_renderer->property_text(), cols.type);
	col_type->set_cell_data_func(*type_renderer, sigc::mem_fun(*this, &Actions::on_cell_data_type));

	CellRendererTextish *arg_renderer = cell_renderer_textish_new();
	GtkCellRenderer *cmd_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col_arg = gtk_tree_view_column_new();
	gtk_tree_view_column_pack_start(col_arg, GTK_CELL_RENDERER (arg_renderer), TRUE);
	gtk_tree_view_column_pack_start(col_arg, cmd_renderer, FALSE);
	gtk_tree_view_column_add_attribute(col_arg, GTK_CELL_RENDERER (arg_renderer), "text", cols.arg.index());
	gtk_tree_view_column_add_attribute(col_arg, GTK_CELL_RENDERER (arg_renderer), "icon", cols.action_icon.index());
	gtk_tree_view_column_add_attribute(col_arg, cmd_renderer, "text", cols.cmd_path.index());
	gtk_tree_view_column_set_title(col_arg, _("Details"));
	gtk_tree_view_append_column(tv.gobj(), col_arg);

	gtk_tree_view_column_set_cell_data_func (col_arg, GTK_CELL_RENDERER (arg_renderer), on_actions_cell_data_arg, this, nullptr);
	gtk_tree_view_column_set_resizable(col_arg, true);
	g_object_set(arg_renderer, "editable", true, nullptr);
	g_object_set(cmd_renderer, "editable", true, nullptr);
	g_object_set(cmd_renderer, "max-width-chars", 35, nullptr);
	g_object_set(cmd_renderer, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
	g_signal_connect(arg_renderer, "key-edited", G_CALLBACK(on_actions_accel_edited), this);
	g_signal_connect(arg_renderer, "combo-edited", G_CALLBACK(on_actions_combo_edited), this);
	g_signal_connect(arg_renderer, "edited", G_CALLBACK(on_actions_text_edited), this);
	g_signal_connect(cmd_renderer, "edited", G_CALLBACK(on_actions_text_edited), this);
	g_signal_connect(arg_renderer, "editing-started", G_CALLBACK(on_actions_editing_started), this);
	
	load_command_infos();
	update_action_list();
	tv.set_model(tm);
	tv.enable_model_drag_source();
	tv.enable_model_drag_dest();

	check_show_deleted->signal_toggled().connect(sigc::mem_fun(*this, &Actions::update_action_list));
	apps_view->get_selection()->signal_changed().connect(sigc::mem_fun(*this, &Actions::on_apps_selection_changed));
	apps_model = AppsStore::create(ca, this);

	load_app_list(apps_model->children(), actions.get_root());
	update_counts();

	apps_view->append_column_editable(_("Application"), ca.app);
	apps_view->get_column(0)->set_expand(true);
	apps_view->get_column(0)->set_cell_data_func(*apps_view->get_column_cell_renderer(0),
		[this](Gtk::CellRenderer* cell, const Gtk::TreeModel::iterator& iter) {
			ActionListDiff<false> *as = (*iter)[ca.actions];
			Gtk::CellRendererText *renderer = dynamic_cast<Gtk::CellRendererText *>(cell);
			if (renderer)
				renderer->property_editable().set_value(actions.get_root() != as && !as->app);
		});
			
	Gtk::CellRendererText *app_name_renderer =
		dynamic_cast<Gtk::CellRendererText *>(apps_view->get_column_cell_renderer(0));
	app_name_renderer->signal_edited().connect([this](const Glib::ustring& path, const Glib::ustring& new_text) {
		Gtk::TreeRow row(*apps_model->get_iter(path));
		row[ca.app] = new_text;
		ActionListDiff<false> *as = row[ca.actions];
		as->name = new_text;
		update_actions();
	});
	apps_view->append_column(_("Actions"), ca.count);

	apps_view->set_model(apps_model);
	apps_view->enable_model_drag_dest();
	apps_view->expand_all();
	
	/* list of exceptions */
	Gtk::Button *add_exception, *remove_exception;
	widgets->get_widget("button_add_exception", add_exception);
	widgets->get_widget("button_remove_exception", remove_exception);
	widgets->get_widget("treeview_exceptions", exclude_tv);
	
	exclude_tm = Gtk::ListStore::create(exclude_cols);
	exclude_tv->set_model(exclude_tm);
	exclude_tv->append_column(_("Application (WM__CLASS)"), exclude_cols.type);
	exclude_tm->set_sort_column(exclude_cols.type, Gtk::SORT_ASCENDING);
	
	add_exception->signal_clicked().connect(sigc::mem_fun(*this, &Actions::on_add_exclude));
	remove_exception->signal_clicked().connect(sigc::mem_fun(*this, &Actions::on_remove_exclude));
	
	for(const std::string& cl : actions.get_exclude_apps()) {
		Gtk::TreeModel::Row row = *(exclude_tm->append());
		row[exclude_cols.type] = cl;
	}
	
	/* timeout for saving actions */
	timeout->connect([this] () {
		if(exiting) return false;
		if(actions_changed) {
			save_actions();
			actions_changed = false;
		}
		return true;
	});
	timeout->attach();
	
	/* dialog to select touchpad actions */
	Gtk::RadioButton *radio_scroll;
	Gtk::SpinButton *spin_fingers;
	widgets->get_widget("touchpad_type_scroll", radio_scroll);
	widgets->get_widget("touchpad_fingers", spin_fingers);
	radio_scroll->signal_toggled().connect([radio_scroll, spin_fingers](){
		spin_fingers->set_sensitive(!radio_scroll->get_active());
	});
	
	main_win->show();
}

static Glib::ustring app_name_hr(Glib::ustring src) {
	return src == "" ? _("<unnamed>") : src;
}

static bool get_app_id_dialog_fallback(std::string& app_id) {
	Gtk::Dialog dialog("Add new app", true);
	auto x = dialog.get_content_area();
	Gtk::Label label("Please enter the app ID of the application to add:");
	Gtk::Entry app_id_entry;
	x->pack_start(label, false, false, 10);
	x->pack_start(app_id_entry, false, false, 10);
	label.show();
	app_id_entry.show();
	dialog.add_button("OK", Gtk::RESPONSE_OK);
	dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
	int r = dialog.run();
	if(r == Gtk::RESPONSE_OK) {
		app_id = app_id_entry.get_text();
		return true;
	}
	return false;
}

struct app_id_dialog_data {
	std::string& app_id;
	Gtk::Dialog* dialog;
	app_id_dialog_data(std::string& app_id_, Gtk::Dialog* dialog_):
		app_id(app_id_), dialog(dialog_) { }
};

static void app_id_cb(void* p, tl_grabber* gr) {
	auto data = (app_id_dialog_data*)p;
	char* app_id = toplevel_grabber_get_app_id(gr);
	toplevel_grabber_set_callback(gr, nullptr, nullptr);
	if(!app_id) {
		fprintf(stderr, "Cannot get app ID of selected toplevel view!\n");
		data->dialog->response(Gtk::RESPONSE_NONE);
	}
	data->app_id = std::string(app_id);
	free(app_id);
	data->dialog->response(Gtk::RESPONSE_OK);	
}

static bool get_app_id_dialog(std::string& app_id, Gtk::Window& main_win) {
	auto gdk_display = Gdk::Display::get_default();
	struct tl_grabber* gr = nullptr;
	struct wl_display* dpy = nullptr;
#ifdef GDK_WINDOWING_WAYLAND
	{
		auto tmp = gdk_display->gobj();
		if(GDK_IS_WAYLAND_DISPLAY(tmp)) {
			dpy = gdk_wayland_display_get_wl_display(gdk_display->gobj());
			gr = toplevel_grabber_new(dpy, nullptr, nullptr);
		}
	}
#endif
	if(!gr) {
		fprintf(stderr, "Cannot initiate foreign toplevel grabber interface, falling back to manual entry of app ID\n");
		return get_app_id_dialog_fallback(app_id);
	}
	
	Gtk::Dialog dialog("Add new app", true);
	dialog.set_transient_for(main_win);
	auto x = dialog.get_content_area();
	Gtk::Label label("Please select the app to add by clicking on it or click Cancel to enter the app ID manually");
	x->pack_start(label, false, false, 10);
	label.show();
	dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
	app_id_dialog_data data(app_id, &dialog);
	toplevel_grabber_set_callback(gr, app_id_cb, &data);
	int r = dialog.run();
	dialog.hide();
	wl_display_roundtrip(dpy);
	auto seat = gdk_display->get_default_seat();
	struct wl_seat* wl_seat = gdk_wayland_seat_get_wl_seat(seat->gobj());
	toplevel_grabber_activate_app(gr, "wstroke-config", wl_seat, 1);
	toplevel_grabber_free(gr);
	switch(r) {
		case Gtk::RESPONSE_OK:
			return true;
		case Gtk::RESPONSE_CANCEL:
			return get_app_id_dialog_fallback(app_id);
		default:
			return false;
	}
}

template<class ModelRef, class ColType, class KeyType>
static void set_tree_to_item(ModelRef& model, Gtk::TreeView& tv, const Gtk::TreeModelColumn<ColType>& col, const KeyType& x) {
	model->foreach([&x, &col, &tv] (const Gtk::TreeModel::Path& path, const Gtk::TreeModel::iterator& iter) {
		if ((*iter)[col] == x) {
			tv.set_cursor(path);
			return true;
		}
		return false;
	});
}

void Actions::on_add_exclude() {
	std::string name;
	if (!get_app_id_dialog(name, *main_win)) return;
	if (actions.add_exclude_app(name)) {
		Gtk::TreeModel::Row row = *(exclude_tm->append());
		row[exclude_cols.type] = name;
		Gtk::TreePath path = exclude_tm->get_path(row);
		exclude_tv->set_cursor(path);
	} else set_tree_to_item(exclude_tm, *exclude_tv, exclude_cols.type, name);
}

void Actions::on_remove_exclude() {
	Gtk::TreePath path;
	Gtk::TreeViewColumn *col;
	exclude_tv->get_cursor(path, col);
	if (path.gobj() != 0) {
		Gtk::TreeIter iter = *exclude_tm->get_iter(path);
		Glib::ustring tmp = (*iter)[exclude_cols.type];
		if (!actions.remove_exclude_app(tmp)) {
			fprintf(stderr, "Erased app from exclude list (%s) not found!\n", tmp.c_str());
		}
		exclude_tm->erase(iter);
	}
}


void Actions::load_app_list(const Gtk::TreeNodeChildren &ch, ActionListDiff<false> *actions) {
	Gtk::TreeRow row = *(apps_model->append(ch));
	row[ca.app] = app_name_hr(actions->name);
	row[ca.actions] = actions;
	for (auto i = actions->begin(); i != actions->end(); i++)
		load_app_list(row.children(), &(*i));
}

void Actions::on_cell_data_name(Gtk::CellRenderer* cell, const Gtk::TreeModel::iterator& iter) {
	bool bold = (*iter)[cols.name_bold];
	bool deactivated = (*iter)[cols.deactivated];
	Gtk::CellRendererText *renderer = dynamic_cast<Gtk::CellRendererText *>(cell);
	if (renderer)
		renderer->property_weight().set_value(bold ? 700 : 400);
	cell->property_sensitive().set_value(!deactivated);
}

void Actions::on_cell_data_type(Gtk::CellRenderer* cell, const Gtk::TreeModel::iterator& iter) {
	bool bold = (*iter)[cols.action_bold];
	bool deactivated = (*iter)[cols.deactivated];
	Gtk::CellRendererText *renderer = dynamic_cast<Gtk::CellRendererText *>(cell);
	if (renderer)
		renderer->property_weight().set_value(bold ? 700 : 400);
	cell->property_sensitive().set_value(!deactivated);
}

void Actions::on_cell_data_arg(GtkCellRenderer *cell, gchar *path) {
	Gtk::TreeModel::iterator iter = tm->get_iter(path);
	bool bold = (*iter)[cols.action_bold];
	bool deactivated = (*iter)[cols.deactivated];
	g_object_set(cell, "sensitive", !deactivated, "weight", bold ? 700 : 400, nullptr);
	CellRendererTextish *renderer = CELL_RENDERER_TEXTISH (cell);
	if (!renderer)
		return;
	Glib::ustring str = (*iter)[cols.type];
	const auto& type_info = type_info_from_name(str);
	renderer->mode = type_info.mode;
	
	if(type_info.type == Type::GLOBAL)
		cell_renderer_textish_set_items(renderer, const_cast<gchar**>(Global::types), Global::n_actions);
	else if(type_info.type == Type::VIEW)
		cell_renderer_textish_set_items(renderer, const_cast<gchar**>(View::types), View::n_actions);
}

int Actions::compare_ids(const Gtk::TreeModel::iterator &a, const Gtk::TreeModel::iterator &b) {
	unsigned int x = actions.get_stroke_order((*a)[cols.id]);
	unsigned int y = actions.get_stroke_order((*b)[cols.id]);
	if(x < y) return -1;
	if(x > y) return 1;
	return 0;
}

bool Actions::AppsStore::row_drop_possible_vfunc(const Gtk::TreeModel::Path &dest,
		const Gtk::SelectionData &selection) const {
	static bool expecting = false;
	static Gtk::TreePath expected;
	if (expecting && expected != dest)
		expecting = false;
	if (!expecting) {
		if (gtk_tree_path_get_depth((GtkTreePath *)dest.gobj()) < 2 || dest.back() != 0)
			return false;
		expected = dest;
		expected.up();
		expecting = true;
		return false;
	}
	expecting = false;
	Gtk::TreePath src;
	Glib::RefPtr<TreeModel> model;
	if (!Gtk::TreeModel::Path::get_from_selection_data(selection, model, src))
		return false;
	if (model != parent->tm)
		return false;
	Gtk::TreeIter dest_iter = parent->apps_model->get_iter(dest);
	ActionListDiff<false> *actions = dest_iter ? (*dest_iter)[parent->ca.actions] : (ActionListDiff<false> *)nullptr;
	return actions && actions != parent->action_list;
}

bool Actions::AppsStore::drag_data_received_vfunc(const Gtk::TreeModel::Path &dest, const Gtk::SelectionData &selection) {
	Gtk::TreePath src;
	Glib::RefPtr<TreeModel> model;
	if (!Gtk::TreeModel::Path::get_from_selection_data(selection, model, src))
		return false;
	if (model != parent->tm)
		return false;
	Gtk::TreeIter dest_iter = parent->apps_model->get_iter(dest);
	ActionListDiff<false> *actions = dest_iter ? (*dest_iter)[parent->ca.actions] : (ActionListDiff<false> *)nullptr;
	if (!actions || actions == parent->action_list)
		return false;
	Glib::RefPtr<Gtk::TreeSelection> sel = parent->tv.get_selection();
	if (sel->count_selected_rows() > 1) {
		/* we temporarily unset soring so as not to resort the list every
		 * time an item is removed */
		int col;
		Gtk::SortType sort;
		parent->tm->get_sort_column_id(col, sort);
		parent->tm->set_sort_column(GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, sort);
		
		std::vector<Gtk::TreePath> paths = sel->get_selected_rows();
		std::vector<Gtk::TreeRowReference> refs;
		for(const auto& x : paths)
			refs.push_back(Gtk::TreeRowReference(parent->tm, x));
		
		for(const auto& x : refs) {
			Gtk::TreeIter i = parent->tm->get_iter(x.get_path());
			stroke_id id = (*i)[parent->cols.id];
			if(parent->actions.move_stroke_to_app(parent->action_list, actions, id))
				parent->tm->erase(i);
			else parent->update_row(*i);
		}
		
		parent->tm->set_sort_column(col, sort);
	}
	else {
		auto i = parent->tm->get_iter(src);
		stroke_id src_id = (*i)[parent->cols.id];
		if(parent->actions.move_stroke_to_app(parent->action_list, actions, src_id))
			parent->tm->erase(i);
		else parent->update_row(*i);
	}
	
	// parent->update_action_list();
	parent->update_actions();
	return true;
}

bool Actions::Store::row_draggable_vfunc(const Gtk::TreeModel::Path&) const {
	int col;
	Gtk::SortType sort;
	parent->tm->get_sort_column_id(col, sort);
	if(col ==  Gtk::TreeSortable::DEFAULT_SORT_COLUMN_ID ||
		col == parent->cols.id.index()) return true;
	else return false;
}

bool Actions::Store::row_drop_possible_vfunc(const Gtk::TreeModel::Path &dest, const Gtk::SelectionData&) const {
	static bool ignore_next = false;
	if (gtk_tree_path_get_depth((GtkTreePath *)dest.gobj()) > 1) {
		// this gets triggered when the drag is over an existing row (as opposed to being "in between" rows)
		ignore_next = true;
		return false;
	}
	if (ignore_next) {
		ignore_next = false;
		return false;
	}
	return true;
}

bool Actions::Store::drag_data_received_vfunc(const Gtk::TreeModel::Path &dest, const Gtk::SelectionData &selection) {
	Gtk::TreePath src;
	Glib::RefPtr<TreeModel> model;
	if (!Gtk::TreeModel::Path::get_from_selection_data(selection, model, src))
		return false;
	if (model != parent->tm)
		return false;
	int col;
	Gtk::SortType sort;
	parent->tm->get_sort_column_id(col, sort);
	if( !(col ==  Gtk::TreeSortable::DEFAULT_SORT_COLUMN_ID ||
		col == parent->cols.id.index()) ) return false;
	
	stroke_id src_id = (*parent->tm->get_iter(src))[parent->cols.id];
	Gtk::TreeIter dest_iter = parent->tm->get_iter(dest);
	stroke_id dest_id = dest_iter ? (*dest_iter)[parent->cols.id] : 0;
	
	Glib::RefPtr<Gtk::TreeSelection> sel = parent->tv.get_selection();
	if (sel->count_selected_rows() <= 1) {
		parent->actions.move_stroke(src_id, dest_id, sort == Gtk::SORT_DESCENDING);
		(*parent->tm->get_iter(src))[parent->cols.id] = src_id;
		parent->update_actions();
	} else {
		/* note: we temporarily unset sorting so that the list is not resorted for each update */
		parent->tm->set_sort_column(GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, sort);
		std::vector<Gtk::TreePath> paths = sel->get_selected_rows();
		std::vector<stroke_id> ids;
		for(const auto& p : paths) ids.push_back((*parent->tm->get_iter(p))[parent->cols.id]);
		if(sort == Gtk::SORT_DESCENDING) parent->actions.move_strokes(ids.rbegin(), ids.rend(), dest_id, true);
		else parent->actions.move_strokes(ids.begin(), ids.end(), dest_id, false);
		parent->tm->set_sort_column(col, sort); // this will apply the new sort
		parent->update_actions();
	}
	return false;
}

void Actions::on_type_edited(const Glib::ustring &path, const Glib::ustring &new_text) {
	tv.grab_focus();
	Gtk::TreeRow row(*tm->get_iter(path));
	Type new_type = from_name(new_text);
	Type old_type = from_name(row[cols.type]);
	bool edit = true;
	if (old_type == new_type) {
		edit = editing_new;
	} else {
		row[cols.type] = new_text;
		std::unique_ptr<Action> new_action;
		if (old_type == Type::COMMAND) {
			row[cols.cmd_save] = (Glib::ustring)row[cols.arg];
		}
		if (old_type == Type::PLUGIN) {
			row[cols.plugin_action_save] = (Glib::ustring)row[cols.arg];
		}
		
		switch(new_type) {
			case Type::COMMAND:
				{
					Glib::ustring cmd_save = row[cols.cmd_save];
					if (cmd_save != "")
						edit = false;
					new_action = Command::create(cmd_save);
				}
				break;
			case Type::KEY:
				new_action = SendKey::create(0, (Gdk::ModifierType)0);
				edit = true;
				break;
			case Type::TEXT:
				new_action = SendText::create(Glib::ustring());
				edit = true;
				break;
			case Type::SCROLL:
				new_action = Scroll::create((Gdk::ModifierType)0);
				edit = false;
				break;
			case Type::IGNORE:
				new_action = Ignore::create((Gdk::ModifierType)0);
				edit = false;
				break;
			case Type::BUTTON:
				new_action = Button::create((Gdk::ModifierType)0, 0);
				edit = true;
				break;
			case Type::GLOBAL:
				new_action = Global::create(Global::Type::NONE);
				edit = true;
				break;
			case Type::VIEW:
				new_action = View::create(View::Type::NONE);
				edit = true;
				break;
			case Type::PLUGIN:
				{
					Glib::ustring cmd_save = row[cols.plugin_action_save];
					if (cmd_save != "")
						edit = false;
					new_action = Plugin::create(cmd_save);
				}
				break;
			case Type::TOUCHPAD:
				new_action = Touchpad::create(Touchpad::Type::NONE, 2, 0);
				edit = true;
				break;
		}
		action_list->set_action(row[cols.id], std::move(new_action));
		update_row(row);
		update_actions();
	}
	editing_new = false;
	if (! (new_type == Type::VIEW || new_type == Type::GLOBAL))
		focus(row[cols.id], 3, edit);
}

void Actions::on_button_delete() {
	bool show_deleted = check_show_deleted->get_active();
	unsigned int to_delete = 0, to_disable = 0;
	Glib::RefPtr<Gtk::TreeSelection> sel = tv.get_selection();
	std::vector<Gtk::TreePath> paths = sel->get_selected_rows();
	
	for(const auto& x : paths) {
		stroke_id id = (*tm->get_iter(x))[cols.id];
		if(actions.get_stroke_owner(id) == action_list) to_delete++;
		else to_disable++;
	}
	
	if(to_delete) {
		Glib::ustring str;
		if (to_delete == 1) for(const auto& x : paths) {
			auto tmp = Gtk::TreeRow(*tm->get_iter(x));
			if(actions.get_stroke_owner(tmp[cols.id]) == action_list) {
				str = Glib::ustring::compose(_("Action \"%1\" is about to be deleted."), tmp[cols.name]);
				break;
			}
		}
		else str = Glib::ustring::compose(ngettext("One action is about to be deleted",
						"%1 actions are about to be deleted", to_delete), to_delete);
		
		if(to_disable) str += Glib::ustring::compose(ngettext(" (one additional action will be disabled).",
						" (%1 additional actions will be disabled).,", to_disable), to_disable);
		else str += ".";
		
		Gtk::MessageDialog *dialog;
		widgets->get_widget("dialog_delete", dialog);
		dialog->set_message(ngettext("Delete an Action", "Delete Actions", to_delete));
		dialog->set_secondary_text(str);
		Gtk::Button *del;
		widgets->get_widget("button_delete_delete", del);

		dialog->show();
		del->grab_focus();
		bool ok = dialog->run() == 1;
		dialog->hide();
		if (!ok)
			return;
	}

	if (to_delete + to_disable == 1) {
		auto i = tm->get_iter(*paths.begin());
		stroke_id id = (*i)[cols.id];
		if(!(to_disable && show_deleted)) tm->erase(i);
		actions.remove_stroke(action_list, id);
		if(to_disable && show_deleted) update_row(*i);
	}
	else {
		/* note: we temporarily unset sorting so that the list is not resorted for each update */
		int col;
		Gtk::SortType sort;
		tm->get_sort_column_id(col, sort);
		tm->set_sort_column(GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, sort);
		
		std::vector<Gtk::TreeRowReference> refs;
		std::vector<stroke_id> ids;
		for(const auto& x : paths) {
			refs.push_back(Gtk::TreeRowReference(tm, x));
			ids.push_back((*tm->get_iter(x))[cols.id]);
		}
		auto end = std::remove_if(refs.begin(), refs.end(), [this, show_deleted](const auto& x) {
			Gtk::TreeIter i = tm->get_iter(x.get_path());
			bool remove = !show_deleted || (actions.get_stroke_owner((*i)[cols.id]) == action_list);
			if(remove) tm->erase(i);
			return show_deleted && remove;
		});
		
		actions.remove_strokes(action_list, ids.begin(), ids.end());
		
		if(show_deleted) for(auto it = refs.begin(); it != end; ++it) update_row(*tm->get_iter(it->get_path()));
		/* apply sorting to the new selection */
		tm->set_sort_column(col, sort);
	}
	if(show_deleted && to_disable) 	button_reset_actions->set_sensitive(true);
	update_actions();
	update_counts();
}

bool Actions::get_action_item(const ActionListDiff<false>* x, Gtk::TreeIter& it) {
	bool found = false;
	apps_model->foreach_iter([this, x, &it, &found] (const Gtk::TreeModel::iterator& iter) {
		if ((*iter)[ca.actions] == x) {
			it = iter;
			found = true;
			return true;
		}
		return false;
	});
	return found;
}

void Actions::on_add_app() {
	std::string name;
	if (!get_app_id_dialog(name, *main_win)) return;
	
	const ActionListDiff<false>* name_match = actions.get_action_list(name);
	if(name_match) {
		set_tree_to_item(apps_model, *apps_view, ca.actions, name_match);
		return;
	}
	
	ActionListDiff<false> *parent = action_list->app ? action_list->get_parent() : action_list;
	if (!parent || parent->app) throw std::runtime_error("Expected app group as parent node of an app!\n");
	Gtk::TreeModel::iterator tree_it;
	if(!get_action_item(parent, tree_it)) throw std::runtime_error("Expected group not found in the app list!\n");
	
	ActionListDiff<false> *child = actions.add_app(parent, name, true);
	Gtk::TreeRow row = *(apps_model->append(tree_it->children()));
	row[ca.app] = app_name_hr(name);
	row[ca.actions] = child;
	row[ca.count] = child->count_actions();
	Gtk::TreePath path = apps_model->get_path(row);
	apps_view->expand_to_path(path);
	apps_view->set_cursor(path);
	update_actions();
}

void Actions::on_remove_app() {
	if (action_list == actions.get_root())
		return;
	int size = action_list->size_rec();
	if (size) {
		Gtk::MessageDialog *dialog;
		widgets->get_widget("dialog_delete", dialog);
		Glib::ustring str = Glib::ustring::compose(_("%1 \"%2\" (containing %3 %4) is about to be deleted."),
				action_list->app ? _("The application") : _("The group"),
				action_list->name,
				size,
				ngettext("action", "actions", size));
		dialog->set_message(action_list->app ? _("Delete an Application") : _("Delete an Application Group"));
		dialog->set_secondary_text(str);
		Gtk::Button *del;
		widgets->get_widget("button_delete_delete", del);
		dialog->show();
		del->grab_focus();
		bool ok = dialog->run() == 1;
		dialog->hide();
		if (!ok) return;
	}
	actions.remove_app(action_list);
	apps_model->erase(*apps_view->get_selection()->get_selected());
	update_actions();
}

void Actions::on_add_group() {
	ActionListDiff<false> *parent = action_list;
	Glib::ustring name = _("Group");
	
	Gtk::TreeModel::iterator tree_it;
	if(!get_action_item(parent, tree_it)) throw std::runtime_error("Expected item not found in the app list!\n");
	Gtk::TreeRow row = *(apps_model->append(tree_it->children()));
	
	ActionListDiff<false> *child;
	Gtk::TreePath path;
	if(parent->app) {
		/* we convert this app to a new group and add the app as its child */
		child = actions.add_app(parent, parent->name, true);
		parent->app = false;
		parent->name = name;
		row[ca.app] = app_name_hr(child->name);
		(*tree_it)[ca.app] = name;
		path = apps_model->get_path(*tree_it);
	}
	else {
		/* parent is a group, we add a group as its child */
		child = parent->add_child(name, false);
		row[ca.app] = name;
		path = apps_model->get_path(row);
	}
	row[ca.actions] = child;
	row[ca.count] = child->count_actions();
	apps_view->expand_to_path(path);
	apps_view->set_cursor(path, *apps_view->get_column(0), true);
	update_actions();
}

void Actions::on_apps_selection_changed() {
	ActionListDiff<false> *new_action_list = actions.get_root();
	
	if (apps_view->get_selection()->count_selected_rows()) {
		Gtk::TreeIter i = apps_view->get_selection()->get_selected();
		new_action_list = (*i)[ca.actions];
	}
	button_remove_app->set_sensitive(new_action_list != actions.get_root());
	
	if (action_list != new_action_list) {
		action_list = new_action_list;
		update_action_list();
		on_selection_changed();
	}
}

void Actions::update_counts() {
	apps_model->foreach_iter([this](const Gtk::TreeIter &i) {
		(*i)[ca.count] = ((ActionListDiff<false>*)(*i)[ca.actions])->count_actions();
		return false;
	});
}

void Actions::update_action_list() {
	check_show_deleted->set_sensitive(action_list != actions.get_root());
	std::set<stroke_id> ids = action_list->get_ids(check_show_deleted->get_active());
	
	/* note: we temporarily unset sorting so that the list is not resorted for each update */
	int col;
	Gtk::SortType sort;
	tm->get_sort_column_id(col, sort);
	tm->set_sort_column(GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, sort);
	
	const Gtk::TreeNodeChildren &ch = tm->children();
	std::list<Gtk::TreeRowReference> refs;
	for(const auto& x : ch) refs.push_back(Gtk::TreeRowReference(tm, Gtk::TreePath(x)));

	for(const auto& ref : refs) {
		Gtk::TreeIter i = tm->get_iter(ref.get_path());
		Gtk::TreeRow row = *i;
		auto it = ids.find(row[cols.id]);
		if (it == ids.end()) {
			tm->erase(i);
		} else {
			ids.erase(it);
			update_row(row);
		}
	}
	for(const auto& x : ids) {
		Gtk::TreeRow row = *tm->append();
		row[cols.id] = x;
		update_row(row);
	}
	
	/* apply sorting to the new selection */
	tm->set_sort_column(col, sort);
}

void Actions::update_row(const Gtk::TreeRow& row) {
	StrokeRow si = action_list->get_info(row[cols.id]);
	row[cols.stroke] = (si.stroke && !si.stroke->trivial()) ? 
		StrokeDrawer::draw((si.stroke), STROKE_SIZE, si.stroke_overwrite ? 4.0 : 2.0) : StrokeDrawer::drawEmpty(STROKE_SIZE);
	row[cols.name] = *si.name;
	row[cols.type] = si.action ? type_info_to_name(&typeid(*si.action)) : "";
	row[cols.arg]  = get_action_label(si.action);
	row[cols.deactivated] = si.deleted;
	row[cols.name_bold] = si.name_overwrite;
	row[cols.action_bold] = si.action_overwrite;
	row[cols.action_icon] = Glib::RefPtr<Gdk::Pixbuf>();
	row[cols.cmd_path] = "";
	button_reset_actions->set_sensitive(si.stroke_overwrite || si.deleted || si.name_overwrite || si.action_overwrite);
	if(si.action) {
		const Command* tmp = dynamic_cast<const Command*>(si.action);
		if(tmp) {
			Glib::ustring cmd1 = row[cols.arg];
			row[cols.cmd_path] = cmd1;
			auto it = command_info.find(tmp->desktop_file);
			if(it != command_info.end()) {
				row[cols.arg] = it->second.name;
				row[cols.action_icon] = it->second.icon;
			}
			else {
				row[cols.arg] = _("Custom command:  ");
				auto icon_theme = Gtk::IconTheme::get_default();
				auto pb = icon_theme->load_icon("application-x-executable", 32);
				if(pb) {
					if(pb->get_width() > 32) pb = pb->scale_simple(32, 32, Gdk::INTERP_BILINEAR);
					row[cols.action_icon] = pb;
				}
			}
		}
	}
}

class Actions::OnStroke {
	Actions *parent;
	Gtk::Dialog *dialog;
	Gtk::TreeRow &row;
public:
	void run(Stroke* stroke) {
		parent->action_list->set_stroke(row[parent->cols.id], std::move(*stroke));
		parent->update_row(row);
		parent->on_selection_changed();
		parent->update_actions();
		dialog->response(0);
	}
	OnStroke(Actions *parent_, Gtk::Dialog *dialog_, Gtk::TreeRow &row_) : parent(parent_), dialog(dialog_), row(row_) {}
};

void Actions::on_stroke_editing(const char* path) {
	Gtk::TreeRow row(*tm->get_iter(path));
	on_row_activated(row);
}

void Actions::on_row_activated(Gtk::TreeRow& row) {
	Gtk::MessageDialog *dialog;
	static SRArea *drawarea = nullptr;
	widgets->get_widget("dialog_record", dialog);
	if(input_inhibitor_grab()) dialog->set_secondary_text(Glib::ustring::compose(_("The next stroke will be associated with the action \"%1\".  You can draw it in the area below, using any pointer button."), row[cols.name]));
	else dialog->set_secondary_text(Glib::ustring::compose(_("The next stroke will be associated with the action \"%1\".  You can draw it in the area below. You may need to use a different pointer button than the one normally used for gestures."), row[cols.name]));
	
	if(!drawarea) {
		drawarea = Gtk::manage(new SRArea());
		drawarea->set_size_request(600, 400);
		auto box = dialog->get_content_area();
		box->pack_start(*drawarea, true, false, 0);
	}
	
	static Gtk::Button *del = 0, *cancel = 0;
	if (!del) widgets->get_widget("button_record_delete", del);
	if (!cancel) widgets->get_widget("button_record_cancel", cancel);
	del->set_sensitive(action_list->has_stroke(row[cols.id]));

	OnStroke ps(this, dialog, row);
	sigc::connection sig = drawarea->stroke_recorded.connect(sigc::mem_fun(ps, &OnStroke::run));
	
	dialog->show_all();
	cancel->grab_focus();
	int response = dialog->run();
	dialog->hide();
	input_inhibitor_ungrab();
	sig.disconnect();
	drawarea->clear();
	if (response != 1)
		return;

	action_list->set_stroke(row[cols.id], Stroke());
	update_row(row);
	on_selection_changed();
	update_actions();
}

void Actions::on_selection_changed() {
	int n = tv.get_selection()->count_selected_rows();
	button_record->set_sensitive(n == 1);
	button_delete->set_sensitive(n >= 1);
	bool resettable = false;
	if (n) {
		std::vector<Gtk::TreePath> paths = tv.get_selection()->get_selected_rows();
		for (std::vector<Gtk::TreePath>::iterator i = paths.begin(); i != paths.end(); ++i) {
			Gtk::TreeRow row(*tm->get_iter(*i));
			if (action_list->resettable(row[cols.id])) {
				resettable = true;
				break;
			}
		}
	}
	button_reset_actions->set_sensitive(resettable);
}

void Actions::on_button_new() {
	editing_new = true;
	stroke_id before = 0;
	if (tv.get_selection()->count_selected_rows()) {
		std::vector<Gtk::TreePath> paths = tv.get_selection()->get_selected_rows();
		Gtk::TreeIter i = tm->get_iter(paths[paths.size()-1]);
		i++;
		if (i != tm->children().end()) before = (*i)[cols.id];
	}

	Gtk::TreeModel::Row row = *(tm->append());
	stroke_id id = actions.add_stroke(action_list, StrokeInfo(Command::create("")), before);
	row[cols.id] = id;
	std::string name;
	if (action_list != actions.get_root())
		name = action_list->name + " ";
	name += Glib::ustring::compose(_("Gesture %1"), actions.count_owned_strokes(action_list));
	action_list->set_name(id, name);

	update_row(row);
	focus(id, 1, true);
	update_actions();
	update_counts();
}

void Actions::focus(stroke_id id, int col, bool edit) {
	editing = false;
	Gtk::TreeViewColumn *col1 = tv.get_column(col);
	Glib::signal_idle().connect([this, id, col1, edit] () {
		if (!editing) {
			Gtk::TreeModel::Children chs = tm->children();
			for (Gtk::TreeIter i = chs.begin(); i != chs.end(); ++i)
				if (id == (*i)[cols.id]) {
					tv.set_cursor(Gtk::TreePath(*i), *col1, edit);
				}
		}
		return false;
	});
}

void Actions::on_name_edited(const Glib::ustring& path, const Glib::ustring& new_text) {
	Gtk::TreeRow row(*tm->get_iter(path));
	StrokeRow si = action_list->get_info(row[cols.id], false);
	if(new_text != *si.name) {
		action_list->set_name(row[cols.id], new_text);
		update_actions();
		update_row(row);
	}
	focus(row[cols.id], 2, editing_new);
}

void Actions::on_text_edited(const gchar *path, const gchar *new_text) {
	Gtk::TreeRow row(*tm->get_iter(path));
	Type type = from_name(row[cols.type]);
	bool changed = true;
	const Action* action = action_list->get_info(row[cols.id]).action;
	if (type == Type::COMMAND) {
		auto cmd = dynamic_cast<const Command*>(action);
		if(cmd && new_text != cmd->get_cmd()) action_list->set_action(row[cols.id], Command::create(new_text));
		else changed = false;
	} else if (type == Type::TEXT) {
		auto text = dynamic_cast<const SendText*>(action);
		if(text && new_text != text->get_text()) action_list->set_action(row[cols.id], SendText::create(new_text));
		else changed = false;
	} else if (type == Type::PLUGIN) {
		auto plugin = dynamic_cast<const Plugin*>(action);
		if(plugin && new_text != plugin->get_action()) action_list->set_action(row[cols.id], Plugin::create(new_text));
		else changed = false;
	}
	else return;
	if(changed) {
		update_row(row);
		update_actions();
	}
}

void Actions::on_accel_edited(const gchar *path_string, guint accel_key, GdkModifierType mods) {
	uint32_t accel_mods = KeyCodes::convert_modifier(mods);
	Gtk::TreeRow row(*tm->get_iter(path_string));
	Type type = from_name(row[cols.type]);
	if (type == Type::KEY) {
		auto send_key = SendKey::create(accel_key, accel_mods);
		Glib::ustring str = get_action_label(send_key.get());
		if (row[cols.arg] == str)
			return;
		action_list->set_action(row[cols.id], std::move(send_key));
	} else if (type == Type::SCROLL) {
		auto scroll = Scroll::create(accel_mods);
		Glib::ustring str = get_action_label(scroll.get());
		if (row[cols.arg] == str)
			return;
		action_list->set_action(row[cols.id], std::move(scroll));
	} else if (type == Type::IGNORE) {
		auto ignore = Ignore::create(accel_mods);
		Glib::ustring str = get_action_label(ignore.get());
		if (row[cols.arg] == str)
			return;
		action_list->set_action(row[cols.id], std::move(ignore));
	} else return;
	update_row(row);
	update_actions();
}

void Actions::on_combo_edited(const gchar *path_string, guint item) {
	std::unique_ptr<Action> action;
	Gtk::TreeRow row(*tm->get_iter(path_string));
	Type type = from_name(row[cols.type]);
	switch(type) {
		case Type::GLOBAL:
			action = Global::create((Global::Type)item);
			break;
		case Type::VIEW:
			action = View::create((View::Type)item);
			break;
		default:
			return;
	}
	Glib::ustring str = get_action_label(action.get());
	if (row[cols.arg] == str)
		return;
	action_list->set_action(row[cols.id], std::move(action));
	update_row(row);
	update_actions();
}

void Actions::on_arg_editing_started(G_GNUC_UNUSED GtkCellEditable *editable, G_GNUC_UNUSED const gchar *path) {
	tv.grab_focus();
	Gtk::TreeRow row(*tm->get_iter(path));
	Type type = from_name(row[cols.type]);
	if(type == Type::COMMAND) {
		if(chooser.run(row[cols.name])) {
			auto icon_theme = Gtk::IconTheme::get_default();
			if(chooser.custom_res) {
				action_list->set_action(row[cols.id], Command::create(chooser.res_cmdline));
				update_row(row);
				update_actions();
			}
			else {
				auto selected_app_desktop = dynamic_cast<Gio::DesktopAppInfo*>(chooser.res_app.get());
				std::string desktop = selected_app_desktop ? selected_app_desktop->get_filename() : std::string();
				if(!desktop.empty()) {
					auto cmd = Command::create(chooser.res_cmdline, desktop);
					CommandInfo info;
					info.name = selected_app_desktop->get_name();
					auto icon_theme = Gtk::IconTheme::get_default();
					auto icon_info = icon_theme->lookup_icon(selected_app_desktop->get_icon(), 32, Gtk::ICON_LOOKUP_FORCE_SIZE);
					auto pb = icon_info.load_icon();
					if(pb) info.icon = pb;
					command_info[desktop] = std::move(info);
					action_list->set_action(row[cols.id], std::move(cmd));
				}
				else action_list->set_action(row[cols.id], Command::create(chooser.res_cmdline));
				update_row(row);
				update_actions();
			}
		}
	}
	if (type == Type::BUTTON) {
		Button* bt = dynamic_cast<Button*>(action_list->get_stroke_action(row[cols.id]));
		Button tmp;
		SelectButton sb(bt ? *bt : tmp, widgets);
		if (!sb.run())
			return;
		action_list->set_action(row[cols.id], Button::create(Gdk::ModifierType(sb.state), sb.button));
		update_row(row);
		update_actions();
	}
	if (type == Type::TOUCHPAD) {
		Touchpad* tp = dynamic_cast<Touchpad*>(action_list->get_stroke_action(row[cols.id]));
		Touchpad tmp;
		SelectTouchpad stp(tp ? *tp : tmp, widgets, row[cols.name]);
		if (!stp.run()) return;
		auto t = stp.get_type();
		action_list->set_action(row[cols.id], Touchpad::create(t, (t == Touchpad::Type::SCROLL) ? 2 : stp.get_fingers(), Gdk::ModifierType(stp.state)));
		update_row(row);
		update_actions();
	}
}

void Actions::load_command_infos_r(ActionListDiff<false>& x) {
	x.visit_all_actions([this] (const Action* a) {
		const Command* c = dynamic_cast<const Command*>(a);
		if(c && !c->desktop_file.empty() && !command_info.count(c->desktop_file)) {
			auto dinfo = Gio::DesktopAppInfo::create_from_filename(c->desktop_file);
			if(dinfo) {
				CommandInfo info;
				info.name = dinfo->get_name();
				auto icon_theme = Gtk::IconTheme::get_default();
				auto icon_info = icon_theme->lookup_icon(dinfo->get_icon(), 32, Gtk::ICON_LOOKUP_FORCE_SIZE);
				auto pb = icon_info.load_icon();
				if(pb) info.icon = pb;
				command_info[c->desktop_file] = std::move(info);
			}
		}
	});
	for(auto& y : x) load_command_infos_r(y);
}

void Actions::load_command_infos() {
	// go through all Command actions and load the name and icon of the command if possible
	load_command_infos_r(*actions.get_root());
}

void Actions::save_actions() {
	if(save_error) return;
	try {
		actions.write(config_dir + ActionDB::wstroke_actions_versions[0]);
	} catch (std::exception &e) {
		save_error = true;
		fprintf(stderr, _("Error: Couldn't save action database: %s.\n"), e.what());
		auto error_message = Glib::ustring::compose(_( "Couldn't save %1.  Your changes will be lost.  "
				"Make sure that \"%2\" is a directory and that you have write access to it.  "
				"You can change the configuration directory "
				"using the XDG_CONFIG_HOME environment variable."), _("actions"), config_dir);
				
		Gtk::MessageDialog dialog(error_message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
		if(main_win->get_mapped()) dialog.set_transient_for(*main_win);
		dialog.show();
		dialog.run();
		if(!exiting) {
			auto app = Gtk::Application::get_default();
			if(app) app->quit();
			else Gtk::Main::quit();
		}
	}
}

void Actions::try_import() {
	fprintf(stderr, "Import target: %s\n", import_file_chooser->get_filename().c_str());
	ActionDB tmp_db;
	bool read = false;
	try {
		read = tmp_db.read(import_file_chooser->get_filename());
	}
	catch(std::exception& e) {
		fprintf(stderr, "%s\n", e.what());
		read = false;
	}
	
	if(read) {
		tm->clear();
		apps_model->clear();
		
		if(import_add->get_active()) actions.merge_actions(std::move(tmp_db));
		else actions.overwrite_actions(std::move(tmp_db));
		command_info.clear(); // we reload names and icons in case the list of installed apps changed
		load_command_infos();
		
		update_action_list();
		load_app_list(apps_model->children(), actions.get_root());
		update_counts();
	}
	
	import_dialog->close();
	save_actions();
}

void Actions::try_export() {
	auto fc = Gtk::FileChooserNative::create(_("Save strokes"), *main_win, Gtk::FileChooserAction::FILE_CHOOSER_ACTION_SAVE, _("Save"), _("Cancel"));
	if(fc->run() == Gtk::RESPONSE_ACCEPT) {
		fprintf(stderr, "Trying to save actions to %s\n", fc->get_filename().c_str());
		try {
			actions.write(fc->get_filename());
		}
		catch(std::exception& e) {
			fprintf(stderr, "%s\n", e.what());
		}
	}
}

SelectButton::SelectButton(const Button& bt, Glib::RefPtr<Gtk::Builder>& widgets_) : widgets(widgets_) {
	widgets->get_widget("dialog_select", dialog);
	dialog->set_message(_("Select a Mouse or Pen Button"));
	dialog->set_secondary_text(_("Please place your mouse or pen in the box below and press the button that you want to select.  You can also hold down additional modifiers."));
	widgets->get_widget("eventbox", eventbox);
	widgets->get_widget("toggle_shift", toggle_shift);
	widgets->get_widget("toggle_alt", toggle_alt);
	widgets->get_widget("toggle_control", toggle_control);
	widgets->get_widget("toggle_super", toggle_super);
	Gtk::Bin *box_button;
	widgets->get_widget("box_button", box_button);
	select_button = dynamic_cast<Gtk::ComboBoxText *>(box_button->get_child());
	if (!select_button) {
		select_button = Gtk::manage(new Gtk::ComboBoxText);
		box_button->add(*select_button);
		for (int i = 1; i <= 12; i++)
			select_button->append(Glib::ustring::compose(_("Button %1"), i));
		select_button->show();
	}
	select_button->set_active(bt.get_button()-1);
	toggle_shift->set_active(bt.get_button() && (bt.get_mods() & GDK_SHIFT_MASK));
	toggle_control->set_active(bt.get_button() && (bt.get_mods() & GDK_CONTROL_MASK));
	toggle_alt->set_active(bt.get_button() && (bt.get_mods() & GDK_MOD1_MASK));
	toggle_super->set_active(bt.get_button() && (bt.get_mods() & GDK_SUPER_MASK));
	
	if (!eventbox->get_children().size()) {
		eventbox->set_events(Gdk::BUTTON_PRESS_MASK);

		Glib::RefPtr<Gdk::Pixbuf> pb = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB,true,8,400,200);
		pb->fill(0x808080ff);
		
		Gtk::Image& box = *Gtk::manage(new Gtk::Image(pb));
		eventbox->add(box);
		box.show();
	}
	handler = eventbox->signal_button_press_event().connect(sigc::mem_fun(*this, &SelectButton::on_button_press));
}

SelectButton::~SelectButton() {
	handler.disconnect();
}

bool SelectButton::run() {
	dialog->show();
	Gtk::Button *select_ok;
	widgets->get_widget("select_ok", select_ok);
	select_ok->grab_focus();
	int response;
	do {
		response = dialog->run();
	} while (!response);
	dialog->hide();
	switch (response) {
		case 1: // Okay
			button = select_button->get_active_row_number() + 1;
			if (!button)
				return false;
			state = 0;
			if (toggle_shift->get_active())
				state |= GDK_SHIFT_MASK;
			if (toggle_control->get_active())
				state |= GDK_CONTROL_MASK;
			if (toggle_alt->get_active())
				state |= GDK_MOD1_MASK;
			if (toggle_super->get_active())
				state |= GDK_SUPER_MASK;
			return true;
		case 2: // Default
			button = 0;
			state = 0;
			return true;
		case 3: // Click - all the work has already been done
			return true;
		case -1: // Cancel
		default: // Something went wrong
			return false;
	}
}

bool SelectButton::on_button_press(GdkEventButton *ev) {
	button = ev->button;
	state  = ev->state;
	if (state & Mod4Mask)
		state |= GDK_SUPER_MASK;
	state &= gtk_accelerator_get_default_mod_mask();
	dialog->response(3);
	return true;
}


SelectTouchpad::SelectTouchpad(const Touchpad& bt, Glib::RefPtr<Gtk::Builder>& widgets_, const Glib::ustring& action_name) : widgets(widgets_) {
	widgets->get_widget("dialog_touchpad", dialog);
	Gtk::HeaderBar *header;
	widgets->get_widget("header_touchpad", header);
	widgets->get_widget("touchpad_toggle_shift", toggle_shift);
	widgets->get_widget("touchpad_toggle_alt", toggle_alt);
	widgets->get_widget("touchpad_toggle_control", toggle_control);
	widgets->get_widget("touchpad_toggle_super", toggle_super);
	widgets->get_widget("touchpad_type_scroll", radio_scroll);
	widgets->get_widget("touchpad_type_swipe", radio_swipe);
	widgets->get_widget("touchpad_type_pinch", radio_pinch);
	widgets->get_widget("touchpad_fingers", spin_fingers);
	
	/* Note: a GtkAdjustment is not a GtkWidget, so the get_widget function
	 * cannot be used to retrieve it. Also, widgets holds a reference to it,
	 * so we don't need to keep the RefPtr. */
	Glib::RefPtr<Glib::Object> adj_obj = widgets->get_object("touchpad_fingers_adj");
	adj_fingers = dynamic_cast<Gtk::Adjustment*>(adj_obj.get());
	if(!adj_fingers) throw std::runtime_error("Error loading UI!\n");
	
	Glib::ustring str = Glib::ustring::compose(_("Set properties for action %1"), action_name);
	header->set_subtitle(str);
	
	toggle_shift->set_active(bt.get_mods() & GDK_SHIFT_MASK);
	toggle_control->set_active(bt.get_mods() & GDK_CONTROL_MASK);
	toggle_alt->set_active(bt.get_mods() & GDK_MOD1_MASK);
	toggle_super->set_active(bt.get_mods() & GDK_SUPER_MASK);
	
	adj_fingers->set_value(bt.fingers);
	
	switch(bt.get_action_type()) {
		// note: NONE is the default when a new action is added
		case Touchpad::Type::NONE:
		case Touchpad::Type::SCROLL:
			radio_scroll->set_active(true);
			spin_fingers->set_sensitive(false);
			break;
		case Touchpad::Type::PINCH:
			radio_pinch->set_active(true);
			spin_fingers->set_sensitive(true);
			break;
		case Touchpad::Type::SWIPE:
			radio_swipe->set_active(true);
			spin_fingers->set_sensitive(true);
			break;
	}
}

bool SelectTouchpad::run() {
	dialog->show();
	Gtk::Button *select_ok;
	widgets->get_widget("touchpad_select_ok", select_ok);
	select_ok->grab_focus();
	int response;
	do {
		response = dialog->run();
	} while (!response);
	dialog->hide();
	switch (response) {
		case Gtk::RESPONSE_OK: // Okay
			state = 0;
			if (toggle_shift->get_active())
				state |= GDK_SHIFT_MASK;
			if (toggle_control->get_active())
				state |= GDK_CONTROL_MASK;
			if (toggle_alt->get_active())
				state |= GDK_MOD1_MASK;
			if (toggle_super->get_active())
				state |= GDK_SUPER_MASK;
			return true;
		case Gtk::RESPONSE_CANCEL: // Cancel
		default: // Something went wrong
			return false;
	}
}

Touchpad::Type SelectTouchpad::get_type() const {
	if(radio_scroll->get_active()) return Touchpad::Type::SCROLL;
	if(radio_swipe->get_active()) return Touchpad::Type::SWIPE;
	if(radio_pinch->get_active()) return Touchpad::Type::PINCH;
	return Touchpad::Type::NONE;
}

uint32_t SelectTouchpad::get_fingers() const {
	uint32_t tmp = (uint32_t)adj_fingers->get_value();
	return std::max(tmp, 2U);
}

struct ActionLabel : public ActionVisitor {
	protected:
		Glib::ustring label;
	public:
		void visit(const Command* action) override {
			label = action->get_cmd();
		}
		void visit(const SendKey* action) override {
			uint32_t mods = action->get_mods();
			mods = KeyCodes::add_virtual_modifiers(mods);
			uint32_t keysym = KeyCodes::convert_keycode(action->get_key());
			label = Gtk::AccelGroup::get_label(keysym, static_cast<Gdk::ModifierType>(mods));
		}
		void visit(const SendText* action) override {
			label = action->get_text();
		}
		void visit(const Scroll* action) override {
			uint32_t mods = action->get_mods();
			mods = KeyCodes::add_virtual_modifiers(mods);
			if(mods) label = Gtk::AccelGroup::get_label(0, static_cast<Gdk::ModifierType>(mods)) + _(" + Scroll");
			else label = _("Scroll");
		}
		void visit(const Ignore* action) override {
			uint32_t mods = action->get_mods();
			mods = KeyCodes::add_virtual_modifiers(mods);
			if (mods) label = Gtk::AccelGroup::get_label(0, static_cast<Gdk::ModifierType>(mods));
			else label = _("Ignore");
		}
		void visit(const Button* action) override {
			label = Gtk::AccelGroup::get_label(0, (Gdk::ModifierType)action->get_mods());
			label += Glib::ustring::compose(_(" + Button %1"), action->get_button());
		}
		void visit(const Global* action) override {
			auto t = action->get_action_type();
			label = Global::get_type_str(t);
		}
		void visit(const View* action) override {
			auto t = action->get_action_type();
			label = View::get_type_str(t);
		}
		void visit(const Touchpad* action) override {
			auto t = action->get_action_type();
			if(t == Touchpad::Type::NONE) {
				label = "None";
				return;
			}
			
			uint32_t mods = action->get_mods();
			mods = KeyCodes::add_virtual_modifiers(mods);
			if(mods) label = Gtk::AccelGroup::get_label(0, static_cast<Gdk::ModifierType>(mods)) + " + ";
			else label = "";
			
			if(t == Touchpad::Type::SCROLL) {
				label += "Scroll";
				return;
			}
			
			/* Pinch or swipe */
			if(t == Touchpad::Type::PINCH) label += Glib::ustring::compose(_(" %1 finger pinch"), action->fingers);
			else label += Glib::ustring::compose(_(" %1 finger swipe"), action->fingers);
		}
		void visit(const Plugin* action) override {
			label = action->get_action();
		}
		
		const Glib::ustring get_label() const { return label; }
};

static Glib::ustring get_action_label(const Action* action) {
	if(!action) return "";
	ActionLabel label;
	action->visit(&label);
	return label.get_label();
}

const char* Global::types[Global::n_actions] = { N_("None"), N_("Show expo"), N_("Scale (current workspace)"), N_("Scale (all workspaces)"),
	N_("Configure gestures"), N_("Toggle show desktop") };

const char* Global::get_type_str(Type type) { return types[static_cast<int>(type)]; }

const char* View::types[View::n_actions] = { N_("None"), N_("Close"), N_("Toggle maximized"), N_("Move"), N_("Resize"), N_("Minimize"),
	N_("Toggle fullscreen"), N_("Send to back"), N_("Toggle always on top"), N_("Toggle sticky") };

const char* View::get_type_str(Type type) { return types[static_cast<int>(type)]; }


