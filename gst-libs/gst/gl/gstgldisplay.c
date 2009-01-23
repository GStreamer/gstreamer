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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgldisplay.h"

/*
 * gst-launch-0.10 --gst-debug=gldisplay:N pipeline
 * N=1: errors
 * N=2: errors warnings
 * N=3: errors warnings infos
 * N=4: errors warnings infos
 * N=5: errors warnings infos logs
 */

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0, "opengl display");

GST_BOILERPLATE_FULL (GstGLDisplay, gst_gl_display, GObject, G_TYPE_OBJECT, DEBUG_INIT);
static void gst_gl_display_finalize (GObject* object);

/* Called in the gl thread, protected by lock and unlock */
gpointer gst_gl_display_thread_create_context (GstGLDisplay* display);
void gst_gl_display_thread_destroy_context (GstGLDisplay* display);
void gst_gl_display_thread_run_generic (GstGLDisplay *display);
void gst_gl_display_thread_gen_texture (GstGLDisplay* display);
void gst_gl_display_thread_del_texture (GstGLDisplay* display);
void gst_gl_display_thread_init_upload (GstGLDisplay* display);
void gst_gl_display_thread_do_upload (GstGLDisplay* display);
void gst_gl_display_thread_init_download (GstGLDisplay *display);
void gst_gl_display_thread_do_download (GstGLDisplay* display);
void gst_gl_display_thread_gen_fbo (GstGLDisplay *display);
void gst_gl_display_thread_use_fbo (GstGLDisplay *display);
void gst_gl_display_thread_del_fbo (GstGLDisplay *display);
void gst_gl_display_thread_gen_shader (GstGLDisplay *display);
void gst_gl_display_thread_del_shader (GstGLDisplay *display);

/* private methods */
void gst_gl_display_lock (GstGLDisplay* display);
void gst_gl_display_unlock (GstGLDisplay* display);
void gst_gl_display_on_resize(GstGLDisplay* display, gint width, gint height);
void gst_gl_display_on_draw (GstGLDisplay* display);
void gst_gl_display_on_close (GstGLDisplay* display);
void gst_gl_display_glgen_texture (GstGLDisplay* display, GLuint* pTexture, GLint width, GLint height);
void gst_gl_display_gldel_texture (GstGLDisplay* display, GLuint* pTexture, GLint width, GLint height);
gboolean gst_gl_display_texture_pool_func_clean (gpointer key, gpointer value, gpointer data);
void gst_gl_display_check_framebuffer_status (void);

/* To not make gst_gl_display_thread_do_upload
 * and gst_gl_display_thread_do_download too big */
void gst_gl_display_thread_init_upload_fbo (GstGLDisplay *display);
void gst_gl_display_thread_do_upload_make (GstGLDisplay *display);
void gst_gl_display_thread_do_upload_fill (GstGLDisplay *display);
void gst_gl_display_thread_do_upload_draw (GstGLDisplay *display);
void gst_gl_display_thread_do_download_draw_rgb (GstGLDisplay *display);
void gst_gl_display_thread_do_download_draw_yuv (GstGLDisplay *display);


//------------------------------------------------------------
//---------------------- For klass GstGLDisplay ---------------
//------------------------------------------------------------

static void
gst_gl_display_base_init (gpointer g_class)
{
}

static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;
}


static void
gst_gl_display_init (GstGLDisplay *display, GstGLDisplayClass *klass)
{
  //thread safe
  display->mutex = g_mutex_new ();

  //gl context
  display->gl_thread = NULL;
  display->gl_window = NULL;
  display->visible = FALSE;
  display->isAlive = TRUE;
  display->texture_pool = g_hash_table_new (g_direct_hash, g_direct_equal);

  //conditions
  display->cond_create_context = g_cond_new ();
  display->cond_destroy_context = g_cond_new ();

  //action redisplay
  display->redisplay_texture = 0;
  display->redisplay_texture_width = 0;
  display->redisplay_texture_height = 0;

  //action gen and del texture
  display->gen_texture = 0;
  display->gen_texture_width = 0;
  display->gen_texture_height = 0;
  display->del_texture = 0;
  display->del_texture_width = 0;
  display->del_texture_height = 0;

  //client callbacks
  display->clientReshapeCallback = NULL;
  display->clientDrawCallback = NULL;

  //upload
  display->upload_fbo = 0;
  display->upload_depth_buffer = 0;
  display->upload_outtex = 0;
  display->upload_intex = 0;
  display->upload_intex_u = 0;
  display->upload_intex_v = 0;
  display->upload_width = 0;
  display->upload_height = 0;
  display->upload_video_format = GST_VIDEO_FORMAT_RGBx;
  display->upload_colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MESA;
  display->upload_data_width = 0;
  display->upload_data_height = 0;
  display->upload_data = NULL;

  //filter gen fbo
  display->gen_fbo_width = 0;
  display->gen_fbo_height = 0;
  display->generated_fbo = 0;
  display->generated_depth_buffer = 0;

  //filter use fbo
  display->use_fbo = 0;
  display->use_depth_buffer = 0;
  display->use_fbo_texture = 0;
  display->use_fbo_width = 0;
  display->use_fbo_height = 0;
  display->use_fbo_scene_cb = NULL;
  display->use_fbo_proj_param1 = 0;
  display->use_fbo_proj_param2 = 0;
  display->use_fbo_proj_param3 = 0;
  display->use_fbo_proj_param4 = 0;
  display->use_fbo_projection = 0;
  display->use_fbo_stuff = NULL;
  display->input_texture_width = 0;
  display->input_texture_height = 0;
  display->input_texture = 0;

  //filter del fbo
  display->del_fbo = 0;
  display->del_depth_buffer = 0;

  //download
  display->download_fbo = 0;
  display->download_depth_buffer = 0;
  display->download_texture = 0;
  display->download_texture_u = 0;
  display->download_texture_v = 0;
  display->download_width = 0;
  display->download_height = 0;
  display->download_video_format = 0;
  display->download_data = NULL;
  display->ouput_texture = 0;
  display->ouput_texture_width = 0;
  display->ouput_texture_height = 0;

  //action gen and del shader
  display->gen_shader_fragment_source = NULL;
  display->gen_shader_vertex_source = NULL;
  display->gen_shader = NULL;
  display->del_shader = NULL;

  //fragement shader upload
  display->shader_upload_YUY2 = NULL;
  display->shader_upload_UYVY = NULL;
  display->shader_upload_I420_YV12 = NULL;
  display->shader_upload_AYUV = NULL;

  //fragement shader download
  display->shader_download_YUY2 = NULL;
  display->shader_download_UYVY = NULL;
  display->shader_download_I420_YV12 = NULL;
  display->shader_download_AYUV = NULL;

  //YUY2:r,g,a
  //UYVY:a,b,r
  display->text_shader_upload_YUY2_UYVY =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect Ytex, UVtex;\n"
    "void main(void) {\n"
    "  float fx, fy, y, u, v, r, g, b;\n"
    "  fx   = gl_TexCoord[0].x;\n"
    "  fy   = gl_TexCoord[0].y;\n"
    "  y = texture2DRect(Ytex,vec2(fx,fy)).%c;\n"
    "  u = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  v = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  y=1.164*(y-0.0627);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r = y+1.5958*v;\n"
    "  g = y-0.39173*u-0.81290*v;\n"
    "  b = y+2.017*u;\n"
    "  gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

  //ATI: "*0.5", ""
  //normal: "", "*0.5"
  display->text_shader_upload_I420_YV12 =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect Ytex,Utex,Vtex;\n"
    "void main(void) {\n"
    "  float r,g,b,y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  y=texture2DRect(Ytex,nxy%s).r;\n"
    "  u=texture2DRect(Utex,nxy%s).r;\n"
    "  v=texture2DRect(Vtex,nxy*0.5).r;\n"
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n";

  display->text_shader_upload_AYUV =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
    "void main(void) {\n"
    "  float r,g,b,y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  y=texture2DRect(tex,nxy).r;\n"
    "  u=texture2DRect(tex,nxy).g;\n"
    "  v=texture2DRect(tex,nxy).b;\n"
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=clamp(y+1.5958*v, 0, 1);\n"
    "  g=clamp(y-0.39173*u-0.81290*v, 0, 1);\n"
    "  b=clamp(y+2.017*u, 0, 1);\n"
    "  gl_FragColor=vec4(r,g,b,1.0);\n"
    "}\n";

  //YUY2:y2,u,y1,v
  //UYVY:v,y1,u,y2
  display->text_shader_download_YUY2_UYVY =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
    "void main(void) {\n"
    "  float fx,fy,r,g,b,r2,g2,b2,y1,y2,u,v;\n"
    "  fx = gl_TexCoord[0].x;\n"
    "  fy = gl_TexCoord[0].y;\n"
    "  r=texture2DRect(tex,vec2(fx*2.0,fy)).r;\n"
    "  g=texture2DRect(tex,vec2(fx*2.0,fy)).g;\n"
    "  b=texture2DRect(tex,vec2(fx*2.0,fy)).b;\n"
    "  r2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).r;\n"
    "  g2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).g;\n"
    "  b2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).b;\n"
    "  y1=0.299011*r + 0.586987*g + 0.114001*b;\n"
    "  y2=0.299011*r2 + 0.586987*g2 + 0.114001*b2;\n"
    "  u=-0.148246*r -0.29102*g + 0.439266*b;\n"
    "  v=0.439271*r - 0.367833*g - 0.071438*b ;\n"
    "  y1=0.858885*y1 + 0.0625;\n"
    "  y2=0.858885*y2 + 0.0625;\n"
    "  u=u + 0.5;\n"
    "  v=v + 0.5;\n"
    "  gl_FragColor=vec4(%s);\n"
    "}\n";

  display->text_shader_download_I420_YV12 =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
    "uniform float w, h;\n"
    "void main(void) {\n"
    "  float r,g,b,r2,b2,g2,y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  vec2 nxy2=mod(2.0*nxy, vec2(w, h));\n"
    "  r=texture2DRect(tex,nxy).r;\n"
    "  g=texture2DRect(tex,nxy).g;\n"
    "  b=texture2DRect(tex,nxy).b;\n"
    "  r2=texture2DRect(tex,nxy2).r;\n"
    "  g2=texture2DRect(tex,nxy2).g;\n"
    "  b2=texture2DRect(tex,nxy2).b;\n"
    "  y=0.299011*r + 0.586987*g + 0.114001*b;\n"
    "  u=-0.148246*r2 -0.29102*g2 + 0.439266*b2;\n"
    "  v=0.439271*r2 - 0.367833*g2 - 0.071438*b2 ;\n"
    "  y=0.858885*y + 0.0625;\n"
    "  u=u + 0.5;\n"
    "  v=v + 0.5;\n"
    "  gl_FragData[0] = vec4(y, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[1] = vec4(u, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[2] = vec4(v, 0.0, 0.0, 1.0);\n"
    "}\n";

  display->text_shader_download_AYUV =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
    "void main(void) {\n"
    "  float r,g,b,y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  r=texture2DRect(tex,nxy).r;\n"
    "  g=texture2DRect(tex,nxy).g;\n"
    "  b=texture2DRect(tex,nxy).b;\n"
    "  y=0.299011*r + 0.586987*g + 0.114001*b;\n"
    "  u=-0.148246*r -0.29102*g + 0.439266*b;\n"
    "  v=0.439271*r - 0.367833*g - 0.071438*b ;\n"
    "  y=0.858885*y + 0.0625;\n"
    "  u=u + 0.5;\n"
    "  v=v + 0.5;\n"
    "  gl_FragColor=vec4(y,u,v,1.0);\n"
    "}\n";
}

