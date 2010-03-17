/* GStreamer
 *
 * unit test for audiopanorama
 *
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
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

#include <gst/base/gstbasetransform.h>
#include <gst/check/gstcheck.h>

gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;


#define PANORAMA_MONO_CAPS_STRING    \
    "audio/x-raw-int, "                 \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 16, "                \
    "depth = (int) 16, "                \
    "signed = (bool) TRUE"

#define PANORAMA_STEREO_CAPS_STRING  \
    "audio/x-raw-int, "                 \
    "channels = (int) 2, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 16, "                \
    "depth = (int) 16, "                \
    "signed = (bool) TRUE"

#define PANORAMA_WRONG_CAPS_STRING  \
    "audio/x-raw-int, "                 \
    "channels = (int) 5, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 16, "                \
    "depth = (int) 16, "                \
    "signed = (bool) FALSE"


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "channels = (int) 2, "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );
static GstStaticPadTemplate msrctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "channels = (int) 1, "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );
static GstStaticPadTemplate ssrctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "channels = (int) 2, "
        "rate = (int) [ 1,  MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (bool) TRUE")
    );

static GstElement *
setup_panorama_m (void)
{
  GstElement *panorama;

  GST_DEBUG ("setup_panorama");
  panorama = gst_check_setup_element ("audiopanorama");
  mysrcpad = gst_check_setup_src_pad (panorama, &msrctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (panorama, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return panorama;
}

static GstElement *
setup_panorama_s (void)
{
  GstElement *panorama;

  GST_DEBUG ("setup_panorama");
  panorama = gst_check_setup_element ("audiopanorama");
  mysrcpad = gst_check_setup_src_pad (panorama, &ssrctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (panorama, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return panorama;
}

static void
cleanup_panorama (GstElement * panorama)
{
  GST_DEBUG ("cleanup_panorama");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (panorama);
  gst_check_teardown_sink_pad (panorama);
  gst_check_teardown_element (panorama);
}

GST_START_TEST (test_mono_middle)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[4] = { 8192, 8192, -128, -128 };
  gint16 *res;

  panorama = setup_panorama_m ();
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  caps = gst_caps_from_string (PANORAMA_MONO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_mono_left)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[4] = { 16384, 0, -256, 0 };
  gint16 *res;

  panorama = setup_panorama_m ();
  g_object_set (G_OBJECT (panorama), "panorama", -1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (PANORAMA_MONO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_mono_right)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[4] = { 0, 16384, 0, -256 };
  gint16 *res;

  panorama = setup_panorama_m ();
  g_object_set (G_OBJECT (panorama), "panorama", 1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (PANORAMA_MONO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_stereo_middle)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[4] = { 16384, -256, 8192, 128 };
  gint16 *res;

  panorama = setup_panorama_s ();
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  caps = gst_caps_from_string (PANORAMA_STEREO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... so keep an extra one */
  gst_buffer_ref (inbuffer);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (inbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      in[0], in[1], in[2], in[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), in, 8) == 0);

  /* cleanup */
  gst_buffer_unref (inbuffer);
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_stereo_left)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[4] = { 16384, -256, 8192, 128 };
  gint16 out[4] = { 16384 - 256, 0, 8192 + 128, 0 };
  gint16 *res;

  panorama = setup_panorama_s ();
  g_object_set (G_OBJECT (panorama), "panorama", -1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (PANORAMA_STEREO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_stereo_right)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[4] = { 16384, -256, 8192, 128 };
  gint16 out[4] = { 0, -256 + 16384, 0, 128 + 8192 };
  gint16 *res;

  panorama = setup_panorama_s ();
  g_object_set (G_OBJECT (panorama), "panorama", 1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (PANORAMA_STEREO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_mono_middle_simple)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[4] = { 16384, 16384, -256, -256 };
  gint16 *res;

  panorama = setup_panorama_m ();
  g_object_set (G_OBJECT (panorama), "method", 1, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  caps = gst_caps_from_string (PANORAMA_MONO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_mono_left_simple)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[4] = { 16384, 0, -256, 0 };
  gint16 *res;

  panorama = setup_panorama_m ();
  g_object_set (G_OBJECT (panorama), "method", 1, NULL);
  g_object_set (G_OBJECT (panorama), "panorama", -1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (PANORAMA_MONO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_mono_right_simple)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[4] = { 0, 16384, 0, -256 };
  gint16 *res;

  panorama = setup_panorama_m ();
  g_object_set (G_OBJECT (panorama), "method", 1, NULL);
  g_object_set (G_OBJECT (panorama), "panorama", 1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (PANORAMA_MONO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_stereo_middle_simple)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[4] = { 16384, -256, 8192, 128 };
  gint16 *res;

  panorama = setup_panorama_s ();
  g_object_set (G_OBJECT (panorama), "method", 1, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  caps = gst_caps_from_string (PANORAMA_STEREO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... so keep an extra one */
  gst_buffer_ref (inbuffer);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (inbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      in[0], in[1], in[2], in[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), in, 8) == 0);

  /* cleanup */
  gst_buffer_unref (inbuffer);
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_stereo_left_simple)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[4] = { 16384, -256, 8192, 128 };
  gint16 out[4] = { 16384, 0, 8192, 0 };
  gint16 *res;

  panorama = setup_panorama_s ();
  g_object_set (G_OBJECT (panorama), "method", 1, NULL);
  g_object_set (G_OBJECT (panorama), "panorama", -1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (PANORAMA_STEREO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_stereo_right_simple)
{
  GstElement *panorama;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[4] = { 16384, -256, 8192, 128 };
  gint16 out[4] = { 0, -256, 0, 128 };
  gint16 *res;

  panorama = setup_panorama_s ();
  g_object_set (G_OBJECT (panorama), "method", 1, NULL);
  g_object_set (G_OBJECT (panorama), "panorama", 1.0, NULL);
  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (PANORAMA_STEREO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d %+5d %+5d real %+5d %+5d %+5d %+5d",
      out[0], out[1], out[2], out[3], res[0], res[1], res[2], res[3]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_panorama (panorama);
}

GST_END_TEST;

GST_START_TEST (test_wrong_caps)
{
  GstElement *panorama;
  GstBuffer *inbuffer;
  gint16 in[2] = { 16384, -256 };
  GstBus *bus;
  GstMessage *message;
  GstCaps *caps;

  panorama = setup_panorama_m ();
  bus = gst_bus_new ();

  fail_unless (gst_element_set_state (panorama,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  caps = gst_caps_from_string (PANORAMA_WRONG_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  /* set a bus here so we avoid getting state change messages */
  gst_element_set_bus (panorama, bus);

  /* pushing gives an error because it can't negotiate with wrong caps */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer),
      GST_FLOW_NOT_NEGOTIATED);
  /* ... and the buffer would have been lost if we didn't ref it ourselves */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* panorama_set_caps should not have been called since basetransform caught
   * the negotiation problem */
  fail_if ((message = gst_bus_pop (bus)) != NULL);

  /* cleanup */
  gst_element_set_bus (panorama, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_panorama (panorama);
}

GST_END_TEST;


static Suite *
panorama_suite (void)
{
  Suite *s = suite_create ("panorama");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_mono_middle);
  tcase_add_test (tc_chain, test_mono_left);
  tcase_add_test (tc_chain, test_mono_right);
  tcase_add_test (tc_chain, test_stereo_middle);
  tcase_add_test (tc_chain, test_stereo_left);
  tcase_add_test (tc_chain, test_stereo_right);
  tcase_add_test (tc_chain, test_mono_middle_simple);
  tcase_add_test (tc_chain, test_mono_left_simple);
  tcase_add_test (tc_chain, test_mono_right_simple);
  tcase_add_test (tc_chain, test_stereo_middle_simple);
  tcase_add_test (tc_chain, test_stereo_left_simple);
  tcase_add_test (tc_chain, test_stereo_right_simple);
  tcase_add_test (tc_chain, test_wrong_caps);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = panorama_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
