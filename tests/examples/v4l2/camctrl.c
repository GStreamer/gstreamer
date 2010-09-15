/* GStreamer
 * Copyright (C) 2010 Stefan Kost <stefan.kost@nokia.com>
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
/* demo for using gstcontroler with camera capture for e.g. bracketing
 *
 * gcc `pkg-config --cflags --libs gstreamer-0.10 gstreamer-controller-0.10` camctrl.c -o camctrl
 *
 * TODO:
 * - handle stream status and switch capture thread to SCHED_RR/FIFO
 * - the queue-size controls the controler offset
 *   - right now we work with 1 queued picture and thus active settings for next
 *     frame
 * - we want some feedback about how precisely a program can be realized
 *   - we might want to adjust the framerate to handle hardware limmits
 * - we e.g. can't change resolution per frame right now
 */

#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstinterpolationcontrolsource.h>

static void
event_loop (GstElement * bin)
{
  GstBus *bus;
  GstMessage *message = NULL;

  bus = gst_element_get_bus (GST_ELEMENT (bin));

  while (TRUE) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (message->type) {
      case GST_MESSAGE_EOS:
        gst_message_unref (message);
        return;
      case GST_MESSAGE_WARNING:
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        gst_message_unref (message);
        g_error_free (gerror);
        g_free (debug);
        return;
      }
      default:
        gst_message_unref (message);
        break;
    }
  }
}

static void
set_program (GstController * ctrl, GstStructure * prog)
{
  const GstStructure *s;
  GstInterpolationControlSource *cs;
  GstClockTime ts, dur;
  GValue val = { 0, };
  gint v;
  const GValue *frame;
  GHashTable *css;
  gint i, j;
  const gchar *name;

  css = g_hash_table_new (g_str_hash, g_str_equal);

  g_value_init (&val, G_TYPE_INT);

  ts = 0;
  dur = gst_util_uint64_scale_int (GST_SECOND, 1, 15);

  /* loop over each image in prog */
  for (i = 0; i < gst_structure_n_fields (prog); i++) {
    GST_DEBUG ("ctrl on %" GST_TIME_FORMAT, GST_TIME_ARGS (ts));

    frame =
        gst_structure_get_value (prog, gst_structure_nth_field_name (prog, i));
    s = gst_value_get_structure (frame);
    for (j = 0; j < gst_structure_n_fields (s); j++) {
      name = gst_structure_nth_field_name (s, j);
      cs = g_hash_table_lookup (css, name);
      if (!cs) {
        cs = gst_interpolation_control_source_new ();
        gst_controller_set_control_source (ctrl, name, GST_CONTROL_SOURCE (cs));
        gst_interpolation_control_source_set_interpolation_mode (cs,
            GST_INTERPOLATE_NONE);
        g_hash_table_insert (css, (gpointer) name, cs);
        g_object_unref (cs);
      }
      gst_structure_get_int (s, name, &v);
      g_value_set_int (&val, v);
      gst_interpolation_control_source_set (cs, ts, &val);
      GST_DEBUG ("  %s = %d", name, v);
    }
    ts += dur;
  }

  g_value_unset (&val);

  g_hash_table_unref (css);
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *bin;
  GstElement *src, *fmt, *enc, *sink;
  GstCaps *caps;
  GstController *ctrl;
  GstStructure *prog;

  /* init gstreamer */
  gst_init (&argc, &argv);
  gst_controller_init (&argc, &argv);

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("camera");

  /* create elements */
  if (!(sink = gst_element_factory_make ("multifilesink", NULL))) {
    GST_WARNING ("Can't create element \"multifilesink\"");
    return -1;
  }
  g_object_set (sink, "location", "image%02d.jpg", NULL);

  if (!(enc = gst_element_factory_make ("jpegenc", NULL))) {
    GST_WARNING ("Can't create element \"jpegenc\"");
    return -1;
  }

  if (!(fmt = gst_element_factory_make ("capsfilter", NULL))) {
    GST_WARNING ("Can't create element \"capsfilter\"");
    return -1;
  }
  caps =
      gst_caps_from_string
      ("video/x-raw-yuv, width=640, height=480, framerate=(fraction)15/1");
  g_object_set (fmt, "caps", caps, NULL);

  if (!(src = gst_element_factory_make ("v4l2src", NULL))) {
    GST_WARNING ("Can't create element \"v4l2src\"");
    return -1;
  }
  g_object_set (src, "queue-size", 1, NULL);

  /* add objects to the main bin */
  gst_bin_add_many (GST_BIN (bin), src, fmt, enc, sink, NULL);

  /* link elements */
  if (!gst_element_link_many (src, fmt, enc, sink, NULL)) {
    GST_WARNING ("Can't link elements");
    return -1;
  }

  /* get the controller */
  if (!(ctrl = gst_controller_new (G_OBJECT (src), "brightness", "contrast",
              "saturation", NULL))) {
    GST_WARNING ("can't control source element");
    return -1;
  }

  /* programm a pattern of events */
#if 0
  prog = gst_structure_from_string ("program"
      ", image00=(structure)\"image\\,contrast\\=0\\;\""
      ", image01=(structure)\"image\\,contrast\\=79\\;\""
      ", image02=(structure)\"image\\,contrast\\=255\\;\""
      ", image03=(structure)\"image\\,contrast\\=15\\;\";", NULL);
#endif
#if 1
  prog = gst_structure_from_string ("program"
      ", image00=(structure)\"image\\,brightness\\=255\\,contrast\\=0\\;\""
      ", image01=(structure)\"image\\,brightness\\=127\\,contrast\\=79\\;\""
      ", image02=(structure)\"image\\,brightness\\=64\\,contrast\\=255\\;\""
      ", image03=(structure)\"image\\,brightness\\=0\\,contrast\\=15\\;\";",
      NULL);
#endif
  set_program (ctrl, prog);
  g_object_set (src, "num-buffers", gst_structure_n_fields (prog), NULL);

  /* prepare playback */
  gst_element_set_state (bin, GST_STATE_PAUSED);

  /* play and wait */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  /* mainloop and wait for eos */
  event_loop (bin);

  /* stop and cleanup */
  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (bin));
  return 0;
}
