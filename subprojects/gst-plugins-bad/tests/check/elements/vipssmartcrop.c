/* Unit tests for the vips-smartcrop element.
 *
 * A single frame of the deterministic "spokes" videotestsrc pattern is pushed
 * through the element in every "interesting" mode. The spokes pattern is
 * fully symmetric and its most salient point is the centre of the frame, so
 * the expected crop position of every mode is known exactly.
 *
 * The caps negotiation is also checked: the element must reject an output
 * size that is larger than the input, and it must pass the buffers through
 * unchanged when the downstream does not fix the output size.
 *
 * The allocation query is also checked: the zero-copy crop requires the
 * downstream to accept video meta, and the element must fall back to the
 * copying path when it does not.
 */

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video-info.h>
#include <string.h>

#define IN_WIDTH 640
#define IN_HEIGHT 480
#define OUT_WIDTH 320
#define OUT_HEIGHT 240
#define NUM_BANDS 3

typedef struct
{
  const gchar *mode;
  gint left, top;
  gint out_width, out_height;
  /* The zero-copy modes produce a view on the input buffer's memory. */
  gboolean zero_copy;
  /* -1: not checked. Only the attention mode reports a non-zero point. */
  gint attention_x, attention_y;
} ModeExpectation;

static const ModeExpectation expectations[] = {
  {"none", 0, 0, OUT_WIDTH, OUT_HEIGHT, TRUE, -1, -1},
  {"low", 0, 0, OUT_WIDTH, OUT_HEIGHT, TRUE, -1, -1},
  {"centre", (IN_WIDTH - OUT_WIDTH) / 2, (IN_HEIGHT - OUT_HEIGHT) / 2,
      OUT_WIDTH, OUT_HEIGHT, TRUE, -1, -1},
  {"high", IN_WIDTH - OUT_WIDTH, IN_HEIGHT - OUT_HEIGHT,
      OUT_WIDTH, OUT_HEIGHT, TRUE, -1, -1},
  {"entropy", (IN_WIDTH - OUT_WIDTH) / 2, (IN_HEIGHT - OUT_HEIGHT) / 2,
      OUT_WIDTH, OUT_HEIGHT, FALSE, -1, -1},
  {"attention", (IN_WIDTH - OUT_WIDTH) / 2, (IN_HEIGHT - OUT_HEIGHT) / 2,
      OUT_WIDTH, OUT_HEIGHT, TRUE, IN_WIDTH / 2, IN_HEIGHT / 2},
};

static GstBuffer *
pull_spokes_frame (void)
{
  GstHarness *h;
  GstCaps *caps;
  GstBuffer *buf = NULL;

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, IN_WIDTH, "height", G_TYPE_INT, IN_HEIGHT, NULL);

  h = gst_harness_new_parse ("videotestsrc num-buffers=1 pattern=spokes");
  gst_harness_set_sink_caps (h, caps);

  gst_harness_play (h);

  buf = gst_harness_pull (h);

  gst_harness_teardown (h);

  return buf;
}

static GstCaps *
get_caps_from_pad (GstPad * pad)
{
  GstEvent *event;
  GstCaps *caps = NULL;

  event = gst_pad_get_sticky_event (pad, GST_EVENT_CAPS, 0);
  if (event != NULL) {
    GstCaps *parsed;

    gst_event_parse_caps (event, &parsed);
    if (parsed != NULL)
      caps = gst_caps_ref (parsed);
    gst_event_unref (event);
  }

  return caps;
}

static void
get_video_info_from_buffer (GstVideoInfo * info, GstBuffer * buf,
    GstCaps * caps)
{
  GstVideoMeta *meta;

  g_assert (gst_video_info_from_caps (info, caps));
  meta = gst_buffer_get_video_meta (buf);
  if (meta != NULL)
    info->stride[0] = meta->stride[0];
}

