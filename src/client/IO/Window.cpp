//////////////////////////////////////////////////////////////////////////////
// This file is part of the Journey MMORPG client                           //
// Copyright © 2015-2016 Daniel Allendorf                                   //
//                                                                          //
// This program is free software: you can redistribute it and/or modify     //
// it under the terms of the GNU Affero General Public License as           //
// published by the Free Software Foundation, either version 3 of the       //
// License, or (at your option) any later version.                          //
//                                                                          //
// This program is distributed in the hope that it will be useful,          //
// but WITHOUT ANY WARRANTY; without even the implied warranty of           //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            //
// GNU Affero General Public License for more details.                      //
//                                                                          //
// You should have received a copy of the GNU Affero General Public License //
// along with this program.  If not, see <http://www.gnu.org/licenses/>.    //
//////////////////////////////////////////////////////////////////////////////
#include "Window.h"

#ifdef MS_PLATFORM_WASM
#include <emscripten.h>
#endif

#include "UI.h"

#include "../Console.h"
#include "../Constants.h"
#include "../Configuration.h"
#include "../Graphics/GraphicsGL.h"

#include <cstdlib>

namespace jrc
{
    namespace
    {
        constexpr double DOUBLECLICK_MAX_INTERVAL_SECONDS = 0.35;
        constexpr int16_t DOUBLECLICK_MAX_DISTANCE_PIXELS = 4;

        struct DoubleclickState
        {
            double last_left_press_time = -1.0;
            Point<int16_t> last_left_press_position = {};
        };

        DoubleclickState doubleclick_state;

        // GLFW does not emit double-click events, so synthesize them from
        // successive left-button presses that happen close together.
        bool is_doubleclick(Point<int16_t> position)
        {
            double now = glfwGetTime();
            double elapsed = now - doubleclick_state.last_left_press_time;
            bool in_time_window =
                doubleclick_state.last_left_press_time >= 0.0 &&
                elapsed >= 0.0 &&
                elapsed <= DOUBLECLICK_MAX_INTERVAL_SECONDS;

            Point<int16_t> previous = doubleclick_state.last_left_press_position;
            bool in_distance_window =
                std::abs(position.x() - previous.x()) <= DOUBLECLICK_MAX_DISTANCE_PIXELS &&
                std::abs(position.y() - previous.y()) <= DOUBLECLICK_MAX_DISTANCE_PIXELS;

            doubleclick_state.last_left_press_time = now;
            doubleclick_state.last_left_press_position = position;
            return in_time_window && in_distance_window;
        }
    }

    Window::Window()
    {
        context = nullptr;
        glwnd = nullptr;
        opacity = 1.0f;
        opcstep = 0.0f;
        f11_down = false;
    }

    Window::~Window()
    {
        glfwTerminate();
    }

    void error_callback(int no, const char* description)
    {
        Console::get()
            .print("glfw error: " + std::string(description) + " (" + std::to_string(no) + ")");
    }

    void key_callback(GLFWwindow*, int key, int, int action, int)
    {
        UI::get().send_key(key, action != GLFW_RELEASE);
    }

