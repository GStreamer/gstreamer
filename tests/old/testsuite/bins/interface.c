/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>

/* tests if gst_bin_iterate_(all_)by_interface works */

gint
main (gint argc, gchar * argv[])
{
  GstBin *bin, *bin2;
  GstElement *filesrc;
  GstIterator *it;
  gpointer item;

  gst_init (&argc, &argv);

  bin = GST_BIN (gst_bin_new (NULL));
  g_assert (bin);

  filesrc = gst_element_factory_make ("filesrc", NULL);
  g_assert (filesrc);
  g_assert (GST_IS_URI_HANDLER (filesrc));
  gst_bin_add (bin, filesrc);

  g_assert (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  g_assert (it != NULL);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_assert (item == (gpointer) filesrc);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL), NULL);
  g_assert (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  g_assert (it != NULL);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_assert (item == (gpointer) filesrc);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  bin2 = bin;
  bin = GST_BIN (gst_bin_new (NULL));
  g_assert (bin);
  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      GST_ELEMENT (bin2), gst_element_factory_make ("identity", NULL), NULL);
  g_assert (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_assert (item == (gpointer) filesrc);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add (bin, gst_element_factory_make ("filesrc", NULL));
  gst_bin_add (bin2, gst_element_factory_make ("filesrc", NULL));
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_assert (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  g_object_unref (bin);
  return 0;
}
