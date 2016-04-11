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

static gboolean set_caps_pt1_called;

static gboolean
set_caps_pt1 (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GST_DEBUG_OBJECT (trans, "set_caps called");

  set_caps_pt1_called = TRUE;

  return TRUE;
}

/* basic passthrough, we don't have any transform functions so we can only
 * perform passthrough. We also don't have caps, which is fine */
GST_START_TEST (basetransform_chain_pt1)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;
  GstCaps *caps;

  klass_set_caps = set_caps_pt1;
  trans = gst_test_trans_new ();

  gst_test_trans_push_segment (trans);

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);

  set_caps_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (set_caps_pt1_called == FALSE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 10");

  buffer = gst_buffer_new_and_alloc (10);
  set_caps_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (set_caps_pt1_called == FALSE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 10);

  gst_buffer_unref (buffer);

  gst_pad_push_event (trans->srcpad, gst_event_new_flush_start ());
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_stop (TRUE));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  set_caps_pt1_called = FALSE;
  gst_test_trans_setcaps (trans, caps);
  fail_unless (set_caps_pt1_called == TRUE);
  gst_caps_unref (caps);

  gst_test_trans_push_segment (trans);

  gst_test_trans_free (trans);

  klass_transform_ip = NULL;
  klass_transform = NULL;
  klass_transform_caps = NULL;
  klass_transform_size = NULL;
  klass_set_caps = NULL;
  klass_submit_input_buffer = NULL;
  klass_generate_output = NULL;
}

GST_END_TEST;

static gboolean set_caps_pt2_called;

static gboolean
set_caps_pt2 (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GST_DEBUG_OBJECT (trans, "set_caps called");

  set_caps_pt2_called = TRUE;

  fail_unless (gst_caps_is_equal (incaps, outcaps));

  return TRUE;
}

/* basic passthrough, we don't have any transform functions so we can only
 * perform passthrough with same caps */
GST_START_TEST (basetransform_chain_pt2)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstCaps *caps;
  GstFlowReturn res;

  klass_set_caps = set_caps_pt2;
  trans = gst_test_trans_new ();

  /* first buffer */
  set_caps_pt2_called = FALSE;
  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_test_trans_setcaps (trans, caps);
  gst_test_trans_push_segment (trans);

  GST_DEBUG_OBJECT (trans, "buffer with caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);

  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (set_caps_pt2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  gst_buffer_unref (buffer);

  gst_caps_unref (caps);

  /* second buffer, renegotiates, keeps extra type arg in caps */
  caps = gst_caps_new_simple ("foo/x-bar", "type", G_TYPE_INT, 1, NULL);
  set_caps_pt2_called = FALSE;
  gst_test_trans_setcaps (trans, caps);

  GST_DEBUG_OBJECT (trans, "buffer with caps, size 10");

  buffer = gst_buffer_new_and_alloc (10);

  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (set_caps_pt2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 10);

  gst_buffer_unref (buffer);

  gst_caps_unref (caps);

  /* with caps that is a superset */
  caps = gst_caps_new_empty_simple ("foo/x-bar");
  set_caps_pt2_called = FALSE;
  gst_test_trans_setcaps (trans, caps);

  GST_DEBUG_OBJECT (trans, "buffer with caps, size 10");

  buffer = gst_buffer_new_and_alloc (10);

  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (set_caps_pt2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 10);

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

  gst_test_trans_push_segment (trans);

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = TRUE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);
  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer without caps extra ref, size 20");

  buffer = gst_buffer_new_and_alloc (20);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  /* copy should have been taken with pad-alloc */
  fail_unless (transform_ip_1_writable == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc without caps, size 20");

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

  caps = gst_caps_new_empty_simple ("foo/x-bar");

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

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_test_trans_push_segment (trans);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 20");

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  set_caps_1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ip_1_called == FALSE);
  fail_unless (transform_ip_1_writable == FALSE);
  fail_unless (set_caps_1_called == FALSE);

  /* try to push a buffer with caps */
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_start ());
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_stop (TRUE));

  set_caps_1_called = FALSE;
  gst_test_trans_setcaps (trans, caps);
  gst_test_trans_push_segment (trans);

  GST_DEBUG_OBJECT (trans, "buffer with caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);
  fail_unless (set_caps_1_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);
  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer with caps extra ref, size 20");

  buffer = gst_buffer_new_and_alloc (20);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  gst_caps_unref (caps);

  gst_test_trans_free (trans);
}

GST_END_TEST;

static GstStaticPadTemplate sink_template_ct1 = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("baz/x-foo")
    );

static gboolean set_caps_ct1_called;

static gboolean
set_caps_ct1 (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GstCaps *caps1, *caps2;

  GST_DEBUG_OBJECT (trans, "set_caps called");

  caps1 = gst_caps_new_empty_simple ("baz/x-foo");
  caps2 = gst_caps_new_empty_simple ("foo/x-bar");

  fail_unless (gst_caps_is_equal (incaps, caps1));
  fail_unless (gst_caps_is_equal (outcaps, caps2));

  set_caps_ct1_called = TRUE;

  gst_caps_unref (caps1);
  gst_caps_unref (caps2);

  return TRUE;
}

