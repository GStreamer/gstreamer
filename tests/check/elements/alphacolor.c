/* GStreamer unit test for the alphacolor element
 *
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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
#include <gst/video/video.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV"))
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_RGB)
    );

static GstElement *
setup_alphacolor (void)
{
  GstElement *alphacolor;

  alphacolor = gst_check_setup_element ("alphacolor");
  mysrcpad = gst_check_setup_src_pad (alphacolor, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (alphacolor, &sinktemplate, NULL);

  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return alphacolor;
}

static void
cleanup_alphacolor (GstElement * alphacolor)
{
  GST_DEBUG ("cleaning up");

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (alphacolor);
  gst_check_teardown_sink_pad (alphacolor);
  gst_check_teardown_element (alphacolor);
}

#define WIDTH 3
#define HEIGHT 4

static GstCaps *
create_caps_rgb24 (void)
{
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "width", G_TYPE_INT, 3,
      "height", G_TYPE_INT, 4,
      "bpp", G_TYPE_INT, 24,
      "depth", G_TYPE_INT, 24,
      "framerate", GST_TYPE_FRACTION, 0, 1,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "red_mask", G_TYPE_INT, 0x00ff0000,
      "green_mask", G_TYPE_INT, 0x0000ff00,
      "blue_mask", G_TYPE_INT, 0x000000ff, NULL);

  return caps;
}

static GstCaps *
create_caps_rgba32 (void)
{
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "width", G_TYPE_INT, 3,
      "height", G_TYPE_INT, 4,
      "bpp", G_TYPE_INT, 32,
      "depth", G_TYPE_INT, 32,
      "framerate", GST_TYPE_FRACTION, 0, 1,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "red_mask", G_TYPE_INT, 0xff000000,
      "green_mask", G_TYPE_INT, 0x00ff0000,
      "blue_mask", G_TYPE_INT, 0x0000ff00,
      "alpha_mask", G_TYPE_INT, 0x000000ff, NULL);

  return caps;
}

static GstBuffer *
create_buffer_rgb24_3x4 (void)
{
  /* stride is rounded up to multiple of 4, so 3 bytes padding for each row */
  const guint8 rgb24_3x4_img[HEIGHT * GST_ROUND_UP_4 (WIDTH * 3)] = {
    0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
    0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00
  };
  guint rowstride = GST_ROUND_UP_4 (WIDTH * 3);
  GstBuffer *buf;
  GstCaps *caps;

  buf = gst_buffer_new_and_alloc (HEIGHT * rowstride);
  fail_unless_equals_int (GST_BUFFER_SIZE (buf), sizeof (rgb24_3x4_img));
  memcpy (GST_BUFFER_DATA (buf), rgb24_3x4_img, sizeof (rgb24_3x4_img));

  caps = create_caps_rgb24 ();
  gst_buffer_set_caps (buf, caps);
  gst_caps_unref (caps);

  return buf;
}

static GstBuffer *
create_buffer_rgba32_3x4 (void)
{
  /* stride is rounded up to multiple of 4, so 3 bytes padding for each row */
  /* should be:  RED     BLUE    WHITE    where 'nothing' is fully transparent
   *             GREEN   RED     BLUE     and all other colours are fully
   *             NOTHING GREEN   RED      opaque.
   *             BLACK   NOTHING GREEN
   */
  const guint8 rgba32_3x4_img[HEIGHT * WIDTH * 4] = {
    0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0xff, 0xff,
    0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff
  };
  guint rowstride = WIDTH * 4;
  GstBuffer *buf;
  GstCaps *caps;

  buf = gst_buffer_new_and_alloc (HEIGHT * rowstride);
  fail_unless_equals_int (GST_BUFFER_SIZE (buf), sizeof (rgba32_3x4_img));
  memcpy (GST_BUFFER_DATA (buf), rgba32_3x4_img, sizeof (rgba32_3x4_img));

  caps = create_caps_rgba32 ();
  gst_buffer_set_caps (buf, caps);
  gst_caps_unref (caps);

  return buf;
}

