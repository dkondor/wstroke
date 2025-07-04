/*
 * Copyright (c) 2020-2024, Daniel Kondor <kondor.dani@gmail.com>
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
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/core.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <sys/inotify.h>
#include <memory>
#include <filesystem>
#include <cstring>

#include <cairo.h>
#include <pixman.h>
#include <drm_fourcc.h> // for DRM_FORMAT_ARGB8888

#include <linux/input-event-codes.h>
extern "C"
{
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/pixman.h>
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

class ws_node_base;


class ws_render_instance : public wf::scene::simple_render_instance_t<ws_node_base> {
	public:
		/* render the current content of the overlay texture */
		void render(const wf::scene::render_instruction_t& data) override;
		
		ws_render_instance(ws_node_base *self, wf::scene::damage_callback push_damage, wf::output_t *output) :
			wf::scene::simple_render_instance_t<ws_node_base>(self, push_damage, output) { }
};


/** 
 * Node to draw lines on the screen, a simplified version of annotate.
 * This is a base class that does nothing. Derived classes implement
 * actual rendering based on the render backend in use. */
class ws_node_base : public wf::scene::node_t {
	protected:
		wf::option_wrapper_t<wf::color_t> stroke_color{"wstroke/stroke_color"};
		wf::option_wrapper_t<int> stroke_width{"wstroke/stroke_width"};
		
		/* Helper to apply damage after updating the current stroke */
		static void pad_damage_rect(wf::geometry_t& damageRect, float stroke_width) {
			damageRect.x = std::floor(damageRect.x - stroke_width / 2.0);
			damageRect.y = std::floor(damageRect.y - stroke_width / 2.0);
			damageRect.width += std::ceil(stroke_width + 1);
			damageRect.height += std::ceil(stroke_width + 1);
		}
	
	public:
		ws_node_base(wf::output_t* output_) : node_t(false), output(output_) { }
		
		/* output to which this node renders -- needs to be public as it is
		 * used by ws_render_instance::render() */
		wf::output_t* const output;
		
		/** Main interface used by our plugin: */
		/* draw a line into our overlay between the given points */
		virtual void draw_line(int x1, int y1, int x2, int y2) { }
		
		/* clear everything rendered by this plugin and deallocate any textrue or framebuffer used */
		virtual void clear_lines() { }
		
		
		/** Override functions for node_t -- these do nothing in the base case */
		void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
			wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
		{ }
		
		wf::geometry_t get_bounding_box() override {
			return {0, 0, 0, 0};
		}
		
		/* Function used by ws_render_instance::render() to get the actual
		 * texture to show (if any). */
		virtual wf::texture_t get_texture() {
			return {};
		}
};

/* EGL version */
class ws_node_egl : public ws_node_base {
	private:
		/* current annotation to be rendered -- store it in an auxiliary buffer */
		wf::auxilliary_buffer_t fb;
		OpenGL::program_t color_program;
		
		/* allocate frambuffer for storing the drawings if it
		 * has not been allocated yet, according to the current
		 * output size */
		bool ensure_fb() {
			auto dim = output->get_screen_size();
			auto ret = fb.allocate(dim); //!! TODO: is it guaranteed that this will allocate an EGL buffer?
			
			if (ret == wf::buffer_reallocation_result_t::REALLOCATED) {
				wf::gles::run_in_context([&] {
					wf::gles::bind_render_buffer(fb.get_renderbuffer());
					OpenGL::clear({0, 0, 0, 0});
				});
			}
			
			return (ret != wf::buffer_reallocation_result_t::FAILED);
		}
		
		/* render a sequence of vertices, using the given color 
		 * should only be called in wf::gles::run_in_context() */
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
		
	public:
		ws_node_egl(wf::output_t* output_) : ws_node_base(output_) {
			wf::gles::run_in_context([&] {
				color_program.set_simple(OpenGL::compile_program(
					default_vertex_shader_source, color_rect_fragment_source));
			});
		}
		
		void draw_line(int x1, int y1, int x2, int y2) override {
			if(stroke_width == 0) return;
			if(!ensure_fb()) return;
			
			wf::dimensions_t dim = output->get_screen_size();
			auto ortho = glm::ortho(0.0f, (float)dim.width, 0.0f, (float)dim.height);
			
			wf::gles::run_in_context([&] {
				wf::gles::bind_render_buffer(fb.get_renderbuffer());
				GL_CALL(glLineWidth((float)stroke_width));
				GLfloat vertexData[4] = { (float)x1, (float)y1, (float)x2, (float)y2 };
				render_vertices(vertexData, 2, stroke_color, GL_LINES, ortho);
			});
			
			wf::geometry_t d{std::min(x1,x2), std::min(y1,y2), std::abs(x1-x2), std::abs(y1-y2)};
			pad_damage_rect(d, stroke_width);
			
			wf::scene::node_damage_signal ev;
			ev.region = d; /* note: implicit conversion to wf::region_t */
			this->emit(&ev);
		}
		
		void clear_lines() override {
			fb.free();
			output->render->damage_whole();
		}
		
		void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
				wf::scene::damage_callback push_damage, wf::output_t *shown_on) override {
			if(shown_on == output)
				instances.push_back(std::make_unique<ws_render_instance>(this, push_damage, shown_on));
		}
		
		wf::geometry_t get_bounding_box() override {
			wf::dimensions_t dim = output->get_screen_size();
			return {0, 0, dim.width, dim.height};
		}
		
		wf::texture_t get_texture() override {
			if(!fb.get_buffer()) return {};
			return {fb.get_texture()};
		}
};