static gboolean transform_ct1_called;
static gboolean transform_ct1_writable;

static GstFlowReturn
transform_ct1 (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  transform_ct1_called = TRUE;
  transform_ct1_writable = gst_buffer_is_writable (out);

  GST_DEBUG_OBJECT (trans, "writable: %d", transform_ct1_writable);

  return GST_FLOW_OK;
}

static GstCaps *
transform_caps_ct1 (GstBaseTransform * trans, GstPadDirection dir,
    GstCaps * caps, GstCaps * filter)
{
  GstCaps *res;

  if (dir == GST_PAD_SINK) {
    res = gst_caps_new_empty_simple ("foo/x-bar");
  } else {
    res = gst_caps_new_empty_simple ("baz/x-foo");
  }

  if (filter) {
    GstCaps *temp =
        gst_caps_intersect_full (filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = temp;
  }

  return res;
}

static gboolean
transform_size_ct1 (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize)
{
  if (direction == GST_PAD_SINK) {
    *othersize = size * 2;
  } else {
    *othersize = size / 2;
  }

  return TRUE;
}

/* basic copy-transform, check if the transform function is called,
 * buffer should be writable. we also set a setcaps function and
 * see if it's called. */
GST_START_TEST (basetransform_chain_ct1)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;
  GstCaps *incaps, *outcaps;

  sink_template = &sink_template_ct1;
  klass_transform = transform_ct1;
  klass_set_caps = set_caps_ct1;
  klass_transform_caps = transform_caps_ct1;
  klass_transform_size = transform_size_ct1;

  trans = gst_test_trans_new ();

  incaps = gst_caps_new_empty_simple ("baz/x-foo");
  outcaps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_test_trans_push_segment (trans);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps");

  transform_ct1_called = FALSE;
  transform_ct1_writable = FALSE;
  set_caps_ct1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ct1_called == FALSE);
  fail_unless (transform_ct1_writable == FALSE);
  fail_unless (set_caps_ct1_called == FALSE);

  /* try to push a buffer with caps */
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_start ());
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_stop (TRUE));

  set_caps_ct1_called = FALSE;
  gst_test_trans_setcaps (trans, incaps);
  gst_test_trans_push_segment (trans);

  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct1_called = FALSE;
  transform_ct1_writable = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct1_called == TRUE);
  fail_unless (transform_ct1_writable == TRUE);
  fail_unless (set_caps_ct1_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 40);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct1_called = FALSE;
  transform_ct1_writable = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct1_called == TRUE);
  fail_unless (transform_ct1_writable == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

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

static GstStaticPadTemplate src_template_ct2 = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("baz/x-foo; foo/x-bar")
    );

static gint set_caps_ct2_case;
static gboolean set_caps_ct2_called;

static gboolean
set_caps_ct2 (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GstCaps *caps1, *caps2;

  GST_DEBUG_OBJECT (trans, "set_caps called");

  caps1 = gst_caps_new_empty_simple ("foo/x-bar");

  if (set_caps_ct2_case == 1)
    caps2 = gst_caps_copy (caps1);
  else
    caps2 = gst_caps_new_empty_simple ("baz/x-foo");

  fail_unless (gst_caps_is_equal (incaps, caps1));
  fail_unless (gst_caps_is_equal (outcaps, caps2));

  set_caps_ct2_called = TRUE;

  gst_caps_unref (caps1);
  gst_caps_unref (caps2);

  return TRUE;
}

static gboolean transform_ct2_called;
static gboolean transform_ct2_writable;

static GstFlowReturn
transform_ct2 (GstBaseTransform * trans, GstBuffer * in, GstBuffer * out)
{
  transform_ct2_called = TRUE;
  transform_ct2_writable = gst_buffer_is_writable (out);

  GST_DEBUG_OBJECT (trans, "writable: %d", transform_ct2_writable);

  return GST_FLOW_OK;
}

