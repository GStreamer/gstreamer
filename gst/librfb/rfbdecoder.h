#ifndef _LIBRFB_DECODER_H_
#define _LIBRFB_DECODER_H_

#include <glib.h>
#include <librfb/rfbbytestream.h>

G_BEGIN_DECLS

typedef struct _RfbDecoder RfbDecoder;

struct _RfbDecoder
{
  /* callbacks */
  gint (*send_data) (guint8 *buffer, gint length, gpointer user_data);
  void (*paint_rect) (RfbDecoder *decoder, gint x, gint y, gint w, gint h,
      guint8 *data);
  void (*copy_rect) (RfbDecoder *decoder, gint x, gint y, gint w, gint h,
      gint src_x, gint src_y);
  gboolean (*state) (RfbDecoder *decoder);

  gpointer buffer_handler_data;

  gint fd;
  RfbBytestream *bytestream;

  gpointer decoder_private;

  /* settable properties */
  gboolean shared_flag;

  /* readable properties */
  gboolean inited;

  guint protocol_major;
  guint protocol_minor;
  guint security_type;

  guint width;
  guint height;
  guint bpp;
  guint depth;
  gboolean big_endian;
  gboolean true_colour;
  guint red_max;
  guint green_max;
  guint blue_max;
  guint red_shift;
  guint green_shift;
  guint blue_shift;

  gchar *name;

  gint n_rects;
};

#if 0
typedef struct _RfbRect
{
  RfbConnection *connection;

  guint x_pos;
  guint y_pos;
  guint width;
  guint height;
  guint encoding_type;

  gchar *data;
} RfbRect;
#endif

RfbDecoder *rfb_decoder_new                 (void);
void        rfb_decoder_free                (RfbDecoder * decoder);
void        rfb_decoder_use_file_descriptor (RfbDecoder * decoder,
                                             gint fd);
gboolean    rfb_decoder_connect_tcp         (RfbDecoder * decoder,
                                             gchar * addr,
                                             guint port);
gboolean    rfb_decoder_iterate             (RfbDecoder * decoder);
gint        rfb_decoder_send                (RfbDecoder * decoder,
                                             guint8 *data,
                                             gint len);
void        rfb_decoder_send_update_request (RfbDecoder * decoder,
                                             gboolean incremental,
                                             gint x,
                                             gint y,
                                             gint width,
                                             gint height);
void        rfb_decoder_send_key_event      (RfbDecoder * decoder,
                                             guint key,
                                             gboolean down_flag);
void        rfb_decoder_send_pointer_event  (RfbDecoder * decoder,
                                             gint button_mask,
                                             gint x,
                                             gint y);

G_END_DECLS

#endif
