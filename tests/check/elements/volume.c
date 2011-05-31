/* GStreamer
 *
 * unit test for volume
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

#include <gst/base/gstbasetransform.h>
#include <gst/check/gstcheck.h>
#include <gst/controller/gstcontroller.h>
#include <gst/interfaces/streamvolume.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;


#define VOLUME_CAPS_TEMPLATE_STRING     \
    "audio/x-raw-int, "                 \
    "channels = (int) [ 1, MAX ], "     \
    "rate = (int) [ 1,  MAX ], "        \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) { 8, 16, 24, 32 }, " \
    "depth = (int) { 8, 16, 24, 32 }, " \
    "signed = (bool) TRUE; "            \
    "audio/x-raw-float, "               \
    "channels = (int) [ 1, MAX ], "     \
    "rate = (int) [ 1,  MAX ], "        \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) { 32, 64 }"          \

#define VOLUME_CAPS_STRING_S8           \
    "audio/x-raw-int, "                 \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 8, "                 \
    "depth = (int) 8, "                 \
    "signed = (bool) TRUE"

#define VOLUME_CAPS_STRING_S16          \
    "audio/x-raw-int, "                 \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 16, "                \
    "depth = (int) 16, "                \
    "signed = (bool) TRUE"

#define VOLUME_CAPS_STRING_S24          \
    "audio/x-raw-int, "                 \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 24, "                \
    "depth = (int) 24, "                \
    "signed = (bool) TRUE"

#define VOLUME_CAPS_STRING_S32          \
    "audio/x-raw-int, "                 \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 32, "                \
    "depth = (int) 32, "                \
    "signed = (bool) TRUE"

#define VOLUME_CAPS_STRING_F32          \
    "audio/x-raw-float, "               \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 32"

#define VOLUME_CAPS_STRING_F64          \
    "audio/x-raw-float, "               \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 64"

#define VOLUME_WRONG_CAPS_STRING        \
    "audio/x-raw-int, "                 \
    "channels = (int) 1, "              \
    "rate = (int) 44100, "              \
    "endianness = (int) BYTE_ORDER, "   \
    "width = (int) 16, "                \
    "depth = (int) 16, "                \
    "signed = (bool) FALSE"


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VOLUME_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VOLUME_CAPS_TEMPLATE_STRING)
    );

static GstElement *
setup_volume (void)
{
  GstElement *volume;

  GST_DEBUG ("setup_volume");
  volume = gst_check_setup_element ("volume");
  mysrcpad = gst_check_setup_src_pad (volume, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (volume, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return volume;
}

static void
cleanup_volume (GstElement * volume)
{
  GST_DEBUG ("cleanup_volume");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (volume);
  gst_check_teardown_sink_pad (volume);
  gst_check_teardown_element (volume);
}

GST_START_TEST (test_get_set)
{
  GstElement *volume = gst_element_factory_make ("volume", NULL);
  gdouble val;

  fail_unless (volume != NULL);
  g_object_get (G_OBJECT (volume), "volume", &val, NULL);
  fail_unless (val == 1.0);
  fail_unless (val == gst_stream_volume_get_volume (GST_STREAM_VOLUME (volume),
          GST_STREAM_VOLUME_FORMAT_LINEAR));

  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  g_object_get (G_OBJECT (volume), "volume", &val, NULL);
  fail_unless (val == 0.5);
  fail_unless (val == gst_stream_volume_get_volume (GST_STREAM_VOLUME (volume),
          GST_STREAM_VOLUME_FORMAT_LINEAR));

  gst_stream_volume_set_volume (GST_STREAM_VOLUME (volume),
      GST_STREAM_VOLUME_FORMAT_LINEAR, 1.0);
  g_object_get (G_OBJECT (volume), "volume", &val, NULL);
  fail_unless (val == 1.0);
  fail_unless (val == gst_stream_volume_get_volume (GST_STREAM_VOLUME (volume),
          GST_STREAM_VOLUME_FORMAT_LINEAR));

  gst_object_unref (volume);
}

GST_END_TEST;

GST_START_TEST (test_unity_s8)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint8 in[2] = { 64, -16 };
  gint8 *res;

  volume = setup_volume ();
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (2);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 2);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S8);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint8 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", in[0], in[1], res[0], res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 2) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_half_s8)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint8 in[2] = { 64, -16 };
  gint8 out[2] = { 32, -8 };
  gint8 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (2);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 2);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 2) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S8);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint8 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 2) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_double_s8)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint8 in[2] = { 64, -16 };
  gint8 out[2] = { 127, -32 };  /* notice the clamped sample */
  gint8 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 2.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (2);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 2);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 2) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S8);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint8 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 2) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_ten_s8)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint8 in[2] = { 64, -10 };
  gint8 out[2] = { 127, -100 }; /* notice the clamped sample */
  gint8 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 10.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (2);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 2);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 2) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S8);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint8 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 2) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_mute_s8)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint8 in[2] = { 64, -16 };
  gint8 out[2] = { 0, 0 };
  gint8 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "mute", TRUE, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (2);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 2);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 2) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S8);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint8 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 2) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_unity_s16)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 *res;

  volume = setup_volume ();
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S16);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", in[0], in[1], res[0], res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_half_s16)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[2] = { 8192, -128 };
  gint16 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S16);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_double_s16)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[2] = { 32767, -512 };      /* notice the clamped sample */
  gint16 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 2.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S16);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_ten_s16)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -10 };
  gint16 out[2] = { 32767, -100 };      /* notice the clamped sample */
  gint16 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 10.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S16);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;


