
#ifndef _LIBRFB_BUFFER_H_
#define _LIBRFB_BUFFER_H_

#include <glib.h>

G_BEGIN_DECLS

typedef struct _RfbBuffer RfbBuffer;

struct _RfbBuffer
{
  guint8 *data;
  int length;

  void (*free_data) (guint8 *data, gpointer priv);
  gpointer buffer_private;
};

RfbBuffer *rfb_buffer_new (void);
RfbBuffer *rfb_buffer_new_and_alloc (int len);
void rfb_buffer_free (RfbBuffer *buffer);

G_END_DECLS

#endif
