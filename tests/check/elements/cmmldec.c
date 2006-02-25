/*
 * cmmldec.c - GStreamer CMML decoder test suite
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
  "<!DOCTYPE cmml SYSTEM \"cmml.dtd\">\n"\

#define PREAMBLE \
 XML_PREAMBLE "<?cmml?>"

#define PREAMBLE_DECODED \
 XML_PREAMBLE "<cmml >"

#define HEAD_TAG \
  "<head>"\
  "<title>The Research Hunter</title>"\
  "<meta name=\"DC.audience\" content=\"General\"/>"\
  "<meta name=\"DC.author\" content=\"CSIRO Publishing\"/>"\
  "<meta name=\"DC.format\" content=\"video\"/>"\
  "<meta name=\"DC.language\" content=\"English\"/>"\
  "<meta name=\"DC.publisher\" content=\"CSIRO Australia\"/>"\
  "</head>"

#define HEAD_TAG_DECODED HEAD_TAG

#define CLIP_TEMPLATE \
  "<clip id=\"%s\" track=\"%s\">"\
  "<a href=\"http://www.csiro.au/\">http://www.csiro.au</a>"\
  "<img src=\"images/index1.jpg\"/>"\
  "<desc>Welcome to CSIRO</desc>"\
  "<meta name=\"test\" content=\"test content\"/>"\
  "</clip>"

#define CLIP_TEMPLATE_DECODED \
  "<clip id=\"%s\" track=\"%s\" start=\"%s\">"\
  "<a href=\"http://www.csiro.au/\">http://www.csiro.au</a>"\
  "<img src=\"images/index1.jpg\"/>"\
  "<desc>Welcome to CSIRO</desc>"\
  "<meta name=\"test\" content=\"test content\"/>"\
  "</clip>"

#define END_TAG \
  "</cmml>"

GList *buffers;
GList *current_buf = NULL;
gint64 granulerate;
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

static GstBuffer *
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
  ASSERT_OBJECT_REFCOUNT (buffer, "buf", 1);
  gst_buffer_unref (GST_BUFFER (buffer));
}

GstElement *
setup_cmmldec ()
{
  GstElement *cmmldec;
  GstBus *bus;

  GST_DEBUG ("setup_cmmldec");
  cmmldec = gst_check_setup_element ("cmmldec");
  srcpad = gst_check_setup_src_pad (cmmldec, &srctemplate, NULL);
  sinkpad = gst_check_setup_sink_pad (cmmldec, &sinktemplate, NULL);

  bus = gst_bus_new ();
  gst_element_set_bus (cmmldec, bus);

  fail_unless (gst_element_set_state (cmmldec,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  granulerate = GST_SECOND / 1000;
  granuleshift = 32;
  buffers = NULL;

  return cmmldec;
}

static void
cleanup_cmmldec (GstElement * cmmldec)
{
  GstBus *bus;

  g_list_foreach (buffers, buffer_unref, NULL);
  g_list_free (buffers);

  bus = GST_ELEMENT_BUS (cmmldec);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  GST_DEBUG ("cleanup_cmmldec");
  gst_check_teardown_src_pad (cmmldec);
  gst_check_teardown_sink_pad (cmmldec);
  gst_check_teardown_element (cmmldec);
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
    const gchar * data, gint size, gint64 granulepos,
    GstFlowReturn expected_return)
{
  GstBuffer *buffer;
  GstFlowReturn res;

  buffer = buffer_new (data, size);
  GST_BUFFER_OFFSET_END (buffer) = granulepos;
  res = gst_pad_push (srcpad, buffer);
  fail_unless (res == expected_return,
      "pushing %s returned %d not %d", name, res, expected_return);
}

static void
check_headers ()
{
  /* push the ident header */
  push_data ("ident-header", IDENT_HEADER, 29, 0, GST_FLOW_OK);
  /* push the cmml start tag */
  push_data ("preamble", PREAMBLE, strlen (PREAMBLE), 0, GST_FLOW_OK);
  /* push the head tag */
  push_data ("head", HEAD_TAG, strlen (HEAD_TAG), 0, GST_FLOW_OK);

  current_buf = buffers;
  fail_unless_equals_int (g_list_length (current_buf), 2);

  /* check the preamble */
  check_output_buffer_is_equal ("cmml-preamble-buffer", PREAMBLE_DECODED, 1);

  /* check the decoded head tag */
  current_buf = current_buf->next;
  check_output_buffer_is_equal ("head-tag-buffer", HEAD_TAG_DECODED, 1);
}

static void
push_clip (const gchar * name, const gchar * track, GstClockTime prev,
    GstClockTime start, GstClockTime end, GstFlowReturn expected_return)
{
  gchar *clip;
  gint64 keyindex, keyoffset, granulepos;

  if (track == NULL)
    track = "default";

  keyindex = prev / granulerate << granuleshift;
  keyoffset = (start - prev) / granulerate;
  granulepos = keyindex + keyoffset;

  clip = g_strdup_printf (CLIP_TEMPLATE, name, track);
  push_data (name, clip, strlen (clip), granulepos, expected_return);
  g_free (clip);
}

