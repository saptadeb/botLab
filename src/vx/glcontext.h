#ifndef _GLCONTEXT_H
#define _GLCONTEXT_H

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct glcontext glcontext_t;
struct glcontext
{
    void *impl;
};

typedef struct gl_fbo gl_fbo_t;
struct gl_fbo
{
    glcontext_t *glc;

    GLuint fbo_id; // frame buffer object id
    GLuint depth_id;
    GLuint color_id;
};

glcontext_t *glcontext_X11_create();
void glcontext_X11_destroy(glcontext_t * glc);

gl_fbo_t *gl_fbo_create(glcontext_t *glc, int width, int height);
void gl_fbo_bind(gl_fbo_t *fbo);
void gl_fbo_destroy(gl_fbo_t *fbo);

#ifdef __cplusplus
}
#endif

#endif
