/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
 *
 * gsttypefindhelper.c:
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

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsttypefindhelper.h"

/**
 * typefind code here
 */
typedef struct
{
  GstPad *src;
  guint best_probability;
  GstCaps *caps;
  guint64 size;
  GSList *buffers;
  GstTypeFindFactory *factory;
}
GstTypeFindHelper;

static guint8 *
helper_find_peek (gpointer data, gint64 offset, guint size)
{
  GstTypeFindHelper *find;
  GstBuffer *buffer;
  GstPad *src;
  GstFlowReturn ret;

  find = (GstTypeFindHelper *) data;
  src = find->src;

  GST_LOG_OBJECT (src, "'%s' called peek (%" G_GINT64_FORMAT ", %u)",
      GST_PLUGIN_FEATURE_NAME (find->factory), offset, size);

  if (size == 0)
    return NULL;

  if (offset < 0) {
    if (find->size == -1 || find->size < -offset)
      return NULL;

    offset += find->size;
  }

  /* see if we have a matching buffer already in our list */
  if (size > 0) {
    GSList *walk;

    for (walk = find->buffers; walk; walk = walk->next) {
      GstBuffer *buf = GST_BUFFER_CAST (walk->data);
      guint64 buf_offset = GST_BUFFER_OFFSET (buf);
      guint buf_size = GST_BUFFER_SIZE (buf);

      if (buf_offset <= offset && (offset + size) < (buf_offset + buf_size))
        return GST_BUFFER_DATA (buf) + (offset - buf_offset);
    }
  }

  buffer = NULL;
  ret = GST_PAD_GETRANGEFUNC (src) (src, offset, size, &buffer);

  if (ret != GST_FLOW_OK)
    goto error;

  /* getrange might silently return shortened buffers at the end of a file,
   * we must, however, always return either the full requested data or NULL */
  if (GST_BUFFER_OFFSET (buffer) != offset || GST_BUFFER_SIZE (buffer) < size) {
    GST_DEBUG ("droping short buffer: %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT
        " instead of %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT,
        GST_BUFFER_OFFSET (buffer), GST_BUFFER_OFFSET (buffer) +
        GST_BUFFER_SIZE (buffer) - 1, offset, offset + size - 1);
    gst_buffer_unref (buffer);
    return NULL;
  }

  find->buffers = g_slist_prepend (find->buffers, buffer);
  return GST_BUFFER_DATA (buffer);

error:
  {
    return NULL;
  }
}

static void
helper_find_suggest (gpointer data, guint probability, const GstCaps * caps)
{
  GstTypeFindHelper *find = (GstTypeFindHelper *) data;

  GST_LOG_OBJECT (find->src,
      "'%s' called called suggest (%u, %" GST_PTR_FORMAT ")",
      GST_PLUGIN_FEATURE_NAME (find->factory), probability, caps);

  if (probability > find->best_probability) {
    GstCaps *copy = gst_caps_copy (caps);

    gst_caps_replace (&find->caps, copy);
    gst_caps_unref (copy);
    find->best_probability = probability;
  }
}

/**
 * gst_type_find_helper:
 * @src: A source #GstPad
 * @size: The length in bytes
 *
 * Tries to find what type of data is flowing from the given source #GstPad.
 *
 * Returns: The #GstCaps corresponding to the data stream.
 * Returns #NULL if no #GstCaps matches the data stream.
 */

GstCaps *
gst_type_find_helper (GstPad * src, guint64 size)
{
  GstTypeFind gst_find;
  GstTypeFindHelper find;
  GSList *l;
  GList *walk, *type_list = NULL;
  GstCaps *result = NULL;

  g_return_val_if_fail (src != NULL, NULL);
  g_return_val_if_fail (GST_PAD_GETRANGEFUNC (src) != NULL, NULL);

  walk = type_list = gst_type_find_factory_get_list ();

  find.src = src;
  find.best_probability = 0;
  find.caps = NULL;
  find.size = size;
  find.buffers = NULL;
  gst_find.data = &find;
  gst_find.peek = helper_find_peek;
  gst_find.suggest = helper_find_suggest;
  gst_find.get_length = NULL;

  while (walk) {
    GstTypeFindFactory *factory = GST_TYPE_FIND_FACTORY (walk->data);

    find.factory = factory;

    gst_type_find_factory_call_function (factory, &gst_find);
    if (find.best_probability >= GST_TYPE_FIND_MAXIMUM)
      break;
    walk = g_list_next (walk);
  }

  if (find.best_probability > 0)
    result = find.caps;

  for (l = find.buffers; l; l = l->next)
    gst_buffer_unref (GST_BUFFER_CAST (l->data));
  g_slist_free (find.buffers);

  return result;
}
