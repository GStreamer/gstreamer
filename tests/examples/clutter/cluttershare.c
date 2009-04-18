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
#include <gst/gst.h>

/* This example shows how to use textures that come from a
 * gst-plugins-gl pipeline, into the clutter framework
 */

/* hack */
typedef struct _GstGLBuffer GstGLBuffer;
struct _GstGLBuffer {
    GstBuffer buffer;

    GObject *obj;

    gint width;
    gint height;
    GLuint texture;
};

/* clutter scene */
ClutterActor*
setup_stage (ClutterStage * stage)
{
  /* timeline */

  ClutterTimeline *timeline = clutter_timeline_new (120, 50);
  clutter_timeline_set_loop (timeline, TRUE);
  clutter_timeline_start (timeline);

  /* effect template */

  ClutterEffectTemplate *effect_template = clutter_effect_template_new (timeline, CLUTTER_ALPHA_SINE_INC);

  /* texture actor */

	ClutterActor *texture_actor = clutter_texture_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), texture_actor);
  clutter_actor_set_position (texture_actor, 300, 170);
  clutter_actor_set_scale (texture_actor, 0.8, 0.8);
  clutter_effect_rotate (effect_template, texture_actor,
                         CLUTTER_Z_AXIS, 180.0,
                         50, 50, 0,
                         CLUTTER_ROTATE_CW,
                         NULL, NULL);
  clutter_actor_show (texture_actor);
  g_object_set_data (G_OBJECT (texture_actor), "stage", stage);

  g_object_unref (effect_template);
  g_object_unref (timeline);

  /* rectangle actor */

	ClutterColor rect_color = { 125, 50, 200, 255 };
	ClutterActor* actorRect = clutter_rectangle_new_with_color (&rect_color);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), actorRect);
  clutter_actor_set_size (actorRect, 50, 50);
  clutter_actor_set_position (actorRect, 300, 300);
  clutter_effect_rotate (effect_template, actorRect,
                         CLUTTER_Z_AXIS, 180.0,
                         25, 25, 0,
                         CLUTTER_ROTATE_CW,
                         NULL, NULL);
  clutter_actor_show (actorRect);

  return texture_actor;
}

/* put a gst gl buffer in the texture actor */
gboolean
update_texture_actor (gpointer data)
{
	GstGLBuffer *gst_gl_buf = (GstGLBuffer *) data;

	/* Create a cogl texture from the gst gl texture */
	glEnable (GL_TEXTURE_2D);
	glBindTexture (GL_TEXTURE_2D, gst_gl_buf->texture);
	CoglHandle cogl_texture = cogl_texture_new_from_foreign (gst_gl_buf->texture,
		GL_TEXTURE_2D, gst_gl_buf->width, gst_gl_buf->height, 0, 0, COGL_PIXEL_FORMAT_RGBA_8888);
	cogl_texture_set_filters (cogl_texture, GL_LINEAR, GL_LINEAR);
	glDisable (GL_TEXTURE_2D);

  /* Previous cogl texture is replaced and so its ref counter discreases to 0.
   * According to the source code, glDeleteTexture is not called when the previous
   * ref counter of the previous cogl texture is reaching 0 because is_foreign is TRUE */
  ClutterTexture *texture_actor = g_type_get_qdata (G_TYPE_FROM_INSTANCE (gst_gl_buf), g_quark_from_string ("texture_actor"));
	clutter_texture_set_cogl_texture (CLUTTER_TEXTURE (texture_actor), cogl_texture);
	cogl_texture_unref (cogl_texture);

	/* Keep a ref on the current gst_gl_buffer associated to the texture_actor.
	 * The old gst_gl_buffer is unref */
	g_object_set_data_full (G_OBJECT (texture_actor), "gst_gl_buffer",
		gst_gl_buf, (GDestroyNotify) gst_mini_object_unref);

	/* we can now show the clutter scene if not yet visible */
	ClutterActor *stage = g_object_get_data (G_OBJECT (texture_actor), "stage");
	if (!CLUTTER_ACTOR_IS_VISIBLE (stage))
		clutter_actor_show_all (stage);

	return FALSE;
}