static void
gst_gl_display_finalize (GObject* object)
{
  GstGLDisplay* display = GST_GL_DISPLAY (object);

  if (display->mutex && display->gl_window)
  {

    gst_gl_display_lock (display);

    gst_gl_window_set_resize_callback (display->gl_window, NULL, NULL);
    gst_gl_window_set_draw_callback (display->gl_window, NULL, NULL);
    gst_gl_window_set_close_callback (display->gl_window, NULL, NULL);

    GST_INFO ("send quit gl window loop");

    gst_gl_window_quit_loop (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_destroy_context), display);

    GST_INFO ("quit sent to gl window loop");

    g_cond_wait (display->cond_destroy_context, display->mutex);
    GST_INFO ("quit received from gl window");
    gst_gl_display_unlock (display);
  }

  if (display->gl_thread)
  {
    gpointer ret = g_thread_join (display->gl_thread);
    GST_INFO ("gl thread joined");
    if (ret != NULL)
      GST_ERROR ("gl thread returned a not null pointer");
    display->gl_thread = NULL;
  }

  if (display->texture_pool) {
    //texture pool is empty after destroying the gl context
    if (g_hash_table_size (display->texture_pool) != 0)
      GST_ERROR ("texture pool is not empty");
    g_hash_table_unref (display->texture_pool);
    display->texture_pool = NULL;
  }
  if (display->mutex) {
    g_mutex_free (display->mutex);
    display->mutex = NULL;
  }
  if (display->cond_destroy_context) {
    g_cond_free (display->cond_destroy_context);
    display->cond_destroy_context = NULL;
  }
  if (display->cond_create_context) {
    g_cond_free (display->cond_create_context);
    display->cond_create_context = NULL;
  }
  if (display->clientReshapeCallback)
    display->clientReshapeCallback = NULL;
  if (display->clientDrawCallback)
    display->clientDrawCallback = NULL;
  if (display->use_fbo_scene_cb)
    display->use_fbo_scene_cb = NULL;
  if (display->use_fbo_stuff)
    display->use_fbo_stuff = NULL;
}


//------------------------------------------------------------
//------------------ BEGIN GL THREAD PROCS -------------------
//------------------------------------------------------------

/* Called in the gl thread */
gpointer
gst_gl_display_thread_create_context (GstGLDisplay *display)
{
  GLenum err = 0;

  display->gl_window = gst_gl_window_new (display->upload_width, display->upload_height);

  if (!display->gl_window)
  {
    display->isAlive = FALSE;
    GST_ERROR_OBJECT (display, "Failed to create gl window");
    g_cond_signal (display->cond_create_context);
    return NULL;
  }

  GST_INFO ("gl window created");

  //Init glew
  err = glewInit();
  if (err != GLEW_OK)
  {
    GST_ERROR_OBJECT (display, "Failed to init GLEW: %s",
      glewGetErrorString (err));
    display->isAlive = FALSE;
  }
  else
  {
    //OpenGL > 1.2.0 and Glew > 1.4.0
    GString* opengl_version = g_string_truncate (g_string_new ((gchar*) glGetString (GL_VERSION)), 3);
    gint opengl_version_major = 0;
    gint opengl_version_minor = 0;

    sscanf(opengl_version->str, "%d.%d", &opengl_version_major, &opengl_version_minor);

    GST_INFO ("GL_VERSION: %s", glGetString (GL_VERSION));
    GST_INFO ("GLEW_VERSION: %s", glewGetString (GLEW_VERSION));
    if (glGetString (GL_SHADING_LANGUAGE_VERSION))
      GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s", glGetString (GL_SHADING_LANGUAGE_VERSION));
    else
      GST_INFO ("Your driver does not support GLSL (OpenGL Shading Language)");

    GST_INFO ("GL_VENDOR: %s", glGetString (GL_VENDOR));
    GST_INFO ("GL_RENDERER: %s", glGetString (GL_RENDERER));

    g_string_free (opengl_version, TRUE);

    if ((opengl_version_major < 1) ||
        (GLEW_VERSION_MAJOR   < 1) ||
        (opengl_version_major < 2 && opengl_version_major >= 1 && opengl_version_minor < 2) ||
        (GLEW_VERSION_MAJOR   < 2 && GLEW_VERSION_MAJOR   >= 1 && GLEW_VERSION_MINOR   < 4) )
    {
      //turn off the pipeline, the old drivers are not yet supported
      GST_WARNING ("Required OpenGL >= 1.2.0 and Glew >= 1.4.0");
      display->isAlive = FALSE;
    }
  }

  //setup callbacks
  gst_gl_window_set_resize_callback (display->gl_window, GST_GL_WINDOW_CB2 (gst_gl_display_on_resize), display);
  gst_gl_window_set_draw_callback (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_on_draw), display);
  gst_gl_window_set_close_callback (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_on_close), display);

  g_cond_signal (display->cond_create_context);

  gst_gl_window_run_loop (display->gl_window);

  GST_INFO ("loop exited\n");

  gst_gl_display_lock (display);

  display->isAlive = FALSE;

  g_object_unref (G_OBJECT (display->gl_window));

  display->gl_window = NULL;

  g_cond_signal (display->cond_destroy_context);

  gst_gl_display_unlock (display);

  return NULL;
}


