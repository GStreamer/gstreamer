
#ifndef _LIBRFB_RFBCONTEXT_H_
#define _LIBRFB_RFBCONTEXT_H_

G_BEGIN_DECLS

#include <glib.h>

typedef struct _RfbContext
{
  RfbConnection *connection;

  guint8 *buffer1;
  void *buffer1_alloc;
  unsigned int buffer1_len;

  guint8 *buffer2;
  void *buffer2_alloc;
  unsigned int buffer2_len;

  char *name;
} RfbContext;

typedef struct _RfbRect
{
  RfbContext *context;

  unsigned int x_pos;
  unsigned int y_pos;
  unsigned int width;
  unsigned int height;
  unsigned int encoding_type;

  char *data;
} RfbRect;



G_END_DECLS

#endif
