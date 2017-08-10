/*
 *  gstvaapiutils_glx.h - GLX utilties
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
#include <GL/glx.h>
#include <glib.h>

#if GLX_GLXEXT_VERSION < 18
typedef void (*PFNGLXBINDTEXIMAGEEXTPROC) (Display *, GLXDrawable, int,
    const int *);
typedef void (*PFNGLXRELEASETEXIMAGEEXTPROC) (Display *, GLXDrawable, int);
#endif

#if GLX_GLXEXT_VERSION < 27
/* XXX: this is not exactly that version but this is the only means to
   make sure we have the correct <GL/glx.h> with those signatures */
typedef GLXPixmap (*PFNGLXCREATEPIXMAPPROC) (Display *, GLXFBConfig, Pixmap,
    const int *);
typedef void (*PFNGLXDESTROYPIXMAPPROC) (Display *, GLXPixmap);
#endif

#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING GL_FRAMEBUFFER_BINDING_EXT
#endif

G_GNUC_INTERNAL
const gchar *
gl_get_error_string (GLenum error);

G_GNUC_INTERNAL
void
gl_purge_errors (void);

G_GNUC_INTERNAL
gboolean
gl_check_error (void);

G_GNUC_INTERNAL
gboolean
gl_get_param (GLenum param, guint * pval);

G_GNUC_INTERNAL
gboolean
gl_get_texture_param (GLenum target, GLenum param, guint * pval);

G_GNUC_INTERNAL
void
gl_set_bgcolor (guint32 color);

G_GNUC_INTERNAL
void
gl_resize (guint width, guint height);

typedef struct _GLContextState GLContextState;
struct _GLContextState
{
  Display *display;
  Window window;
  XVisualInfo *visual;
  GLXContext context;
  guint swapped_buffers:1;
};

G_GNUC_INTERNAL
GLContextState *
gl_create_context (Display * dpy, int screen, GLContextState * parent);

G_GNUC_INTERNAL
void
gl_destroy_context (GLContextState * cs);

G_GNUC_INTERNAL
void
gl_get_current_context (GLContextState * cs);

G_GNUC_INTERNAL
gboolean
gl_set_current_context (GLContextState * new_cs, GLContextState * old_cs);

G_GNUC_INTERNAL
void
gl_swap_buffers (GLContextState * cs);

typedef struct _GLTextureState GLTextureState;
struct _GLTextureState
{
  GLenum target;
  GLuint old_texture;
  guint was_enabled:1;
  guint was_bound:1;
};

G_GNUC_INTERNAL
gboolean
gl_bind_texture (GLTextureState * ts, GLenum target, GLuint texture);

G_GNUC_INTERNAL
gboolean
gl3_bind_texture_2d (GLTextureState * ts, GLenum target, GLuint texture);

G_GNUC_INTERNAL
void
gl_unbind_texture (GLTextureState * ts);

G_GNUC_INTERNAL
GLuint
gl_create_texture (GLenum target, GLenum format, guint width, guint height);

typedef struct _GLVTable GLVTable;
struct _GLVTable
{
  PFNGLXCREATEPIXMAPPROC glx_create_pixmap;
  PFNGLXDESTROYPIXMAPPROC glx_destroy_pixmap;
  PFNGLXBINDTEXIMAGEEXTPROC glx_bind_tex_image;
  PFNGLXRELEASETEXIMAGEEXTPROC glx_release_tex_image;
  PFNGLGENFRAMEBUFFERSEXTPROC gl_gen_framebuffers;
  PFNGLDELETEFRAMEBUFFERSEXTPROC gl_delete_framebuffers;
  PFNGLBINDFRAMEBUFFEREXTPROC gl_bind_framebuffer;
  PFNGLGENRENDERBUFFERSEXTPROC gl_gen_renderbuffers;
  PFNGLDELETERENDERBUFFERSEXTPROC gl_delete_renderbuffers;
  PFNGLBINDRENDERBUFFEREXTPROC gl_bind_renderbuffer;
  PFNGLRENDERBUFFERSTORAGEEXTPROC gl_renderbuffer_storage;
  PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC gl_framebuffer_renderbuffer;
  PFNGLFRAMEBUFFERTEXTURE2DEXTPROC gl_framebuffer_texture_2d;
  PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC gl_check_framebuffer_status;
  guint has_texture_from_pixmap:1;
  guint has_framebuffer_object:1;
};

G_GNUC_INTERNAL
GLVTable *
gl_get_vtable (void);

typedef struct _GLPixmapObject GLPixmapObject;
struct _GLPixmapObject
{
  Display *dpy;
  GLenum target;
  GLuint texture;
  GLTextureState old_texture;
  guint width;
  guint height;
  Pixmap pixmap;
  GLXPixmap glx_pixmap;
  guint is_bound:1;
};

G_GNUC_INTERNAL
GLPixmapObject *
gl_create_pixmap_object (Display * dpy, guint width, guint height);

G_GNUC_INTERNAL
void
gl_destroy_pixmap_object (GLPixmapObject * pixo);

G_GNUC_INTERNAL
gboolean
gl_bind_pixmap_object (GLPixmapObject * pixo);

G_GNUC_INTERNAL
gboolean
gl_unbind_pixmap_object (GLPixmapObject * pixo);

typedef struct _GLFramebufferObject GLFramebufferObject;
struct _GLFramebufferObject
{
  guint width;
  guint height;
  GLuint fbo;
  GLuint old_fbo;
  guint is_bound:1;
};

G_GNUC_INTERNAL
GLFramebufferObject *
gl_create_framebuffer_object (GLenum target,
    GLuint texture, guint width, guint height);

G_GNUC_INTERNAL
void
gl_destroy_framebuffer_object (GLFramebufferObject * fbo);

G_GNUC_INTERNAL
gboolean
gl_bind_framebuffer_object (GLFramebufferObject * fbo);

G_GNUC_INTERNAL
gboolean
gl_unbind_framebuffer_object (GLFramebufferObject * fbo);

typedef enum {
  GST_VAAPI_GL_API_NONE = 0,
  GST_VAAPI_GL_API_OPENGL = (1 << 0),
  GST_VAAPI_GL_API_OPENGL3 = (1 << 1),
  GST_VAAPI_GL_API_GLES1 = (1 << 15),
  GST_VAAPI_GL_API_GLES2 = (1 << 16),

  GST_VAAPI_GL_API_ANY = G_MAXUINT32
} GstVaapiGLApi;

G_GNUC_INTERNAL
GstVaapiGLApi
gl_get_current_api (guint * major, guint * minor);

#endif /* GST_VAAPI_UTILS_GLX_H */
