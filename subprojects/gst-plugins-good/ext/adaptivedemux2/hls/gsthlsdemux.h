/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
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
#include "gstisoff.h"
#include "gstadaptivedemux.h"
#if defined(HAVE_OPENSSL)
#include <openssl/evp.h>
#elif defined(HAVE_NETTLE)
#include <nettle/aes.h>
#include <nettle/cbc.h>
#elif defined(HAVE_LIBGCRYPT)
#include <gcrypt.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_HLS_DEMUX2 \
  (gst_hls_demux2_get_type())
#define GST_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HLS_DEMUX2,GstHLSDemux))
#define GST_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HLS_DEMUX2,GstHLSDemuxClass))
#define GST_IS_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HLS_DEMUX2))
#define GST_IS_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HLS_DEMUX2))
#define GST_HLS_DEMUX_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_HLS_DEMUX2,GstHLSDemuxClass))
#define GST_HLS_DEMUX_CAST(obj) \
  ((GstHLSDemux *)obj)

typedef struct _GstHLSDemux2 GstHLSDemux;
typedef struct _GstHLSDemux2Class GstHLSDemuxClass;

#define GST_TYPE_HLS_DEMUX_STREAM \
  (gst_hls_demux_stream_get_type())
#define GST_HLS_DEMUX_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HLS_DEMUX_STREAM,GstHLSDemuxStream))
#define GST_HLS_DEMUX_STREAM_CAST(obj) ((GstHLSDemuxStream *)obj)

typedef struct _GstHLSDemuxStream GstHLSDemuxStream;
typedef GstAdaptiveDemux2StreamClass GstHLSDemuxStreamClass;

typedef enum {
  GST_HLS_PARSER_NONE,
  GST_HLS_PARSER_MPEGTS,
  GST_HLS_PARSER_ID3,
  GST_HLS_PARSER_WEBVTT,
  GST_HLS_PARSER_ISOBMFF
} GstHLSParserType;

typedef enum {
  /* More data is needed to parse the fragment */
  GST_HLS_PARSER_RESULT_NEED_MORE_DATA,
  /* An error happened, discard the fragment */
  GST_HLS_PARSER_RESULT_ERROR,
  /* Parsing suceeded, it no longer needs to be called for the fragment */
  GST_HLS_PARSER_RESULT_DONE,
  /* The fragment wasn't the expected one. Current data must be dropped and
   * GST_ADAPTIVE_DEMUX_FLOW_RESTART_FRAGMENT returned to the parent class */
  GST_HLS_PARSER_RESULT_RESYNC
} GstHLSParserResult;

struct _GstHLSDemuxStream
{
  GstAdaptiveDemux2Stream adaptive_demux_stream;

  /* A stream either variants or renditions */
  gboolean is_variant;

  /* A copy of the demuxer flag, stored when the
   * stream is created, so it can't change after
   * the stream starts downloading things */
  gboolean llhls_enabled;

  /* Rendition-specific fields */
  GstStreamType rendition_type;	/* FIXME: Also used by variant streams */
  gchar *lang;
  gchar *name;
  GstHLSRenditionStream *current_rendition;
  /* rendition to switch to */
  GstHLSRenditionStream *pending_rendition;
  /* End of Rendition-specific fields */

  /* Whether the underlying playlist was fetched on creation */
  gboolean playlist_fetched;
  GstClockTime playlist_last_update_time;

  /* The media playlist currently used */
  GstHLSMediaPlaylist *playlist;

  /* The current header / init_file data */
  GstM3U8InitFile *init_file;

  /* The segment (from the above playlist) currently being used */
  GstM3U8MediaSegment *current_segment;
  /* When playing partial segments in LL-HLS, in_partial_segments is TRUE,
   * and part_no is the current part index in the current_segment */
  gboolean in_partial_segments;
  guint part_idx;

  /* Whether we need to typefind the next buffer */
  gboolean do_typefind;

  /* for collecting data until typefind succeeds */
  GstBuffer *pending_typefind_buffer;

  /* for chunking data into 16 byte multiples for decryption */
  GstAdapter *pending_encrypted_data;

