/*
 * main.cc
 * 
 * Copyright 2020-2023 Daniel Kondor <kondor.dani@gmail.com>
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


#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gtkmm.h>
#include <gtkmm/builder.h>
#include <glibmm/i18n.h>
#include <string>
#include <filesystem>
#include <random>
#include "actions.h"
#include "actiondb.h"
#include "ecres.h"
#include "convert_keycodes.h"
#include "input_inhibitor.h"
#include "config.h"

static void error_dialog(const Glib::ustring &text) {
	Gtk::MessageDialog dialog(text, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
	dialog.show();
	dialog.run();
}

/* Display a dialog with an error text if the configuration cannot be read. */
bool config_error_dialog(const Glib::ustring& fn, const Glib::ustring& err, Gtk::Builder* widgets) {
	std::unique_ptr<Gtk::Dialog> dialog;
	{
		Gtk::Dialog* tmp;
		widgets->get_widget("dialog_config_error", tmp);
		dialog.reset(tmp);
	}
	
	Glib::ustring msg = Glib::ustring::compose(_("The gesture configuration file \"%1\" exists but cannot be read. The following error was encountered:"), fn);
	Gtk::Label* label;
	Gtk::TextView* tv;
	widgets->get_widget("label_config_error", label);
	widgets->get_widget("textview_config_error", tv);
	tv->get_buffer()->set_text(err);
	label->set_text(msg);
	dialog->show();
	return (dialog->run() == 1); // note: response == 1 means that user clicked on the "Overwrite" button
}


void startup(Gtk::Application* app, Actions** p_actions)
{
	char* xdg_config = getenv("XDG_CONFIG_HOME");
	std::string home_dir = getenv("HOME");
	std::string old_config_dir = home_dir + "/.easystroke/";
	std::string config_dir = xdg_config ? std::string(xdg_config) + "/wstroke/" :
		home_dir + "/.config/wstroke/";
	
	// ensure that config dir exists
	std::error_code ec;
	if(std::filesystem::exists(config_dir, ec)) {
		if(!std::filesystem::is_directory(config_dir, ec)) {
			error_dialog(Glib::ustring::compose(_( "Path for config files (%1) is not a directory! "
				"Cannot store configuration. "
				"You can change the configuration directory "
				"using the XDG_CONFIG_HOME environment variable."
				), config_dir));
			return; // note: not creating a main window will result in automatically exiting
		}
	}
	else {
		if(!std::filesystem::create_directories(config_dir, ec)) {
			error_dialog(Glib::ustring::compose(_( "Cannot create configuration directory \"%1\"! "
				"Cannot store the configuration. "
				"You can change the configuration directory "
				"using the XDG_CONFIG_HOME environment variable."
				), config_dir));
			return; // note: not creating a main window will result in automatically exiting
		}
	}
	
	Glib::RefPtr<Gtk::Builder> widgets = Gtk::Builder::create_from_resource("/easystroke/gui.glade");
	auto actions = new Actions(config_dir, widgets);
	*p_actions = actions;
	ActionDB& actions_db = actions->actions;
	KeyCodes::init();
	bool config_read;
	std::string config_err_msg;
	std::string easystroke_convert_msg;
	std::string keycode_err_msg;
	
	for(const char* const * x = ActionDB::wstroke_actions_versions; *x; ++x) {
		std::string fn = config_dir + *x;
		try {
			config_read = actions_db.read(fn);
		}
		catch(std::exception& e) {
			fprintf(stderr, "%s\n", e.what());
			config_read = false;
			actions_db.clear();
			
			if(x == ActionDB::wstroke_actions_versions) {
				/* In this case, the error is with reading the current config
				 * (which would be overwritten by us). Signal an error to the user */
				if(!config_error_dialog(fn, e.what(), widgets.get())) return;
				
				/* move the configuration file -- try to assign a new filename
				 * in a naive way (we assume that there is no gain from TOCTOU
				 * attacks here :) */
				std::string new_fn = fn;
				new_fn += ".bak";
				if(std::filesystem::exists(new_fn, ec)) {
					std::default_random_engine rng(time(0));
					std::uniform_int_distribution<unsigned int> dd(1, 999999);
					new_fn += "-";
					while(true) {
						std::string tmp;
						tmp = new_fn + std::to_string(dd(rng));
						if(!std::filesystem::exists(tmp, ec)) {
							new_fn = tmp;
							break;
						}
					}
				}
				rename(fn.c_str(), new_fn.c_str());
				fprintf(stderr, "Moved unreadable config file to new location: %s\n", new_fn.c_str());
				config_err_msg = "Created a backup of the previous, unreadable config file here:\n" + new_fn;
			}
		}
		if(config_read) break;
	}
	if(!config_read) {
		if(std::filesystem::exists(old_config_dir, ec) && std::filesystem::is_directory(old_config_dir, ec)) {
			KeyCodes::keycode_errors = 0;
			for(const char* const * x = ActionDB::easystroke_actions_versions; *x; ++x) {
				std::string fn = old_config_dir + *x;
				try {
					config_read = actions_db.read(fn);
				}
				catch(std::exception& e) {
					fprintf(stderr, "%s\n", e.what());
					config_read = false;
					actions_db.clear();
				}
				if(config_read) {
					easystroke_convert_msg = "Imported gestures from Easystroke's configuration:\n" + fn;
					easystroke_convert_msg += "\nPlease check that all actions were interpreted correctly.";
					break;
				}
			}
		}
		if(!config_read) {
			try {
				config_read = actions_db.read(std::string(DATA_DIR) + "/" + ActionDB::wstroke_actions_versions[0]);
			}
			catch(std::exception& e) {
				fprintf(stderr, "%s\n", e.what());
			}
		}
	}
	if(KeyCodes::keycode_errors) keycode_err_msg =  _("Could not convert some keycodes. "
				"Some Key actions have missing values");
	
	Gtk::Dialog* d = nullptr;
	if(!(keycode_err_msg.empty() && easystroke_convert_msg.empty() && config_err_msg.empty())) {
		std::string text;
		if(!config_err_msg.empty()) text += (config_err_msg + "\n\n");
		if(!easystroke_convert_msg.empty()) text += (easystroke_convert_msg + "\n\n");
		if(!keycode_err_msg.empty()) text += (keycode_err_msg + "\n\n");
		d = new Gtk::MessageDialog(text, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
	}
	
	if(!input_inhibitor_init())
		fprintf(stderr, _("Could not initialize keyboard grabber interface. Assigning key combinations might not work.\n"));
	
	actions->startup(app, d);
}

int main(int argc, char **argv) {
	Actions* actions = nullptr;
	auto app = Gtk::Application::create(argc, argv, "org.wstroke.config");
	app->signal_startup().connect([&app, &actions]() { startup(app.get(), &actions); });
	app->signal_activate().connect([&actions]() { if(actions) actions->get_main_win()->present(); });
	int ret = app->run();
	if(actions) {
		actions->exit();
		delete actions;
	}
	return ret;
}