GST_START_TEST (test_mute_s16)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 out[2] = { 0, 0 };
  gint16 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "mute", TRUE, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S16);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
#define get_unaligned_i24(_x) ( (((guint8*)_x)[0]) | ((((guint8*)_x)[1]) << 8) | ((((gint8*)_x)[2]) << 16) )
#define write_unaligned_u24(_x,samp) do { (((guint8*)_x)[0]) = samp & 0xFF; (((guint8*)_x)[1]) = (samp >> 8) & 0xFF; (((guint8*)_x)[2]) = (samp >> 16) & 0xFF; } while (0)
#else /* BIG ENDIAN */
#define get_unaligned_i24(_x) ( (((guint8*)_x)[2]) | ((((guint8*)_x)[1]) << 8) | ((((gint8*)_x)[0]) << 16) )
#define write_unaligned_u24(_x,samp) do { (((guint8*)_x)[0]) = (samp >> 16) & 0xFF; (((guint8*)_x)[1]) = (samp >> 8) & 0xFF; (((guint8*)_x)[2]) = samp & 0xFF; } while (0)
#endif

GST_START_TEST (test_unity_s24)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint32 in_32[2] = { 4194304, -4096 };
  guint8 in[6];
  guint8 *res;
  gint32 res_32[2];


  write_unaligned_u24 (in, in_32[0]);
  write_unaligned_u24 (in + 3, in_32[1]);

  volume = setup_volume ();
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (6);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 6);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S24);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = GST_BUFFER_DATA (outbuffer);

  res_32[0] = get_unaligned_i24 (res);
  res_32[1] = get_unaligned_i24 ((res + 3));

  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", in_32[0], in_32[1], res_32[0],
      res_32[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 6) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_half_s24)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in_32[2] = { 4194304, -4096 };
  guint8 in[6];
  guint8 *res;
  gint32 res_32[2];
  gint32 out_32[2] = { 2097152, -2048 };

  write_unaligned_u24 (in, in_32[0]);
  write_unaligned_u24 (in + 3, in_32[1]);

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (6);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 6);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 6) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S24);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = GST_BUFFER_DATA (outbuffer);

  res_32[0] = get_unaligned_i24 (res);
  res_32[1] = get_unaligned_i24 ((res + 3));

  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out_32[0], out_32[1],
      res_32[0], res_32[1]);
  fail_unless (memcmp (res_32, out_32, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_double_s24)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in_32[2] = { 4194304, -4096 };
  guint8 in[6];
  guint8 *res;
  gint32 res_32[2];
  gint32 out_32[2] = { 8388607, -8192 };        /* notice the clamped sample */

  write_unaligned_u24 (in, in_32[0]);
  write_unaligned_u24 (in + 3, in_32[1]);

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 2.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (6);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 6);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 6) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S24);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = GST_BUFFER_DATA (outbuffer);

  res_32[0] = get_unaligned_i24 (res);
  res_32[1] = get_unaligned_i24 ((res + 3));

  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out_32[0], out_32[1],
      res_32[0], res_32[1]);
  fail_unless (memcmp (res_32, out_32, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_ten_s24)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in_32[2] = { 4194304, -10 };
  guint8 in[6];
  guint8 *res;
  gint32 res_32[2];
  gint32 out_32[2] = { 8388607, -100 }; /* notice the clamped sample */

  write_unaligned_u24 (in, in_32[0]);
  write_unaligned_u24 (in + 3, in_32[1]);

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 10.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (6);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 6);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 6) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S24);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = GST_BUFFER_DATA (outbuffer);

  res_32[0] = get_unaligned_i24 (res);
  res_32[1] = get_unaligned_i24 ((res + 3));

  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out_32[0], out_32[1],
      res_32[0], res_32[1]);
  fail_unless (memcmp (res_32, out_32, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_mute_s24)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in_32[2] = { 4194304, -4096 };
  guint8 in[6];
  guint8 *res;
  gint32 res_32[2];
  gint32 out_32[2] = { 0, 0 };  /* notice the clamped sample */

  write_unaligned_u24 (in, in_32[0]);
  write_unaligned_u24 (in + 3, in_32[1]);

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "mute", TRUE, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (6);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 6);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 6) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S24);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);

  res = GST_BUFFER_DATA (outbuffer);

  res_32[0] = get_unaligned_i24 (res);
  res_32[1] = get_unaligned_i24 ((res + 3));

  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out_32[0], out_32[1],
      res_32[0], res_32[1]);
  fail_unless (memcmp (res_32, out_32, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_unity_s32)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint32 in[2] = { 1073741824, -65536 };
  gint32 *res;

  volume = setup_volume ();
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint32 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", in[0], in[1], res[0], res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_half_s32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in[2] = { 1073741824, -65536 };
  gint32 out[2] = { 536870912, -32768 };
  gint32 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint32 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_double_s32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in[2] = { 1073741824, -65536 };
  gint32 out[2] = { 2147483647, -131072 };      /* notice the clamped sample */
  gint32 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 2.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint32 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_ten_s32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in[2] = { 1073741824, -10 };
  gint32 out[2] = { 2147483647, -100 }; /* notice the clamped sample */
  gint32 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 10.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint32 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_mute_s32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gint32 in[2] = { 1073741824, -65536 };
  gint32 out[2] = { 0, 0 };
  gint32 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "mute", TRUE, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint32 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", out[0], out[1], res[0],
      res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, 8) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_unity_f32)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gfloat in[2] = { 0.75, -0.25 };
  gfloat *res;

  volume = setup_volume ();
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gfloat *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", in[0], in[1], res[0],
      res[1]);
  fail_unless_equals_float (res[0], in[0]);
  fail_unless_equals_float (res[1], in[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_half_f32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gfloat in[2] = { 0.75, -0.25 };
  gfloat out[2] = { 0.375, -0.125 };
  gfloat *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gfloat *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_double_f32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gfloat in[2] = { 0.75, -0.25 };
  gfloat out[2] = { 1.5, -0.5 };        /* nothing is clamped */
  gfloat *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 2.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gfloat *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_ten_f32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gfloat in[2] = { 0.75, -0.25 };
  gfloat out[2] = { 7.5, -2.5 };        /* nothing is clamped */
  gfloat *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 10.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gfloat *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;


GST_START_TEST (test_mute_f32)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gfloat in[2] = { 0.75, -0.25 };
  gfloat out[2] = { 0, 0 };
  gfloat *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "mute", TRUE, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (8);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 8);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 8) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F32);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gfloat *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_unity_f64)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gdouble in[2] = { 0.75, -0.25 };
  gdouble *res;

  volume = setup_volume ();
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gdouble *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", in[0], in[1], res[0],
      res[1]);
  fail_unless_equals_float (res[0], in[0]);
  fail_unless_equals_float (res[1], in[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_half_f64)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gdouble in[2] = { 0.75, -0.25 };
  gdouble out[2] = { 0.375, -0.125 };
  gdouble *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 0.5, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gdouble *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_double_f64)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gdouble in[2] = { 0.75, -0.25 };
  gdouble out[2] = { 1.5, -0.5 };       /* nothing is clamped */
  gdouble *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 2.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gdouble *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_ten_f64)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gdouble in[2] = { 0.75, -0.25 };
  gdouble out[2] = { 7.5, -2.5 };       /* nothing is clamped */
  gdouble *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 10.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gdouble *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;