/* Base class for pixman and vulkan cases, strokes are drawn with Cairo and
 * converted to a wlr_texture to use later. */
class ws_node_cairo : public ws_node_base {
	protected:
		cairo_t *ctx = nullptr;
		cairo_surface_t *surface = nullptr;
		wf::texture_t texture;
		// account for whether we have rendered at least once (needed by the pixman renderer)
		bool first_render_done = false;
	
	private:
		bool ensure_surface() {
			auto dim = output->get_screen_size();
			
			if(surface) {
				if(cairo_image_surface_get_height(surface) != dim.height ||
						cairo_image_surface_get_width(surface) != dim.width) {
					free_texture();
					free_cairo();
				}
			}
			
			if(!surface) {
				surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dim.width, dim.height);
				if(!surface) return false;
				ctx = cairo_create(surface);
				clear_overlay();
				cairo_surface_flush(surface);
				// note: in this case, we should have texture.texture == nullptr
			}
			
			return true;
		}
		
		void clear_overlay() {
			cairo_set_source_rgba(ctx, 0, 0, 0, 0);
			cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
			cairo_paint(ctx);
		}
		
		void free_texture() {
			if(texture.texture) {
				wlr_texture_destroy(texture.texture);
				texture.texture = nullptr;
			}
			first_render_done = false;
		}
		
		void free_cairo() {
			if(ctx) {
				cairo_destroy(ctx);
				ctx = nullptr;
			}
			if(surface) {
				cairo_surface_destroy(surface);
				surface = nullptr;
			}
		}
	
	protected:
		/* create our wlr_texture from our cairo surface */
		bool create_texture() {
			free_texture();
			texture.texture = wlr_texture_from_pixels(wf::get_core().renderer,
				DRM_FORMAT_ARGB8888, cairo_image_surface_get_stride(surface),
				cairo_image_surface_get_width(surface),
				cairo_image_surface_get_height(surface),
				cairo_image_surface_get_data(surface));
			return (texture.texture != nullptr);
		}
		
		virtual bool update_texture(const wf::geometry_t& d) = 0;
	
	public:
		ws_node_cairo(wf::output_t* output_) : ws_node_base(output_) {
			/* nothing else to do, we will allocate a buffer when first drawing */			
		}
		
		~ws_node_cairo() {
			/* Destroy our texture first, as it might refer to the same memory
			 * as our cairo surface. */
			free_texture();
			free_cairo();
		}
		
		void draw_line(int x1, int y1, int x2, int y2) override {
			if(stroke_width == 0) return;
			if(!ensure_surface()) return;
			
			wf::color_t color = stroke_color;
			cairo_set_line_width(ctx, stroke_width);
			cairo_set_source_rgba(ctx, color.r, color.g, color.b, color.a);
			cairo_move_to(ctx, x1, y1);
			cairo_line_to(ctx, x2, y2);
			cairo_stroke(ctx);
			cairo_surface_flush(surface);
			
			wf::geometry_t d{std::min(x1,x2), std::min(y1,y2), std::abs(x1-x2), std::abs(y1-y2)};
			pad_damage_rect(d, stroke_width);
			
			bool res;
			if(!texture.texture) res = create_texture();
			else res = update_texture(d);
			
			if(res) {
				wf::scene::node_damage_signal ev;
				ev.region = d; /* note: implicit conversion to wf::region_t */
				this->emit(&ev);
			}
		}
		
		void clear_lines() override {
			free_texture();
			if(ctx) clear_overlay();
			output->render->damage_whole();
		}
		
		void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
				wf::scene::damage_callback push_damage, wf::output_t *shown_on) override {
			if(shown_on == output)
				instances.push_back(std::make_unique<ws_render_instance>(this, push_damage, shown_on));
		}
		
		wf::geometry_t get_bounding_box() override {
			wf::dimensions_t dim = output->get_screen_size();
			return {0, 0, dim.width, dim.height};
		}
		
		wf::texture_t get_texture() override {
			return texture;
		}
};

class ws_node_pixman : public ws_node_cairo {
	private:
		wf::geometry_t damage_acc{};
		wf::wl_idle_call idle_damage;
		
		void extend_damage(const wf::geometry_t& d) {
			if(damage_acc.width == 0 && damage_acc.height == 0) {
				damage_acc = d;
				return;
			}
			if(d.width == 0 && d.height == 0) return;
			int x1 = std::min(d.x, damage_acc.x);
			int y1 = std::min(d.y, damage_acc.y);
			int x2 = std::max(d.x + d.width,  damage_acc.x + damage_acc.width);
			int y2 = std::max(d.y + d.height, damage_acc.y + damage_acc.height);
			damage_acc = {x1, y1, x2 - x1, y2 - y1};
		}
		
