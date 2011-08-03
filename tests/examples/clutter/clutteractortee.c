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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <clutter/clutter.h>
#include <clutter/x11/clutter-x11.h>
#include <clutter/glx/clutter-glx.h>
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>

#define ROWS 3
#define COLS 3
#define N_ACTORS ROWS*COLS
#define W 160
#define H 120

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
  static gint xpos = 0;
  static gint ypos = 0;
  actor->texture = g_object_new (CLUTTER_GLX_TYPE_TEXTURE_PIXMAP,
      "window", actor->win, "automatic-updates", TRUE, NULL);
  clutter_container_add_actor (CLUTTER_CONTAINER (actor->stage),
      actor->texture);
  clutter_actor_set_position (actor->texture, xpos, ypos);

  if (xpos > (COLS - 1) * W) {
    xpos = 0;
    ypos += H + 1;
  } else
    xpos += W + 1;
  clutter_actor_show (actor->texture);

  return FALSE;
}

static GstBusSyncReply
create_window (GstBus * bus, GstMessage * message, gpointer data)
{
  GstGLClutterActor **actor = (GstGLClutterActor **) data;
  static gint count = 0;
  static GMutex *mutex = NULL;
  // ignore anything but 'prepare-xwindow-id' element messages
  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;

  if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
    return GST_BUS_PASS;

  if (!mutex)
    mutex = g_mutex_new ();

  g_mutex_lock (mutex);

  if (count < N_ACTORS) {
    g_message ("adding actor %d", count);
    gst_x_overlay_set_window_handle (GST_X_OVERLAY (GST_MESSAGE_SRC (message)),
        actor[count]->win);
    clutter_threads_add_idle ((GSourceFunc) create_actor, actor[count]);
    count++;
  }

  g_mutex_unlock (mutex);

  gst_message_unref (message);
  return GST_BUS_DROP;
}

#if 0
void
apply_fx (GstElement * element, const gchar * fx)
{
  GEnumClass *p_class;

  /* from fxtest ;) */
  /* heeeellppppp!! */
  p_class =
      G_PARAM_SPEC_ENUM (g_object_class_find_property (G_OBJECT_GET_CLASS
          (G_OBJECT (data)), "effect")
      )->enum_class;

  g_print ("setting: %s - %s\n", fx, g_enum_get_value_by_nick (p_class,
          fx)->value_name);
  g_object_set (G_OBJECT (element), "effect", g_enum_get_value_by_nick (p_class,
          fx)->value, NULL);
}
#endif

int
main (int argc, char *argv[])
{
  GstPipeline *pipeline;
  GstBus *bus;

  GstElement *srcbin;
  GstElement *tee;
  GstElement *queue[N_ACTORS], *sink[N_ACTORS];
/*
  GstElement *upload[N_ACTORS];
  GstElement *effect[N_ACTORS];
*/
  ClutterActor *stage;
  GstGLClutterActor *actor[N_ACTORS];
  Display *disp;
  Window stage_win;
  const gchar *desc;
  gint i;
  gint ok = FALSE;

  clutter_init (&argc, &argv);
  gst_init (&argc, &argv);

  disp = clutter_x11_get_default_display ();
  if (!clutter_x11_has_composite_extension ()) {
    g_error ("XComposite extension missing");
  }

  stage = clutter_stage_get_default ();
  clutter_actor_set_size (CLUTTER_ACTOR (stage),
      W * COLS + (COLS - 1), H * ROWS + (ROWS - 1));

  stage_win = clutter_x11_get_stage_window (CLUTTER_STAGE (stage));
  XCompositeRedirectSubwindows (disp, stage_win, CompositeRedirectManual);

  for (i = 0; i < N_ACTORS; i++) {
    actor[i] = g_new0 (GstGLClutterActor, 1);
    actor[i]->stage = stage;
    actor[i]->win = XCreateSimpleWindow (disp, stage_win, 0, 0, W, H, 0, 0, 0);
    XMapRaised (disp, actor[i]->win);
    XSync (disp, FALSE);
  }
/*
  desc = g_strdup_printf ("v4l2src ! "
                          "video/x-raw-yuv, width=640, height=480, framerate=30/1 ! "
                          "videoscale !"
                          "video/x-raw-yuv, width=%d, height=%d ! "
                          "identity", W, H);
*/
  desc = g_strdup_printf ("videotestsrc ! "
      "video/x-raw-rgb, width=%d, height=%d !" "identity", W, H);
  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));

  srcbin = gst_parse_bin_from_description (desc, TRUE, NULL);
  if (!srcbin)
    g_error ("Source bin creation failed");

  tee = gst_element_factory_make ("tee", NULL);

  gst_bin_add_many (GST_BIN (pipeline), srcbin, tee, NULL);

  for (i = 0; i < N_ACTORS; i++) {
    queue[i] = gst_element_factory_make ("queue", NULL);
/*    upload[i] = gst_element_factory_make ("glupload", NULL);
      effect[i] = gst_element_factory_make ("gleffects", NULL); */
    sink[i] = gst_element_factory_make ("glimagesink", NULL);
/*    gst_bin_add_many (GST_BIN (pipeline),
        queue[i], upload[i], effect[i], sink[i], NULL); */
    gst_bin_add_many (GST_BIN (pipeline), queue[i], sink[i], NULL);
  }

  gst_element_link_many (srcbin, tee, NULL);

  for (i = 0; i < N_ACTORS; i++) {
    ok |=
//        gst_element_link_many (tee, queue[i], upload[i], effect[i], sink[i],
        gst_element_link_many (tee, queue[i], sink[i], NULL);
  }

  if (!ok)
    g_error ("Failed to link one or more elements");

/*
  for (i = 0; i < N_ACTORS; i++) {
    g_message ("setting effect %d on %s", i + 1,
        gst_element_get_name (effect[i]));
    g_object_set (G_OBJECT (effect[i]), "effect", i + 1, NULL);
  }
*/

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) create_window, actor);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  clutter_actor_show_all (stage);

  clutter_main ();

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  g_object_unref (pipeline);

  return 0;
}
