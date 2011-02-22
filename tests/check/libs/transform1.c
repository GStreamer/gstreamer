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

#include "test_transform.c"

static gboolean buffer_alloc_pt1_called;

static GstFlowReturn
buffer_alloc_pt1 (GstPad * pad, guint64 offset, guint size, GstCaps * caps,
    GstBuffer ** buf)
{
  GST_DEBUG_OBJECT (pad, "buffer_alloc called %" G_GUINT64_FORMAT ", %u, %"
      GST_PTR_FORMAT, offset, size, caps);

  buffer_alloc_pt1_called = TRUE;

  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, caps);

  return GST_FLOW_OK;
}

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
  trans->buffer_alloc = buffer_alloc_pt1;

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);

  buffer_alloc_pt1_called = FALSE;
  set_caps_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  /* FIXME, passthough without pad-alloc, do pad-alloc on the srcpad */
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt1_called == FALSE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  /* caps should not have been set */
  fail_unless (GST_BUFFER_CAPS (buffer) == NULL);

  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 10");

  buffer = gst_buffer_new_and_alloc (10);
  buffer_alloc_pt1_called = FALSE;
  set_caps_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  /* FIXME, passthough without pad-alloc, do pad-alloc on the srcpad */
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt1_called == FALSE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  /* caps should not have been set */
  fail_unless (GST_BUFFER_CAPS (buffer) == NULL);

  gst_buffer_unref (buffer);

  /* proxy buffer-alloc without caps */
  GST_DEBUG_OBJECT (trans, "alloc without caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  set_caps_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, NULL, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt1_called == FALSE);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 10");

  caps = gst_caps_new_simple ("foo/x-bar", NULL);
  buffer_alloc_pt1_called = FALSE;
  set_caps_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt1_called == FALSE);
  gst_buffer_unref (buffer);

  /* once more */
  buffer_alloc_pt1_called = FALSE;
  set_caps_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt1_called == FALSE);
  gst_buffer_unref (buffer);

  gst_caps_unref (caps);

  gst_test_trans_free (trans);
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
  trans->buffer_alloc = buffer_alloc_pt1;

  /* first buffer */
  caps = gst_caps_new_simple ("foo/x-bar", NULL);

  GST_DEBUG_OBJECT (trans, "buffer with caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, caps);

  buffer_alloc_pt1_called = FALSE;
  set_caps_pt2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  /* FIXME, passthough without pad-alloc, do pad-alloc on the srcpad */
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), caps));

  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  set_caps_pt2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt2_called == FALSE);
  gst_buffer_unref (buffer);

  gst_caps_unref (caps);

  /* second buffer, renegotiates, keeps extra type arg in caps */
  caps = gst_caps_new_simple ("foo/x-bar", "type", G_TYPE_INT, 1, NULL);

  GST_DEBUG_OBJECT (trans, "buffer with caps, size 10");

  buffer = gst_buffer_new_and_alloc (10);
  gst_buffer_set_caps (buffer, caps);

  buffer_alloc_pt1_called = FALSE;
  set_caps_pt2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  /* FIXME, passthough without pad-alloc, do pad-alloc on the srcpad */
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), caps));

  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  set_caps_pt2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt2_called == FALSE);
  gst_buffer_unref (buffer);

  gst_caps_unref (caps);

  /* with caps that is a superset */
  caps = gst_caps_new_simple ("foo/x-bar", NULL);

  GST_DEBUG_OBJECT (trans, "alloc with superset caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  set_caps_pt2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (set_caps_pt2_called == FALSE);
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
  trans->buffer_alloc = buffer_alloc_pt1;

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = TRUE;
  buffer_alloc_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);
  /* FIXME, in-place without pad-alloc, do pad-alloc on the srcpad */
  fail_unless (buffer_alloc_pt1_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer without caps extra ref, size 20");

  buffer = gst_buffer_new_and_alloc (20);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  buffer_alloc_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  /* copy should have been taken with pad-alloc */
  fail_unless (transform_ip_1_writable == TRUE);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc without caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, NULL, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
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
  trans->buffer_alloc = buffer_alloc_pt1;

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc without caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, NULL, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (GST_BUFFER_CAPS (buffer) == NULL);
  gst_buffer_unref (buffer);

  caps = gst_caps_new_simple ("foo/x-bar", NULL);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (GST_BUFFER_CAPS (buffer) == caps);
  gst_buffer_unref (buffer);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps, size 20");

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  buffer_alloc_pt1_called = FALSE;
  set_caps_1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ip_1_called == FALSE);
  fail_unless (transform_ip_1_writable == FALSE);
  fail_unless (set_caps_1_called == FALSE);
  fail_unless (buffer_alloc_pt1_called == FALSE);

  /* try to push a buffer with caps */
  GST_DEBUG_OBJECT (trans, "buffer with caps, size 20");

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, caps);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  set_caps_1_called = FALSE;
  buffer_alloc_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);
  fail_unless (set_caps_1_called == TRUE);
  /* FIXME, in-place without pad-alloc, do pad-alloc on the srcpad */
  fail_unless (buffer_alloc_pt1_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), caps));
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer with caps extra ref, size 20");

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, caps);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  transform_ip_1_called = FALSE;
  transform_ip_1_writable = FALSE;
  buffer_alloc_pt1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ip_1_called == TRUE);
  fail_unless (transform_ip_1_writable == TRUE);
  fail_unless (buffer_alloc_pt1_called == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), caps));

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 20");

  buffer_alloc_pt1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, caps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_pt1_called == TRUE);
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

  caps1 = gst_caps_new_simple ("baz/x-foo", NULL);
  caps2 = gst_caps_new_simple ("foo/x-bar", NULL);

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
    GstCaps * caps)
{
  GstCaps *res;

  if (dir == GST_PAD_SINK) {
    res = gst_caps_new_simple ("foo/x-bar", NULL);
  } else {
    res = gst_caps_new_simple ("baz/x-foo", NULL);
  }
  return res;
}

