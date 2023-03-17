/* GStreamer
 *
 * unit test for x264enc
 *
 * Copyright (C) <2008> Mark Nauwelaerts <mnauw@users.sf.net>
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

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define VIDEO_CAPS_STRING "video/x-raw, " \
                           "width = (int) 384, " \
                           "height = (int) 288, " \
                           "framerate = (fraction) 25/1"

#define H264_CAPS_STRING "video/x-h264, " \
                           "width = (int) 384, " \
                           "height = (int) 288, " \
                           "framerate = (fraction) 25/1"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING));

static void cleanup_x264enc (GstElement * x264enc);

static GstElement *
setup_x264enc (const gchar * profile, const gchar * stream_format,
    GstVideoFormat input_format)
{
  GstPadTemplate *sink_tmpl, *tmpl;
  GstElement *x264enc;
  GstCaps *caps, *tmpl_caps;

  GST_DEBUG ("setup_x264enc");

  caps = gst_caps_from_string (H264_CAPS_STRING);
  gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile,
      "stream-format", G_TYPE_STRING, stream_format, NULL);
  sink_tmpl = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_caps_unref (caps);

  x264enc = gst_check_setup_element ("x264enc");
  mysrcpad = gst_check_setup_src_pad (x264enc, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad_from_template (x264enc, sink_tmpl);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_caps_set_simple (caps, "format", G_TYPE_STRING,
      gst_video_format_to_string (input_format), NULL);

  tmpl = gst_element_get_pad_template (x264enc, "sink");
  tmpl_caps = gst_pad_template_get_caps (tmpl);

  if (gst_caps_can_intersect (caps, tmpl_caps)) {
    gst_check_setup_events (mysrcpad, x264enc, caps, GST_FORMAT_TIME);
  } else {
    cleanup_x264enc (x264enc);
    x264enc = NULL;
  }

  gst_caps_unref (tmpl_caps);
  gst_caps_unref (caps);
  gst_object_unref (sink_tmpl);

  return x264enc;
}

static void
cleanup_x264enc (GstElement * x264enc)
{
  GST_DEBUG ("cleanup_x264enc");
  gst_element_set_state (x264enc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (x264enc);
  gst_check_teardown_sink_pad (x264enc);
  gst_check_teardown_element (x264enc);
}

static void
check_caps (GstCaps * caps, const gchar * profile, gint profile_id)
{
  GstStructure *s;
  const GValue *sf, *avcc, *pf;
  const gchar *stream_format;
  const gchar *caps_profile;

  fail_unless (caps != NULL);

  GST_INFO ("caps %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (s != NULL);
  fail_if (!gst_structure_has_name (s, "video/x-h264"));
  sf = gst_structure_get_value (s, "stream-format");
  fail_unless (sf != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (sf));
  stream_format = g_value_get_string (sf);
  fail_unless (stream_format != NULL);
  if (strcmp (stream_format, "avc") == 0) {
    GstMapInfo map;
    GstBuffer *buf;

    avcc = gst_structure_get_value (s, "codec_data");
    fail_unless (avcc != NULL);
    fail_unless (GST_VALUE_HOLDS_BUFFER (avcc));
    buf = gst_value_get_buffer (avcc);
    fail_unless (buf != NULL);
    gst_buffer_map (buf, &map, GST_MAP_READ);
    fail_unless_equals_int (map.data[0], 1);
    fail_unless (map.data[1] == profile_id,
        "Expected profile ID %#04x, got %#04x", profile_id, map.data[1]);
    gst_buffer_unmap (buf, &map);
  } else if (strcmp (stream_format, "byte-stream") == 0) {
    fail_if (gst_structure_get_value (s, "codec_data") != NULL);
  } else {
    fail_if (TRUE, "unexpected stream-format in caps: %s", stream_format);
  }

  pf = gst_structure_get_value (s, "profile");
  fail_unless (pf != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (pf));
  caps_profile = g_value_get_string (pf);
  fail_unless (caps_profile != NULL);
  fail_unless (!strcmp (caps_profile, profile));
}

static const GstVideoFormat formats_420_8[] =
    { GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_NV12,
  GST_VIDEO_FORMAT_UNKNOWN
};

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
static const GstVideoFormat formats_420_10[] =
    { GST_VIDEO_FORMAT_I420_10LE, GST_VIDEO_FORMAT_UNKNOWN };
static const GstVideoFormat formats_422[] =
    { GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_I422_10LE,
  GST_VIDEO_FORMAT_UNKNOWN
};

static const GstVideoFormat formats_444[] =
    { GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_Y444_10LE,
  GST_VIDEO_FORMAT_UNKNOWN
};
#else
static const GstVideoFormat formats_420_10[] =
    { GST_VIDEO_FORMAT_I420_10BE, GST_VIDEO_FORMAT_UNKNOWN };
static const GstVideoFormat formats_422[] =
    { GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_I422_10BE,
  GST_VIDEO_FORMAT_UNKNOWN
};

static const GstVideoFormat formats_444[] =
    { GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_Y444_10BE,
  GST_VIDEO_FORMAT_UNKNOWN
};
#endif

static void
test_video_profile (const gchar * profile, gint profile_id,
    const GstVideoFormat input_formats[], gint input_format_index)
{
  GstVideoFormat input_format = input_formats[input_format_index];
  GstElement *x264enc;
  GstBuffer *inbuffer, *outbuffer;
  int i, num_buffers;
  GstVideoInfo vinfo;
  GstCaps *caps;

  fail_unless (gst_video_info_set_format (&vinfo, input_format, 384, 288));

  x264enc = setup_x264enc (profile, "avc", input_format);
  if (x264enc == NULL) {
    g_printerr ("WARNING: input format '%s' not supported\n",
        gst_video_format_to_string (input_format));
    return;
  }

  fail_unless (gst_element_set_state (x264enc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* check that we only accept input formats compatible with the output caps */
  caps = gst_pad_peer_query_caps (mysrcpad, NULL);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    const GValue *v, *vi;
    guint vlen, j = 0;

    v = gst_structure_get_value (s, "format");

    if (G_VALUE_TYPE (v) == G_TYPE_STRING) {
      vlen = 1;
      vi = v;
    } else if (G_VALUE_TYPE (v) == GST_TYPE_LIST) {
      vlen = gst_value_list_get_size (v);
      fail_unless (vlen > 0, "Got empty format list");
      vi = gst_value_list_get_value (v, 0);
    } else {
      fail ("Bad format in structure: %" GST_PTR_FORMAT, s);
      g_assert_not_reached ();
    }

    while (TRUE) {
      const gchar *str = g_value_get_string (vi);
      GstVideoFormat format = gst_video_format_from_string (str);
      int k;

      for (k = 0;; k++) {
        fail_unless (input_formats[k] != GST_VIDEO_FORMAT_UNKNOWN,
            "Bad format: %s", str);
        if (input_formats[k] == format)
          break;
      }

      if (++j < vlen)
        vi = gst_value_list_get_value (v, j);
      else
        break;
    }
  }
  gst_caps_unref (caps);

  /* corresponds to buffer for the size mentioned in the caps */
  inbuffer = gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (&vinfo));

  /* makes valgrind's memcheck happier */
  gst_buffer_memset (inbuffer, 0, 0, -1);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);

  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers == 1);

  /* check output caps */
  {
    GstCaps *outcaps;

    outcaps = gst_pad_get_current_caps (mysinkpad);
    check_caps (outcaps, profile, profile_id);
    gst_caps_unref (outcaps);
  }

  /* validate buffers */
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    switch (i) {
      case 0:
      {
        gint nsize, npos, type, next_type;
        GstMapInfo map;
        const guint8 *data;
        gsize size;

        gst_buffer_map (outbuffer, &map, GST_MAP_READ);
        data = map.data;
        size = map.size;

        npos = 0;
        /* need SPS first */
        next_type = 7;
        /* loop through NALs */
        while (npos < size) {
          fail_unless (size - npos >= 4);
          nsize = GST_READ_UINT32_BE (data + npos);
          fail_unless (nsize > 0);
          fail_unless (npos + 4 + nsize <= size);
          type = data[npos + 4] & 0x1F;
          /* check the first NALs, disregard AU (9), SEI (6) */
          if (type != 9 && type != 6) {
            fail_unless (type == next_type);
            switch (type) {
              case 7:
                /* SPS */
                next_type = 8;
                break;
              case 8:
                /* PPS */
                next_type = 5;
                break;
              default:
                break;
            }
          }
          npos += nsize + 4;
        }
        gst_buffer_unmap (outbuffer, &map);
        /* should have reached the exact end */
        fail_unless (npos == size);
        break;
      }
      default:
        break;
    }

    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_x264enc (x264enc);
  g_list_free (buffers);
  buffers = NULL;
}

