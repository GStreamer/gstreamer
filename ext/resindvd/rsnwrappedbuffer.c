/* GStreamer
 * Copyright (C) 2008 Jan Schmidt <thaytan@noraisin.net>
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
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "rsnwrappedbuffer.h"

GstBuffer *
rsn_wrapped_buffer_new (GstBuffer * buf_to_wrap, GstElement * owner)
{
  GstBuffer *buf;
  RsnMetaWrapped *meta;

  g_return_val_if_fail (buf_to_wrap, NULL);

  buf = gst_buffer_new ();
  meta = RSN_META_WRAPPED_ADD (buf);

  meta->wrapped_buffer = buf_to_wrap;
  meta->owner = gst_object_ref (owner);

  GST_BUFFER_DATA (buf) = GST_BUFFER_DATA (buf_to_wrap);
  GST_BUFFER_SIZE (buf) = GST_BUFFER_SIZE (buf_to_wrap);
  gst_buffer_copy_metadata (GST_BUFFER (buf), buf_to_wrap, GST_BUFFER_COPY_ALL);

  /* If the wrapped buffer isn't writable, make sure this one isn't either */
  if (!gst_buffer_is_writable (buf_to_wrap))
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);

  return buf;
}

void
rsn_meta_wrapped_set_owner (RsnMetaWrapped * meta, GstElement * owner)
{
  g_return_if_fail (meta != NULL);

  if (meta->owner)
    gst_object_unref (meta->owner);

  if (owner)
    gst_object_ref (owner);

  meta->owner = owner;
}

GstBuffer *
rsn_meta_wrapped_unwrap_and_unref (GstBuffer * wrap_buf, RsnMetaWrapped * meta)
{
  GstBuffer *buf;
  gboolean is_readonly;

  g_return_val_if_fail (wrap_buf != NULL, NULL);
  g_return_val_if_fail (meta->wrapped_buffer != NULL, NULL);

  buf = gst_buffer_ref (meta->wrapped_buffer);
  buf = gst_buffer_make_metadata_writable (buf);

  /* Copy changed metadata back to the wrapped buffer from the wrapper,
   * except the the read-only flag and the caps. */
  is_readonly = GST_BUFFER_FLAG_IS_SET (wrap_buf, GST_BUFFER_FLAG_READONLY);
  gst_buffer_copy_metadata (buf, GST_BUFFER (wrap_buf),
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
  if (!is_readonly)
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_READONLY);

  gst_buffer_unref (wrap_buf);

  return buf;
}

static void
rsn_meta_wrapped_init (RsnMetaWrapped * meta, GstBuffer * buffer)
{
  meta->owner = NULL;
}

static void
rsn_meta_wrapped_free (RsnMetaWrapped * meta, GstBuffer * buffer)
{
  gst_buffer_unref (meta->wrapped_buffer);
  if (meta->owner)
    gst_object_unref (meta->owner);
}

const GstMetaInfo *
rsn_meta_wrapped_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (meta_info == NULL) {
    meta_info = gst_meta_register ("RsnMetaWrapped", "RsnMetaWrapped",
        sizeof (RsnMetaWrapped),
        (GstMetaInitFunction) rsn_meta_wrapped_init,
        (GstMetaFreeFunction) rsn_meta_wrapped_free,
        (GstMetaTransformFunction) NULL,
        (GstMetaSerializeFunction) NULL, (GstMetaDeserializeFunction) NULL);
  }
  return meta_info;
}
