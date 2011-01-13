/* GStreamer
 *
 * unit test for qtmux
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>

#include <gst/check/gstcheck.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define AUDIO_CAPS_STRING "audio/mpeg, " \
                        "mpegversion = (int) 1, " \
                        "layer = (int) 3, " \
                        "channels = (int) 2, " \
                        "rate = (int) 48000"
#define VIDEO_CAPS_STRING "video/mpeg, " \
                           "mpegversion = (int) 4, " \
                           "width = (int) 384, " \
                           "height = (int) 288, " \
                           "framerate = (fraction) 25/1"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/quicktime"));
static GstStaticPadTemplate srcvideotemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING));

static GstStaticPadTemplate srcaudiotemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AUDIO_CAPS_STRING));


/* setup and teardown needs some special handling for muxer */
static GstPad *
setup_src_pad (GstElement * element,
    GstStaticPadTemplate * template, GstCaps * caps, const gchar * sinkname)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up sending pad");
  /* sending pad */
  srcpad = gst_pad_new_from_static_template (template, "src");
  fail_if (srcpad == NULL, "Could not create a srcpad");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  if (!(sinkpad = gst_element_get_static_pad (element, sinkname)))
    sinkpad = gst_element_get_request_pad (element, sinkname);
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));
  /* references are owned by: 1) us, 2) qtmux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  if (caps)
    fail_unless (gst_pad_set_caps (srcpad, caps));
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (sinkpad);   /* because we got it higher up */

  /* references are owned by: 1) qtmux, 2) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);

  return srcpad;
}

static void
teardown_src_pad (GstPad * srcpad)
{
  GstPad *sinkpad;

  /* clean up floating src pad */
  sinkpad = gst_pad_get_peer (srcpad);
  fail_if (sinkpad == NULL);
  /* pad refs held by 1) qtmux 2) collectpads and 3) us (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);

  gst_pad_unlink (srcpad, sinkpad);

  /* after unlinking, pad refs still held by
   * 1) qtmux and 2) collectpads and 3) us (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  gst_object_unref (sinkpad);
  /* one more ref is held by element itself */

  /* pad refs held by creator */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);
  gst_object_unref (srcpad);
}

static GstElement *
setup_qtmux (GstStaticPadTemplate * srctemplate, const gchar * sinkname)
{
  GstElement *qtmux;

  GST_DEBUG ("setup_qtmux");
  qtmux = gst_check_setup_element ("qtmux");
  mysrcpad = setup_src_pad (qtmux, srctemplate, NULL, sinkname);
  mysinkpad = gst_check_setup_sink_pad (qtmux, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return qtmux;
}

static void
cleanup_qtmux (GstElement * qtmux, const gchar * sinkname)
{
  GST_DEBUG ("cleanup_qtmux");
  gst_element_set_state (qtmux, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  teardown_src_pad (mysrcpad);
  gst_check_teardown_sink_pad (qtmux);
  gst_check_teardown_element (qtmux);
}

static void
check_qtmux_pad (GstStaticPadTemplate * srctemplate, const gchar * sinkname)
{
  GstElement *qtmux;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  int num_buffers;
  int i;
  guint8 data0[12] = "\000\000\000\024ftypqt  ";
  guint8 data1[8] = "\000\000\000\001mdat";
  guint8 data2[4] = "moov";

  qtmux = setup_qtmux (srctemplate, sinkname);
  fail_unless (gst_element_set_state (qtmux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (1);
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (mysrcpad));
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  GST_BUFFER_DURATION (inbuffer) = 40 * GST_MSECOND;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* send eos to have moov written */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);

  num_buffers = g_list_length (buffers);
  /* at least expect ftyp, mdat header, buffer chunk and moov */
  fail_unless (num_buffers >= 4);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    switch (i) {
      case 0:
      {
        /* ftyp header */
        guint8 *data = GST_BUFFER_DATA (outbuffer);

        fail_unless (GST_BUFFER_SIZE (outbuffer) >= 20);
        fail_unless (memcmp (data, data0, sizeof (data0)) == 0);
        fail_unless (memcmp (data + 16, data0 + 8, 4) == 0);
        break;
      }
      case 1:                  /* mdat header */
        fail_unless (GST_BUFFER_SIZE (outbuffer) == 16);
        fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), data1, sizeof (data1))
            == 0);
        break;
      case 2:                  /* buffer we put in */
        fail_unless (GST_BUFFER_SIZE (outbuffer) == 1);
        break;
      case 3:                  /* moov */
        fail_unless (GST_BUFFER_SIZE (outbuffer) > 8);
        fail_unless (memcmp (GST_BUFFER_DATA (outbuffer) + 4, data2,
                sizeof (data2)) == 0);
        break;
      default:
        break;
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  cleanup_qtmux (qtmux, sinkname);
}

