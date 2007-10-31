#ifndef _LIBRFB_BUFFER_H_
#define _LIBRFB_BUFFER_H_

#include <glib.h>

G_BEGIN_DECLS typedef struct _RfbBuffer RfbBuffer;
typedef void (*RfbBufferFreeFunc) (guint8 * data, gpointer priv);

struct _RfbBuffer
{
  RfbBufferFreeFunc free_data;
  gpointer buffer_private;

  guint8 *data;
  gint length;
};

RfbBuffer *rfb_buffer_new (void);
RfbBuffer *rfb_buffer_new_and_alloc (gint len);
void rfb_buffer_free (RfbBuffer * buffer);

G_END_DECLS
#endif