/* Called in the gl thread */
void
gst_gl_display_thread_destroy_context (GstGLDisplay *display)
{
  //colorspace_conversion specific
  switch (display->upload_colorspace_conversion)
  {
  case GST_GL_DISPLAY_CONVERSION_MESA:
  case GST_GL_DISPLAY_CONVERSION_MATRIX:
    break;
  case GST_GL_DISPLAY_CONVERSION_GLSL:
  {
    glUseProgramObjectARB (0);
    if (display->shader_upload_YUY2)
    {
      g_object_unref (G_OBJECT (display->shader_upload_YUY2));
      display->shader_upload_YUY2 = NULL;
    }
    if (display->shader_upload_UYVY)
    {
      g_object_unref (G_OBJECT (display->shader_upload_UYVY));
      display->shader_upload_UYVY = NULL;
    }
    if (display->shader_upload_I420_YV12)
    {
      g_object_unref (G_OBJECT (display->shader_upload_I420_YV12));
      display->shader_upload_I420_YV12 = NULL;
    }
    if (display->shader_upload_AYUV)
    {
      g_object_unref (G_OBJECT (display->shader_upload_AYUV));
      display->shader_upload_AYUV = NULL;
    }
    if (display->shader_download_YUY2)
    {
      g_object_unref (G_OBJECT (display->shader_download_YUY2));
      display->shader_download_YUY2 = NULL;
    }
    if (display->shader_download_UYVY)
    {
      g_object_unref (G_OBJECT (display->shader_download_UYVY));
      display->shader_download_UYVY = NULL;
    }
    if (display->shader_download_I420_YV12)
    {
      g_object_unref (G_OBJECT (display->shader_download_I420_YV12));
      display->shader_download_I420_YV12 = NULL;
    }
    if (display->shader_download_AYUV)
    {
      g_object_unref (G_OBJECT (display->shader_download_AYUV));
      display->shader_download_AYUV = NULL;
    }
  }
  break;
  default:
    g_assert_not_reached ();
  }

  if (display->upload_fbo)
  {
    glDeleteFramebuffersEXT (1, &display->upload_fbo);
    display->upload_fbo = 0;
  }
  if (display->upload_depth_buffer)
  {
    glDeleteRenderbuffersEXT(1, &display->upload_depth_buffer);
    display->upload_depth_buffer = 0;
  }
  if (display->download_fbo)
  {
    glDeleteFramebuffersEXT (1, &display->download_fbo);
    display->download_fbo = 0;
  }
  if (display->download_depth_buffer)
  {
    glDeleteRenderbuffersEXT(1, &display->download_depth_buffer);
    display->download_depth_buffer = 0;
  }
  if (display->download_texture)
  {
    glDeleteTextures (1, &display->download_texture);
    display->download_texture = 0;
  }
  if (display->download_texture_u)
  {
    glDeleteTextures (1, &display->download_texture_u);
    display->download_texture_u = 0;
  }
  if (display->download_texture_v)
  {
    glDeleteTextures (1, &display->download_texture_v);
    display->download_texture_v = 0;
  }
  if (display->upload_intex != 0)
  {
    glDeleteTextures (1, &display->upload_intex);
    display->upload_intex = 0;
  }
  if (display->upload_intex_u != 0)
  {
    glDeleteTextures (1, &display->upload_intex_u);
    display->upload_intex_u = 0;
  }
  if (display->upload_intex_v != 0)
  {
    glDeleteTextures (1, &display->upload_intex_v);
    display->upload_intex_v = 0;
  }

  GST_INFO ("Cleaning texture pool");

  //clean up the texture pool
  g_hash_table_foreach_remove (display->texture_pool, gst_gl_display_texture_pool_func_clean,
    NULL);

  GST_INFO ("Context destroyed");
}


void
gst_gl_display_thread_run_generic (GstGLDisplay *display)
{
  display->generic_callback (display, display->data);
}


/* Called in the gl thread */
void
gst_gl_display_thread_gen_texture (GstGLDisplay * display)
{
  //setup a texture to render to (this one will be in a gl buffer)
  gst_gl_display_glgen_texture (display, &display->gen_texture,
    display->gen_texture_width, display->gen_texture_height);
}


/* Called in the gl thread */
void
gst_gl_display_thread_del_texture (GstGLDisplay* display)
{
  gst_gl_display_gldel_texture (display, &display->del_texture,
    display->del_texture_width, display->del_texture_height);
}


/* Called in the gl thread */
void
gst_gl_display_thread_init_upload (GstGLDisplay *display)
{
  switch (display->upload_video_format)
  {
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
    //color space conversion is not needed
    //but if the size is different we need to redraw it
    //using fbo
    if (display->upload_width != display->upload_data_width ||
        display->upload_height != display->upload_data_height)
      gst_gl_display_thread_init_upload_fbo (display);
    break;
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  case GST_VIDEO_FORMAT_AYUV:
    //color space conversion is needed
  {
	  //check if fragment shader is available, then load them
    /* shouldn't we require ARB_shading_language_100? --Filippo */
    if (GLEW_ARB_fragment_shader)
    {
      GST_INFO ("Context, ARB_fragment_shader supported: yes");

      display->upload_colorspace_conversion = GST_GL_DISPLAY_CONVERSION_GLSL;

      gst_gl_display_thread_init_upload_fbo (display);
      if (!display->isAlive)
        break;

      switch (display->upload_video_format)
      {
      case GST_VIDEO_FORMAT_YUY2:
        {
          gchar text_shader_upload_YUY2[2048];
          sprintf (text_shader_upload_YUY2, display->text_shader_upload_YUY2_UYVY, 'r', 'g', 'a');

          display->shader_upload_YUY2 = gst_gl_shader_new ();
          if(!gst_gl_shader_compile_and_check (display->shader_upload_YUY2,
                       text_shader_upload_YUY2, GST_GL_SHADER_FRAGMENT_SOURCE))
          {
            display->isAlive = FALSE;
            g_object_unref (G_OBJECT (display->shader_upload_YUY2));
            display->shader_upload_YUY2 = NULL;
          }
        }
      break;
      case GST_VIDEO_FORMAT_UYVY:
        {
          gchar text_shader_upload_UYVY[2048];
          sprintf (text_shader_upload_UYVY, display->text_shader_upload_YUY2_UYVY, 'a', 'b', 'r');

          display->shader_upload_UYVY = gst_gl_shader_new ();
          if(!gst_gl_shader_compile_and_check (display->shader_upload_UYVY,
                       text_shader_upload_UYVY, GST_GL_SHADER_FRAGMENT_SOURCE))
          {
            display->isAlive = FALSE;
            g_object_unref (G_OBJECT (display->shader_upload_UYVY));
            display->shader_upload_UYVY = NULL;
          }
        }
        break;
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
        {
          gchar text_shader_upload_I420_YV12[2048];
          if (g_ascii_strncasecmp ("ATI", (gchar *) glGetString (GL_VENDOR), 3) == 0)
              sprintf (text_shader_upload_I420_YV12, display->text_shader_upload_I420_YV12, "*0.5", "");
          else
              sprintf (text_shader_upload_I420_YV12, display->text_shader_upload_I420_YV12, "", "*0.5");

          display->shader_upload_I420_YV12 = gst_gl_shader_new ();
          if(!gst_gl_shader_compile_and_check (display->shader_upload_I420_YV12,
                       text_shader_upload_I420_YV12, GST_GL_SHADER_FRAGMENT_SOURCE))
          {
            display->isAlive = FALSE;
            g_object_unref (G_OBJECT (display->shader_upload_I420_YV12));
            display->shader_upload_I420_YV12 = NULL;
          }
        }
        break;
      case GST_VIDEO_FORMAT_AYUV:
        display->shader_upload_AYUV = gst_gl_shader_new ();
        if(!gst_gl_shader_compile_and_check (display->shader_upload_AYUV,
                     display->text_shader_upload_AYUV, GST_GL_SHADER_FRAGMENT_SOURCE))
        {
          display->isAlive = FALSE;
          g_object_unref (G_OBJECT (display->shader_upload_AYUV));
          display->shader_upload_AYUV = NULL;
        }
        break;
      default:
        g_assert_not_reached ();
      }
    }
    //check if YCBCR MESA is available
    else if (GLEW_MESA_ycbcr_texture)
    {
      //GLSL and Color Matrix are not available on your drivers, switch to YCBCR MESA
      GST_INFO ("Context, ARB_fragment_shader supported: no");
      GST_INFO ("Context, GLEW_MESA_ycbcr_texture supported: yes");

      display->upload_colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MESA;

      switch (display->upload_video_format)
      {
      case GST_VIDEO_FORMAT_YUY2:
      case GST_VIDEO_FORMAT_UYVY:
        //color space conversion is not needed
        //but if the size is different we need to redraw it
        //using fbo
        if (display->upload_width != display->upload_data_width ||
            display->upload_height != display->upload_data_height)
          gst_gl_display_thread_init_upload_fbo (display);
        break;
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
      case GST_VIDEO_FORMAT_AYUV:
        //turn off the pipeline because
        //MESA only support YUY2 and UYVY
        GST_WARNING ("Your MESA version only supports YUY2 and UYVY (GLSL is required for others yuv formats");
        display->isAlive = FALSE;
        break;
            default:
        g_assert_not_reached ();
      }
    }
    //check if color matrix is available
    else if (GLEW_ARB_imaging)
    {
      //GLSL is not available on your drivers, switch to Color Matrix
      GST_INFO ("Context, ARB_fragment_shader supported: no");
      GST_INFO ("Context, GLEW_MESA_ycbcr_texture supported: no");
      GST_INFO ("Context, GLEW_ARB_imaging supported: yes");

      display->upload_colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MATRIX;

      //turn off the pipeline because we do not support it yet
      GST_WARNING ("Colorspace conversion using Color Matrix is not yet supported");
      display->isAlive = FALSE;
    }
    else
    {
      GST_WARNING ("Context, ARB_fragment_shader supported: no");
      GST_WARNING ("Context, GLEW_ARB_imaging supported: no");
      GST_WARNING ("Context, GLEW_MESA_ycbcr_texture supported: no");

      //turn off the pipeline because colorspace conversion is not possible
      display->isAlive = FALSE;
    }
  }
  break;
  default:
    g_assert_not_reached ();
  }
}


