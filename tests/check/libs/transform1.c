/* GStreamer
 *
 * some unit tests for GstBaseTransform
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstbasetransform.h>

#undef FAILING_TESTS

#include "test_transform.c"

/* basic passthrough, we don't have any transform functions so we can only
 * perform passthrough. We also don't have caps, which is fine */
GST_START_TEST (basetransform_chain_pt1)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;

  trans = gst_test_trans_new ();

  buffer = gst_buffer_new_and_alloc (20);

  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  /* caps should not have been set */
  fail_unless (GST_BUFFER_CAPS (buffer) == NULL);

  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (10);
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  /* caps should not have been set */
  fail_unless (GST_BUFFER_CAPS (buffer) == NULL);

  gst_buffer_unref (buffer);

  gst_test_trans_free (trans);
}

GST_END_TEST;

/* basic passthrough, we don't have any transform functions so we can only
 * perform passthrough with same caps */
GST_START_TEST (basetransform_chain_pt2)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstCaps *caps;
  GstFlowReturn res;

  trans = gst_test_trans_new ();

  /* first buffer */
  caps = gst_caps_new_simple ("foo/x-bar", NULL);

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, caps);

  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (GST_BUFFER_CAPS (buffer) == caps);

  gst_buffer_unref (buffer);
  gst_caps_unref (caps);

  /* second buffer, renegotiates, keeps extra type arg in caps */
  caps = gst_caps_new_simple ("foo/x-bar", "type", G_TYPE_INT, 1, NULL);

  buffer = gst_buffer_new_and_alloc (10);
  gst_buffer_set_caps (buffer, caps);

  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  fail_unless (GST_BUFFER_CAPS (buffer) == caps);

  gst_buffer_unref (buffer);
  gst_caps_unref (caps);

  gst_test_trans_free (trans);
}

GST_END_TEST;

static gboolean transform_ip_1_called;
static gboolean transform_ip_1_writable;

static GstFlowReturn
transform_ip_1 (GstBaseTransform * trans, GstBuffer * buf)
{
  GST_DEBUG_OBJECT (trans, "transform called");

  transform_ip_1_called = TRUE;
  transform_ip_1_writable = gst_buffer_is_writable (buf);

  GST_DEBUG_OBJECT (trans, "writable: %d", transform_ip_1_writable);

  return GST_FLOW_OK;
}

/* basic in-place, check if the _ip function is called, buffer should
 * be writable. no setcaps is set */
GST_START_TEST (basetransform_chain_ip1)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;

  klass_transform_ip = transform_ip_1;
  trans = gst_test_trans_new ();

  buffer = gst_buffer_new_and_alloc (20);

  transform_ip_1_called = FALSE;;
  transform_ip_1_writable = TRUE;;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  transform_ip_1_called = FALSE;;
  transform_ip_1_writable = FALSE;;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  /* copy should have been taken */
  fail_unless (transform_ip_1_writable == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  gst_test_trans_free (trans);
}

GST_END_TEST;

static gboolean set_caps_1_called;

static gboolean
set_caps_1 (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GstCaps *caps;

  GST_DEBUG_OBJECT (trans, "set_caps called");

  set_caps_1_called = TRUE;

  caps = gst_caps_new_simple ("foo/x-bar", NULL);

  fail_unless (gst_caps_is_equal (incaps, caps));
  fail_unless (gst_caps_is_equal (outcaps, caps));

  gst_caps_unref (caps);

  return TRUE;
}

/* basic in-place, check if the _ip function is called, buffer should be
 * writable. we also set a setcaps function and see if it's called. */
GST_START_TEST (basetransform_chain_ip2)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;
  GstCaps *caps;

  klass_transform_ip = transform_ip_1;
  klass_set_caps = set_caps_1;

  trans = gst_test_trans_new ();

  caps = gst_caps_new_simple ("foo/x-bar", NULL);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  transform_ip_1_called = FALSE;;
  transform_ip_1_writable = FALSE;;
  set_caps_1_called = FALSE;;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ip_1_called == FALSE);
  fail_unless (transform_ip_1_writable == FALSE);
  fail_unless (set_caps_1_called == FALSE);

  /* try to push a buffer with caps */
  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, caps);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  set_caps_1_called = FALSE;;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);
  fail_unless (set_caps_1_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (GST_BUFFER_CAPS (buffer) == caps);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, caps);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  transform_ip_1_called = FALSE;;
  transform_ip_1_writable = FALSE;;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (GST_BUFFER_CAPS (buffer) == caps);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  gst_caps_unref (caps);

  trans->klass->transform_ip = NULL;
  gst_test_trans_free (trans);
}

GST_END_TEST;

static Suite *
gst_basetransform_suite (void)
{
  Suite *s = suite_create ("GstBaseTransform");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, basetransform_chain_pt1);
  tcase_add_test (tc, basetransform_chain_pt2);
  tcase_add_test (tc, basetransform_chain_ip1);
  tcase_add_test (tc, basetransform_chain_ip2);

  return s;
}

GST_CHECK_MAIN (gst_basetransform);
