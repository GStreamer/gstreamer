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
#include <gst/base/gstadapter.h>
#include "m3u8.h"
#include "gstfragmented.h"
#include <gst/uridownloader/gsturidownloader.h>
#ifdef HAVE_NETTLE
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
  GstBin parent;

  GstPad *sinkpad;
  GstPad *srcpad;
  gint srcpad_counter;

  gboolean have_group_id;
  guint group_id;

  GstBuffer *playlist;
  GstCaps *input_caps;
  GstUriDownloader *downloader;
  gchar *uri;                   /* Original playlist URI */
  GstM3U8Client *client;        /* M3U8 client */
  gboolean do_typefind;         /* Whether we need to typefind the next buffer */
  gboolean new_playlist;        /* Whether a new playlist is about to start and pads should be switched */

  /* Properties */
  guint fragments_cache;        /* number of fragments needed to be cached to start playing */
  gfloat bitrate_limit;         /* limit of the available bitrate to use */
  guint connection_speed;       /* Network connection speed in kbps (0 = unknown) */

  /* Streaming task */
  GstTask *stream_task;
  GRecMutex stream_lock;
  gboolean stop_stream_task;
  GMutex download_lock;         /* Used for protecting queue and the two conds */
  GCond download_cond;          /* Signalled when something is added to the queue */
  gboolean end_of_playlist;
  gint download_failed_count;
  gint64 next_download;

  /* Updates task */
  GstTask *updates_task;
  GRecMutex updates_lock;
  gint64 next_update;           /* Time of the next update */
  gboolean stop_updates_task;
  GMutex updates_timed_lock;
  GCond updates_timed_cond;     /* Signalled when the playlist should be updated */

  /* Position in the stream */
  GstSegment segment;
  gboolean need_segment;
  gboolean discont;

  /* Cache for the last key */
  gchar *key_url;
  GstFragment *key_fragment;

  /* Current download rate (bps) */
  gint current_download_rate;

  /* fragment download tooling */
  GstElement *src;
  GstPad *src_srcpad; /* handy link to src's src pad */
  GMutex fragment_download_lock;
  GCond fragment_download_cond;
  GstClockTime current_timestamp;
  GstClockTime current_duration;
  gboolean starting_fragment;
  gboolean reset_crypto;
  gint64 download_start_time;
  gint64 download_total_time;
  gint64 download_total_bytes;
  GstFlowReturn last_ret;
  GError *last_error;

  /* decryption tooling */
#ifdef HAVE_NETTLE
  struct CBC_CTX (struct aes_ctx, AES_BLOCK_SIZE) aes_ctx;
#else
  gcry_cipher_hd_t aes_ctx;
#endif
  const gchar *current_key;
  const guint8 *current_iv;
  GstAdapter *adapter; /* used to accumulate 16 bytes multiple chunks */
  GstBuffer *pending_buffer; /* decryption scenario:
                              * the last buffer can only be pushed when
                              * resized, so need to store and wait for
                              * EOS to know it is the last */
};

struct _GstHLSDemuxClass
{
  GstBinClass parent_class;
};

GType gst_hls_demux_get_type (void);

G_END_DECLS
#endif /* __GST_HLS_DEMUX_H__ */