/* Called by the idle function */
void
gst_gl_display_thread_do_upload (GstGLDisplay *display)
{
  gst_gl_display_thread_do_upload_fill (display);

  switch (display->upload_video_format)
  {
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
    //color space conversion is not needed
    //but if the size is different we need to redraw it
    //using fbo
    if (display->upload_width != display->upload_data_width ||
        display->upload_height != display->upload_data_height)
      gst_gl_display_thread_do_upload_draw (display);
    break;
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  case GST_VIDEO_FORMAT_AYUV:
    {
      switch (display->upload_colorspace_conversion)
      {
      case GST_GL_DISPLAY_CONVERSION_GLSL:
        //color space conversion is needed
        gst_gl_display_thread_do_upload_draw (display);
        break;
      case GST_GL_DISPLAY_CONVERSION_MATRIX:
        //color space conversion is needed
        //not yet supported
        break;
      case GST_GL_DISPLAY_CONVERSION_MESA:
        //color space conversion is not needed
        //but if the size is different we need to redraw it
        //using fbo
        if (display->upload_width != display->upload_data_width ||
            display->upload_height != display->upload_data_height)
          gst_gl_display_thread_do_upload_draw (display);
        break;
      default:
	      g_assert_not_reached ();
      }
    }
    break;
  default:
	  g_assert_not_reached ();
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_init_download (GstGLDisplay *display)
{
  switch (display->download_video_format)
  {
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
    //color space conversion is not needed
    break;
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  case GST_VIDEO_FORMAT_AYUV:
    //color space conversion is needed
  {

    if (GLEW_EXT_framebuffer_object)
    {
      GST_INFO ("Context, EXT_framebuffer_object supported: yes");

      //-- init output frame buffer object (GL -> video)

      //setup FBO
      glGenFramebuffersEXT (1, &display->download_fbo);
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->download_fbo);

      //setup the render buffer for depth
      glGenRenderbuffersEXT(1, &display->download_depth_buffer);
      glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->download_depth_buffer);
      glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
			       display->download_width, display->download_height);

      //setup a first texture to render to
      glGenTextures (1, &display->download_texture);
      glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->download_texture);
      glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
		   display->download_width, display->download_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      //attach the first texture to the FBO to renderer to
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
			        GL_TEXTURE_RECTANGLE_ARB, display->download_texture, 0);

      switch (display->download_video_format)
      {
      case GST_VIDEO_FORMAT_YUY2:
      case GST_VIDEO_FORMAT_UYVY:
      case GST_VIDEO_FORMAT_AYUV:
        //only one attached texture is needed
        break;

      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
        //setup a second texture to render to
        glGenTextures (1, &display->download_texture_u);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->download_texture_u);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
		     display->download_width, display->download_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the second texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT,
				  GL_TEXTURE_RECTANGLE_ARB, display->download_texture_u, 0);

        //setup a third texture to render to
        glGenTextures (1, &display->download_texture_v);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->download_texture_v);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
		     display->download_width, display->download_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the third texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT,
				  GL_TEXTURE_RECTANGLE_ARB, display->download_texture_v, 0);

        display->multipleRT[0] = GL_COLOR_ATTACHMENT0_EXT;
        display->multipleRT[1] = GL_COLOR_ATTACHMENT1_EXT;
        display->multipleRT[2] = GL_COLOR_ATTACHMENT2_EXT;
        break;
      default:
        g_assert_not_reached ();
      }

      //attach the depth render buffer to the FBO
      glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
				   GL_RENDERBUFFER_EXT, display->download_depth_buffer);

      gst_gl_display_check_framebuffer_status();

      g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
	        GL_FRAMEBUFFER_COMPLETE_EXT);

      //unbind the FBO
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
      }
      else
      {
        //turn off the pipeline because Frame buffer object is a requirement when using filters
        //or when using GLSL colorspace conversion
        GST_WARNING ("Context, EXT_framebuffer_object supported: no");
        display->isAlive = FALSE;
      }
    }
    break;
  default:
    g_assert_not_reached ();
  }

  switch (display->download_video_format)
  {
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
    //color space conversion is not needed
    break;
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  case GST_VIDEO_FORMAT_AYUV:
    //color space conversion is needed
  {
    //check if fragment shader is available, then load them
    //GLSL is a requirement for donwload
    if (GLEW_ARB_fragment_shader)
    {
      switch (display->download_video_format)
      {
      case GST_VIDEO_FORMAT_YUY2:
      {
	gchar text_shader_download_YUY2[2048];
	sprintf (text_shader_download_YUY2, display->text_shader_download_YUY2_UYVY, "y2,u,y1,v");

	display->shader_download_YUY2 = gst_gl_shader_new ();
	if(!gst_gl_shader_compile_and_check (display->shader_download_YUY2,
					     text_shader_download_YUY2, GST_GL_SHADER_FRAGMENT_SOURCE))
	{
	  display->isAlive = FALSE;
	  g_object_unref (G_OBJECT (display->shader_download_YUY2));
	  display->shader_download_YUY2 = NULL;
	}
      }
      break;
      case GST_VIDEO_FORMAT_UYVY:
      {
	gchar text_shader_download_UYVY[2048];
	sprintf (text_shader_download_UYVY, display->text_shader_download_YUY2_UYVY, "v,y1,u,y2");

	display->shader_download_UYVY = gst_gl_shader_new ();
	if(!gst_gl_shader_compile_and_check (display->shader_download_UYVY,
					     text_shader_download_UYVY, GST_GL_SHADER_FRAGMENT_SOURCE))
	{
	  display->isAlive = FALSE;
	  g_object_unref (G_OBJECT (display->shader_download_UYVY));
	  display->shader_download_UYVY = NULL;
	}
      }
      break;
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
	display->shader_download_I420_YV12 = gst_gl_shader_new ();
	if(!gst_gl_shader_compile_and_check (display->shader_download_I420_YV12,
					     display->text_shader_download_I420_YV12, GST_GL_SHADER_FRAGMENT_SOURCE))
	{
	  display->isAlive = FALSE;
	  g_object_unref (G_OBJECT (display->shader_download_I420_YV12));
	  display->shader_download_I420_YV12 = NULL;
	}
	break;
      case GST_VIDEO_FORMAT_AYUV:
	display->shader_download_AYUV = gst_gl_shader_new ();
	if(!gst_gl_shader_compile_and_check (display->shader_download_AYUV,
					     display->text_shader_download_AYUV, GST_GL_SHADER_FRAGMENT_SOURCE))
	{
	  display->isAlive = FALSE;
	  g_object_unref (G_OBJECT (display->shader_download_AYUV));
	  display->shader_download_AYUV = NULL;
	}
	break;
      default:
	g_assert_not_reached ();
      }
    }
    else
    {
      //turn off the pipeline because colorspace conversion is not possible
      GST_WARNING ("Context, ARB_fragment_shader supported: no");
      display->isAlive = FALSE;
    }
  }
  break;
  default:
    g_assert_not_reached ();
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_do_download (GstGLDisplay * display)
{
  switch (display->download_video_format)
  {
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
    //color space conversion is not needed
    gst_gl_display_thread_do_download_draw_rgb (display);
    break;
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  case GST_VIDEO_FORMAT_AYUV:
    //color space conversion is needed
    gst_gl_display_thread_do_download_draw_yuv (display);
    break;
  default:
    g_assert_not_reached ();
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_gen_fbo (GstGLDisplay *display)
{
  //a texture must be attached to the FBO
  GLuint fake_texture = 0;

  //-- generate frame buffer object

  if (!GLEW_EXT_framebuffer_object) {
    //turn off the pipeline because Frame buffer object is a not present
    GST_WARNING ("Context, EXT_framebuffer_object supported: no");
    display->isAlive = FALSE;
    return;
  }

  //setup FBO
  glGenFramebuffersEXT (1, &display->generated_fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->generated_fbo);

  //setup the render buffer for depth
  glGenRenderbuffersEXT(1, &display->generated_depth_buffer);
  glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->generated_depth_buffer);
  glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
			   display->gen_fbo_width, display->gen_fbo_height);

  //setup a texture to render to
  glGenTextures (1, &fake_texture);
  glBindTexture(GL_TEXTURE_RECTANGLE_ARB, fake_texture);
  glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
	       display->gen_fbo_width, display->gen_fbo_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  //attach the texture to the FBO to renderer to
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
			    GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

  //attach the depth render buffer to the FBO
  glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
			       GL_RENDERBUFFER_EXT, display->generated_depth_buffer);

  g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
	    GL_FRAMEBUFFER_COMPLETE_EXT);

  //unbind the FBO
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

  glDeleteTextures (1, &fake_texture);
}


