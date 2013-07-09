/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-element_wrapper.c - QA ElementWrapper class
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

#include "gst-qa-element-wrapper.h"

/**
 * SECTION:gst-qa-element-wrapper
 * @short_description: Class that wraps a #GstElement for QA checks
 *
 * TODO
 */

GST_DEBUG_CATEGORY_STATIC (gst_qa_element_wrapper_debug);
#define GST_CAT_DEFAULT gst_qa_element_wrapper_debug

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_element_wrapper_debug, "qa_element_wrapper", 0, "QA ElementWrapper");
#define gst_qa_element_wrapper_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaElementWrapper, gst_qa_element_wrapper,
    G_TYPE_OBJECT, _do_init);

static void
gst_qa_element_wrapper_wrap_pad (GstQaElementWrapper * wrapper, GstPad * pad);

static void
_qa_element_pad_added (GstElement * element, GstPad * pad,
    GstQaElementWrapper * wrapper);

static void
gst_qa_element_wrapper_dispose (GObject * object)
{
  GstQaElementWrapper *wrapper = GST_QA_ELEMENT_WRAPPER_CAST (object);

  if (wrapper->pad_added_id)
      g_signal_handler_disconnect (wrapper->element, wrapper->pad_added_id);

  g_list_free_full (wrapper->pad_wrappers, g_object_unref);

  if (wrapper->element)
    gst_object_unref (wrapper->element);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_qa_element_wrapper_class_init (GstQaElementWrapperClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_qa_element_wrapper_dispose;
}

static void
gst_qa_element_wrapper_init (GstQaElementWrapper * element_wrapper)
{
  element_wrapper->setup = FALSE;
}

/**
 * gst_qa_element_wrapper_new:
 * @element: (transfer-full): a #GstElement to run QA on
 */
GstQaElementWrapper *
gst_qa_element_wrapper_new (GstElement * element)
{
  GstQaElementWrapper *wrapper =
      g_object_new (GST_TYPE_QA_ELEMENT_WRAPPER, NULL);

  g_return_val_if_fail (element != NULL, NULL);

  wrapper->element = element;
  return wrapper;
}

gboolean
gst_qa_element_wrapper_setup (GstQaElementWrapper * wrapper)
{
  GstIterator *iterator;
  gboolean done;
  GstPad *pad;

  if (wrapper->setup)
    return TRUE;

  GST_DEBUG_OBJECT (wrapper, "Setting up wrapper for element %" GST_PTR_FORMAT,
      wrapper->element);

  wrapper->pad_added_id = g_signal_connect (wrapper->element, "pad-added",
      G_CALLBACK (_qa_element_pad_added), wrapper);

  iterator = gst_element_iterate_pads (wrapper->element);
  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iterator, (gpointer *) &pad)) {
      case GST_ITERATOR_OK:
        gst_qa_element_wrapper_wrap_pad (wrapper, pad);
        gst_object_unref (pad);
        break;
      case GST_ITERATOR_RESYNC:
        /* TODO how to handle this? */
        gst_iterator_resync (iterator);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iterator);

  wrapper->setup = TRUE;
  return TRUE;
}

static void
gst_qa_element_wrapper_wrap_pad (GstQaElementWrapper * wrapper, GstPad * pad)
{
  GST_DEBUG_OBJECT (wrapper, "Wrapping pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  /* TODO */
}

static void
_qa_element_pad_added (GstElement * element, GstPad * pad,
    GstQaElementWrapper * wrapper)
{
  g_return_if_fail (wrapper->element == element);
  gst_qa_element_wrapper_wrap_pad (wrapper, pad);
}
