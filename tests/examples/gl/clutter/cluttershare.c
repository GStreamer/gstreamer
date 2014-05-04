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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <GL/gl.h>

#define CLUTTER_VERSION_MIN_REQUIRED CLUTTER_VERSION_1_8
#define CLUTTER_VERSION_MAX_ALLOWED CLUTTER_VERSION_1_10
#define COGL_VERSION_MIN_REQUIRED COGL_VERSION_ENCODE (1, 16, 0)
#define COGL_VERSION_MAX_ALLOWED COGL_VERSION_ENCODE (1, 18, 0)
#include <clutter/clutter.h>
#ifndef WIN32
#include <clutter/x11/clutter-x11.h>
#include <GL/glx.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/gl/gstglmemory.h>

/* This example shows how to use textures that come from a
 * gst-plugins-gl pipeline, into the clutter framework
 * It requires at least clutter 0.8.6
 */

/* rotation */
static void
on_new_frame (ClutterTimeline * timeline, gint msecs, gpointer data)
{
  ClutterActor *rect_actor = CLUTTER_ACTOR (data);
  ClutterActor *texture_actor =
      g_object_get_data (G_OBJECT (timeline), "texture_actor");

  clutter_actor_set_rotation (rect_actor, CLUTTER_Z_AXIS,
      60.0 * (gdouble) msecs / 1000.0, clutter_actor_get_width (rect_actor) / 2,
      clutter_actor_get_height (rect_actor) / 2, 0);

  clutter_actor_set_rotation (texture_actor, CLUTTER_Z_AXIS,
      60.0 * (gdouble) msecs / 1000.0,
      clutter_actor_get_width (texture_actor) / 6,
      clutter_actor_get_height (texture_actor) / 6, 0);
}


/* clutter scene */
static ClutterActor *
setup_stage (ClutterStage * stage)
{
  ClutterTimeline *timeline = NULL;
  ClutterActor *texture_actor = NULL;
  ClutterColor rect_color = { 125, 50, 200, 255 };
  ClutterActor *rect_actor = NULL;

  /* texture actor */

  texture_actor = clutter_texture_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), texture_actor);
  clutter_actor_set_position (texture_actor, 300, 170);
  clutter_actor_set_scale (texture_actor, 0.6, 0.6);
  clutter_actor_show (texture_actor);
  g_object_set_data (G_OBJECT (texture_actor), "stage", stage);

  /* rectangle actor */

  rect_actor = clutter_rectangle_new_with_color (&rect_color);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), rect_actor);
  clutter_actor_set_size (rect_actor, 50, 50);
  clutter_actor_set_position (rect_actor, 300, 300);
  clutter_actor_show (rect_actor);

  /* timeline */

  timeline = clutter_timeline_new (6000);
  g_object_set_data (G_OBJECT (timeline), "texture_actor", texture_actor);
  clutter_timeline_set_loop (timeline, TRUE);
  clutter_timeline_start (timeline);
  g_signal_connect (timeline, "new-frame", G_CALLBACK (on_new_frame),
      rect_actor);

  return texture_actor;
}

/* put a gst gl buffer in the texture actor */
static gboolean
update_texture_actor (gpointer data)
{
  ClutterTexture *texture_actor = (ClutterTexture *) data;
  GAsyncQueue *queue_input_buf =
      g_object_get_data (G_OBJECT (texture_actor), "queue_input_buf");
  GAsyncQueue *queue_output_buf =
      g_object_get_data (G_OBJECT (texture_actor), "queue_output_buf");
  GstBuffer *inbuf = g_async_queue_pop (queue_input_buf);
  ClutterActor *stage = g_object_get_data (G_OBJECT (texture_actor), "stage");
  CoglHandle cogl_texture = 0;
  GstVideoMeta *v_meta;
  GstVideoInfo info;
  GstVideoFrame frame;
  guint tex_id;

  v_meta = gst_buffer_get_video_meta (inbuf);
  if (!v_meta) {
    g_warning ("Required Meta was not found on buffers");
    return FALSE;
  }

  gst_video_info_set_format (&info, v_meta->format, v_meta->width,
      v_meta->height);

  if (!gst_video_frame_map (&frame, &info, inbuf, GST_MAP_READ | GST_MAP_GL)) {
    g_warning ("Failed to map video frame");
    return FALSE;
  }

  if (!gst_is_gl_memory (frame.map[0].memory)) {
    g_warning ("Input buffer does not have GLMemory");
    gst_video_frame_unmap (&frame);
    return FALSE;
  }

  tex_id = *(guint *) frame.data[0];

  /* Create a cogl texture from the gst gl texture */
  glEnable (GL_TEXTURE_2D);
  glBindTexture (GL_TEXTURE_2D, tex_id);
  if (glGetError () != GL_NO_ERROR)
    g_debug ("failed to bind texture that comes from gst-gl\n");
  cogl_texture = cogl_texture_new_from_foreign (tex_id,
      GL_TEXTURE_2D, v_meta->width, v_meta->height, 0, 0,
      COGL_PIXEL_FORMAT_RGBA_8888);
  glBindTexture (GL_TEXTURE_2D, 0);

  gst_video_frame_unmap (&frame);

  /* Previous cogl texture is replaced and so its ref counter discreases to 0.
   * According to the source code, glDeleteTexture is not called when the previous
   * ref counter of the previous cogl texture is reaching 0 because is_foreign is TRUE */
  clutter_texture_set_cogl_texture (CLUTTER_TEXTURE (texture_actor),
      cogl_texture);
  cogl_handle_unref (cogl_texture);

  /* we can now show the clutter scene if not yet visible */
  if (!CLUTTER_ACTOR_IS_VISIBLE (stage))
    clutter_actor_show_all (stage);

  /* push buffer so it can be unref later */
  g_async_queue_push (queue_output_buf, inbuf);

  return FALSE;
}


