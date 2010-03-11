/* GStreamer
 *
 * unit tests for audio support library
 *
 * Copyright (C) 2007 Jan Schmidt <thaytan@noraisin.net>
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

#include <gst/check/gstcheck.h>

#include <gst/interfaces/mixer.h>

#include <string.h>

#define TEST_ELEMENT_TYPE (test_element_get_type())

typedef struct TestElement TestElement;
typedef struct TestElementClass TestElementClass;

struct TestElement
{
  GstElement parent;
};

struct TestElementClass
{
  GstElementClass parent_class;
};

GType test_element_get_type (void);

static void init_interface (GType type);
static void gst_implements_interface_init (GstImplementsInterfaceClass * klass);

GST_BOILERPLATE_FULL (TestElement, test_element, GstElement, GST_TYPE_ELEMENT,
    init_interface);

static void
test_element_mixer_interface_init (GstMixerClass * klass)
{
  /* Not actually implementing any interfaces for this test atm */
}

static void
init_interface (GType type)
{
  static const GInterfaceInfo mixer_iface_info = {
    (GInterfaceInitFunc) test_element_mixer_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_implements_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);
  g_type_add_interface_static (type, GST_TYPE_MIXER, &mixer_iface_info);
}

static void
test_element_base_init (gpointer klass)
{
}

static void
test_element_class_init (TestElementClass * klass)
{
}

static gboolean
test_element_interface_supported (GstImplementsInterface * ifacE,
    GType interface_type)
{
  if (interface_type == GST_TYPE_MIXER)
    return TRUE;

  return FALSE;
}

static void
gst_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = test_element_interface_supported;
}

static void
test_element_init (TestElement * this, TestElementClass * klass)
{
}

GST_START_TEST (test_messages)
{
  /* Create an empty GstElement that has a GstMixer interface and then
   * send some notifications and validate them */
  GstElement *test_element =
      (GstElement *) g_object_new (TEST_ELEMENT_TYPE, NULL);
  GstBus *bus = gst_bus_new ();
  GstMixerTrack *mtrack = g_object_new (GST_TYPE_MIXER_TRACK, NULL);
  GstMixerOptions *mopts = g_object_new (GST_TYPE_MIXER_OPTIONS, NULL);
  GstMixerTrack *t;
  GstMixerOptions *o;
  gint vols_in[2] = { 50, 75 };
  gboolean mute, record;
  gint *vols_out;
  gint n_chans, i;
  const gchar *val;
  GstMessage *message;

  mtrack->num_channels = 2;
  mtrack->flags = GST_MIXER_TRACK_MUTE | GST_MIXER_TRACK_RECORD;

  gst_element_set_bus (test_element, bus);

  /* Test mute-toggled */
  gst_mixer_mute_toggled (GST_MIXER (test_element), mtrack, TRUE);
  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, GST_CLOCK_TIME_NONE);
  fail_if (message == NULL);
  fail_unless (gst_mixer_message_get_type (message) ==
      GST_MIXER_MESSAGE_MUTE_TOGGLED);
  /* Test that we can pass NULL args */
  gst_mixer_message_parse_mute_toggled (message, NULL, NULL);
  /* Test the parsing */
  gst_mixer_message_parse_mute_toggled (message, &t, &mute);
  fail_unless (t == mtrack);
  fail_unless (mute == TRUE);
  gst_message_unref (message);

  /* Test record-toggled */
  gst_mixer_record_toggled (GST_MIXER (test_element), mtrack, TRUE);
  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, GST_CLOCK_TIME_NONE);
  fail_if (message == NULL);
  fail_unless (gst_mixer_message_get_type (message) ==
      GST_MIXER_MESSAGE_RECORD_TOGGLED);
  gst_mixer_message_parse_record_toggled (message, NULL, NULL);
  gst_mixer_message_parse_record_toggled (message, &t, &record);
  fail_unless (t == mtrack);
  fail_unless (record == TRUE);
  gst_message_unref (message);

  /* Test volume-changed */
  gst_mixer_volume_changed (GST_MIXER (test_element), mtrack, vols_in);
  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, GST_CLOCK_TIME_NONE);
  fail_if (message == NULL);
  fail_unless (gst_mixer_message_get_type (message) ==
      GST_MIXER_MESSAGE_VOLUME_CHANGED);
  gst_mixer_message_parse_volume_changed (message, NULL, NULL, NULL);
  gst_mixer_message_parse_volume_changed (message, NULL, NULL, &n_chans);
  fail_unless (n_chans == 2);
  gst_mixer_message_parse_volume_changed (message, &t, &vols_out, &n_chans);
  fail_unless (mtrack == t);
  for (i = 0; i < n_chans; i++)
    fail_unless (vols_out[i] == vols_in[i]);

  gst_message_unref (message);
  g_free (vols_out);

  /* Test option-changed */
  gst_mixer_option_changed (GST_MIXER (test_element), mopts, "TESTING");
  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, GST_CLOCK_TIME_NONE);
  fail_if (message == NULL);
  fail_unless (gst_mixer_message_get_type (message) ==
      GST_MIXER_MESSAGE_OPTION_CHANGED);
  gst_mixer_message_parse_option_changed (message, NULL, NULL);
  gst_mixer_message_parse_option_changed (message, &o, &val);
  fail_unless (o == mopts);
  fail_unless (g_str_equal (val, "TESTING"));
  gst_message_unref (message);

  /* Test options-list-changed */
  gst_mixer_options_list_changed (GST_MIXER (test_element), mopts);
  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, GST_CLOCK_TIME_NONE);
  fail_if (message == NULL);
  fail_unless (gst_mixer_message_get_type (message) ==
      GST_MIXER_MESSAGE_OPTIONS_LIST_CHANGED);
  gst_mixer_message_parse_options_list_changed (message, &o);
  fail_unless (o == mopts);
  gst_message_unref (message);

  /* Test mixer-changed */
  gst_mixer_mixer_changed (GST_MIXER (test_element));
  message = gst_bus_poll (bus, GST_MESSAGE_ELEMENT, GST_CLOCK_TIME_NONE);
  fail_if (message == NULL);
  fail_unless (gst_mixer_message_get_type (message) ==
      GST_MIXER_MESSAGE_MIXER_CHANGED);
  gst_message_unref (message);

  gst_object_unref (mtrack);
  gst_object_unref (mopts);
  gst_object_unref (bus);
  gst_object_unref (test_element);
}

GST_END_TEST;

static Suite *
mixer_suite (void)
{
  Suite *s = suite_create ("mixer interface");
  TCase *tc_chain = tcase_create ("notifications");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_messages);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = mixer_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
