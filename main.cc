/*
 * main.cc
 * 
 * Copyright 2020 Daniel Kondor <kondor.dani@gmail.com>
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

Glib::RefPtr<Gtk::Builder> widgets;

static void error_dialog(const Glib::ustring &text) {
	Gtk::MessageDialog dialog(text, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
	dialog.show();
	dialog.run();
}


int main(int argc, char **argv)
{
	std::string config_dir = getenv("HOME");
	std::string old_config_dir = config_dir + "/.easystroke/";
	config_dir += "/.config/wstroke";
	
	auto app = Gtk::Application::create(argc, argv, "org.wstroke.config");
	
	// ensure that config dir exists
	std::error_code ec;
	if(std::filesystem::exists(config_dir, ec)) {
		if(!std::filesystem::is_directory(config_dir, ec)) {
			error_dialog(Glib::ustring::compose(_( "Path for config files (%1) is not a directory! "
				"Cannot store configuration. "
				"You can change the configuration directory "
				"using the -c or --config-dir command line options."
				), config_dir));
			return 1;
		}
	}
	else {
		if(!std::filesystem::create_directories(config_dir, ec)) {
			error_dialog(Glib::ustring::compose(_( "Cannot create configuration directory \"%1\", cannot store the configuration. "
				"You can change the configuration directory "
				"using the -c or --config-dir command line options."
				), config_dir));
			return 1;
		}
	}
	config_dir += '/';
	
	widgets = Gtk::Builder::create_from_resource("/easystroke/gui.glade");
	
	ActionDB actions_db;
	bool config_read = actions_db.read(config_dir);
	if(!config_read) actions_db.read(old_config_dir);
	
	Actions actions(widgets, actions_db);
	
	Gtk::Window* win = nullptr;
	widgets->get_widget("main", win);
	
	app->run(*win, argc, argv);
	try {
		actions_db.write(config_dir);
	} catch (std::exception &e) {
		printf(_("Error: Couldn't save action database: %s.\n"), e.what());
		// good_state = false;
		error_dialog(Glib::ustring::compose(_( "Couldn't save %1.  Your changes will be lost.  "
				"Make sure that \"%2\" is a directory and that you have write access to it.  "
				"You can change the configuration directory "
				"using the -c or --config-dir command line options."), _("actions"), config_dir));
	}
	
	
	delete win;
	return 0;
}