	protected:
		bool update_texture(const wf::geometry_t& d) override {
			/**
			 * wlroots' pixman renderer works with the following quirks when
			 * using a texture created with wlr_texture_from_pixels():
			 *  1. First a wlr_readonly_data_buffer is created which just encapsulates
			 *      whatever data it gets (so it is just a wrapper around our cairo
			 *      surface at this point)
			 *  2. Then, a wlr_pixman_texture is created from this buffer. This creates
			 *      a pixman_image_t that also wraps our cairo surface.
			 *  3. After this, the buffer is "dropped". Instead of actually freeing
			 *      it up, it is kept around (saved in the texture), but its data
			 *      pointer is replaced by a newly created copy of the original data.
			 *  4. Thus at this point, we have two copies of our image data:
			 *     -- texture->image, which just wraps our cairo surface
			 *     -- texture->buffer, which has a copy
			 *  5. When we try to render this texture, the pixman renderer compares
			 *      the above two data pointers, and decides that the pixman image
			 *      is "out of date". This is motivated by the fact that if working
			 *      with a buffer supplied by a Wayland client, its data pointer
			 *      can indeed change.
			 *  6. The pixman renderer deals with this by destroying the original
			 *      pixman image and creating a new one which now wraps the data
			 *      pointer in the wlr_buffer (i.e. which is a copy of the original
			 *      image we submitted when creating the texture).
			 * This means that any changes to the image (i.e. our cairo surface)
			 * between steps #4 and #5 (between wlr_texture_from_pixels() and the
			 * first time we actually render it) will not be visible.
			 * 
			 * To account for the above, we need to track any such changes and redo
			 * the copy after we have done at least one render pass.
			 * 
			 * Note: this means that we likely should not rely on being able to
			 * change the content of a wlr_texture after it has been created, but
			 * we do not want to recreate it every time when the stroke is updated.
			 */
			
			extend_damage(d);
			
			/* If we have not rendered first, we just make note of the newly damaged area (as above). */
			if(!first_render_done) return true;
			
			/* Copy the damaged area to our texture if needed, which is a pixman image */
			pixman_image_t *img = wlr_pixman_texture_get_image(texture.texture);
			if(!img) {
				LOGE("Cannot access pixman texture data!");
				return false;
			}
			
			if (pixman_image_get_format(img) != PIXMAN_a8r8g8b8) {
				// should not happen, img should be a copy of our data
				LOGE("Pixman texture data in incorrect format!");
				return false;
			}
			
			uint8_t *dst = (uint8_t*)pixman_image_get_data(img);
			int stride_dst = pixman_image_get_stride(img);
			uint8_t *src = cairo_image_surface_get_data(surface);
			int stride_src = cairo_image_surface_get_stride(surface);
			
			if(dst != src) {
				/** Based on the above, we expect this case. */
				
				/* copy our data
				 * Note: wf::geometry_t is a wlr_box which has the following definition:
					struct wlr_box {
						int x, y;
						int width, height;
					};
				 */
				for(int y = damage_acc.y; y < damage_acc.y + damage_acc.height; y++) {
					size_t base_src = y * stride_src;
					size_t base_dst = y * stride_dst;
					// note: each pixel is 4 bytes
					std::memcpy(dst + base_dst + damage_acc.x * 4UL, src + base_src + damage_acc.x * 4UL, damage_acc.width * 4UL);
				}
			}
			
			/* reset previous damage */
			damage_acc = {0, 0, 0, 0};
			
			return true;
		}
	
	public:
		ws_node_pixman(wf::output_t* output_) : ws_node_cairo(output_) { }
		
		wf::texture_t get_texture() override {
			/* This is called from the render() function, so we set here
			 * that the first render pass was done, but also we schedule
			 * an update to the now updated texture and re-submit the
			 * accummulated damage. */
			if(!first_render_done && texture.texture) {
				idle_damage.run_once([this] () {
					if(texture.texture) {
						wf::scene::node_damage_signal ev;
						ev.region = damage_acc;
						update_texture({});
						this->emit(&ev);
					}
				});
				first_render_done = true;
			}
			return texture;
		}
};

class ws_node_vulkan : public ws_node_cairo {
	private:
		bool need_update = false;
	
	protected:
		bool update_texture(const wf::geometry_t& d) override {
			/* Just mark that we need to update the overlay texture. It
			 * will be recreated (re-uploaded to the GPU) at the next render.
			 * This way, we have maximum one texture upload per render cycle. */
			need_update = true;
			return true;
		}
	
	public:
		ws_node_vulkan(wf::output_t* output_) : ws_node_cairo(output_) { }
		
		wf::texture_t get_texture() override {
			/* I don't know how to update only part of a Vulkan-based wlr_texture,
			 * so we just recreate the whole thing. This means re-uploading the
			 * full surface to the GPU again unfortunately.
			 * Note: create_texture() will free up the current texture. */
			if(need_update) {
				create_texture();
				need_update = false;
			}
			return texture;
		}
};



void ws_render_instance::render(const wf::scene::render_instruction_t& data) {
	auto texture = this->self->get_texture();
	if(!texture.texture) return;
	auto geometry = this->self->output->get_relative_geometry();
	data.pass->add_texture(texture, data.target, geometry, data.damage);
}


/* Helper to create the correct type of render node based on the render
 * backend in use. */
static std::shared_ptr<ws_node_base> get_ws_node(wf::output_t* output_) {
	if(wf::get_core().is_gles2())
		return std::shared_ptr<ws_node_base>(new ws_node_egl(output_));
	
	if(wf::get_core().is_vulkan())
		return std::shared_ptr<ws_node_base>(new ws_node_vulkan(output_));
	
	if(wlr_renderer_is_pixman(wf::get_core().renderer))
		return std::shared_ptr<ws_node_base>(new ws_node_pixman(output_));
	
	return std::shared_ptr<ws_node_base>(new ws_node_base(output_));
}


