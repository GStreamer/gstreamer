/* GStreamer Musepack decoder plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include "gstmusepackreader.h"
#include <gst/gst.h>
#include <string.h>


GST_DEBUG_CATEGORY_EXTERN (musepackdec_debug);
#define GST_CAT_DEFAULT musepackdec_debug

static mpc_int32_t gst_musepack_reader_peek (void *this, void *ptr,
    mpc_int32_t size);
static mpc_int32_t gst_musepack_reader_read (void *this, void *ptr,
    mpc_int32_t size);
static mpc_bool_t gst_musepack_reader_seek (void *this, mpc_int32_t offset);
static mpc_int32_t gst_musepack_reader_tell (void *this);
static mpc_int32_t gst_musepack_reader_get_size (void *this);
static mpc_bool_t gst_musepack_reader_canseek (void *this);

static mpc_int32_t
gst_musepack_reader_peek (void *this, void *ptr, mpc_int32_t size)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  GstFlowReturn flow_ret;
  GstBuffer *buf = NULL;
  guint read;

  g_return_val_if_fail (size > 0, 0);

  /* GST_LOG_OBJECT (musepackdec, "size=%d", size); */

  flow_ret = gst_pad_pull_range (musepackdec->sinkpad, musepackdec->offset,
      size, &buf);

  if (flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (musepackdec, "Flow: %s", gst_flow_get_name (flow_ret));
    return 0;
  }

  read = MIN (GST_BUFFER_SIZE (buf), size);

  if (read < size) {
    GST_WARNING_OBJECT (musepackdec, "Short read: got only %u bytes of %u "
        "bytes requested at offset %" G_GINT64_FORMAT, read, size,
        musepackdec->offset);
    /* GST_ELEMENT_ERROR (musepackdec, RESOURCE, READ, (NULL), (NULL)); */
  }

  memcpy (ptr, GST_BUFFER_DATA (buf), read);
  gst_buffer_unref (buf);
  return read;
}

static mpc_int32_t
gst_musepack_reader_read (void *this, void *ptr, mpc_int32_t size)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  gint read;

  /* read = peek + flush */
  if ((read = gst_musepack_reader_peek (this, ptr, size)) > 0) {
    musepackdec->offset += read;
  }

  return read;
}

static mpc_bool_t
gst_musepack_reader_seek (void *this, mpc_int32_t offset)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  mpc_int32_t length;

  length = gst_musepack_reader_get_size (this);
  if (length > 0 && offset >= 0 && offset < length) {
    musepackdec->offset = offset;
    GST_LOG_OBJECT (musepackdec, "Seek'ed to byte offset %d", (gint) offset);
    return TRUE;
  } else {
    GST_DEBUG_OBJECT (musepackdec, "Cannot seek to offset %d", (gint) offset);
    return FALSE;
  }
}

static mpc_int32_t
gst_musepack_reader_tell (void *this)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);

  return musepackdec->offset;
}

static mpc_int32_t
gst_musepack_reader_get_size (void *this)
{
  GstMusepackDec *dec = GST_MUSEPACK_DEC (this);
  GstFormat format = GST_FORMAT_BYTES;
  gint64 length = -1;
  GstPad *peer;

  peer = gst_pad_get_peer (dec->sinkpad);
  if (peer) {
    if (!gst_pad_query_duration (peer, &format, &length) || length <= 0) {
      length = -1;
    }
    gst_object_unref (peer);
  }

  return (mpc_int32_t) length;
}

static mpc_bool_t
gst_musepack_reader_canseek (void *this)
{
  return TRUE;
}

void
gst_musepack_init_reader (mpc_reader * r, GstMusepackDec * musepackdec)
{
  r->data = musepackdec;

  r->read = gst_musepack_reader_read;
  r->seek = gst_musepack_reader_seek;
  r->tell = gst_musepack_reader_tell;
  r->get_size = gst_musepack_reader_get_size;
  r->canseek = gst_musepack_reader_canseek;
}
