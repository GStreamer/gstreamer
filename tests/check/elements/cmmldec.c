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
#define SRC_CAPS "text/x-cmml, encoded=(boolean)TRUE"

#define IDENT_HEADER \
  "CMML\x00\x00\x00\x00"\
  "\x03\x00\x00\x00"\
  "\xe8\x03\x00\x00\x00\x00\x00\x00"\
  "\x01\x00\x00\x00\x00\x00\x00\x00"\
  "\x20"
#define IDENT_HEADER_SIZE 29

#define PREAMBLE_NO_PI \
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"\
  "<!DOCTYPE cmml SYSTEM \"cmml.dtd\">\n"
#define PREAMBLE PREAMBLE_NO_PI "<?cmml?>"
#define PREAMBLE_DECODED PREAMBLE_NO_PI "<cmml >"

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

#define EMPTY_CLIP_TEMPLATE \
  "<clip id=\"%s\" track=\"%s\" />"

#define END_TAG \
  "</cmml>"

#define fail_unless_equals_flow_return(a, b)                            \
G_STMT_START {                                                          \
  gchar *a_up = g_ascii_strup (gst_flow_get_name (a), -1);              \
  gchar *b_up = g_ascii_strup (gst_flow_get_name (b), -1);              \
  fail_unless (a == b,                                                  \
      "'" #a "' (GST_FLOW_%s) is not equal to '" #b "' (GST_FLOW_%s)",  \
      a_up, b_up);                                                      \
  g_free (a_up);                                                        \
  g_free (b_up);                                                        \
} G_STMT_END;

static GstElement *cmmldec;

static GstBus *bus;

static GstFlowReturn flow;

static GList *current_buf;

static gint64 granulerate;

static guint8 granuleshift;

static GstPad *srcpad, *sinkpad;

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

static void
setup_cmmldec (void)
{
  GST_DEBUG ("setup_cmmldec");
  cmmldec = gst_check_setup_element ("cmmldec");
  srcpad = gst_check_setup_src_pad (cmmldec, &srctemplate, NULL);
  sinkpad = gst_check_setup_sink_pad (cmmldec, &sinktemplate, NULL);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  bus = gst_bus_new ();
  gst_element_set_bus (cmmldec, bus);

  fail_unless (gst_element_set_state (cmmldec,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  granulerate = GST_SECOND / 1000;
  granuleshift = 32;
  buffers = NULL;
}

static void
teardown_cmmldec (void)
{
  g_list_foreach (buffers, buffer_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;
  current_buf = NULL;

  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  GST_DEBUG ("teardown_cmmldec");
  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (cmmldec);
  gst_check_teardown_sink_pad (cmmldec);
  gst_check_teardown_element (cmmldec);
}

static void
check_output_buffer_is_equal (const gchar * name,
    const gchar * data, gint refcount)
{
  GstBuffer *buffer;

  if (current_buf == NULL)
    current_buf = buffers;
  else
    current_buf = g_list_next (current_buf);

  fail_unless (current_buf != NULL);

  buffer = GST_BUFFER (current_buf->data);

  ASSERT_OBJECT_REFCOUNT (buffer, name, refcount);
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), data,
          GST_BUFFER_SIZE (buffer)) == 0,
      "'%s' (%s) is not equal to (%s)", name, GST_BUFFER_DATA (buffer), data);
}

static GstFlowReturn
push_data (const gchar * name, const gchar * data, gint size, gint64 granulepos)
{
  GstBuffer *buffer;

  buffer = buffer_new (data, size);
  GST_BUFFER_OFFSET_END (buffer) = granulepos;
  return gst_pad_push (srcpad, buffer);
}

static GObject *
cmml_tag_message_pop (GstBus * bus, const gchar * tag)
{
  GstMessage *message;

  GstTagList *taglist;

  const GValue *value;

  GObject *obj;

  message = gst_bus_poll (bus, GST_MESSAGE_TAG, 0);
  if (message == NULL)
    return NULL;

  gst_message_parse_tag (message, &taglist);
  value = gst_tag_list_get_value_index (taglist, tag, 0);
  if (value == NULL) {
    gst_message_unref (message);
    gst_tag_list_free (taglist);
    return NULL;
  }

  obj = g_value_dup_object (value);
  gst_message_unref (message);
  gst_tag_list_free (taglist);

  return obj;
}

