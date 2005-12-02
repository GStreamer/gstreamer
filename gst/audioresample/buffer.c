
#ifndef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <string.h>

#include "buffer.h"
#include "debug.h"

static void audioresample_buffer_free_mem (AudioresampleBuffer * buffer,
    void *);
static void audioresample_buffer_free_subbuffer (AudioresampleBuffer * buffer,
    void *priv);


AudioresampleBuffer *
audioresample_buffer_new (void)
{
  AudioresampleBuffer *buffer;

  buffer = g_new0 (AudioresampleBuffer, 1);
  buffer->ref_count = 1;
  return buffer;
}

AudioresampleBuffer *
audioresample_buffer_new_and_alloc (int size)
{
  AudioresampleBuffer *buffer = audioresample_buffer_new ();

  buffer->data = g_malloc (size);
  buffer->length = size;
  buffer->free = audioresample_buffer_free_mem;

  return buffer;
}

AudioresampleBuffer *
audioresample_buffer_new_with_data (void *data, int size)
{
  AudioresampleBuffer *buffer = audioresample_buffer_new ();

  buffer->data = data;
  buffer->length = size;
  buffer->free = audioresample_buffer_free_mem;

  return buffer;
}

AudioresampleBuffer *
audioresample_buffer_new_subbuffer (AudioresampleBuffer * buffer, int offset,
    int length)
{
  AudioresampleBuffer *subbuffer = audioresample_buffer_new ();

  if (buffer->parent) {
    audioresample_buffer_ref (buffer->parent);
    subbuffer->parent = buffer->parent;
  } else {
    audioresample_buffer_ref (buffer);
    subbuffer->parent = buffer;
  }
  subbuffer->data = buffer->data + offset;
  subbuffer->length = length;
  subbuffer->free = audioresample_buffer_free_subbuffer;

  return subbuffer;
}

void
audioresample_buffer_ref (AudioresampleBuffer * buffer)
{
  buffer->ref_count++;
}

void
audioresample_buffer_unref (AudioresampleBuffer * buffer)
{
  buffer->ref_count--;
  if (buffer->ref_count == 0) {
    if (buffer->free)
      buffer->free (buffer, buffer->priv);
    g_free (buffer);
  }
}

static void
audioresample_buffer_free_mem (AudioresampleBuffer * buffer, void *priv)
{
  g_free (buffer->data);
}

static void
audioresample_buffer_free_subbuffer (AudioresampleBuffer * buffer, void *priv)
{
  audioresample_buffer_unref (buffer->parent);
}


AudioresampleBufferQueue *
audioresample_buffer_queue_new (void)
{
  return g_new0 (AudioresampleBufferQueue, 1);
}

int
audioresample_buffer_queue_get_depth (AudioresampleBufferQueue * queue)
{
  return queue->depth;
}

int
audioresample_buffer_queue_get_offset (AudioresampleBufferQueue * queue)
{
  return queue->offset;
}

void
audioresample_buffer_queue_free (AudioresampleBufferQueue * queue)
{
  GList *g;

  for (g = g_list_first (queue->buffers); g; g = g_list_next (g)) {
    audioresample_buffer_unref ((AudioresampleBuffer *) g->data);
  }
  g_list_free (queue->buffers);
  g_free (queue);
}

void
audioresample_buffer_queue_push (AudioresampleBufferQueue * queue,
    AudioresampleBuffer * buffer)
{
  queue->buffers = g_list_append (queue->buffers, buffer);
  queue->depth += buffer->length;
}

AudioresampleBuffer *
audioresample_buffer_queue_pull (AudioresampleBufferQueue * queue, int length)
{
  GList *g;
  AudioresampleBuffer *newbuffer;
  AudioresampleBuffer *buffer;
  AudioresampleBuffer *subbuffer;

  g_return_val_if_fail (length > 0, NULL);

  if (queue->depth < length) {
    return NULL;
  }

  RESAMPLE_LOG ("pulling %d, %d available", length, queue->depth);

  g = g_list_first (queue->buffers);
  buffer = g->data;

  if (buffer->length > length) {
    newbuffer = audioresample_buffer_new_subbuffer (buffer, 0, length);

    subbuffer = audioresample_buffer_new_subbuffer (buffer, length,
        buffer->length - length);
    g->data = subbuffer;
    audioresample_buffer_unref (buffer);
  } else {
    int offset = 0;

    newbuffer = audioresample_buffer_new_and_alloc (length);

    while (offset < length) {
      g = g_list_first (queue->buffers);
      buffer = g->data;

      if (buffer->length > length - offset) {
        int n = length - offset;

        memcpy (newbuffer->data + offset, buffer->data, n);
        subbuffer =
            audioresample_buffer_new_subbuffer (buffer, n, buffer->length - n);
        g->data = subbuffer;
        audioresample_buffer_unref (buffer);
        offset += n;
      } else {
        memcpy (newbuffer->data + offset, buffer->data, buffer->length);

        queue->buffers = g_list_delete_link (queue->buffers, g);
        offset += buffer->length;
        audioresample_buffer_unref (buffer);
      }
    }
  }

  queue->depth -= length;
  queue->offset += length;

  return newbuffer;
}

AudioresampleBuffer *
audioresample_buffer_queue_peek (AudioresampleBufferQueue * queue, int length)
{
  GList *g;
  AudioresampleBuffer *newbuffer;
  AudioresampleBuffer *buffer;
  int offset = 0;

  g_return_val_if_fail (length > 0, NULL);

  if (queue->depth < length) {
    return NULL;
  }

  RESAMPLE_LOG ("peeking %d, %d available", length, queue->depth);

  g = g_list_first (queue->buffers);
  buffer = g->data;
  if (buffer->length > length) {
    newbuffer = audioresample_buffer_new_subbuffer (buffer, 0, length);
  } else {
    newbuffer = audioresample_buffer_new_and_alloc (length);
    while (offset < length) {
      buffer = g->data;

      if (buffer->length > length - offset) {
        int n = length - offset;

        memcpy (newbuffer->data + offset, buffer->data, n);
        offset += n;
      } else {
        memcpy (newbuffer->data + offset, buffer->data, buffer->length);
        offset += buffer->length;
      }
      g = g_list_next (g);
    }
  }

  return newbuffer;
}

void
audioresample_buffer_queue_flush (AudioresampleBufferQueue * queue)
{
  GList *g;

  for (g = g_list_first (queue->buffers); g; g = g_list_next (g)) {
    audioresample_buffer_unref ((AudioresampleBuffer *) g->data);
  }
  g_list_free (queue->buffers);
  queue->buffers = NULL;
  queue->depth = 0;
  queue->offset = 0;
}
