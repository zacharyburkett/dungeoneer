#include <stdio.h>
#include <string.h>

#include "core.h"
#include "nuklear_features.h"

#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_GLFW_GL2_IMPLEMENTATION
#include "nuklear_glfw_gl2.h"

int main(void)
{
    GLFWwindow *window;
    struct nk_context *ctx;
    struct nk_font_atlas *atlas;
    dg_nuklear_app_t app;

    if (glfwInit() == 0) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    window = glfwCreateWindow(1400, 900, "dungeoneer (nuklear demo)", NULL, NULL);
    if (window == NULL) {
        fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    ctx = nk_glfw3_init(window, NK_GLFW3_INSTALL_CALLBACKS);
    if (ctx == NULL) {
        fprintf(stderr, "nk_glfw3_init failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    nk_glfw3_font_stash_begin(&atlas);
    nk_glfw3_font_stash_end();
    (void)atlas;

    dg_nuklear_app_init(&app);

    while (!glfwWindowShouldClose(window)) {
        int window_width;
        int window_height;
        int fb_width;
        int fb_height;

        glfwPollEvents();
        nk_glfw3_new_frame();

        glfwGetWindowSize(window, &window_width, &window_height);
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        dg_nuklear_app_draw(ctx, &app, window_width, window_height);

        glViewport(0, 0, fb_width, fb_height);
        glClearColor(0.11f, 0.13f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        nk_glfw3_render(NK_ANTI_ALIASING_ON);

        glfwSwapBuffers(window);
    }

    dg_nuklear_app_shutdown(&app);
    nk_glfw3_shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
