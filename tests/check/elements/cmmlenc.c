/*
 * cmmlenc.c - GStreamer CMML decoder test suite
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#include <gst/check/gstcheck.h>

#include <gst/tag/tag.h>

#define SINK_CAPS "text/x-cmml"
#define SRC_CAPS "text/x-cmml"

#define IDENT_HEADER \
  "CMML\x00\x00\x00\x00"\
  "\x03\x00\x00\x00"\
  "\xe8\x03\x00\x00\x00\x00\x00\x00"\
  "\x01\x00\x00\x00\x00\x00\x00\x00"\
  "\x20"

#define XML_PREAMBLE \
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"\
  "<!DOCTYPE cmml SYSTEM \"cmml.dtd\">\n"

#define START_TAG \
  "<cmml>"

#define PROCESSING_INSTRUCTION \
  "<?cmml ?>"

#define PREAMBLE \
  XML_PREAMBLE START_TAG

#define PREAMBLE_ENCODED \
  XML_PREAMBLE PROCESSING_INSTRUCTION

#define STREAM_TAG \
  "<stream timebase=\"10\">"\
     "<import src=\"test.ogg\"/>"\
     "<import src=\"test1.ogg\"/>"\
   "</stream>"

#define STREAM_TAG_ENCODED STREAM_TAG

#define HEAD_TAG \
  "<head>"\
    "<title>The Research Hunter</title>"\
    "<meta name=\"DC.audience\" content=\"General\"/>"\
    "<meta name=\"DC.author\" content=\"CSIRO Publishing\"/>"\
    "<meta name=\"DC.format\" content=\"video\"/>"\
    "<meta name=\"DC.language\" content=\"English\"/>"\
    "<meta name=\"DC.publisher\" content=\"CSIRO Australia\"/>"\
  "</head>"

#define HEAD_TAG_ENCODED HEAD_TAG

#define END_TAG \
  "</cmml>"

#define CLIP_TEMPLATE \
  "<clip id=\"%s\" track=\"%s\" start=\"%s\">"\
    "<a href=\"http://www.annodex.org/\">http://www.annodex.org</a>"\
    "<img src=\"images/index.jpg\"/>"\
    "<desc>Annodex Foundation</desc>"\
    "<meta name=\"test\" content=\"test content\"/>"\
  "</clip>"

#define CLIP_TEMPLATE_ENCODED \
  "<clip id=\"%s\" track=\"%s\">"\
    "<a href=\"http://www.annodex.org/\">http://www.annodex.org</a>"\
    "<img src=\"images/index.jpg\"/>"\
    "<desc>Annodex Foundation</desc>"\
    "<meta name=\"test\" content=\"test content\"/>"\
  "</clip>"

GList *buffers;
GList *current_buf = NULL;
guint64 granulerate;
guint8 granuleshift;

GstPad *srcpad, *sinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS)
    );

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS)
    );

GstBuffer *
buffer_new (const gchar * buffer_data, guint size)
{
  GstBuffer *buffer;
  GstCaps *caps;

  buffer = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buffer), buffer_data, size);
  caps = gst_caps_from_string (SRC_CAPS);
  gst_buffer_set_caps (buffer, caps);
  gst_caps_unref (caps);

  return buffer;
}

static void
buffer_unref (void *buffer, void *user_data)
{
  gst_buffer_unref (GST_BUFFER (buffer));
}

GstElement *
setup_cmmlenc ()
{
  GstElement *cmmlenc;
  GstBus *bus;
  guint64 granulerate_n, granulerate_d;

  GST_DEBUG ("setup_cmmlenc");

  cmmlenc = gst_check_setup_element ("cmmlenc");
  srcpad = gst_check_setup_src_pad (cmmlenc, &srctemplate, NULL);
  sinkpad = gst_check_setup_sink_pad (cmmlenc, &sinktemplate, NULL);

  bus = gst_bus_new ();
  gst_element_set_bus (cmmlenc, bus);

  fail_unless (gst_element_set_state (cmmlenc,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  g_object_get (cmmlenc, "granule-rate-numerator", &granulerate_n,
      "granule-rate-denominator", &granulerate_d,
      "granule-shift", &granuleshift, NULL);

  granulerate = GST_SECOND * granulerate_d / granulerate_n;
  buffers = NULL;
  return cmmlenc;
}

static void
cleanup_cmmlenc (GstElement * cmmlenc)
{
  GstBus *bus;

  /* free encoded buffers */
  g_list_foreach (buffers, buffer_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  bus = GST_ELEMENT_BUS (cmmlenc);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  GST_DEBUG ("cleanup_cmmlenc");
  gst_check_teardown_src_pad (cmmlenc);
  gst_check_teardown_sink_pad (cmmlenc);
  gst_check_teardown_element (cmmlenc);
}

static void
check_output_buffer_is_equal (const gchar * name,
    const gchar * data, gint refcount)
{
  GstBuffer *buffer = GST_BUFFER (current_buf->data);

  ASSERT_OBJECT_REFCOUNT (buffer, name, refcount);
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), data,
          GST_BUFFER_SIZE (buffer)) == 0,
      "'%s' (%s) is not equal to (%s)", name, GST_BUFFER_DATA (buffer), data);
}

static void
push_data (const gchar * name,
    const gchar * data, gint size, GstFlowReturn expected_return)
{
  GstBuffer *buffer;
  GstFlowReturn res;

  buffer = buffer_new (data, size);
  res = gst_pad_push (srcpad, buffer);
  fail_unless (res == expected_return,
      "pushing %s returned %d not %d", name, res, expected_return);
}

