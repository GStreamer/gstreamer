
#include <rfbbytestream.h>
#include <string.h>

RfbBytestream *
rfb_bytestream_new (void)
{
  return g_new0 (RfbBytestream, 1);
}

int
rfb_bytestream_get (RfbBytestream * bs, int len)
{
  RfbBuffer *buffer;

  buffer = bs->get_buffer (len, bs->user_data);

  if (buffer) {
    g_print ("got buffer (%d bytes)\n", buffer->length);
    bs->buffer_list = g_list_append (bs->buffer_list, buffer);

    bs->length += buffer->length;

    return len;
  }

  return 0;
}

gboolean
rfb_bytestream_check (RfbBytestream * bs, int len)
{
  while (bs->length < len) {
    rfb_bytestream_get (bs, len - bs->length);
  }
  return TRUE;
}

static int
rfb_bytestream_copy_nocheck (RfbBytestream * bs, RfbBuffer * buffer, int len)
{
  GList *item;
  int offset;
  int first_offset;
  RfbBuffer *frombuf;
  int n;

  offset = 0;
  first_offset = bs->offset;
  for (item = bs->buffer_list; item; item = g_list_next (item)) {
    frombuf = (RfbBuffer *) item->data;
    n = MIN (len, frombuf->length - first_offset);
    g_print ("copying %d bytes from %p\n", n, frombuf);
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

int
rfb_bytestream_read (RfbBytestream * bs, RfbBuffer ** buffer, int len)
{
  RfbBuffer *buf;

  rfb_bytestream_check (bs, len);

  buf = rfb_buffer_new_and_alloc (len);
  rfb_bytestream_copy_nocheck (bs, buf, len);

  rfb_bytestream_flush (bs, len);

  *buffer = buf;
  return len;
}

int
rfb_bytestream_peek (RfbBytestream * bs, RfbBuffer ** buffer, int len)
{
  RfbBuffer *buf;

  rfb_bytestream_check (bs, len);

  buf = rfb_buffer_new_and_alloc (len);
  rfb_bytestream_copy_nocheck (bs, buf, len);

  *buffer = buf;
  return len;
}

int
rfb_bytestream_flush (RfbBytestream * bs, int len)
{
  GList *item;
  RfbBuffer *buf;
  int n;

  while ((item = bs->buffer_list)) {
    buf = (RfbBuffer *) item->data;

    n = MIN (buf->length - bs->offset, len);
    if (n <= len) {
      bs->offset = 0;
      bs->buffer_list = g_list_delete_link (bs->buffer_list, item);
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