    void mousekey_callback(GLFWwindow* window, int button, int action, int)
    {
        switch (button)
        {
        case GLFW_MOUSE_BUTTON_LEFT:
            switch (action)
            {
            case GLFW_PRESS:
            {
                double xpos = 0.0;
                double ypos = 0.0;
                glfwGetCursorPos(window, &xpos, &ypos);
                Point<int16_t> position(
                    static_cast<int16_t>(xpos),
                    static_cast<int16_t>(ypos)
                );

                UI::get().send_cursor(true);
                if (is_doubleclick(position))
                {
                    UI::get().cancel_drag();
                    UI::get().doubleclick();
                }
                break;
            }
            case GLFW_RELEASE:
                UI::get().send_cursor(false);
                break;
            default:
                break;
            }
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            switch (action)
            {
            case GLFW_PRESS:
                UI::get().rightclick();
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    void cursor_callback(GLFWwindow*, double xpos, double ypos)
    {
        auto x = static_cast<int16_t>(xpos);
        auto y = static_cast<int16_t>(ypos);

        // Everything is drawn VIEWYOFFSET pixels lower than its coordinate says,
        // and layouts are authored around that. Pointer positions arrive in raw
        // screen pixels, so they have to be moved into the same space or the
        // drawn cursor trails the real one and every control is clickable
        // slightly away from where it appears.
        y = static_cast<int16_t>(y - Constants::VIEWYOFFSET);

        Point<int16_t> pos = Point<int16_t>(x, y);
        UI::get().send_cursor(pos);
    }

    void scroll_callback(GLFWwindow*, double, double yoffset)
    {
        UI::get().send_scroll(yoffset);
    }

    void focus_callback(GLFWwindow*, int focused)
    {
        UI::get().send_focus(focused);
    }

    void close_callback(GLFWwindow* window)
    {
        UI::get().send_close();
        glfwSetWindowShouldClose(window, GL_FALSE);
    }

    // Internal viewport the client last asked for. Browser resizes only change
    // how the canvas is stretched by css, so the render surface must follow this
    // rather than whatever size the canvas element reports.
    int16_t requested_view_width = Constants::VIEWWIDTH;
    int16_t requested_view_height = Constants::VIEWHEIGHT;

    void framebuffer_size_callback(GLFWwindow*, int width, int height)
    {
        if (width > 0 && height > 0)
        {
#ifdef MS_PLATFORM_WASM
            Constants::set_viewsize(requested_view_width, requested_view_height);
            glViewport(0, 0, requested_view_width, requested_view_height);
#else
            Constants::set_viewsize(width, height);
            glViewport(0, 0, width, height);
#endif
            GraphicsGL::get().set_screensize(
                Constants::viewwidth(),
                Constants::viewheight()
            );
        }
    }

    Error Window::init()
    {
        fullscreen = Setting<Fullscreen>::get().load();

        if (!glfwInit())
        {
            return Error::GLFW;
        }

#ifdef MS_PLATFORM_WASM
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
        glwnd = glfwCreateWindow(
            Constants::VIEWWIDTH,
            Constants::VIEWHEIGHT,
            "Journey",
            fullscreen ? glfwGetPrimaryMonitor() : nullptr,
            nullptr
        );
        if (!glwnd) {
            return Error::WINDOW;
        }
        glfwMakeContextCurrent(glwnd);
        glfwSetErrorCallback(error_callback);
#else
        glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
        context = glfwCreateWindow(1, 1, "", nullptr, nullptr);
        glfwMakeContextCurrent(context);
        glfwSetErrorCallback(error_callback);
        glfwWindowHint(GLFW_VISIBLE, GL_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
#endif

        if (Error error = GraphicsGL::get().init())
        {
            return error;
        }

        return initwindow();
    }

    Error Window::initwindow()
    {
#ifndef MS_PLATFORM_WASM
        if (glwnd)
        {
            glfwDestroyWindow(glwnd);
        }

        glwnd = glfwCreateWindow(
            Constants::VIEWWIDTH,
            Constants::VIEWHEIGHT,
            "Journey",
            fullscreen ? glfwGetPrimaryMonitor() : nullptr,
            context
        );

        if (!glwnd)
        {
            return Error::WINDOW;
        }

        glfwMakeContextCurrent(glwnd);
#endif
#ifndef MS_PLATFORM_WASM
        bool vsync = Setting<VSync>::get().load();
        glfwSwapInterval(vsync ? 1 : 0);
#endif

        glViewport(0, 0, Constants::VIEWWIDTH, Constants::VIEWHEIGHT);
#ifndef MS_PLATFORM_WASM
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();

        glfwSetInputMode(glwnd, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        glfwSetInputMode(glwnd, GLFW_STICKY_KEYS, 1);
#endif
        glfwSetKeyCallback(glwnd, key_callback);
        glfwSetMouseButtonCallback(glwnd, mousekey_callback);
        glfwSetCursorPosCallback(glwnd, cursor_callback);
        glfwSetWindowFocusCallback(glwnd, focus_callback);
        glfwSetScrollCallback(glwnd, scroll_callback);
        glfwSetWindowCloseCallback(glwnd, close_callback);
        glfwSetFramebufferSizeCallback(glwnd, framebuffer_size_callback);

        int32_t framebuffer_width = 0;
        int32_t framebuffer_height = 0;
        glfwGetFramebufferSize(glwnd, &framebuffer_width, &framebuffer_height);
        if (framebuffer_width > 0 && framebuffer_height > 0)
        {
            framebuffer_size_callback(glwnd, framebuffer_width, framebuffer_height);
        }

        GraphicsGL::get().reinit();

#ifdef MS_PLATFORM_WASM
        // Keep browser default-prevention rules configurable from JS so the input list is easy to extend.
        EM_ASM({
            window.MapleWasmInputGuard = window.MapleWasmInputGuard || {};
            var guard = window.MapleWasmInputGuard;

            if (!Array.isArray(guard.preventedKeys)) {
                guard.preventedKeys =
                    "Tab|Alt|Escape|ArrowLeft|ArrowUp|ArrowRight|ArrowDown|F11".split("|");
            }

            if (!Array.isArray(guard.preventedMouseButtons)) {
                // Right mouse button.
                guard.preventedMouseButtons = [];
                guard.preventedMouseButtons.push(2);
            }

            if (!guard.installed) {
                var preventKeyDefaults = function(event) {
                    if (guard.preventedKeys.indexOf(event.key) !== -1) {
                        event.preventDefault();
                    }
                };

                var preventMouseDefaults = function(event) {
                    if (guard.preventedMouseButtons.indexOf(event.button) !== -1) {
                        event.preventDefault();
                    }
                };

                window.addEventListener("keydown", preventKeyDefaults, false);
                window.addEventListener("keyup", preventKeyDefaults, false);
                window.addEventListener("mousedown", preventMouseDefaults, false);
                window.addEventListener("contextmenu", preventMouseDefaults, false);
                guard.installed = true;
            }
        });

        // Ask JS side to sync canvas size with the current viewport/fullscreen state.
        EM_ASM({
            if (window.MapleWasmUI && window.MapleWasmUI.applyScale) {
                window.MapleWasmUI.applyScale();
            }
        });
#endif
        return Error::NONE;
    }

    bool Window::not_closed() const
    {
        return glfwWindowShouldClose(glwnd) == 0;
    }

    void Window::update()
    {
        updateopc();
    }

    void Window::updateopc()
    {
        if (opcstep != 0.0f)
        {
            opacity += opcstep;

            if (opacity >= 1.0f)
            {
                opacity = 1.0f;
                opcstep = 0.0f;
            }
            else if (opacity <= 0.0f)
            {
                opacity = 0.0f;
                opcstep = -opcstep;

                fadeprocedure();
            }
        }
    }

    void Window::set_viewsize(int16_t width, int16_t height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }
        if (width == Constants::viewwidth() && height == Constants::viewheight())
        {
            return;
        }

        requested_view_width = width;
        requested_view_height = height;
        Constants::set_viewsize(width, height);

        // Glfw has to be told too, and not only the page. Pointer positions are
        // reported after being scaled from the window size glfw has on record
        // into the render surface, so leaving that at the old size drags the
        // cursor further from the real pointer the closer it gets to the bottom
        // right of a canvas that has since grown.
        glfwSetWindowSize(glwnd, width, height);

#ifdef MS_PLATFORM_WASM
        // Resizing the window lets the browser layer restyle the canvas, so the
        // page redoes its own aspect ratio fit afterwards and stays in charge.
        EM_ASM({
            if (window.MapleWasmUI && window.MapleWasmUI.setGameSize) {
                window.MapleWasmUI.setGameSize($0, $1);
            }
        }, width, height);
#endif

        glViewport(0, 0, width, height);
        GraphicsGL::get().set_screensize(width, height);
    }

    void Window::check_events()
    {
#ifdef MS_PLATFORM_WASM
        int32_t f11state = glfwGetKey(glwnd, GLFW_KEY_F11);
        if (f11state == GLFW_PRESS && !f11_down)
        {
            fullscreen = !fullscreen;
            EM_ASM({
                const canvas = Module && Module.canvas ? Module.canvas : null;
                if (!canvas) {
                    return;
                }

                const isCanvasFullscreen = document.fullscreenElement === canvas;
                const next = !isCanvasFullscreen;

                if (next) {
                    canvas.requestFullscreen({ navigationUI: "hide" }).catch(() => {});
                } else if (document.fullscreenElement) {
                    document.exitFullscreen().catch(() => {});
                }

                if (window.MapleWasmUI && window.MapleWasmUI.applyScale) {
                    setTimeout(window.MapleWasmUI.applyScale, 0);
                }
            });
        }
        f11_down = (f11state == GLFW_PRESS);
#else
        int32_t f11state = glfwGetKey(glwnd, GLFW_KEY_F11);
        if (f11state == GLFW_PRESS && !f11_down)
        {
            fullscreen = !fullscreen;
            initwindow();
        }
        f11_down = (f11state == GLFW_PRESS);
#endif
        glfwPollEvents();
    }

    void Window::begin() const
    {
        GraphicsGL::get().clearscene();
    }

    void Window::end() const
    {
        GraphicsGL::get().flush(opacity);
        glfwSwapBuffers(glwnd);
    }

    void Window::fadeout(float step, std::function<void()> fadeproc)
    {
        opcstep = -step;
        fadeprocedure = std::move(fadeproc);
    }

    void Window::setclipboard(const std::string& text) const
    {
        glfwSetClipboardString(glwnd, text.c_str());
    }

    std::string Window::getclipboard() const
    {
        const char* text = glfwGetClipboardString(glwnd);
        return text ? text : "";
    }
}