 /* last decrypted buffer for pkcs7 unpadding.  We only know that it is the last
  * on ::finish_fragment() */
  GstBuffer *pending_decrypted_buffer;

  /* Current offset (in bytes) in fragment data we pushed downstream. Resets to
   * -1 at every fragment start */
  guint64 current_offset;

  gboolean reset_pts;

  /* decryption tooling */
#if defined(HAVE_OPENSSL)
# if OPENSSL_VERSION_NUMBER < 0x10100000L
  EVP_CIPHER_CTX aes_ctx;
# else
  EVP_CIPHER_CTX *aes_ctx;
# endif
#elif defined(HAVE_NETTLE)
  struct CBC_CTX (struct aes128_ctx, AES_BLOCK_SIZE) aes_ctx;
#elif defined(HAVE_LIBGCRYPT)
  gcry_cipher_hd_t aes_ctx;
#endif

  gchar     *current_key;
  guint8    *current_iv;

  /* The type of parser used for data handling */
  GstHLSParserType parser_type;

  /* Is content processing required ? */
  gboolean process_buffer_content;
  /* Data to be analyzed by  */
  GstBuffer *pending_segment_data;
  /* TRUE if pending_segment_data contains data from a header/index */
  gboolean pending_data_is_header;

  /* ISOBMFF */
  GstMoovBox *moov;

  /* Presentation offset to use and report. This value will be appended to all
   * "output" stream times. Not enabled (i.e 0) if variant is ISOBMFF
   */
  GstClockTime presentation_offset;

  gboolean pdt_tag_sent;
};

typedef struct {
  guint8 data[16];
} GstHLSKey;

/**
 * GstHLSDemux:
 *
 * Opaque #GstHLSDemux data structure.
 */
struct _GstHLSDemux2
{
  GstAdaptiveDemux parent;

  /* Initial bitrate to use before any bandwidth measurement */
  guint start_bitrate;

  /* Whether LL-HLS (Low Latency HLS) features are enabled */
  gboolean llhls_enabled;

  /* Decryption key cache: url => GstHLSKey */
  GHashTable *keys;
  GMutex      keys_lock;

  /* FIXME: check locking, protected automatically by manifest_lock already? */
  /* The master playlist with the available variant streams */
  GstHLSMasterPlaylist *master;

  GstHLSVariantStream  *current_variant;
  /* The variant to switch to */
  GstHLSVariantStream  *pending_variant;

  GstHLSDemuxStream *main_stream;

  /* Time Mappings (GstHLSTimeMap) */
  GList *mappings;
};

struct _GstHLSDemux2Class
{
  GstAdaptiveDemuxClass parent_class;
};


gchar *gst_hls_buf_to_utf8_text (GstBuffer * buf);

/* Private */

GstHLSParserResult gst_hlsdemux_handle_content_mpegts (GstHLSDemux       *demux,
						       GstHLSDemuxStream *hls_stream,
						       gboolean           draining,
						       GstBuffer        **buffer);

GstHLSParserResult gst_hlsdemux_handle_content_id3 (GstHLSDemux       *demux,
						    GstHLSDemuxStream *hls_stream,
						    gboolean           draining,
						    GstBuffer        **buffer);

GstHLSParserResult gst_hlsdemux_handle_content_isobmff (GstHLSDemux       *demux,
							GstHLSDemuxStream *hls_stream,
							gboolean           draining,
							GstBuffer        **buffer);

GstHLSParserResult gst_hlsdemux_handle_content_webvtt (GstHLSDemux        *demux,
						       GstHLSDemuxStream *hls_stream,
						       gboolean           draining,
						       GstBuffer        **buffer);

GstHLSParserResult gst_hlsdemux_handle_internal_time (GstHLSDemux       *demux,
						      GstHLSDemuxStream *hls_stream,
						      GstClockTime       internal_time);

GstClockTimeDiff gst_hls_internal_to_stream_time (GstHLSTimeMap *map,
						  GstClockTime   internal_time);

GstHLSTimeMap *gst_hls_find_time_map (GstHLSDemux * demux, gint64 dsn);

GType gst_hls_demux2_get_type (void);
GType gst_hls_demux_stream_get_type (void);

G_END_DECLS
#endif /* __GST_HLS_DEMUX_H__ */