GST_START_TEST (test_video_baseline)
{
  gint i;

  for (i = 0; formats_420_8[i] != GST_VIDEO_FORMAT_UNKNOWN; i++)
    test_video_profile ("constrained-baseline", 0x42, formats_420_8, i);
}

GST_END_TEST;

GST_START_TEST (test_video_main)
{
  gint i;

  for (i = 0; formats_420_8[i] != GST_VIDEO_FORMAT_UNKNOWN; i++)
    test_video_profile ("main", 0x4d, formats_420_8, i);
}

GST_END_TEST;

GST_START_TEST (test_video_high)
{
  gint i;

  for (i = 0; formats_420_8[i] != GST_VIDEO_FORMAT_UNKNOWN; i++)
    test_video_profile ("high", 0x64, formats_420_8, i);
}

GST_END_TEST;

GST_START_TEST (test_video_high10)
{
  gint i;

  for (i = 0; formats_420_10[i] != GST_VIDEO_FORMAT_UNKNOWN; i++)
    test_video_profile ("high-10", 0x6e, formats_420_10, i);
}

GST_END_TEST;

GST_START_TEST (test_video_high422)
{
  gint i;

  for (i = 0; formats_422[i] != GST_VIDEO_FORMAT_UNKNOWN; i++)
    test_video_profile ("high-4:2:2", 0x7A, formats_422, i);
}

GST_END_TEST;

GST_START_TEST (test_video_high444)
{
  gint i;

  for (i = 0; formats_444[i] != GST_VIDEO_FORMAT_UNKNOWN; i++)
    test_video_profile ("high-4:4:4", 0xF4, formats_444, i);
}

GST_END_TEST;

Suite *
x264enc_suite (void)
{
  Suite *s = suite_create ("x264enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_video_baseline);
  tcase_add_test (tc_chain, test_video_main);
  tcase_add_test (tc_chain, test_video_high);
  tcase_add_test (tc_chain, test_video_high10);
  tcase_add_test (tc_chain, test_video_high422);
  tcase_add_test (tc_chain, test_video_high444);

  return s;
}

GST_CHECK_MAIN (x264enc);
