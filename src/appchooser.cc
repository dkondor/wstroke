/*
 * Copyright (c) 2023, Daniel Kondor <kondor.dani@gmail.com>
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


#include "appchooser.h"
#include <glibmm/i18n.h>

void AppChooser::update_apps() {
	bool tmp;
	mutex.lock();
	tmp = thread_running;
	if(tmp) more_work = true;
	mutex.unlock();
	
	if(!tmp) {
		if(thread.joinable()) thread.join();
		thread_running = true;
		more_work = false;
		thread = std::thread(&AppChooser::thread_func, this);
	}
	update_pending = false;
}

void AppChooser::thread_func() {
	while(true) {
		if(exit_request.load()) break;
		
		AppContent* tmp = new AppContent();
		tmp->apps = Gio::AppInfo::get_all();
		if(exit_request.load()) {
			delete tmp;
			break;
		}
		
		auto it = std::remove_if(tmp->apps.begin(), tmp->apps.end(), [](const Glib::RefPtr<Gio::AppInfo>& p) { return !p->should_show(); });
		tmp->apps.erase(it, tmp->apps.end());
		
		tmp->flowbox.reset(new Gtk::FlowBox());
		
		for(auto& a : tmp->apps) {
			if(exit_request.load()) break;
			auto box = new Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL);
			auto image = new Gtk::Image(a->get_icon(), Gtk::IconSize(Gtk::ICON_SIZE_DIALOG));
			image->set_pixel_size(48);
			box->add(*image);
			const std::string& name = a->get_name();
			auto label = new Gtk::Label(name.length() > 23 ? name.substr(0, 20) + "..." : name);
			box->add(*label);
			tmp->flowbox->add(*box);
			tmp->app_buttons[box] = a;
		}
		
		std::lock_guard<std::mutex> lock(mutex);
		tmp = apps_pending.exchange(tmp);
		if(tmp) delete tmp;
		if(!more_work) {
			thread_running = false;
			break;
		}
		more_work = false;
	}
}


void on_apps_changed(GAppInfoMonitor*, void* p) {
	AppChooser* chooser = (AppChooser*)p;
	if(chooser->update_pending) return;
	chooser->update_pending = true;
	
	Glib::signal_timeout().connect_seconds_once([chooser](){ chooser->update_apps(); }, 4);
}

void AppChooser::startup() {
	update_apps();
	monitor = g_app_info_monitor_get();
	g_signal_connect(monitor, "changed", G_CALLBACK(on_apps_changed), this);
	
	/* basic setup for the GUI */
	widgets->get_widget("dialog_appchooser", dialog);
	widgets->get_widget("header_appchooser", header);
	widgets->get_widget("entry_appchooser", entry);
	widgets->get_widget("checkbutton_appchooser", cb);
	widgets->get_widget("scrolledwindow_appchooser", sw);
	widgets->get_widget("appchooser_ok", select_ok);
	
	cb->signal_toggled().connect([this]() {
		entry->set_sensitive(cb->get_active());
	});
}


bool AppChooser::run(const Glib::ustring& gesture_name) {
	if(!apps && !apps_pending) thread.join(); // in this case, the worker thread should be running
	
	AppContent* tmp = nullptr;
	tmp = apps_pending.exchange(tmp);
	if(tmp) {
		sw->remove();
		apps.reset(tmp);
		apps->flowbox->signal_child_activated().connect([this](Gtk::FlowBoxChild*) {
			dialog->response(Gtk::RESPONSE_OK);
		});
		apps->flowbox->set_valign(Gtk::ALIGN_START);
		apps->flowbox->set_homogeneous(true);
		apps->flowbox->set_activate_on_single_click(false);
		sw->add(*apps->flowbox);
	}
	
	if(!apps) return false;
	
	cb->set_active(false);
	entry->set_sensitive(false);
	select_ok->grab_focus();
	Glib::ustring str = Glib::ustring::compose(_("Choose app to run for gesture %1"), gesture_name);
	header->set_subtitle(str);
		
	dialog->show_all();
	
	auto x = dialog->run();
	dialog->hide();
	
	if(x == Gtk::RESPONSE_OK) {
		if(cb->get_active()) {
			res_cmdline = entry->get_text();
			custom_res = true;
			res_app.reset();
			return true;
		}
		else {
			custom_res = false;
			auto tmp = apps->flowbox->get_selected_children();
			if(tmp.size()) {
				auto selected = tmp.front();
				Gtk::Box* box = dynamic_cast<Gtk::Box*>(selected->get_child());
				if(box) {
					res_app = apps->app_buttons.at(box);
					res_cmdline = res_app->get_executable();
					if(res_cmdline == "env" || res_cmdline == "steam") res_cmdline = res_app->get_commandline();
					return true;
				}
			}
		}
	}
	res_app.reset();
	res_cmdline = "";
	custom_res = false;
	return false;
}



AppChooser::~AppChooser() {
	if(monitor) g_object_unref(monitor);
	exit_request.store(true);
	if(thread.joinable()) thread.join();
	AppContent* tmp = apps_pending;
	if(tmp) delete tmp;
	sw->remove();
}


