#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
        
        PreStroke ps;
        ActionDB actions;
        input_headless input;
        wf::wl_idle_call idle_generate;
        wayfire_view active_view;
        
        bool active = false;
        bool is_gesture = false;
        
        static constexpr std::array<std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>, 10> modifier_match = {
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::SHIFT_MASK, WLR_MODIFIER_SHIFT),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::LOCK_MASK, WLR_MODIFIER_CAPS),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::CONTROL_MASK, WLR_MODIFIER_CTRL),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::MOD1_MASK, WLR_MODIFIER_ALT),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::MOD2_MASK, WLR_MODIFIER_MOD2),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::MOD3_MASK, WLR_MODIFIER_MOD3),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::MOD4_MASK, WLR_MODIFIER_LOGO),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::MOD5_MASK, WLR_MODIFIER_MOD5),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::META_MASK, WLR_MODIFIER_ALT),
	std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>(Gdk::ModifierType::SUPER_MASK, WLR_MODIFIER_LOGO)
};
        
    public:
        wayfire_easystroke() {
            stroke_initiate = [=](uint32_t button, int32_t x, int32_t y) {
                return start_stroke(x, y);
            };
        }
        
        void init() override {
			std::string config_dir = getenv("HOME");
			config_dir += "/.config/wstroke/";
			actions.read(config_dir);
			
			input.init();
			
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
        }
        
        /* visitor interface for carrying out actions */
        void visit(const Command* action) override {
			LOGW("Command action not implemented!");
		}
		void visit(const SendKey* action) override {
			LOGW("SendKey action not implemented!");
		}
		void visit(const SendText* action) override {
			LOGW("SendText action not implemented!");
		}
		void visit(const Scroll* action) override {
			LOGW("Scroll action not implemented!");
		}
		void visit(const Ignore* action) override {
			auto ignore_mods = action->get_mods();
			uint32_t mods_latched = 0;
			for(auto p : modifier_match) if(ignore_mods & p.first) mods_latched |= p.second;
			input.keyboard_mods(0, mods_latched, 0);
		}
		void visit(const Button* action) override {
			LOGW("Button action not implemented!");
		}
		void visit(const Misc* action) override {
			switch(action->get_action_type()) {
				case Misc::Type::CLOSE:
					if(active_view) active_view->close();
					break;
				case Misc::Type::MINIMIZE:
					if(active_view) active_view->minimize_request(true);
					break;
				default:
					break;
			}
		}
    
    protected:
        /* callback when the stroke mouse button is pressed */
        bool start_stroke(int32_t x, int32_t y) {
            if(active) {
                LOGW("start_stroke() already active!");
                return false;
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
				auto matcher = actions.get_root(); // TODO: match based on active window!
				RRanking rr;
				RAction action = matcher->handle(stroke, rr);
				if(action) {
					const auto& type = action->get_type();
					const auto& label = action->get_label();
					LOGI("Matched stroke: ", type, " -- ", label);
					active_view = output->get_active_view();
					action->run(this);
				}
				else LOGI("Unmatched stroke");
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
            active = false;
            is_gesture = false;
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
			auto ortho = glm::ortho(0.0f, (float)dim.width, (float)dim.height, 0.0f);
			
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

constexpr std::array<std::pair<Gdk::ModifierType, enum wlr_keyboard_modifier>, 10> wayfire_easystroke::modifier_match;

DECLARE_WAYFIRE_PLUGIN(wayfire_easystroke)