/* Called in the gl thread */
void
gst_gl_display_thread_use_fbo (GstGLDisplay *display)
{
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->use_fbo);

  //setup a texture to render to
  glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture);

  //attach the texture to the FBO to renderer to
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
			    GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture, 0);

  if (GLEW_ARB_fragment_shader)
    gst_gl_shader_use (NULL);


  glPushAttrib(GL_VIEWPORT_BIT);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();

  switch (display->use_fbo_projection)
  {
  case GST_GL_DISPLAY_PROJECTION_ORTHO2D:
    gluOrtho2D(display->use_fbo_proj_param1, display->use_fbo_proj_param2,
	       display->use_fbo_proj_param3, display->use_fbo_proj_param4);
    break;
  case GST_GL_DISPLAY_PROJECTION_PERSPECIVE:
    gluPerspective(display->use_fbo_proj_param1, display->use_fbo_proj_param2,
		   display->use_fbo_proj_param3, display->use_fbo_proj_param4);
    break;
  default:
    g_assert_not_reached ();
  }

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glViewport(0, 0, display->use_fbo_width, display->use_fbo_height);

  glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  //the opengl scene
  display->use_fbo_scene_cb (display->input_texture_width, display->input_texture_height,
			     display->input_texture, display->use_fbo_stuff);

  glDrawBuffer(GL_NONE);

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glPopAttrib();

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}


/* Called in the gl thread */
void
gst_gl_display_thread_del_fbo (GstGLDisplay* display)
{
  if (display->del_fbo)
  {
    glDeleteFramebuffersEXT (1, &display->del_fbo);
    display->del_fbo = 0;
  }
  if (display->del_depth_buffer)
  {
    glDeleteRenderbuffersEXT(1, &display->del_depth_buffer);
    display->del_depth_buffer = 0;
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_gen_shader (GstGLDisplay* display)
{
  if (GLEW_ARB_fragment_shader)
  {
    if (display->gen_shader_vertex_source ||
         display->gen_shader_fragment_source)
    {
      gboolean isAlive = TRUE;
      GError *error = NULL;

      display->gen_shader = gst_gl_shader_new ();

      if (display->gen_shader_vertex_source)
        gst_gl_shader_set_vertex_source(display->gen_shader, display->gen_shader_vertex_source);

      if (display->gen_shader_fragment_source)
        gst_gl_shader_set_fragment_source(display->gen_shader, display->gen_shader_fragment_source);

      gst_gl_shader_compile (display->gen_shader, &error);
      if (error)
      {
        GST_ERROR ("%s", error->message);
        g_error_free (error);
        error = NULL;
        gst_gl_shader_use (NULL);
        isAlive = FALSE;
      }

      if (!isAlive)
      {
        display->isAlive = FALSE;
        g_object_unref (G_OBJECT (display->gen_shader));
        display->gen_shader = NULL;
      }
    }
  }
  else
  {
    GST_WARNING ("One of the filter required ARB_fragment_shader");
    display->isAlive = FALSE;
    display->gen_shader = NULL;
  }
}


/* Called in the gl thread */
void
gst_gl_display_thread_del_shader (GstGLDisplay* display)
{
  if (display->del_shader)
  {
    g_object_unref (G_OBJECT (display->del_shader));
    display->del_shader = NULL;
  }
}


//------------------------------------------------------------
//------------------ BEGIN GL THREAD ACTIONS -----------------
//------------------------------------------------------------


//------------------------------------------------------------
//---------------------- BEGIN PRIVATE -----------------------
//------------------------------------------------------------


void
gst_gl_display_lock (GstGLDisplay * display)
{
  g_mutex_lock (display->mutex);
}


void
gst_gl_display_unlock (GstGLDisplay * display)
{
  g_mutex_unlock (display->mutex);
}


void
gst_gl_display_on_resize (GstGLDisplay* display, gint width, gint height)
{
  //check if a client reshape callback is registered
  if (display->clientReshapeCallback)
    display->clientReshapeCallback(width, height);

  //default reshape
  else
  {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, width, 0, height);
    glMatrixMode(GL_MODELVIEW);
  }
}


void gst_gl_display_on_draw(GstGLDisplay* display)
{
  //check if tecture is ready for being drawn
  if (!display->redisplay_texture)
    return;

  //opengl scene
  GST_DEBUG ("on draw");

  //make sure that the environnement is clean
  if (display->upload_colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
    glUseProgramObjectARB (0);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

  //check if a client draw callback is registered
  if (display->clientDrawCallback)
  {
    gboolean doRedisplay =
      display->clientDrawCallback (display->redisplay_texture,
				  display->redisplay_texture_width, display->redisplay_texture_height);

    if (doRedisplay && display->gl_window)
      gst_gl_window_draw_unlocked (display->gl_window);
  }
  //default opengl scene
  else
  {

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->redisplay_texture);
    glEnable (GL_TEXTURE_RECTANGLE_ARB);

    glBegin (GL_QUADS);
    glTexCoord2i (display->redisplay_texture_width, 0);
    glVertex2f (1.0f, 1.0f);
    glTexCoord2i (0, 0);
    glVertex2f (-1.0f, 1.0f);
    glTexCoord2i (0, display->redisplay_texture_height);
    glVertex2f (-1.0f, -1.0f);
    glTexCoord2i (display->redisplay_texture_width, display->redisplay_texture_height);
    glVertex2f (1.0f, -1.0f);
    glEnd ();

    glDisable(GL_TEXTURE_RECTANGLE_ARB);
  }//end default opengl scene
}


void gst_gl_display_on_close (GstGLDisplay* display)
{
  GST_INFO ("on close");

  display->isAlive = FALSE;
}


/* Generate a texture if no one is available in the pool
 * Called in the gl thread */
void
gst_gl_display_glgen_texture (GstGLDisplay* display, GLuint* pTexture, GLint width, GLint height)
{
  if (display->isAlive)
  {
    GQueue* sub_texture_pool = NULL;

    //make a unique key from w and h
    //the key cannot be w*h because (4*6 = 6*4 = 2*12 = 12*2)
    guint key = (gint)width;
    key <<= 16;
    key |= (gint)height;
    sub_texture_pool = g_hash_table_lookup (display->texture_pool, GUINT_TO_POINTER (key));

    //if there is a sub texture pool associated to th given key
    if (sub_texture_pool && g_queue_get_length(sub_texture_pool) > 0)
    {
      //a texture is available in the pool
      GstGLDisplayTex* tex = g_queue_pop_head (sub_texture_pool);
      *pTexture = tex->texture;
      g_free (tex);
      GST_LOG ("get texture id:%d from the sub texture pool: %d",
        *pTexture, key);
    }
    else
    {
      //sub texture pool does not exist yet or empty
      glGenTextures (1, pTexture);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture);

      switch (display->upload_video_format)
      {
        case GST_VIDEO_FORMAT_RGB:
        case GST_VIDEO_FORMAT_BGR:
        case GST_VIDEO_FORMAT_RGBx:
        case GST_VIDEO_FORMAT_BGRx:
        case GST_VIDEO_FORMAT_xRGB:
        case GST_VIDEO_FORMAT_xBGR:
        case GST_VIDEO_FORMAT_RGBA:
        case GST_VIDEO_FORMAT_BGRA:
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ABGR:
          glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
            width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
          break;
        case GST_VIDEO_FORMAT_YUY2:
        case GST_VIDEO_FORMAT_UYVY:
          switch (display->upload_colorspace_conversion)
          {
          case GST_GL_DISPLAY_CONVERSION_GLSL:
          case GST_GL_DISPLAY_CONVERSION_MATRIX:
            glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
              width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            break;
          case GST_GL_DISPLAY_CONVERSION_MESA:
            if (display->upload_width != display->upload_data_width ||
                display->upload_height != display->upload_data_height)
              glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            else
              glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA,width, height,
                0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
            break;
          default:
            g_assert_not_reached ();
          }
          break;
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
        case GST_VIDEO_FORMAT_AYUV:
          glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
            width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
          break;
        default:
            g_assert_not_reached ();
      }

      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      GST_LOG ("generate texture id:%d", *pTexture);
    }

  }
  else
    *pTexture = 0;
}


/* Delete a texture, actually the texture is just added to the pool
 * Called in the gl thread */
void
gst_gl_display_gldel_texture (GstGLDisplay* display, GLuint* pTexture, GLint width, GLint height)
{
  //Each existing texture is destroyed only when the pool is destroyed
  //The pool of textures is deleted in the GstGLDisplay destructor

  GQueue* sub_texture_pool = NULL;
  GstGLDisplayTex* tex = NULL;

  //make a unique key from w and h
  //the key cannot be w*h because (4*6 = 6*4 = 2*12 = 12*2)
  guint key = (gint)width;
  key <<= 16;
  key |= (gint)height;
  sub_texture_pool = g_hash_table_lookup (display->texture_pool, GUINT_TO_POINTER (key));

  //if the size is known
  if (!sub_texture_pool)
  {
    sub_texture_pool = g_queue_new ();
    g_hash_table_insert (display->texture_pool, GUINT_TO_POINTER (key), sub_texture_pool);

    GST_INFO ("one more sub texture pool inserted: %d ", key);
    GST_INFO ("nb sub texture pools: %d", g_hash_table_size (display->texture_pool));
  }

  //contruct a sub texture pool element
  tex = g_new0 (GstGLDisplayTex, 1);
  tex->texture = *pTexture;
  *pTexture = 0;

  //add tex to the pool, it makes texture allocation reusable
  g_queue_push_tail (sub_texture_pool, tex);
  GST_LOG ("texture id:%d added to the sub texture pool: %d",
    tex->texture, key);
  GST_LOG ("%d texture(s) in the sub texture pool: %d",
    g_queue_get_length (sub_texture_pool), key);
}


