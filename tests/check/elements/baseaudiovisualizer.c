/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * unit test for the baseaudiovisualizer class
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

#include <gst/gst.h>
#include <string.h>

#include "gstbaseaudiovisualizer.h"

/* dummy subclass for testing */

#define GST_TYPE_TEST_SCOPE            (gst_test_scope_get_type())
#define GST_TEST_SCOPE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TEST_SCOPE,GstTestScope))
#define GST_TEST_SCOPE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TEST_SCOPE,GstTestScopeClass))
typedef struct _GstTestScope GstTestScope;
typedef struct _GstTestScopeClass GstTestScopeClass;

struct _GstTestScope
{
  GstBaseAudioVisualizer parent;
};

struct _GstTestScopeClass
{
  GstBaseAudioVisualizerClass parent_class;
};

static GstStaticPadTemplate gst_test_scope_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate gst_test_scope_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );

static GType gst_test_scope_get_type (void);

GST_BOILERPLATE (GstTestScope, gst_test_scope, GstBaseAudioVisualizer,
    GST_TYPE_BASE_AUDIO_VISUALIZER);

static void
gst_test_scope_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "test scope",
      "Visualization",
      "Dummy test scope", "Stefan Kost <ensonic@users.sf.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_test_scope_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_test_scope_sink_template));
}

static void
gst_test_scope_class_init (GstTestScopeClass * g_class)
{
  /* do nothing */
}

static void
gst_test_scope_init (GstTestScope * scope, GstTestScopeClass * g_class)
{
  /* do nothing */
}

/* tests */

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, " "endianness = (int) BIG_ENDIAN, "
#if G_BYTE_ORDER == G_BIG_ENDIAN
        "red_mask = (int) 0xFF000000, "
        "green_mask = (int) 0x00FF0000, " "blue_mask = (int) 0x0000FF00, "
#else
        "red_mask = (int) 0x0000FF00, "
        "green_mask = (int) 0x00FF0000, " "blue_mask = (int) 0xFF000000, "
#endif
        "width = (int) 320, "
        "height = (int) 240, " "framerate = (fraction) 30/1")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) 44100, "
        "channels = (int) 2, "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true")
    );

GST_START_TEST (count_in_out)
{
  GstElement *elem;
  GstPad *srcpad, *sinkpad;
  GstBuffer *buffer;

  /* setup up */
  elem = gst_check_setup_element ("testscope");
  srcpad = gst_check_setup_src_pad (elem, &srctemplate, NULL);
  sinkpad = gst_check_setup_sink_pad (elem, &sinktemplate, NULL);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);
  fail_unless (gst_element_set_state (elem,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* push 1s audio to get 30 video-frames */
  buffer = gst_buffer_new_and_alloc (44100 * 2 * sizeof (gint16));
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (srcpad));
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (srcpad, buffer) == GST_FLOW_OK);
  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 30);

  /* clean up */
  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (elem);
  gst_check_teardown_sink_pad (elem);
  gst_check_teardown_element (elem);
}

GST_END_TEST;

static void
baseaudiovisualizer_init (void)
{
  gst_element_register (NULL, "testscope", GST_RANK_NONE, GST_TYPE_TEST_SCOPE);
}

static Suite *
baseaudiovisualizer_suite (void)
{
  Suite *s = suite_create ("baseaudiovisualizer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, baseaudiovisualizer_init, NULL);

  tcase_add_test (tc_chain, count_in_out);

  return s;
}


GST_CHECK_MAIN (baseaudiovisualizer);
