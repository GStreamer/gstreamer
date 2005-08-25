/* GStreamer
 *
 * unit test for audioconvert
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>

GList *buffers = NULL;
gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define CONVERT_CAPS_TEMPLATE_STRING	\
  "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32, " \
    "buffer-frames = (int) [ 0, MAX ];" \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 32, " \
    "depth = (int) [ 1, 32 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 24, " \
    "depth = (int) [ 1, 24 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 16, " \
    "depth = (int) [ 1, 16 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 8, " \
    "depth = (int) [ 1, 8 ], " \
    "signed = (boolean) { true, false } "

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CONVERT_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CONVERT_CAPS_TEMPLATE_STRING)
    );

/* takes over reference for outcaps */
GstElement *
setup_audioconvert (GstCaps * outcaps)
{
  GstElement *audioconvert;

  GST_DEBUG ("setup_audioconvert");
  audioconvert = gst_check_setup_element ("audioconvert");
  mysrcpad = gst_check_setup_src_pad (audioconvert, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (audioconvert, &sinktemplate, NULL);
  /* this installs a getcaps func that will always return the caps we set
   * later */
  gst_pad_use_fixed_caps (mysinkpad);
  gst_pad_set_caps (mysinkpad, outcaps);
  gst_caps_unref (outcaps);
  outcaps = gst_pad_get_negotiated_caps (mysinkpad);
  fail_unless (gst_caps_is_fixed (outcaps));
  gst_caps_unref (outcaps);

  return audioconvert;
}

void
cleanup_audioconvert (GstElement * audioconvert)
{
  GST_DEBUG ("cleanup_audioconvert");

  gst_check_teardown_src_pad (audioconvert);
  gst_check_teardown_sink_pad (audioconvert);
  gst_check_teardown_element (audioconvert);
}

GstCaps *
get_int_caps (guint rate, guint channels, gchar * endianness, guint width,
    guint depth, gboolean signedness)
{
  GstCaps *caps;
  gchar *string;

  string = g_strdup_printf ("audio/x-raw-int, "
      "rate = (int) %d, "
      "channels = (int) %d, "
      "endianness = (int) %s, "
      "width = (int) %d, "
      "depth = (int) %d, "
      "signed = (boolean) %s ",
      rate, channels, endianness, width, depth, signedness ? "true" : "false");
  GST_DEBUG ("creating caps from %s", string);
  caps = gst_caps_from_string (string);
  fail_unless (caps != NULL);
  g_free (string);
  return caps;
}

static void
verify_convert (GstElement * audioconvert, void *in, int inlength, void *out,
    int outlength, GstCaps * incaps)
{
  GstBuffer *inbuffer, *outbuffer;

  fail_unless (gst_element_set_state (audioconvert,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS, "could not set to playing");

  GST_DEBUG ("Creating buffer of %d bytes", inlength);
  inbuffer = gst_buffer_new_and_alloc (inlength);
  memcpy (GST_BUFFER_DATA (inbuffer), in, inlength);
  gst_buffer_set_caps (inbuffer, incaps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) == 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
  fail_unless_equals_int (GST_BUFFER_SIZE (outbuffer), outlength);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, outlength) == 0);
  buffers = g_list_remove (buffers, outbuffer);
  gst_buffer_unref (outbuffer);
}

#define RUN_CONVERSION(inarray, in_get_caps, outarray, out_get_caps)    \
G_STMT_START {								\
  GstElement *audioconvert;						\
  GstCaps *incaps, *outcaps;						\
									\
  outcaps = out_get_caps;						\
  audioconvert = setup_audioconvert (outcaps);				\
									\
  incaps = in_get_caps;							\
  verify_convert (audioconvert, in, sizeof (in), out, sizeof (out),	\
	incaps);							\
									\
  /* cleanup */								\
  cleanup_audioconvert (audioconvert);					\
} G_STMT_END;

GST_START_TEST (test_int16)
{
  /* stereo to mono */
  {
    gint16 in[] = { 16384, -256, 1024, 1024 };
    gint16 out[] = { 8064, 1024 };

    RUN_CONVERSION (in, get_int_caps (44100, 2, "LITTLE_ENDIAN", 16, 16, TRUE),
        out, get_int_caps (44100, 1, "LITTLE_ENDIAN", 16, 16, TRUE));
  }
  /* mono to stereo */
  {
    gint16 in[] = { 512, 1024 };
    gint16 out[] = { 512, 512, 1024, 1024 };

    RUN_CONVERSION (in, get_int_caps (44100, 1, "LITTLE_ENDIAN", 16, 16, TRUE),
        out, get_int_caps (44100, 2, "LITTLE_ENDIAN", 16, 16, TRUE));
  }
}

GST_END_TEST;

Suite *
audioconvert_suite (void)
{
  Suite *s = suite_create ("audioconvert");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_int16);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = audioconvert_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
