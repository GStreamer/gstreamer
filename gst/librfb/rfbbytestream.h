#ifndef _LIBRFB_BYTESTREAM_H_
#define _LIBRFB_BYTESTREAM_H_

#include <glib.h>

#include <librfb/rfbbuffer.h>

G_BEGIN_DECLS

typedef struct _RfbBytestream RfbBytestream;

struct _RfbBytestream
{
  RfbBuffer * (* get_buffer) (gint length, gpointer user_data);

  gpointer user_data;
  
  GSList *buffer_list;
  gint length;
  gint offset;
};

RfbBytestream *rfb_bytestream_new     (void);
void           rfb_bytestream_free    (RfbBytestream * bs);

gint           rfb_bytestream_read    (RfbBytestream * bs,
                                       RfbBuffer ** buffer,
                                       gint len);
gint           rfb_bytestream_peek    (RfbBytestream * bs,
                                       RfbBuffer ** buffer,
                                       gint len);
gint           rfb_bytestream_flush   (RfbBytestream * bs,
                                       gint len);

G_END_DECLS

#endif
