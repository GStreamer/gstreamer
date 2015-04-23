/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * gsthlsdemux.h:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_HLS_DEMUX_H__
#define __GST_HLS_DEMUX_H__

#include <gst/gst.h>
#include "m3u8.h"
#include "gstfragmented.h"
#include <gst/adaptivedemux/gstadaptivedemux.h>
#if defined(HAVE_OPENSSL)
#include <openssl/evp.h>
#elif defined(HAVE_NETTLE)
#include <nettle/aes.h>
#include <nettle/cbc.h>
#else
#include <gcrypt.h>
#endif

G_BEGIN_DECLS
#define GST_TYPE_HLS_DEMUX \
  (gst_hls_demux_get_type())
#define GST_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HLS_DEMUX,GstHLSDemux))
#define GST_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HLS_DEMUX,GstHLSDemuxClass))
#define GST_IS_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HLS_DEMUX))
#define GST_IS_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HLS_DEMUX))
#define GST_HLS_DEMUX_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_HLS_DEMUX,GstHLSDemuxClass))
#define GST_HLS_DEMUX_CAST(obj) \
  ((GstHLSDemux *)obj)
typedef struct _GstHLSDemux GstHLSDemux;
typedef struct _GstHLSDemuxClass GstHLSDemuxClass;

/**
 * GstHLSDemux:
 *
 * Opaque #GstHLSDemux data structure.
 */
struct _GstHLSDemux
{
  GstAdaptiveDemux parent;

  gint srcpad_counter;

  GstCaps *input_caps;
  gchar *uri;                   /* Original playlist URI */
  GstM3U8Client *client;        /* M3U8 client */
  gboolean do_typefind;         /* Whether we need to typefind the next buffer */
  gboolean new_playlist;        /* Whether a new playlist is about to start and pads should be switched */


  /* Streaming task */
  gint64 next_download;

  /* Cache for the last key */
  gchar *key_url;
  GstFragment *key_fragment;

  /* decryption tooling */
#if defined(HAVE_OPENSSL)
  EVP_CIPHER_CTX aes_ctx;
#elif defined(HAVE_NETTLE)
  struct CBC_CTX (struct aes_ctx, AES_BLOCK_SIZE) aes_ctx;
#else
  gcry_cipher_hd_t aes_ctx;
#endif
  gchar *current_key;
  guint8 *current_iv;
  GstBuffer *pending_buffer; /* decryption scenario:
                              * the last buffer can only be pushed when
                              * resized, so need to store and wait for
                              * EOS to know it is the last */

  gboolean reset_pts;
};

struct _GstHLSDemuxClass
{
  GstAdaptiveDemuxClass parent_class;
};

GType gst_hls_demux_get_type (void);

G_END_DECLS
#endif /* __GST_HLS_DEMUX_H__ */
