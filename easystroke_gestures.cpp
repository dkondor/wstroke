/*
 * Copyright (c) 2020, Daniel Kondor <kondor.dani@gmail.com>
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

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <sys/inotify.h>
#include <memory>

#include <linux/input-event-codes.h>
extern "C"
{
#include <wlr/interfaces/wlr_keyboard.h>
}


#include <iostream>
#include "gesture.h"
#include "actiondb.h"

#include "input_events.hpp"


static const char *default_vertex_shader_source =
R"(#version 100

attribute mediump vec2 position;
attribute highp vec2 uvPosition;
varying highp vec2 uvpos;

uniform mat4 MVP;

void main() {
	gl_Position = MVP * vec4(position.xy, 0.0, 1.0);
	uvpos = uvPosition;
})";

static const char *color_rect_fragment_source =
R"(#version 100
varying highp vec2 uvpos;
uniform mediump vec4 color;

void main()
{
	gl_FragColor = color;
})";



class wstroke : public wf::plugin_interface_t, ActionVisitor {
	protected:
		wf::button_callback stroke_initiate;
		wf::option_wrapper_t<wf::buttonbinding_t> initiate{"wstroke/initiate"};
		wf::option_wrapper_t<bool> target_mouse{"wstroke/target_view_mouse"};
		wf::option_wrapper_t<std::string> focus_mode{"wstroke/focus_mode"};
		wf::option_wrapper_t<int> start_timeout{"wstroke/start_timeout"};
		wf::option_wrapper_t<int> end_timeout{"wstroke/end_timeout"};
		
		PreStroke ps;
		std::unique_ptr<ActionDB> actions;
		input_headless input;
		wf::wl_idle_call idle_generate;
		wayfire_view target_view;
		wayfire_view initial_active_view;
		wayfire_view mouse_view;
		int inotify_fd = -1;
		struct wl_event_source* inotify_source = nullptr;
		static constexpr size_t inotify_buffer_size = 10*(sizeof(struct inotify_event) + NAME_MAX + 1);
		char inotify_buffer[inotify_buffer_size];
		
		bool needs_refocus = false;
		
		bool active = false;
		bool is_gesture = false;
		
		bool ptr_moved = false;
		wf::wl_timer timeout;
		
		std::string config_dir;
		
	public:
		wstroke() {
			stroke_initiate = [=](const wf::buttonbinding_t& btn) {
				auto p = output->get_cursor_position();
				return start_stroke(p.x, p.y);
			};
			char* xdg_config = getenv("XDG_CONFIG_HOME");
			if(xdg_config) config_dir = std::string(xdg_config) + "/wstroke/";
			else config_dir = std::string(getenv("HOME")) + "/.config/wstroke/";
		}
		~wstroke() { fini(); }
		
		void init() override {
			inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
			reload_config();
			inotify_source = wl_event_loop_add_fd(wf::get_core().ev_loop, inotify_fd, WL_EVENT_READABLE,
				config_updated, this);
			
			/* start the headless backend, but not instantly since it
			 * might be started automatically by the core multi_backend */
			idle_generate.run_once([this] () {
				input.init();
			});
			
			/* copied from opengl.cpp */
			OpenGL::render_begin();
			color_program.set_simple(OpenGL::compile_program(
				default_vertex_shader_source, color_rect_fragment_source));
			OpenGL::render_end();
			
			grab_interface->name = "wstroke";
			grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;
			grab_interface->callbacks.pointer.motion = [=](int32_t x, int32_t y) {
				ptr_moved = true;
				handle_input_move(x, y);
			};
			grab_interface->callbacks.pointer.button = [=](uint32_t button, uint32_t state) {
				// LOGI("button: ", button, "state: ", state);
				wf::buttonbinding_t tmp = initiate;
				if(button == tmp.get_button() && state == WLR_BUTTON_RELEASED) {
					if(start_timeout > 0 && !ptr_moved) timeout.set_timeout(start_timeout, [this]() { end_stroke(); });
					else end_stroke();
				}
			};
			output->add_button(initiate, &stroke_initiate);
		}
		
		void fini() override {
			if(active) cancel_stroke();
			output->rem_binding(&stroke_initiate);
			input.fini();
			color_program.free_resources();
			actions.reset();
			if(inotify_source) {
				wl_event_source_remove(inotify_source);
				inotify_source = nullptr;
			}
			if(inotify_fd >= 0) {
				close(inotify_fd);
				inotify_fd = -1;
			}
		}
		
		/* visitor interface for carrying out actions */
		void visit(const Command* action) override {
			const auto& cmd = action->get_cmd();
			LOGI("Running command: ", cmd);
			wf::get_core().run(cmd);
		}
		void visit(const SendKey* action) override {
			uint32_t mod = action->get_mods();
			uint32_t key = action->get_key();
			bool needs_refocus_tmp = needs_refocus;
			if(key) {
				idle_generate.run_once([this, mod, key, needs_refocus_tmp] () {
					uint32_t t = wf::get_current_time();
					keyboard_modifiers(t, mod, WL_KEYBOARD_KEY_STATE_PRESSED);
					if(mod) input.keyboard_mods(mod, 0, 0);
					input.keyboard_key(t, key - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
					t++;
					input.keyboard_key(t, key - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
					keyboard_modifiers(t, mod, WL_KEYBOARD_KEY_STATE_RELEASED);
					if(mod) input.keyboard_mods(0, 0, 0);
					if(needs_refocus_tmp) output->focus_view(initial_active_view, false);
				});
				needs_refocus = false;
			}
		}
		void visit(const SendText* action) override {
			LOGW("SendText action not implemented!");
		}
		void visit(const Scroll* action) override {
			LOGW("Scroll action not implemented!");
		}
		void visit(const Ignore* action) override {
			uint32_t ignore_mods = action->get_mods();
			input.keyboard_mods(0, ignore_mods, 0);
		}
		void visit(const Button* action) override {
			LOGW("Button action not implemented!");
		}
		/* global actions */
		void visit(const Global* action) override {
			std::string plugin_activator;
			switch(action->get_action_type()) {
				case Global::Type::EXPO:
					plugin_activator = "expo/toggle";
					break;
				case Global::Type::SCALE:
					plugin_activator = "scale/toggle";
					break;
				case Global::Type::SCALE_ALL:
					plugin_activator = "scale/toggle_all";
					break;
				case Global::Type::SHOW_CONFIG:
					wf::get_core().run("wstroke-config");
					/* fallthrough */
				default:
					return;
			}
			call_plugin(plugin_activator);
		}
		/* actions on the currently active view */
		void visit(const View* action) override {
			if(!target_view) return;
			switch(action->get_action_type()) {
				case View::Type::CLOSE:
					target_view->close();
					break;
				case View::Type::MINIMIZE:
					target_view->minimize_request(true);
					break;
				case View::Type::MAXIMIZE:
					/* toggle maximized state */
					if(target_view->tiled_edges == wf::TILED_EDGES_ALL)
						target_view->tile_request(0);
					else target_view->tile_request(wf::TILED_EDGES_ALL);
					break;
				case View::Type::MOVE:
					target_view->move_request();
					/* in this case we don't refocus the original view,
					 * since the move plugin will raise the selected view,
					 * so it would be confusing for it not to end up
					 * focused as well */
					needs_refocus = false;
					break;
				case View::Type::RESIZE:
					target_view->resize_request(WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
					break;
				case View::Type::FULLSCREEN:
					call_plugin("wm-actions/toggle_fullscreen");
					break;
				default:
					break;
			}
		}
		void visit(const Plugin* action) override {
			call_plugin(action->get_action());
		}
	
	protected:
		
		/* load / reload the configuration; also set up a watch for changes */
		void reload_config() {
			ActionDB* actions_tmp = new ActionDB();
			if(actions_tmp) {
				bool config_read = false;
				try {
					config_read = actions_tmp->read(config_dir);
				}
				catch(std::exception& e) {
					LOGE(e.what());
				}
				if(!config_read) {
					LOGW("Could not find configuration file. Run the wstroke-config program first to assign actions to gestures.");
					delete actions_tmp;
				}
				else actions.reset(actions_tmp);
			}
			if(inotify_fd >= 0) {
				inotify_add_watch(inotify_fd, config_dir.c_str(), IN_CREATE | IN_MOVED_TO);
				std::string config_file = config_dir + "actions-wstroke";
				inotify_add_watch(inotify_fd, config_file.c_str(), IN_MODIFY);
			}
		}
		
		void handle_config_updated() {
			while(read(inotify_fd, inotify_buffer, inotify_buffer_size) > 0) { }
			reload_config();
		}
		
		static int config_updated(int fd, uint32_t mask, void* ptr) {
			wstroke* w = (wstroke*)ptr;
			w->handle_config_updated();
			return 0;
		}
		
		/* call a plugin activator -- do this from the idle_call, so 
		 * that our grab interface does not get in the way; also take
		 * care of refocusing the original view if needed */
		void call_plugin(const std::string& plugin_activator) {
			bool needs_refocus_tmp = needs_refocus;
			idle_generate.run_once([this, needs_refocus_tmp, plugin_activator] () {
				wf::activator_data_t data;
				data.source = wf::activator_source_t::PLUGIN;
				output->call_plugin(plugin_activator, data);
				if(needs_refocus_tmp) output->focus_view(initial_active_view, false);
			});
			needs_refocus = false;
		}
		
		/* focus the view under the mouse if needed -- no gesture case */
		void check_focus_mouse_view() {
			if(mouse_view) {
				const std::string& mode = focus_mode;
				if(mode == "no_gesture" || mode == "always")
					output->focus_view(mouse_view, true);
			}
		}
		
		/* callback when the stroke mouse button is pressed */
		bool start_stroke(int32_t x, int32_t y) {
			if(!actions) return false;
			if(active) {
				LOGW("already active!");
				return false;
			}
			
			initial_active_view = output->get_active_view();
			if(initial_active_view && initial_active_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
				initial_active_view = nullptr;
			
			mouse_view = wf::get_core().get_view_at(wf::pointf_t{(double)x, (double)y});
			if(mouse_view && mouse_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
				mouse_view = nullptr;
			
			target_view = target_mouse ? mouse_view : initial_active_view;
			
			if(target_view) {
				const std::string& app_id = target_view->get_app_id();
				if(actions->exclude_app(app_id)) {
					LOGI("Excluding strokes for app: ", app_id);
					check_focus_mouse_view();
					return false;
				}
			}
			
			if(!output->activate_plugin(grab_interface,0)) {
				LOGE("could not activate");
				return false;
			}
			if(!grab_interface->grab()) {
				LOGE("could not get grab");
				return false;
			}
			
			active = true;
			uint32_t t = wf::get_current_time();
			ps.add(Triple{(float)x, (float)y, t});
			return true;
		}
		
		/* callback when the mouse is moved */
		void handle_input_move(int32_t x, int32_t y) {
			Triple t{(float)x, (float)y, wf::get_current_time()};
			if(!is_gesture) {
				float dist = hypot(t.x - ps.front().x, t.y - ps.front().y);
				if(dist > 16.0f) {
					is_gesture = true;
					start_drawing();
					if(target_mouse && target_view && target_view != initial_active_view) {
						const std::string& mode = focus_mode;
						if(mode == "always" || mode == "only_gesture") needs_refocus = false;
						else needs_refocus = true;
						/* raise the view if it should stay focused after the gesture */
						output->focus_view(target_view, !needs_refocus);
					}
				}
			}
			ps.add(t);
			if(is_gesture) draw_line(ps[ps.size()-2].x, ps[ps.size()-2].y,
				ps.back().x, ps.back().y);
			if(timeout.is_connected()) {
				timeout.disconnect();
				int timeout_len = end_timeout > 0 ? end_timeout : start_timeout;
				timeout.set_timeout(timeout_len, [this]() { end_stroke(); });
			}
		}
		
		/* start drawing the stroke on the screen */
		void start_drawing() {
			for(size_t i = 1; i < ps.size(); i++)
				draw_line(ps[i-1].x, ps[i-1].y, ps[i].x, ps[i].y);
		}
		
		/* callback when the mouse button is released */
		void end_stroke() {
			if(!active) return; /* in case the timeout was not disconnected */
			timeout.disconnect();
			ptr_moved = false;
			clear_lines();
			grab_interface->ungrab();
			output->deactivate_plugin(grab_interface);
			if(is_gesture) {
				RStroke stroke = Stroke::create(ps, 0, 0, 0, 0);
				/* try to match the stroke, write out match */
				const ActionListDiff* matcher = nullptr;
				if(target_view) {
					const std::string& app_id = target_view->get_app_id();
					LOGI("Target app id: ", app_id);
					matcher = actions->get_action_list(app_id);
				}
				else matcher = actions->get_root();
				
				RRanking rr;
				RAction action = matcher->handle(stroke, rr);
				if(action) {
					LOGI("Matched stroke: ", rr->name);
					action->visit(this);
				}
				else LOGI("Unmatched stroke");
				if(needs_refocus) {
					idle_generate.run_once([this]() {
						/* note: initial_active_view can be NULL -- in this
						 * case, we just unfocus the view under the mouse */
						output->focus_view(initial_active_view, false);
					});
					needs_refocus = false;
				}
				is_gesture = false;
			}
			else {
				/* Generate a "fake" mouse click to pass on to the view
				 * originally under the mouse.
				 * 
				 * Note: we cannot directly generate a click since using the
				 * grab interface "unfocuses" any view under the cursor for
				 * the purpose of receiving these events. The
				 * grab_interface->ungrab() call does not instantly reset
				 * this to avoid propagating this event, but adds the
				 * necessary "refocus" to the idle loop. With this call,
				 * we are adding the emulated click to the idle loop as well. */
				idle_generate.run_once([this]() {
					check_focus_mouse_view();
					const wf::buttonbinding_t& tmp = initiate;
					auto t = wf::get_current_time();
					output->rem_binding(&stroke_initiate);
					input.pointer_button(t, tmp.get_button(), WLR_BUTTON_PRESSED);
					input.pointer_button(t, tmp.get_button(), WLR_BUTTON_RELEASED);
					output->add_button(initiate, &stroke_initiate);
				});
			}
			ps.clear();
			active = false;
		}
		
		/* callback to cancel a stroke */
		void cancel_stroke() {
			grab_interface->ungrab();
			output->deactivate_plugin(grab_interface);
			ps.clear();
			clear_lines();
			if(target_mouse) output->focus_view(initial_active_view, false);
			active = false;
			ptr_moved = false;
			is_gesture = false;
			timeout.disconnect();
		}
		
		static constexpr std::array<std::pair<enum wlr_keyboard_modifier, uint32_t>, 4> mod_map = {
	std::pair<enum wlr_keyboard_modifier, uint32_t>(WLR_MODIFIER_SHIFT, KEY_LEFTSHIFT),
	std::pair<enum wlr_keyboard_modifier, uint32_t>(WLR_MODIFIER_CTRL, KEY_LEFTCTRL),
	std::pair<enum wlr_keyboard_modifier, uint32_t>(WLR_MODIFIER_ALT, KEY_LEFTALT),
	std::pair<enum wlr_keyboard_modifier, uint32_t>(WLR_MODIFIER_LOGO, KEY_LEFTMETA)
};
		
		void keyboard_modifiers(uint32_t t, uint32_t mod, enum wl_keyboard_key_state state) {
			for(const auto& x : mod_map) 
				if(x.first & mod) input.keyboard_key(t, x.second, state);
		}
	
	/*****************************************************************
	 * Annotate-like functionality to draw the strokes on the screen *
	 * This could be replaced by a dependence on an external plugin  *
	 *****************************************************************/
	
		/* draw lines on the screen, a simplified version of annotate */
		/* current annotation to be rendered -- store it in a framebuffer */
		wf::framebuffer_t fb;
		/* render function */
		wf::effect_hook_t render_hook = [=] () { render(); };
		/* flag to indicate if render_hook is active */
		bool render_active = false;
		wf::option_wrapper_t<wf::color_t> stroke_color{"wstroke/stroke_color"};
		OpenGL::program_t color_program;
		
		
		/* allocate frambuffer for storing the drawings if it
		 * has not been allocated yet
		 * TODO: track changes in screen size! */
		bool ensure_fb() {
			bool ret = true;
			if(fb.tex == (GLuint)-1 || fb.fb == (GLuint)-1) {
				auto dim = output->get_screen_size();
				OpenGL::render_begin();
				ret = fb.allocate(dim.width, dim.height);
				if(ret) {
					fb.bind(); // bind buffer to clear it
					OpenGL::clear({0, 0, 0, 0});
				}
				OpenGL::render_end();
			}
			return ret;
		}
		
		static void pad_damage_rect(wf::geometry_t& damageRect, float stroke_width) {
			damageRect.x = std::floor(damageRect.x - stroke_width / 2.0);
			damageRect.y = std::floor(damageRect.y - stroke_width / 2.0);
			damageRect.width += std::ceil(stroke_width + 1);
			damageRect.height += std::ceil(stroke_width + 1);
		}

		/* draw a line into the overlay texture between the given points;
		 * allocates the overlay texture if necessary and activates rendering */
		void draw_line(int x1, int y1, int x2, int y2) {
			if(!ensure_fb()) return;
	
			wf::dimensions_t dim = output->get_screen_size();
			auto ortho = glm::ortho(0.0f, (float)dim.width, (float)dim.height, 0.0f);
			
			float stroke_width = 2.0;
			OpenGL::render_begin(fb);
			GL_CALL(glLineWidth(stroke_width));
			/* GL_CALL(glEnable(GL_LINE_SMOOTH)); -- TODO: antialiasing! */
			GLfloat vertexData[4] = { (float)x1, (float)y1, (float)x2, (float)y2 };
			render_vertices(vertexData, 2, stroke_color, GL_LINES, ortho);
			OpenGL::render_end();
			
			if(!render_active) output->render->add_effect(&render_hook, wf::OUTPUT_EFFECT_OVERLAY);
			render_active = true;
			wf::geometry_t d{std::min(x1,x2), std::min(y1,y2), std::abs(x1-x2), std::abs(y1-y2)};
			pad_damage_rect(d, stroke_width);
			output->render->damage(d);
		}
		
		/* clear everything rendered by this plugin and deactivate rendering */
		void clear_lines() {
			if(render_active) {
				output->render->rem_effect(&render_hook);
				fb.release();
				output->render->damage_whole();
				render_active = false;
			}
		}
		
		/* render the current content of the overlay texture */
		void render() {
			if(fb.tex == (GLuint)-1) return;
			auto out_fb = output->render->get_target_framebuffer();
			auto geometry = output->get_relative_geometry();
			auto ortho = out_fb.get_orthographic_projection();
			
			OpenGL::render_begin(out_fb);
			GL_CALL(glEnable(GL_BLEND));
			OpenGL::render_transformed_texture(fb.tex, geometry, ortho);
			GL_CALL(glDisable(GL_BLEND));
			OpenGL::render_end();
		}
		
		/* render a sequence of vertices, using the given color 
		 * should only be called between OpenGL::render_begin() and
		 * OpenGL::render_end() */
		void render_vertices(GLfloat* vertexData, GLsizei nvertices,
			wf::color_t color, GLenum mode, glm::mat4 matrix)
		{
			color_program.use(wf::TEXTURE_TYPE_RGBA);
			
			color_program.attrib_pointer("position", 2, 0, vertexData);
			color_program.uniformMatrix4f("MVP", matrix);
			color_program.uniform4f("color", {color.r, color.g, color.b, color.a});

			GL_CALL(glEnable(GL_BLEND));
			GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
			GL_CALL(glDrawArrays(mode, 0, nvertices));

			color_program.deactivate();
		}
};

constexpr std::array<std::pair<enum wlr_keyboard_modifier, uint32_t>, 4> wstroke::mod_map;

DECLARE_WAYFIRE_PLUGIN(wstroke)