GST_START_TEST (test_mute_f64)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  GstCaps *caps;
  gdouble in[2] = { 0.75, -0.25 };
  gdouble out[2] = { 0, 0 };
  gdouble *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "mute", TRUE, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (16);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 16);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 16) == 0);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_F64);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  /* FIXME: reffing the inbuffer should make the transformation not be
   * inplace
   gst_buffer_ref (inbuffer);
   */

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being modified inplace and
   * collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gdouble *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+1.4f %+1.4f  real %+1.4f %+1.4f", out[0], out[1],
      res[0], res[1]);
  fail_unless_equals_float (res[0], out[0]);
  fail_unless_equals_float (res[1], out[1]);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_wrong_caps)
{
  GstElement *volume;
  GstBuffer *inbuffer;
  gint16 in[2] = { 16384, -256 };
  GstBus *bus;
  GstMessage *message;
  GstCaps *caps;

  volume = setup_volume ();
  bus = gst_bus_new ();

  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  caps = gst_caps_from_string (VOLUME_WRONG_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  /* set a bus here so we avoid getting state change messages */
  gst_element_set_bus (volume, bus);

  /* pushing gives an error because it can't negotiate with wrong caps */
  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer),
      GST_FLOW_NOT_NEGOTIATED);
  /* ... and the buffer would have been lost if we didn't ref it ourselves */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* volume_set_caps should not have been called since basetransform caught
   * the negotiation problem */
  fail_if ((message = gst_bus_pop (bus)) != NULL);

  /* cleanup */
  gst_element_set_bus (volume, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_passthrough)
{
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 *res;

  volume = setup_volume ();
  g_object_set (G_OBJECT (volume), "volume", 1.0, NULL);
  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S16);
  gst_buffer_set_caps (inbuffer, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", in[0], in[1], res[0], res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);

  /* cleanup */
  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_controller_usability)
{
  GstInterpolationControlSource *csource;
  GstController *c;
  GstElement *volume;
  GValue value = { 0, };

  /* note: the volume element should init the controller library for us */
  volume = setup_volume ();

  c = gst_controller_new (G_OBJECT (volume), "volume", NULL);

  fail_unless (GST_IS_CONTROLLER (c));

  /* this shouldn't crash, whether this mode is implemented or not */
  csource = gst_interpolation_control_source_new ();
  gst_interpolation_control_source_set_interpolation_mode (csource,
      GST_INTERPOLATE_CUBIC);
  gst_controller_set_control_source (c, "volume", GST_CONTROL_SOURCE (csource));
  g_object_unref (csource);

  g_value_init (&value, G_TYPE_DOUBLE);
  g_value_set_double (&value, 0.0);
  gst_interpolation_control_source_set (csource, 0 * GST_SECOND, &value);
  g_value_set_double (&value, 1.0);
  gst_interpolation_control_source_set (csource, 5 * GST_SECOND, &value);
  g_value_set_double (&value, 0.0);
  gst_interpolation_control_source_set (csource, 10 * GST_SECOND, &value);
  g_value_unset (&value);

  g_object_unref (c);

  cleanup_volume (volume);
}

