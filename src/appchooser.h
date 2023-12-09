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


#ifndef APPCHOOSER_H
#define APPCHOOSER_H

#include <gtkmm.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

class AppChooser {
	private:
		Glib::RefPtr<Gtk::Builder> widgets;
		Gtk::Dialog* dialog;
		Gtk::HeaderBar* header;
		Gtk::ScrolledWindow* sw;
		Gtk::Entry* entry;
		Gtk::CheckButton* cb;
		Gtk::Button *select_ok;
		
		GAppInfoMonitor* monitor = nullptr;
		
		struct AppContent {
			std::vector<Glib::RefPtr<Gio::AppInfo> > apps;
			std::unique_ptr<Gtk::FlowBox> flowbox;
			std::unordered_map<Gtk::Box*, Glib::RefPtr<Gio::AppInfo> > app_buttons;
		};
		
		std::unique_ptr<AppContent> apps;
		std::atomic<AppContent*> apps_pending;
		
		std::thread thread;
		std::mutex mutex;
		bool thread_running = false;
		bool more_work = false;
		std::atomic<bool> exit_request{false};
		bool first_run = false;
		
		bool update_pending = false;
		
		void update_apps();
		void thread_func();
		
		friend void on_apps_changed(GAppInfoMonitor*, void* p);
		
	public:
		Glib::RefPtr<Gio::AppInfo> res_app;
		std::string res_cmdline;
		bool custom_res = false;
		
		AppChooser(Glib::RefPtr<Gtk::Builder>& w) : widgets(w) { }
		~AppChooser();
		
		void startup();
		
		bool run(const Glib::ustring& gesture_name);
};

#endif


