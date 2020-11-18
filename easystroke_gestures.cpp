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



class wayfire_easystroke : public wf::plugin_interface_t, ActionVisitor {
	protected:
		wf::button_callback stroke_initiate;
		wf::option_wrapper_t<wf::buttonbinding_t> initiate{"easystroke/initiate"};
		wf::option_wrapper_t<bool> target_mouse{"easystroke/target_view_mouse"};
		
		PreStroke ps;
		ActionDB* actions = nullptr;
		input_headless input;
		wf::wl_idle_call idle_generate;
		wayfire_view target_view;
		wayfire_view initial_active_view;
		int inotify_fd = -1;
		struct wl_event_source* inotify_source = nullptr;
		static constexpr size_t inotify_buffer_size = 10*(sizeof(struct inotify_event) + NAME_MAX + 1);
		char inotify_buffer[inotify_buffer_size];
		
		bool needs_refocus = false;
		
		bool active = false;
		bool is_gesture = false;
		
	public:
		wayfire_easystroke() {
			stroke_initiate = [=](uint32_t button, int32_t x, int32_t y) {
				return start_stroke(x, y);
			};
		}
		~wayfire_easystroke() { fini(); }
		
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
			
			grab_interface->name = "easystroke";
			grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;
			grab_interface->callbacks.pointer.motion = [=](int32_t x, int32_t y) {
				handle_input_move(x, y);
			};
			grab_interface->callbacks.pointer.button = [=](uint32_t button, uint32_t state) {
				wf::buttonbinding_t tmp = initiate;
				if(button == tmp.get_button() && state == WLR_BUTTON_RELEASED) end_stroke();
			};
			output->add_button(initiate, &stroke_initiate);
		}
		
		void fini() override {
			if(active) cancel_stroke();
			output->rem_binding(&stroke_initiate);
			input.fini();
			color_program.free_resources();
			if(actions) {
				delete actions;
				actions = nullptr;
			}
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
		void visit(const Misc* action) override {
			if(action->get_action_type() == Misc::Type::SHOWHIDE) 
				wf::get_core().run("wstroke-config");
			if(!target_view) return;
			switch(action->get_action_type()) {
				case Misc::Type::CLOSE:
					target_view->close();
					break;
				case Misc::Type::MINIMIZE:
					target_view->minimize_request(true);
					break;
				case Misc::Type::MAXIMIZE:
					/* toggle maximized state */
					if(target_view->tiled_edges == wf::TILED_EDGES_ALL)
						target_view->tile_request(0);
					else target_view->tile_request(wf::TILED_EDGES_ALL);
					break;
				case Misc::Type::MOVE:
					target_view->move_request();
					break;
				case Misc::Type::RESIZE:
					target_view->resize_request(WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT);
					break;
				default:
					break;
			}
		}
	
	protected:
		
		/* load / reload the configuration; also set up a watch for changes */
		void reload_config() {
			std::string config_dir = getenv("HOME");
			config_dir += "/.config/wstroke/";
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
				else {
					if(actions) delete actions;
					actions = actions_tmp;
				}
			}
			if(inotify_fd >= 0) {
				inotify_add_watch(inotify_fd, config_dir.c_str(), IN_CREATE | IN_MOVED_TO);
				std::string config_file = config_dir + "inotify_buffer_size";
				inotify_add_watch(inotify_fd, config_file.c_str(), IN_MODIFY);
			}
		}
		
		void handle_config_updated() {
			while(read(inotify_fd, inotify_buffer, inotify_buffer_size) > 0) { }
			reload_config();
		}
		
		static int config_updated(int fd, uint32_t mask, void* ptr) {
			wayfire_easystroke* w = (wayfire_easystroke*)ptr;
			w->handle_config_updated();
			return 0;
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
			if(target_mouse) {
				target_view = wf::get_core().get_view_at(wf::pointf_t{(double)x, (double)y});
				if(target_view && target_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
					target_view = nullptr;
				if(target_view && target_view != initial_active_view) output->focus_view(target_view, false);
			}
			else target_view = initial_active_view;
			
			if(target_view) {
				const std::string& app_id = target_view->get_app_id();
				if(actions->exclude_app(app_id)) {
					LOGI("Excluding strokes for app: ", app_id);
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
				}
			}
			ps.add(t);
			draw_line(ps[ps.size()-2].x, ps[ps.size()-2].y,
				ps.back().x, ps.back().y);
		}
		
		/* start drawing the stroke on the screen with the annotate plugin */
		void start_drawing() {
			for(size_t i = 1; i < ps.size(); i++)
				draw_line(ps[i-1].x, ps[i-1].y, ps[i].x, ps[i].y);
		}
		
		/* callback when the mouse button is released */
		void end_stroke() {
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
				needs_refocus = target_mouse && (target_view != initial_active_view);
				if(action) {
					LOGI("Matched stroke: ", rr->name);
					action->visit(this);
				}
				else LOGI("Unmatched stroke");
				if(needs_refocus) {
					idle_generate.run_once([this]() {
						output->focus_view(initial_active_view, false);
					});
					needs_refocus = false;
				}
				is_gesture = false;
			}
			else {
				/* Note: we cannot directly generate a click since using the
				 * grab interface "unfocuses" any view under the cursor for
				 * the purpose of receiving these events. The
				 * grab_interface->ungrab() call does not instantly reset
				 * this to avoid propagating this event, but adds the
				 * necessary "refocus" to the idle loop. With this call,
				 * we are adding the emulated click to the idle loop as well. */
				idle_generate.run_once([this]() {
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
			is_gesture = false;
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
		wf::post_hook_t render_hook = [=] (const wf::framebuffer_base_t& source,
			const wf::framebuffer_base_t& destination) { render(source, destination); };
		/* flag to indicate if render_hook is active */
		bool render_active = false;
		wf::option_wrapper_t<wf::color_t> stroke_color{"easystroke/stroke_color"};
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

		
		void draw_line(int x1, int y1, int x2, int y2) {
			if(!ensure_fb()) return;
	
			wf::dimensions_t dim = output->get_screen_size();
			auto ortho = glm::ortho(0.0f, (float)dim.width, 0.0f, (float)dim.height);
			
			float stroke_width = 2.0;
			OpenGL::render_begin(fb);
			GL_CALL(glLineWidth(stroke_width));
			/* GL_CALL(glEnable(GL_LINE_SMOOTH)); -- TODO: antialiasing! */
			GLfloat vertexData[4] = { (float)x1, (float)y1, (float)x2, (float)y2 };
			render_vertices(vertexData, 2, stroke_color, GL_LINES, ortho);
			OpenGL::render_end();
			
			if(!render_active) output->render->add_post(&render_hook);
			render_active = true;
			wf::geometry_t d{std::min(x1,x2), std::min(y1,y2), std::abs(x1-x2), std::abs(y1-y2)};
			pad_damage_rect(d, stroke_width);
			output->render->damage(d);
		}
		
		void clear_lines() {
			if(render_active) {
				output->render->rem_post(&render_hook);
				fb.release();
				output->render->damage_whole();
				render_active = false;
			}
		}
		
		
		void render(const wf::framebuffer_base_t& source, const wf::framebuffer_base_t& destination) {
			wf::dimensions_t dim = output->get_screen_size();
			
			auto ortho = glm::ortho(0.0f, (float)dim.width, (float)dim.height, 0.0f);
			wf::geometry_t geom{0,0,dim.width,dim.height};
			OpenGL::render_begin(destination);
			
			OpenGL::render_transformed_texture(source.tex, geom, ortho);
			GL_CALL(glEnable(GL_BLEND));
			if(fb.tex != (GLuint)-1)
				OpenGL::render_transformed_texture(fb.tex, geom, ortho);
			// GL_CALL(glDisable(GL_BLEND));
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

constexpr std::array<std::pair<enum wlr_keyboard_modifier, uint32_t>, 4> wayfire_easystroke::mod_map;

DECLARE_WAYFIRE_PLUGIN(wayfire_easystroke)

