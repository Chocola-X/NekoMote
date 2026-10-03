// NekoMote preview: minimal GLFW + OpenGL 3.3 core window.
//
// P0 scope: open a window, load the handful of GL entry points needed to clear
// the framebuffer, print GPU/driver diagnostics to stdout, render a solid
// diagnostic color, and shut down cleanly on close/ESC. No model, no assets.
//
// GL function loading is done by hand for the few entries we need, to keep P0
// free of an extra loader dependency. A real loader (glad) is introduced in P2
// when actual draw calls are needed.

#include <nekomote/types.hpp>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>

namespace {

// GL enums used here. Spelled out so we don't need a full GL header just for
// the diagnostic path.
constexpr unsigned int kGlColorBufferBit = 0x4000;
constexpr unsigned int kGlVendor = 0x1F00;
constexpr unsigned int kGlRenderer = 0x1F01;
constexpr unsigned int kGlVersion = 0x1F02;
constexpr unsigned int kGlShadingLanguageVersion = 0x8B8C;

using GlClearFn = void (*)(unsigned int);
using GlClearColorFn = void (*)(float, float, float, float);
using GlViewportFn = void (*)(int, int, int, int);
using GlGetStringFn = const unsigned char* (*)(unsigned int);

struct GlProcs {
    GlClearFn clear{};
    GlClearColorFn clear_color{};
    GlViewportFn viewport{};
    GlGetStringFn get_string{};
};

bool load_gl_procs(GlProcs& out) {
    out.clear = reinterpret_cast<GlClearFn>(glfwGetProcAddress("glClear"));
    out.clear_color = reinterpret_cast<GlClearColorFn>(glfwGetProcAddress("glClearColor"));
    out.viewport = reinterpret_cast<GlViewportFn>(glfwGetProcAddress("glViewport"));
    out.get_string = reinterpret_cast<GlGetStringFn>(glfwGetProcAddress("glGetString"));
    return out.clear && out.clear_color && out.viewport && out.get_string;
}

void print_diagnostics(const GlProcs& gl) {
    const char* vendor = reinterpret_cast<const char*>(gl.get_string(kGlVendor));
    const char* renderer = reinterpret_cast<const char*>(gl.get_string(kGlRenderer));
    const char* version = reinterpret_cast<const char*>(gl.get_string(kGlVersion));
    const char* glsl = reinterpret_cast<const char*>(gl.get_string(kGlShadingLanguageVersion));

    std::printf("=== NekoMote preview GPU diagnostics ===\n");
    std::printf("GL_VENDOR:   %s\n", vendor ? vendor : "(null)");
    std::printf("GL_RENDERER: %s\n", renderer ? renderer : "(null)");
    std::printf("GL_VERSION:  %s\n", version ? version : "(null)");
    std::printf("GLSL:        %s\n", glsl ? glsl : "(null)");
    std::printf("Target: OpenGL 3.3 core. ESC or close window to exit.\n");
    std::fflush(stdout);
}

int run_window() {
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "nekomote_preview: glfw error %d: %s\n", code, description);
    });

    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "nekomote_preview: glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    // Prefer EGL over GLX: GLX is unavailable on some X servers (headless /
    // container setups without a GLX module). EGL also works under Mesa
    // software rendering (LIBGL_ALWAYS_SOFTWARE=1) without /dev/dri.
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);

    GLFWwindow* window = glfwCreateWindow(800, 600, "NekoMote preview", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr,
                     "nekomote_preview: failed to create a GLFW window.\n"
                     "This usually means no display server or no GL 3.3 capable context.\n");
        glfwTerminate();
        return 2;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    GlProcs gl{};
    if (!load_gl_procs(gl)) {
        std::fprintf(stderr, "nekomote_preview: failed to load GL entry points\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 3;
    }

    print_diagnostics(gl);

    // Confirm the core library is usable from the preview TU (boundary check).
    nekomote::IdGenerator<nekomote::NodeIdTag> gen;
    nekomote::NodeId first = gen.next();
    if (!first.valid()) {
        std::fprintf(stderr, "nekomote_preview: core id generator broken\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 4;
    }

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        int fb_w = 0;
        int fb_h = 0;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        gl.viewport(0, 0, fb_w, fb_h);

        // Diagnostic clear color: deep purple, distinct from any desktop default.
        gl.clear_color(0.10f, 0.06f, 0.16f, 1.0f);
        gl.clear(kGlColorBufferBit);

        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // Reserved for future flags (e.g. --asset-root, --model). P0 ignores them.
    (void)argc;
    (void)argv;
    return run_window();
}