/* call when a sub texture pool is removed from the texture pool (ghash table) */
gboolean gst_gl_display_texture_pool_func_clean (gpointer key, gpointer value, gpointer data)
{
  GQueue* sub_texture_pool = (GQueue*) value;

  while (g_queue_get_length (sub_texture_pool) > 0)
  {
    GstGLDisplayTex* tex = g_queue_pop_head (sub_texture_pool);
    GST_INFO ("trying to delete texture id: %d deleted", tex->texture);
    glDeleteTextures (1, &tex->texture);
    GST_INFO ("texture id: %d deleted", tex->texture);
    g_free (tex);
  }

  g_queue_free (sub_texture_pool);

  return TRUE;
}


/* called in the gl thread */
void
gst_gl_display_check_framebuffer_status(void)
{
  GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);

  switch(status)
  {
  case GL_FRAMEBUFFER_COMPLETE_EXT:
    break;

  case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
    GST_ERROR ("GL_FRAMEBUFFER_UNSUPPORTED_EXT");
    break;

  default:
    GST_ERROR ("General FBO error");
  }
}


//------------------------------------------------------------
//---------------------  END PRIVATE -------------------------
//------------------------------------------------------------


//------------------------------------------------------------
//---------------------- BEGIN PUBLIC ------------------------
//------------------------------------------------------------


/* Called by the first gl element of a video/x-raw-gl flow */
GstGLDisplay*
gst_gl_display_new (void)
{
  return g_object_new (GST_TYPE_GL_DISPLAY, NULL);
}


/* Create an opengl context (one context for one GstGLDisplay)
 * Called by the first gl element of a video/x-raw-gl flow */
void
gst_gl_display_create_context (GstGLDisplay *display,
                               GLint width, GLint height)
{
  gst_gl_display_lock (display);

  display->upload_width = width;
  display->upload_height = height;

  display->gl_thread = g_thread_create (
    (GThreadFunc) gst_gl_display_thread_create_context, display, TRUE, NULL);

  g_cond_wait (display->cond_create_context, display->mutex);

  GST_INFO ("gl thread created");

  gst_gl_display_unlock (display);
}


/* Called by the glimagesink element */
gboolean
gst_gl_display_redisplay (GstGLDisplay* display, GLuint texture, gint width, gint height)
{
  gboolean isAlive = TRUE;

  gst_gl_display_lock (display);
  isAlive = display->isAlive;
  if (isAlive)
  {
    if (texture)
    {
      display->redisplay_texture = texture;
      display->redisplay_texture_width = width;
      display->redisplay_texture_height = height;
    }
    if (display->gl_window)
      gst_gl_window_draw (display->gl_window);
  }
  gst_gl_display_unlock (display);

  return isAlive;
}

void
gst_gl_display_thread_add (GstGLDisplay *display,
			   GstGLDisplayThreadFunc func, gpointer data)
{
  gst_gl_display_lock (display);
  display->data = data;
  display->generic_callback = func;
  gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_run_generic), display);
  gst_gl_display_unlock (display);
}

/* Called by gst_gl_buffer_new */
void
gst_gl_display_gen_texture (GstGLDisplay* display, GLuint* pTexture, GLint width, GLint height)
{
  gst_gl_display_lock (display);
  display->gen_texture_width = width;
  display->gen_texture_height = height;
  gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_gen_texture), display);
  *pTexture = display->gen_texture;
  gst_gl_display_unlock (display);
}


/* Called by gst_gl_buffer_finalize */
void
gst_gl_display_del_texture (GstGLDisplay* display, GLuint texture, GLint width, GLint height)
{
  gst_gl_display_lock (display);
  if (texture)
  {
    display->del_texture = texture;
    display->del_texture_width = width;
    display->del_texture_height = height;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_del_texture), display);
  }
  gst_gl_display_unlock (display);
}


/* Called by the first gl element of a video/x-raw-gl flow */
void
gst_gl_display_init_upload (GstGLDisplay* display, GstVideoFormat video_format,
                            guint gl_width, guint gl_height,
                            gint video_width, gint video_height)
{
  gst_gl_display_lock (display);
  display->upload_video_format = video_format;
  display->upload_width = gl_width;
  display->upload_height = gl_height;
  display->upload_data_width = video_width;
  display->upload_data_height = video_height;
  gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_init_upload), display);
  gst_gl_display_unlock (display);
}


/* Called by the first gl element of a video/x-raw-gl flow */
gboolean
gst_gl_display_do_upload (GstGLDisplay *display, GLuint texture,
                          gint data_width, gint data_height,
                          gpointer data)
{
  gboolean isAlive = TRUE;

  gst_gl_display_lock (display);
  isAlive = display->isAlive;
  if (isAlive)
  {
    display->upload_outtex = texture;
    display->upload_data_width = data_width;
    display->upload_data_height = data_height;
    display->upload_data = data;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_do_upload), display);
  }
  gst_gl_display_unlock (display);

  return isAlive;
}


/* Called by the gldownload and glcolorscale element */
void
gst_gl_display_init_download (GstGLDisplay* display, GstVideoFormat video_format,
                              gint width, gint height)
{
  gst_gl_display_lock (display);
  display->download_video_format = video_format;
  display->download_width = width;
  display->download_height = height;
  gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_init_download), display);
  gst_gl_display_unlock (display);
}


/* Called by the gldownload and glcolorscale element */
gboolean
gst_gl_display_do_download (GstGLDisplay* display, GLuint texture,
                            gint width, gint height,
                            gpointer data)
{
  gboolean isAlive = TRUE;

  gst_gl_display_lock (display);
  isAlive = display->isAlive;
  if (isAlive)
  {
    //data size is aocciated to the glcontext size
    display->download_data = data;
    display->ouput_texture = texture;
    display->ouput_texture_width = width;
    display->ouput_texture_height = height;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_do_download), display);
  }
  gst_gl_display_unlock (display);

  return isAlive;
}


/* Called by gltestsrc and glfilter */
void
gst_gl_display_gen_fbo (GstGLDisplay* display, gint width, gint height,
                        GLuint* fbo, GLuint* depthbuffer)
{
  gst_gl_display_lock (display);
  if (display->isAlive)
  {
    display->gen_fbo_width = width;
    display->gen_fbo_height = height;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_gen_fbo), display);
    *fbo = display->generated_fbo;
    *depthbuffer = display->generated_depth_buffer;
  }
  gst_gl_display_unlock (display);
}


/* Called by glfilter */
/* this function really has to be simplified...  do we really need to
   set projection this way? Wouldn't be better a set_projection
   separate call? or just make glut functions available out of
   gst-libs and call it if needed on drawcallback? -- Filippo */
/* GLCB too.. I think that only needed parameters should be
 * GstGLDisplay *display and gpointer data, or just gpointer data */
/* ..everything here has to be simplified! */
gboolean
gst_gl_display_use_fbo (GstGLDisplay* display, gint texture_fbo_width, gint texture_fbo_height,
                        GLuint fbo, GLuint depth_buffer, GLuint texture_fbo, GLCB cb,
                        gint input_texture_width, gint input_texture_height, GLuint input_texture,
                        gdouble proj_param1, gdouble proj_param2,
                        gdouble proj_param3, gdouble proj_param4,
                        GstGLDisplayProjection projection, gpointer* stuff)
{
  gboolean isAlive = TRUE;

  gst_gl_display_lock (display);
  isAlive = display->isAlive;
  if (isAlive)
  {
    display->use_fbo = fbo;
    display->use_depth_buffer = depth_buffer;
    display->use_fbo_texture = texture_fbo;
    display->use_fbo_width = texture_fbo_width;
    display->use_fbo_height = texture_fbo_height;
    display->use_fbo_scene_cb = cb;
    display->use_fbo_proj_param1 = proj_param1;
    display->use_fbo_proj_param2 = proj_param2;
    display->use_fbo_proj_param3 = proj_param3;
    display->use_fbo_proj_param4 = proj_param4;
    display->use_fbo_projection = projection;
    display->use_fbo_stuff = stuff;
    display->input_texture_width = input_texture_width;
    display->input_texture_height = input_texture_height;
    display->input_texture = input_texture;
    gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_use_fbo), display);
  }
  gst_gl_display_unlock (display);

  return isAlive;
}


