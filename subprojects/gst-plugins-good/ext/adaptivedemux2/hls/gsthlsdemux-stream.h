/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2015 Tim-Philipp Müller <tim@centricular.com>
 *
 * gsthlsdemux-stream.h:
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
#ifndef __GST_HLS_DEMUX_STREAM_H__
#define __GST_HLS_DEMUX_STREAM_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "m3u8.h"
#include "gstisoff.h"
#include <gstadaptivedemux.h>
#include "gsthlsdemux.h"
#include "gsthlsdemux-preloader.h"
#include "gsthlsdemux-playlist-loader.h"

#if defined(HAVE_OPENSSL)
#include <openssl/evp.h>
#elif defined(HAVE_NETTLE)
#include <nettle/aes.h>
#include <nettle/cbc.h>
#elif defined(HAVE_LIBGCRYPT)
#include <gcrypt.h>
#endif

G_BEGIN_DECLS

#define GST_TYPE_HLS_DEMUX_STREAM \
  (gst_hls_demux_stream_get_type())
#define GST_HLS_DEMUX_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HLS_DEMUX_STREAM,GstHLSDemuxStream))
#define GST_HLS_DEMUX_STREAM_CAST(obj) ((GstHLSDemuxStream *)obj)

#define GST_HLS_DEMUX_STREAM_GET_DEMUX(obj) (GST_HLS_DEMUX_CAST(GST_ADAPTIVE_DEMUX2_STREAM((obj))->demux))

typedef struct _GstHLSDemuxStream GstHLSDemuxStream;
typedef GstAdaptiveDemux2StreamClass GstHLSDemuxStreamClass;

typedef enum {
  GST_HLS_PARSER_NONE,
  GST_HLS_PARSER_MPEGTS,
  GST_HLS_PARSER_ID3,
  GST_HLS_PARSER_WEBVTT,
  GST_HLS_PARSER_ISOBMFF
} GstHLSParserType;

struct _GstHLSDemuxStream
{
  GstAdaptiveDemux2Stream adaptive_demux_stream;

  /* A stream either variants or renditions */
  gboolean is_variant;

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

  /* Playlist loading helper */
  GstHLSDemuxPlaylistLoader *playlistloader;

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

  /* Preload helper, that manages blocking preload downloads */
  GstHLSDemuxPreloader *preloader;

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

  /* The next segment needs to have the discont flag set on it. This is set when
   * a playlist update was detected as not being continuous/contiguous with the
   * previous one. */
  gboolean pending_discont;
};

GstFlowReturn
gst_hls_demux_stream_seek (GstAdaptiveDemux2Stream * stream, gboolean forward,
    GstSeekFlags flags, GstClockTimeDiff ts, GstClockTimeDiff * final_ts);

void
gst_hls_demux_stream_set_playlist_uri (GstHLSDemuxStream * stream, gchar * uri);

void
gst_hls_demux_stream_start_playlist_loading (GstHLSDemuxStream * stream);

GstFlowReturn gst_hls_demux_stream_check_current_playlist_uri (GstHLSDemuxStream * stream, gchar *uri);

void
gst_hls_demux_stream_clear_pending_data (GstHLSDemuxStream * hls_stream,
    gboolean force);

GstHLSParserResult gst_hlsdemux_stream_handle_internal_time (
						      GstHLSDemuxStream *hls_stream,
						      GstClockTime       internal_time);

G_END_DECLS
#endif /* __GST_HLS_DEMUX_STREAM_H__ */