static gboolean
transform_size_ct1 (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, guint size, GstCaps * othercaps, guint * othersize)
{
  if (direction == GST_PAD_SINK) {
    *othersize = size * 2;
  } else {
    *othersize = size / 2;
  }

  return TRUE;
}

gboolean buffer_alloc_ct1_called;

static GstFlowReturn
buffer_alloc_ct1 (GstPad * pad, guint64 offset, guint size, GstCaps * caps,
    GstBuffer ** buf)
{
  GstCaps *outcaps;

  GST_DEBUG_OBJECT (pad, "buffer_alloc called %" G_GUINT64_FORMAT ", %u, %"
      GST_PTR_FORMAT, offset, size, caps);

  buffer_alloc_ct1_called = TRUE;

  outcaps = gst_caps_new_simple ("foo/x-bar", NULL);
  fail_unless (gst_caps_is_equal (outcaps, caps));
  gst_caps_unref (outcaps);

  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, caps);

  return GST_FLOW_OK;
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
  trans->buffer_alloc = buffer_alloc_ct1;

  incaps = gst_caps_new_simple ("baz/x-foo", NULL);
  outcaps = gst_caps_new_simple ("foo/x-bar", NULL);

#if 0
  /* without caps buffer, I think this should fail */
  GST_DEBUG_OBJECT (trans, "alloc without caps, size 20");

  buffer_alloc_ct1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, NULL, &buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  /* should not call pad-alloc because the caps and sizes are different */
  fail_unless (buffer_alloc_ct1_called == FALSE);
