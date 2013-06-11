/*
 * GStreamer
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_GL_H__
#define __GST_GL_H__

#include "gstglconfig.h"

#include <gst/video/video.h>

typedef struct _GstGLUpload GstGLUpload;
typedef struct _GstGLDownload GstGLDownload;
typedef struct _GstGLShader GstGLShader;

#include "gstglwindow.h"
#include "gstglshader.h"
#include "gstglupload.h"
#include "gstgldownload.h"

G_BEGIN_DECLS

GType gst_gl_display_get_type (void);
#define GST_GL_TYPE_DISPLAY (gst_gl_display_get_type())
#define GST_GL_DISPLAY(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_GL_TYPE_DISPLAY,GstGLDisplay))
#define GST_GL_DISPLAY_CLASS(klass)	\
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_GL_TYPE_DISPLAY,GstGLDisplayClass))
#define GST_IS_GL_DISPLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_GL_TYPE_DISPLAY))
#define GST_IS_GL_DISPLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_GL_TYPE_DISPLAY))
#define GST_GL_DISPLAY_CAST(obj) ((GstGLDisplay*)(obj))

typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstGLDisplayClass GstGLDisplayClass;
typedef struct _GstGLDisplayPrivate GstGLDisplayPrivate;

/**
 * GstGLDisplayConversion:
 *
 * %GST_GL_DISPLAY_CONVERSION_GLSL: Convert using GLSL (shaders)
 * %GST_GL_DISPLAY_CONVERSION_MATRIX: Convert using the ARB_imaging extension (not implemented)
 * %GST_GL_DISPLAY_CONVERSION_MESA: Convert using support in MESA
 */
typedef enum
{
  GST_GL_DISPLAY_CONVERSION_GLSL,
  GST_GL_DISPLAY_CONVERSION_MATRIX,
  GST_GL_DISPLAY_CONVERSION_MESA,
} GstGLDisplayConversion;

/**
 * GstGLDisplayProjection:
 *
 * %GST_GL_DISPLAY_PROJECTION_ORTHO2D: Orthogonal projection
 * %GST_GL_DISPLAY_CONVERSION_MATRIX: Perspective projection 
 */
typedef enum
{
  GST_GL_DISPLAY_PROJECTION_ORTHO2D,
  GST_GL_DISPLAY_PROJECTION_PERSPECTIVE
} GstGLDisplayProjection;

/**
 * CRCB:
 * @width: new width
 * @height: new height:
 * @data: user data
 *
 * client reshape callback
 */
typedef void (*CRCB) (GLuint width, GLuint height, gpointer data);
/**
 * CDCB:
 * @texture: texture to draw
 * @width: new width
 * @height: new height:
 * @data: user data
 *
 * client draw callback
 */
typedef gboolean (*CDCB) (GLuint texture, GLuint width, GLuint height, gpointer data);
/**
 * GstGLDisplayThreadFunc:
 * @display: a #GstGLDisplay
 * @data: user data
 *
 * Represents a function to run in the GL thread
 */
typedef void (*GstGLDisplayThreadFunc) (GstGLDisplay * display, gpointer data);

/**
 * GLCB:
 * @width: the width
 * @height: the height
 * @texture: texture
 * @stuff: user data
 *
 * callback definition for operating on textures
 */
typedef void (*GLCB) (gint, gint, guint, gpointer stuff);
/**
 * GLCB_V2:
 * @stuff: user data
 *
 * callback definition for operating through a Framebuffer object
 */
typedef void (*GLCB_V2) (gpointer stuff);

#define GST_GL_DISPLAY_ERR_MSG(obj) ("%s", GST_GL_DISPLAY_CAST(obj)->error_message)

/**
 * GstGLDisplay:
 *
 * the contents of a #GstGLDisplay are private and should only be accessed
 * through the provided API
 */
struct _GstGLDisplay
{
  GObject        object;

  /* thread safe */
  GMutex         mutex;

  /* gl context */
  GThread       *gl_thread;
  GstGLWindow   *gl_window;
  gboolean       isAlive;
  gboolean       context_created;

  /* gl API we are using */
  GstGLAPI       gl_api;
  gboolean       keep_aspect_ratio;

  /* foreign gl context */
  gulong         external_gl_context;

  GstGLDisplayConversion colorspace_conversion;

  GSList        *uploads;
  GSList        *downloads;

  gchar *error_message;

  GstGLFuncs *gl_vtable;

  GstGLDisplayPrivate *priv;
};


struct _GstGLDisplayClass
{
  GObjectClass object_class;
};


/*-----------------------------------------------------------*\
 -------------------- Public declarations -------------------
\*-----------------------------------------------------------*/
GstGLDisplay *gst_gl_display_new (void);

gboolean gst_gl_display_create_context (GstGLDisplay * display,
    gulong external_gl_context);

void gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data);

void gst_gl_display_gen_texture (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height);
void gst_gl_display_gen_texture_thread (GstGLDisplay * display, GLuint * pTexture,
    GstVideoFormat v_format, GLint width, GLint height);
void gst_gl_display_del_texture (GstGLDisplay * display, GLuint * pTexture);

gboolean gst_gl_display_gen_fbo (GstGLDisplay * display, gint width, gint height,
    GLuint * fbo, GLuint * depthbuffer);
gboolean gst_gl_display_use_fbo (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB cb, gint input_texture_width,
    gint input_texture_height, GLuint input_texture, gdouble proj_param1,
    gdouble proj_param2, gdouble proj_param3, gdouble proj_param4,
    GstGLDisplayProjection projection, gpointer * stuff);
gboolean gst_gl_display_use_fbo_v2 (GstGLDisplay * display, gint texture_fbo_width,
    gint texture_fbo_height, GLuint fbo, GLuint depth_buffer,
    GLuint texture_fbo, GLCB_V2 cb, gpointer * stuff);
void gst_gl_display_del_fbo (GstGLDisplay * display, GLuint fbo,
    GLuint depth_buffer);

gboolean gst_gl_display_gen_shader (GstGLDisplay * display,
    const gchar * shader_vertex_source,
    const gchar * shader_fragment_source, GstGLShader ** shader);
void gst_gl_display_del_shader (GstGLDisplay * display, GstGLShader * shader);

gulong gst_gl_display_get_internal_gl_context (GstGLDisplay * display);
void gst_gl_display_activate_gl_context (GstGLDisplay * display, gboolean activate);

/* Must be called inside a lock/unlock on display, or within the glthread */
void gst_gl_display_set_error (GstGLDisplay * display, const char * format, ...);
gboolean gst_gl_display_check_framebuffer_status (GstGLDisplay * display);

void gst_gl_display_lock (GstGLDisplay * display);
void gst_gl_display_unlock (GstGLDisplay * display);
GstGLAPI gst_gl_display_get_gl_api (GstGLDisplay * display);
GstGLAPI gst_gl_display_get_gl_api_unlocked (GstGLDisplay * display);

gpointer gst_gl_display_get_gl_vtable (GstGLDisplay * display);

G_END_DECLS

#endif /* __GST_GL_H__ */