/* fakesink handoff callback */
void
on_gst_buffer (GstElement* element, GstBuffer* buf, GstPad* pad, ClutterActor* texture_actor)
{
	/* increase ref because our pipeline and clutter scene have not a same framerate */
	gst_buffer_ref (buf);

	/* Just to avoid a global variable of texture_actor
	 * Texture_actor is not null because callback connection is set after the
	 * texture_actor was being setted up */
	g_assert (texture_actor);
	g_type_set_qdata (G_TYPE_FROM_INSTANCE (buf), g_quark_from_string ("texture_actor"), texture_actor);

	/* Here we are in the pipeline thread. It means that this thread may be
	 * not the same as the clutter thread
	 * make sure that the texture actor is updated in the clutter thread */
	clutter_threads_add_idle (update_texture_actor, buf);
}

int
main (int argc, char *argv[])
{

  /* init clutter then gstreamer */

  clutter_init (&argc, &argv);
  gst_init (&argc, &argv);

  /* init glew */

  GLenum err = glewInit ();
  if (err != GLEW_OK)
    g_debug ("failed to init GLEW: %s", glewGetErrorString (err));

	/* retrieve and turn off clutter opengl context */

#ifdef WIN32
	HGLRC clutter_gl_context = wglGetCurrentContext ();
	HDC clutter_dc = wglGetCurrentDC ();
	wglMakeCurrent (0, 0);
#else
	Display *clutter_display = clutter_x11_get_default_display ();
	Window clutter_win = clutter_x11_get_stage_window (CLUTTER_STAGE (stage));
	GLXContext clutter_gl_context = glXGetCurrentContext ();
	glXMakeCurrent (clutter_display, None, 0);
#endif

	/* setup gstreamer pipeline */

	GstPipeline *pipeline =
      GST_PIPELINE (gst_parse_launch
      ("videotestsrc ! video/x-raw-rgb, bpp=32, depth=32, width=320, height=240, framerate=(fraction)30/1 ! "
				  "glupload ! fakesink sync=1", NULL));

	/* clutter_gl_context is an external OpenGL context with which gst-plugins-gl want to share textures */
	GstElement *glupload = gst_bin_get_by_name (GST_BIN (pipeline), "glupload0");
	g_object_set (G_OBJECT (glupload), "external-opengl-context", (guint64) GPOINTER_TO_UINT (clutter_gl_context), NULL);
	g_object_unref (glupload);

  /* play pipeline */

	gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
	GstState state = GST_STATE_PLAYING;
	if (gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL, GST_CLOCK_TIME_NONE) != GST_STATE_CHANGE_SUCCESS)
	{
		g_debug ("failed to play pipeline\n");
		return -1;
	}

	/* turn on back clutter opengl context */

#ifdef WIN32
	wglMakeCurrent (clutter_dc, clutter_gl_context);
#else
	glXMakeCurrent (clutter_display, clutter_win, clutter_gl_context);
#endif

	/* clutter stage */

	ClutterActor* stage = clutter_stage_get_default ();
	clutter_actor_set_size (stage, 640, 480);
	clutter_actor_set_position  (stage, 0, 0);
	clutter_stage_set_title (CLUTTER_STAGE (stage), "clutter and gst-plugins-gl");
	ClutterActor *clutter_texture = setup_stage (CLUTTER_STAGE (stage));

	/* set a callback to retrieve the gst gl textures */

	GstElement *fakesink = gst_bin_get_by_name (GST_BIN (pipeline), "fakesink0");
	g_object_set (G_OBJECT (fakesink), "signal-handoffs", TRUE, NULL);
	g_signal_connect (fakesink, "handoff", G_CALLBACK (on_gst_buffer), clutter_texture);
	g_object_unref (fakesink);

	/* main loop */

  clutter_main ();

  /* deinit */

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  g_object_unref (pipeline);

  return 0;
}
