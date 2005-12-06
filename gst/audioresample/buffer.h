
#ifndef __AUDIORESAMPLE_BUFFER_H__
#define __AUDIORESAMPLE_BUFFER_H__

#include <glib.h>

typedef struct _AudioresampleBuffer AudioresampleBuffer;
typedef struct _AudioresampleBufferQueue AudioresampleBufferQueue;

struct _AudioresampleBuffer
{
  unsigned char *data;
  int length;

  int ref_count;

  AudioresampleBuffer *parent;

  void (*free) (AudioresampleBuffer *, void *);
  void *priv;
  void *priv2;
};

struct _AudioresampleBufferQueue
{
  GList *buffers;
  int depth;
  int offset;
};

AudioresampleBuffer *   audioresample_buffer_new                (void);
AudioresampleBuffer *   audioresample_buffer_new_and_alloc      (int size);
AudioresampleBuffer *   audioresample_buffer_new_with_data      (void *data, int size);
AudioresampleBuffer *   audioresample_buffer_new_subbuffer      (AudioresampleBuffer * buffer, 
                                                                 int offset,
                                                                 int length);
void                    audioresample_buffer_ref                (AudioresampleBuffer * buffer);
void                    audioresample_buffer_unref              (AudioresampleBuffer * buffer);

AudioresampleBufferQueue *      
                        audioresample_buffer_queue_new          (void);
void                    audioresample_buffer_queue_free         (AudioresampleBufferQueue * queue);
int                     audioresample_buffer_queue_get_depth    (AudioresampleBufferQueue * queue);
int                     audioresample_buffer_queue_get_offset   (AudioresampleBufferQueue * queue);
void                    audioresample_buffer_queue_push         (AudioresampleBufferQueue * queue,
                                                                 AudioresampleBuffer * buffer);
AudioresampleBuffer *   audioresample_buffer_queue_pull         (AudioresampleBufferQueue * queue, int len);
AudioresampleBuffer *   audioresample_buffer_queue_peek         (AudioresampleBufferQueue * queue, int len);
void                    audioresample_buffer_queue_flush        (AudioresampleBufferQueue * queue);

#endif