GST_START_TEST (test_rgb24)
{
  GstElement *alphacolor;
  GstBuffer *inbuffer;
  GstCaps *incaps;

  incaps = create_caps_rgb24 ();
  alphacolor = setup_alphacolor ();

  fail_unless_equals_int (gst_element_set_state (alphacolor, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  inbuffer = create_buffer_rgb24_3x4 ();
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away reference; this should error out with a not-negotiated
   * error, alphacolor should only accept RGBA caps, not but plain RGB24 caps */
  GST_DEBUG ("push it");
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer),
      GST_FLOW_NOT_NEGOTIATED);
  GST_DEBUG ("pushed it");

  fail_unless (g_list_length (buffers) == 0);

  fail_unless_equals_int (gst_element_set_state (alphacolor, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  /* cleanup */
  GST_DEBUG ("cleanup alphacolor");
  cleanup_alphacolor (alphacolor);
  GST_DEBUG ("cleanup, unref incaps");
  ASSERT_CAPS_REFCOUNT (incaps, "incaps", 1);
  gst_caps_unref (incaps);
}

GST_END_TEST;

/* these macros assume WIDTH and HEIGHT is fixed to what's defined above */
#define fail_unless_ayuv_pixel_has_alpha(ayuv,x,y,a) \
    { \
        guint8 *pixel; \
        pixel = ((guint8*)(ayuv) + ((WIDTH * 4) * (y)) + ((x) * 4)); \
        fail_unless_equals_int (pixel[0], a); \
    }

GST_START_TEST (test_rgba32)
{
  GstElement *alphacolor;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *incaps;
  guint8 *ayuv;
  guint outlength;

  incaps = create_caps_rgba32 ();
  alphacolor = setup_alphacolor ();

  fail_unless_equals_int (gst_element_set_state (alphacolor, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  inbuffer = create_buffer_rgba32_3x4 ();
  GST_DEBUG ("Created buffer of %d bytes", GST_BUFFER_SIZE (inbuffer));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away reference */
  GST_DEBUG ("push it");
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_OK);
  GST_DEBUG ("pushed it");

  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) == 1);
  outbuffer = (GstBuffer *) buffers->data;
  fail_if (outbuffer == NULL);
  fail_unless (GST_IS_BUFFER (outbuffer));

  ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
  outlength = WIDTH * HEIGHT * 4;       /* output is AYUV */
  fail_unless_equals_int (GST_BUFFER_SIZE (outbuffer), outlength);

  ayuv = GST_BUFFER_DATA (outbuffer);

  /* check alpha values (0x00 = totally transparent, 0xff = totally opaque) */
  fail_unless_ayuv_pixel_has_alpha (ayuv, 0, 0, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 1, 0, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 2, 0, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 0, 1, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 1, 1, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 2, 1, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 0, 2, 0x00);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 1, 2, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 2, 2, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 0, 3, 0xff);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 1, 3, 0x00);
  fail_unless_ayuv_pixel_has_alpha (ayuv, 2, 3, 0xff);

  /* we don't check the YUV data, because apparently results differ slightly
   * depending on whether we run in valgrind or not */

  buffers = g_list_remove (buffers, outbuffer);
  gst_buffer_unref (outbuffer);

  fail_unless_equals_int (gst_element_set_state (alphacolor, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  /* cleanup */
  GST_DEBUG ("cleanup alphacolor");
  cleanup_alphacolor (alphacolor);
  GST_DEBUG ("cleanup, unref incaps");
  ASSERT_CAPS_REFCOUNT (incaps, "incaps", 1);
  gst_caps_unref (incaps);
}

GST_END_TEST;


static Suite *
alphacolor_suite (void)
{
  Suite *s = suite_create ("alphacolor");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rgb24);
  tcase_add_test (tc_chain, test_rgba32);

  return s;
}

GST_CHECK_MAIN (alphacolor);
