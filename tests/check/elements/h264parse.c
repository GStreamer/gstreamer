/*
 * GStreamer
 *
 * unit test for h264parse
 *
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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
#include "parser.h"

#define SRC_CAPS_TMPL   "video/x-h264, parsed=(boolean)false"
#define SINK_CAPS_TMPL  "video/x-h264, parsed=(boolean)true"

GstStaticPadTemplate sinktemplate_bs_nal = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL
        ", stream-format = (string) byte-stream, alignment = (string) nal")
    );

GstStaticPadTemplate sinktemplate_bs_au = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL
        ", stream-format = (string) byte-stream, alignment = (string) au")
    );

GstStaticPadTemplate sinktemplate_avc_au = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL
        ", stream-format = (string) avc, alignment = (string) au")
    );

GstStaticPadTemplate sinktemplate_avc3_au = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL
        ", stream-format = (string) avc3, alignment = (string) au")
    );

GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_TMPL)
    );

/* some data */

/* AUD */
static guint8 h264_aud[] = {
  0x00, 0x00, 0x00, 0x01, 0x09, 0xf0
};


/* SPS */
static guint8 h264_sps[] = {
  0x00, 0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x15,
  0xec, 0xa4, 0xbf, 0x2e, 0x02, 0x20, 0x00, 0x00,
  0x03, 0x00, 0x2e, 0xe6, 0xb2, 0x80, 0x01, 0xe2,
  0xc5, 0xb2, 0xc0
};

/* PPS */
static guint8 h264_pps[] = {
  0x00, 0x00, 0x00, 0x01, 0x68, 0xeb, 0xec, 0xb2
};

/* SEI buffering_period() message */
static guint8 h264_sei_buffering_period[] = {
  0x00, 0x00, 0x00, 0x01, 0x06, 0x00, 0x01, 0xc0
};

/* combines to this codec-data */
static guint8 h264_avc_codec_data[] = {
  0x01, 0x4d, 0x40, 0x15, 0xff, 0xe1, 0x00, 0x17,
  0x67, 0x4d, 0x40, 0x15, 0xec, 0xa4, 0xbf, 0x2e,
  0x02, 0x20, 0x00, 0x00, 0x03, 0x00, 0x2e, 0xe6,
  0xb2, 0x80, 0x01, 0xe2, 0xc5, 0xb2, 0xc0, 0x01,
  0x00, 0x04, 0x68, 0xeb, 0xec, 0xb2
};

/* codec-data for avc3 where there are no SPS/PPS in the codec_data */
static guint8 h264_avc3_codec_data[] = {
  0x01,                         /* config version, always == 1 */
  0x4d,                         /* profile */
  0x40,                         /* profile compatibility */
  0x15, 0xff,                   /* 6 reserved bits, lengthSizeMinusOne */
  0xe0,                         /* 3 reserved bits, numSPS */
  0x00                          /* numPPS */
};

static guint8 *h264_codec_data = NULL;
static guint8 h264_codec_data_size = 0;


/* keyframes all around */
static guint8 h264_idrframe[] = {
  0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00,
  0x10, 0xff, 0xfe, 0xf6, 0xf0, 0xfe, 0x05, 0x36,
  0x56, 0x04, 0x50, 0x96, 0x7b, 0x3f, 0x53, 0xe1
};

/* truncated nal */
static guint8 garbage_frame[] = {
  0x00, 0x00, 0x00, 0x01, 0x05
};

/* context to tweak tests */
static const gchar *ctx_suite;
static gboolean ctx_codec_data;

