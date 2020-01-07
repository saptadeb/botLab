#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>
#include <GL/glx.h>

#include "glcontext.h"

// you create as glcontext_t using a platform-specific implementation.
// Once created, you use these functions to setup and manipulate FBOs.

// Note: will bind the new FBO.
gl_fbo_t *gl_fbo_create(glcontext_t *glc, int width, int height)
{
    gl_fbo_t *fbo = (gl_fbo_t*) calloc(1, sizeof(gl_fbo_t));

    fbo->glc = glc;
    glGenFramebuffers(1, &fbo->fbo_id);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo_id);

    // set up depth buffer
    glGenRenderbuffers(1, &fbo->depth_id);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo->depth_id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo->depth_id);

    glGenRenderbuffers(1, &fbo->color_id);
    glBindRenderbuffer(GL_RENDERBUFFER, fbo->color_id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fbo->color_id);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); //fbo->fbo_id);
    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        printf(__FILE__ " Error creating frame buffer. %d 0x%x\n",status,status);
        return NULL;
    }

    return fbo;
}

void gl_fbo_bind(gl_fbo_t *fbo)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo_id);
}

void gl_fbo_destroy(gl_fbo_t *fbo)
{
    glDeleteRenderbuffers(1, &fbo->color_id);
    glDeleteRenderbuffers(1, &fbo->depth_id);
    glDeleteFramebuffers(1, &fbo->fbo_id);

    free(fbo);
}