/* Called by gltestsrc and glfilter */
void
gst_gl_display_del_fbo (GstGLDisplay* display, GLuint fbo,
                        GLuint depth_buffer)
{
  gst_gl_display_lock (display);
  display->del_fbo = fbo;
  display->del_depth_buffer = depth_buffer;
  gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_del_fbo), display);
  gst_gl_display_unlock (display);
}


/* Called by glfilter */
void
gst_gl_display_gen_shader (GstGLDisplay* display,
                           const gchar* shader_vertex_source,
                           const gchar* shader_fragment_source,
                           GstGLShader** shader)
{
  gst_gl_display_lock (display);
  display->gen_shader_vertex_source = shader_vertex_source;
  display->gen_shader_fragment_source = shader_fragment_source;
  gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_gen_shader), display);
  if (shader)
    *shader = display->gen_shader;
  display->gen_shader = NULL;
  display->gen_shader_vertex_source = NULL;
  display->gen_shader_fragment_source = NULL;
  gst_gl_display_unlock (display);
}


/* Called by glfilter */
void
gst_gl_display_del_shader (GstGLDisplay* display, GstGLShader* shader)
{
  gst_gl_display_lock (display);
  display->del_shader = shader;
  gst_gl_window_send_message (display->gl_window, GST_GL_WINDOW_CB (gst_gl_display_thread_del_shader), display);
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_window_id (GstGLDisplay* display, gulong window_id)
{
  gst_gl_display_lock (display);
  gst_gl_window_set_external_window_id (display->gl_window, window_id);
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_reshape_callback (GstGLDisplay * display, CRCB cb)
{
  gst_gl_display_lock (display);
  display->clientReshapeCallback = cb;
  gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_draw_callback (GstGLDisplay * display, CDCB cb)
{
  gst_gl_display_lock (display);
  display->clientDrawCallback = cb;
  gst_gl_display_unlock (display);
}


//------------------------------------------------------------
//------------------------ END PUBLIC ------------------------
//------------------------------------------------------------

/* called by gst_gl_display_thread_init_upload (in the gl thread) */
void gst_gl_display_thread_init_upload_fbo (GstGLDisplay *display)
{
  //Frame buffer object is a requirement for every cases
  if (GLEW_EXT_framebuffer_object)
  {
    //a texture must be attached to the FBO
    GLuint fake_texture = 0;

    GST_INFO ("Context, EXT_framebuffer_object supported: yes");

    //-- init intput frame buffer object (video -> GL)

    //setup FBO
    glGenFramebuffersEXT (1, &display->upload_fbo);
    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->upload_fbo);

    //setup the render buffer for depth
    glGenRenderbuffersEXT(1, &display->upload_depth_buffer);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->upload_depth_buffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
		       display->upload_width, display->upload_height);

    //a fake texture is attached to the upload FBO (cannot init without it)
    glGenTextures (1, &fake_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, fake_texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
       display->upload_width, display->upload_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    //attach the texture to the FBO to renderer to
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
		        GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

    //attach the depth render buffer to the FBO
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
		       GL_RENDERBUFFER_EXT, display->upload_depth_buffer);

    gst_gl_display_check_framebuffer_status();

    g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
        GL_FRAMEBUFFER_COMPLETE_EXT);

    //unbind the FBO
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    glDeleteTextures (1, &fake_texture);

    //alloc texture (related to upload) memory only on time
    gst_gl_display_thread_do_upload_make (display);
  }
  else
  {
    //turn off the pipeline because Frame buffer object is a not present
    GST_WARNING ("Context, EXT_framebuffer_object supported: no");
    display->isAlive = FALSE;
  }
}

/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void gst_gl_display_thread_do_upload_make (GstGLDisplay *display)
{
  gint width = display->upload_data_width;
  gint height = display->upload_data_height;

  glGenTextures (1, &display->upload_intex);

  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
  switch (display->upload_video_format)
  {
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
	    width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    break;
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
	    width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    break;
  case GST_VIDEO_FORMAT_AYUV:
		glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
	    width, height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, NULL);
    break;
  case GST_VIDEO_FORMAT_YUY2:
    switch (display->upload_colorspace_conversion)
      {
      case GST_GL_DISPLAY_CONVERSION_GLSL:
      case GST_GL_DISPLAY_CONVERSION_MATRIX:
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
          width, height,
          0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);
        glGenTextures (1, &display->upload_intex_u);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
          width, height,
          0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
        break;
      case GST_GL_DISPLAY_CONVERSION_MESA:
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
        break;
      default:
        g_assert_not_reached ();
      }
    break;
  case GST_VIDEO_FORMAT_UYVY:
    switch (display->upload_colorspace_conversion)
      {
      case GST_GL_DISPLAY_CONVERSION_GLSL:
      case GST_GL_DISPLAY_CONVERSION_MATRIX:
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
          width, height,
          0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);
        glGenTextures (1, &display->upload_intex_u);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
          width, height,
          0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
        break;
      case GST_GL_DISPLAY_CONVERSION_MESA:
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
        break;
      default:
        g_assert_not_reached ();
      }
    break;
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
      width, height,
      0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    glGenTextures (1, &display->upload_intex_u);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
      GST_ROUND_UP_2 (width) / 2,
      GST_ROUND_UP_2 (height) / 2,
      0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    glGenTextures (1, &display->upload_intex_v);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_v);
    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
      GST_ROUND_UP_2 (width) / 2,
      GST_ROUND_UP_2 (height) / 2,
      0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    break;

  default:
    g_assert_not_reached ();
  }
}


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
gst_gl_display_thread_do_upload_fill (GstGLDisplay *display)
{
  gint width = display->upload_data_width;
  gint height = display->upload_data_height;
  gpointer data = display->upload_data;

  switch (display->upload_video_format)
  {
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
    //color space conversion is not needed
    if (display->upload_width != display->upload_data_width ||
        display->upload_height != display->upload_data_height)
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
    else
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_outtex);
    break;
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  case GST_VIDEO_FORMAT_AYUV:
    switch (display->upload_colorspace_conversion)
    {
    case GST_GL_DISPLAY_CONVERSION_GLSL:
    case GST_GL_DISPLAY_CONVERSION_MATRIX:
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
      break;
    case GST_GL_DISPLAY_CONVERSION_MESA:
      if (display->upload_width != display->upload_data_width ||
          display->upload_height != display->upload_data_height)
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
      else
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_outtex);
      break;
    default:
      g_assert_not_reached();
    }
    break;
  default:
	  g_assert_not_reached ();
  }

  switch (display->upload_video_format) {
  case GST_VIDEO_FORMAT_RGB:
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		     GL_RGB, GL_UNSIGNED_BYTE, data);
    break;
  case GST_VIDEO_FORMAT_BGR:
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		     GL_BGR, GL_UNSIGNED_BYTE, data);
    break;
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_RGBA:
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		     GL_RGBA, GL_UNSIGNED_BYTE, data);
    break;
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_BGRA:
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		     GL_BGRA, GL_UNSIGNED_BYTE, data);
    break;
  case GST_VIDEO_FORMAT_AYUV:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_ARGB:
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		     GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
    break;
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_ABGR:
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		     GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
    break;
  case GST_VIDEO_FORMAT_YUY2:
    switch (display->upload_colorspace_conversion)
    {
    case GST_GL_DISPLAY_CONVERSION_GLSL:
    case GST_GL_DISPLAY_CONVERSION_MATRIX:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		       GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, data);

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		       GST_ROUND_UP_2 (width) / 2, height,
		       GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
      break;
    case GST_GL_DISPLAY_CONVERSION_MESA:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		       GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    default:
      g_assert_not_reached ();
    }
    break;
  case GST_VIDEO_FORMAT_UYVY:
    switch (display->upload_colorspace_conversion)
    {
    case GST_GL_DISPLAY_CONVERSION_GLSL:
    case GST_GL_DISPLAY_CONVERSION_MATRIX:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		       GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, data);

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		       GST_ROUND_UP_2 (width) / 2, height,
		       GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
      break;
    case GST_GL_DISPLAY_CONVERSION_MESA:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		       GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    default:
      g_assert_not_reached ();
    }
    break;
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  {
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE, data);

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		     GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE,
		     (guint8 *) data +
		     gst_video_format_get_component_offset (display->upload_video_format, 1, width, height));

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_v);
    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
		     GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE,
		     (guint8 *) data +
		     gst_video_format_get_component_offset (display->upload_video_format, 2, width, height));
  }
  break;
  default:
    g_assert_not_reached ();
  }
}


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
gst_gl_display_thread_do_upload_draw (GstGLDisplay *display)
{
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->upload_fbo);

  //setup a texture to render to
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->upload_outtex);

  //attach the texture to the FBO to renderer to
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
			    GL_TEXTURE_RECTANGLE_ARB, display->upload_outtex, 0);

  if (GLEW_ARB_fragment_shader)
    gst_gl_shader_use (NULL);

  glPushAttrib(GL_VIEWPORT_BIT);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluOrtho2D(0.0, display->upload_width, 0.0, display->upload_height);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glViewport(0, 0, display->upload_width, display->upload_height);

  glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  switch (display->upload_video_format)
  {
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_ARGB:
  case GST_VIDEO_FORMAT_ABGR:
  case GST_VIDEO_FORMAT_RGB:
  case GST_VIDEO_FORMAT_BGR:
	{
		glMatrixMode (GL_PROJECTION);
		glLoadIdentity ();

		glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
		glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}
	break;

  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  {
    switch (display->upload_colorspace_conversion)
    {
    case GST_GL_DISPLAY_CONVERSION_GLSL:
    case GST_GL_DISPLAY_CONVERSION_MATRIX:
    {
        GstGLShader* shader_upload_YUY2_UYVY = NULL;

        switch (display->upload_video_format)
        {
        case GST_VIDEO_FORMAT_YUY2:
          shader_upload_YUY2_UYVY = display->shader_upload_YUY2;
          break;
        case GST_VIDEO_FORMAT_UYVY:
          shader_upload_YUY2_UYVY = display->shader_upload_UYVY;
          break;
        default:
          g_assert_not_reached ();
        }

        gst_gl_shader_use (shader_upload_YUY2_UYVY);

        glMatrixMode (GL_PROJECTION);
        glLoadIdentity ();

        glActiveTextureARB(GL_TEXTURE1_ARB);
        gst_gl_shader_set_uniform_1i (shader_upload_YUY2_UYVY, "UVtex", 1);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glActiveTextureARB(GL_TEXTURE0_ARB);
        gst_gl_shader_set_uniform_1i (shader_upload_YUY2_UYVY, "Ytex", 0);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
      break;
    case GST_GL_DISPLAY_CONVERSION_MESA:
      {
        glMatrixMode (GL_PROJECTION);
        glLoadIdentity ();

        glEnable(GL_TEXTURE_RECTANGLE_ARB);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      }
      break;
    default:
      g_assert_not_reached ();
    }
  }
  break;

  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  {
    gst_gl_shader_use (display->shader_upload_I420_YV12);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glActiveTextureARB(GL_TEXTURE1_ARB);
    gst_gl_shader_set_uniform_1i (display->shader_upload_I420_YV12, "Utex", 1);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_u);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTextureARB(GL_TEXTURE2_ARB);
    gst_gl_shader_set_uniform_1i (display->shader_upload_I420_YV12, "Vtex", 2);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex_v);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTextureARB(GL_TEXTURE0_ARB);
    gst_gl_shader_set_uniform_1i (display->shader_upload_I420_YV12, "Ytex", 0);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  break;

  case GST_VIDEO_FORMAT_AYUV:
  {
    gst_gl_shader_use (display->shader_upload_AYUV);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glActiveTextureARB(GL_TEXTURE0_ARB);
    gst_gl_shader_set_uniform_1i (display->shader_upload_AYUV, "tex", 0);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->upload_intex);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  break;

  default:
    g_assert_not_reached ();

  }//end switch display->currentVideo_format

  glBegin (GL_QUADS);
  glTexCoord2i (display->upload_data_width, 0);
  glVertex2f (1.0f, -1.0f);
  glTexCoord2i (0, 0);
  glVertex2f (-1.0f, -1.0f);
  glTexCoord2i (0, display->upload_data_height);
  glVertex2f (-1.0f, 1.0f);
  glTexCoord2i (display->upload_data_width, display->upload_data_height);
  glVertex2f (1.0f, 1.0f);
  glEnd ();

  glDrawBuffer(GL_NONE);

  //we are done with the shader
  if (display->upload_colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
    glUseProgramObjectARB (0);

  glDisable(GL_TEXTURE_RECTANGLE_ARB);

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glPopAttrib();

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

  gst_gl_display_check_framebuffer_status();
}