/* fakesink handoff callback */
static void
on_gst_buffer (GstElement * element, GstBuffer * buf, GstPad * pad,
    ClutterActor * texture_actor)
{
  GAsyncQueue *queue_input_buf = NULL;
  GAsyncQueue *queue_output_buf = NULL;

  /* ref then push buffer to use it in clutter */
  gst_buffer_ref (buf);
  queue_input_buf =
      g_object_get_data (G_OBJECT (texture_actor), "queue_input_buf");
  g_async_queue_push (queue_input_buf, buf);
  if (g_async_queue_length (queue_input_buf) > 2)
    clutter_threads_add_idle_full (G_PRIORITY_HIGH, update_texture_actor,
        texture_actor, NULL);

  /* pop then unref buffer we have finished to use in clutter */
  queue_output_buf =
      g_object_get_data (G_OBJECT (texture_actor), "queue_output_buf");
  if (g_async_queue_length (queue_output_buf) > 2) {
    GstBuffer *buf_old = g_async_queue_pop (queue_output_buf);
    gst_buffer_unref (buf_old);
  }
}

/* gst bus signal watch callback */
static void
end_stream_cb (GstBus * bus, GstMessage * msg, gpointer data)
{
  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End-of-stream\n");
      g_print
          ("For more information, try to run: GST_DEBUG=gldisplay:2 ./cluttershare\n");
      break;

    case GST_MESSAGE_ERROR:
    {
      gchar *debug = NULL;
      GError *err = NULL;

      gst_message_parse_error (msg, &err, &debug);

      g_print ("Error: %s\n", err->message);
      g_error_free (err);

      if (debug) {
        g_print ("Debug deails: %s\n", debug);
        g_free (debug);
      }

      break;
    }

    default:
      break;
  }

  clutter_main_quit ();
}

int
main (int argc, char *argv[])
{
  ClutterInitError clutter_err = CLUTTER_INIT_ERROR_UNKNOWN;
#ifdef WIN32
  HGLRC clutter_gl_context = 0;
  HDC clutter_dc = 0;
#else
  Display *clutter_display = NULL;
  Window clutter_win = 0;
  GLXContext clutter_gl_context = NULL;
#endif
  GstPipeline *pipeline = NULL;
  GstBus *bus = NULL;
  GstElement *glfilter = NULL;
  GstState state = 0;
  ClutterActor *stage = NULL;
  ClutterActor *clutter_texture = NULL;
  GAsyncQueue *queue_input_buf = NULL;
  GAsyncQueue *queue_output_buf = NULL;
  GstElement *fakesink = NULL;

  /* init gstreamer then clutter */

  gst_init (&argc, &argv);
  clutter_threads_init ();
  clutter_err = clutter_init (&argc, &argv);
  if (clutter_err != CLUTTER_INIT_SUCCESS)
    g_warning ("Failed to initalize clutter: %d\n", clutter_err);
  clutter_threads_enter ();
  g_print ("clutter version: %s\n", CLUTTER_VERSION_S);
  clutter_set_default_frame_rate (2);

  /* avoid to dispatch unecesary events */
  clutter_ungrab_keyboard ();
  clutter_ungrab_pointer ();

  /* retrieve and turn off clutter opengl context */
  stage = clutter_stage_get_default ();

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
      ("videotestsrc ! video/x-raw, width=320, height=240, framerate=(fraction)30/1 ! "
          "gleffects effect=5 ! glfiltercube ! fakesink sync=1", NULL));

  /* setup bus */

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::error", G_CALLBACK (end_stream_cb), NULL);
  g_signal_connect (bus, "message::warning", G_CALLBACK (end_stream_cb), NULL);
  g_signal_connect (bus, "message::eos", G_CALLBACK (end_stream_cb), NULL);
  gst_object_unref (bus);

  /* clutter_gl_context is an external OpenGL context with which gst-plugins-gl want to share textures */
  glfilter = gst_bin_get_by_name (GST_BIN (pipeline), "glfiltercube0");
  g_object_set (G_OBJECT (glfilter), "external-opengl-context",
      clutter_gl_context, NULL);
  gst_object_unref (glfilter);

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
  queue_input_buf = g_async_queue_new ();
  queue_output_buf = g_async_queue_new ();
  g_object_set_data (G_OBJECT (clutter_texture), "queue_input_buf",
      queue_input_buf);
  g_object_set_data (G_OBJECT (clutter_texture), "queue_output_buf",
      queue_output_buf);

  /* set a callback to retrieve the gst gl textures */
  fakesink = gst_bin_get_by_name (GST_BIN (pipeline), "fakesink0");
  g_object_set (G_OBJECT (fakesink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (on_gst_buffer),
      clutter_texture);
  gst_object_unref (fakesink);

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

  /* stop and clean up the pipeline */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);

  /* make sure there is no pending gst gl buffer in the communication queues
   * between clutter and gst-gl
   */
  while (g_async_queue_length (queue_input_buf) > 0) {
    GstBuffer *buf = g_async_queue_pop (queue_input_buf);
    gst_buffer_unref (buf);
  }

  while (g_async_queue_length (queue_output_buf) > 0) {
    GstBuffer *buf = g_async_queue_pop (queue_output_buf);
    gst_buffer_unref (buf);
  }

  g_print ("END\n");

  return 0;
}
