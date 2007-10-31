#ifndef _LIBRFB_RFBCONTEXT_H_
#define _LIBRFB_RFBCONTEXT_H_

#include <glib.h>

G_BEGIN_DECLS typedef struct _RfbContext
{
  RfbConnection *connection;

  guint8 *buffer1;
  gpointer buffer1_alloc;
  guint buffer1_len;

  guint8 *buffer2;
  gpointer buffer2_alloc;
  guint buffer2_len;

  gchar *name;
} RfbContext;

typedef struct _RfbRect
{
  RfbContext *context;

  guint x_pos;
  guint y_pos;
  guint width;
  guint height;
  guint encoding_type;

  gchar *data;
} RfbRect;

G_END_DECLS
#endif
