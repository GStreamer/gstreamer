/* GStreamer
 *
 * Unit tests for basetransform collation/separation
 *
 * Copyright (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstbasetransform.h>

#include "test_transform.c"

GstBuffer *buf1, *buf2;

/* Output buffers are twice the size as input */
static gboolean
transform_size_collate (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize)
{
  if (direction == GST_PAD_SINK) {
    *othersize = size * 2;
  } else {
    *othersize = size / 2;
  }

  return TRUE;
}

static GstFlowReturn
collate_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * input)
{
  GstBaseTransformClass *tt_parent_class;
  GstFlowReturn ret;

  tt_parent_class =
      g_type_class_peek_parent (GST_BASE_TRANSFORM_GET_CLASS (trans));

  ret = tt_parent_class->submit_input_buffer (trans, is_discont, input);

  if (ret != GST_FLOW_OK)
    return ret;

  fail_unless (buf1 == NULL || buf2 == NULL);

  if (buf1 == NULL) {
    buf1 = trans->queued_buf;
    trans->queued_buf = NULL;
  } else if (buf2 == NULL) {
    buf2 = trans->queued_buf;
    trans->queued_buf = NULL;
  }

  return ret;
}

static GstFlowReturn
collate_generate_output (GstBaseTransform * trans, GstBuffer ** outbuf)
{
  /* Not ready to generate output unless we've collected 2 buffers */
  if (buf1 == NULL || buf2 == NULL)
    return GST_BASE_TRANSFORM_FLOW_DROPPED;

  fail_unless (buf1 != NULL && buf2 != NULL);
  *outbuf = gst_buffer_new_and_alloc (40);

  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
  buf1 = NULL;
  buf2 = NULL;

  return GST_FLOW_OK;
}

/* Take 2 input buffers, generate 1 output
 * buffer with twice the size
 */
GST_START_TEST (basetransform_chain_collate)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;
  GstCaps *incaps, *outcaps;

  src_template = &gst_test_trans_src_template;
  klass_passthrough_on_same_caps = FALSE;
  klass_transform_size = transform_size_collate;
  klass_submit_input_buffer = collate_submit_input_buffer;
  klass_generate_output = collate_generate_output;

  trans = gst_test_trans_new ();

  incaps = gst_caps_new_empty_simple ("foo/x-bar");
  outcaps = gst_caps_new_empty_simple ("foo/x-bar");

  gst_test_trans_push_segment (trans);

  gst_pad_push_event (trans->srcpad, gst_event_new_flush_start ());
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_stop (TRUE));

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);
  gst_test_trans_setcaps (trans, incaps);
  gst_test_trans_push_segment (trans);

  buffer = gst_buffer_new_and_alloc (20);
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);

  /* We do not expect an output buffer after only pushing one input */
  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer == NULL);

  buffer = gst_buffer_new_and_alloc (20);
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 40);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  gst_caps_unref (incaps);
  gst_caps_unref (outcaps);

  gst_test_trans_free (trans);
}

GST_END_TEST;


static Suite *
gst_basetransform_collate_suite (void)
{
  Suite *s = suite_create ("GstBaseTransformCollate");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, basetransform_chain_collate);

  return s;
}

GST_CHECK_MAIN (gst_basetransform_collate);