#endif

  /* with wrong (unsupported) caps */
  GST_DEBUG_OBJECT (trans, "alloc with wrong caps, size 20");

  buffer_alloc_ct1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, outcaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_if (buffer == NULL);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);
  /* FIXME, why would this call the alloc function? we try to alloc something
   * with caps that are not supported on the sinkpad */
  fail_unless (buffer_alloc_ct1_called == FALSE);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 20");

  buffer_alloc_ct1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, incaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  /* should not call pad-alloc because the caps and sizes are different */
  fail_unless (buffer_alloc_ct1_called == FALSE);
  gst_buffer_unref (buffer);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps");

  transform_ct1_called = FALSE;
  transform_ct1_writable = FALSE;
  set_caps_ct1_called = FALSE;
  buffer_alloc_ct1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ct1_called == FALSE);
  fail_unless (transform_ct1_writable == FALSE);
  fail_unless (set_caps_ct1_called == FALSE);
  fail_unless (buffer_alloc_ct1_called == FALSE);

  /* try to push a buffer with caps */
  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, incaps);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct1_called = FALSE;
  transform_ct1_writable = FALSE;
  set_caps_ct1_called = FALSE;
  buffer_alloc_ct1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct1_called == TRUE);
  fail_unless (transform_ct1_writable == TRUE);
  fail_unless (set_caps_ct1_called == TRUE);
  fail_unless (buffer_alloc_ct1_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 40);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), outcaps));
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, incaps);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct1_called = FALSE;
  transform_ct1_writable = FALSE;
  buffer_alloc_ct1_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct1_called == TRUE);
  fail_unless (transform_ct1_writable == TRUE);
  fail_unless (buffer_alloc_ct1_called == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 40);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), outcaps));

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 10");

  buffer_alloc_ct1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, incaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  /* should not call pad-alloc because the caps and sizes are different, it
   * currently still calls the pad alloc for no reason and then throws away the
   * buffer. */
  fail_unless (buffer_alloc_ct1_called == FALSE);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with wrong caps, size 10");

  buffer_alloc_ct1_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, outcaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_if (buffer == NULL);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);
  /* should not call the pad-alloc function */
  fail_unless (buffer_alloc_ct1_called == FALSE);

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

  caps1 = gst_caps_new_simple ("foo/x-bar", NULL);

  if (set_caps_ct2_case == 1)
    caps2 = gst_caps_copy (caps1);
  else
    caps2 = gst_caps_new_simple ("baz/x-foo", NULL);

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
    GstCaps * caps)
{
  GstCaps *res;

  if (dir == GST_PAD_SINK) {
    /* everything on the sinkpad can be transformed to the output formats */
    res = gst_caps_from_string ("foo/x-bar;baz/x-foo");
  } else {
    /* all on the srcpad can be transformed to the format of the sinkpad */
    res = gst_caps_new_simple ("foo/x-bar", NULL);
  }
  return res;
}

static gboolean
transform_size_ct2 (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps, guint size, GstCaps * othercaps, guint * othersize)
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

static gint buffer_alloc_ct2_case;
static gboolean buffer_alloc_ct2_called;
static gboolean buffer_alloc_ct2_suggest;

static GstFlowReturn
buffer_alloc_ct2 (GstPad * pad, guint64 offset, guint size, GstCaps * caps,
    GstBuffer ** buf)
{
  GstCaps *incaps, *outcaps;

  GST_DEBUG_OBJECT (pad, "buffer_alloc called %" G_GUINT64_FORMAT ", %u, %"
      GST_PTR_FORMAT, offset, size, caps);

  buffer_alloc_ct2_called = TRUE;

  if (buffer_alloc_ct2_case == 1) {
    incaps = gst_caps_new_simple ("foo/x-bar", NULL);
    if (buffer_alloc_ct2_suggest) {
      outcaps = gst_caps_new_simple ("baz/x-foo", NULL);
      size *= 2;
    } else
      outcaps = gst_caps_ref (incaps);
  } else {
    incaps = gst_caps_new_simple ("baz/x-foo", NULL);
    if (buffer_alloc_ct2_suggest) {
      outcaps = gst_caps_new_simple ("foo/x-bar", NULL);
      size /= 2;
    } else
      outcaps = gst_caps_ref (incaps);
  }
  GST_DEBUG_OBJECT (pad, "expect %" GST_PTR_FORMAT, incaps);

  fail_unless (gst_caps_is_equal (caps, incaps));

  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, outcaps);

  GST_DEBUG_OBJECT (pad, "return buffer of size %u, caps %" GST_PTR_FORMAT,
      size, outcaps);

  gst_caps_unref (outcaps);
  gst_caps_unref (incaps);

  return GST_FLOW_OK;
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
  trans->buffer_alloc = buffer_alloc_ct2;

  incaps = gst_caps_new_simple ("foo/x-bar", NULL);
  outcaps = gst_caps_new_simple ("baz/x-foo", NULL);