static gboolean
verify_buffer (buffer_verify_data_s * vdata, GstBuffer * buffer)
{
  if (vdata->discard) {
    /* check separate header NALs */
    gint i = vdata->buffer_counter;
    guint ofs;
    gboolean aud;

    /* SEI with start code prefix with 2 0-bytes */
    ofs = i == 2;
    aud = i == 0;
    fail_unless (i <= 3);

    if (aud) {
      fail_unless (gst_buffer_get_size (buffer) == sizeof (h264_aud));
      fail_unless (gst_buffer_memcmp (buffer, 0, h264_aud,
              gst_buffer_get_size (buffer)) == 0);
      vdata->discard++;
    } else {
      i -= 1;

      fail_unless (gst_buffer_get_size (buffer) == ctx_headers[i].size - ofs);
      fail_unless (gst_buffer_memcmp (buffer, 0, ctx_headers[i].data + ofs,
              gst_buffer_get_size (buffer)) == 0);
    }
  } else {
    GstMapInfo map;

    gst_buffer_map (buffer, &map, GST_MAP_READ);
    fail_unless (map.size > 4);
    /* only need to check avc and bs-to-nal output case */
    if (GST_READ_UINT24_BE (map.data) == 0x01) {
      /* in bs-to-nal, a leading 0x00 is stripped from output */
      fail_unless (gst_buffer_get_size (buffer) ==
          vdata->data_to_verify_size - 1);
      fail_unless (gst_buffer_memcmp (buffer, 0, vdata->data_to_verify + 1,
              vdata->data_to_verify_size - 1) == 0);
      gst_buffer_unmap (buffer, &map);
      return TRUE;
    } else if (GST_READ_UINT32_BE (map.data) == 0x01) {
      gboolean aud = FALSE;
      aud = vdata->buffer_counter % 2;
      if (aud) {
        fail_unless (gst_buffer_get_size (buffer) == sizeof (h264_aud));
        fail_unless (gst_buffer_memcmp (buffer, 0, h264_aud,
                gst_buffer_get_size (buffer)) == 0);
        gst_buffer_unmap (buffer, &map);
        return TRUE;
      }

      /* this is not avc, use default tests from parser.c */
      gst_buffer_unmap (buffer, &map);
      return FALSE;
    }
    /* header is merged in initial frame */
    if (vdata->buffer_counter == 0) {
      guint8 *data = map.data;

      fail_unless (map.size == vdata->data_to_verify_size +
          ctx_headers[0].size + ctx_headers[1].size + ctx_headers[2].size);
      fail_unless (GST_READ_UINT32_BE (data) == ctx_headers[0].size - 4);
      fail_unless (memcmp (data + 4, ctx_headers[0].data + 4,
              ctx_headers[0].size - 4) == 0);
      data += ctx_headers[0].size;
      fail_unless (GST_READ_UINT32_BE (data) == ctx_headers[1].size - 4);
      fail_unless (memcmp (data + 4, ctx_headers[1].data + 4,
              ctx_headers[1].size - 4) == 0);
      data += ctx_headers[1].size;
      fail_unless (GST_READ_UINT32_BE (data) == ctx_headers[2].size - 4);
      fail_unless (memcmp (data + 4, ctx_headers[2].data + 4,
              ctx_headers[2].size - 4) == 0);
      data += ctx_headers[2].size;
      fail_unless (GST_READ_UINT32_BE (data) == vdata->data_to_verify_size - 4);
      fail_unless (memcmp (data + 4, vdata->data_to_verify + 4,
              vdata->data_to_verify_size - 4) == 0);
    } else {
      fail_unless (GST_READ_UINT32_BE (map.data) == map.size - 4);
      fail_unless (map.size == vdata->data_to_verify_size);
      fail_unless (memcmp (map.data + 4, vdata->data_to_verify + 4,
              map.size - 4) == 0);
    }
    gst_buffer_unmap (buffer, &map);
    return TRUE;
  }

  return FALSE;
}

/* A single access unit comprising of SPS, SEI, PPS and IDR frame */
static gboolean
verify_buffer_bs_au (buffer_verify_data_s * vdata, GstBuffer * buffer)
{
  GstMapInfo map;

  fail_unless (ctx_sink_template == &sinktemplate_bs_au);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  fail_unless (map.size > 4);

  if (vdata->buffer_counter == 0) {
    guint8 *data = map.data;

    /* AUD, SPS, SEI, PPS */
    fail_unless (map.size == vdata->data_to_verify_size +
        sizeof (h264_aud) + ctx_headers[0].size +
        ctx_headers[1].size + ctx_headers[2].size);
    fail_unless (memcmp (data, h264_aud, sizeof (h264_aud)) == 0);
    data += sizeof (h264_aud);
    fail_unless (memcmp (data, ctx_headers[0].data, ctx_headers[0].size) == 0);
    data += ctx_headers[0].size;
    fail_unless (memcmp (data, ctx_headers[1].data, ctx_headers[1].size) == 0);
    data += ctx_headers[1].size;
    fail_unless (memcmp (data, ctx_headers[2].data, ctx_headers[2].size) == 0);
    data += ctx_headers[2].size;

    /* IDR frame */
    fail_unless (memcmp (data, vdata->data_to_verify,
            vdata->data_to_verify_size) == 0);
  } else {
    /* IDR frame */
    guint aud_size = sizeof (h264_aud);
    fail_unless (map.size == vdata->data_to_verify_size + aud_size);
    fail_unless (memcmp (map.data, h264_aud, aud_size) == 0);
    fail_unless (memcmp (map.data + aud_size, vdata->data_to_verify,
            map.size - aud_size) == 0);
  }

  gst_buffer_unmap (buffer, &map);
  return TRUE;
}

GST_START_TEST (test_parse_normal)
{
  gst_parser_test_normal (h264_idrframe, sizeof (h264_idrframe));
}

GST_END_TEST;