static void
check_headers (void)
{
  GObject *head_tag;

  gchar *title, *base;

  GValueArray *meta;

  /* push the ident header */
  flow = push_data ("ident-header", IDENT_HEADER, IDENT_HEADER_SIZE, 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  /* push the cmml preamble */
  flow = push_data ("preamble", PREAMBLE, strlen (PREAMBLE), 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  /* push the head tag */
  flow = push_data ("head", HEAD_TAG, strlen (HEAD_TAG), 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  fail_unless_equals_int (g_list_length (buffers), 2);

  /* check the decoded preamble */
  check_output_buffer_is_equal ("cmml-preamble-buffer", PREAMBLE_DECODED, 1);

  /* check the decoded head tag */
  check_output_buffer_is_equal ("head-tag-buffer", HEAD_TAG_DECODED, 1);

  /* check the GstCmmlTagHead tag object */
  head_tag = cmml_tag_message_pop (bus, GST_TAG_CMML_HEAD);
  fail_unless (head_tag != NULL);
  g_object_get (head_tag,
      "title", &title, "base-uri", &base, "meta", &meta, NULL);
  fail_unless_equals_string ("The Research Hunter", title);
  fail_unless (base == NULL);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->n_values, 10);

  g_free (title);
  g_free (base);
  g_value_array_free (meta);
  g_object_unref (head_tag);
}

static GstFlowReturn
push_clip_full (const gchar * name, const gchar * track, const gchar * template,
    GstClockTime prev, GstClockTime start)
{
  gchar *clip;

  gint64 keyindex, keyoffset, granulepos;

  GstFlowReturn res;

  if (track == NULL)
    track = "default";

  if (prev == GST_CLOCK_TIME_NONE)
    prev = 0;

  keyindex = prev / granulerate << granuleshift;
  keyoffset = (start - prev) / granulerate;
  granulepos = keyindex + keyoffset;

  clip = g_strdup_printf (template, name, track);
  res = push_data (name, clip, strlen (clip), granulepos);
  g_free (clip);

  return res;
}

static GstFlowReturn
push_clip (const gchar * name, const gchar * track,
    GstClockTime prev, GstClockTime start)
{
  return push_clip_full (name, track, CLIP_TEMPLATE, prev, start);
}

static GstFlowReturn
push_empty_clip (const gchar * name, const gchar * track, GstClockTime start)
{
  return push_clip_full (name, track,
      EMPTY_CLIP_TEMPLATE, GST_CLOCK_TIME_NONE, start);
}


static void
check_output_clip (const gchar * name, const gchar * track,
    const gchar * start, const gchar * end)
{
  gchar *decoded_clip;

  if (track == NULL)
    track = "default";

  decoded_clip = g_strdup_printf (CLIP_TEMPLATE_DECODED, name, track, start);
  check_output_buffer_is_equal (name, decoded_clip, 1);
  g_free (decoded_clip);
}

GST_START_TEST (test_dec)
{
  GstClockTime clip1_start = 1 * GST_SECOND + 234 * GST_MSECOND;

  GstClockTime clip2_start = clip1_start;

  GstClockTime clip3_start =
      ((100 * 3600) + (59 * 60) + 59) * GST_SECOND + 678 * GST_MSECOND;

  check_headers ();

  flow = push_clip ("clip-1", "default", GST_CLOCK_TIME_NONE, clip1_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  flow = push_clip ("clip-2", "othertrack", GST_CLOCK_TIME_NONE, clip2_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  flow = push_clip ("clip-3", "default", clip1_start, clip3_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  /* send EOS to flush clip-2 and clip-3 */
  gst_pad_send_event (GST_PAD_PEER (srcpad), gst_event_new_eos ());

  check_output_clip ("clip-1", "default", "0:00:01.234", NULL);
  check_output_clip ("clip-2", "othertrack", "0:00:01.234", NULL);
  check_output_clip ("clip-3", "default", "100:59:59.678", NULL);
  check_output_buffer_is_equal ("cmml-end-tag", END_TAG, 1);
}

GST_END_TEST;

GST_START_TEST (test_preamble_no_pi)
{
  flow = push_data ("ident-header", IDENT_HEADER, IDENT_HEADER_SIZE, 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 0);

  flow = push_data ("preamble-no-pi",
      PREAMBLE_NO_PI, strlen (PREAMBLE_NO_PI), 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);

  check_output_buffer_is_equal ("cmml-preamble-buffer",
      PREAMBLE_NO_PI "<cmml>", 1);
}

GST_END_TEST;

GST_START_TEST (test_tags)
{
  GObject *tag;

  gboolean empty;

  gchar *id, *track;

  gint64 start_time, end_time;

  gchar *anchor_href, *anchor_text;

  gchar *img_src, *img_alt;

  gchar *desc;

  GValueArray *meta;

  GstClockTime clip1_start;

  check_headers ();

  clip1_start = 1 * GST_SECOND + 234 * GST_MSECOND;
  flow = push_clip ("clip-1", "default", 0, clip1_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  tag = cmml_tag_message_pop (bus, GST_TAG_CMML_CLIP);
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
  g_object_unref (tag);
}

GST_END_TEST;

GST_START_TEST (test_wait_clip_end)
{
  GObject *tag;

  gchar *id;

  GstClockTime end_time = 0;

  GstClockTime clip1_start = 1 * GST_SECOND + 234 * GST_MSECOND;

  GstClockTime clip2_start = 2 * GST_SECOND + 234 * GST_MSECOND;

  GstClockTime clip3_start = 3 * GST_SECOND + 234 * GST_MSECOND;

  GstClockTime clip3_end = 4 * GST_SECOND + 234 * GST_MSECOND;

  GstClockTime clip4_start = 5 * GST_SECOND + 234 * GST_MSECOND;

  g_object_set (cmmldec, "wait-clip-end-time", TRUE, NULL);

  check_headers ();

  flow = push_clip ("clip-1", "default", 0, clip1_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  /* no tag has been posted yet */
  fail_unless (cmml_tag_message_pop (bus, GST_TAG_CMML_CLIP) == NULL);

  flow = push_clip ("clip-2", "default", clip1_start, clip2_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  tag = cmml_tag_message_pop (bus, GST_TAG_CMML_CLIP);
  fail_unless (tag != NULL);
  g_object_get (tag, "id", &id, "end-time", &end_time, NULL);
  /* clip-1 is posted when clip-2 is decoded. clip-1 ends when clip-2 starts */
  fail_unless_equals_string (id, "clip-1");
  fail_unless_equals_int (end_time, clip2_start);
  g_free (id);
  g_object_unref (tag);

  flow = push_clip ("clip-3", "default", clip2_start, clip3_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  tag = cmml_tag_message_pop (bus, GST_TAG_CMML_CLIP);
  fail_unless (tag != NULL);
  g_object_get (tag, "id", &id, "end-time", &end_time, NULL);
  /* clip-2 is posted when clip-3 is decoded. It ends when clip-3 starts */
  fail_unless_equals_string (id, "clip-2");
  fail_unless_equals_int (end_time, clip3_start);
  g_free (id);
  g_object_unref (tag);

  flow = push_empty_clip ("empty-clip", "default", clip3_end);
  tag = cmml_tag_message_pop (bus, GST_TAG_CMML_CLIP);
  fail_unless (tag != NULL);
  g_object_get (tag, "id", &id, "end-time", &end_time, NULL);
  /* clip-3 ends when empty-clip is decoded */
  fail_unless_equals_string (id, "clip-3");
  fail_unless_equals_int (end_time, clip3_end);
  g_free (id);
  g_object_unref (tag);

  flow = push_clip ("clip-4", "default", clip3_start, clip4_start);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  /* an empty clip just marks the end of the previous one, so no tag is posted
   * for empty-clip */
  fail_unless (cmml_tag_message_pop (bus, GST_TAG_CMML_CLIP) == NULL);
  /* send EOS to flush clip-4 */
  gst_pad_send_event (GST_PAD_PEER (srcpad), gst_event_new_eos ());

  tag = cmml_tag_message_pop (bus, GST_TAG_CMML_CLIP);
  fail_unless (tag != NULL);
  g_object_get (tag, "id", &id, NULL);
  fail_unless_equals_string (id, "clip-4");
  g_free (id);
  g_object_unref (tag);
}

GST_END_TEST;

GST_START_TEST (test_weird_input)
{
  const gchar *bad_xml = "<?xml version=\"1.0\"?><a><b></a>";

  /* malformed ident header */
  flow = push_data ("bad-ident-header", "CMML\0\0\0\0garbage", 15, 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  /* push invalid xml */
  flow = push_data ("bad-xml", bad_xml, strlen (bad_xml), 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  /* and now for something completely different: an empty buffer. This is valid
   * as 'NIL' EOS pages are allowed */
  flow = push_data ("empty-eos", NULL, 0, 0);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
}

GST_END_TEST;

GST_START_TEST (test_sink_query_convert)
{
  guint64 keyindex, keyoffset, granulepos;

  GstClockTime index_time, offset_time;

  GstFormat dstfmt = GST_FORMAT_TIME;

  gint64 dstval;

  /* send headers to set the granulerate */
  check_headers ();

  /* create a 1|1 granulepos */
  index_time = 1 * GST_SECOND;
  offset_time = 1 * GST_SECOND;

  keyindex = (index_time / granulerate) << granuleshift;
  keyoffset = offset_time / granulerate;
  granulepos = keyindex + keyoffset;

  fail_unless (gst_pad_query_convert (GST_PAD_PEER (srcpad),
          GST_FORMAT_DEFAULT, granulepos, &dstfmt, &dstval));

  fail_unless (dstfmt == GST_FORMAT_TIME);
  /* fail unless dstval == index + offset */
  fail_unless_equals_int (2 * GST_SECOND, dstval);
}

GST_END_TEST;

static Suite *
cmmldec_suite (void)
{
  Suite *s = suite_create ("cmmldec");

  TCase *tc_general = tcase_create ("general");

  suite_add_tcase (s, tc_general);
  tcase_add_checked_fixture (tc_general, setup_cmmldec, teardown_cmmldec);
  tcase_add_test (tc_general, test_dec);
  tcase_add_test (tc_general, test_tags);
  tcase_add_test (tc_general, test_preamble_no_pi);
  tcase_add_test (tc_general, test_wait_clip_end);
  tcase_add_test (tc_general, test_sink_query_convert);
  tcase_add_test (tc_general, test_weird_input);

  return s;
}

GST_CHECK_MAIN (cmmldec);
