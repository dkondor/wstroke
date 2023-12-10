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

AppChooser::AppBox::AppBox(Glib::RefPtr<Gio::AppInfo>& app_) : Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL), app(app_) {
	auto image = new Gtk::Image(app->get_icon(), Gtk::IconSize(Gtk::ICON_SIZE_DIALOG));
	image->set_pixel_size(48);
	this->add(*image);
	const std::string& name = app->get_name();
	auto label = new Gtk::Label(name.length() > 23 ? name.substr(0, 20) + "..." : name);
	this->add(*label);
	name_lower = Glib::ustring(app->get_name()).lowercase();
}

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
			auto box = new AppBox(a);
			tmp->flowbox->add(*box);
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
	widgets->get_widget("searchentry_appchooser", searchentry);
	
	cb->signal_toggled().connect([this]() {
		entry->set_sensitive(cb->get_active());
	});
	
	searchentry->signal_search_changed().connect([this]() {
		filter_lower = searchentry->get_text().lowercase();
		if(apps) apps->flowbox->invalidate_filter();
	});
	searchentry->signal_stop_search().connect([this]() {
		searchentry->set_text(Glib::ustring());
	});
}

int AppChooser::apps_sort(const Gtk::FlowBoxChild* x, const Gtk::FlowBoxChild* y) {
	const AppBox* a = dynamic_cast<const AppBox*>(x->get_child());
	const AppBox* b = dynamic_cast<const AppBox*>(y->get_child());
	if(!(a && b)) return 0; // or throw an exception?
	return a->compare(*b);
}

bool AppChooser::apps_filter(const Gtk::FlowBoxChild* x) const {
	if(filter_lower.empty()) return true;
	const AppBox* a = dynamic_cast<const AppBox*>(x->get_child());
	return a && a->filter(filter_lower);
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
		apps->flowbox->set_sort_func(&apps_sort);
		apps->flowbox->set_filter_func([this](const Gtk::FlowBoxChild* x) { return apps_filter(x); });
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
				const AppBox* box = dynamic_cast<const AppBox*>(selected->get_child());
				if(box) {
					res_app = box->get_app();
					res_cmdline = res_app->get_commandline();
					// remove placeholders (%f, %F, %u and %U for files, and misc; note: in theory, we should properly parse %i, %c and %k)
					auto i = res_cmdline.begin();
					for(auto j = res_cmdline.begin(); j != res_cmdline.end(); ++j) {
						if(*j == '%') {
							++j;
							if(j == res_cmdline.end()) break;
							if(*j != '%') continue;
						}
						if(j != i) *i = *j;
						++i;
					}
					res_cmdline.erase(i, res_cmdline.end());
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