class wstroke : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t, ActionVisitor {
	protected:
		wf::button_callback stroke_initiate;
		wf::option_wrapper_t<wf::buttonbinding_t> initiate{"wstroke/initiate"};
		wf::option_wrapper_t<bool> target_mouse{"wstroke/target_view_mouse"};
		wf::option_wrapper_t<std::string> focus_mode{"wstroke/focus_mode"};
		wf::option_wrapper_t<int> start_timeout{"wstroke/start_timeout"};
		wf::option_wrapper_t<int> end_timeout{"wstroke/end_timeout"};
		wf::option_wrapper_t<std::string> resize_edges{"wstroke/resize_edges"};
		wf::option_wrapper_t<double> touchpad_scroll_sensitivity{"wstroke/touchpad_scroll_sensitivity"};
		wf::option_wrapper_t<int> touchpad_pinch_sensitivity{"wstroke/touchpad_pinch_sensitivity"};
		
		std::unique_ptr<wf::input_grab_t> input_grab;
		wf::plugin_activation_data_t grab_interface{
			.name = "wstroke",
			.capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
			.cancel = [this]() { cancel_stroke(); }
		};
		
		Stroke::PreStroke ps;
		std::unique_ptr<const ActionDB> actions;
		input_headless input;
		wf::wl_idle_call idle_generate;
		wayfire_view target_view;
		wayfire_view initial_active_view;
		wayfire_view mouse_view;
		int inotify_fd = -1;
		struct wl_event_source* inotify_source = nullptr;
		static constexpr size_t inotify_buffer_size = 10*(sizeof(struct inotify_event) + NAME_MAX + 1);
		char inotify_buffer[inotify_buffer_size];
		
		/* true if we need to refocus the initial view after the gesture
		 * action -- refocusing might be done by the action visitor or
		 * the main function when ending the gesture */
		bool needs_refocus = false;
		bool needs_refocus2 = false; /* temporary copy of the above used by the idle callbacks */
		
		bool active = false;
		bool is_gesture = false; /* whether currently processing a gesture */
		/* current modifier keys held by the ignore action */
		uint32_t ignore_active = 0;
		
		/* currently active touchpad action */
		Touchpad::Type touchpad_active = Touchpad::Type::NONE;
		double touchpad_last_angle = 0.0; // in radians, compared to the x axis
		double touchpad_last_scale = 1.0; // last scale sent in a pinch gesture
		bool next_release_touchpad = false; // if true, do not propagate the next button release event
		bool ignore_next_own_btn = false; // allow to generate a click that will be ignored
		uint32_t touchpad_fingers = 0; // number of fingers in the current touchpad gesture
		
		bool ptr_moved = false;
		wf::wl_timer<false> timeout;
		
		std::string config_dir;
		std::string config_file;
		
		/* Handle views being unmapped -- needed to avoid segfault if the "target" views disappear */
		wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped = [=] (wf::view_unmapped_signal *ev) {
			auto view = ev->view;
			if(view) {
				if(target_view == view) target_view = nullptr;
				if(initial_active_view == view) {
					needs_refocus = false; /* "lost" the initially active view, avoid refocusing */
					needs_refocus2 = false;
					initial_active_view = nullptr;
				}
				if(mouse_view == view) mouse_view = nullptr;
			}
		};
		
		/* scenegraph node for drawing an overlay -- it is active
		 * (i.e. added to the scenegraph) iff. is_gesture == true */
		std::shared_ptr<ws_node_base> overlay_node;
		
	public:
		wstroke() {
			stroke_initiate = [=](const wf::buttonbinding_t& btn) {
				auto p = output->get_cursor_position();
				return start_stroke(p.x, p.y);
			};
			char* xdg_config = getenv("XDG_CONFIG_HOME");
			if(xdg_config) config_dir = std::string(xdg_config) + "/wstroke/";
			else config_dir = std::string(getenv("HOME")) + "/.config/wstroke/";
			config_file = config_dir + ActionDB::wstroke_actions_versions[0];
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
			
			overlay_node = get_ws_node(output);
			
			output->add_button(initiate, &stroke_initiate);
			wf::get_core().connect(&on_raw_pointer_button);
			wf::get_core().connect(&on_raw_pointer_motion);
			// wf::get_core().connect_signal("keyboard_key_post", &ignore_key_cb); -- ignore does not work combined with the real keyboard
			
			input_grab = std::make_unique<wf::input_grab_t>(this->grab_interface.name, output, nullptr, this, nullptr);
			input_grab->set_wants_raw_input(true);
		}
		
