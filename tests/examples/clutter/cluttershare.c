/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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

#include <GL/glew.h>
#ifdef WIN32
#include <GL/wglew.h>
#else
#include <GL/glxew.h>
#endif

#include <GL/gl.h>
#include <clutter/clutter.h>
#ifndef WIN32
#include <clutter/x11/clutter-x11.h>
#endif
#include <gst/gst.h>

/* This example shows how to use textures that come from a
 * gst-plugins-gl pipeline, into the clutter framework
 */

/* hack */
typedef struct _GstGLBuffer GstGLBuffer;
struct _GstGLBuffer
{
  GstBuffer buffer;

  GObject *obj;

  gint width;
  gint height;
  GLuint texture;
};

/* clutter scene */
ClutterActor *
setup_stage (ClutterStage * stage)
{
  ClutterTimeline *timeline = NULL;
  ClutterEffectTemplate *effect_template = NULL;
  ClutterActor *texture_actor = NULL;
  ClutterColor rect_color = { 125, 50, 200, 255 };
  ClutterActor *actorRect = NULL;

  /* timeline */

  timeline = clutter_timeline_new (120, 50);
  clutter_timeline_set_loop (timeline, TRUE);
  clutter_timeline_start (timeline);

  /* effect template */

  effect_template =
      clutter_effect_template_new (timeline, CLUTTER_ALPHA_SINE_INC);

  /* texture actor */

  texture_actor = clutter_texture_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), texture_actor);
  clutter_actor_set_position (texture_actor, 300, 170);
  clutter_actor_set_scale (texture_actor, 0.8, 0.8);
  clutter_effect_rotate (effect_template, texture_actor,
      CLUTTER_Z_AXIS, 180.0, 50, 50, 0, CLUTTER_ROTATE_CW, NULL, NULL);
  clutter_actor_show (texture_actor);
  g_object_set_data (G_OBJECT (texture_actor), "stage", stage);

  g_object_unref (effect_template);
  g_object_unref (timeline);

  /* rectangle actor */

  actorRect = clutter_rectangle_new_with_color (&rect_color);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), actorRect);
  clutter_actor_set_size (actorRect, 50, 50);
  clutter_actor_set_position (actorRect, 300, 300);
  clutter_effect_rotate (effect_template, actorRect,
      CLUTTER_Z_AXIS, 180.0, 25, 25, 0, CLUTTER_ROTATE_CW, NULL, NULL);
  clutter_actor_show (actorRect);

  return texture_actor;
}

/* put a gst gl buffer in the texture actor */
gboolean
update_texture_actor (gpointer data)
{
  ClutterTexture *texture_actor = (ClutterTexture *) data;
  GQueue *queue_input_buf = g_object_get_data (G_OBJECT (texture_actor), "queue_input_buf");
  GQueue *queue_output_buf = g_object_get_data (G_OBJECT (texture_actor), "queue_output_buf");
  GstGLBuffer *gst_gl_buf = g_queue_pop_head (queue_input_buf);
  ClutterActor *stage = g_object_get_data (G_OBJECT (texture_actor), "stage");
  CoglHandle cogl_texture = 0;

  /* Create a cogl texture from the gst gl texture */
  glEnable (GL_TEXTURE_2D);
  glBindTexture (GL_TEXTURE_2D, gst_gl_buf->texture);
  cogl_texture = cogl_texture_new_from_foreign (gst_gl_buf->texture,
      GL_TEXTURE_2D, gst_gl_buf->width, gst_gl_buf->height, 0, 0,
      COGL_PIXEL_FORMAT_RGBA_8888);
  glBindTexture (GL_TEXTURE_2D, 0);

  /* Previous cogl texture is replaced and so its ref counter discreases to 0.
   * According to the source code, glDeleteTexture is not called when the previous
   * ref counter of the previous cogl texture is reaching 0 because is_foreign is TRUE */
  clutter_texture_set_cogl_texture (CLUTTER_TEXTURE (texture_actor), cogl_texture);
  cogl_texture_unref (cogl_texture);

  /* we can now show the clutter scene if not yet visible */
  if (!CLUTTER_ACTOR_IS_VISIBLE (stage))
    clutter_actor_show_all (stage);

  /* push buffer so it can be unref later */
  g_queue_push_tail (queue_output_buf, gst_gl_buf);

  return FALSE;
}


/* fakesink handoff callback */
void
on_gst_buffer (GstElement * element, GstBuffer * buf, GstPad * pad,
    ClutterActor * texture_actor)
{
	GQueue *queue_input_buf = NULL;
	GQueue *queue_output_buf = NULL;

	/* hold clutter lock */
	clutter_threads_enter ();

  /* ref then push buffer to use it in clutter */
  gst_buffer_ref (buf);
	queue_input_buf = g_object_get_data (G_OBJECT (texture_actor), "queue_input_buf");
  g_queue_push_tail (queue_input_buf, buf);
  if (g_queue_get_length (queue_input_buf) > 2)
		clutter_threads_add_idle (update_texture_actor, texture_actor);

  /* pop then unref buffer we have finished to use in clutter */
  queue_output_buf = g_object_get_data (G_OBJECT (texture_actor), "queue_output_buf");
  if (g_queue_get_length (queue_output_buf) > 2) {
		GstGLBuffer *gst_gl_buf_old = g_queue_pop_head (queue_output_buf);
		gst_buffer_unref (gst_gl_buf_old);
	}

  /* release clutter lock */
	clutter_threads_leave ();
}

