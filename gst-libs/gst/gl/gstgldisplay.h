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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_GL_H__
#define __GST_GL_H__

#include <gst/video/video.h>

#include "gstglwindow.h"
#include "gstglshader.h"
#include "gstglupload.h"
#include "gstgldownload.h"

G_BEGIN_DECLS

GType gst_gl_display_get_type (void);
#define GST_TYPE_GL_DISPLAY (gst_gl_display_get_type())
#define GST_GL_DISPLAY(obj)	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_DISPLAY,GstGLDisplay))
#define GST_GL_DISPLAY_CLASS(klass)	\
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_DISPLAY,GstGLDisplayClass))
#define GST_IS_GL_DISPLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_DISPLAY))
#define GST_IS_GL_DISPLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_DISPLAY))
#define GST_GL_DISPLAY_CAST(obj) ((GstGLDisplay*)(obj))

typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstGLDisplayClass GstGLDisplayClass;

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
  GMutex        *mutex;

  /* gl context */
  GThread       *gl_thread;
  GstGLWindow   *gl_window;
  gboolean       isAlive;

  /* conditions */
  GCond         *cond_create_context;
  GCond         *cond_destroy_context;

  /* generic gl code */
  GstGLDisplayThreadFunc   generic_callback;
  gpointer                 data;

  /* action redisplay */
  GLuint         redisplay_texture;
  GLuint         redisplay_texture_width;
  GLuint         redisplay_texture_height;
  gboolean       keep_aspect_ratio;
#ifdef OPENGL_ES2
  GstGLShader   *redisplay_shader;
  gchar         *redisplay_vertex_shader_str;
  gchar         *redisplay_fragment_shader_str;
  GLint          redisplay_attr_position_loc;
  GLint          redisplay_attr_texture_loc;
#endif

  /* action gen and del texture */
  GLuint         gen_texture;
  GLuint         gen_texture_width;
  GLuint         gen_texture_height;
  GstVideoFormat gen_texture_video_format;

  /* client callbacks */
  CRCB           clientReshapeCallback;
  CDCB           clientDrawCallback;
  gpointer       client_data;

  GstGLDisplayConversion colorspace_conversion;

  GSList        *uploads;
  GSList        *downloads;

  /* foreign gl context */
  gulong         external_gl_context;

  /* filter gen fbo */
  GLuint         gen_fbo_width;
  GLuint         gen_fbo_height;
  GLuint         generated_fbo;
  GLuint         generated_depth_buffer;

  /* filter use fbo */
  GLuint         use_fbo;
  GLuint         use_depth_buffer;
  GLuint         use_fbo_texture;
  GLuint         use_fbo_width;
  GLuint         use_fbo_height;
  GLCB           use_fbo_scene_cb;
  GLCB_V2        use_fbo_scene_cb_v2;
  gdouble        use_fbo_proj_param1;
  gdouble        use_fbo_proj_param2;
  gdouble        use_fbo_proj_param3;
  gdouble        use_fbo_proj_param4;
  GstGLDisplayProjection use_fbo_projection;
  gpointer      *use_fbo_stuff;
  GLuint         input_texture_width;
  GLuint         input_texture_height;
  GLuint         input_texture;

  /* filter del fbo */
  GLuint         del_fbo;
  GLuint         del_depth_buffer;

  /* action gen and del shader */
  const gchar   *gen_shader_fragment_source;
  const gchar   *gen_shader_vertex_source;
  GstGLShader   *gen_shader;
  GstGLShader   *del_shader;

  gchar *error_message;
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
gboolean gst_gl_display_redisplay (GstGLDisplay * display, GLuint texture,
    gint gl_width, gint gl_height, gint window_width, gint window_height,
    gboolean keep_aspect_ratio);

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

void gst_gl_display_set_window_id (GstGLDisplay * display, gulong window_id);
void gst_gl_display_set_client_reshape_callback (GstGLDisplay * display,
    CRCB cb);
void gst_gl_display_set_client_draw_callback (GstGLDisplay * display, CDCB cb);
void gst_gl_display_set_client_data (GstGLDisplay * display, gpointer data);

gulong gst_gl_display_get_internal_gl_context (GstGLDisplay * display);
void gst_gl_display_activate_gl_context (GstGLDisplay * display, gboolean activate);

/* Must be called inside a lock/unlock on display, or within the glthread */
void gst_gl_display_set_error (GstGLDisplay * display, const char * format, ...);
void gst_gl_display_check_framebuffer_status (void);

void gst_gl_display_lock (GstGLDisplay * display);
void gst_gl_display_unlock (GstGLDisplay * display);

G_END_DECLS

#endif /* __GST_GL_H__ */