static gboolean
test_mode (const ModeExpectation * exp, gboolean downstream_video_meta)
{
  GstBuffer *inbuf, *outbuf, *keep;
  GstHarness *h;
  GstCaps *in_caps, *sink_caps, *in_neg, *out_neg;
  GstVideoInfo in_info, out_info;
  GstVideoFrame in_frame, out_frame;
  guint y;
  gboolean ret = FALSE;

  inbuf = pull_spokes_frame ();
  fail_unless (inbuf != NULL, "failed to pull a frame from videotestsrc");

  in_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, IN_WIDTH, "height", G_TYPE_INT, IN_HEIGHT, NULL);

  sink_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, exp->out_width,
      "height", G_TYPE_INT, exp->out_height, NULL);

  h = gst_harness_new ("vips-smartcrop");
  fail_unless (h != NULL, "failed to create the vips-smartcrop element");

  if (downstream_video_meta) {
    /* The zero-copy crop is only possible when the downstream accepts
     * video meta. */
    gst_harness_add_propose_allocation_meta (h, GST_VIDEO_META_API_TYPE, NULL);
  }

  {
    GEnumClass *ec;
    GEnumValue *ev;

    ec = g_type_class_ref (g_type_from_name ("GstVipsInteresting"));
    ev = g_enum_get_value_by_nick (ec, exp->mode);
    g_assert (ev != NULL);
    g_object_set (GST_ELEMENT (h->element), "interesting", ev->value, NULL);
    g_type_class_unref (ec);
  }

  gst_harness_set_src_caps (h, in_caps);
  gst_harness_set_sink_caps (h, sink_caps);

  keep = gst_buffer_ref (inbuf);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL, "no output buffer for the %s mode", exp->mode);

  in_neg = get_caps_from_pad (h->srcpad);
  out_neg = get_caps_from_pad (h->sinkpad);
  fail_unless (in_neg != NULL && out_neg != NULL,
      "no negotiated caps for the %s mode", exp->mode);

  get_video_info_from_buffer (&in_info, keep, in_neg);
  get_video_info_from_buffer (&out_info, outbuf, out_neg);
  fail_unless_equals_uint64 (out_info.width, exp->out_width);
  fail_unless_equals_uint64 (out_info.height, exp->out_height);

  fail_unless (gst_video_frame_map (&in_frame, &in_info, keep, GST_MAP_READ));
  fail_unless (gst_video_frame_map (&out_frame, &out_info, outbuf,
          GST_MAP_READ));

  for (y = 0; y < out_info.height; y++) {
    const guint8 *in_row = (const guint8 *) in_frame.data[0]
        + (gsize) (exp->top + y) * GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, 0)
        + (gsize) exp->left * NUM_BANDS;
    const guint8 *out_row = (const guint8 *) out_frame.data[0]
        + (gsize) y * GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, 0);

    fail_unless (memcmp (out_row, in_row,
            (gsize) out_info.width * NUM_BANDS) == 0,
        "pixel mismatch in the %s mode at row %u", exp->mode, y);
  }

  if (exp->zero_copy) {
    /* The output buffer is a view on the input memory: its single memory
     * must start exactly at the expected offset of the input memory. */
    GstMemory *out_mem;
    GstMapInfo out_map;
    gsize offset;

    fail_unless_equals_uint64 (gst_buffer_n_memory (keep), 1);
    fail_unless_equals_uint64 (gst_buffer_n_memory (outbuf), 1);

    out_mem = gst_buffer_peek_memory (outbuf, 0);
    fail_unless (gst_memory_map (out_mem, &out_map, GST_MAP_READ));

    offset = (gsize) exp->top * GST_VIDEO_FRAME_PLANE_STRIDE (&in_frame, 0)
        + (gsize) exp->left * NUM_BANDS;

    fail_unless (out_map.data == (const guint8 *) in_frame.data[0] + offset,
        "the output of the %s mode does not share the input memory", exp->mode);

    gst_memory_unmap (out_mem, &out_map);
  } else {
    /* The copying path must produce a full frame laid out as the caps
     * describe; any video meta on it (e.g. from the videofilter's buffer
     * pool) must agree with the caps, so that the buffer is also usable
     * downstream without video meta support. */
    GstVideoInfo caps_info;
    GstVideoMeta *meta;

    g_assert (gst_video_info_from_caps (&caps_info, out_neg));

    fail_unless (gst_buffer_get_size (outbuf) ==
        GST_VIDEO_INFO_SIZE (&caps_info),
        "the output of the %s mode is not a full default-stride frame",
        exp->mode);

    meta = gst_buffer_get_video_meta (outbuf);
    if (meta != NULL) {
      fail_unless (meta->offset[0] == GST_VIDEO_INFO_PLANE_OFFSET (&caps_info,
              0), "the video meta of the %s mode output has a non-zero offset",
          exp->mode);
      fail_unless_equals_int (meta->stride[0],
          GST_VIDEO_INFO_PLANE_STRIDE (&caps_info, 0));
    }
  }

  if (exp->attention_x >= 0) {
    gint attention_x, attention_y;

    g_object_get (h->element,
        "attention-x", &attention_x, "attention-y", &attention_y, NULL);
    fail_unless_equals_int (attention_x, exp->attention_x);
    fail_unless_equals_int (attention_y, exp->attention_y);
  }

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);
  gst_buffer_unref (outbuf);
  gst_buffer_unref (keep);
  gst_caps_unref (in_neg);
  gst_caps_unref (out_neg);
  gst_harness_teardown (h);

  ret = TRUE;

  return ret;
}

GST_START_TEST (test_none)
{
  fail_unless (test_mode (&expectations[0], TRUE));
}

GST_END_TEST;

