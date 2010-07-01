/*
 *  gstvaapiutils_glx.h - GLX utilties
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_UTILS_GLX_H
#define GST_VAAPI_UTILS_GLX_H

#include "config.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <glib/gtypes.h>

#if GLX_GLXEXT_VERSION < 18
typedef void (*PFNGLXBINDTEXIMAGEEXTPROC)(Display *, GLXDrawable, int, const int *);
typedef void (*PFNGLXRELEASETEXIMAGEEXTPROC)(Display *, GLXDrawable, int);
#endif

#if GLX_GLXEXT_VERSION < 27
/* XXX: this is not exactly that version but this is the only means to
   make sure we have the correct <GL/glx.h> with those signatures */
typedef GLXPixmap (*PFNGLXCREATEPIXMAPPROC)(Display *, GLXFBConfig, Pixmap, const int *);
typedef void (*PFNGLXDESTROYPIXMAPPROC)(Display *, GLXPixmap);
#endif

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING GL_FRAMEBUFFER_BINDING_EXT
#endif

const char *
gl_get_error_string(GLenum error)
    attribute_hidden;

void
gl_purge_errors(void)
    attribute_hidden;

gboolean
gl_check_error(void)
    attribute_hidden;

gboolean
gl_get_param(GLenum param, guint *pval)
    attribute_hidden;

gboolean
gl_get_texture_param(GLenum target, GLenum param, guint *pval)
    attribute_hidden;

void
gl_set_bgcolor(guint32 color)
    attribute_hidden;

void
gl_resize(guint width, guint height)
    attribute_hidden;

typedef struct _GLContextState GLContextState;
struct _GLContextState {
    Display     *display;
    Window       window;
    XVisualInfo *visual;
    GLXContext   context;
    guint        swapped_buffers : 1;
};

GLContextState *
gl_create_context(Display *dpy, int screen, GLContextState *parent)
    attribute_hidden;

void
gl_destroy_context(GLContextState *cs)
    attribute_hidden;

void
gl_get_current_context(GLContextState *cs)
    attribute_hidden;

gboolean
gl_set_current_context(GLContextState *new_cs, GLContextState *old_cs)
    attribute_hidden;

void
gl_swap_buffers(GLContextState *cs)
    attribute_hidden;

typedef struct _GLTextureState GLTextureState;
struct _GLTextureState {
    GLenum      target;
    GLuint      old_texture;
    guint       was_enabled     : 1;
    guint       was_bound       : 1;
};

gboolean
gl_bind_texture(GLTextureState *ts, GLenum target, GLuint texture)
    attribute_hidden;

void
gl_unbind_texture(GLTextureState *ts)
    attribute_hidden;

GLuint
gl_create_texture(GLenum target, GLenum format, guint width, guint height)
    attribute_hidden;

typedef struct _GLVTable GLVTable;
struct _GLVTable {
    PFNGLXCREATEPIXMAPPROC              glx_create_pixmap;
    PFNGLXDESTROYPIXMAPPROC             glx_destroy_pixmap;
    PFNGLXBINDTEXIMAGEEXTPROC           glx_bind_tex_image;
    PFNGLXRELEASETEXIMAGEEXTPROC        glx_release_tex_image;
    PFNGLGENFRAMEBUFFERSEXTPROC         gl_gen_framebuffers;
    PFNGLDELETEFRAMEBUFFERSEXTPROC      gl_delete_framebuffers;
    PFNGLBINDFRAMEBUFFEREXTPROC         gl_bind_framebuffer;
    PFNGLGENRENDERBUFFERSEXTPROC        gl_gen_renderbuffers;
    PFNGLDELETERENDERBUFFERSEXTPROC     gl_delete_renderbuffers;
    PFNGLBINDRENDERBUFFEREXTPROC        gl_bind_renderbuffer;
    PFNGLRENDERBUFFERSTORAGEEXTPROC     gl_renderbuffer_storage;
    PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC gl_framebuffer_renderbuffer;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC    gl_framebuffer_texture_2d;
    PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC  gl_check_framebuffer_status;
    PFNGLGENPROGRAMSARBPROC             gl_gen_programs;
    PFNGLDELETEPROGRAMSARBPROC          gl_delete_programs;
    PFNGLBINDPROGRAMARBPROC             gl_bind_program;
    PFNGLPROGRAMSTRINGARBPROC           gl_program_string;
    PFNGLGETPROGRAMIVARBPROC            gl_get_program_iv;
    PFNGLPROGRAMLOCALPARAMETER4FVARBPROC gl_program_local_parameter_4fv;
    PFNGLACTIVETEXTUREPROC              gl_active_texture;
    PFNGLMULTITEXCOORD2FPROC            gl_multi_tex_coord_2f;
    guint                               has_texture_from_pixmap : 1;
    guint                               has_framebuffer_object  : 1;
    guint                               has_fragment_program    : 1;
    guint                               has_multitexture        : 1;
};

GLVTable *
gl_get_vtable(void)
    attribute_hidden;

typedef struct _GLPixmapObject GLPixmapObject;
struct _GLPixmapObject {
    Display        *dpy;
    GLenum          target;
    GLuint          texture;
    GLTextureState  old_texture;
    guint           width;
    guint           height;
    Pixmap          pixmap;
    GLXPixmap       glx_pixmap;
    guint           is_bound    : 1;
};

GLPixmapObject *
gl_create_pixmap_object(Display *dpy, guint width, guint height)
    attribute_hidden;

void
gl_destroy_pixmap_object(GLPixmapObject *pixo)
    attribute_hidden;

gboolean
gl_bind_pixmap_object(GLPixmapObject *pixo)
    attribute_hidden;

gboolean
gl_unbind_pixmap_object(GLPixmapObject *pixo)
    attribute_hidden;

typedef struct _GLFramebufferObject GLFramebufferObject;
struct _GLFramebufferObject {
    guint           width;
    guint           height;
    GLuint          fbo;
    GLuint          old_fbo;
    guint           is_bound    : 1;
};

GLFramebufferObject *
gl_create_framebuffer_object(
    GLenum target,
    GLuint texture,
    guint  width,
    guint  height
) attribute_hidden;

void
gl_destroy_framebuffer_object(GLFramebufferObject *fbo)
    attribute_hidden;

gboolean
gl_bind_framebuffer_object(GLFramebufferObject *fbo)
    attribute_hidden;

gboolean
gl_unbind_framebuffer_object(GLFramebufferObject *fbo)
    attribute_hidden;

#endif /* GST_VAAPI_UTILS_GLX_H */
