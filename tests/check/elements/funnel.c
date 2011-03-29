/* GStreamer unit tests for the funnel
 *
 * Copyright (C) 2008 Collabora, Nokia
 * @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>

struct TestData
{
  GstElement *funnel;
  GstPad *funnelsrc, *funnelsink11, *funnelsink22;
  GstPad *mysink, *mysrc1, *mysrc2;
  GstCaps *mycaps;
};

static void
setup_test_objects (struct TestData *td, GstPadChainFunction chain_func,
    GstPadBufferAllocFunction alloc_func)
{
  td->mycaps = gst_caps_new_simple ("test/test", NULL);

  td->funnel = gst_element_factory_make ("funnel", NULL);

  td->funnelsrc = gst_element_get_static_pad (td->funnel, "src");
  fail_unless (td->funnelsrc != NULL);

  td->funnelsink11 = gst_element_get_request_pad (td->funnel, "sink11");
  fail_unless (td->funnelsink11 != NULL);
  fail_unless (!strcmp (GST_OBJECT_NAME (td->funnelsink11), "sink11"));

  td->funnelsink22 = gst_element_get_request_pad (td->funnel, "sink22");
  fail_unless (td->funnelsink22 != NULL);
  fail_unless (!strcmp (GST_OBJECT_NAME (td->funnelsink22), "sink22"));

  fail_unless (gst_element_set_state (td->funnel, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);

  td->mysink = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (td->mysink, chain_func);
  gst_pad_set_bufferalloc_function (td->mysink, alloc_func);
  gst_pad_set_active (td->mysink, TRUE);
  gst_pad_set_caps (td->mysink, td->mycaps);

  td->mysrc1 = gst_pad_new ("src1", GST_PAD_SRC);
  gst_pad_set_active (td->mysrc1, TRUE);
  gst_pad_set_caps (td->mysrc1, td->mycaps);

  td->mysrc2 = gst_pad_new ("src2", GST_PAD_SRC);
  gst_pad_set_active (td->mysrc2, TRUE);
  gst_pad_set_caps (td->mysrc2, td->mycaps);

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td->funnelsrc,
              td->mysink)));

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td->mysrc1,
              td->funnelsink11)));

  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td->mysrc2,
              td->funnelsink22)));

}

static void
release_test_objects (struct TestData *td)
{
  gst_pad_set_active (td->mysink, FALSE);
  gst_pad_set_active (td->mysrc1, FALSE);
  gst_pad_set_active (td->mysrc1, FALSE);

  gst_object_unref (td->mysink);
  gst_object_unref (td->mysrc1);
  gst_object_unref (td->mysrc2);

  fail_unless (gst_element_set_state (td->funnel, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (td->funnelsrc);
  gst_object_unref (td->funnelsink11);
  gst_element_release_request_pad (td->funnel, td->funnelsink11);
  gst_object_unref (td->funnelsink22);
  gst_element_release_request_pad (td->funnel, td->funnelsink22);

  gst_caps_unref (td->mycaps);
  gst_object_unref (td->funnel);
}

static gint bufcount = 0;
static gint alloccount = 0;

static GstFlowReturn
chain_ok (GstPad * pad, GstBuffer * buffer)
{
  bufcount++;

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

static GstFlowReturn
alloc_ok (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buffer)
{
  alloccount++;

  fail_unless (buffer != NULL);
  fail_unless (*buffer == NULL);

  *buffer = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buffer, caps);
  GST_BUFFER_OFFSET (*buffer) = offset;

  return GST_FLOW_OK;
}

GST_START_TEST (test_funnel_simple)
{
  struct TestData td;
  GstBuffer *buf1 = NULL;
  GstBuffer *buf2 = NULL;

  setup_test_objects (&td, chain_ok, alloc_ok);

  bufcount = 0;
  alloccount = 0;

  fail_unless (gst_pad_push (td.mysrc1, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_push (td.mysrc2, gst_buffer_new ()) == GST_FLOW_OK);

  fail_unless (bufcount == 2);

  fail_unless (gst_pad_alloc_buffer (td.mysrc1, 0, 1024, td.mycaps,
          &buf1) == GST_FLOW_OK);
  fail_unless (gst_pad_alloc_buffer (td.mysrc2, 1024, 1024, td.mycaps,
          &buf2) == GST_FLOW_OK);

  fail_unless (alloccount == 2);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);

  release_test_objects (&td);
}

GST_END_TEST;

static Suite *
funnel_suite (void)
{
  Suite *s = suite_create ("funnel");
  TCase *tc_chain;
  GLogLevelFlags fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);

  tc_chain = tcase_create ("funnel simple");
  tcase_add_test (tc_chain, test_funnel_simple);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (funnel);
