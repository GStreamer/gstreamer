
#ifndef _LIBRFB_BYTESTREAM_H_
#define _LIBRFB_BYTESTREAM_H_

#include <glib.h>

#include <librfb/rfbbuffer.h>

G_BEGIN_DECLS

typedef struct _RfbBytestream RfbBytestream;

struct _RfbBytestream
{
  RfbBuffer * (*get_buffer) (int length, gpointer user_data);
  gpointer user_data;
  
  GList *buffer_list;
  int length;
  int offset;
};


RfbBytestream * rfb_bytestream_new (void);

int rfb_bytestream_read (RfbBytestream *bs, RfbBuffer **buffer, int len);
int rfb_bytestream_peek (RfbBytestream *bs, RfbBuffer **buffer, int len);
int rfb_bytestream_flush (RfbBytestream *bs, int len);


G_END_DECLS

#endif
