
#ifndef _LIBRFB_DECODER_H_
#define _LIBRFB_DECODER_H_

#include <glib.h>
#include <librfb/rfbbytestream.h>

G_BEGIN_DECLS

typedef struct _RfbDecoder RfbDecoder;

struct _RfbDecoder
{
  int (*send_data) (guint8 *buffer, int length, gpointer user_data);
  gpointer buffer_handler_data;

  RfbBytestream *bytestream;

  gpointer decoder_private;

  void (*paint_rect) (RfbDecoder *decoder, int x, int y, int w, int h,
      guint8 *data);
  void (*copy_rect) (RfbDecoder *decoder, int x, int y, int w, int h,
      int src_x, int src_y);

  /* settable properties */
  gboolean shared_flag;

  /* readable properties */
  gboolean inited;

  int protocol_major;
  int protocol_minor;
  unsigned int security_type;

  unsigned int width;
  unsigned int height;
  unsigned int bpp;
  unsigned int depth;
  gboolean big_endian;
  gboolean true_colour;
  unsigned int red_max;
  unsigned int green_max;
  unsigned int blue_max;
  unsigned int red_shift;
  unsigned int green_shift;
  unsigned int blue_shift;

  char *name;

  /* state information */
  gboolean (*state) (RfbDecoder *decoder);
  int n_rects;
};

#if 0
typedef struct _RfbRect
{
  RfbConnection *connection;

  unsigned int x_pos;
  unsigned int y_pos;
  unsigned int width;
  unsigned int height;
  unsigned int encoding_type;

  char *data;
} RfbRect;
#endif


RfbDecoder *rfb_decoder_new (void);
void rfb_decoder_use_file_descriptor (RfbDecoder * decoder, int fd);
void rfb_decoder_connect_tcp (RfbDecoder *decoder, char * addr, unsigned int port);
void rfb_decoder_set_peer (RfbDecoder * decoder);
gboolean rfb_decoder_iterate (RfbDecoder * decoder);
int rfb_decoder_send (RfbDecoder * decoder, guint8 *data, int len);
void rfb_decoder_send_update_request (RfbDecoder * decoder,
    gboolean incremental, int x, int y, int width, int height);
void rfb_decoder_send_key_event (RfbDecoder * decoder, unsigned int key,
    gboolean down_flag);
void rfb_decoder_send_pointer_event (RfbDecoder * decoder,
    int button_mask, int x, int y);

G_END_DECLS

#endif