#if 0
  /* without caps buffer, I think this should fail */
  GST_DEBUG_OBJECT (trans, "alloc without caps, size 20");

  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, NULL, &buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  /* should not call pad-alloc because the caps and sizes are different */
  fail_unless (buffer_alloc_ct2_called == FALSE);
#endif

  /* with passthrough caps */
  GST_DEBUG_OBJECT (trans, "alloc size 20, with passthrough caps %"
      GST_PTR_FORMAT, incaps);

  buffer_alloc_ct2_case = 1;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, incaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc size 20, with wrong caps %" GST_PTR_FORMAT,
      outcaps);

  buffer_alloc_ct2_case = 2;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, outcaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_if (buffer == NULL);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);
  /* should not call pad-alloc because the caps and sizes are different */
  fail_unless (buffer_alloc_ct2_called == FALSE);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps");

  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  set_caps_ct2_called = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ct2_called == FALSE);
  fail_unless (transform_ct2_writable == FALSE);
  fail_unless (set_caps_ct2_called == FALSE);
  fail_unless (buffer_alloc_ct2_called == FALSE);

  /* try to push a buffer with caps */
  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, incaps);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  buffer_alloc_ct2_case = 1;
  set_caps_ct2_case = 1;
  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  set_caps_ct2_called = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);
  fail_unless (transform_ct2_writable == TRUE);
  fail_unless (set_caps_ct2_called == TRUE);
  fail_unless (buffer_alloc_ct2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, incaps);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);
  fail_unless (transform_ct2_writable == TRUE);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 10");

  buffer_alloc_ct2_case = 1;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, incaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with wrong caps, size 10");

  buffer_alloc_ct2_case = 2;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, outcaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_if (buffer == NULL);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);
  /* should not call the pad-alloc function */
  fail_unless (buffer_alloc_ct2_called == FALSE);

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
  trans->buffer_alloc = buffer_alloc_ct2;

  incaps = gst_caps_new_simple ("foo/x-bar", NULL);
  outcaps = gst_caps_new_simple ("baz/x-foo", NULL);

#if 0
  /* without caps buffer, I think this should fail */
  GST_DEBUG_OBJECT (trans, "alloc without caps, size 20");

  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, NULL, &buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  /* should not call pad-alloc because the caps and sizes are different */
  fail_unless (buffer_alloc_ct2_called == FALSE);
