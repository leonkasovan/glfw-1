//========================================================================
// OpenGL ES 2.0 triangle example
// Copyright (c) Camilla Löwy <elmindreda@glfw.org>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================

#define GLAD_GLES2_IMPLEMENTATION
#include <glad/gles2.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "linmath.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

typedef struct Vertex {
    vec2 pos;
    vec3 col;
} Vertex;

static const Vertex vertices[3] =
{
    { { -0.6f, -0.4f }, { 1.f, 0.f, 0.f } },
    { {  0.6f, -0.4f }, { 0.f, 1.f, 0.f } },
    { {   0.f,  0.6f }, { 0.f, 0.f, 1.f } }
};

static const char* vertex_shader_text =
"#version 310 es\n"
"precision mediump float;\n"
"#if __VERSION__ >= 130\n"
"#define COMPAT_VARYING out\n"
"#define COMPAT_ATTRIBUTE in\n"
"#define COMPAT_TEXTURE texture\n"
"#else\n"
"#define COMPAT_VARYING varying \n"
"#define COMPAT_ATTRIBUTE attribute \n"
"#define COMPAT_TEXTURE texture2D\n"
"#endif\n"
"uniform mat4 MVP;\n"
"COMPAT_ATTRIBUTE vec3 vCol;\n"
"COMPAT_ATTRIBUTE vec2 vPos;\n"
"COMPAT_VARYING vec3 color;\n"
"void main()\n"
"{\n"
"    gl_Position = MVP * vec4(vPos, 0.0, 1.0);\n"
"    color = vCol;\n"
"}\n";

static const char* fragment_shader_text =
"#version 310 es\n"
"precision mediump float;\n"
"#if __VERSION__ >= 130\n"
"#define COMPAT_VARYING in\n"
"#define COMPAT_TEXTURE texture\n"
"out vec4 FragColor;\n"
"#else\n"
"#define COMPAT_VARYING varying\n"
"#define FragColor gl_FragColor\n"
"#define COMPAT_TEXTURE texture2D\n"
"#endif\n"
"COMPAT_VARYING vec3 color;\n"
"void main()\n"
"{\n"
"    FragColor = vec4(color, 1.0);\n"
"}\n";

// Function to Compile Shader
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        printf("Error compiling shader: %s\n", log);
        exit(EXIT_FAILURE);
    } else {
        // printf("compiling shader ok: %d\n", shader);
    }

    return shader;
}

static void error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int main(void) {
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    GLFWwindow* window = glfwCreateWindow(640, 480, "OpenGL ES 2.0 Triangle (EGL)", NULL, NULL);
    if (!window) {
        // glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_NATIVE_CONTEXT_API);
        // printf("examples/triangle-opengles.c:%d: GLFW_NATIVE_CONTEXT_API\n", __LINE__);
        // window = glfwCreateWindow(640, 480, "OpenGL ES 2.0 Triangle", NULL, NULL);
        // if (!window) {
        //     printf("glfwCreateWindow2 failed too\n");
        //     glfwTerminate();
        //     // exit(EXIT_FAILURE);
        //     return GLFW_FALSE;
        // } else {
        //     printf("glfwCreateWindow2 succed\n");
        // }
        return GLFW_FALSE;
    } else {
        // printf("glfwCreateWindow1 succed\n");
    }
    // printf("examples/triangle-opengles.c: %d\n", __LINE__);

    glfwSetKeyCallback(window, key_callback);
    // printf("examples/triangle-opengles.c: %d\n", __LINE__);

    glfwMakeContextCurrent(window);
    // printf("examples/triangle-opengles.c: %d\n", __LINE__);
    int version = gladLoadGLES2(glfwGetProcAddress);
    // printf("examples/triangle-opengles.c: version=%d\n", version);
    glfwSwapInterval(1);
    // printf("examples/triangle-opengles.c: %d\n", __LINE__);

    GLuint vertex_buffer;
    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    const GLuint vertex_shader = compileShader(GL_VERTEX_SHADER, vertex_shader_text);
    // const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    // glShaderSource(vertex_shader, 1, &vertex_shader_text, NULL);
    // glCompileShader(vertex_shader);

    const GLuint fragment_shader = compileShader(GL_FRAGMENT_SHADER, fragment_shader_text);
    // const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    // glShaderSource(fragment_shader, 1, &fragment_shader_text, NULL);
    // glCompileShader(fragment_shader);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    const GLint mvp_location = glGetUniformLocation(program, "MVP");
    const GLint vpos_location = glGetAttribLocation(program, "vPos");
    const GLint vcol_location = glGetAttribLocation(program, "vCol");

    glEnableVertexAttribArray(vpos_location);
    glEnableVertexAttribArray(vcol_location);
    glVertexAttribPointer(vpos_location, 2, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*) offsetof(Vertex, pos));
    glVertexAttribPointer(vcol_location, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*) offsetof(Vertex, col));
    // printf("examples/triangle-opengles.c: %d\n", __LINE__);
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    printf("window=%dx%d\n", width, height);
    glViewport(0, 0, 480, 640);
    while (!glfwWindowShouldClose(window)) {
        const float ratio = width / (float) height;
        glClear(GL_COLOR_BUFFER_BIT);

        mat4x4 m, p, mvp;
        mat4x4_identity(m);
        mat4x4_rotate_Z(m, m, (float) glfwGetTime());
        mat4x4_ortho(p, -ratio, ratio, -1.f, 1.f, 1.f, -1.f);
        mat4x4_mul(mvp, p, m);

        glUseProgram(program);
        glUniformMatrix4fv(mvp_location, 1, GL_FALSE, (const GLfloat*) &mvp);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // printf("examples/triangle-opengles.c: %d\n", __LINE__);
        glfwSwapBuffers(window);
        glfwPollEvents();
        // printf("examples/triangle-opengles.c: %d\n", __LINE__);
    }

    glfwDestroyWindow(window);

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

