/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-pad_wrapper.c - QA PadWrapper class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "gst-qa-pad-wrapper.h"

/**
 * SECTION:gst-qa-pad-wrapper
 * @short_description: Class that wraps a #GstPad for QA checks
 *
 * TODO
 */

GST_DEBUG_CATEGORY_STATIC (gst_qa_pad_wrapper_debug);
#define GST_CAT_DEFAULT gst_qa_pad_wrapper_debug

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_pad_wrapper_debug, "qa_pad_wrapper", 0, "QA PadWrapper");
#define gst_qa_pad_wrapper_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaPadWrapper, gst_qa_pad_wrapper,
    G_TYPE_OBJECT, _do_init);


static void
gst_qa_pad_wrapper_dispose (GObject * object)
{
  GstQaPadWrapper *wrapper = GST_QA_PAD_WRAPPER_CAST (object);

  if (wrapper->pad)
    gst_object_unref (wrapper->pad);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_qa_pad_wrapper_class_init (GstQaPadWrapperClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_qa_pad_wrapper_dispose;
}

static void
gst_qa_pad_wrapper_init (GstQaPadWrapper * pad_wrapper)
{
  pad_wrapper->setup = FALSE;
}

/**
 * gst_qa_pad_wrapper_new:
 * @pad: (transfer-none): a #GstPad to run QA on
 */
GstQaPadWrapper *
gst_qa_pad_wrapper_new (GstPad * pad)
{
  GstQaPadWrapper *wrapper =
      g_object_new (GST_TYPE_QA_PAD_WRAPPER, NULL);

  g_return_val_if_fail (pad != NULL, NULL);

  wrapper->pad = gst_object_ref (pad);
  return wrapper;
}

gboolean
gst_qa_pad_wrapper_setup (GstQaPadWrapper * wrapper)
{
  if (wrapper->setup)
    return TRUE;

  GST_DEBUG_OBJECT (wrapper, "Setting up wrapper for pad %" GST_PTR_FORMAT,
      wrapper->pad);

  wrapper->setup = TRUE;
  return TRUE;
}