static void
check_qtmux_pad_fragmented (GstStaticPadTemplate * srctemplate,
    const gchar * sinkname, gboolean streamable)
{
  GstElement *qtmux;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  int num_buffers;
  int i;
  guint8 data0[12] = "\000\000\000\024ftypqt  ";
  guint8 data1[4] = "mdat";
  guint8 data2[4] = "moov";
  guint8 data3[4] = "moof";
  guint8 data4[4] = "mfra";

  qtmux = setup_qtmux (srctemplate, sinkname);
  g_object_set (qtmux, "fragment-duration", 2000, NULL);
  g_object_set (qtmux, "streamable", streamable, NULL);
  fail_unless (gst_element_set_state (qtmux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (1);
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (mysrcpad));
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  GST_BUFFER_DURATION (inbuffer) = 40 * GST_MSECOND;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* send eos to have all written */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);

  num_buffers = g_list_length (buffers);
  /* at least expect ftyp, moov, moof, mdat header, buffer chunk
   * and optionally mfra */
  fail_unless (num_buffers >= 5);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    switch (i) {
      case 0:
      {
        /* ftyp header */
        guint8 *data = GST_BUFFER_DATA (outbuffer);

        fail_unless (GST_BUFFER_SIZE (outbuffer) >= 20);
        fail_unless (memcmp (data, data0, sizeof (data0)) == 0);
        fail_unless (memcmp (data + 16, data0 + 8, 4) == 0);
        break;
      }
      case 1:                  /* moov */
        fail_unless (GST_BUFFER_SIZE (outbuffer) > 8);
        fail_unless (memcmp (GST_BUFFER_DATA (outbuffer) + 4, data2,
                sizeof (data2)) == 0);
        break;
      case 2:                  /* moof */
        fail_unless (GST_BUFFER_SIZE (outbuffer) > 8);
        fail_unless (memcmp (GST_BUFFER_DATA (outbuffer) + 4, data3,
                sizeof (data3)) == 0);
        break;
      case 3:                  /* mdat header */
        fail_unless (GST_BUFFER_SIZE (outbuffer) == 8);
        fail_unless (memcmp (GST_BUFFER_DATA (outbuffer) + 4, data1,
                sizeof (data1)) == 0);
        break;
      case 4:                  /* buffer we put in */
        fail_unless (GST_BUFFER_SIZE (outbuffer) == 1);
        break;
      case 5:                  /* mfra */
        fail_unless (GST_BUFFER_SIZE (outbuffer) > 8);
        fail_unless (memcmp (GST_BUFFER_DATA (outbuffer) + 4, data4,
                sizeof (data4)) == 0);
        break;
      default:
        break;
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  cleanup_qtmux (qtmux, sinkname);
}


GST_START_TEST (test_video_pad)
{
  check_qtmux_pad (&srcvideotemplate, "video_%d");
}

GST_END_TEST;

GST_START_TEST (test_audio_pad)
{
  check_qtmux_pad (&srcaudiotemplate, "audio_%d");
}

GST_END_TEST;


GST_START_TEST (test_video_pad_frag)
{
  check_qtmux_pad_fragmented (&srcvideotemplate, "video_%d", FALSE);
}

GST_END_TEST;

GST_START_TEST (test_audio_pad_frag)
{
  check_qtmux_pad_fragmented (&srcaudiotemplate, "audio_%d", FALSE);
}

GST_END_TEST;


GST_START_TEST (test_video_pad_frag_streamable)
{
  check_qtmux_pad_fragmented (&srcvideotemplate, "video_%d", TRUE);
}

GST_END_TEST;


GST_START_TEST (test_audio_pad_frag_streamable)
{
  check_qtmux_pad_fragmented (&srcaudiotemplate, "audio_%d", TRUE);
}

GST_END_TEST;


GST_START_TEST (test_reuse)
{
  GstElement *qtmux = setup_qtmux (&srcvideotemplate, "video_%d");
  GstBuffer *inbuffer;
  GstCaps *caps;

  gst_element_set_state (qtmux, GST_STATE_PLAYING);
  gst_element_set_state (qtmux, GST_STATE_NULL);
  gst_element_set_state (qtmux, GST_STATE_PLAYING);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  inbuffer = gst_buffer_new_and_alloc (1);
  fail_unless (inbuffer != NULL);
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (mysrcpad));
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  GST_BUFFER_DURATION (inbuffer) = 40 * GST_MSECOND;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* send eos to have all written */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);

  cleanup_qtmux (qtmux, "video_%d");
}

GST_END_TEST;

static Suite *
qtmux_suite (void)
{
  Suite *s = suite_create ("qtmux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_video_pad);
  tcase_add_test (tc_chain, test_audio_pad);
  tcase_add_test (tc_chain, test_video_pad_frag);
  tcase_add_test (tc_chain, test_audio_pad_frag);
  tcase_add_test (tc_chain, test_video_pad_frag_streamable);
  tcase_add_test (tc_chain, test_audio_pad_frag_streamable);

  tcase_add_test (tc_chain, test_reuse);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = qtmux_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
