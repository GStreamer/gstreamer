#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rfbbytestream.h>
#include <string.h>

static gint rfb_bytestream_copy_nocheck (RfbBytestream * bs,
    RfbBuffer * buffer, gint len);

RfbBytestream *
rfb_bytestream_new (void)
{
  return g_new0 (RfbBytestream, 1);
}

void
rfb_bytestream_free (RfbBytestream * bs)
{
  g_return_if_fail (bs != NULL);

  g_slist_free (bs->buffer_list);
  g_free (bs);
}

gint
rfb_bytestream_get (RfbBytestream * bs, gint len)
{
  RfbBuffer *buffer;

  g_return_val_if_fail (bs != NULL, 0);

  buffer = bs->get_buffer (len, bs->user_data);

  if (buffer) {
    // g_print ("got buffer (%d bytes)\n", buffer->length);
    bs->buffer_list = g_slist_append (bs->buffer_list, buffer);

    bs->length += buffer->length;

    return len;
  }

  return 0;
}

gboolean
rfb_bytestream_check (RfbBytestream * bs, gint len)
{
  g_return_val_if_fail (bs != NULL, FALSE);

  while (bs->length < len) {
    rfb_bytestream_get (bs, len - bs->length);
  }
  return TRUE;
}

gint
rfb_bytestream_read (RfbBytestream * bs, RfbBuffer ** buffer, gint len)
{
  RfbBuffer *buf;

  g_return_val_if_fail (bs != NULL, 0);
  g_return_val_if_fail (buffer != NULL, 0);

  rfb_bytestream_check (bs, len);

  buf = rfb_buffer_new_and_alloc (len);
  rfb_bytestream_copy_nocheck (bs, buf, len);

  rfb_bytestream_flush (bs, len);

  *buffer = buf;
  return len;
}

gint
rfb_bytestream_peek (RfbBytestream * bs, RfbBuffer ** buffer, gint len)
{
  RfbBuffer *buf;

  g_return_val_if_fail (bs != NULL, 0);
  g_return_val_if_fail (buffer != NULL, 0);

  rfb_bytestream_check (bs, len);

  buf = rfb_buffer_new_and_alloc (len);
  rfb_bytestream_copy_nocheck (bs, buf, len);

  *buffer = buf;
  return len;
}

gint
rfb_bytestream_flush (RfbBytestream * bs, gint len)
{
  GSList *item;
  RfbBuffer *buf;
  gint n;

  g_return_val_if_fail (bs != NULL, 0);

  while ((item = bs->buffer_list)) {
    buf = (RfbBuffer *) item->data;

    n = MIN (buf->length - bs->offset, len);
    if (n <= len) {
      bs->offset = 0;
      bs->buffer_list = g_slist_delete_link (bs->buffer_list, item);
      rfb_buffer_free (buf);
    } else {
      bs->offset = bs->offset + len;
    }
    bs->length -= n;
    len -= n;
    if (len == 0)
      return 0;
  }

  g_assert_not_reached ();
  return 0;
}

static gint
rfb_bytestream_copy_nocheck (RfbBytestream * bs, RfbBuffer * buffer, gint len)
{
  GSList *item;
  gint offset;
  gint first_offset;
  RfbBuffer *frombuf;
  gint n;

  offset = 0;
  first_offset = bs->offset;
  for (item = bs->buffer_list; item; item = item->next) {
    frombuf = (RfbBuffer *) item->data;
    n = MIN (len, frombuf->length - first_offset);
    // g_print ("copying %d bytes from %p\n", n, frombuf);
    memcpy (buffer->data + offset, frombuf->data + first_offset, n);
    first_offset = 0;
    len -= n;
    offset += n;
    if (len == 0)
      return len;
  }

  g_assert_not_reached ();
  return 0;
}