#endif

  /* with passthrough caps */
  GST_DEBUG_OBJECT (trans, "alloc size 20, with passthrough caps %"
      GST_PTR_FORMAT, incaps);

  buffer_alloc_ct2_case = 1;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, incaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc size 20, with wrong caps %" GST_PTR_FORMAT,
      outcaps);

  buffer_alloc_ct2_case = 2;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 20, outcaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_if (buffer == NULL);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);
  /* should not call pad-alloc because the caps and sizes are different */
  fail_unless (buffer_alloc_ct2_called == FALSE);

  /* first try to push a buffer without caps, this should fail */
  buffer = gst_buffer_new_and_alloc (20);

  GST_DEBUG_OBJECT (trans, "buffer without caps");

  transform_ct2_called = FALSE;
  transform_ct2_writable = FALSE;
  set_caps_ct2_called = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_NOT_NEGOTIATED);
  fail_unless (transform_ct2_called == FALSE);
  fail_unless (transform_ct2_writable == FALSE);
  fail_unless (set_caps_ct2_called == FALSE);
  fail_unless (buffer_alloc_ct2_called == FALSE);

  /* try to push a buffer with caps */
  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, incaps);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  buffer_alloc_ct2_case = 1;
  set_caps_ct2_case = 1;
  transform_ct2_called = FALSE;
  set_caps_ct2_called = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == FALSE);
  fail_unless (set_caps_ct2_called == TRUE);
  fail_unless (buffer_alloc_ct2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);

  buffer = gst_buffer_new_and_alloc (20);
  gst_buffer_set_caps (buffer, incaps);
  /* take additional ref to make it non-writable */
  gst_buffer_ref (buffer);

  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 2);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);

  transform_ct2_called = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == FALSE);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  /* after push, get rid of the final ref we had */
  gst_buffer_unref (buffer);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 10");

  buffer_alloc_ct2_case = 1;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, incaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with wrong caps, size 10");

  buffer_alloc_ct2_case = 2;
  buffer_alloc_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, outcaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_if (buffer == NULL);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  /* if we don't push here, basetransform will think it doesn't need do a
   * pad alloc for downstream caps suggestions */
  res = gst_test_trans_push (trans, buffer);
  buffer = gst_test_trans_pop (trans);
  gst_buffer_unref (buffer);
  /* FIXME should not call the pad-alloc function but it currently does */
  fail_unless (buffer_alloc_ct2_called == FALSE);

  /* change the return value of the buffer-alloc function */
  GST_DEBUG_OBJECT (trans, "switching transform output");
  buffer_alloc_ct2_suggest = TRUE;

  GST_DEBUG_OBJECT (trans,
      "buffer with in passthrough with caps %" GST_PTR_FORMAT, incaps);
  buffer = gst_buffer_new_and_alloc (10);
  gst_buffer_set_caps (buffer, incaps);

  /* don't suggest anything else */
  buffer_alloc_ct2_case = 1;
  set_caps_ct2_case = 2;
  transform_ct2_called = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);
  /* FIXME, pad alloc must be called to get the new caps, because we don't call
   * pad alloc */
  fail_unless (buffer_alloc_ct2_called == TRUE);

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  /* FIXME changing src caps should produce converted buffer */
  GST_DEBUG_OBJECT (trans, "received caps %" GST_PTR_FORMAT,
      GST_BUFFER_CAPS (buffer));
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), outcaps));
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  /* with caps buffer */
  GST_DEBUG_OBJECT (trans, "alloc with caps, size 10");

  set_caps_ct2_case = 0;
  buffer_alloc_ct2_case = 1;
  buffer_alloc_ct2_called = FALSE;
  set_caps_ct2_called = FALSE;
  res = gst_pad_alloc_buffer (trans->srcpad, 0, 10, incaps, &buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  /* FIXME a buffer alloc should never set caps */
  fail_unless (set_caps_ct2_called == FALSE);
  fail_unless (GST_BUFFER_SIZE (buffer) == 10);
  /* FIXME, ideally we want to reuse these caps */
  fail_unless (GST_BUFFER_CAPS (buffer) == incaps);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), incaps));
  gst_buffer_unref (buffer);

  GST_DEBUG_OBJECT (trans, "buffer with caps %" GST_PTR_FORMAT, incaps);
  buffer = gst_buffer_new_and_alloc (10);
  gst_buffer_set_caps (buffer, incaps);

  /* don't suggest anything else */
  buffer_alloc_ct2_suggest = FALSE;
  buffer_alloc_ct2_case = 0;
  transform_ct2_called = FALSE;
  buffer_alloc_ct2_called = FALSE;
  res = gst_test_trans_push (trans, buffer);
  fail_unless (res == GST_FLOW_OK);
  fail_unless (transform_ct2_called == TRUE);
  fail_unless (buffer_alloc_ct2_called == TRUE);
  /* after push, get rid of the final ref we had */

  buffer = gst_test_trans_pop (trans);
  fail_unless (buffer != NULL);
  fail_unless (GST_BUFFER_SIZE (buffer) == 20);
  fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (buffer), outcaps));

  /* output buffer has refcount 1 */
  fail_unless (GST_MINI_OBJECT_REFCOUNT_VALUE (buffer) == 1);
  gst_buffer_unref (buffer);

  gst_caps_unref (incaps);
  gst_caps_unref (outcaps);

  gst_test_trans_free (trans);
}

GST_END_TEST;


static Suite *
gst_basetransform_suite (void)
{
  Suite *s = suite_create ("GstBaseTransform");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
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