static void
check_headers ()
{
  /* push the cmml start tag */
  push_data ("preamble", PREAMBLE, strlen (PREAMBLE), GST_FLOW_OK);
  /* push the stream tag */
  push_data ("stream", STREAM_TAG, strlen (STREAM_TAG), GST_FLOW_OK);
  /* push the head tag */
  push_data ("head", HEAD_TAG, strlen (HEAD_TAG), GST_FLOW_OK);

  /* should output the cmml ident header and the cmml start tag transformed
   * into a processing instruction */
  current_buf = buffers;
  fail_unless_equals_int (g_list_length (current_buf), 3);

  /* check the ident header */
  check_output_buffer_is_equal ("cmml-ident-buffer", IDENT_HEADER, 1);

  /* check the cmml processing instruction */
  current_buf = current_buf->next;
  check_output_buffer_is_equal ("cmml-preamble-buffer", PREAMBLE_ENCODED, 1);

  /* check the encoded head tag */
  current_buf = current_buf->next;
  check_output_buffer_is_equal ("head-tag-buffer", HEAD_TAG_ENCODED, 1);
}

static void
push_clip (const gchar * name, const gchar * track,
    const gchar * start, const gchar * end, GstFlowReturn expected_return)
{
  gchar *clip;

  if (track == NULL)
    track = "default";

  clip = g_strdup_printf (CLIP_TEMPLATE, name, track, start);
  push_data (name, clip, strlen (clip), expected_return);
  g_free (clip);
}

static void
check_clip (const gchar * name, const gchar * track,
    GstClockTime start, GstClockTime prev)
{
  gchar *encoded_clip;
  GstBuffer *buffer;
  gint64 keyindex, keyoffset, granulepos;

  if (track == NULL)
    track = "default";

  current_buf = current_buf->next;
  fail_unless (g_list_length (current_buf));
  encoded_clip = g_strdup_printf (CLIP_TEMPLATE_ENCODED, name, track);
  check_output_buffer_is_equal (name, encoded_clip, 1);
  g_free (encoded_clip);
  buffer = GST_BUFFER (current_buf->data);
  granulepos = GST_BUFFER_OFFSET_END (GST_BUFFER (buffer));
  keyindex = granulepos >> granuleshift;
  keyoffset = granulepos - (keyindex << granuleshift);
  fail_unless_equals_uint64 (keyindex * granulerate, prev);
  fail_unless_equals_uint64 ((keyindex + keyoffset) * granulerate, start);
}

static void
push_end ()
{
  push_data ("end", END_TAG, strlen (END_TAG), GST_FLOW_OK);
}

static void
check_end ()
{
  /* should output the EOS page */
  current_buf = current_buf->next;
  fail_unless_equals_int (g_list_length (current_buf), 1);
  check_output_buffer_is_equal ("cmml-eos-buffer", NULL, 1);
}

GST_START_TEST (test_enc)
{
  GstElement *cmmlenc;

  cmmlenc = setup_cmmlenc ();

  check_headers ();

  push_clip ("clip-1", "default", "1.234", NULL, GST_FLOW_OK);
  check_clip ("clip-1", "default", 1234 * granulerate, 0);

  push_clip ("clip-2", NULL, "5.678", NULL, GST_FLOW_OK);
  check_clip ("clip-2", "default", 5678 * granulerate, 1234 * granulerate);

  push_clip ("clip-3", "othertrack", "9.123", NULL, GST_FLOW_OK);
  check_clip ("clip-3", "othertrack", 9123 * granulerate, 0);

  push_end ();
  check_end ();

  cleanup_cmmlenc (cmmlenc);
}

GST_END_TEST;

GST_START_TEST (test_bad_start_time)
{
  GstElement *cmmlenc;

  cmmlenc = setup_cmmlenc ();

  check_headers ();

  push_clip ("clip-1", "default", "1000:00:00.000", NULL, GST_FLOW_OK);
  check_clip ("clip-1", "default", (guint64) 3600000 * 1000 * granulerate, 0);

  /* keyindex overflow: npt:1000:00:00.000 doesn't fit in 32 bits */
  push_clip ("clip-2", NULL, "5.678", NULL, GST_FLOW_ERROR);

  /* other tracks should work */
  push_clip ("clip-3", "othertrack", "9.123", NULL, GST_FLOW_OK);
  check_clip ("clip-3", "othertrack", 9123 * granulerate, 0);

  /* bad msecs */
  push_clip ("clip-bad-msecs", "default", "0.1000", NULL, GST_FLOW_ERROR);

  /* bad secs */
  push_clip ("clip-bad-secs", "default", "00:00:60.123", NULL, GST_FLOW_ERROR);

  /* bad minutes */
  push_clip ("clip-bad-minutes", "default", "00:60:12.345",
      NULL, GST_FLOW_ERROR);

  /* bad hours */
  push_clip ("clip-bad-hours", "default", "10000:12:34.567",
      NULL, GST_FLOW_ERROR);

  push_end ();
  check_end ();

  cleanup_cmmlenc (cmmlenc);
}
GST_END_TEST static Suite *
cmmlenc_suite ()
{
  Suite *s = suite_create ("cmmlenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_enc);
  tcase_add_test (tc_chain, test_bad_start_time);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = cmmlenc_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
