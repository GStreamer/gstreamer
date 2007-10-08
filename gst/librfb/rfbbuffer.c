#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rfbbuffer.h"

RfbBuffer *
rfb_buffer_new (void)
{
  return g_new0 (RfbBuffer, 1);
}

RfbBuffer *
rfb_buffer_new_and_alloc (int len)
{
  RfbBuffer *buffer = g_new0 (RfbBuffer, 1);

  buffer->data = g_malloc (len);
  buffer->free_data = (RfbBufferFreeFunc) g_free;

  return buffer;
}

void
rfb_buffer_free (RfbBuffer * buffer)
{
  g_return_if_fail (buffer != NULL);

  buffer->free_data (buffer->data, buffer->buffer_private);
}