int
main (int argc, char *argv[])
{
  GLenum err = 0;
#ifdef WIN32
  HGLRC clutter_gl_context = 0;
  HDC clutter_dc = 0;
#else
  Display *clutter_display = NULL;
  Window clutter_win = 0;
  GLXContext clutter_gl_context = NULL;
#endif
  GstPipeline *pipeline = NULL;
  GstElement *glupload = NULL;
  GstState state = 0;
  ClutterActor *stage = NULL;
  ClutterActor *clutter_texture = NULL;
  GQueue* queue_input_buf = NULL;
  GQueue* queue_output_buf = NULL;
  GstElement *fakesink = NULL;

  /* init gstreamer then clutter */

	gst_init (&argc, &argv);
	clutter_threads_init ();
  clutter_init (&argc, &argv);
  clutter_threads_enter ();

  /* init glew */

  err = glewInit ();
  if (err != GLEW_OK)
    g_debug ("failed to init GLEW: %s", glewGetErrorString (err));

  /* avoid to dispatch unecesary events */

  clutter_ungrab_keyboard ();
  clutter_ungrab_pointer ();

  /* retrieve and turn off clutter opengl context */

  stage = clutter_stage_get_default ();

  /* retrieve and turn off clutter opengl context */

#ifdef WIN32
  clutter_gl_context = wglGetCurrentContext ();
  clutter_dc = wglGetCurrentDC ();
  wglMakeCurrent (0, 0);
#else
  clutter_display = clutter_x11_get_default_display ();
  clutter_win = clutter_x11_get_stage_window (CLUTTER_STAGE (stage));
  clutter_gl_context = glXGetCurrentContext ();
  glXMakeCurrent (clutter_display, None, 0);
#endif

  /* setup gstreamer pipeline */

  pipeline =
      GST_PIPELINE (gst_parse_launch
      ("videotestsrc ! video/x-raw-rgb, bpp=32, depth=32, width=320, height=240, framerate=(fraction)30/1 ! "
          "glupload ! fakesink sync=1", NULL));

  /* clutter_gl_context is an external OpenGL context with which gst-plugins-gl want to share textures */

  glupload = gst_bin_get_by_name (GST_BIN (pipeline), "glupload0");
  g_object_set (G_OBJECT (glupload), "external-opengl-context",
      clutter_gl_context, NULL);
  g_object_unref (glupload);

  /* NULL to PAUSED state pipeline to make sure the gst opengl context is created and
   * shared with the clutter one */

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
  state = GST_STATE_PAUSED;
  if (gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL,
          GST_CLOCK_TIME_NONE) != GST_STATE_CHANGE_SUCCESS) {
    g_debug ("failed to pause pipeline\n");
    return -1;
  }

  /* turn on back clutter opengl context */

#ifdef WIN32
  wglMakeCurrent (clutter_dc, clutter_gl_context);
#else
  glXMakeCurrent (clutter_display, clutter_win, clutter_gl_context);
#endif

  /* clutter stage */

  clutter_actor_set_size (stage, 640, 480);
  clutter_actor_set_position (stage, 0, 0);
  clutter_stage_set_title (CLUTTER_STAGE (stage), "clutter and gst-plugins-gl");
  clutter_texture = setup_stage (CLUTTER_STAGE (stage));

  /* append a gst-gl texture to this queue when you do not need it no more */

  queue_input_buf = g_queue_new ();
  queue_output_buf = g_queue_new ();
	g_object_set_data (G_OBJECT (clutter_texture), "queue_input_buf", queue_input_buf);
	g_object_set_data (G_OBJECT (clutter_texture), "queue_output_buf", queue_output_buf);

  /* set a callback to retrieve the gst gl textures */

  fakesink = gst_bin_get_by_name (GST_BIN (pipeline), "fakesink0");
  g_object_set (G_OBJECT (fakesink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (on_gst_buffer),
      clutter_texture);
  g_object_unref (fakesink);

  /* play gst */

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  /* main loop */

  clutter_main ();

  /* before to deinitialize the gst-gl-opengl context,
   * no shared context (here the clutter one) must be current
   */
#ifdef WIN32
  wglMakeCurrent (0, 0);
#else
  glXMakeCurrent (clutter_display, None, 0);
#endif

  clutter_threads_leave ();

  /* deinit */

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  g_object_unref (pipeline);

  return 0;
}
