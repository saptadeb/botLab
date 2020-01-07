#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <assert.h>
#include <stdio.h>

#include "glcontext.h"

typedef struct
{
    Display * dpy;
    GLXContext glc;
} state_t;

glcontext_t *glcontext_X11_create()
{
    glcontext_t *glc = (glcontext_t*) calloc(1, sizeof(glcontext_t));

    state_t * state  = calloc(1,sizeof(state_t));
    glc->impl = state;

    Display *dpy = XOpenDisplay(NULL);
    state->dpy = dpy;
    assert(dpy != NULL);

    Window root = DefaultRootWindow(dpy);
    // These GL settings are unimportant; see glcontext.c for the allocation of the FBO.
    GLint att[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };
    XVisualInfo *vi = glXChooseVisual(dpy, 0, att);
    Colormap cmap = XCreateColormap(dpy, root, vi->visual, AllocNone);

    XSetWindowAttributes    swa;
    memset(&swa, 0, sizeof(XSetWindowAttributes));
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask;

    Window win = XCreateWindow(dpy, root, 0, 0, 100, 100, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);

    GLXContext _glc = glXCreateContext(dpy, vi, NULL, GL_TRUE);
    state->glc = _glc;
    XFree(vi);
    glXMakeCurrent(dpy, win, _glc);

    return glc;
}

void glcontext_X11_destroy(glcontext_t * glc)
{
    state_t * state = glc->impl;

    //printf("state 0x%p, dpy 0x%p glc %ld\n", (void*)state, (void*)state->dpy, (uint64_t)state->glc);
    glXDestroyContext(state->dpy, state->glc);

    XCloseDisplay(state->dpy);

    free(state);
    free(glc);
}