GST_START_TEST (test_low)
{
  fail_unless (test_mode (&expectations[1], TRUE));
}

GST_END_TEST;

GST_START_TEST (test_centre)
{
  fail_unless (test_mode (&expectations[2], TRUE));
}

GST_END_TEST;

GST_START_TEST (test_high)
{
  fail_unless (test_mode (&expectations[3], TRUE));
}

GST_END_TEST;

GST_START_TEST (test_entropy)
{
  fail_unless (test_mode (&expectations[4], TRUE));
}

GST_END_TEST;

GST_START_TEST (test_attention)
{
  fail_unless (test_mode (&expectations[5], TRUE));
}

GST_END_TEST;

/* Without video meta support downstream the zero-copy crop must not be
 * used: the crop is copied into a plain buffer with the default stride. */
GST_START_TEST (test_no_downstream_video_meta)
{
  ModeExpectation exp = expectations[2];        /* centre */

  exp.zero_copy = FALSE;
  fail_unless (test_mode (&exp, FALSE));
}

GST_END_TEST;

/* An output size larger than the input size must not negotiate. */
GST_START_TEST (test_output_larger_not_negotiated)
{
  GstBuffer *inbuf, *keep;
  GstHarness *h;
  GstCaps *in_caps, *sink_caps;

  inbuf = pull_spokes_frame ();
  fail_unless (inbuf != NULL, "failed to pull a frame from videotestsrc");

  in_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, IN_WIDTH, "height", G_TYPE_INT, IN_HEIGHT, NULL);

  sink_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, IN_WIDTH * 2, "height", G_TYPE_INT, IN_HEIGHT * 2,
      NULL);

  h = gst_harness_new ("vips-smartcrop");
  fail_unless (h != NULL, "failed to create the vips-smartcrop element");

  gst_harness_set_src_caps (h, in_caps);
  gst_harness_set_sink_caps (h, sink_caps);

  keep = gst_buffer_ref (inbuf);
  fail_unless_equals_int (gst_harness_push (h, inbuf), GST_FLOW_NOT_NEGOTIATED);
  fail_unless (gst_harness_try_pull (h) == NULL, "no buffer may be emitted");

  gst_buffer_unref (keep);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* When the downstream caps do not fix the size, the element negotiates the
 * input size on the output and passes the buffer through unchanged. */
GST_START_TEST (test_output_unfixed_passthrough)
{
  GstBuffer *inbuf, *outbuf, *keep;
  GstHarness *h;
  GstCaps *in_caps, *sink_caps, *in_neg, *out_neg;

  inbuf = pull_spokes_frame ();
  fail_unless (inbuf != NULL, "failed to pull a frame from videotestsrc");

  in_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "RGB",
      "width", G_TYPE_INT, IN_WIDTH, "height", G_TYPE_INT, IN_HEIGHT, NULL);

  sink_caps = gst_caps_from_string ("video/x-raw,format=RGB,"
      "width=[1, 1280],height=[1, 960]");

  h = gst_harness_new ("vips-smartcrop");
  fail_unless (h != NULL, "failed to create the vips-smartcrop element");

  gst_harness_set_src_caps (h, in_caps);
  gst_harness_set_sink_caps (h, sink_caps);

  keep = gst_buffer_ref (inbuf);
  outbuf = gst_harness_push_and_pull (h, inbuf);
  fail_unless (outbuf != NULL, "no output buffer");

  in_neg = get_caps_from_pad (h->srcpad);
  out_neg = get_caps_from_pad (h->sinkpad);
  fail_unless (in_neg != NULL && out_neg != NULL, "no negotiated caps");
  fail_unless (gst_caps_is_equal (in_neg, out_neg),
      "the output caps were not negotiated to the input caps (%" GST_PTR_FORMAT
      " vs %" GST_PTR_FORMAT ")", in_neg, out_neg);

  /* In passthrough the output buffer is the input buffer itself. */
  fail_unless (outbuf == keep,
      "the output buffer does not share the input memory");

  gst_buffer_unref (outbuf);
  gst_buffer_unref (keep);
  gst_caps_unref (in_neg);
  gst_caps_unref (out_neg);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
vipssmartcrop_suite (void)
{
  Suite *s = suite_create ("vips-smartcrop");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, test_none);
  tcase_add_test (tc, test_low);
  tcase_add_test (tc, test_centre);
  tcase_add_test (tc, test_high);
  tcase_add_test (tc, test_entropy);
  tcase_add_test (tc, test_attention);
  tcase_add_test (tc, test_no_downstream_video_meta);
  tcase_add_test (tc, test_output_larger_not_negotiated);
  tcase_add_test (tc, test_output_unfixed_passthrough);

  return s;
}

GST_CHECK_MAIN (vipssmartcrop);
