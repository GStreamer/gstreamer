/* 
 * GStreamer
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define CLUTTER_VERSION_MIN_REQUIRED CLUTTER_VERSION_1_8

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter/glx/clutter-glx.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#define W 320
#define H 240

struct GstGLClutterActor_
{
  Window win;
  Window root;
  ClutterActor *texture;
  ClutterActor *stage;
};

typedef struct GstGLClutterActor_ GstGLClutterActor;

static gboolean
create_actor (GstGLClutterActor * actor)
{
  //ClutterKnot knot[2];
  //ClutterTimeline *timeline;
  ClutterAnimation *animation = NULL;

  actor->texture = g_object_new (CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
      "window", actor->win, "automatic-updates", TRUE, NULL);
  clutter_container_add_actor (CLUTTER_CONTAINER (actor->stage),
      actor->texture);
  clutter_actor_set_scale (actor->texture, 0.2, 0.2);
  clutter_actor_set_opacity (actor->texture, 0);
  clutter_actor_show (actor->texture);

  //timeline =
  //    clutter_timeline_new (120 /* frames */ , 50 /* frames per second. */ );
  //clutter_timeline_set_loop (timeline, TRUE);
  //clutter_timeline_start (timeline);

  /* Instead of our custom callback, 
   * we could use a standard callback. For instance, CLUTTER_ALPHA_SINE_INC. 
   */
  /*effect_template =
     clutter_effect_template_new (timeline, CLUTTER_ALPHA_SINE_INC); */
  animation =
      clutter_actor_animate (actor->texture, CLUTTER_LINEAR, 2400,
      "x", 100.0, "y", 100.0, "opacity", 0, NULL);

  /* knot[0].x = -10;
     knot[0].y = -10;
     knot[1].x = 160;
     knot[1].y = 120; */

  // Move the actor along the path:
  /* clutter_effect_path (effect_template, actor->texture, knot,
     sizeof (knot) / sizeof (ClutterKnot), NULL, NULL);
     clutter_effect_scale (effect_template, actor->texture, 1.0, 1.0, NULL, NULL);
     clutter_effect_rotate (effect_template, actor->texture,
     CLUTTER_Z_AXIS, 360.0, W / 2.0, H / 2.0, 0.0,
     CLUTTER_ROTATE_CW, NULL, NULL);
     clutter_effect_rotate (effect_template, actor->texture,
     CLUTTER_X_AXIS, 360.0, 0.0, W / 4.0, 0.0, CLUTTER_ROTATE_CW, NULL, NULL); */

  // Also change the actor's opacity while moving it along the path:
  // (You would probably want to use a different ClutterEffectTemplate, 
  // so you could use a different alpha callback for this.)
  //clutter_effect_fade (effect_template, actor->texture, 255, NULL, NULL);

  g_object_unref (animation);
  //g_object_unref (timeline);

  return FALSE;
}

static GstBusSyncReply
create_window (GstBus * bus, GstMessage * message, gpointer data)
{
  GstGLClutterActor *actor = (GstGLClutterActor *) data;
  // ignore anything but 'prepare-window-handle' element messages
  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;

  if (!gst_is_video_overlay_prepare_window_handle_message (message))
    return GST_BUS_PASS;

  g_debug ("CREATING WINDOW");

  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (GST_MESSAGE_SRC
          (message)), actor->win);
  clutter_threads_add_idle ((GSourceFunc) create_actor, actor);

  gst_message_unref (message);
  return GST_BUS_DROP;
}

int
main (int argc, char *argv[])
{
  GstPipeline *pipeline;
  GstBus *bus;
  ClutterActor *stage;
  GstGLClutterActor *actor;
  Display *disp;
  Window stage_win;
  ClutterInitError clutter_err = CLUTTER_INIT_ERROR_UNKNOWN;

  clutter_err = clutter_init (&argc, &argv);
  if (clutter_err != CLUTTER_INIT_SUCCESS)
    g_warning ("Failed to initalize clutter: %d\n", clutter_err);

  gst_init (&argc, &argv);

  disp = clutter_x11_get_default_display ();
  if (!clutter_x11_has_composite_extension ()) {
    g_error ("XComposite extension missing");
  }


  stage = clutter_stage_get_default ();
//  clutter_actor_set_size (CLUTTER_ACTOR (stage), W*3+2, H);

  stage_win = clutter_x11_get_stage_window (CLUTTER_STAGE (stage));

  actor = g_new0 (GstGLClutterActor, 1);
  actor->stage = stage;
  actor->win = XCreateSimpleWindow (disp, stage_win, 0, 0, W, H, 0, 0, 0);
  XCompositeRedirectWindow (disp, actor->win, CompositeRedirectManual);
  XMapRaised (disp, actor->win);
  XSync (disp, FALSE);

  pipeline =
      GST_PIPELINE (gst_parse_launch
      ("videotestsrc ! video/x-raw, width=320, height=240, framerate=(fraction)30/1 ! "
          "gleffects effect=twirl ! glimagesink", NULL));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) create_window, actor,
      NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  clutter_actor_show_all (stage);

  clutter_main ();

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}
