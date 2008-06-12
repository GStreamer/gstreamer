/* GStreamer OSS4 audio tests
 * Copyright (C) 2007-2008 Tim-Philipp Müller <tim centricular net>
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
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <gst/gst.h>
#include <gst/interfaces/propertyprobe.h>
#include <gst/interfaces/mixer.h>

static gboolean opt_show_mixer_messages = FALSE;

#define WAIT_TIME  60.0         /* in seconds */

static void
show_mixer_messages (GstElement * element)
{
  GstMessage *msg;
  GstBus *bus;
  GTimer *t;

  t = g_timer_new ();

  bus = gst_bus_new ();
  gst_element_set_bus (element, bus);

  g_print ("\nShowing mixer messages for %u seconds ...\n", (guint) WAIT_TIME);

  while (g_timer_elapsed (t, NULL) < WAIT_TIME) {
    gdouble remaining = WAIT_TIME - g_timer_elapsed (t, NULL);
    gint64 maxwait =
        GST_SECOND * gst_util_gdouble_to_guint64 (MAX (0.0, remaining));
    gchar *s = NULL;

    msg = gst_bus_timed_pop (bus, maxwait);
    if (!msg)
      break;

    if (msg->structure)
      s = gst_structure_to_string (msg->structure);
    g_print ("%s message: %s\n", GST_MESSAGE_TYPE_NAME (msg), s);
    gst_message_unref (msg);
    g_free (s);
  }

  gst_element_set_bus (element, NULL);
  gst_object_unref (bus);
  g_timer_destroy (t);
}

static void
probe_mixer_tracks (GstElement * element)
{
  const GList *tracks, *t;
  GstMixer *mixer;
  guint count;

  if (!GST_IS_MIXER (element))
    return;

  mixer = GST_MIXER (element);
  tracks = gst_mixer_list_tracks (mixer);
  count = g_list_length ((GList *) tracks);
  g_print ("  %d mixer tracks%c\n", count, (count == 0) ? '.' : ':');

  for (t = tracks; t != NULL; t = t->next) {
    GstMixerTrack *track;
    gchar *label = NULL;
    guint flags = 0;

    track = GST_MIXER_TRACK (t->data);
    g_object_get (track, "label", &label, "flags", &flags, NULL);

    if (GST_IS_MIXER_OPTIONS (track)) {
      GString *s;
      GList *vals, *v;

      vals = gst_mixer_options_get_values (GST_MIXER_OPTIONS (track));
      s = g_string_new ("options: ");
      for (v = vals; v != NULL; v = v->next) {
        if (v->prev != NULL)
          g_string_append (s, ", ");
        g_string_append (s, (const gchar *) v->data);
      }

      g_print ("    [%s] flags=0x%08x, %s\n", label, flags, s->str);
      g_string_free (s, TRUE);
    } else if (track->num_channels == 0) {
      g_print ("    [%s] flags=0x%08x, switch\n", label, flags);
    } else if (track->num_channels > 0) {
      g_print ("    [%s] flags=0x%08x, slider (%d channels)\n", label, flags,
          track->num_channels);
    } else {
      g_print ("    [%s] flags=0x%08x, UNKNOWN TYPE\n", label, flags);
    }

    g_free (label);
  }

  /* for testing the mixer watch thread / auto-notifications */
  if (strstr (GST_ELEMENT_NAME (element), "mixer") != NULL &&
      opt_show_mixer_messages) {
    show_mixer_messages (element);
  }
}

static void
probe_pad (GstElement * element, const gchar * pad_name)
{
  GstCaps *caps = NULL;
  GstPad *pad;
  guint i;

  pad = gst_element_get_static_pad (element, pad_name);
  if (pad == NULL)
    return;

  caps = gst_pad_get_caps (pad);
  g_return_if_fail (caps != NULL);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    gchar *s;

    s = gst_structure_to_string (gst_caps_get_structure (caps, i));
    g_print ("  %4s[%d]: %s\n", GST_PAD_NAME (pad), i, s);
    g_free (s);
  }
  gst_caps_unref (caps);
  gst_object_unref (pad);
}

static void
probe_details (GstElement * element)
{
  GstStateChangeReturn ret;

  ret = gst_element_set_state (element, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_print ("Could not set element %s to READY.", GST_ELEMENT_NAME (element));
    return;
  }

  probe_pad (element, "sink");
  probe_pad (element, "src");

  probe_mixer_tracks (element);

  gst_element_set_state (element, GST_STATE_NULL);
}

static void
probe_element (const gchar * name)
{
  GstPropertyProbe *probe;
  GValueArray *arr;
  GstElement *element;
  gchar *devname = NULL;
  gint i;

  element = gst_element_factory_make (name, name);

  /* make sure we don't deadlock on GST_ELEMENT_ERROR or do other silly things
   * if we try to query the "device-name" property when the device isn't open */
  g_object_set (element, "device", "/dev/does/not/exist", NULL);
  g_object_get (element, "device-name", &devname, NULL);
  g_assert (devname == NULL);

  /* and now for real */

  probe = GST_PROPERTY_PROBE (element);
  arr = gst_property_probe_probe_and_get_values_name (probe, "device");

  for (i = 0; arr != NULL && i < arr->n_values; ++i) {
    GValue *val;
    gchar *dev_name = NULL;

    g_print ("\n");
    /* we assume the element supports getting the device-name in NULL state */
    val = g_value_array_get_nth (arr, i);
    g_object_set (element, "device", g_value_get_string (val), NULL);
    g_object_get (element, "device-name", &dev_name, NULL);
    g_print ("%-10s device[%d] = %s (%s)\n", GST_OBJECT_NAME (element),
        i, g_value_get_string (val), dev_name);
    if (strstr (dev_name, "/usb")) {
      g_print ("\n\nWARNING: going to probe USB audio device. OSS4 USB support"
          " is still\npretty shaky, so bad things may happen (e.g. kernel "
          "lockup).\nPress Control-C NOW if you don't want to continue. "
          "(waiting 5secs)\n\n");
      g_usleep (5 * G_USEC_PER_SEC);
    }
    g_free (dev_name);

    probe_details (element);
  }

  if (arr) {
    g_value_array_free (arr);
  }

  gst_object_unref (element);
}

int
main (int argc, char **argv)
{
  GOptionEntry options[] = {
    {"show-mixer-messages", 'm', 0, G_OPTION_ARG_NONE, &opt_show_mixer_messages,
        "For mixer elements, wait 60 seconds and show any mixer messages "
          "(for debugging auto-notifications)", NULL},
    {NULL,}
  };
  GOptionContext *ctx;
  GError *err = NULL;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }
  g_option_context_free (ctx);

  probe_element ("oss4sink");
  probe_element ("oss4src");
  probe_element ("oss4mixer");

  return 0;
}
