#include <stdio.h>
#include <string.h>

#include "core.h"
#include "nuklear_features.h"

#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_GLFW_GL2_IMPLEMENTATION
#include "nuklear_glfw_gl2.h"

typedef struct dg_glfw_preview_renderer {
    GLuint texture;
    int width;
    int height;
} dg_glfw_preview_renderer_t;

static bool dg_glfw_preview_upload_rgba8(
    void *user_data,
    int width,
    int height,
    const unsigned char *pixels,
    struct nk_image *out_image
)
{
    dg_glfw_preview_renderer_t *renderer;

    if (user_data == NULL || pixels == NULL || out_image == NULL || width <= 0 || height <= 0) {
        return false;
    }

    renderer = (dg_glfw_preview_renderer_t *)user_data;
    if (renderer->texture == 0u) {
        glGenTextures(1, &renderer->texture);
        if (renderer->texture == 0u) {
            return false;
        }
    }

    glBindTexture(GL_TEXTURE_2D, renderer->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (renderer->width != width || renderer->height != height) {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            (GLsizei)width,
            (GLsizei)height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels
        );
        renderer->width = width;
        renderer->height = height;
    } else {
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            (GLsizei)width,
            (GLsizei)height,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels
        );
    }

    *out_image = nk_image_id((int)renderer->texture);
    return true;
}

int main(void)
{
    GLFWwindow *window;
    struct nk_context *ctx;
    struct nk_font_atlas *atlas;
    dg_nuklear_app_t app;
    dg_glfw_preview_renderer_t preview_renderer;
    dg_nuklear_preview_renderer_t core_preview_renderer;

    if (glfwInit() == 0) {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);

    window = glfwCreateWindow(1400, 900, "dungeoneer editor", NULL, NULL);
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
    preview_renderer = (dg_glfw_preview_renderer_t){0};
    core_preview_renderer.user_data = &preview_renderer;
    core_preview_renderer.upload_rgba8 = dg_glfw_preview_upload_rgba8;

    while (!glfwWindowShouldClose(window)) {
        int window_width;
        int window_height;
        int fb_width;
        int fb_height;

        glfwPollEvents();
        nk_glfw3_new_frame();

        glfwGetWindowSize(window, &window_width, &window_height);
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        dg_nuklear_app_draw(
            ctx,
            &app,
            window_width,
            window_height,
            &core_preview_renderer
        );

        glViewport(0, 0, fb_width, fb_height);
        glClearColor(0.11f, 0.13f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        nk_glfw3_render(NK_ANTI_ALIASING_ON);

        glfwSwapBuffers(window);
    }

    if (preview_renderer.texture != 0u) {
        glDeleteTextures(1, &preview_renderer.texture);
    }
    dg_nuklear_app_shutdown(&app);
    nk_glfw3_shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