/* called by gst_gl_display_thread_do_download (in the gl thread) */
void
gst_gl_display_thread_do_download_draw_rgb (GstGLDisplay *display)
{
  GstVideoFormat video_format = display->download_video_format;
  gpointer data = display->download_data;

  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);

  switch (video_format)
  {
  case GST_VIDEO_FORMAT_RGBA:
  case GST_VIDEO_FORMAT_RGBx:
  case GST_VIDEO_FORMAT_xRGB:
  case GST_VIDEO_FORMAT_ARGB:
    glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
		  GL_UNSIGNED_BYTE, data);
    break;
  case GST_VIDEO_FORMAT_BGRx:
  case GST_VIDEO_FORMAT_BGRA:
  case GST_VIDEO_FORMAT_xBGR:
  case GST_VIDEO_FORMAT_ABGR:
    glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_BGRA,
		  GL_UNSIGNED_BYTE, data);
    break;
  case GST_VIDEO_FORMAT_RGB:
    glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
		  GL_UNSIGNED_BYTE, data);
    break;
  case GST_VIDEO_FORMAT_BGR:
    glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_BGR,
		  GL_UNSIGNED_BYTE, data);
    break;
  default:
      g_assert_not_reached ();
    }
}


/* called by gst_gl_display_thread_do_download (in the gl thread) */
void
gst_gl_display_thread_do_download_draw_yuv (GstGLDisplay *display)
{
  gint width = display->download_width;
  gint height = display->download_height;
  GstVideoFormat video_format = display->download_video_format;
  gpointer data = display->download_data;

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->download_fbo);

  glPushAttrib(GL_VIEWPORT_BIT);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluOrtho2D(0.0, width, 0.0, height);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glViewport(0, 0, width, height);

  switch (video_format)
  {
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
  {
    GstGLShader* shader_download_YUY2_UYVY = NULL;

    switch (video_format)
    {
    case GST_VIDEO_FORMAT_YUY2:
      shader_download_YUY2_UYVY = display->shader_download_YUY2;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      shader_download_YUY2_UYVY = display->shader_download_UYVY;
      break;
    default:
      g_assert_not_reached ();
    }

    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gst_gl_shader_use (shader_download_YUY2_UYVY);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glActiveTextureARB(GL_TEXTURE0_ARB);
    gst_gl_shader_set_uniform_1i (shader_download_YUY2_UYVY, "tex", 0);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);
  }
  break;

  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  {
    glDrawBuffers(3, display->multipleRT);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gst_gl_shader_use (display->shader_download_I420_YV12);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glActiveTextureARB(GL_TEXTURE0_ARB);
    gst_gl_shader_set_uniform_1i (display->shader_download_I420_YV12, "tex", 0);
    gst_gl_shader_set_uniform_1f (display->shader_download_I420_YV12, "w",
				  (gfloat)display->ouput_texture_width);
    gst_gl_shader_set_uniform_1f (display->shader_download_I420_YV12, "h",
				  (gfloat)display->ouput_texture_height);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);
  }
  break;

  case GST_VIDEO_FORMAT_AYUV:
  {
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gst_gl_shader_use (display->shader_download_AYUV);

    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();

    glActiveTextureARB(GL_TEXTURE0_ARB);
    gst_gl_shader_set_uniform_1i (display->shader_download_AYUV, "tex", 0);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);
  }
  break;

  default:
    g_assert_not_reached ();

  }//end switch display->currentVideo_format

  glBegin (GL_QUADS);
  glTexCoord2i (0, 0);
  glVertex2f (-1.0f, -1.0f);
  glTexCoord2i (width, 0);
  glVertex2f (1.0f, -1.0f);
  glTexCoord2i (width, height);
  glVertex2f (1.0f, 1.0f);
  glTexCoord2i (0, height);
  glVertex2f (-1.0f, 1.0f);
  glEnd ();

  glDrawBuffer(GL_NONE);

  //dot not check if GLSL is available
  //because for now download is not available
  //without GLSL
  glUseProgramObjectARB (0);

  glDisable(GL_TEXTURE_RECTANGLE_ARB);

  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glPopAttrib();

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  gst_gl_display_check_framebuffer_status();

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->download_fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);

  switch (video_format)
  {
  case GST_VIDEO_FORMAT_AYUV:
  case GST_VIDEO_FORMAT_xRGB:
    glReadPixels (0, 0, width, height, GL_BGRA,
		  GL_UNSIGNED_INT_8_8_8_8, data);
    break;
  case GST_VIDEO_FORMAT_YUY2:
  case GST_VIDEO_FORMAT_UYVY:
    glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, height, GL_BGRA,
		  GL_UNSIGNED_INT_8_8_8_8_REV, data);
    break;
  case GST_VIDEO_FORMAT_I420:
  case GST_VIDEO_FORMAT_YV12:
  {
    glReadPixels (0, 0, width, height, GL_LUMINANCE,
		  GL_UNSIGNED_BYTE, data);

    glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
    glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
		  GL_LUMINANCE, GL_UNSIGNED_BYTE,
		  (guint8 *) data +
		  gst_video_format_get_component_offset (video_format, 1, width, height));

    glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
    glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
		  GL_LUMINANCE, GL_UNSIGNED_BYTE,
		  (guint8 *) data +
		  gst_video_format_get_component_offset (video_format, 2, width, height));
  }
  break;
  default:
    g_assert_not_reached ();
  }

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
  gst_gl_display_check_framebuffer_status();
}
