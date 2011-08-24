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

G_DEFINE_TYPE (RsnWrappedBuffer, rsn_wrappedbuffer, GST_TYPE_BUFFER);

static gboolean
rsn_wrapped_buffer_default_release (GstElement * owner, RsnWrappedBuffer * buf);

static void rsn_wrapped_buffer_finalize (RsnWrappedBuffer * wrap_buf);

static void
rsn_wrappedbuffer_class_init (RsnWrappedBufferClass * klass)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (klass);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      rsn_wrapped_buffer_finalize;
}

static void
rsn_wrappedbuffer_init (RsnWrappedBuffer * self)
{
  self->release = rsn_wrapped_buffer_default_release;
}

static void
rsn_wrapped_buffer_finalize (RsnWrappedBuffer * wrap_buf)
{
  if (wrap_buf->release) {
    /* Release might increment the refcount to recycle and return TRUE,
     * in which case, exit without chaining up */
    if (wrap_buf->release (wrap_buf->owner, wrap_buf))
      return;
  }

  GST_MINI_OBJECT_CLASS (rsn_wrappedbuffer_parent_class)->finalize
      (GST_MINI_OBJECT (wrap_buf));
}

RsnWrappedBuffer *
rsn_wrapped_buffer_new (GstBuffer * buf_to_wrap)
{
  RsnWrappedBuffer *buf;
  g_return_val_if_fail (buf_to_wrap, NULL);

  buf = (RsnWrappedBuffer *) gst_mini_object_new (RSN_TYPE_WRAPPEDBUFFER);
  if (buf == NULL)
    return NULL;

  buf->wrapped_buffer = buf_to_wrap;

  GST_BUFFER_DATA (buf) = GST_BUFFER_DATA (buf_to_wrap);
  GST_BUFFER_SIZE (buf) = GST_BUFFER_SIZE (buf_to_wrap);
  gst_buffer_copy_metadata (GST_BUFFER (buf), buf_to_wrap, GST_BUFFER_COPY_ALL);

  /* If the wrapped buffer isn't writable, make sure this one isn't either */
  if (!gst_buffer_is_writable (buf_to_wrap))
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);

  return buf;
}

void
rsn_wrapped_buffer_set_owner (RsnWrappedBuffer * wrapped_buf,
    GstElement * owner)
{
  g_return_if_fail (wrapped_buf != NULL);

  if (wrapped_buf->owner)
    gst_object_unref (wrapped_buf->owner);

  if (owner)
    wrapped_buf->owner = gst_object_ref (owner);
  else
    wrapped_buf->owner = NULL;
}

void
rsn_wrapped_buffer_set_releasefunc (RsnWrappedBuffer * wrapped_buf,
    RsnWrappedBufferReleaseFunc release_func)
{
  g_return_if_fail (wrapped_buf != NULL);

  wrapped_buf->release = release_func;
}

static gboolean
rsn_wrapped_buffer_default_release (GstElement * owner, RsnWrappedBuffer * buf)
{
  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (buf->wrapped_buffer != NULL, FALSE);

  gst_buffer_unref (buf->wrapped_buffer);
  if (buf->owner)
    gst_object_unref (buf->owner);

  return FALSE;
}

GstBuffer *
rsn_wrappedbuffer_unwrap_and_unref (RsnWrappedBuffer * wrap_buf)
{
  GstBuffer *buf;
  gboolean is_readonly;

  g_return_val_if_fail (wrap_buf != NULL, NULL);
  g_return_val_if_fail (wrap_buf->wrapped_buffer != NULL, NULL);

  buf = gst_buffer_ref (wrap_buf->wrapped_buffer);
  buf = gst_buffer_make_metadata_writable (buf);

  /* Copy changed metadata back to the wrapped buffer from the wrapper,
   * except the the read-only flag and the caps. */
  is_readonly = GST_BUFFER_FLAG_IS_SET (wrap_buf, GST_BUFFER_FLAG_READONLY);
  gst_buffer_copy_metadata (buf, GST_BUFFER (wrap_buf),
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
  if (!is_readonly)
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_READONLY);

  gst_buffer_unref (GST_BUFFER (wrap_buf));

  return buf;
}