GST_START_TEST (test_parse_drain_single)
{
  gst_parser_test_drain_single (h264_idrframe, sizeof (h264_idrframe));
}

GST_END_TEST;


GST_START_TEST (test_parse_drain_garbage)
{
  gst_parser_test_drain_garbage (h264_idrframe, sizeof (h264_idrframe),
      garbage_frame, sizeof (garbage_frame));
}

GST_END_TEST
GST_START_TEST (test_parse_split)
{
  gst_parser_test_split (h264_idrframe, sizeof (h264_idrframe));
}

GST_END_TEST;


GST_START_TEST (test_parse_skip_garbage)
{
  gst_parser_test_skip_garbage (h264_idrframe, sizeof (h264_idrframe),
      garbage_frame, sizeof (garbage_frame));
}

GST_END_TEST;

#define structure_get_int(s,f) \
    (g_value_get_int(gst_structure_get_value(s,f)))
#define fail_unless_structure_field_int_equals(s,field,num) \
    fail_unless_equals_int (structure_get_int(s,field), num)


GST_START_TEST (test_parse_detect_stream)
{
  GstCaps *caps;
  GstStructure *s;
  GstBuffer *buf;
  const GValue *val;

  /* parser does not really care that mpeg1 and mpeg2 frame data
   * should be a bit different */
  caps = gst_parser_test_get_output_caps (h264_idrframe, sizeof (h264_idrframe),
      NULL);
  fail_unless (caps != NULL);

  /* Check that the negotiated caps are as expected */
  /* When codec_data is present, parser assumes that data is version 4 */
  GST_LOG ("h264 output caps: %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_has_name (s, "video/x-h264"));
  fail_unless_structure_field_int_equals (s, "width", 32);
  fail_unless_structure_field_int_equals (s, "height", 24);

  if (ctx_codec_data) {
    fail_unless (gst_structure_has_field (s, "codec_data"));

    /* check codec-data in more detail */
    val = gst_structure_get_value (s, "codec_data");
    fail_unless (val != NULL);
    buf = gst_value_get_buffer (val);
    fail_unless (buf != NULL);
    fail_unless (gst_buffer_get_size (buf) == h264_codec_data_size);
    fail_unless (gst_buffer_memcmp (buf, 0, h264_codec_data,
            gst_buffer_get_size (buf)) == 0);
  }

  gst_caps_unref (caps);
}

GST_END_TEST;


static Suite *
h264parse_suite (void)
{
  Suite *s = suite_create (ctx_suite);
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_normal);
  tcase_add_test (tc_chain, test_parse_drain_single);
  tcase_add_test (tc_chain, test_parse_drain_garbage);
  tcase_add_test (tc_chain, test_parse_split);
  tcase_add_test (tc_chain, test_parse_skip_garbage);
  tcase_add_test (tc_chain, test_parse_detect_stream);

  return s;
}

static gboolean
verify_buffer_packetized (buffer_verify_data_s * vdata, GstBuffer * buffer)
{
  GstMapInfo map;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  fail_unless (map.size > 4);
  fail_unless (GST_READ_UINT32_BE (map.data) == 0x01);
  if (vdata->discard) {
    /* check separate header NALs */
    guint8 *data;
    gint size;

    if (vdata->buffer_counter == 0) {
      data = h264_aud;
      size = sizeof (h264_aud);
      vdata->discard++;
    } else if (vdata->buffer_counter == 1) {
      data = h264_sps;
      size = sizeof (h264_sps);
    } else {
      data = h264_pps;
      size = sizeof (h264_pps);
    }

    fail_unless (map.size == size);
    fail_unless (memcmp (map.data + 4, data + 4, size - 4) == 0);
  } else {
    guint8 *data;
    gint size;
    gboolean aud = vdata->buffer_counter % 2;
    if (aud) {
      data = h264_aud;
      size = sizeof (h264_aud);
    } else {
      data = (gpointer) vdata->data_to_verify;
      size = map.size;
    }

    fail_unless (map.size == size);
    fail_unless (memcmp (map.data + 4, data + 4, size - 4) == 0);
  }
  gst_buffer_unmap (buffer, &map);

  return TRUE;
}

