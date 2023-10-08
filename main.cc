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


int main(int argc, char **argv)
{
	char* xdg_config = getenv("XDG_CONFIG_HOME");
	std::string home_dir = getenv("HOME");
	std::string old_config_dir = home_dir + "/.easystroke/";
	std::string config_dir = xdg_config ? std::string(xdg_config) + "/wstroke/" :
		home_dir + "/.config/wstroke/";
	
	auto app = Gtk::Application::create(argc, argv, "org.wstroke.config");
	
	KeyCodes::init();
	
	// ensure that config dir exists
	std::error_code ec;
	if(std::filesystem::exists(config_dir, ec)) {
		if(!std::filesystem::is_directory(config_dir, ec)) {
			error_dialog(Glib::ustring::compose(_( "Path for config files (%1) is not a directory! "
				"Cannot store configuration. "
				"You can change the configuration directory "
				"using the XDG_CONFIG_HOME environment variable."
				), config_dir));
			return 1;
		}
	}
	else {
		if(!std::filesystem::create_directories(config_dir, ec)) {
			error_dialog(Glib::ustring::compose(_( "Cannot create configuration directory \"%1\"! "
				"Cannot store the configuration. "
				"You can change the configuration directory "
				"using the XDG_CONFIG_HOME environment variable."
				), config_dir));
			return 1;
		}
	}
	
	ActionDB actions_db;
	bool config_read;
	try {
		config_read = actions_db.read(config_dir + ActionDB::current_actions_fn);
	}
	catch(std::exception& e) {
		fprintf(stderr, "%s\n", e.what());
		config_read = false;
	}
	if(!config_read) {
		if(std::filesystem::exists(old_config_dir, ec) && std::filesystem::is_directory(old_config_dir, ec)) {
			KeyCodes::keycode_errors = 0;
			for(const char* const * x = ActionDB::easystroke_actions_versions; *x; ++x) {
				try {
					config_read = actions_db.read(old_config_dir + *x);
				}
				catch(std::exception& e) {
					fprintf(stderr, "%s\n", e.what());
					config_read = false;
				}
			}
		}
		if(!config_read) {
			try {
				config_read = actions_db.read(std::string(DATA_DIR) + "/" + ActionDB::current_actions_fn);
			}
			catch(std::exception& e) {
				fprintf(stderr, "%s\n", e.what());
			}
		}
	}
	if(KeyCodes::keycode_errors) error_dialog(_("Could not convert some keycodes. "
				"Some Key actions have missing values"));
	
	
	Actions actions(actions_db, config_dir);
	
	if(!input_inhibitor_init())
		fprintf(stderr, _("Could not initialize keyboard grabber interface. Assigning key combinations might not work.\n"));
	
	app->run(*actions.get_main_win(), argc, argv);
	actions.exit();
	
	return 0;
}

