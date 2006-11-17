#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rfbdecoder.h"

#include <rfb.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define RFB_GET_UINT32(ptr) GUINT32_FROM_BE (*(guint32 *)(ptr))
#define RFB_GET_UINT16(ptr) GUINT16_FROM_BE (*(guint16 *)(ptr))
#define RFB_GET_UINT8(ptr) (*(guint8 *)(ptr))

#define RFB_SET_UINT32(ptr, val) (*(guint32 *)(ptr) = GUINT32_TO_BE (val))
#define RFB_SET_UINT16(ptr, val) (*(guint16 *)(ptr) = GUINT16_TO_BE (val))
#define RFB_SET_UINT8(ptr, val) (*(guint8 *)(ptr) = val)

#if 0
struct _RfbSocketPrivate
{
  gint fd;
  sockaddr sa;
}
#endif

static gboolean rfb_decoder_state_wait_for_protocol_version (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_wait_for_security (RfbDecoder * decoder);
static gboolean rfb_decoder_state_send_client_initialisation (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_wait_for_server_initialisation (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_normal (RfbDecoder * decoder);
static gboolean rfb_decoder_state_framebuffer_update (RfbDecoder * decoder);
static gboolean rfb_decoder_state_framebuffer_update_rectangle (RfbDecoder *
    decoder);
static gboolean rfb_decoder_state_set_colour_map_entries (RfbDecoder * decoder);
static gboolean rfb_decoder_state_server_cut_text (RfbDecoder * decoder);
static RfbBuffer *rfb_socket_get_buffer (gint length, gpointer user_data);
static gint rfb_socket_send_buffer (guint8 * buffer, gint length,
    gpointer user_data);

RfbDecoder *
rfb_decoder_new (void)
{
  RfbDecoder *decoder = g_new0 (RfbDecoder, 1);

  decoder->fd = -1;
  decoder->bytestream = rfb_bytestream_new ();

  return decoder;
}

void
rfb_decoder_free (RfbDecoder * decoder)
{
  g_return_if_fail (decoder != NULL);

  rfb_bytestream_free (decoder->bytestream);
  if (decoder->fd >= 0)
    close (decoder->fd);
}

void
rfb_decoder_use_file_descriptor (RfbDecoder * decoder, gint fd)
{
  g_return_if_fail (decoder != NULL);
  g_return_if_fail (decoder->fd == -1);
  g_return_if_fail (!decoder->inited);
  g_return_if_fail (fd >= 0);

  decoder->fd = fd;

  decoder->bytestream->get_buffer = rfb_socket_get_buffer;
  decoder->bytestream->user_data = (void *) fd;

  decoder->send_data = rfb_socket_send_buffer;
  decoder->buffer_handler_data = (void *) fd;
}

gboolean
rfb_decoder_connect_tcp (RfbDecoder * decoder, gchar * addr, guint port)
{
  gint fd;
  struct sockaddr_in sa;

  g_return_val_if_fail (decoder != NULL, FALSE);
  g_return_val_if_fail (decoder->fd == -1, FALSE);
  g_return_val_if_fail (addr != NULL, FALSE);

  fd = socket (PF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    return FALSE;

  sa.sin_family = AF_INET;
  sa.sin_port = htons (port);
  inet_pton (AF_INET, addr, &sa.sin_addr);
  if (connect (fd, (struct sockaddr *) &sa, sizeof (struct sockaddr)) == -1) {
    close (fd);
    return FALSE;
  }

  rfb_decoder_use_file_descriptor (decoder, fd);
  return TRUE;
}

gboolean
rfb_decoder_iterate (RfbDecoder * decoder)
{
  g_return_val_if_fail (decoder != NULL, FALSE);
  g_return_val_if_fail (decoder->fd != -1, FALSE);

  if (decoder->state == NULL) {
    decoder->state = rfb_decoder_state_wait_for_protocol_version;
  }
  // g_print ("iterating...\n");

  return decoder->state (decoder);
}

gint
rfb_decoder_send (RfbDecoder * decoder, guint8 * buffer, gint len)
{
  g_return_val_if_fail (decoder != NULL, 0);
  g_return_val_if_fail (decoder->fd != -1, 0);
  g_return_val_if_fail (buffer != NULL, 0);

  return decoder->send_data (buffer, len, decoder->buffer_handler_data);
}

void
rfb_decoder_send_update_request (RfbDecoder * decoder,
    gboolean incremental, gint x, gint y, gint width, gint height)
{
  guint8 data[10];

  g_return_if_fail (decoder != NULL);
  g_return_if_fail (decoder->fd != -1);

  data[0] = 3;
  data[1] = incremental;
  RFB_SET_UINT16 (data + 2, x);
  RFB_SET_UINT16 (data + 4, y);
  RFB_SET_UINT16 (data + 6, width);
  RFB_SET_UINT16 (data + 8, height);

  rfb_decoder_send (decoder, data, 10);
}

void
rfb_decoder_send_key_event (RfbDecoder * decoder, guint key, gboolean down_flag)
{
  guint8 data[8];

  g_return_if_fail (decoder != NULL);
  g_return_if_fail (decoder->fd != -1);

  data[0] = 4;
  data[1] = down_flag;
  RFB_SET_UINT16 (data + 2, 0);
  RFB_SET_UINT32 (data + 4, key);

  rfb_decoder_send (decoder, data, 8);
}

void
rfb_decoder_send_pointer_event (RfbDecoder * decoder,
    gint button_mask, gint x, gint y)
{
  guint8 data[6];

  g_return_if_fail (decoder != NULL);
  g_return_if_fail (decoder->fd != -1);

  data[0] = 5;
  data[1] = button_mask;
  RFB_SET_UINT16 (data + 2, x);
  RFB_SET_UINT16 (data + 4, y);

  rfb_decoder_send (decoder, data, 6);
}

static gboolean
rfb_decoder_state_wait_for_protocol_version (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  guint8 *data;
  gint ret;

  ret = rfb_bytestream_read (decoder->bytestream, &buffer, 12);
  if (ret < 12)
    return FALSE;

  data = buffer->data;

  g_assert (memcmp (buffer->data, "RFB 003.00", 10) == 0);
  // g_print ("\"%.11s\"\n", buffer->data);
  rfb_buffer_free (buffer);

  rfb_decoder_send (decoder, (guint8 *) "RFB 003.003\n", 12);

  decoder->state = rfb_decoder_state_wait_for_security;

  return TRUE;
}

static gboolean
rfb_decoder_state_wait_for_security (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  gint ret;

  ret = rfb_bytestream_read (decoder->bytestream, &buffer, 4);
  if (ret < 4)
    return FALSE;

  decoder->security_type = RFB_GET_UINT32 (buffer->data);
  // g_print ("security = %d\n", decoder->security_type);

  rfb_buffer_free (buffer);

  decoder->state = rfb_decoder_state_send_client_initialisation;
  return TRUE;
}

static gboolean
rfb_decoder_state_send_client_initialisation (RfbDecoder * decoder)
{
  guint8 shared_flag;

  shared_flag = decoder->shared_flag;
  rfb_decoder_send (decoder, &shared_flag, 1);

  decoder->state = rfb_decoder_state_wait_for_server_initialisation;
  return TRUE;
}

static gboolean
rfb_decoder_state_wait_for_server_initialisation (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  guint8 *data;
  gint ret;
  guint32 name_length;

  ret = rfb_bytestream_peek (decoder->bytestream, &buffer, 24);
  if (ret < 24)
    return FALSE;

  data = buffer->data;

  decoder->width = RFB_GET_UINT16 (data + 0);
  decoder->height = RFB_GET_UINT16 (data + 2);
  decoder->bpp = RFB_GET_UINT8 (data + 4);
  decoder->depth = RFB_GET_UINT8 (data + 5);
  decoder->big_endian = RFB_GET_UINT8 (data + 6);
  decoder->true_colour = RFB_GET_UINT8 (data + 7);
  decoder->red_max = RFB_GET_UINT16 (data + 8);
  decoder->green_max = RFB_GET_UINT16 (data + 10);
  decoder->blue_max = RFB_GET_UINT16 (data + 12);
  decoder->red_shift = RFB_GET_UINT8 (data + 14);
  decoder->green_shift = RFB_GET_UINT8 (data + 15);
  decoder->blue_shift = RFB_GET_UINT8 (data + 16);

  // g_print ("width: %d\n", decoder->width);
  // g_print ("height: %d\n", decoder->height);

  name_length = RFB_GET_UINT32 (data + 20);
  rfb_buffer_free (buffer);

  ret = rfb_bytestream_read (decoder->bytestream, &buffer, 24 + name_length);
  if (ret < 24 + name_length)
    return FALSE;

  decoder->name = g_strndup ((gchar *) (buffer->data) + 24, name_length);
  // g_print ("name: %s\n", decoder->name);
  rfb_buffer_free (buffer);

  decoder->state = rfb_decoder_state_normal;
  decoder->inited = TRUE;

  return TRUE;
}

static gboolean
rfb_decoder_state_normal (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  gint ret;
  gint message_type;

  ret = rfb_bytestream_read (decoder->bytestream, &buffer, 1);
  message_type = RFB_GET_UINT8 (buffer->data);

  switch (message_type) {
    case 0:
      decoder->state = rfb_decoder_state_framebuffer_update;
      break;
    case 1:
      decoder->state = rfb_decoder_state_set_colour_map_entries;
      break;
    case 2:
      /* bell, ignored */
      decoder->state = rfb_decoder_state_normal;
      break;
    case 3:
      decoder->state = rfb_decoder_state_server_cut_text;
      break;
    default:
      g_critical ("unknown message type %d", message_type);
  }

  rfb_buffer_free (buffer);

  return TRUE;
}

static gboolean
rfb_decoder_state_framebuffer_update (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  gint ret;

  ret = rfb_bytestream_read (decoder->bytestream, &buffer, 3);

  decoder->n_rects = RFB_GET_UINT16 (buffer->data + 1);
  decoder->state = rfb_decoder_state_framebuffer_update_rectangle;

  return TRUE;
}

static gboolean
rfb_decoder_state_framebuffer_update_rectangle (RfbDecoder * decoder)
{
  RfbBuffer *buffer;
  gint ret;
  gint x, y, w, h;
  gint encoding;
  gint size;

  ret = rfb_bytestream_peek (decoder->bytestream, &buffer, 12);
  if (ret < 12)
    return FALSE;

  x = RFB_GET_UINT16 (buffer->data + 0);
  y = RFB_GET_UINT16 (buffer->data + 2);
  w = RFB_GET_UINT16 (buffer->data + 4);
  h = RFB_GET_UINT16 (buffer->data + 6);
  encoding = RFB_GET_UINT32 (buffer->data + 8);

  if (encoding != 0)
    g_critical ("unimplemented encoding\n");

  rfb_buffer_free (buffer);

  size = w * h;
  ret = rfb_bytestream_read (decoder->bytestream, &buffer, size + 12);
  if (ret < size)
    return FALSE;

  if (decoder->paint_rect) {
    decoder->paint_rect (decoder, x, y, w, h, buffer->data + 12);
  }

  rfb_buffer_free (buffer);

  decoder->n_rects--;
  if (decoder->n_rects == 0) {
    decoder->state = rfb_decoder_state_normal;
  }
  return TRUE;
}

static gboolean
rfb_decoder_state_set_colour_map_entries (RfbDecoder * decoder)
{
  g_critical ("not implemented");

  return FALSE;
}

static gboolean
rfb_decoder_state_server_cut_text (RfbDecoder * decoder)
{
  g_critical ("not implemented");

  return FALSE;
}

static RfbBuffer *
rfb_socket_get_buffer (gint length, gpointer user_data)
{
  RfbBuffer *buffer;
  gint fd = (gint) user_data;
  gint ret;

  buffer = rfb_buffer_new ();

  buffer->data = g_malloc (length);
  buffer->free_data = (void *) g_free;

  // g_print ("calling read(%d, %p, %d)\n", fd, buffer->data, length);
  ret = read (fd, buffer->data, length);
  if (ret <= 0) {
    g_critical ("read: %s", strerror (errno));
    rfb_buffer_free (buffer);
    return NULL;
  }

  buffer->length = ret;

  return buffer;
}

static gint
rfb_socket_send_buffer (guint8 * buffer, gint length, gpointer user_data)
{
  gint fd = (gint) user_data;
  gint ret;

  // g_print ("calling write(%d, %p, %d)\n", fd, buffer, length);
  ret = write (fd, buffer, length);
  if (ret < 0) {
    g_critical ("write: %s", strerror (errno));
    return 0;
  }

  g_assert (ret == length);

  return ret;
}