GST_START_TEST (test_parse_packetized)
{
  guint8 *frame;
  GstCaps *caps;
  GstBuffer *cdata;
  GstStructure *s;
  gchar *desc;

  /* make AVC frame */
  frame = g_malloc (sizeof (h264_idrframe));
  GST_WRITE_UINT32_BE (frame, sizeof (h264_idrframe) - 4);
  memcpy (frame + 4, h264_idrframe + 4, sizeof (h264_idrframe) - 4);

  /* some caps messing */
  caps = gst_caps_from_string (SRC_CAPS_TMPL);
  cdata =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, h264_codec_data,
      h264_codec_data_size, 0, h264_codec_data_size, NULL, NULL);
  gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, cdata,
      "stream-format", G_TYPE_STRING, "avc", NULL);
  gst_buffer_unref (cdata);
  desc = gst_caps_to_string (caps);
  gst_caps_unref (caps);

  caps = gst_parser_test_get_output_caps (frame, sizeof (h264_idrframe), desc);
  g_free (desc);
  g_free (frame);

  /* minor caps checks */
  GST_LOG ("h264 output caps: %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_has_name (s, "video/x-h264"));
  fail_unless_structure_field_int_equals (s, "width", 32);
  fail_unless_structure_field_int_equals (s, "height", 24);

  gst_caps_unref (caps);
}

GST_END_TEST;

static Suite *
h264parse_packetized_suite (void)
{
  Suite *s = suite_create (ctx_suite);
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_packetized);

  return s;
}


/*
 * TODO:
 *   - Both push- and pull-modes need to be tested
 *      * Pull-mode & EOS
 */

int
main (int argc, char **argv)
{
  int nf = 0;

  Suite *s;
  SRunner *sr;

  gst_check_init (&argc, &argv);

  /* globabl init test context */
  ctx_factory = "h264parse";
  ctx_sink_template = &sinktemplate_bs_nal;
  ctx_src_template = &srctemplate;
  ctx_headers[0].data = h264_sps;
  ctx_headers[0].size = sizeof (h264_sps);
  ctx_headers[1].data = h264_sei_buffering_period;
  ctx_headers[1].size = sizeof (h264_sei_buffering_period);
  ctx_headers[2].data = h264_pps;
  ctx_headers[2].size = sizeof (h264_pps);
  ctx_verify_buffer = verify_buffer;
  ctx_frame_generated = TRUE;
  /* discard initial sps/pps buffers */
  ctx_discard = 3;
  /* no timing info to parse */
  ctx_no_metadata = TRUE;
  ctx_codec_data = FALSE;

  h264_codec_data = h264_avc_codec_data;
  h264_codec_data_size = sizeof (h264_avc_codec_data);

  ctx_suite = "h264parse_to_bs_nal";
  s = h264parse_suite ();
  nf += gst_check_run_suite (s, ctx_suite, __FILE__ "_to_bs_nal.c");

  /* setup and tweak to handle bs au output */
  ctx_suite = "h264parse_to_bs_au";
  ctx_sink_template = &sinktemplate_bs_au;
  ctx_verify_buffer = verify_buffer_bs_au;
  ctx_discard = 0;
  ctx_frame_generated = FALSE;

  s = h264parse_suite ();
  nf += gst_check_run_suite (s, ctx_suite, __FILE__ "_to_bs_au.c");

  /* setup and tweak to handle avc au output */
  ctx_suite = "h264parse_to_avc_au";
  ctx_sink_template = &sinktemplate_avc_au;
  ctx_verify_buffer = verify_buffer;
  ctx_discard = 0;
  ctx_codec_data = TRUE;

  s = h264parse_suite ();
  sr = srunner_create (s);
  srunner_run_all (sr, CK_NORMAL);
  nf += srunner_ntests_failed (sr);
  srunner_free (sr);

  /* setup and tweak to handle avc3 au output */
  h264_codec_data = h264_avc3_codec_data;
  h264_codec_data_size = sizeof (h264_avc3_codec_data);
  ctx_suite = "h264parse_to_avc3_au";
  ctx_sink_template = &sinktemplate_avc3_au;
  ctx_discard = 0;
  ctx_codec_data = TRUE;

  s = h264parse_suite ();
  nf += gst_check_run_suite (s, ctx_suite, __FILE__ "_to_avc3_au.c");

  /* setup and tweak to handle avc packetized input */
  h264_codec_data = h264_avc_codec_data;
  h264_codec_data_size = sizeof (h264_avc_codec_data);
  ctx_suite = "h264parse_packetized";
  /* turn into separate byte stream NALs */
  ctx_sink_template = &sinktemplate_bs_nal;
  /* and ignore inserted codec-data NALs */
  ctx_discard = 2;
  ctx_frame_generated = TRUE;
  /* no more config headers */
  ctx_headers[0].data = NULL;
  ctx_headers[1].data = NULL;
  ctx_headers[2].data = NULL;
  ctx_headers[0].size = 0;
  ctx_headers[1].size = 0;
  ctx_headers[2].size = 0;
  /* and need adapter buffer check */
  ctx_verify_buffer = verify_buffer_packetized;

  s = h264parse_packetized_suite ();
  nf += gst_check_run_suite (s, ctx_suite, __FILE__ "_packetized.c");

  return nf;
}