static void
check_clip (const gchar * name, const gchar * track,
    const gchar * start, const gchar * end)
{
  gchar *decoded_clip;

  if (track == NULL)
    track = "default";

  current_buf = current_buf->next;
  fail_unless (g_list_length (current_buf));
  decoded_clip = g_strdup_printf (CLIP_TEMPLATE_DECODED, name, track, start);
  check_output_buffer_is_equal (name, decoded_clip, 1);
  g_free (decoded_clip);
}

static void
check_end ()
{
  current_buf = current_buf->next;
  check_output_buffer_is_equal ("cmml-end-tag", END_TAG, 1);
}

GST_START_TEST (test_dec)
{
  GstElement *cmmldec;

  cmmldec = setup_cmmldec ();

  check_headers ();

  push_clip ("clip-1", "default",
      0, 1 * GST_SECOND + 234 * GST_MSECOND, 0, GST_FLOW_OK);
  push_clip ("clip-2", "othertrack",
      0, 4 * GST_SECOND + 321 * GST_MSECOND, 0, GST_FLOW_OK);
  push_clip ("clip-3", "default",
      1 * GST_SECOND + 234 * GST_MSECOND,
      ((100 * 3600) + (59 * 60) + 59) * GST_SECOND + 678 * GST_MSECOND, 0,
      GST_FLOW_OK);
  /* send EOS to flush clip-2 and clip-3 */
  gst_pad_send_event (GST_PAD_PEER (srcpad), gst_event_new_eos ());

  check_clip ("clip-1", "default", "0:00:01.234", NULL);
  check_clip ("clip-2", "othertrack", "0:00:04.321", NULL);
  check_clip ("clip-3", "default", "100:59:59.678", NULL);
  check_end ();

  cleanup_cmmldec (cmmldec);
}

GST_END_TEST;

GST_START_TEST (test_tags)
{
  GstElement *cmmldec;
  GstBus *bus;
  GstMessage *message;
  GstTagList *tags;
  const GValue *tag_val;
  GObject *tag;
  gchar *title, *base;
  gboolean empty;
  gchar *id, *track;
  gint64 start_time, end_time;
  gchar *anchor_href, *anchor_text;
  gchar *img_src, *img_alt;
  gchar *desc;
  GValueArray *meta;

  cmmldec = setup_cmmldec ();
  bus = gst_element_get_bus (cmmldec);

  check_headers ();

  /* read the GstCmmlTagHead tag */
  message = gst_bus_poll (bus, GST_MESSAGE_TAG, -1);
  fail_unless (message != NULL);

  gst_message_parse_tag (message, &tags);
  fail_unless (tags != NULL);

  tag_val = gst_tag_list_get_value_index (tags, GST_TAG_CMML_HEAD, 0);
  fail_unless (tag_val != NULL);

  tag = g_value_get_object (tag_val);
  fail_unless (tags != NULL);

  g_object_get (tag, "title", &title, "base-uri", &base, "meta", &meta, NULL);
  fail_unless_equals_string ("The Research Hunter", title);
  fail_unless (base == NULL);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->n_values, 10);

  gst_message_unref (message);
  gst_tag_list_free (tags);
  g_free (title);
  g_free (base);
  g_value_array_free (meta);

  push_clip ("clip-1", "default",
      0, 1 * GST_SECOND + 234 * GST_MSECOND, 0, GST_FLOW_OK);

  /* read the GstCmmlTagClip */
  message = gst_bus_poll (bus, GST_MESSAGE_TAG, -1);
  fail_unless (message != NULL);

  gst_message_parse_tag (message, &tags);
  fail_unless (tags != NULL);

  tag_val = gst_tag_list_get_value_index (tags, GST_TAG_CMML_CLIP, 0);
  fail_unless (tag_val != NULL);

  tag = g_value_get_object (tag_val);
  fail_unless (tag != NULL);

  g_object_get (tag, "id", &id, "empty", &empty, "track", &track,
      "start-time", &start_time, "end-time", &end_time,
      "anchor-uri", &anchor_href, "anchor-text", &anchor_text,
      "img-uri", &img_src, "img-alt", &img_alt,
      "description", &desc, "meta", &meta, NULL);

  fail_unless (empty == FALSE);
  fail_unless_equals_string (id, "clip-1");
  fail_unless_equals_string (track, "default");
  fail_unless_equals_int (start_time, 1 * GST_SECOND + 234 * GST_MSECOND);
  fail_unless_equals_uint64 (end_time, GST_CLOCK_TIME_NONE);
  fail_unless_equals_string (anchor_href, "http://www.csiro.au/");
  fail_unless_equals_string (anchor_text, "http://www.csiro.au");
  fail_unless_equals_string (img_src, "images/index1.jpg");
  fail_unless (img_alt == NULL);
  fail_unless_equals_string (desc, "Welcome to CSIRO");
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->n_values, 2);

  g_free (id);
  g_free (track);
  g_free (anchor_href);
  g_free (anchor_text);
  g_free (img_src);
  g_free (img_alt);
  g_free (desc);
  g_value_array_free (meta);
  gst_tag_list_free (tags);
  gst_message_unref (message);
  gst_object_unref (bus);
  cleanup_cmmldec (cmmldec);
}

GST_END_TEST;

Suite *
cmmldec_suite ()
{
  Suite *s = suite_create ("cmmldec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_dec);
  tcase_add_test (tc_chain, test_tags);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = cmmldec_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
