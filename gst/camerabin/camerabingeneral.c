/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
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

/**
 * SECTION:camerabingeneral
 * @short_description: helper functions for #GstCameraBin and it's modules
 *
 * Common helper functions for #GstCameraBin, #GstCameraBinImage and
 * #GstCameraBinVideo.
 *
 */

#include "camerabingeneral.h"
#include <glib.h>

GST_DEBUG_CATEGORY (gst_camerabin_debug);

static gboolean
camerabin_general_dbg_have_event (GstPad * pad, GstEvent * event,
    gpointer u_data)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstElement *elem = (GstElement *) u_data;
      gchar *elem_name = gst_element_get_name (elem);
      gchar *pad_name = gst_pad_get_name (pad);

      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 start, stop, pos;
      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &pos);

      GST_DEBUG ("element %s, pad %s, new_seg_start =%" GST_TIME_FORMAT
          ", new_seg_stop =%" GST_TIME_FORMAT
          ", new_seg_pos =%" GST_TIME_FORMAT "\n", elem_name, pad_name,
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (pos));

      g_free (pad_name);
      g_free (elem_name);
    }
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
camerabin_general_dbg_have_buffer (GstPad * pad, GstBuffer * buffer,
    gpointer u_data)
{
  GstElement *elem = (GstElement *) u_data;
  gchar *elem_name = gst_element_get_name (elem);
  gchar *pad_name = gst_pad_get_name (pad);

  GST_DEBUG ("element %s, pad %s, buf_ts =%" GST_TIME_FORMAT "\n", elem_name,
      pad_name, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  g_free (pad_name);
  g_free (elem_name);

  return TRUE;

}

void
camerabin_general_dbg_set_probe (GstElement * elem, gchar * pad_name,
    gboolean buf, gboolean evt)
{
  GstPad *pad = gst_element_get_static_pad (elem, pad_name);

  if (buf)
    gst_pad_add_buffer_probe (pad,
        G_CALLBACK (camerabin_general_dbg_have_buffer), elem);
  if (evt)
    gst_pad_add_event_probe (pad,
        G_CALLBACK (camerabin_general_dbg_have_event), elem);

  gst_object_unref (pad);
}

/**
 * gst_camerabin_add_element:
 * @bin: add an element to this bin
 * @new_elem: new element to be added
 *
 * Adds given element to given @bin. Looks for an unconnected src pad
 * from the @bin and links the element to it.  Raises an error if adding
 * or linking failed.
 *
 * Returns: %TRUE if adding and linking succeeded, %FALSE otherwise.
 */
gboolean
gst_camerabin_add_element (GstBin * bin, GstElement * new_elem)
{
  gboolean ret = FALSE;

  ret = gst_camerabin_try_add_element (bin, new_elem);

  if (!ret) {
    gchar *elem_name = gst_element_get_name (new_elem);
    GST_ELEMENT_ERROR (bin, CORE, NEGOTIATION, (NULL),
        ("linking %s failed", elem_name));
    g_free (elem_name);
  }

  return ret;
}

/**
 * gst_camerabin_try_add_element:
 * @bin: tries adding an element to this bin
 * @new_elem: new element to be added
 *
 * Adds given element to given @bin. Looks for an unconnected src pad
 * from the @bin and links the element to it.
 *
 * Returns: %TRUE if adding and linking succeeded, %FALSE otherwise.
 */
gboolean
gst_camerabin_try_add_element (GstBin * bin, GstElement * new_elem)
{
  GstPad *bin_pad;
  GstElement *bin_elem;
  gboolean ret = TRUE;

  if (!bin || !new_elem) {
    return FALSE;
  }

  /* Get pads for linking */
  GST_DEBUG ("finding unconnected src pad");
  bin_pad = gst_bin_find_unlinked_pad (bin, GST_PAD_SRC);
  GST_DEBUG ("unconnected pad %s:%s", GST_DEBUG_PAD_NAME (bin_pad));
  /* Add to bin */
  gst_bin_add (GST_BIN (bin), new_elem);
  /* Link, if unconnected pad was found, otherwise just add it to bin */
  if (bin_pad) {
    bin_elem = gst_pad_get_parent_element (bin_pad);
    gst_object_unref (bin_pad);
    if (!gst_element_link (bin_elem, new_elem)) {
      gst_bin_remove (bin, new_elem);
      ret = FALSE;
    }
    gst_object_unref (bin_elem);
  }

  return ret;
}

/**
 * gst_camerabin_create_and_add_element:
 * @bin: tries adding an element to this bin
 * @elem_name: name of the element to be created
 *
 * Creates an element according to given name and
 * adds it to given @bin. Looks for an unconnected src pad
 * from the @bin and links the element to it.
 *
 * Returns: pointer to the new element if successful, NULL otherwise.
 */
GstElement *
gst_camerabin_create_and_add_element (GstBin * bin, const gchar * elem_name)
{
  GstElement *new_elem = NULL;

  GST_DEBUG ("adding %s", elem_name);
  new_elem = gst_element_factory_make (elem_name, NULL);
  if (!new_elem) {
    GST_ELEMENT_ERROR (bin, CORE, MISSING_PLUGIN, (NULL),
        ("could not create \"%s\" element.", elem_name));
  } else if (!gst_camerabin_add_element (bin, new_elem)) {
    new_elem = NULL;
  }

  return new_elem;
}

/**
 * gst_camerabin_remove_elements_from_bin:
 * @bin: removes all elements from this bin
 *
 * Removes all elements from this @bin.
 */
void
gst_camerabin_remove_elements_from_bin (GstBin * bin)
{
  GstIterator *iter = NULL;
  gpointer data = NULL;
  GstElement *elem = NULL;
  gboolean done = FALSE;

  iter = gst_bin_iterate_elements (bin);
  while (!done) {
    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
        elem = GST_ELEMENT (data);
        gst_bin_remove (bin, elem);
        /* Iterator increased the element refcount, so unref */
        gst_object_unref (elem);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (bin, "error in iterating elements");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
}

/**
 * gst_camerabin_drop_eos_probe:
 * @pad: pad receiving the event
 * @event: received event
 * @u_data: not used
 *
 * Event probe that drop all eos events.
 *
 * Returns: FALSE to drop the event, TRUE otherwise
 */
gboolean
gst_camerabin_drop_eos_probe (GstPad * pad, GstEvent * event, gpointer u_data)
{
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG ("dropping eos in %s:%s", GST_DEBUG_PAD_NAME (pad));
      ret = FALSE;
      break;
    default:
      break;
  }
  return ret;
}
