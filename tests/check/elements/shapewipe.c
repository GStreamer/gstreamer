/* GStreamer
 *
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *myvideosrcpad, *mymasksrcpad, *mysinkpad;


#define SHAPEWIPE_VIDEO_CAPS_STRING    \
    "video/x-raw-yuv, " \
    "format = (GstFourcc)AYUV, " \
    "width = 400, " \
    "height = 400, " \
    "framerate = 0/1"

#define SHAPEWIPE_MASK_CAPS_STRING    \
    "video/x-raw-gray, " \
    "bpp = 8, " \
    "depth = 8, " \
    "width = 400, " \
    "height = 400, " \
    "framerate = 0/1"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SHAPEWIPE_VIDEO_CAPS_STRING)
    );
static GstStaticPadTemplate videosrctemplate =
GST_STATIC_PAD_TEMPLATE ("videosrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SHAPEWIPE_VIDEO_CAPS_STRING)
    );
static GstStaticPadTemplate masksrctemplate =
GST_STATIC_PAD_TEMPLATE ("masksrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SHAPEWIPE_MASK_CAPS_STRING)
    );


static GstBuffer *output = NULL;

static GstFlowReturn
on_chain (GstPad * pad, GstBuffer * buffer)
{
  g_return_val_if_fail (output == NULL, GST_FLOW_ERROR);

  output = buffer;
  return GST_FLOW_OK;
}

GST_START_TEST (test_general)
{
  GstElement *shapewipe;
  GstPad *p;
  GstCaps *caps;
  GstBuffer *mask, *input;
  guint i, j;
  guint8 *data;

  myvideosrcpad =
      gst_pad_new_from_static_template (&videosrctemplate, "videosrc");
  gst_pad_set_active (myvideosrcpad, TRUE);
  caps = gst_caps_from_string (SHAPEWIPE_VIDEO_CAPS_STRING);
  gst_pad_set_caps (myvideosrcpad, caps);
  gst_caps_unref (caps);

  mymasksrcpad = gst_pad_new_from_static_template (&masksrctemplate, "masksrc");
  gst_pad_set_active (mymasksrcpad, TRUE);
  caps = gst_caps_from_string (SHAPEWIPE_MASK_CAPS_STRING);
  gst_pad_set_caps (mymasksrcpad, caps);
  gst_caps_unref (caps);

  mysinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (mysinkpad, on_chain);
  gst_pad_set_active (mysinkpad, TRUE);
  caps = gst_caps_from_string (SHAPEWIPE_VIDEO_CAPS_STRING);
  gst_pad_set_caps (mysinkpad, caps);
  gst_caps_unref (caps);

  shapewipe = gst_element_factory_make ("shapewipe", NULL);
  fail_unless (shapewipe != NULL);

  p = gst_element_get_static_pad (shapewipe, "video_sink");
  fail_unless (gst_pad_link (myvideosrcpad, p) == GST_PAD_LINK_OK);
  gst_object_unref (p);
  p = gst_element_get_static_pad (shapewipe, "mask_sink");
  fail_unless (gst_pad_link (mymasksrcpad, p) == GST_PAD_LINK_OK);
  gst_object_unref (p);
  p = gst_element_get_static_pad (shapewipe, "src");
  fail_unless (gst_pad_link (p, mysinkpad) == GST_PAD_LINK_OK);
  gst_object_unref (p);

  fail_unless (gst_element_set_state (shapewipe,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  mask = gst_buffer_new_and_alloc (400 * 400);
  caps = gst_caps_from_string (SHAPEWIPE_MASK_CAPS_STRING);
  gst_buffer_set_caps (mask, caps);
  gst_caps_unref (caps);
  data = GST_BUFFER_DATA (mask);
  for (i = 0; i < 400; i++) {
    for (j = 0; j < 400; j++) {
      if (i < 100 && j < 100)
        data[0] = 0;
      else if (i < 200 && j < 200)
        data[0] = 85;
      else if (i < 300 && j < 300)
        data[0] = 170;
      else
        data[0] = 254;
      data++;
    }
  }

  fail_unless (gst_pad_push (mymasksrcpad, mask) == GST_FLOW_OK);

  input = gst_buffer_new_and_alloc (400 * 400 * 4);
  caps = gst_caps_from_string (SHAPEWIPE_VIDEO_CAPS_STRING);
  gst_buffer_set_caps (input, caps);
  gst_caps_unref (caps);
  data = GST_BUFFER_DATA (input);
  for (i = 0; i < 400; i++) {
    for (j = 0; j < 400; j++) {
      /* This is green */
      data[0] = 255;            /* A */
      data[1] = 173;            /* Y */
      data[2] = 42;             /* U */
      data[3] = 26;             /* V */
      data += 4;
    }
  }

  g_object_set (G_OBJECT (shapewipe), "position", 0.0, NULL);
  output = NULL;
  fail_unless (gst_pad_push (myvideosrcpad,
          gst_buffer_ref (input)) == GST_FLOW_OK);
  fail_unless (output != NULL);
  data = GST_BUFFER_DATA (output);
  for (i = 0; i < 400; i++) {
    for (j = 0; j < 400; j++) {
      fail_unless_equals_int (data[0], 255);    /* A */
      fail_unless_equals_int (data[1], 173);    /* Y */
      fail_unless_equals_int (data[2], 42);     /* U */
      fail_unless_equals_int (data[3], 26);     /* V */
      data += 4;
    }
  }
  gst_buffer_unref (output);
  output = NULL;

  g_object_set (G_OBJECT (shapewipe), "position", 0.1, NULL);
  output = NULL;
  fail_unless (gst_pad_push (myvideosrcpad,
          gst_buffer_ref (input)) == GST_FLOW_OK);
  fail_unless (output != NULL);
  data = GST_BUFFER_DATA (output);
  for (i = 0; i < 400; i++) {
    for (j = 0; j < 400; j++) {
      if (i < 100 && j < 100) {
        fail_unless_equals_int (data[0], 0);    /* A */
        fail_unless_equals_int (data[1], 173);  /* Y */
        fail_unless_equals_int (data[2], 42);   /* U */
        fail_unless_equals_int (data[3], 26);   /* V */
      } else {
        fail_unless_equals_int (data[0], 255);  /* A */
        fail_unless_equals_int (data[1], 173);  /* Y */
        fail_unless_equals_int (data[2], 42);   /* U */
        fail_unless_equals_int (data[3], 26);   /* V */
      }
      data += 4;
    }
  }
  gst_buffer_unref (output);
  output = NULL;

  g_object_set (G_OBJECT (shapewipe), "position", 0.34, NULL);
  output = NULL;
  fail_unless (gst_pad_push (myvideosrcpad,
          gst_buffer_ref (input)) == GST_FLOW_OK);
  fail_unless (output != NULL);
  data = GST_BUFFER_DATA (output);
  for (i = 0; i < 400; i++) {
    for (j = 0; j < 400; j++) {
      if (i < 200 && j < 200) {
        fail_unless_equals_int (data[0], 0);    /* A */
        fail_unless_equals_int (data[1], 173);  /* Y */
        fail_unless_equals_int (data[2], 42);   /* U */
        fail_unless_equals_int (data[3], 26);   /* V */
      } else {
        fail_unless_equals_int (data[0], 255);  /* A */
        fail_unless_equals_int (data[1], 173);  /* Y */
        fail_unless_equals_int (data[2], 42);   /* U */
        fail_unless_equals_int (data[3], 26);   /* V */
      }
      data += 4;
    }
  }
  gst_buffer_unref (output);
  output = NULL;

  g_object_set (G_OBJECT (shapewipe), "position", 0.67, NULL);
  output = NULL;
  fail_unless (gst_pad_push (myvideosrcpad,
          gst_buffer_ref (input)) == GST_FLOW_OK);
  fail_unless (output != NULL);
  data = GST_BUFFER_DATA (output);
  for (i = 0; i < 400; i++) {
    for (j = 0; j < 400; j++) {
      if (i < 300 && j < 300) {
        fail_unless_equals_int (data[0], 0);    /* A */
        fail_unless_equals_int (data[1], 173);  /* Y */
        fail_unless_equals_int (data[2], 42);   /* U */
        fail_unless_equals_int (data[3], 26);   /* V */
      } else {
        fail_unless_equals_int (data[0], 255);  /* A */
        fail_unless_equals_int (data[1], 173);  /* Y */
        fail_unless_equals_int (data[2], 42);   /* U */
        fail_unless_equals_int (data[3], 26);   /* V */
      }
      data += 4;
    }
  }
  gst_buffer_unref (output);
  output = NULL;

  g_object_set (G_OBJECT (shapewipe), "position", 1.0, NULL);
  output = NULL;
  fail_unless (gst_pad_push (myvideosrcpad,
          gst_buffer_ref (input)) == GST_FLOW_OK);
  fail_unless (output != NULL);
  data = GST_BUFFER_DATA (output);
  for (i = 0; i < 400; i++) {
    for (j = 0; j < 400; j++) {
      fail_unless_equals_int (data[0], 0);      /* A */
      fail_unless_equals_int (data[1], 173);    /* Y */
      fail_unless_equals_int (data[2], 42);     /* U */
      fail_unless_equals_int (data[3], 26);     /* V */
      data += 4;
    }
  }
  gst_buffer_unref (output);
  output = NULL;

  gst_buffer_unref (input);

  fail_unless (gst_element_set_state (shapewipe,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  p = gst_element_get_static_pad (shapewipe, "video_sink");
  fail_unless (gst_pad_unlink (myvideosrcpad, p));
  gst_object_unref (p);
  p = gst_element_get_static_pad (shapewipe, "mask_sink");
  fail_unless (gst_pad_unlink (mymasksrcpad, p));
  gst_object_unref (p);
  p = gst_element_get_static_pad (shapewipe, "src");
  fail_unless (gst_pad_unlink (p, mysinkpad));
  gst_object_unref (p);

  gst_object_unref (myvideosrcpad);
  gst_object_unref (mymasksrcpad);
  gst_object_unref (mysinkpad);
  gst_object_unref (shapewipe);
}

GST_END_TEST;

Suite *
shapewipe_suite (void)
{
  Suite *s = suite_create ("shapewipe");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_set_timeout (tc_chain, 180);
  tcase_add_test (tc_chain, test_general);

  return s;
}

GST_CHECK_MAIN (shapewipe);