static GstCaps *
transform_caps_ct2 (GstBaseTransform * trans, GstPadDirection dir,
    GstCaps * caps, GstCaps * filter)
{
  GstCaps *res;

  if (dir == GST_PAD_SINK) {
    /* everything on the sinkpad can be transformed to the output formats */
    if (set_caps_ct2_case == 1)
      res = gst_caps_new_empty_simple ("foo/x-bar");
    else
      res = gst_caps_new_empty_simple ("baz/x-foo");
  } else {
    /* all on the srcpad can be transformed to the format of the sinkpad */
    res = gst_caps_new_empty_simple ("foo/x-bar");
  }

  if (filter) {
    GstCaps *temp =
        gst_caps_intersect_full (filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = temp;
  }

  return res;
}

static gboolean
transform_size_ct2 (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize)
{
  if (gst_caps_is_equal (caps, othercaps)) {
    *othersize = size;
  } else {
    if (direction == GST_PAD_SINK) {
      *othersize = size * 2;
    } else {
      *othersize = size / 2;
    }
  }

  return TRUE;
}

/* basic copy-transform, check if the transform function is called,
 * buffer should be writable. we also set a setcaps function and
 * see if it's called. */
GST_START_TEST (basetransform_chain_ct2)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;
  GstCaps *incaps, *outcaps;

  src_template = &src_template_ct2;
  klass_transform = transform_ct2;
  klass_set_caps = set_caps_ct2;
  klass_transform_caps = transform_caps_ct2;
  klass_transform_size = transform_size_ct2;

  trans = gst_test_trans_new ();

  incaps = gst_caps_new_empty_simple ("foo/x-bar");
  outcaps = gst_caps_new_empty_simple ("baz/x-foo");

  gst_test_trans_push_segment (trans);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps");

  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  set_caps_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ct2_called == FALSE);
  fail_unless (transform_ct2_writable == FALSE);
  fail_unless (set_caps_ct2_called == FALSE);


  /* try to push a buffer with caps */
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_start ());
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_stop (TRUE));

  set_caps_ct2_case = 1;
  set_caps_ct2_called = FALSE;
  gst_test_trans_setcaps (trans, incaps);
  gst_test_trans_push_segment (trans);

  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);
  fail_unless (transform_ct2_writable == TRUE);
  fail_unless (set_caps_ct2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);
  fail_unless (transform_ct2_writable == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  gst_caps_unref (incaps);
  gst_caps_unref (outcaps);

  gst_test_trans_free (trans);
}

GST_END_TEST;

/* basic copy-transform, we work in passthrough here. */
GST_START_TEST (basetransform_chain_ct3)
{
  TestTransData *trans;
  GstBuffer *buffer;
  GstFlowReturn res;
  GstCaps *incaps, *outcaps;

  src_template = &src_template_ct2;
  klass_passthrough_on_same_caps = TRUE;
  klass_transform = transform_ct2;
  klass_set_caps = set_caps_ct2;
  klass_transform_caps = transform_caps_ct2;
  klass_transform_size = transform_size_ct2;

  trans = gst_test_trans_new ();

  incaps = gst_caps_new_empty_simple ("foo/x-bar");
  outcaps = gst_caps_new_empty_simple ("baz/x-foo");

  /* with passthrough caps */
  gst_test_trans_push_segment (trans);
  GST_DEBUG_OBJECT (trans, "alloc size 20, with passthrough caps %"
      GST_PTR_FORMAT, incaps);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps");

  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  set_caps_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ct2_called == FALSE);
  fail_unless (transform_ct2_writable == FALSE);
  fail_unless (set_caps_ct2_called == FALSE);

  /* try to push a buffer with caps */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  gst_pad_push_event (trans->srcpad, gst_event_new_flush_start ());
  gst_pad_push_event (trans->srcpad, gst_event_new_flush_stop (TRUE));

  set_caps_ct2_case = 1;
  set_caps_ct2_called = FALSE;
  gst_test_trans_setcaps (trans, incaps);
  gst_test_trans_push_segment (trans);

  transform_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == FALSE);
  fail_unless (set_caps_ct2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == FALSE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* change the return value of the buffer-alloc function */
  GST_DEBUG_OBJECT (trans, "switching transform output");

  GST_DEBUG_OBJECT (trans,
      "buffer with in passthrough with caps %" GST_PTR_FORMAT, incaps);
  buffer = gst_buffer_new_and_alloc (10);

  /* don't suggest anything else */
  set_caps_ct2_case = 2;
  gst_pad_push_event (trans->sinkpad, gst_event_new_reconfigure ());
  transform_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);
  buffer = gst_buffer_new_and_alloc (10);

  /* don't suggest anything else */
  transform_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);
  /* after push, get rid of the final ref we had */

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (gst_buffer_get_size (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  gst_caps_unref (incaps);
  gst_caps_unref (outcaps);

  gst_test_trans_free (trans);
}

GST_END_TEST;

static void
transform1_setup (void)
{
  sink_template = &gst_test_trans_sink_template;
  src_template = &gst_test_trans_src_template;
}

static void
transform1_teardown (void)
{
  /* reset global state */
  klass_transform_ip = NULL;
  klass_transform = NULL;
  klass_transform_caps = NULL;
  klass_transform_size = NULL;
  klass_set_caps = NULL;
  klass_submit_input_buffer = NULL;
  klass_generate_output = NULL;
}

static Suite *
gst_basetransform_suite (void)
{
  Suite *s = suite_create ("GstBaseTransform");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_checked_fixture (tc, transform1_setup, transform1_teardown);

  /* pass through */
  tcase_add_test (tc, basetransform_chain_pt1);
  tcase_add_test (tc, basetransform_chain_pt2);
  /* in place */
  tcase_add_test (tc, basetransform_chain_ip1);
  tcase_add_test (tc, basetransform_chain_ip2);
  /* copy transform */
  tcase_add_test (tc, basetransform_chain_ct1);
  tcase_add_test (tc, basetransform_chain_ct2);
  tcase_add_test (tc, basetransform_chain_ct3);

  return s;
}

GST_CHECK_MAIN (gst_basetransform);