GST_END_TEST;

GST_START_TEST (test_controller_processing)
{
  GstInterpolationControlSource *csource;
  GstController *c;
  GstElement *volume;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint16 in[2] = { 16384, -256 };
  gint16 *res;

  volume = setup_volume ();

  c = gst_controller_new (G_OBJECT (volume), "volume", NULL);

  fail_unless (GST_IS_CONTROLLER (c));

  csource = gst_interpolation_control_source_new ();
  gst_interpolation_control_source_set_interpolation_mode (csource,
      GST_INTERPOLATE_CUBIC);
  gst_controller_set_control_source (c, "volume", GST_CONTROL_SOURCE (csource));
  g_object_unref (csource);

  fail_unless (gst_element_set_state (volume,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memcpy (GST_BUFFER_DATA (inbuffer), in, 4);
  caps = gst_caps_from_string (VOLUME_CAPS_STRING_S16);
  gst_buffer_set_caps (inbuffer, caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);
  fail_unless (inbuffer == outbuffer);
  res = (gint16 *) GST_BUFFER_DATA (outbuffer);
  GST_INFO ("expected %+5d %+5d  real %+5d %+5d", in[0], in[1], res[0], res[1]);
  fail_unless (memcmp (GST_BUFFER_DATA (inbuffer), in, 4) == 0);

  g_object_unref (c);

  cleanup_volume (volume);
}

GST_END_TEST;

static Suite *
volume_suite (void)
{
  Suite *s = suite_create ("volume");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_get_set);
  tcase_add_test (tc_chain, test_unity_s8);
  tcase_add_test (tc_chain, test_half_s8);
  tcase_add_test (tc_chain, test_double_s8);
  tcase_add_test (tc_chain, test_ten_s8);
  tcase_add_test (tc_chain, test_mute_s8);
  tcase_add_test (tc_chain, test_unity_s16);
  tcase_add_test (tc_chain, test_half_s16);
  tcase_add_test (tc_chain, test_double_s16);
  tcase_add_test (tc_chain, test_ten_s16);
  tcase_add_test (tc_chain, test_mute_s16);
  tcase_add_test (tc_chain, test_unity_s24);
  tcase_add_test (tc_chain, test_half_s24);
  tcase_add_test (tc_chain, test_double_s24);
  tcase_add_test (tc_chain, test_ten_s24);
  tcase_add_test (tc_chain, test_mute_s24);
  tcase_add_test (tc_chain, test_unity_s32);
  tcase_add_test (tc_chain, test_half_s32);
  tcase_add_test (tc_chain, test_double_s32);
  tcase_add_test (tc_chain, test_ten_s32);
  tcase_add_test (tc_chain, test_mute_s32);
  tcase_add_test (tc_chain, test_unity_f32);
  tcase_add_test (tc_chain, test_half_f32);
  tcase_add_test (tc_chain, test_double_f32);
  tcase_add_test (tc_chain, test_ten_f32);
  tcase_add_test (tc_chain, test_mute_f32);
  tcase_add_test (tc_chain, test_unity_f64);
  tcase_add_test (tc_chain, test_half_f64);
  tcase_add_test (tc_chain, test_double_f64);
  tcase_add_test (tc_chain, test_ten_f64);
  tcase_add_test (tc_chain, test_mute_f64);
  tcase_add_test (tc_chain, test_wrong_caps);
  tcase_add_test (tc_chain, test_passthrough);
  tcase_add_test (tc_chain, test_controller_usability);
  tcase_add_test (tc_chain, test_controller_processing);

  return s;
}

GST_CHECK_MAIN (volume)