		void fini() override {
			if(active) cancel_stroke();
			on_raw_pointer_button.disconnect();
			on_raw_pointer_motion.disconnect();
			// ignore_key_cb.disconnect();
			output->rem_binding(&stroke_initiate);
			input.fini();
			overlay_node = nullptr;
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
		
		/* pointer tracking interface */
		void handle_pointer_button(const wlr_pointer_button_event& event) override {
			wf::buttonbinding_t tmp = initiate;
			if(event.button == tmp.get_button() && event.state == WL_POINTER_BUTTON_STATE_RELEASED) {
				if(start_timeout > 0 && !ptr_moved) timeout.set_timeout(start_timeout, [this]() { end_stroke(); });
				else end_stroke();
			}
		}
		
		void handle_pointer_motion(wf::pointf_t pointer_position, uint32_t time_ms) override {
			ptr_moved = true;
			auto geom = output->get_layout_geometry();
			handle_input_move(pointer_position.x - geom.x, pointer_position.y - geom.y);
		}
		
		
		/* visitor interface for carrying out actions */
		void visit(const Command* action) override {
			const auto& cmd = action->get_cmd();
			LOGD("Running command: ", cmd);
			set_idle_action([cmd] () {wf::get_core().run(cmd);}, false);
		}
		void visit(const SendKey* action) override {
			uint32_t mod = action->get_mods();
			uint32_t key = action->get_key();
			if(key) {
				set_idle_action([this, mod, key] () {
					uint32_t t = wf::get_current_time();
					keyboard_modifiers(t, mod, WL_KEYBOARD_KEY_STATE_PRESSED);
					if(mod) input.keyboard_mods(mod, 0, 0);
					input.keyboard_key(t, key - 8, WL_KEYBOARD_KEY_STATE_PRESSED);
					t++;
					input.keyboard_key(t, key - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
					keyboard_modifiers(t, mod, WL_KEYBOARD_KEY_STATE_RELEASED);
					if(mod) input.keyboard_mods(0, 0, 0);
				});
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
			set_idle_action([this, ignore_mods] () {
				uint32_t t = wf::get_current_time();
				keyboard_modifiers(t, ignore_mods, WL_KEYBOARD_KEY_STATE_PRESSED);
				input.keyboard_mods(ignore_mods, 0, 0);
				ignore_active = ignore_mods;
			});
		}
		void visit(const Button* action) override {
			uint32_t btn = action->get_button();
			uint32_t mod = action->get_mods();
			
			// convert button to event code (btn is 1-based mouse button number)
			switch(btn) {
				case 1:
					btn = BTN_LEFT;
					break;
				case 2:
					btn = BTN_MIDDLE;
					break;
				case 3:
					btn = BTN_RIGHT;
					break;
				default:
					LOGW("Unsupported mouse button: ", btn);
					return;
			}
			
			set_idle_action([this, mod, btn] () {
				uint32_t t = wf::get_current_time();
				if(mod) {
					keyboard_modifiers(t, mod, WL_KEYBOARD_KEY_STATE_PRESSED);
					input.keyboard_mods(mod, 0, 0);
				}
				input.pointer_button(t, btn, WL_POINTER_BUTTON_STATE_PRESSED);
				t++;
				input.pointer_button(t, btn, WL_POINTER_BUTTON_STATE_RELEASED);
				if(mod) {
					keyboard_modifiers(t, mod, WL_KEYBOARD_KEY_STATE_RELEASED);
					input.keyboard_mods(0, 0, 0);
				}
			});
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
				case Global::Type::SHOW_DESKTOP:
					plugin_activator = "wm-actions/toggle_showdesktop";
					break;
				case Global::Type::CUBE:
					plugin_activator = "cube/activate";
					break;
				case Global::Type::SHOW_CONFIG:
					set_idle_action([] () {wf::get_core().run("wstroke-config");}, false);
					/* fallthrough */
				default:
					return;
			}
			call_plugin(plugin_activator);
		}
		/* actions on the currently active view */
		void visit(const View* action) override {
			if(!target_view) return;
			wayfire_toplevel_view toplevel = wf::toplevel_cast(target_view);
			switch(action->get_action_type()) {
				case View::Type::CLOSE:
					target_view->close();
					break;
				case View::Type::MINIMIZE:
					if(toplevel) wf::get_core().default_wm->minimize_request(toplevel, true);
					break;
				case View::Type::MAXIMIZE:
					/* toggle maximized state */
					if(toplevel) {
						if(toplevel->pending_tiled_edges() == wf::TILED_EDGES_ALL)
							 wf::get_core().default_wm->tile_request(toplevel, 0);
						else wf::get_core().default_wm->tile_request(toplevel, wf::TILED_EDGES_ALL);
					}
					break;
				case View::Type::MOVE:
					if(toplevel) {
						/* in this case we don't refocus the original view,
						 * since the move plugin will raise the selected view,
						 * so it would be confusing for it not to end up
						 * focused as well */
						needs_refocus = false;
						set_idle_action([this] () {
							if(target_view) {
								wayfire_toplevel_view toplevel = wf::toplevel_cast(target_view);
								if(toplevel) {
									/* we generate a "fake" click so that the move will
									 * commence from the current pointer location */
									ignore_next_own_btn = true;
									uint32_t t = wf::get_current_time();
									input.pointer_button(t, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
									t++;
									input.pointer_button(t, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
									wf::get_core().default_wm->move_request(toplevel);
								}
							}
						});
					}
					break;
				case View::Type::RESIZE:
					if(toplevel) wf::get_core().default_wm->resize_request(toplevel, get_resize_edges());
					break;
				case View::Type::FULLSCREEN:
					if(toplevel) {
						wf::json_t tmp;
						tmp["state"] = !toplevel->toplevel()->current().fullscreen;
						call_plugin("wm-actions/set-fullscreen", true, std::move(tmp));
					}
					break;
				case View::Type::SEND_TO_BACK:
					call_plugin("wm-actions/send-to-back", true);
					break;
				case View::Type::ALWAYS_ON_TOP:
					{
						wf::json_t tmp;
						tmp["state"] = !target_view->has_data("wm-actions-above");
						call_plugin("wm-actions/set-always-on-top", true, std::move(tmp));
					}
					break;
				case View::Type::STICKY:
					if(toplevel) {
						wf::json_t tmp;
						tmp["state"] = !toplevel->sticky;
						call_plugin("wm-actions/set-sticky", true, std::move(tmp));
					}
					break;
				default:
					break;
			}
		}
		void visit(const Plugin* action) override {
			call_plugin(action->get_action(), true);
		}
		void visit(const Touchpad* action) override {
			auto type = action->get_action_type();
			// needs_refocus = false;
			uint32_t mods = action->get_mods();
			uint32_t fingers = action->fingers;
			set_idle_action([this, type, mods, fingers] () {
				if(mods) {
					uint32_t t = wf::get_current_time();
					keyboard_modifiers(t, mods, WL_KEYBOARD_KEY_STATE_PRESSED);
					input.keyboard_mods(mods, 0, 0);
					ignore_active = mods;
				}
				start_touchpad(type, fingers, wf::get_current_time());
			});
		}
	
	protected:
		
		/* set the action taken by the idle callback;
		 * this automatically handles refocusing if needed */
		template<class CB>
		void set_idle_action(CB&& cb, bool refocus_after_action = true) {
			needs_refocus2 = needs_refocus;
			idle_generate.run_once([this, cb, refocus_after_action] () {
				if(needs_refocus2 && !refocus_after_action) wf::get_core().seat->focus_view(initial_active_view);
				cb();
				if(needs_refocus2 && refocus_after_action) wf::get_core().seat->focus_view(initial_active_view);
				view_unmapped.disconnect();
			});
			needs_refocus = false;
		}
		
		/* load / reload the configuration; also set up a watch for changes */
		void reload_config() {
			ActionDB* actions_tmp = new ActionDB();
			if(actions_tmp) {
				bool config_read = false;
				try {
					std::error_code ec;
					if(std::filesystem::exists(config_file, ec) && std::filesystem::is_regular_file(config_file, ec))
						config_read = actions_tmp->read(config_file, true);
					else {
						std::string config_file_old = config_dir + ActionDB::wstroke_actions_versions[1];
						config_read = actions_tmp->read(config_file_old, true);
					}
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
				inotify_add_watch(inotify_fd, config_file.c_str(), IN_CLOSE_WRITE);
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
		
		/* Determine which corner of a view to start a resize action from. */
		uint32_t get_resize_edges() const {
			const std::string& e = resize_edges;
			if(e == "auto") return 0;
			if(e == "top_left") return WLR_EDGE_TOP | WLR_EDGE_LEFT;
			if(e == "top_right") return WLR_EDGE_TOP | WLR_EDGE_RIGHT;
			if(e == "bottom_left") return WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
			if(e == "bottom_right") return WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
			return 0; // not reached
		}
		
		/* call a plugin activator -- do this from the idle_call, so 
		 * that our grab interface does not get in the way; also take
		 * care of refocusing the original view if needed */
		void call_plugin(const std::string& plugin_activator, bool include_view = false, wf::json_t&& data = wf::json_t()) {
			data["output_id"] = output->get_id();
			if(include_view) data["view_id"] = target_view->get_id();
			set_idle_action([this, plugin_activator, data] () {
				LOGI("Call plugin: ", plugin_activator);
				wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
				repo->call_method(plugin_activator, data);
			});
		}
		
		/* focus the view under the mouse if needed -- no gesture case */
		void check_focus_mouse_view() {
			if(mouse_view) {
				const std::string& mode = focus_mode;
				if(mode == "no_gesture" || mode == "always")
					wf::get_core().default_wm->focus_raise_view(mouse_view);
			}
		}
		
		/* callback when the stroke mouse button is pressed */
		bool start_stroke(int32_t x, int32_t y) {
			if(!actions) return false;
			if(active) {
				LOGW("already active!");
				return false;
			}
			
			/* note: end any previously running stroke action */
			end_touchpad();
			end_ignore();
			
			initial_active_view = wf::get_core().seat->get_active_view();
			if(initial_active_view && initial_active_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
				initial_active_view = nullptr;
			
			mouse_view = wf::get_core().get_cursor_focus_view();
			if(mouse_view && mouse_view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
				mouse_view = nullptr;
			
			target_view = target_mouse ? mouse_view : initial_active_view;
			
			if(target_view) {
				const std::string& app_id = target_view->get_app_id();
				if(actions->exclude_app(app_id)) {
					LOGD("Excluding strokes for app: ", app_id);
					if(initial_active_view != mouse_view) check_focus_mouse_view();
					return false;
				}
			}
			
			/* listen to views being unmapped to handle the case when
			 * initial_active_view or mouse_view disappears while running */
			output->connect(&view_unmapped);
			
			if(!output->activate_plugin(&grab_interface, 0)) {
				LOGE("could not activate");
				view_unmapped.disconnect();
				return false;
			}
			input_grab->grab_input(wf::scene::layer::OVERLAY);
			
			active = true;
			ps.push_back(Stroke::Point{(double)x, (double)y});
			return true;
		}
		
		/* callback when the mouse is moved */
		void handle_input_move(int32_t x, int32_t y) {
			if(ps.size()) {
				const auto& tmp = ps.back();
				/* ignore events without actual movement */
				if(x == tmp.x && y == tmp.y) return;
			}
			Stroke::Point t{(double)x, (double)y};
			if(!is_gesture) {
				float dist = hypot(t.x - ps.front().x, t.y - ps.front().y);
				if(dist > 16.0f) {
					is_gesture = true;
					start_drawing();
					if(target_mouse && target_view && target_view != initial_active_view) {
						const std::string& mode = focus_mode;
						if(mode == "always" || mode == "only_gesture") needs_refocus = false;
						else needs_refocus = true;
						needs_refocus2 = false; /* set this to false, will be set to true if needed later */
						/* raise the view if it should stay focused after the gesture */
						if(needs_refocus) wf::get_core().seat->focus_view(target_view);
						else wf::get_core().default_wm->focus_raise_view(target_view);
					}
				}
			}
			ps.push_back(t);
			if(is_gesture) overlay_node->draw_line(ps[ps.size()-2].x, ps[ps.size()-2].y,
				ps.back().x, ps.back().y);
			if(timeout.is_connected()) {
				timeout.disconnect();
				int timeout_len = end_timeout > 0 ? end_timeout : start_timeout;
				timeout.set_timeout(timeout_len, [this]() { end_stroke(); });
			}
		}
		
		/* start drawing the stroke on the screen */
		void start_drawing() {
			wf::scene::add_front(output->node_for_layer(wf::scene::layer::OVERLAY), overlay_node);
			for(size_t i = 1; i < ps.size(); i++)
				overlay_node->draw_line(ps[i-1].x, ps[i-1].y, ps[i].x, ps[i].y);
		}
		
		/* callback when the mouse button is released */
		void end_stroke() {
			if(!active) return; /* in case the timeout was not disconnected */
			timeout.disconnect();
			ptr_moved = false;
			input_grab->ungrab_input();
			output->deactivate_plugin(&grab_interface);
			if(is_gesture) {
				overlay_node->clear_lines();
				wf::scene::remove_child(overlay_node);
				Stroke stroke(ps);
				/* try to match the stroke, write out match */
				const ActionListDiff<false>* matcher = nullptr;
				if(target_view) {
					const std::string& app_id = target_view->get_app_id();
					LOGD("Target app id: ", app_id);
					matcher = actions->get_action_list(app_id);
				}
				if(!matcher) matcher = actions->get_root();
				
				Ranking rr;
				Action* action = matcher->handle(stroke, &rr);
				if(action) {
					LOGD("Matched stroke: ", rr.name);
					action->visit(this);
				}
				else LOGD("Unmatched stroke");
				if(needs_refocus)
					/* set an "empty" action that will ensure that the original
					 * focused view is refocused (if possible) */
					set_idle_action([](){});
				else if(!needs_refocus2) view_unmapped.disconnect();
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
					input.pointer_button(t, tmp.get_button(), WL_POINTER_BUTTON_STATE_PRESSED);
					input.pointer_button(t, tmp.get_button(), WL_POINTER_BUTTON_STATE_RELEASED);
					output->add_button(initiate, &stroke_initiate);
					view_unmapped.disconnect();
				});
			}
			ps.clear();
			active = false;
		}
		
		/* helpers for the ignore action */
		void end_ignore() {
			if(ignore_active) {
				uint32_t t = wf::get_current_time();
				keyboard_modifiers(t, ignore_active, WL_KEYBOARD_KEY_STATE_RELEASED);
				input.keyboard_mods(0, 0, 0);
				ignore_active = 0;
			}
		}
		
/*		wf::signal_connection_t ignore_key_cb{[this] (wf::signal_data_t *data) {
			auto k = static_cast<wf::input_event_signal<wlr_event_keyboard_key>*>(data);
			if(k->event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
				// ignore modifiers possibly generated by us
				switch(k->event->keycode) {
					case KEY_LEFTSHIFT:
					case KEY_RIGHTSHIFT:
					case KEY_LEFTCTRL:
					case KEY_RIGHTCTRL:
					case KEY_LEFTALT:
					case KEY_RIGHTALT:
					case KEY_LEFTMETA:
					case KEY_RIGHTMETA:
						return;
					default:
						end_ignore();
				}
			}
		}}; */
		
		void start_touchpad(Touchpad::Type type, uint32_t fingers, uint32_t time_msec) {
			touchpad_fingers = fingers;
			switch(type) {
				case Touchpad::Type::SWIPE:
					input.pointer_start_swipe(time_msec, touchpad_fingers);
					break;
				case Touchpad::Type::PINCH:
					input.pointer_start_pinch(time_msec, touchpad_fingers);
					touchpad_last_angle = -1.0 * M_PI / 2.0;
					touchpad_last_scale = 1.0;
					break;
				case Touchpad::Type::NONE:
				case Touchpad::Type::SCROLL:
					/* Note: no action needed for SCROLL */
					break;
			}
			touchpad_active = type;
		}
		
		void end_touchpad(bool cancelled = false) {
			switch(touchpad_active) {
				case Touchpad::Type::SWIPE:
					input.pointer_end_swipe(wf::get_current_time(), cancelled);
					break;
				case Touchpad::Type::PINCH:
					input.pointer_end_pinch(wf::get_current_time(), cancelled);
					break;
				case Touchpad::Type::NONE:
				case Touchpad::Type::SCROLL:
					/* Note: no action needed for SCROLL */
					break;
			}
			touchpad_active = Touchpad::Type::NONE;
		}
		
		wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_raw_pointer_button =
				[=] (wf::input_event_signal<wlr_pointer_button_event> *ev) {
			if(ev->event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
				if(touchpad_active != Touchpad::Type::NONE) {
					next_release_touchpad = true;
					ev->mode = wf::input_event_processing_mode_t::IGNORE;
				}
				else if(ignore_next_own_btn && input.is_own_event_btn(ev->event))
					ev->mode = wf::input_event_processing_mode_t::IGNORE;
			}
			if(ev->event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
				if(next_release_touchpad) {
					ev->mode = wf::input_event_processing_mode_t::IGNORE;
					next_release_touchpad = false;
				}
				else if(ignore_next_own_btn && input.is_own_event_btn(ev->event)) {
					ev->mode = wf::input_event_processing_mode_t::IGNORE;
					ignore_next_own_btn = false;
				}
				end_touchpad();
				end_ignore();
			}
		};
		
		wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> on_raw_pointer_motion =
			[=] (wf::input_event_signal<wlr_pointer_motion_event> *ev) {
			switch(touchpad_active) {
				case Touchpad::Type::NONE:
					return;
				case Touchpad::Type::SCROLL:
					{
						LOGD("Scroll event, dx: ", ev->event->delta_x, ", dy: ", ev->event->delta_y);
						double delta;
						enum WSTROKE_AXIS_ORIENTATION o;
						if(std::abs(ev->event->delta_x) > std::abs(ev->event->delta_y)) {
							delta = ev->event->delta_x;
							o = WSTROKE_AXIS_HORIZONTAL;
						}
						else {
							delta = ev->event->delta_y;
							o = WSTROKE_AXIS_VERTICAL;
						}
						input.pointer_scroll(ev->event->time_msec + 1, 0.2 * delta * touchpad_scroll_sensitivity, o);
					}
					break;
				case Touchpad::Type::SWIPE:
					input.pointer_update_swipe(ev->event->time_msec + 1, touchpad_fingers, ev->event->delta_x, ev->event->delta_y);
					break;
				case Touchpad::Type::PINCH:
					{
						int tmp = touchpad_pinch_sensitivity;
						double sensitivity = tmp > 0 ? tmp : 200.0;
						/* TODO: process angles in a reliable way (so far, it does not work, so we just do zoom based on y-coordinates)
						wf::pointf_t last_pos = {sensitivity * std::cos(touchpad_last_angle), sensitivity * std::sin(touchpad_last_angle)};
						wf::pointf_t new_pos = last_pos;
						new_pos.x += ev->event->delta_x;
						new_pos.y += ev->event->delta_y;
						double new_angle = std::atan2(new_pos.y, new_pos.x);
						double angle_diff = touchpad_last_angle - new_angle;
						if(new_pos.x < 0.0 && last_pos.x < 0.0) {
							if(new_angle < 0.0 && touchpad_last_angle > 0.0) angle_diff -= 2.0 * M_PI;
							else if(new_angle > 0.0 && touchpad_last_angle < 0.0) angle_diff += 2.0 * M_PI;
						}
						double delta_angle_diff = std::abs(last_pos.x * ev->event->delta_x + last_pos.y * ev->event->delta_y) /
							(sensitivity * std::hypot(ev->event->delta_x, ev->event->delta_y));
						if(delta_angle_diff < 0.5) angle_diff = 0.0;
						else touchpad_last_angle = new_angle;
						double scale_factor = (delta_angle_diff > 0.5) ? std::hypot(new_pos.x, new_pos.y) / sensitivity : 1.0;
						touchpad_last_scale *= scale_factor;
						input.pointer_update_pinch(time_msec, 2, 0.0, 0.0, touchpad_last_scale, -180.0 * angle_diff / M_PI);
						*/
						double scale_factor = (sensitivity - ev->event->delta_y) / sensitivity;
						if(scale_factor > 0.0) {
							touchpad_last_scale *= scale_factor;
							uint32_t time_msec = ev->event->time_msec + 1;
							input.pointer_update_pinch(time_msec, touchpad_fingers, 0.0, 0.0, touchpad_last_scale, 0.0);
						}
					}
					break;
			}
			ev->mode = wf::input_event_processing_mode_t::IGNORE;
		};
		
		/* callback to cancel a stroke */
		void cancel_stroke() {
			input_grab->ungrab_input();
			output->deactivate_plugin(&grab_interface);
			end_touchpad(true);
			end_ignore();
			ps.clear();
			if(is_gesture) {
				overlay_node->clear_lines();
				wf::scene::remove_child(overlay_node);
				is_gesture = false;
			}
			if(target_mouse) wf::get_core().seat->focus_view(initial_active_view);
			active = false;
			ptr_moved = false;
			timeout.disconnect();
			view_unmapped.disconnect();
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
};

constexpr std::array<std::pair<enum wlr_keyboard_modifier, uint32_t>, 4> wstroke::mod_map;

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wstroke>)

