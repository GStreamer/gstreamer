/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Tim-Philipp Müller <tim centricular net>
 * (c) 2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * matroska-demux.c: matroska file/stream demuxer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* TODO: check CRC32 if present
 * TODO: there can be a segment after the first segment. Handle like
 *       chained oggs. Fixes #334082
 * TODO: Test samples: http://www.matroska.org/samples/matrix/index.html
 *                     http://samples.mplayerhq.hu/Matroska/
 * TODO: check if demuxing is done correct for all codecs according to spec
 * TODO: seeking with incomplete or without CUE
 */

/**
 * SECTION:element-matroskademux
 *
 * matroskademux demuxes a Matroska file into the different contained streams.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=/path/to/mkv ! matroskademux ! vorbisdec ! audioconvert ! audioresample ! autoaudiosink
 * ]| This pipeline demuxes a Matroska file and outputs the contained Vorbis audio.
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

/* For AVI compatibility mode
   and for fourcc stuff */
#include <gst/riff/riff-read.h>
#include <gst/riff/riff-ids.h>
#include <gst/riff/riff-media.h>

#include <gst/tag/tag.h>

#include <gst/base/gsttypefindhelper.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef HAVE_BZ2
#include <bzlib.h>
#endif

#include "lzo.h"

#include "matroska-demux.h"
#include "matroska-ids.h"

GST_DEBUG_CATEGORY_STATIC (matroskademux_debug);
#define GST_CAT_DEFAULT matroskademux_debug

#define DEBUG_ELEMENT_START(demux, ebml, element) \
    GST_DEBUG_OBJECT (demux, "Parsing " element " element at offset %" \
        G_GUINT64_FORMAT, ebml->offset)

#define DEBUG_ELEMENT_STOP(demux, ebml, element, ret) \
    GST_DEBUG_OBJECT (demux, "Parsing " element " element at offset %" \
        G_GUINT64_FORMAT " finished with '%s'", ebml->offset, \
	gst_flow_get_name (ret))

enum
{
  ARG_0,
  ARG_METADATA,
  ARG_STREAMINFO
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska")
    );

/* TODO: fill in caps! */

static GstStaticPadTemplate audio_src_templ =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate video_src_templ =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate subtitle_src_templ =
    GST_STATIC_PAD_TEMPLATE ("subtitle_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("text/plain; application/x-ssa; application/x-ass; "
        "application/x-usf; video/x-dvd-subpicture; "
        "subpicture/x-pgs; subtitle/x-kate; " "application/x-subtitle-unknown")
    );

static GstFlowReturn gst_matroska_demux_parse_contents (GstMatroskaDemux *
    demux);

/* element functions */
static void gst_matroska_demux_loop (GstPad * pad);

static gboolean gst_matroska_demux_element_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_matroska_demux_element_query (GstElement * element,
    GstQuery * query);

/* pad functions */
static gboolean gst_matroska_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_matroska_demux_sink_activate (GstPad * sinkpad);

static gboolean gst_matroska_demux_handle_seek_event (GstMatroskaDemux * demux,
    GstPad * pad, GstEvent * event);
static gboolean gst_matroska_demux_handle_src_event (GstPad * pad,
    GstEvent * event);
static const GstQueryType *gst_matroska_demux_get_src_query_types (GstPad *
    pad);
static gboolean gst_matroska_demux_handle_src_query (GstPad * pad,
    GstQuery * query);

static gboolean gst_matroska_demux_handle_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_matroska_demux_chain (GstPad * pad,
    GstBuffer * buffer);

static GstStateChangeReturn
gst_matroska_demux_change_state (GstElement * element,
    GstStateChange transition);
static void
gst_matroska_demux_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_matroska_demux_get_index (GstElement * element);

/* caps functions */
static GstCaps *gst_matroska_demux_video_caps (GstMatroskaTrackVideoContext
    * videocontext,
    const gchar * codec_id, guint8 * data, guint size, gchar ** codec_name);
static GstCaps *gst_matroska_demux_audio_caps (GstMatroskaTrackAudioContext
    * audiocontext,
    const gchar * codec_id, guint8 * data, guint size, gchar ** codec_name);
static GstCaps
    * gst_matroska_demux_subtitle_caps (GstMatroskaTrackSubtitleContext *
    subtitlecontext, const gchar * codec_id, gpointer data, guint size);

/* stream methods */
static void gst_matroska_demux_reset (GstElement * element);

GST_BOILERPLATE (GstMatroskaDemux, gst_matroska_demux, GstEbmlRead,
    GST_TYPE_EBML_READ);

static void
gst_matroska_demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subtitle_src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));

  gst_element_class_set_details_simple (element_class, "Matroska demuxer",
      "Codec/Demuxer",
      "Demuxes a Matroska Stream into video/audio/subtitles",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");
}

static void
gst_matroska_demux_finalize (GObject * object)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (object);

  if (demux->src) {
    g_ptr_array_free (demux->src, TRUE);
    demux->src = NULL;
  }

  if (demux->global_tags) {
    gst_tag_list_free (demux->global_tags);
    demux->global_tags = NULL;
  }

  g_object_unref (demux->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_matroska_demux_class_init (GstMatroskaDemuxClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (matroskademux_debug, "matroskademux", 0,
      "Matroska demuxer");

  gobject_class->finalize = gst_matroska_demux_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_change_state);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_element_send_event);
  gstelement_class->query =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_element_query);

  gstelement_class->set_index =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_set_index);
  gstelement_class->get_index =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_get_index);
}

static void
gst_matroska_demux_init (GstMatroskaDemux * demux,
    GstMatroskaDemuxClass * klass)
{
  demux->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_sink_activate));
  gst_pad_set_activatepull_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_sink_activate_pull));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_handle_sink_event));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
  GST_EBML_READ (demux)->sinkpad = demux->sinkpad;

  /* initial stream no. */
  demux->src = NULL;

  demux->writing_app = NULL;
  demux->muxing_app = NULL;
  demux->index = NULL;
  demux->global_tags = NULL;

  demux->adapter = gst_adapter_new ();

  /* finish off */
  gst_matroska_demux_reset (GST_ELEMENT (demux));
}

static void
gst_matroska_track_free (GstMatroskaTrackContext * track)
{
  g_free (track->codec_id);
  g_free (track->codec_name);
  g_free (track->name);
  g_free (track->language);
  g_free (track->codec_priv);
  g_free (track->codec_state);

  if (track->encodings != NULL) {
    int i;

    for (i = 0; i < track->encodings->len; ++i) {
      GstMatroskaTrackEncoding *enc = &g_array_index (track->encodings,
          GstMatroskaTrackEncoding,
          i);

      g_free (enc->comp_settings);
    }
    g_array_free (track->encodings, TRUE);
  }

  if (track->pending_tags)
    gst_tag_list_free (track->pending_tags);

  if (track->index_table)
    g_array_free (track->index_table, TRUE);

  g_free (track);
}

/*
 * Returns the aggregated GstFlowReturn.
 */
static GstFlowReturn
gst_matroska_demux_combine_flows (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * track, GstFlowReturn ret)
{
  guint i;

  /* store the value */
  track->last_flow = ret;

  /* any other error that is not-linked can be returned right away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  g_assert (demux->src->len == demux->num_streams);
  for (i = 0; i < demux->src->len; i++) {
    GstMatroskaTrackContext *ostream = g_ptr_array_index (demux->src, i);

    if (ostream == NULL)
      continue;

    ret = ostream->last_flow;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  GST_LOG_OBJECT (demux, "combined return %s", gst_flow_get_name (ret));
  return ret;
}

static void
gst_matroska_demux_reset (GstElement * element)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  guint i;

  GST_DEBUG_OBJECT (demux, "Resetting state");

  /* reset input */
  demux->state = GST_MATROSKA_DEMUX_STATE_START;

  /* clean up existing streams */
  if (demux->src) {
    g_assert (demux->src->len == demux->num_streams);
    for (i = 0; i < demux->src->len; i++) {
      GstMatroskaTrackContext *context = g_ptr_array_index (demux->src, i);

      if (context->pad != NULL)
        gst_element_remove_pad (GST_ELEMENT (demux), context->pad);

      gst_caps_replace (&context->caps, NULL);
      gst_matroska_track_free (context);
    }
    g_ptr_array_free (demux->src, TRUE);
  }
  demux->src = g_ptr_array_new ();

  demux->num_streams = 0;
  demux->num_a_streams = 0;
  demux->num_t_streams = 0;
  demux->num_v_streams = 0;

  /* reset media info */
  g_free (demux->writing_app);
  demux->writing_app = NULL;
  g_free (demux->muxing_app);
  demux->muxing_app = NULL;

  /* reset indexes */
  if (demux->index) {
    g_array_free (demux->index, TRUE);
    demux->index = NULL;
  }

  /* reset timers */
  demux->clock = NULL;
  demux->time_scale = 1000000;
  demux->created = G_MININT64;

  demux->index_parsed = FALSE;
  demux->tracks_parsed = FALSE;
  demux->segmentinfo_parsed = FALSE;
  demux->attachments_parsed = FALSE;

  g_list_foreach (demux->tags_parsed, (GFunc) gst_ebml_level_free, NULL);
  g_list_free (demux->tags_parsed);
  demux->tags_parsed = NULL;

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  demux->duration = -1;
  demux->last_stop_end = GST_CLOCK_TIME_NONE;
  demux->seek_block = 0;

  demux->offset = 0;
  demux->cluster_time = GST_CLOCK_TIME_NONE;
  demux->cluster_offset = 0;

  if (demux->close_segment) {
    gst_event_unref (demux->close_segment);
    demux->close_segment = NULL;
  }

  if (demux->new_segment) {
    gst_event_unref (demux->new_segment);
    demux->new_segment = NULL;
  }

  if (demux->element_index) {
    gst_object_unref (demux->element_index);
    demux->element_index = NULL;
  }
  demux->element_index_writer_id = -1;

  if (demux->global_tags) {
    gst_tag_list_free (demux->global_tags);
  }
  demux->global_tags = gst_tag_list_new ();
}

static gint
gst_matroska_demux_stream_from_num (GstMatroskaDemux * demux, guint track_num)
{
  guint n;

  g_assert (demux->src->len == demux->num_streams);
  for (n = 0; n < demux->src->len; n++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (demux->src, n);

    if (context->num == track_num) {
      return n;
    }
  }

  if (n == demux->num_streams)
    GST_WARNING_OBJECT (demux,
        "Failed to find corresponding pad for tracknum %d", track_num);

  return -1;
}

static gint
gst_matroska_demux_encoding_cmp (GstMatroskaTrackEncoding * a,
    GstMatroskaTrackEncoding * b)
{
  if (b->order > a->order)
    return 1;
  else if (b->order < a->order)
    return -1;
  else
    return 0;
}

static gboolean
gst_matroska_demux_encoding_order_unique (GArray * encodings, guint64 order)
{
  gint i;

  if (encodings == NULL || encodings->len == 0)
    return TRUE;

  for (i = 0; i < encodings->len; i++)
    if (g_array_index (encodings, GstMatroskaTrackEncoding, i).order == order)
      return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_matroska_demux_read_track_encoding (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * context)
{
  GstMatroskaTrackEncoding enc = { 0, };
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret;
  guint32 id;

  DEBUG_ELEMENT_START (demux, ebml, "ContentEncoding");
  /* Set default values */
  enc.scope = 1;
  /* All other default values are 0 */

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "ContentEncoding", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_CONTENTENCODINGORDER:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (!gst_matroska_demux_encoding_order_unique (context->encodings, num)) {
          GST_ERROR_OBJECT (demux, "ContentEncodingOrder %" G_GUINT64_FORMAT
              "is not unique for track %d", num, context->num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (demux, "ContentEncodingOrder: %" G_GUINT64_FORMAT,
            num);
        enc.order = num;
        break;
      }
      case GST_MATROSKA_ID_CONTENTENCODINGSCOPE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > 7 && num == 0) {
          GST_ERROR_OBJECT (demux, "Invalid ContentEncodingScope %"
              G_GUINT64_FORMAT, num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (demux, "ContentEncodingScope: %" G_GUINT64_FORMAT,
            num);
        enc.scope = num;

        break;
      }
      case GST_MATROSKA_ID_CONTENTENCODINGTYPE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > 1) {
          GST_ERROR_OBJECT (demux, "Invalid ContentEncodingType %"
              G_GUINT64_FORMAT, num);
          ret = GST_FLOW_ERROR;
          break;
        } else if (num != 0) {
          GST_ERROR_OBJECT (demux, "Encrypted tracks are not supported yet");
          ret = GST_FLOW_ERROR;
          break;
        }
        GST_DEBUG_OBJECT (demux, "ContentEncodingType: %" G_GUINT64_FORMAT,
            num);
        enc.type = num;
        break;
      }
      case GST_MATROSKA_ID_CONTENTCOMPRESSION:{

        DEBUG_ELEMENT_START (demux, ebml, "ContentCompression");

        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          break;

        while (ret == GST_FLOW_OK) {
          if ((ret = gst_ebml_peek_id (ebml, &demux->level_up,
                      &id)) != GST_FLOW_OK)
            break;

          if (demux->level_up) {
            demux->level_up--;
            break;
          }

          switch (id) {
            case GST_MATROSKA_ID_CONTENTCOMPALGO:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }
              if (num > 3) {
                GST_ERROR_OBJECT (demux, "Invalid ContentCompAlgo %"
                    G_GUINT64_FORMAT, num);
                ret = GST_FLOW_ERROR;
                break;
              }
              GST_DEBUG_OBJECT (demux, "ContentCompAlgo: %" G_GUINT64_FORMAT,
                  num);
              enc.comp_algo = num;

              break;
            }
            case GST_MATROSKA_ID_CONTENTCOMPSETTINGS:{
              guint8 *data;
              guint64 size;

              if ((ret =
                      gst_ebml_read_binary (ebml, &id, &data,
                          &size)) != GST_FLOW_OK) {
                break;
              }
              enc.comp_settings = data;
              enc.comp_settings_length = size;
              GST_DEBUG_OBJECT (demux,
                  "ContentCompSettings of size %" G_GUINT64_FORMAT, size);
              break;
            }
            default:
              GST_WARNING_OBJECT (demux,
                  "Unknown ContentCompression subelement 0x%x - ignoring", id);
              ret = gst_ebml_read_skip (ebml);
              break;
          }

          if (demux->level_up) {
            demux->level_up--;
            break;
          }
        }
        DEBUG_ELEMENT_STOP (demux, ebml, "ContentCompression", ret);
        break;
      }

      case GST_MATROSKA_ID_CONTENTENCRYPTION:
        GST_ERROR_OBJECT (demux, "Encrypted tracks not yet supported");
        gst_ebml_read_skip (ebml);
        ret = GST_FLOW_ERROR;
        break;
      default:
        GST_WARNING_OBJECT (demux,
            "Unknown ContentEncoding subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "ContentEncoding", ret);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED)
    return ret;

  /* TODO: Check if the combination of values is valid */

  g_array_append_val (context->encodings, enc);

  return ret;
}

static gboolean
gst_matroska_decompress_data (GstMatroskaTrackEncoding * enc,
    guint8 ** data_out, guint * size_out,
    GstMatroskaTrackCompressionAlgorithm algo)
{
  guint8 *new_data = NULL;
  guint new_size = 0;

  guint8 *data = *data_out;
  guint size = *size_out;

  gboolean ret = TRUE;

  if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_ZLIB) {
#ifdef HAVE_ZLIB
    /* zlib encoded data */
    z_stream zstream;
    guint orig_size;
    int result;

    orig_size = size;
    zstream.zalloc = (alloc_func) 0;
    zstream.zfree = (free_func) 0;
    zstream.opaque = (voidpf) 0;
    if (inflateInit (&zstream) != Z_OK) {
      GST_WARNING ("zlib initialization failed.");
      ret = FALSE;
      goto out;
    }
    zstream.next_in = (Bytef *) data;
    zstream.avail_in = orig_size;
    new_size = orig_size;
    new_data = g_malloc (new_size);
    zstream.avail_out = new_size;
    zstream.next_out = (Bytef *) new_data;

    do {
      result = inflate (&zstream, Z_NO_FLUSH);
      if (result != Z_OK && result != Z_STREAM_END) {
        GST_WARNING ("zlib decompression failed.");
        g_free (new_data);
        inflateEnd (&zstream);
        break;
      }
      new_size += 4000;
      new_data = g_realloc (new_data, new_size);
      zstream.next_out = (Bytef *) (new_data + zstream.total_out);
      zstream.avail_out += 4000;
    } while (zstream.avail_in != 0 && result != Z_STREAM_END);

    if (result != Z_STREAM_END) {
      ret = FALSE;
      goto out;
    } else {
      new_size = zstream.total_out;
      inflateEnd (&zstream);
    }
#else
    GST_WARNING ("zlib encoded tracks not supported.");
    ret = FALSE;
    goto out;
#endif
  } else if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_BZLIB) {
#ifdef HAVE_BZ2
    /* bzip2 encoded data */
    bz_stream bzstream;
    guint orig_size;
    int result;

    bzstream.bzalloc = NULL;
    bzstream.bzfree = NULL;
    bzstream.opaque = NULL;
    orig_size = size;

    if (BZ2_bzDecompressInit (&bzstream, 0, 0) != BZ_OK) {
      GST_WARNING ("bzip2 initialization failed.");
      ret = FALSE;
      goto out;
    }

    bzstream.next_in = (char *) data;
    bzstream.avail_in = orig_size;
    new_size = orig_size;
    new_data = g_malloc (new_size);
    bzstream.avail_out = new_size;
    bzstream.next_out = (char *) new_data;

    do {
      result = BZ2_bzDecompress (&bzstream);
      if (result != BZ_OK && result != BZ_STREAM_END) {
        GST_WARNING ("bzip2 decompression failed.");
        g_free (new_data);
        BZ2_bzDecompressEnd (&bzstream);
        break;
      }
      new_size += 4000;
      new_data = g_realloc (new_data, new_size);
      bzstream.next_out = (char *) (new_data + bzstream.total_out_lo32);
      bzstream.avail_out += 4000;
    } while (bzstream.avail_in != 0 && result != BZ_STREAM_END);

    if (result != BZ_STREAM_END) {
      ret = FALSE;
      goto out;
    } else {
      new_size = bzstream.total_out_lo32;
      BZ2_bzDecompressEnd (&bzstream);
    }
#else
    GST_WARNING ("bzip2 encoded tracks not supported.");
    ret = FALSE;
    goto out;
#endif
  } else if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_LZO1X) {
    /* lzo encoded data */
    int result;
    int orig_size, out_size;

    orig_size = size;
    out_size = size;
    new_size = size;
    new_data = g_malloc (new_size);

    do {
      orig_size = size;
      out_size = new_size;

      result = lzo1x_decode (new_data, &out_size, data, &orig_size);

      if (orig_size > 0) {
        new_size += 4000;
        new_data = g_realloc (new_data, new_size);
      }
    } while (orig_size > 0 && result == LZO_OUTPUT_FULL);

    new_size -= out_size;

    if (result != LZO_OUTPUT_FULL) {
      GST_WARNING ("lzo decompression failed");
      g_free (new_data);

      ret = FALSE;
      goto out;
    }

  } else if (algo == GST_MATROSKA_TRACK_COMPRESSION_ALGORITHM_HEADERSTRIP) {
    /* header stripped encoded data */
    if (enc->comp_settings_length > 0) {
      new_data = g_malloc (size + enc->comp_settings_length);
      new_size = size + enc->comp_settings_length;

      memcpy (new_data, enc->comp_settings, enc->comp_settings_length);
      memcpy (new_data + enc->comp_settings_length, data, size);
    }
  } else {
    g_assert_not_reached ();
  }

out:

  if (!ret) {
    *data_out = NULL;
    *size_out = 0;
  } else {
    *data_out = new_data;
    *size_out = new_size;
  }

  return ret;
}

static gboolean
gst_matroska_decode_data (GArray * encodings, guint8 ** data_out,
    guint * size_out, GstMatroskaTrackEncodingScope scope, gboolean free)
{
  guint8 *data;
  guint size;
  gboolean ret = TRUE;
  gint i;

  g_return_val_if_fail (encodings != NULL, FALSE);
  g_return_val_if_fail (data_out != NULL && *data_out != NULL, FALSE);
  g_return_val_if_fail (size_out != NULL, FALSE);

  data = *data_out;
  size = *size_out;

  for (i = 0; i < encodings->len; i++) {
    GstMatroskaTrackEncoding *enc =
        &g_array_index (encodings, GstMatroskaTrackEncoding, i);
    guint8 *new_data = NULL;
    guint new_size = 0;

    if ((enc->scope & scope) == 0)
      continue;

    /* Encryption not supported yet */
    if (enc->type != 0) {
      ret = FALSE;
      break;
    }

    new_data = data;
    new_size = size;

    ret =
        gst_matroska_decompress_data (enc, &new_data, &new_size,
        enc->comp_algo);

    if (!ret)
      break;

    if ((data == *data_out && free) || (data != *data_out))
      g_free (data);

    data = new_data;
    size = new_size;
  }

  if (!ret) {
    if ((data == *data_out && free) || (data != *data_out))
      g_free (data);

    *data_out = NULL;
    *size_out = 0;
  } else {
    *data_out = data;
    *size_out = size;
  }

  return ret;
}

static GstBuffer *
gst_matroska_decode_buffer (GstMatroskaTrackContext * context, GstBuffer * buf)
{
  guint8 *data;
  guint size;
  GstBuffer *new_buf;

  g_return_val_if_fail (GST_IS_BUFFER (buf), NULL);

  GST_DEBUG ("decoding buffer %p", buf);

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  g_return_val_if_fail (data != NULL && size > 0, buf);

  if (gst_matroska_decode_data (context->encodings, &data, &size,
          GST_MATROSKA_TRACK_ENCODING_SCOPE_FRAME, FALSE)) {
    new_buf = gst_buffer_new ();
    GST_BUFFER_MALLOCDATA (new_buf) = (guint8 *) data;
    GST_BUFFER_DATA (new_buf) = (guint8 *) data;
    GST_BUFFER_SIZE (new_buf) = size;

    gst_buffer_unref (buf);
    buf = new_buf;

    return buf;
  } else {
    GST_DEBUG ("decode data failed");
    gst_buffer_unref (buf);
    return NULL;
  }
}

static GstFlowReturn
gst_matroska_decode_content_encodings (GArray * encodings)
{
  gint i;

  if (encodings == NULL)
    return GST_FLOW_OK;

  for (i = 0; i < encodings->len; i++) {
    GstMatroskaTrackEncoding *enc =
        &g_array_index (encodings, GstMatroskaTrackEncoding, i);
    GstMatroskaTrackEncoding *enc2;
    guint8 *data = NULL;
    guint size;

    if ((enc->scope & GST_MATROSKA_TRACK_ENCODING_SCOPE_NEXT_CONTENT_ENCODING)
        == 0)
      continue;

    /* Encryption not supported yet */
    if (enc->type != 0)
      return GST_FLOW_ERROR;

    if (i + 1 >= encodings->len)
      return GST_FLOW_ERROR;

    enc2 = &g_array_index (encodings, GstMatroskaTrackEncoding, i + 1);

    if (enc->comp_settings_length == 0)
      continue;

    data = enc->comp_settings;
    size = enc->comp_settings_length;

    if (!gst_matroska_decompress_data (enc, &data, &size, enc->comp_algo))
      return GST_FLOW_ERROR;

    g_free (enc->comp_settings);

    enc->comp_settings = data;
    enc->comp_settings_length = size;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_matroska_demux_read_track_encodings (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * context)
{
  GstFlowReturn ret;
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;

  DEBUG_ELEMENT_START (demux, ebml, "ContentEncodings");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "ContentEncodings", ret);
    return ret;
  }

  context->encodings =
      g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaTrackEncoding), 1);

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_CONTENTENCODING:
        ret = gst_matroska_demux_read_track_encoding (demux, context);
        break;
      default:
        GST_WARNING_OBJECT (demux,
            "Unknown ContentEncodings subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "ContentEncodings", ret);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED)
    return ret;

  /* Sort encodings according to their order */
  g_array_sort (context->encodings,
      (GCompareFunc) gst_matroska_demux_encoding_cmp);

  return gst_matroska_decode_content_encodings (context->encodings);
}

static gboolean
gst_matroska_demux_tracknumber_unique (GstMatroskaDemux * demux, guint64 num)
{
  gint i;

  g_assert (demux->src->len == demux->num_streams);
  for (i = 0; i < demux->src->len; i++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (demux->src, i);

    if (context->num == num)
      return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_matroska_demux_add_stream (GstMatroskaDemux * demux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstMatroskaTrackContext *context;
  GstPadTemplate *templ = NULL;
  GstCaps *caps = NULL;
  gchar *padname = NULL;
  GstFlowReturn ret;
  guint32 id;
  GstTagList *list = NULL;
  gchar *codec = NULL;

  DEBUG_ELEMENT_START (demux, ebml, "TrackEntry");

  /* start with the master */
  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "TrackEntry", ret);
    return ret;
  }

  /* allocate generic... if we know the type, we'll g_renew()
   * with the precise type */
  context = g_new0 (GstMatroskaTrackContext, 1);
  g_ptr_array_add (demux->src, context);
  context->index = demux->num_streams;
  context->index_writer_id = -1;
  context->type = 0;            /* no type yet */
  context->default_duration = 0;
  context->pos = 0;
  context->set_discont = TRUE;
  context->timecodescale = 1.0;
  context->flags =
      GST_MATROSKA_TRACK_ENABLED | GST_MATROSKA_TRACK_DEFAULT |
      GST_MATROSKA_TRACK_LACING;
  context->last_flow = GST_FLOW_OK;
  demux->num_streams++;
  g_assert (demux->src->len == demux->num_streams);

  GST_DEBUG_OBJECT (demux, "Stream number %d", context->index);

  /* try reading the trackentry headers */
  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* track number (unique stream ID) */
      case GST_MATROSKA_ID_TRACKNUMBER:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          GST_ERROR_OBJECT (demux, "Invalid TrackNumber 0");
          ret = GST_FLOW_ERROR;
          break;
        } else if (!gst_matroska_demux_tracknumber_unique (demux, num)) {
          GST_ERROR_OBJECT (demux, "TrackNumber %" G_GUINT64_FORMAT
              " is not unique", num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (demux, "TrackNumber: %" G_GUINT64_FORMAT, num);
        context->num = num;
        break;
      }
        /* track UID (unique identifier) */
      case GST_MATROSKA_ID_TRACKUID:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          GST_ERROR_OBJECT (demux, "Invalid TrackUID 0");
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (demux, "TrackUID: %" G_GUINT64_FORMAT, num);
        context->uid = num;
        break;
      }

        /* track type (video, audio, combined, subtitle, etc.) */
      case GST_MATROSKA_ID_TRACKTYPE:{
        guint64 track_type;

        if ((ret = gst_ebml_read_uint (ebml, &id, &track_type)) != GST_FLOW_OK) {
          break;
        }

        if (context->type != 0 && context->type != track_type) {
          GST_WARNING_OBJECT (demux,
              "More than one tracktype defined in a TrackEntry - skipping");
          break;
        } else if (track_type < 1 || track_type > 254) {
          GST_WARNING_OBJECT (demux, "Invalid TrackType %" G_GUINT64_FORMAT,
              track_type);
          break;
        }

        GST_DEBUG_OBJECT (demux, "TrackType: %" G_GUINT64_FORMAT, track_type);

        /* ok, so we're actually going to reallocate this thing */
        switch (track_type) {
          case GST_MATROSKA_TRACK_TYPE_VIDEO:
            gst_matroska_track_init_video_context (&context);
            break;
          case GST_MATROSKA_TRACK_TYPE_AUDIO:
            gst_matroska_track_init_audio_context (&context);
            break;
          case GST_MATROSKA_TRACK_TYPE_SUBTITLE:
            gst_matroska_track_init_subtitle_context (&context);
            break;
          case GST_MATROSKA_TRACK_TYPE_COMPLEX:
          case GST_MATROSKA_TRACK_TYPE_LOGO:
          case GST_MATROSKA_TRACK_TYPE_BUTTONS:
          case GST_MATROSKA_TRACK_TYPE_CONTROL:
          default:
            GST_WARNING_OBJECT (demux,
                "Unknown or unsupported TrackType %" G_GUINT64_FORMAT,
                track_type);
            context->type = 0;
            break;
        }
        g_ptr_array_index (demux->src, demux->num_streams - 1) = context;
        break;
      }

        /* tracktype specific stuff for video */
      case GST_MATROSKA_ID_TRACKVIDEO:{
        GstMatroskaTrackVideoContext *videocontext;

        DEBUG_ELEMENT_START (demux, ebml, "TrackVideo");

        if (!gst_matroska_track_init_video_context (&context)) {
          GST_WARNING_OBJECT (demux,
              "TrackVideo element in non-video track - ignoring track");
          ret = GST_FLOW_ERROR;
          break;
        } else if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
          break;
        }
        videocontext = (GstMatroskaTrackVideoContext *) context;
        g_ptr_array_index (demux->src, demux->num_streams - 1) = context;

        while (ret == GST_FLOW_OK) {
          if ((ret =
                  gst_ebml_peek_id (ebml, &demux->level_up,
                      &id)) != GST_FLOW_OK)
            break;

          if (demux->level_up) {
            demux->level_up--;
            break;
          }

          switch (id) {
              /* Should be one level up but some broken muxers write it here. */
            case GST_MATROSKA_ID_TRACKDEFAULTDURATION:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackDefaultDuration 0");
                break;
              }

              GST_DEBUG_OBJECT (demux,
                  "TrackDefaultDuration: %" G_GUINT64_FORMAT, num);
              context->default_duration = num;
              break;
            }

              /* video framerate */
              /* NOTE: This one is here only for backward compatibility.
               * Use _TRACKDEFAULDURATION one level up. */
            case GST_MATROSKA_ID_VIDEOFRAMERATE:{
              gdouble num;

              if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num <= 0.0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackVideoFPS %lf", num);
                break;
              }

              GST_DEBUG_OBJECT (demux, "TrackVideoFrameRate: %lf", num);
              if (context->default_duration == 0)
                context->default_duration =
                    gst_gdouble_to_guint64 ((gdouble) GST_SECOND * (1.0 / num));
              videocontext->default_fps = num;
              break;
            }

              /* width of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYWIDTH:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackVideoDisplayWidth 0");
                break;
              }

              GST_DEBUG_OBJECT (demux,
                  "TrackVideoDisplayWidth: %" G_GUINT64_FORMAT, num);
              videocontext->display_width = num;
              break;
            }

              /* height of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYHEIGHT:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackVideoDisplayHeight 0");
                break;
              }

              GST_DEBUG_OBJECT (demux,
                  "TrackVideoDisplayHeight: %" G_GUINT64_FORMAT, num);
              videocontext->display_height = num;
              break;
            }

              /* width of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELWIDTH:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackVideoPixelWidth 0");
                break;
              }

              GST_DEBUG_OBJECT (demux,
                  "TrackVideoPixelWidth: %" G_GUINT64_FORMAT, num);
              videocontext->pixel_width = num;
              break;
            }

              /* height of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELHEIGHT:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackVideoPixelHeight 0");
                break;
              }

              GST_DEBUG_OBJECT (demux,
                  "TrackVideoPixelHeight: %" G_GUINT64_FORMAT, num);
              videocontext->pixel_height = num;
              break;
            }

              /* whether the video is interlaced */
            case GST_MATROSKA_ID_VIDEOFLAGINTERLACED:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num)
                context->flags |= GST_MATROSKA_VIDEOTRACK_INTERLACED;
              else
                context->flags &= ~GST_MATROSKA_VIDEOTRACK_INTERLACED;
              GST_DEBUG_OBJECT (demux, "TrackVideoInterlaced: %d",
                  (context->flags & GST_MATROSKA_VIDEOTRACK_INTERLACED) ? 1 :
                  0);
              break;
            }

              /* aspect ratio behaviour */
            case GST_MATROSKA_ID_VIDEOASPECTRATIOTYPE:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num != GST_MATROSKA_ASPECT_RATIO_MODE_FREE &&
                  num != GST_MATROSKA_ASPECT_RATIO_MODE_KEEP &&
                  num != GST_MATROSKA_ASPECT_RATIO_MODE_FIXED) {
                GST_WARNING_OBJECT (demux,
                    "Unknown TrackVideoAspectRatioType 0x%x", (guint) num);
                break;
              }
              GST_DEBUG_OBJECT (demux,
                  "TrackVideoAspectRatioType: %" G_GUINT64_FORMAT, num);
              videocontext->asr_mode = num;
              break;
            }

              /* colourspace (only matters for raw video) fourcc */
            case GST_MATROSKA_ID_VIDEOCOLOURSPACE:{
              guint8 *data;
              guint64 datalen;

              if ((ret =
                      gst_ebml_read_binary (ebml, &id, &data,
                          &datalen)) != GST_FLOW_OK)
                break;

              if (datalen != 4) {
                g_free (data);
                GST_WARNING_OBJECT (demux,
                    "Invalid TrackVideoColourSpace length %" G_GUINT64_FORMAT,
                    datalen);
                break;
              }

              memcpy (&videocontext->fourcc, data, 4);
              GST_DEBUG_OBJECT (demux,
                  "TrackVideoColourSpace: %" GST_FOURCC_FORMAT,
                  GST_FOURCC_ARGS (videocontext->fourcc));
              g_free (data);
              break;
            }

            default:
              GST_WARNING_OBJECT (demux,
                  "Unknown TrackVideo subelement 0x%x - ignoring", id);
              /* fall through */
            case GST_MATROSKA_ID_VIDEOSTEREOMODE:
            case GST_MATROSKA_ID_VIDEODISPLAYUNIT:
            case GST_MATROSKA_ID_VIDEOPIXELCROPBOTTOM:
            case GST_MATROSKA_ID_VIDEOPIXELCROPTOP:
            case GST_MATROSKA_ID_VIDEOPIXELCROPLEFT:
            case GST_MATROSKA_ID_VIDEOPIXELCROPRIGHT:
            case GST_MATROSKA_ID_VIDEOGAMMAVALUE:
              ret = gst_ebml_read_skip (ebml);
              break;
          }

          if (demux->level_up) {
            demux->level_up--;
            break;
          }
        }

        DEBUG_ELEMENT_STOP (demux, ebml, "TrackVideo", ret);
        break;
      }

        /* tracktype specific stuff for audio */
      case GST_MATROSKA_ID_TRACKAUDIO:{
        GstMatroskaTrackAudioContext *audiocontext;

        DEBUG_ELEMENT_START (demux, ebml, "TrackAudio");

        if (!gst_matroska_track_init_audio_context (&context)) {
          GST_WARNING_OBJECT (demux,
              "TrackAudio element in non-audio track - ignoring track");
          ret = GST_FLOW_ERROR;
          break;
        }

        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          break;

        audiocontext = (GstMatroskaTrackAudioContext *) context;
        g_ptr_array_index (demux->src, demux->num_streams - 1) = context;

        while (ret == GST_FLOW_OK) {
          if ((ret =
                  gst_ebml_peek_id (ebml, &demux->level_up,
                      &id)) != GST_FLOW_OK)
            break;

          if (demux->level_up) {
            demux->level_up--;
            break;
          }

          switch (id) {
              /* samplerate */
            case GST_MATROSKA_ID_AUDIOSAMPLINGFREQ:{
              gdouble num;

              if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK)
                break;


              if (num <= 0.0) {
                GST_WARNING_OBJECT (demux,
                    "Invalid TrackAudioSamplingFrequency %lf", num);
                break;
              }

              GST_DEBUG_OBJECT (demux, "TrackAudioSamplingFrequency: %lf", num);
              audiocontext->samplerate = num;
              break;
            }

              /* bitdepth */
            case GST_MATROSKA_ID_AUDIOBITDEPTH:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackAudioBitDepth 0");
                break;
              }

              GST_DEBUG_OBJECT (demux, "TrackAudioBitDepth: %" G_GUINT64_FORMAT,
                  num);
              audiocontext->bitdepth = num;
              break;
            }

              /* channels */
            case GST_MATROSKA_ID_AUDIOCHANNELS:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (demux, "Invalid TrackAudioChannels 0");
                break;
              }

              GST_DEBUG_OBJECT (demux, "TrackAudioChannels: %" G_GUINT64_FORMAT,
                  num);
              audiocontext->channels = num;
              break;
            }

            default:
              GST_WARNING_OBJECT (demux,
                  "Unknown TrackAudio subelement 0x%x - ignoring", id);
              /* fall through */
            case GST_MATROSKA_ID_AUDIOCHANNELPOSITIONS:
            case GST_MATROSKA_ID_AUDIOOUTPUTSAMPLINGFREQ:
              ret = gst_ebml_read_skip (ebml);
              break;
          }

          if (demux->level_up) {
            demux->level_up--;
            break;
          }
        }

        DEBUG_ELEMENT_STOP (demux, ebml, "TrackAudio", ret);

        break;
      }

        /* codec identifier */
      case GST_MATROSKA_ID_CODECID:{
        gchar *text;

        if ((ret = gst_ebml_read_ascii (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "CodecID: %s", GST_STR_NULL (text));
        context->codec_id = text;
        break;
      }

        /* codec private data */
      case GST_MATROSKA_ID_CODECPRIVATE:{
        guint8 *data;
        guint64 size;

        if ((ret =
                gst_ebml_read_binary (ebml, &id, &data, &size)) != GST_FLOW_OK)
          break;

        context->codec_priv = data;
        context->codec_priv_size = size;

        GST_DEBUG_OBJECT (demux, "CodecPrivate of size %" G_GUINT64_FORMAT,
            size);
        break;
      }

        /* name of the codec */
      case GST_MATROSKA_ID_CODECNAME:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "CodecName: %s", GST_STR_NULL (text));
        context->codec_name = text;
        break;
      }

        /* name of this track */
      case GST_MATROSKA_ID_TRACKNAME:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        context->name = text;
        GST_DEBUG_OBJECT (demux, "TrackName: %s", GST_STR_NULL (text));
        break;
      }

        /* language (matters for audio/subtitles, mostly) */
      case GST_MATROSKA_ID_TRACKLANGUAGE:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;


        context->language = text;

        /* fre-ca => fre */
        if (strlen (context->language) >= 4 && context->language[3] == '-')
          context->language[3] = '\0';

        GST_DEBUG_OBJECT (demux, "TrackLanguage: %s",
            GST_STR_NULL (context->language));
        break;
      }

        /* whether this is actually used */
      case GST_MATROSKA_ID_TRACKFLAGENABLED:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num)
          context->flags |= GST_MATROSKA_TRACK_ENABLED;
        else
          context->flags &= ~GST_MATROSKA_TRACK_ENABLED;

        GST_DEBUG_OBJECT (demux, "TrackEnabled: %d",
            (context->flags & GST_MATROSKA_TRACK_ENABLED) ? 1 : 0);
        break;
      }

        /* whether it's the default for this track type */
      case GST_MATROSKA_ID_TRACKFLAGDEFAULT:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num)
          context->flags |= GST_MATROSKA_TRACK_DEFAULT;
        else
          context->flags &= ~GST_MATROSKA_TRACK_DEFAULT;

        GST_DEBUG_OBJECT (demux, "TrackDefault: %d",
            (context->flags & GST_MATROSKA_TRACK_ENABLED) ? 1 : 0);
        break;
      }

        /* whether the track must be used during playback */
      case GST_MATROSKA_ID_TRACKFLAGFORCED:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num)
          context->flags |= GST_MATROSKA_TRACK_FORCED;
        else
          context->flags &= ~GST_MATROSKA_TRACK_FORCED;

        GST_DEBUG_OBJECT (demux, "TrackForced: %d",
            (context->flags & GST_MATROSKA_TRACK_ENABLED) ? 1 : 0);
        break;
      }

        /* lacing (like MPEG, where blocks don't end/start on frame
         * boundaries) */
      case GST_MATROSKA_ID_TRACKFLAGLACING:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num)
          context->flags |= GST_MATROSKA_TRACK_LACING;
        else
          context->flags &= ~GST_MATROSKA_TRACK_LACING;

        GST_DEBUG_OBJECT (demux, "TrackLacing: %d",
            (context->flags & GST_MATROSKA_TRACK_ENABLED) ? 1 : 0);
        break;
      }

        /* default length (in time) of one data block in this track */
      case GST_MATROSKA_ID_TRACKDEFAULTDURATION:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;


        if (num == 0) {
          GST_WARNING_OBJECT (demux, "Invalid TrackDefaultDuration 0");
          break;
        }

        GST_DEBUG_OBJECT (demux, "TrackDefaultDuration: %" G_GUINT64_FORMAT,
            num);
        context->default_duration = num;
        break;
      }

      case GST_MATROSKA_ID_CONTENTENCODINGS:{
        ret = gst_matroska_demux_read_track_encodings (demux, context);
        break;
      }

      case GST_MATROSKA_ID_TRACKTIMECODESCALE:{
        gdouble num;

        if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num <= 0.0) {
          GST_WARNING_OBJECT (demux, "Invalid TrackTimeCodeScale %lf", num);
          break;
        }

        GST_DEBUG_OBJECT (demux, "TrackTimeCodeScale: %lf", num);
        context->timecodescale = num;
        break;
      }

      default:
        GST_WARNING ("Unknown TrackEntry subelement 0x%x - ignoring", id);
        /* pass-through */

        /* we ignore these because they're nothing useful (i.e. crap)
         * or simply not implemented yet. */
      case GST_MATROSKA_ID_TRACKMINCACHE:
      case GST_MATROSKA_ID_TRACKMAXCACHE:
      case GST_MATROSKA_ID_MAXBLOCKADDITIONID:
      case GST_MATROSKA_ID_TRACKATTACHMENTLINK:
      case GST_MATROSKA_ID_TRACKOVERLAY:
      case GST_MATROSKA_ID_TRACKTRANSLATE:
      case GST_MATROSKA_ID_TRACKOFFSET:
      case GST_MATROSKA_ID_CODECSETTINGS:
      case GST_MATROSKA_ID_CODECINFOURL:
      case GST_MATROSKA_ID_CODECDOWNLOADURL:
      case GST_MATROSKA_ID_CODECDECODEALL:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "TrackEntry", ret);

  /* Decode codec private data if necessary */
  if (context->encodings && context->encodings->len > 0 && context->codec_priv
      && context->codec_priv_size > 0) {
    if (!gst_matroska_decode_data (context->encodings,
            &context->codec_priv, &context->codec_priv_size,
            GST_MATROSKA_TRACK_ENCODING_SCOPE_CODEC_DATA, TRUE)) {
      GST_WARNING_OBJECT (demux, "Decoding codec private data failed");
      ret = GST_FLOW_ERROR;
    }
  }

  if (context->type == 0 || context->codec_id == NULL || (ret != GST_FLOW_OK
          && ret != GST_FLOW_UNEXPECTED)) {
    if (ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED)
      GST_WARNING_OBJECT (ebml, "Unknown stream/codec in track entry header");

    demux->num_streams--;
    g_ptr_array_remove_index (demux->src, demux->num_streams);
    g_assert (demux->src->len == demux->num_streams);
    if (context) {
      gst_matroska_track_free (context);
    }

    return ret;
  }

  /* now create the GStreamer connectivity */
  switch (context->type) {
    case GST_MATROSKA_TRACK_TYPE_VIDEO:{
      GstMatroskaTrackVideoContext *videocontext =
          (GstMatroskaTrackVideoContext *) context;

      padname = g_strdup_printf ("video_%02d", demux->num_v_streams++);
      templ = gst_element_class_get_pad_template (klass, "video_%02d");
      caps = gst_matroska_demux_video_caps (videocontext,
          context->codec_id,
          (guint8 *) context->codec_priv, context->codec_priv_size, &codec);
      if (codec) {
        list = gst_tag_list_new ();
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_VIDEO_CODEC, codec, NULL);
        g_free (codec);
      }
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_AUDIO:{
      GstMatroskaTrackAudioContext *audiocontext =
          (GstMatroskaTrackAudioContext *) context;

      padname = g_strdup_printf ("audio_%02d", demux->num_a_streams++);
      templ = gst_element_class_get_pad_template (klass, "audio_%02d");
      caps = gst_matroska_demux_audio_caps (audiocontext,
          context->codec_id,
          context->codec_priv, context->codec_priv_size, &codec);
      if (codec) {
        list = gst_tag_list_new ();
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_AUDIO_CODEC, codec, NULL);
        g_free (codec);
      }
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_SUBTITLE:{
      GstMatroskaTrackSubtitleContext *subtitlecontext =
          (GstMatroskaTrackSubtitleContext *) context;

      padname = g_strdup_printf ("subtitle_%02d", demux->num_t_streams++);
      templ = gst_element_class_get_pad_template (klass, "subtitle_%02d");
      caps = gst_matroska_demux_subtitle_caps (subtitlecontext,
          context->codec_id, context->codec_priv, context->codec_priv_size);
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_COMPLEX:
    case GST_MATROSKA_TRACK_TYPE_LOGO:
    case GST_MATROSKA_TRACK_TYPE_BUTTONS:
    case GST_MATROSKA_TRACK_TYPE_CONTROL:
    default:
      /* we should already have quit by now */
      g_assert_not_reached ();
  }

  if ((context->language == NULL || *context->language == '\0') &&
      (context->type == GST_MATROSKA_TRACK_TYPE_AUDIO ||
          context->type == GST_MATROSKA_TRACK_TYPE_SUBTITLE)) {
    GST_LOG ("stream %d: language=eng (assuming default)", context->index);
    context->language = g_strdup ("eng");
  }

  if (context->language) {
    const gchar *lang;

    if (!list)
      list = gst_tag_list_new ();

    /* Matroska contains ISO 639-2B codes, we want ISO 639-1 */
    lang = gst_tag_get_language_code (context->language);
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_LANGUAGE_CODE, (lang) ? lang : context->language, NULL);
  }

  if (caps == NULL) {
    GST_WARNING_OBJECT (demux, "could not determine caps for stream with "
        "codec_id='%s'", context->codec_id);
    switch (context->type) {
      case GST_MATROSKA_TRACK_TYPE_VIDEO:
        caps = gst_caps_new_simple ("video/x-unknown", NULL);
        break;
      case GST_MATROSKA_TRACK_TYPE_AUDIO:
        caps = gst_caps_new_simple ("audio/x-unknown", NULL);
        break;
      case GST_MATROSKA_TRACK_TYPE_SUBTITLE:
        caps = gst_caps_new_simple ("application/x-subtitle-unknown", NULL);
        break;
      case GST_MATROSKA_TRACK_TYPE_COMPLEX:
      default:
        caps = gst_caps_new_simple ("application/x-matroska-unknown", NULL);
        break;
    }
    gst_caps_set_simple (caps, "codec-id", G_TYPE_STRING, context->codec_id,
        NULL);
  }

  /* the pad in here */
  context->pad = gst_pad_new_from_template (templ, padname);
  context->caps = caps;

  gst_pad_set_event_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_handle_src_event));
  gst_pad_set_query_type_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_get_src_query_types));
  gst_pad_set_query_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_handle_src_query));

  GST_INFO_OBJECT (demux, "Adding pad '%s' with caps %" GST_PTR_FORMAT,
      padname, caps);

  context->pending_tags = list;

  gst_pad_set_element_private (context->pad, context);

  gst_pad_use_fixed_caps (context->pad);
  gst_pad_set_caps (context->pad, context->caps);
  gst_pad_set_active (context->pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (demux), context->pad);

  g_free (padname);

  /* tadaah! */
  return ret;
}

static const GstQueryType *
gst_matroska_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    0
  };

  return query_types;
}

static gboolean
gst_matroska_demux_query (GstMatroskaDemux * demux, GstPad * pad,
    GstQuery * query)
{
  gboolean res = FALSE;
  GstMatroskaTrackContext *context = NULL;

  if (pad) {
    context = gst_pad_get_element_private (pad);
  }

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_TIME) {
        GST_OBJECT_LOCK (demux);
        if (context)
          gst_query_set_position (query, GST_FORMAT_TIME, context->pos);
        else
          gst_query_set_position (query, GST_FORMAT_TIME,
              demux->segment.last_stop);
        GST_OBJECT_UNLOCK (demux);
      } else if (format == GST_FORMAT_DEFAULT && context
          && context->default_duration) {
        GST_OBJECT_LOCK (demux);
        gst_query_set_position (query, GST_FORMAT_DEFAULT,
            context->pos / context->default_duration);
        GST_OBJECT_UNLOCK (demux);
      } else {
        GST_DEBUG_OBJECT (demux,
            "only position query in TIME and DEFAULT format is supported");
      }

      res = TRUE;
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      if (format == GST_FORMAT_TIME) {
        GST_OBJECT_LOCK (demux);
        gst_query_set_duration (query, GST_FORMAT_TIME, demux->duration);
        GST_OBJECT_UNLOCK (demux);
      } else if (format == GST_FORMAT_DEFAULT && context
          && context->default_duration) {
        GST_OBJECT_LOCK (demux);
        gst_query_set_duration (query, GST_FORMAT_DEFAULT,
            demux->duration / context->default_duration);
        GST_OBJECT_UNLOCK (demux);
      } else {
        GST_DEBUG_OBJECT (demux,
            "only duration query in TIME and DEFAULT format is supported");
      }

      res = TRUE;
      break;
    }

    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      res = TRUE;
      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);

      if (fmt != GST_FORMAT_TIME || !demux->index) {
        gst_query_set_seeking (query, fmt, FALSE, -1, -1);
      } else {
        gst_query_set_seeking (query, GST_FORMAT_TIME, TRUE, 0,
            demux->duration);
      }

      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  return res;
}

static gboolean
gst_matroska_demux_element_query (GstElement * element, GstQuery * query)
{
  return gst_matroska_demux_query (GST_MATROSKA_DEMUX (element), NULL, query);
}

static gboolean
gst_matroska_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean ret;
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));

  ret = gst_matroska_demux_query (demux, pad, query);

  gst_object_unref (demux);

  return ret;
}

static gint
gst_matroska_index_seek_find (GstMatroskaIndex * i1, GstClockTime * time,
    gpointer user_data)
{
  if (i1->time < *time)
    return -1;
  else if (i1->time > *time)
    return 1;
  else
    return 0;
}

static GstMatroskaIndex *
gst_matroskademux_do_index_seek (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * track, gint64 seek_pos, gint64 segment_stop,
    gboolean keyunit)
{
  GstMatroskaIndex *entry = NULL;
  GArray *index;

  if (!demux->index || !demux->index->len)
    return NULL;

  /* find entry just before or at the requested position */
  if (track && track->index_table)
    index = track->index_table;
  else
    index = demux->index;

  entry =
      gst_util_array_binary_search (index->data, index->len,
      sizeof (GstMatroskaIndex),
      (GCompareDataFunc) gst_matroska_index_seek_find, GST_SEARCH_MODE_BEFORE,
      &seek_pos, NULL);

  if (entry == NULL)
    entry = &g_array_index (index, GstMatroskaIndex, 0);

  return entry;
}

/* takes ownership of taglist */
static void
gst_matroska_demux_found_global_tag (GstMatroskaDemux * demux,
    GstTagList * taglist)
{
  if (demux->global_tags) {
    /* nothing sent yet, add to cache */
    gst_tag_list_insert (demux->global_tags, taglist, GST_TAG_MERGE_APPEND);
    gst_tag_list_free (taglist);
  } else {
    /* hm, already sent, no need to cache and wait anymore */
    GST_DEBUG_OBJECT (demux, "Sending late global tags %" GST_PTR_FORMAT,
        taglist);
    gst_element_found_tags (GST_ELEMENT (demux), taglist);
  }
}

/* returns FALSE if there are no pads to deliver event to,
 * otherwise TRUE (whatever the outcome of event sending),
 * takes ownership of the passed event! */
static gboolean
gst_matroska_demux_send_event (GstMatroskaDemux * demux, GstEvent * event)
{
  gboolean is_newsegment;
  gboolean ret = FALSE;
  gint i;

  g_return_val_if_fail (event != NULL, FALSE);

  GST_DEBUG_OBJECT (demux, "Sending event of type %s to all source pads",
      GST_EVENT_TYPE_NAME (event));

  is_newsegment = (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT);

  /* FIXME: access to demux->src is not thread-safe here */
  g_assert (demux->src->len == demux->num_streams);
  for (i = 0; i < demux->src->len; i++) {
    GstMatroskaTrackContext *stream;

    stream = g_ptr_array_index (demux->src, i);
    gst_event_ref (event);
    gst_pad_push_event (stream->pad, event);
    ret = TRUE;

    /* FIXME: send global tags before stream tags */
    if (G_UNLIKELY (is_newsegment && stream->pending_tags != NULL)) {
      GST_DEBUG_OBJECT (demux, "Sending pending_tags %p for pad %s:%s : %"
          GST_PTR_FORMAT, stream->pending_tags,
          GST_DEBUG_PAD_NAME (stream->pad), stream->pending_tags);
      gst_element_found_tags_for_pad (GST_ELEMENT (demux), stream->pad,
          stream->pending_tags);
      stream->pending_tags = NULL;
    }
  }

  if (G_UNLIKELY (is_newsegment && demux->global_tags != NULL)) {
    gst_tag_list_add (demux->global_tags, GST_TAG_MERGE_REPLACE,
        GST_TAG_CONTAINER_FORMAT, "Matroska", NULL);
    GST_DEBUG_OBJECT (demux, "Sending global_tags %p : %" GST_PTR_FORMAT,
        demux->global_tags, demux->global_tags);
    gst_element_found_tags (GST_ELEMENT (demux), demux->global_tags);
    demux->global_tags = NULL;
  }

  gst_event_unref (event);
  return ret;
}

static gboolean
gst_matroska_demux_element_send_event (GstElement * element, GstEvent * event)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  gboolean res;

  g_return_val_if_fail (event != NULL, FALSE);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
    res = gst_matroska_demux_handle_seek_event (demux, NULL, event);
  } else {
    GST_WARNING_OBJECT (demux, "Unhandled event of type %s",
        GST_EVENT_TYPE_NAME (event));
    res = FALSE;
  }
  gst_event_unref (event);
  return res;
}

static gboolean
gst_matroska_demux_handle_seek_event (GstMatroskaDemux * demux,
    GstPad * pad, GstEvent * event)
{
  GstMatroskaIndex *entry = NULL;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  GstFormat format;
  gboolean flush, keyunit;
  gdouble rate;
  gint64 cur, stop;
  gint i;
  GstMatroskaTrackContext *track = NULL;
  GstSegment seeksegment = { 0, };
  gboolean update;

  if (pad)
    track = gst_pad_get_element_private (pad);

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  /* we can only seek on time */
  if (format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (demux, "Can only seek on TIME");
    return FALSE;
  }

  /* cannot yet do backwards playback */
  if (rate <= 0.0) {
    GST_DEBUG_OBJECT (demux, "Can only seek with positive rate");
    return FALSE;
  }

  /* copy segment, we need this because we still need the old
   * segment when we close the current segment. */
  memcpy (&seeksegment, &demux->segment, sizeof (GstSegment));

  if (event) {
    GST_DEBUG_OBJECT (demux, "configuring seek");
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (demux, "New segment %" GST_SEGMENT_FORMAT, &seeksegment);

  /* check sanity before we start flushing and all that */
  GST_OBJECT_LOCK (demux);
  if ((entry =
          gst_matroskademux_do_index_seek (demux, track,
              seeksegment.last_stop, -1, FALSE)) == NULL) {
    GST_DEBUG_OBJECT (demux, "No matching seek entry in index");
    GST_OBJECT_UNLOCK (demux);
    return FALSE;
  }
  GST_DEBUG_OBJECT (demux, "Seek position looks sane");
  GST_OBJECT_UNLOCK (demux);

  flush = !!(flags & GST_SEEK_FLAG_FLUSH);
  keyunit = !!(flags & GST_SEEK_FLAG_KEY_UNIT);

  if (flush) {
    GST_DEBUG_OBJECT (demux, "Starting flush");
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_start ());
    gst_matroska_demux_send_event (demux, gst_event_new_flush_start ());
  } else {
    GST_DEBUG_OBJECT (demux, "Non-flushing seek, pausing task");
    gst_pad_pause_task (demux->sinkpad);
  }

  /* now grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever. */
  GST_DEBUG_OBJECT (demux, "Waiting for streaming to stop");
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  GST_OBJECT_LOCK (demux);

  /* seek (relative to matroska segment) */
  if (gst_ebml_read_seek (GST_EBML_READ (demux),
          entry->pos + demux->ebml_segment_start) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Failed to seek to offset %" G_GUINT64_FORMAT,
        entry->pos + demux->ebml_segment_start);
    goto seek_error;
  }

  GST_DEBUG_OBJECT (demux, "Seeked to offset %" G_GUINT64_FORMAT, entry->pos +
      demux->ebml_segment_start);

  if (keyunit) {
    GST_DEBUG_OBJECT (demux, "seek to key unit, adjusting segment start to %"
        GST_TIME_FORMAT, GST_TIME_ARGS (entry->time));
    seeksegment.start = entry->time;
    seeksegment.last_stop = entry->time;
    seeksegment.time = entry->time;
  }

  GST_OBJECT_UNLOCK (demux);

  if (flush) {
    GST_DEBUG_OBJECT (demux, "Stopping flush");
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop ());
    gst_matroska_demux_send_event (demux, gst_event_new_flush_stop ());
  } else if (demux->segment_running) {
    GST_DEBUG_OBJECT (demux, "Closing currently running segment");

    GST_OBJECT_LOCK (demux);
    if (demux->close_segment)
      gst_event_unref (demux->close_segment);

    demux->close_segment = gst_event_new_new_segment (TRUE,
        demux->segment.rate, GST_FORMAT_TIME, demux->segment.start,
        demux->segment.last_stop, demux->segment.time);
    GST_OBJECT_UNLOCK (demux);
  }

  GST_OBJECT_LOCK (demux);
  /* now update the real segment info */
  GST_DEBUG_OBJECT (demux, "Committing new seek segment");
  memcpy (&demux->segment, &seeksegment, sizeof (GstSegment));
  GST_OBJECT_UNLOCK (demux);

  /* notify start of new segment */
  if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    GstMessage *msg;

    msg = gst_message_new_segment_start (GST_OBJECT (demux),
        GST_FORMAT_TIME, demux->segment.start);
    gst_element_post_message (GST_ELEMENT (demux), msg);
  }

  GST_OBJECT_LOCK (demux);
  if (demux->new_segment)
    gst_event_unref (demux->new_segment);
  demux->new_segment = gst_event_new_new_segment_full (FALSE,
      demux->segment.rate, demux->segment.applied_rate, demux->segment.format,
      demux->segment.last_stop, demux->segment.stop, demux->segment.time);
  GST_OBJECT_UNLOCK (demux);

  /* update the time */
  g_assert (demux->src->len == demux->num_streams);
  for (i = 0; i < demux->src->len; i++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (demux->src, i);
    context->pos = entry->time;
    context->set_discont = TRUE;
    context->last_flow = GST_FLOW_OK;
  }
  demux->segment.last_stop = entry->time;
  demux->seek_block = entry->block;
  demux->last_stop_end = GST_CLOCK_TIME_NONE;

  /* restart our task since it might have been stopped when we did the
   * flush. */
  demux->segment_running = TRUE;
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_matroska_demux_loop,
      demux->sinkpad);

  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;

seek_error:
  {
    GST_OBJECT_UNLOCK (demux);
    GST_PAD_STREAM_UNLOCK (demux->sinkpad);
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL), ("Got a seek error"));
    return FALSE;
  }
}

static gboolean
gst_matroska_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_matroska_demux_handle_seek_event (demux, pad, event);
      gst_event_unref (event);
      break;

      /* events we don't need to handle */
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_QOS:
      gst_event_unref (event);
      res = FALSE;
      break;

    case GST_EVENT_LATENCY:
    default:
      res = gst_pad_push_event (demux->sinkpad, event);
      break;
  }

  gst_object_unref (demux);

  return res;
}

static GstFlowReturn
gst_matroska_demux_parse_header (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret;
  gchar *doctype;
  guint version;

  if ((ret = gst_ebml_read_header (ebml, &doctype, &version)) != GST_FLOW_OK)
    return ret;

  if (!doctype || strcmp (doctype, "matroska") != 0) {
    GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
        ("Input is not a matroska stream (doctype=%s)",
            doctype ? doctype : "none"));
    g_free (doctype);
    return GST_FLOW_ERROR;
  }
  g_free (doctype);
  if (version > 2) {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("Demuxer version (2) is too old to read stream version %d", version));
    return GST_FLOW_ERROR;
  }

  return ret;
}

static GstFlowReturn
gst_matroska_demux_init_stream (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (demux, "Init stream");

  if ((ret = gst_matroska_demux_parse_header (demux)) != GST_FLOW_OK)
    return ret;

  /* find segment, must be the next element but search as long as
   * we find it anyway */
  while (TRUE) {
    guint last_level;

    if ((ret = gst_ebml_peek_id (ebml, &last_level, &id)) != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (demux, "gst_ebml_peek_id() failed!");
      return ret;
    }

    if (id == GST_MATROSKA_ID_SEGMENT)
      break;

    /* oi! */
    GST_WARNING_OBJECT (demux,
        "Expected a Segment ID (0x%x), but received 0x%x!",
        GST_MATROSKA_ID_SEGMENT, id);

    if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
      return ret;
  }

  /* we now have a EBML segment */
  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "gst_ebml_read_master() failed!");
    return ret;
  }

  GST_DEBUG_OBJECT (demux, "Found Segment start at offset %" G_GUINT64_FORMAT,
      ebml->offset);
  /* seeks are from the beginning of the segment,
   * after the segment ID/length */
  demux->ebml_segment_start = ebml->offset;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_tracks (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;

  DEBUG_ELEMENT_START (demux, ebml, "Tracks");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Tracks", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one track within the "all-tracks" header */
      case GST_MATROSKA_ID_TRACKENTRY:
        ret = gst_matroska_demux_add_stream (demux);
        break;

      default:
        GST_WARNING ("Unknown Track subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }
  DEBUG_ELEMENT_STOP (demux, ebml, "Tracks", ret);

  demux->tracks_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_index_cuetrack (GstMatroskaDemux * demux,
    guint * nentries)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret;
  GstMatroskaIndex idx;

  idx.pos = (guint64) - 1;
  idx.track = 0;
  idx.time = GST_CLOCK_TIME_NONE;
  idx.block = 1;

  DEBUG_ELEMENT_START (demux, ebml, "CueTrackPositions");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "CueTrackPositions", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* track number */
      case GST_MATROSKA_ID_CUETRACK:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          idx.track = 0;
          GST_WARNING_OBJECT (demux, "Invalid CueTrack 0");
          break;
        }

        GST_DEBUG_OBJECT (demux, "CueTrack: %" G_GUINT64_FORMAT, num);
        idx.track = num;
        break;
      }

        /* position in file */
      case GST_MATROSKA_ID_CUECLUSTERPOSITION:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > G_MAXINT64) {
          GST_WARNING_OBJECT (demux, "CueClusterPosition %" G_GUINT64_FORMAT
              " too large", num);
          break;
        }

        idx.pos = num;
        break;
      }

        /* number of block in the cluster */
      case GST_MATROSKA_ID_CUEBLOCKNUMBER:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          GST_WARNING_OBJECT (demux, "Invalid CueBlockNumber 0");
          break;
        }

        GST_DEBUG_OBJECT (demux, "CueBlockNumber: %" G_GUINT64_FORMAT, num);
        idx.block = num;

        /* mild sanity check, disregard strange cases ... */
        if (idx.block > G_MAXUINT16) {
          GST_DEBUG_OBJECT (demux, "... looks suspicious, ignoring");
          idx.block = 1;
        }
        break;
      }

      default:
        GST_WARNING ("Unknown CueTrackPositions subelement 0x%x - ignoring",
            id);
        /* fall-through */

      case GST_MATROSKA_ID_CUECODECSTATE:
      case GST_MATROSKA_ID_CUEREFERENCE:
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          break;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "CueTrackPositions", ret);

  if ((ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED)
      && idx.pos != (guint64) - 1 && idx.track > 0) {
    g_array_append_val (demux->index, idx);
    (*nentries)++;
  } else if (ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED) {
    GST_DEBUG_OBJECT (demux, "CueTrackPositions without valid content");
  }

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_index_pointentry (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  guint nentries = 0;

  DEBUG_ELEMENT_START (demux, ebml, "CuePoint");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "CuePoint", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_CUETIME:
      {
        if ((ret = gst_ebml_read_uint (ebml, &id, &time)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "CueTime: %" G_GUINT64_FORMAT, time);
        time = time * demux->time_scale;
        break;
      }

        /* position in the file + track to which it belongs */
      case GST_MATROSKA_ID_CUETRACKPOSITIONS:
      {
        if ((ret =
                gst_matroska_demux_parse_index_cuetrack (demux,
                    &nentries)) != GST_FLOW_OK)
          break;
        break;
      }

      default:
        GST_WARNING_OBJECT (demux,
            "Unknown CuePoint subelement 0x%x - ignoring", id);
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          break;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "CuePoint", ret);

  if (nentries > 0) {
    if (time == GST_CLOCK_TIME_NONE) {
      GST_WARNING_OBJECT (demux, "CuePoint without valid time");
      g_array_remove_range (demux->index, demux->index->len - nentries,
          nentries);
    } else {
      gint i;

      for (i = demux->index->len - nentries; i < demux->index->len; i++) {
        GstMatroskaIndex *idx =
            &g_array_index (demux->index, GstMatroskaIndex, i);

        idx->time = time;
        GST_DEBUG_OBJECT (demux, "Index entry: pos=%" G_GUINT64_FORMAT
            ", time=%" GST_TIME_FORMAT ", track=%u, block=%u", idx->pos,
            GST_TIME_ARGS (idx->time), (guint) idx->track, (guint) idx->block);
      }
    }
  } else {
    GST_DEBUG_OBJECT (demux, "Empty CuePoint");
  }

  return ret;
}

static gint
gst_matroska_index_compare (GstMatroskaIndex * i1, GstMatroskaIndex * i2)
{
  if (i1->time < i2->time)
    return -1;
  else if (i1->time > i2->time)
    return 1;
  else if (i1->block < i2->block)
    return -1;
  else if (i1->block > i2->block)
    return 1;
  else
    return 0;
}

static GstFlowReturn
gst_matroska_demux_parse_index (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;

  if (demux->index)
    g_array_free (demux->index, TRUE);
  demux->index =
      g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaIndex), 128);

  DEBUG_ELEMENT_START (demux, ebml, "Cues");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Cues", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_POINTENTRY:
        ret = gst_matroska_demux_parse_index_pointentry (demux);
        break;

      default:
        GST_WARNING ("Unknown Cues subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }
  DEBUG_ELEMENT_STOP (demux, ebml, "Cues", ret);

  /* Sort index by time, smallest time first, for easier searching */
  g_array_sort (demux->index, (GCompareFunc) gst_matroska_index_compare);

  /* Now sort the track specific index entries into their own arrays */
  for (i = 0; i < demux->index->len; i++) {
    GstMatroskaIndex *idx = &g_array_index (demux->index, GstMatroskaIndex, i);
    gint track_num;
    GstMatroskaTrackContext *ctx;

    if (demux->element_index) {
      gint writer_id;

      if (idx->track != 0 &&
          (track_num =
              gst_matroska_demux_stream_from_num (demux, idx->track)) != -1) {
        ctx = g_ptr_array_index (demux->src, track_num);

        if (ctx->index_writer_id == -1)
          gst_index_get_writer_id (demux->element_index, GST_OBJECT (ctx->pad),
              &ctx->index_writer_id);
        writer_id = ctx->index_writer_id;
      } else {
        if (demux->element_index_writer_id == -1)
          gst_index_get_writer_id (demux->element_index, GST_OBJECT (demux),
              &demux->element_index_writer_id);
        writer_id = demux->element_index_writer_id;
      }

      GST_LOG_OBJECT (demux, "adding association %" GST_TIME_FORMAT "-> %"
          G_GUINT64_FORMAT " for writer id %d", GST_TIME_ARGS (idx->time),
          idx->pos, writer_id);
      gst_index_add_association (demux->element_index, writer_id,
          GST_ASSOCIATION_FLAG_KEY_UNIT, GST_FORMAT_TIME, idx->time,
          GST_FORMAT_BYTES, idx->pos + demux->ebml_segment_start, NULL);
    }

    if (idx->track == 0)
      continue;

    track_num = gst_matroska_demux_stream_from_num (demux, idx->track);
    if (track_num == -1)
      continue;

    ctx = g_ptr_array_index (demux->src, track_num);

    if (ctx->index_table == NULL)
      ctx->index_table =
          g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaIndex), 128);

    g_array_append_vals (ctx->index_table, idx, 1);
  }

  demux->index_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_info (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;

  DEBUG_ELEMENT_START (demux, ebml, "SegmentInfo");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "SegmentInfo", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_TIMECODESCALE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;


        GST_DEBUG_OBJECT (demux, "TimeCodeScale: %" G_GUINT64_FORMAT, num);
        demux->time_scale = num;
        break;
      }

      case GST_MATROSKA_ID_DURATION:{
        gdouble num;
        GstClockTime dur;

        if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num <= 0.0) {
          GST_WARNING_OBJECT (demux, "Invalid duration %lf", num);
          break;
        }

        GST_DEBUG_OBJECT (demux, "Duration: %lf", num);

        dur = gst_gdouble_to_guint64 (num *
            gst_guint64_to_gdouble (demux->time_scale));
        if (GST_CLOCK_TIME_IS_VALID (dur) && dur <= G_MAXINT64)
          demux->duration = dur;
        break;
      }

      case GST_MATROSKA_ID_WRITINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "WritingApp: %s", GST_STR_NULL (text));
        demux->writing_app = text;
        break;
      }

      case GST_MATROSKA_ID_MUXINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "MuxingApp: %s", GST_STR_NULL (text));
        demux->muxing_app = text;
        break;
      }

      case GST_MATROSKA_ID_DATEUTC:{
        gint64 time;

        if ((ret = gst_ebml_read_date (ebml, &id, &time)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "DateUTC: %" G_GINT64_FORMAT, time);
        demux->created = time;
        break;
      }

      case GST_MATROSKA_ID_TITLE:{
        gchar *text;
        GstTagList *taglist;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "Title: %s", GST_STR_NULL (text));
        taglist = gst_tag_list_new ();
        gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, text,
            NULL);
        gst_matroska_demux_found_global_tag (demux, taglist);
        g_free (text);
        break;
      }

      default:
        GST_WARNING_OBJECT (demux,
            "Unknown SegmentInfo subelement 0x%x - ignoring", id);

        /* fall through */
      case GST_MATROSKA_ID_SEGMENTUID:
      case GST_MATROSKA_ID_SEGMENTFILENAME:
      case GST_MATROSKA_ID_PREVUID:
      case GST_MATROSKA_ID_PREVFILENAME:
      case GST_MATROSKA_ID_NEXTUID:
      case GST_MATROSKA_ID_NEXTFILENAME:
      case GST_MATROSKA_ID_SEGMENTFAMILY:
      case GST_MATROSKA_ID_CHAPTERTRANSLATE:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "SegmentInfo", ret);

  demux->segmentinfo_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_metadata_id_simple_tag (GstMatroskaDemux * demux,
    GstTagList ** p_taglist)
{
  /* FIXME: check if there are more useful mappings */
  struct
  {
    gchar *matroska_tagname;
    gchar *gstreamer_tagname;
  }
  tag_conv[] = {
    {
    GST_MATROSKA_TAG_ID_TITLE, GST_TAG_TITLE}, {
    GST_MATROSKA_TAG_ID_AUTHOR, GST_TAG_ARTIST}, {
    GST_MATROSKA_TAG_ID_ALBUM, GST_TAG_ALBUM}, {
    GST_MATROSKA_TAG_ID_COMMENTS, GST_TAG_COMMENT}, {
    GST_MATROSKA_TAG_ID_BITSPS, GST_TAG_BITRATE}, {
    GST_MATROSKA_TAG_ID_BPS, GST_TAG_BITRATE}, {
    GST_MATROSKA_TAG_ID_ENCODER, GST_TAG_ENCODER}, {
    GST_MATROSKA_TAG_ID_DATE, GST_TAG_DATE}, {
    GST_MATROSKA_TAG_ID_ISRC, GST_TAG_ISRC}, {
    GST_MATROSKA_TAG_ID_COPYRIGHT, GST_TAG_COPYRIGHT}, {
    GST_MATROSKA_TAG_ID_BPM, GST_TAG_BEATS_PER_MINUTE}, {
    GST_MATROSKA_TAG_ID_TERMS_OF_USE, GST_TAG_LICENSE}, {
    GST_MATROSKA_TAG_ID_COMPOSER, GST_TAG_COMPOSER}, {
    GST_MATROSKA_TAG_ID_LEAD_PERFORMER, GST_TAG_PERFORMER}, {
    GST_MATROSKA_TAG_ID_GENRE, GST_TAG_GENRE}
  };
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret;
  guint32 id;
  gchar *value = NULL;
  gchar *tag = NULL;

  DEBUG_ELEMENT_START (demux, ebml, "SimpleTag");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "SimpleTag", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_TAGNAME:
        g_free (tag);
        tag = NULL;
        ret = gst_ebml_read_ascii (ebml, &id, &tag);
        GST_DEBUG_OBJECT (demux, "TagName: %s", GST_STR_NULL (tag));
        break;

      case GST_MATROSKA_ID_TAGSTRING:
        g_free (value);
        value = NULL;
        ret = gst_ebml_read_utf8 (ebml, &id, &value);
        GST_DEBUG_OBJECT (demux, "TagString: %s", GST_STR_NULL (value));
        break;

      default:
        GST_WARNING_OBJECT (demux,
            "Unknown SimpleTag subelement 0x%x - ignoring", id);
        /* fall-through */

      case GST_MATROSKA_ID_TAGLANGUAGE:
      case GST_MATROSKA_ID_TAGDEFAULT:
      case GST_MATROSKA_ID_TAGBINARY:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "SimpleTag", ret);

  if (tag && value) {
    guint i;

    for (i = 0; i < G_N_ELEMENTS (tag_conv); i++) {
      const gchar *tagname_gst = tag_conv[i].gstreamer_tagname;

      const gchar *tagname_mkv = tag_conv[i].matroska_tagname;

      if (strcmp (tagname_mkv, tag) == 0) {
        GValue dest = { 0, };
        GType dest_type = gst_tag_get_type (tagname_gst);

        g_value_init (&dest, dest_type);
        if (gst_value_deserialize (&dest, value)) {
          gst_tag_list_add_values (*p_taglist, GST_TAG_MERGE_APPEND,
              tagname_gst, &dest, NULL);
        } else {
          GST_WARNING_OBJECT (demux, "Can't transform tag '%s' with "
              "value '%s' to target type '%s'", tag, value,
              g_type_name (dest_type));
        }
        g_value_unset (&dest);
        break;
      }
    }
  }

  g_free (tag);
  g_free (value);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_metadata_id_tag (GstMatroskaDemux * demux,
    GstTagList ** p_taglist)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret;

  DEBUG_ELEMENT_START (demux, ebml, "Tag");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Tag", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SIMPLETAG:
        ret =
            gst_matroska_demux_parse_metadata_id_simple_tag (demux, p_taglist);
        break;

      default:
        GST_WARNING_OBJECT (demux, "Unknown Tag subelement 0x%x - ignoring",
            id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "Tag", ret);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_metadata (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstTagList *taglist;
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;
  GList *l;
  GstEbmlLevel *curlevel;

  /* Can't be NULL at this point */
  g_assert (ebml->level != NULL);
  curlevel = ebml->level->data;

  /* Make sure we don't parse a tags element twice and
   * post it's tags twice */
  for (l = demux->tags_parsed; l; l = l->next) {
    GstEbmlLevel *level = l->data;

    if (ebml->level)
      curlevel = ebml->level->data;
    else
      break;

    if (level->start == curlevel->start && level->length == curlevel->length) {
      GST_DEBUG_OBJECT (demux, "Skipping already parsed Tags at offset %"
          G_GUINT64_FORMAT, ebml->offset);
      ret = gst_ebml_read_skip (ebml);
      return ret;
    }
  }

  DEBUG_ELEMENT_START (demux, ebml, "Tags");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Tags", ret);
    return ret;
  }

  taglist = gst_tag_list_new ();

  /* TODO: g_slice_dup() if we depend on GLib 2.14 */
  curlevel = g_slice_new (GstEbmlLevel);
  memcpy (curlevel, ebml->level->data, sizeof (GstEbmlLevel));
  demux->tags_parsed = g_list_prepend (demux->tags_parsed, curlevel);

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_TAG:
        ret = gst_matroska_demux_parse_metadata_id_tag (demux, &taglist);
        break;

      default:
        GST_WARNING_OBJECT (demux, "Unknown Tags subelement 0x%x - ignoring",
            id);
        /* FIXME: Use to limit the tags to specific pads */
      case GST_MATROSKA_ID_TARGETS:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "Tags", ret);

  gst_matroska_demux_found_global_tag (demux, taglist);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_attached_file (GstMatroskaDemux * demux,
    GstTagList * taglist)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret;
  gchar *description = NULL;
  gchar *filename = NULL;
  gchar *mimetype = NULL;
  guint8 *data = NULL;
  guint64 datalen = 0;

  DEBUG_ELEMENT_START (demux, ebml, "AttachedFile");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "AttachedFile", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_FILEDESCRIPTION:
        if (description) {
          GST_WARNING_OBJECT (demux, "FileDescription can only appear once");
          break;
        }

        ret = gst_ebml_read_utf8 (ebml, &id, &description);
        GST_DEBUG_OBJECT (demux, "FileDescription: %s",
            GST_STR_NULL (description));
        break;
      case GST_MATROSKA_ID_FILENAME:
        if (filename) {
          GST_WARNING_OBJECT (demux, "FileName can only appear once");
          break;
        }

        ret = gst_ebml_read_utf8 (ebml, &id, &filename);

        GST_DEBUG_OBJECT (demux, "FileName: %s", GST_STR_NULL (filename));
        break;
      case GST_MATROSKA_ID_FILEMIMETYPE:
        if (mimetype) {
          GST_WARNING_OBJECT (demux, "FileMimeType can only appear once");
          break;
        }

        ret = gst_ebml_read_ascii (ebml, &id, &mimetype);
        GST_DEBUG_OBJECT (demux, "FileMimeType: %s", GST_STR_NULL (mimetype));
        break;
      case GST_MATROSKA_ID_FILEDATA:
        if (data) {
          GST_WARNING_OBJECT (demux, "FileData can only appear once");
          break;
        }

        ret = gst_ebml_read_binary (ebml, &id, &data, &datalen);
        GST_DEBUG_OBJECT (demux, "FileData of size %" G_GUINT64_FORMAT,
            datalen);
        break;

      default:
        GST_WARNING_OBJECT (demux,
            "Unknown AttachedFile subelement 0x%x - ignoring", id);
        /* fall through */
      case GST_MATROSKA_ID_FILEUID:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "AttachedFile", ret);

  if (filename && mimetype && data && datalen > 0) {
    GstTagImageType image_type = GST_TAG_IMAGE_TYPE_NONE;
    GstBuffer *tagbuffer = NULL;
    GstCaps *caps;
    gchar *filename_lc = g_utf8_strdown (filename, -1);

    GST_DEBUG_OBJECT (demux, "Creating tag for attachment with filename '%s', "
        "mimetype '%s', description '%s', size %" G_GUINT64_FORMAT, filename,
        mimetype, GST_STR_NULL (description), datalen);

    /* TODO: better heuristics for different image types */
    if (strstr (filename_lc, "cover")) {
      if (strstr (filename_lc, "back"))
        image_type = GST_TAG_IMAGE_TYPE_BACK_COVER;
      else
        image_type = GST_TAG_IMAGE_TYPE_FRONT_COVER;
    } else if (g_str_has_prefix (mimetype, "image/") ||
        g_str_has_suffix (filename_lc, "png") ||
        g_str_has_suffix (filename_lc, "jpg") ||
        g_str_has_suffix (filename_lc, "jpeg") ||
        g_str_has_suffix (filename_lc, "gif") ||
        g_str_has_suffix (filename_lc, "bmp")) {
      image_type = GST_TAG_IMAGE_TYPE_UNDEFINED;
    }
    g_free (filename_lc);

    /* First try to create an image tag buffer from this */
    if (image_type != GST_TAG_IMAGE_TYPE_NONE) {
      tagbuffer =
          gst_tag_image_data_to_image_buffer (data, datalen, image_type);

      if (!tagbuffer)
        image_type = GST_TAG_IMAGE_TYPE_NONE;
    }

    /* if this failed create an attachment buffer */
    if (!tagbuffer) {
      tagbuffer = gst_buffer_new_and_alloc (datalen);

      memcpy (GST_BUFFER_DATA (tagbuffer), data, datalen);
      GST_BUFFER_SIZE (tagbuffer) = datalen;

      caps = gst_type_find_helper_for_buffer (NULL, tagbuffer, NULL);
      if (caps == NULL)
        caps = gst_caps_new_simple (mimetype, NULL);
      gst_buffer_set_caps (tagbuffer, caps);
      gst_caps_unref (caps);
    }

    /* Set filename and description on the caps */
    caps = GST_BUFFER_CAPS (tagbuffer);
    gst_caps_set_simple (caps, "filename", G_TYPE_STRING, filename, NULL);
    if (description)
      gst_caps_set_simple (caps, "description", G_TYPE_STRING, description,
          NULL);

    GST_DEBUG_OBJECT (demux,
        "Created attachment buffer with caps: %" GST_PTR_FORMAT, caps);

    /* and append to the tag list */
    if (image_type != GST_TAG_IMAGE_TYPE_NONE)
      gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_IMAGE, tagbuffer,
          NULL);
    else
      gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_ATTACHMENT,
          tagbuffer, NULL);
  }

  g_free (filename);
  g_free (mimetype);
  g_free (data);
  g_free (description);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_attachments (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;
  GstTagList *taglist;

  DEBUG_ELEMENT_START (demux, ebml, "Attachments");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Attachments", ret);
    return ret;
  }

  taglist = gst_tag_list_new ();

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_ATTACHEDFILE:
        ret = gst_matroska_demux_parse_attached_file (demux, taglist);
        break;

      default:
        GST_WARNING ("Unknown Attachments subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }
  DEBUG_ELEMENT_STOP (demux, ebml, "Attachments", ret);

  if (gst_structure_n_fields (GST_STRUCTURE (taglist)) > 0) {
    GST_DEBUG_OBJECT (demux, "Storing attachment tags");
    gst_matroska_demux_found_global_tag (demux, taglist);
  } else {
    GST_DEBUG_OBJECT (demux, "No valid attachments found");
    gst_tag_list_free (taglist);
  }

  demux->attachments_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_chapters (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_WARNING_OBJECT (demux, "Parsing of chapters not implemented yet");

  /* TODO: implement parsing of chapters */

  DEBUG_ELEMENT_START (demux, ebml, "Chapters");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Chapters", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      default:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "Chapters", ret);
  return ret;
}

/*
 * Read signed/unsigned "EBML" numbers.
 * Return: number of bytes processed.
 */

static gint
gst_matroska_ebmlnum_uint (guint8 * data, guint size, guint64 * num)
{
  gint len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
  guint64 total;

  if (size <= 0) {
    return -1;
  }

  total = data[0];
  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 8)
    return -1;

  if ((total &= (len_mask - 1)) == len_mask - 1)
    num_ffs++;
  if (size < read)
    return -1;
  while (n < read) {
    if (data[n] == 0xff)
      num_ffs++;
    total = (total << 8) | data[n];
    n++;
  }

  if (read == num_ffs && total != 0)
    *num = G_MAXUINT64;
  else
    *num = total;

  return read;
}

static gint
gst_matroska_ebmlnum_sint (guint8 * data, guint size, gint64 * num)
{
  guint64 unum;
  gint res;

  /* read as unsigned number first */
  if ((res = gst_matroska_ebmlnum_uint (data, size, &unum)) < 0)
    return -1;

  /* make signed */
  if (unum == G_MAXUINT64)
    *num = G_MAXINT64;
  else
    *num = unum - ((1 << ((7 * res) - 1)) - 1);

  return res;
}

/*
 * Mostly used for subtitles. We add void filler data for each
 * lagging stream to make sure we don't deadlock.
 */

static void
gst_matroska_demux_sync_streams (GstMatroskaDemux * demux)
{
  gint stream_nr;

  GST_LOG_OBJECT (demux, "Sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->segment.last_stop));

  g_assert (demux->num_streams == demux->src->len);
  for (stream_nr = 0; stream_nr < demux->src->len; stream_nr++) {
    GstMatroskaTrackContext *context;

    context = g_ptr_array_index (demux->src, stream_nr);

    GST_LOG_OBJECT (demux,
        "Checking for resync on stream %d (%" GST_TIME_FORMAT ")", stream_nr,
        GST_TIME_ARGS (context->pos));

    /* does it lag? 0.5 seconds is a random threshold...
     * lag need only be considered if we have advanced into requested segment */
    if (GST_CLOCK_TIME_IS_VALID (context->pos) &&
        GST_CLOCK_TIME_IS_VALID (demux->segment.last_stop) &&
        demux->segment.last_stop > demux->segment.start &&
        context->pos + (GST_SECOND / 2) < demux->segment.last_stop) {
      gint64 new_start;

      new_start = demux->segment.last_stop - (GST_SECOND / 2);
      GST_DEBUG_OBJECT (demux,
          "Synchronizing stream %d with others by advancing time " "from %"
          GST_TIME_FORMAT " to %" GST_TIME_FORMAT, stream_nr,
          GST_TIME_ARGS (context->pos), GST_TIME_ARGS (new_start));

      context->pos = new_start;

      /* advance stream time */
      gst_pad_push_event (context->pad,
          gst_event_new_new_segment (TRUE, demux->segment.rate,
              demux->segment.format, new_start,
              demux->segment.stop, new_start));
    }
  }
}

static GstFlowReturn
gst_matroska_demux_push_hdr_buf (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream, guint8 * data, guint len)
{
  GstFlowReturn ret, cret;
  GstBuffer *header_buf = NULL;

  ret = gst_pad_alloc_buffer_and_set_caps (stream->pad,
      GST_BUFFER_OFFSET_NONE, len, stream->caps, &header_buf);

  /* we combine but don't use the combined value to check if we have a buffer
   * or not. The combined value is what we return. */
  cret = gst_matroska_demux_combine_flows (demux, stream, ret);
  if (ret != GST_FLOW_OK)
    goto no_buffer;

  memcpy (GST_BUFFER_DATA (header_buf), data, len);

  if (stream->set_discont) {
    GST_BUFFER_FLAG_SET (header_buf, GST_BUFFER_FLAG_DISCONT);
    stream->set_discont = FALSE;
  }

  ret = gst_pad_push (stream->pad, header_buf);

  /* combine flows */
  cret = gst_matroska_demux_combine_flows (demux, stream, ret);

  return cret;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (demux, "could not alloc buffer: %s, combined %s",
        gst_flow_get_name (ret), gst_flow_get_name (cret));
    return cret;
  }
}

static GstFlowReturn
gst_matroska_demux_push_flac_codec_priv_data (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  GstFlowReturn ret;
  guint8 *pdata;
  guint off, len;

  GST_LOG_OBJECT (demux, "priv data size = %u", stream->codec_priv_size);

  pdata = (guint8 *) stream->codec_priv;

  /* need at least 'fLaC' marker + STREAMINFO metadata block */
  if (stream->codec_priv_size < ((4) + (4 + 34))) {
    GST_WARNING_OBJECT (demux, "not enough codec priv data for flac headers");
    return GST_FLOW_ERROR;
  }

  if (memcmp (pdata, "fLaC", 4) != 0) {
    GST_WARNING_OBJECT (demux, "no flac marker at start of stream headers");
    return GST_FLOW_ERROR;
  }

  ret = gst_matroska_demux_push_hdr_buf (demux, stream, pdata, 4);
  if (ret != GST_FLOW_OK)
    return ret;

  off = 4;                      /* skip fLaC marker */
  while (off < stream->codec_priv_size) {
    len = GST_READ_UINT8 (pdata + off + 1) << 16;
    len |= GST_READ_UINT8 (pdata + off + 2) << 8;
    len |= GST_READ_UINT8 (pdata + off + 3);

    GST_DEBUG_OBJECT (demux, "header packet: len=%u bytes, flags=0x%02x",
        len, (guint) pdata[off]);

    ret = gst_matroska_demux_push_hdr_buf (demux, stream, pdata + off, len);
    if (ret != GST_FLOW_OK)
      return ret;

    off += 4 + len;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_matroska_demux_push_speex_codec_priv_data (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  GstFlowReturn ret;
  guint8 *pdata;

  GST_LOG_OBJECT (demux, "priv data size = %u", stream->codec_priv_size);

  pdata = (guint8 *) stream->codec_priv;

  /* need at least 'fLaC' marker + STREAMINFO metadata block */
  if (stream->codec_priv_size < 80) {
    GST_WARNING_OBJECT (demux, "not enough codec priv data for speex headers");
    return GST_FLOW_ERROR;
  }

  if (memcmp (pdata, "Speex   ", 8) != 0) {
    GST_WARNING_OBJECT (demux, "no Speex marker at start of stream headers");
    return GST_FLOW_ERROR;
  }

  ret = gst_matroska_demux_push_hdr_buf (demux, stream, pdata, 80);
  if (ret != GST_FLOW_OK)
    return ret;

  if (stream->codec_priv_size == 80)
    return ret;
  else
    return gst_matroska_demux_push_hdr_buf (demux, stream, pdata + 80,
        stream->codec_priv_size - 80);
}

static GstFlowReturn
gst_matroska_demux_push_xiph_codec_priv_data (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  GstFlowReturn ret;
  guint8 *p = (guint8 *) stream->codec_priv;
  gint i, offset, num_packets;
  guint *length, last;

  /* start of the stream and vorbis audio or theora video, need to
   * send the codec_priv data as first three packets */
  num_packets = p[0] + 1;
  GST_DEBUG_OBJECT (demux, "%u stream headers, total length=%u bytes",
      (guint) num_packets, stream->codec_priv_size);

  length = g_alloca (num_packets * sizeof (guint));
  last = 0;
  offset = 1;

  /* first packets, read length values */
  for (i = 0; i < num_packets - 1; i++) {
    length[i] = 0;
    while (offset < stream->codec_priv_size) {
      length[i] += p[offset];
      if (p[offset++] != 0xff)
        break;
    }
    last += length[i];
  }
  if (offset + last > stream->codec_priv_size)
    return GST_FLOW_ERROR;

  /* last packet is the remaining size */
  length[i] = stream->codec_priv_size - offset - last;

  for (i = 0; i < num_packets; i++) {
    GST_DEBUG_OBJECT (demux, "buffer %d: length=%u bytes", i,
        (guint) length[i]);
    if (offset + length[i] > stream->codec_priv_size)
      return GST_FLOW_ERROR;

    ret =
        gst_matroska_demux_push_hdr_buf (demux, stream, p + offset, length[i]);
    if (ret != GST_FLOW_OK)
      return ret;

    offset += length[i];
  }
  return GST_FLOW_OK;
}

static void
gst_matroska_demux_push_dvd_clut_change_event (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  gchar *buf, *start;

  g_assert (!strcmp (stream->codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_VOBSUB));

  if (!stream->codec_priv)
    return;

  /* ideally, VobSub private data should be parsed and stored more convenient
   * elsewhere, but for now, only interested in a small part */

  /* make sure we have terminating 0 */
  buf = g_strndup ((gchar *) stream->codec_priv, stream->codec_priv_size);

  /* just locate and parse palette part */
  start = strstr (buf, "palette:");
  if (start) {
    gint i;
    guint32 clut[16];
    guint32 col;
    guint8 r, g, b, y, u, v;

    start += 8;
    while (g_ascii_isspace (*start))
      start++;
    for (i = 0; i < 16; i++) {
      if (sscanf (start, "%06x", &col) != 1)
        break;
      start += 6;
      while ((*start == ',') || g_ascii_isspace (*start))
        start++;
      /* sigh, need to convert this from vobsub pseudo-RGB to YUV */
      r = (col >> 16) & 0xff;
      g = (col >> 8) & 0xff;
      b = col & 0xff;
      y = CLAMP ((0.1494 * r + 0.6061 * g + 0.2445 * b) * 219 / 255 + 16, 0,
          255);
      u = CLAMP (0.6066 * r - 0.4322 * g - 0.1744 * b + 128, 0, 255);
      v = CLAMP (-0.08435 * r - 0.3422 * g + 0.4266 * b + 128, 0, 255);
      clut[i] = (y << 16) | (u << 8) | v;
    }

    /* got them all without problems; build and send event */
    if (i == 16) {
      GstStructure *s;

      s = gst_structure_new ("application/x-gst-dvd", "event", G_TYPE_STRING,
          "dvd-spu-clut-change", "clut00", G_TYPE_INT, clut[0], "clut01",
          G_TYPE_INT, clut[1], "clut02", G_TYPE_INT, clut[2], "clut03",
          G_TYPE_INT, clut[3], "clut04", G_TYPE_INT, clut[4], "clut05",
          G_TYPE_INT, clut[5], "clut06", G_TYPE_INT, clut[6], "clut07",
          G_TYPE_INT, clut[7], "clut08", G_TYPE_INT, clut[8], "clut09",
          G_TYPE_INT, clut[9], "clut10", G_TYPE_INT, clut[10], "clut11",
          G_TYPE_INT, clut[11], "clut12", G_TYPE_INT, clut[12], "clut13",
          G_TYPE_INT, clut[13], "clut14", G_TYPE_INT, clut[14], "clut15",
          G_TYPE_INT, clut[15], NULL);

      gst_pad_push_event (stream->pad,
          gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s));
    }
  }
  g_free (buf);
}

static GstFlowReturn
gst_matroska_demux_add_mpeg_seq_header (GstElement * element,
    GstMatroskaTrackContext * stream, GstBuffer ** buf)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  guint8 *seq_header;
  guint seq_header_len;
  guint32 header;

  if (stream->codec_state) {
    seq_header = stream->codec_state;
    seq_header_len = stream->codec_state_size;
  } else if (stream->codec_priv) {
    seq_header = stream->codec_priv;
    seq_header_len = stream->codec_priv_size;
  } else {
    return GST_FLOW_OK;
  }

  /* Sequence header only needed for keyframes */
  if (GST_BUFFER_FLAG_IS_SET (*buf, GST_BUFFER_FLAG_DELTA_UNIT))
    return GST_FLOW_OK;

  if (GST_BUFFER_SIZE (*buf) < 4)
    return GST_FLOW_OK;

  header = GST_READ_UINT32_BE (GST_BUFFER_DATA (*buf));
  /* Sequence start code, if not found prepend */
  if (header != 0x000001b3) {
    GstBuffer *newbuf;
    GstFlowReturn ret, cret;

    ret = gst_pad_alloc_buffer_and_set_caps (stream->pad,
        GST_BUFFER_OFFSET_NONE, GST_BUFFER_SIZE (*buf) + seq_header_len,
        stream->caps, &newbuf);
    cret = gst_matroska_demux_combine_flows (demux, stream, ret);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (demux, "Reallocating buffer for sequence header "
          "failed: %s, combined flow return: %s", gst_flow_get_name (ret),
          gst_flow_get_name (cret));
      return cret;
    }

    GST_DEBUG_OBJECT (demux, "Prepending MPEG sequence header");
    gst_buffer_copy_metadata (newbuf, *buf, GST_BUFFER_COPY_TIMESTAMPS |
        GST_BUFFER_COPY_FLAGS);
    g_memmove (GST_BUFFER_DATA (newbuf), seq_header, seq_header_len);
    g_memmove (GST_BUFFER_DATA (newbuf) + seq_header_len,
        GST_BUFFER_DATA (*buf), GST_BUFFER_SIZE (*buf));
    gst_buffer_unref (*buf);
    *buf = newbuf;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_matroska_demux_add_wvpk_header (GstElement * element,
    GstMatroskaTrackContext * stream, GstBuffer ** buf)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  GstMatroskaTrackAudioContext *audiocontext =
      (GstMatroskaTrackAudioContext *) stream;
  GstBuffer *newbuf = NULL;
  guint8 *data;
  guint newlen;
  GstFlowReturn ret, cret = GST_FLOW_OK;
  Wavpack4Header wvh;

  wvh.ck_id[0] = 'w';
  wvh.ck_id[1] = 'v';
  wvh.ck_id[2] = 'p';
  wvh.ck_id[3] = 'k';

  wvh.version = GST_READ_UINT16_LE (stream->codec_priv);
  wvh.track_no = 0;
  wvh.index_no = 0;
  wvh.total_samples = -1;
  wvh.block_index = audiocontext->wvpk_block_index;

  if (audiocontext->channels <= 2) {
    guint32 block_samples;

    block_samples = GST_READ_UINT32_LE (GST_BUFFER_DATA (*buf));
    /* we need to reconstruct the header of the wavpack block */

    /* -20 because ck_size is the size of the wavpack block -8
     * and lace_size is the size of the wavpack block + 12
     * (the three guint32 of the header that already are in the buffer) */
    wvh.ck_size = GST_BUFFER_SIZE (*buf) + sizeof (Wavpack4Header) - 20;

    /* block_samples, flags and crc are already in the buffer */
    newlen = GST_BUFFER_SIZE (*buf) + sizeof (Wavpack4Header) - 12;
    ret =
        gst_pad_alloc_buffer_and_set_caps (stream->pad, GST_BUFFER_OFFSET_NONE,
        newlen, stream->caps, &newbuf);
    cret = gst_matroska_demux_combine_flows (demux, stream, ret);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (demux, "pad_alloc failed %s, combined %s",
          gst_flow_get_name (ret), gst_flow_get_name (cret));
      return cret;
    }

    data = GST_BUFFER_DATA (newbuf);
    data[0] = 'w';
    data[1] = 'v';
    data[2] = 'p';
    data[3] = 'k';
    GST_WRITE_UINT32_LE (data + 4, wvh.ck_size);
    GST_WRITE_UINT16_LE (data + 8, wvh.version);
    GST_WRITE_UINT8 (data + 10, wvh.track_no);
    GST_WRITE_UINT8 (data + 11, wvh.index_no);
    GST_WRITE_UINT32_LE (data + 12, wvh.total_samples);
    GST_WRITE_UINT32_LE (data + 16, wvh.block_index);
    g_memmove (data + 20, GST_BUFFER_DATA (*buf), GST_BUFFER_SIZE (*buf));
    gst_buffer_copy_metadata (newbuf, *buf,
        GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);
    gst_buffer_unref (*buf);
    *buf = newbuf;
    audiocontext->wvpk_block_index += block_samples;
  } else {
    guint8 *outdata;
    guint outpos = 0;
    guint size;
    guint32 block_samples, flags, crc, blocksize;

    data = GST_BUFFER_DATA (*buf);
    size = GST_BUFFER_SIZE (*buf);

    if (size < 4) {
      GST_ERROR_OBJECT (demux, "Too small wavpack buffer");
      return GST_FLOW_ERROR;
    }

    block_samples = GST_READ_UINT32_LE (data);
    data += 4;
    size -= 4;

    while (size > 12) {
      flags = GST_READ_UINT32_LE (data);
      data += 4;
      size -= 4;
      crc = GST_READ_UINT32_LE (data);
      data += 4;
      size -= 4;
      blocksize = GST_READ_UINT32_LE (data);
      data += 4;
      size -= 4;

      if (blocksize == 0 || size < blocksize)
        break;

      if (newbuf == NULL) {
        newbuf = gst_buffer_new_and_alloc (sizeof (Wavpack4Header) + blocksize);
        gst_buffer_set_caps (newbuf, stream->caps);

        gst_buffer_copy_metadata (newbuf, *buf,
            GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);

        outpos = 0;
        outdata = GST_BUFFER_DATA (newbuf);
      } else {
        GST_BUFFER_SIZE (newbuf) += sizeof (Wavpack4Header) + blocksize;
        GST_BUFFER_DATA (newbuf) =
            g_realloc (GST_BUFFER_DATA (newbuf), GST_BUFFER_SIZE (newbuf));
        GST_BUFFER_MALLOCDATA (newbuf) = GST_BUFFER_DATA (newbuf);
        outdata = GST_BUFFER_DATA (newbuf);
      }

      outdata[outpos] = 'w';
      outdata[outpos + 1] = 'v';
      outdata[outpos + 2] = 'p';
      outdata[outpos + 3] = 'k';
      outpos += 4;

      GST_WRITE_UINT32_LE (outdata + outpos,
          blocksize + sizeof (Wavpack4Header) - 8);
      GST_WRITE_UINT16_LE (outdata + outpos + 4, wvh.version);
      GST_WRITE_UINT8 (outdata + outpos + 6, wvh.track_no);
      GST_WRITE_UINT8 (outdata + outpos + 7, wvh.index_no);
      GST_WRITE_UINT32_LE (outdata + outpos + 8, wvh.total_samples);
      GST_WRITE_UINT32_LE (outdata + outpos + 12, wvh.block_index);
      GST_WRITE_UINT32_LE (outdata + outpos + 16, block_samples);
      GST_WRITE_UINT32_LE (outdata + outpos + 20, flags);
      GST_WRITE_UINT32_LE (outdata + outpos + 24, crc);
      outpos += 28;

      g_memmove (outdata + outpos, data, blocksize);
      outpos += blocksize;
      data += blocksize;
      size -= blocksize;
    }
    gst_buffer_unref (*buf);
    *buf = newbuf;
    audiocontext->wvpk_block_index += block_samples;
  }

  return cret;
}

static GstFlowReturn
gst_matroska_demux_check_subtitle_buffer (GstElement * element,
    GstMatroskaTrackContext * stream, GstBuffer ** buf)
{
  GstMatroskaTrackSubtitleContext *sub_stream;
  const gchar *encoding, *data;
  GError *err = NULL;
  GstBuffer *newbuf;
  gchar *utf8;
  guint size;

  sub_stream = (GstMatroskaTrackSubtitleContext *) stream;

  data = (const gchar *) GST_BUFFER_DATA (*buf);
  size = GST_BUFFER_SIZE (*buf);

  if (!sub_stream->invalid_utf8) {
    if (g_utf8_validate (data, size, NULL)) {
      return GST_FLOW_OK;
    }
    GST_WARNING_OBJECT (element, "subtitle stream %d is not valid UTF-8, this "
        "is broken according to the matroska specification", stream->num);
    sub_stream->invalid_utf8 = TRUE;
  }

  /* file with broken non-UTF8 subtitle, do the best we can do to fix it */
  encoding = g_getenv ("GST_SUBTITLE_ENCODING");
  if (encoding == NULL || *encoding == '\0') {
    /* if local encoding is UTF-8 and no encoding specified
     * via the environment variable, assume ISO-8859-15 */
    if (g_get_charset (&encoding)) {
      encoding = "ISO-8859-15";
    }
  }

  utf8 = g_convert_with_fallback (data, size, "UTF-8", encoding, "*",
      NULL, NULL, &err);

  if (err) {
    GST_LOG_OBJECT (element, "could not convert string from '%s' to UTF-8: %s",
        encoding, err->message);
    g_error_free (err);
    g_free (utf8);

    /* invalid input encoding, fall back to ISO-8859-15 (always succeeds) */
    encoding = "ISO-8859-15";
    utf8 = g_convert_with_fallback (data, size, "UTF-8", encoding, "*",
        NULL, NULL, NULL);
  }

  GST_LOG_OBJECT (element, "converted subtitle text from %s to UTF-8 %s",
      encoding, (err) ? "(using ISO-8859-15 as fallback)" : "");

  if (utf8 == NULL)
    utf8 = g_strdup ("invalid subtitle");

  newbuf = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (newbuf) = (guint8 *) utf8;
  GST_BUFFER_DATA (newbuf) = (guint8 *) utf8;
  GST_BUFFER_SIZE (newbuf) = strlen (utf8);
  gst_buffer_copy_metadata (newbuf, *buf,
      GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);
  gst_buffer_unref (*buf);

  *buf = newbuf;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_matroska_demux_parse_blockgroup_or_simpleblock (GstMatroskaDemux * demux,
    guint64 cluster_time, guint64 cluster_offset, gboolean is_simpleblock)
{
  GstMatroskaTrackContext *stream = NULL;
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean readblock = FALSE;
  guint32 id;
  guint64 block_duration = 0;
  GstBuffer *buf = NULL;
  gint stream_num = -1, n, laces = 0;
  guint size = 0;
  gint *lace_size = NULL;
  gint64 time = 0;
  gint flags = 0;
  gint64 referenceblock = 0;

  while (ret == GST_FLOW_OK) {
    if (!is_simpleblock) {
      if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
        break;

      if (demux->level_up) {
        demux->level_up--;
        break;
      }
    } else {
      id = GST_MATROSKA_ID_SIMPLEBLOCK;
    }

    switch (id) {
        /* one block inside the group. Note, block parsing is one
         * of the harder things, so this code is a bit complicated.
         * See http://www.matroska.org/ for documentation. */
      case GST_MATROSKA_ID_SIMPLEBLOCK:
      case GST_MATROSKA_ID_BLOCK:
      {
        guint64 num;
        guint8 *data;

        if (buf) {
          gst_buffer_unref (buf);
          buf = NULL;
        }
        if ((ret = gst_ebml_read_buffer (ebml, &id, &buf)) != GST_FLOW_OK)
          break;

        data = GST_BUFFER_DATA (buf);
        size = GST_BUFFER_SIZE (buf);

        /* first byte(s): blocknum */
        if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0) {
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL), ("Data error"));
          gst_buffer_unref (buf);
          buf = NULL;
          ret = GST_FLOW_ERROR;
          break;
        }
        data += n;
        size -= n;

        /* fetch stream from num */
        stream_num = gst_matroska_demux_stream_from_num (demux, num);
        if (size < 3 || stream_num < 0 || stream_num >= demux->num_streams) {
          gst_buffer_unref (buf);
          buf = NULL;
          GST_WARNING_OBJECT (demux, "Invalid stream %d or size %u", stream_num,
              size);
          ret = GST_FLOW_ERROR;
          break;
        }

        stream = g_ptr_array_index (demux->src, stream_num);

        /* time (relative to cluster time) */
        time = ((gint16) GST_READ_UINT16_BE (data));
        data += 2;
        size -= 2;
        flags = GST_READ_UINT8 (data);
        data += 1;
        size -= 1;

        GST_LOG_OBJECT (demux, "time %" G_GUINT64_FORMAT ", flags %d", time,
            flags);

        switch ((flags & 0x06) >> 1) {
          case 0x0:            /* no lacing */
            laces = 1;
            lace_size = g_new (gint, 1);
            lace_size[0] = size;
            break;

          case 0x1:            /* xiph lacing */
          case 0x2:            /* fixed-size lacing */
          case 0x3:            /* EBML lacing */
            if (size == 0) {
              GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                  ("Invalid lacing size"));
              ret = GST_FLOW_ERROR;
              break;
            }
            laces = GST_READ_UINT8 (data) + 1;
            data += 1;
            size -= 1;
            lace_size = g_new0 (gint, laces);

            switch ((flags & 0x06) >> 1) {
              case 0x1:        /* xiph lacing */  {
                guint temp, total = 0;

                for (n = 0; ret == GST_FLOW_OK && n < laces - 1; n++) {
                  while (1) {
                    if (size == 0) {
                      GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                          ("Invalid lacing size"));
                      ret = GST_FLOW_ERROR;
                      break;
                    }
                    temp = GST_READ_UINT8 (data);
                    lace_size[n] += temp;
                    data += 1;
                    size -= 1;
                    if (temp != 0xff)
                      break;
                  }
                  total += lace_size[n];
                }
                lace_size[n] = size - total;
                break;
              }

              case 0x2:        /* fixed-size lacing */
                for (n = 0; n < laces; n++)
                  lace_size[n] = size / laces;
                break;

              case 0x3:        /* EBML lacing */  {
                guint total;

                if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0) {
                  GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                      ("Data error"));
                  ret = GST_FLOW_ERROR;
                  break;
                }
                data += n;
                size -= n;
                total = lace_size[0] = num;
                for (n = 1; ret == GST_FLOW_OK && n < laces - 1; n++) {
                  gint64 snum;
                  gint r;

                  if ((r = gst_matroska_ebmlnum_sint (data, size, &snum)) < 0) {
                    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                        ("Data error"));
                    ret = GST_FLOW_ERROR;
                    break;
                  }
                  data += r;
                  size -= r;
                  lace_size[n] = lace_size[n - 1] + snum;
                  total += lace_size[n];
                }
                if (n < laces)
                  lace_size[n] = size - total;
                break;
              }
            }
            break;
        }

        if (stream->send_xiph_headers) {
          ret = gst_matroska_demux_push_xiph_codec_priv_data (demux, stream);
          stream->send_xiph_headers = FALSE;
        }

        if (stream->send_flac_headers) {
          ret = gst_matroska_demux_push_flac_codec_priv_data (demux, stream);
          stream->send_flac_headers = FALSE;
        }

        if (stream->send_speex_headers) {
          ret = gst_matroska_demux_push_speex_codec_priv_data (demux, stream);
          stream->send_speex_headers = FALSE;
        }

        if (stream->send_dvd_event) {
          gst_matroska_demux_push_dvd_clut_change_event (demux, stream);
          /* FIXME: should we send this event again after (flushing) seek ? */
          stream->send_dvd_event = FALSE;
        }

        if (ret != GST_FLOW_OK)
          break;

        readblock = TRUE;
        break;
      }

      case GST_MATROSKA_ID_BLOCKDURATION:{
        ret = gst_ebml_read_uint (ebml, &id, &block_duration);
        GST_DEBUG_OBJECT (demux, "BlockDuration: %" G_GUINT64_FORMAT,
            block_duration);
        break;
      }

      case GST_MATROSKA_ID_REFERENCEBLOCK:{
        ret = gst_ebml_read_sint (ebml, &id, &referenceblock);
        GST_DEBUG_OBJECT (demux, "ReferenceBlock: %" G_GINT64_FORMAT,
            referenceblock);
        break;
      }

      case GST_MATROSKA_ID_CODECSTATE:{
        guint8 *data;
        guint64 data_len = 0;

        if ((ret =
                gst_ebml_read_binary (ebml, &id, &data,
                    &data_len)) != GST_FLOW_OK)
          break;

        g_free (stream->codec_state);
        stream->codec_state = data;
        stream->codec_state_size = data_len;

        /* Decode if necessary */
        if (stream->encodings && stream->encodings->len > 0
            && stream->codec_state && stream->codec_state_size > 0) {
          if (!gst_matroska_decode_data (stream->encodings,
                  &stream->codec_state, &stream->codec_state_size,
                  GST_MATROSKA_TRACK_ENCODING_SCOPE_CODEC_DATA, TRUE)) {
            GST_WARNING_OBJECT (demux, "Decoding codec state failed");
          }
        }

        GST_DEBUG_OBJECT (demux, "CodecState of %u bytes",
            stream->codec_state_size);
        break;
      }

      default:
        GST_WARNING_OBJECT (demux,
            "Unknown BlockGroup subelement 0x%x - ignoring", id);
        /* fall-through */

      case GST_MATROSKA_ID_BLOCKVIRTUAL:
      case GST_MATROSKA_ID_BLOCKADDITIONS:
      case GST_MATROSKA_ID_REFERENCEPRIORITY:
      case GST_MATROSKA_ID_REFERENCEVIRTUAL:
      case GST_MATROSKA_ID_SLICES:
        GST_DEBUG_OBJECT (demux,
            "Skipping BlockGroup subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (is_simpleblock)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (ret == GST_FLOW_OK && readblock) {
    guint64 duration = 0;
    gint64 lace_time = 0;

    stream = g_ptr_array_index (demux->src, stream_num);

    if (cluster_time != GST_CLOCK_TIME_NONE) {
      /* FIXME: What to do with negative timestamps? Give timestamp 0 or -1?
       * Drop unless the lace contains timestamp 0? */
      if (time < 0 && (-time) > cluster_time) {
        lace_time = 0;
      } else {
        if (stream->timecodescale == 1.0)
          lace_time = (cluster_time + time) * demux->time_scale;
        else
          lace_time =
              gst_util_guint64_to_gdouble ((cluster_time + time) *
              demux->time_scale) * stream->timecodescale;
      }
    } else {
      lace_time = GST_CLOCK_TIME_NONE;
    }

    if (block_duration) {
      if (stream->timecodescale == 1.0)
        duration = block_duration * demux->time_scale;
      else
        duration =
            gst_util_gdouble_to_guint64 (gst_util_guint64_to_gdouble
            (block_duration * demux->time_scale) * stream->timecodescale);
    } else if (stream->default_duration) {
      duration = stream->default_duration * laces;
    }
    /* else duration is diff between timecode of this and next block */
    for (n = 0; n < laces; n++) {
      GstBuffer *sub;

      sub = gst_buffer_create_sub (buf,
          GST_BUFFER_SIZE (buf) - size, lace_size[n]);
      GST_DEBUG_OBJECT (demux, "created subbuffer %p", sub);

      if (stream->encodings != NULL && stream->encodings->len > 0)
        sub = gst_matroska_decode_buffer (stream, sub);

      if (sub == NULL) {
        GST_WARNING_OBJECT (demux, "Decoding buffer failed");
        goto next_lace;
      }

      GST_BUFFER_TIMESTAMP (sub) = lace_time;

      if (GST_CLOCK_TIME_IS_VALID (lace_time)) {
        GstClockTime last_stop_end;

        /* Check if this stream is after segment stop */
        if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop) &&
            lace_time >= demux->segment.stop) {
          GST_DEBUG_OBJECT (demux,
              "Stream %d after segment stop %" GST_TIME_FORMAT, stream->index,
              GST_TIME_ARGS (demux->segment.stop));
          ret = GST_FLOW_UNEXPECTED;
          /* combine flows */
          ret = gst_matroska_demux_combine_flows (demux, stream, ret);
          goto done;
        }

        /* handle gaps, e.g. non-zero start-time, or an cue index entry
         * that landed us with timestamps not quite intended */
        if (GST_CLOCK_TIME_IS_VALID (demux->segment.last_stop)) {
          GstClockTimeDiff diff;

          /* only send newsegments with increasing start times,
           * otherwise if these go back and forth downstream (sinks) increase
           * accumulated time and running_time */
          diff = GST_CLOCK_DIFF (demux->segment.last_stop, lace_time);
          if (diff > 2 * GST_SECOND && lace_time > demux->segment.start &&
              (!GST_CLOCK_TIME_IS_VALID (demux->segment.stop) ||
                  lace_time < demux->segment.stop)) {
            GST_DEBUG_OBJECT (demux,
                "Gap of %" G_GINT64_FORMAT " ns detected in"
                "stream %d (%" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT "). "
                "Sending updated NEWSEGMENT events", diff,
                stream->index, GST_TIME_ARGS (stream->pos),
                GST_TIME_ARGS (lace_time));
            /* send newsegment events such that the gap is not accounted in
             * accum time, hence running_time */
            /* close ahead of gap */
            gst_matroska_demux_send_event (demux,
                gst_event_new_new_segment (TRUE, demux->segment.rate,
                    demux->segment.format, demux->segment.last_stop,
                    demux->segment.last_stop, demux->segment.last_stop));
            /* skip gap */
            gst_matroska_demux_send_event (demux,
                gst_event_new_new_segment (FALSE, demux->segment.rate,
                    demux->segment.format, lace_time, demux->segment.stop,
                    lace_time));
            /* align segment view with downstream,
             * prevents double-counting accum when closing segment */
            gst_segment_set_newsegment (&demux->segment, FALSE,
                demux->segment.rate, demux->segment.format, lace_time,
                demux->segment.stop, lace_time);
            demux->segment.last_stop = lace_time;
          }
        }

        if (!GST_CLOCK_TIME_IS_VALID (demux->segment.last_stop)
            || demux->segment.last_stop < lace_time) {
          demux->segment.last_stop = lace_time;
        }

        last_stop_end = lace_time;
        if (duration) {
          GST_BUFFER_DURATION (sub) = duration / laces;
          last_stop_end += GST_BUFFER_DURATION (sub);
        }

        if (!GST_CLOCK_TIME_IS_VALID (demux->last_stop_end) ||
            demux->last_stop_end < last_stop_end)
          demux->last_stop_end = last_stop_end;

        if (demux->duration == -1 || demux->duration < lace_time) {
          demux->duration = last_stop_end;
          gst_element_post_message (GST_ELEMENT_CAST (demux),
              gst_message_new_duration (GST_OBJECT_CAST (demux),
                  GST_FORMAT_TIME, GST_CLOCK_TIME_NONE));
        }
      }

      stream->pos = lace_time;

      gst_matroska_demux_sync_streams (demux);

      if (is_simpleblock) {
        if (flags & 0x80)
          GST_BUFFER_FLAG_UNSET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
        else
          GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
      } else {
        if (referenceblock) {
          GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
        } else {
          GST_BUFFER_FLAG_UNSET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
        }
      }

      if (GST_BUFFER_FLAG_IS_SET (sub, GST_BUFFER_FLAG_DELTA_UNIT)
          && stream->set_discont) {
        /* When doing seeks or such, we need to restart on key frames or
         * decoders might choke. */
        GST_DEBUG_OBJECT (demux, "skipping delta unit");
        gst_buffer_unref (sub);
        goto done;
      }

      if (stream->set_discont) {
        GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DISCONT);
        stream->set_discont = FALSE;
      }

      GST_DEBUG_OBJECT (demux,
          "Pushing lace %d, data of size %d for stream %d, time=%"
          GST_TIME_FORMAT " and duration=%" GST_TIME_FORMAT, n,
          GST_BUFFER_SIZE (sub), stream_num,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (sub)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (sub)));

      if (demux->element_index) {
        if (stream->index_writer_id == -1)
          gst_index_get_writer_id (demux->element_index,
              GST_OBJECT (stream->pad), &stream->index_writer_id);

        GST_LOG_OBJECT (demux, "adding association %" GST_TIME_FORMAT "-> %"
            G_GUINT64_FORMAT " for writer id %d",
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (sub)), cluster_offset,
            stream->index_writer_id);
        gst_index_add_association (demux->element_index,
            stream->index_writer_id, GST_BUFFER_FLAG_IS_SET (sub,
                GST_BUFFER_FLAG_DELTA_UNIT) ? 0 : GST_ASSOCIATION_FLAG_KEY_UNIT,
            GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (sub), GST_FORMAT_BYTES,
            cluster_offset, NULL);
      }

      gst_buffer_set_caps (sub, GST_PAD_CAPS (stream->pad));

      /* Postprocess the buffers depending on the codec used */
      if (stream->postprocess_frame) {
        GST_LOG_OBJECT (demux, "running post process");
        ret = stream->postprocess_frame (GST_ELEMENT (demux), stream, &sub);
      }

      ret = gst_pad_push (stream->pad, sub);
      /* combine flows */
      ret = gst_matroska_demux_combine_flows (demux, stream, ret);

    next_lace:
      size -= lace_size[n];
      if (lace_time != GST_CLOCK_TIME_NONE && duration)
        lace_time += duration / laces;
      else
        lace_time = GST_CLOCK_TIME_NONE;
    }
  }

done:
  if (buf)
    gst_buffer_unref (buf);
  g_free (lace_size);

  return ret;
}

/* return FALSE if block(group) should be skipped (due to a seek) */
static inline gboolean
gst_matroska_demux_seek_block (GstMatroskaDemux * demux)
{
  if (G_UNLIKELY (demux->seek_block)) {
    if (!(--demux->seek_block)) {
      return TRUE;
    } else {
      GST_LOG_OBJECT (demux, "should skip block due to seek");
      return FALSE;
    }
  } else {
    return TRUE;
  }
}

static GstFlowReturn
gst_matroska_demux_parse_cluster (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 cluster_time = GST_CLOCK_TIME_NONE;
  guint32 id;
  guint64 cluster_offset = demux->parent.offset;

  DEBUG_ELEMENT_START (demux, ebml, "Cluster");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Cluster", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_CLUSTERTIMECODE:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "ClusterTimeCode: %" G_GUINT64_FORMAT, num);
        cluster_time = num;
        break;
      }

        /* a group of blocks inside a cluster */
      case GST_MATROSKA_ID_BLOCKGROUP:
        if (!gst_matroska_demux_seek_block (demux))
          goto skip;
        DEBUG_ELEMENT_START (demux, ebml, "BlockGroup");
        if ((ret = gst_ebml_read_master (ebml, &id)) == GST_FLOW_OK) {
          ret = gst_matroska_demux_parse_blockgroup_or_simpleblock (demux,
              cluster_time, cluster_offset, FALSE);
        }
        DEBUG_ELEMENT_STOP (demux, ebml, "BlockGroup", ret);
        break;

      case GST_MATROSKA_ID_SIMPLEBLOCK:
      {
        if (!gst_matroska_demux_seek_block (demux))
          goto skip;
        DEBUG_ELEMENT_START (demux, ebml, "SimpleBlock");
        ret = gst_matroska_demux_parse_blockgroup_or_simpleblock (demux,
            cluster_time, cluster_offset, TRUE);
        DEBUG_ELEMENT_STOP (demux, ebml, "SimpleBlock", ret);
        break;
      }

      default:
        GST_WARNING ("Unknown Cluster subelement 0x%x - ignoring", id);
        /* fall-through */
      case GST_MATROSKA_ID_POSITION:
      case GST_MATROSKA_ID_PREVSIZE:
      case GST_MATROSKA_ID_ENCRYPTEDBLOCK:
      case GST_MATROSKA_ID_SILENTTRACKS:
      skip:
        GST_DEBUG ("Skipping Cluster subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (demux->element_index) {
    if (demux->element_index_writer_id == -1)
      gst_index_get_writer_id (demux->element_index,
          GST_OBJECT (demux), &demux->element_index_writer_id);

    GST_LOG_OBJECT (demux, "adding association %" GST_TIME_FORMAT "-> %"
        G_GUINT64_FORMAT " for writer id %d",
        GST_TIME_ARGS (cluster_time), cluster_offset,
        demux->element_index_writer_id);
    gst_index_add_association (demux->element_index,
        demux->element_index_writer_id, GST_ASSOCIATION_FLAG_KEY_UNIT,
        GST_FORMAT_TIME, cluster_time, GST_FORMAT_BYTES, cluster_offset, NULL);
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "Cluster", ret);

  if (G_UNLIKELY (demux->seek_block)) {
    GST_DEBUG_OBJECT (demux, "seek target block %" G_GUINT64_FORMAT
        " not found in Cluster, trying next Cluster's first block instead",
        demux->seek_block);
    demux->seek_block = 0;
  }

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_contents_seekentry (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret;
  guint64 seek_pos = (guint64) - 1;
  guint32 seek_id = 0;
  guint32 id;

  DEBUG_ELEMENT_START (demux, ebml, "Seek");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (demux, ebml, "Seek", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SEEKID:
      {
        guint64 t;

        if ((ret = gst_ebml_read_uint (ebml, &id, &t)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (demux, "SeekID: %" G_GUINT64_FORMAT, t);
        seek_id = t;
        break;
      }

      case GST_MATROSKA_ID_SEEKPOSITION:
      {
        guint64 t;

        if ((ret = gst_ebml_read_uint (ebml, &id, &t)) != GST_FLOW_OK)
          break;

        if (t > G_MAXINT64) {
          GST_WARNING_OBJECT (demux,
              "Too large SeekPosition %" G_GUINT64_FORMAT, t);
          break;
        }

        GST_DEBUG_OBJECT (demux, "SeekPosition: %" G_GUINT64_FORMAT, t);
        seek_pos = t;
        break;
      }

      default:
        GST_WARNING_OBJECT (demux,
            "Unknown SeekHead subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED)
    return ret;

  if (!seek_id || seek_pos == (guint64) - 1) {
    GST_WARNING_OBJECT (demux, "Incomplete seekhead entry (0x%x/%"
        G_GUINT64_FORMAT ")", seek_id, seek_pos);
    return GST_FLOW_OK;
  }

  switch (seek_id) {
    case GST_MATROSKA_ID_CUES:
    case GST_MATROSKA_ID_TAGS:
    case GST_MATROSKA_ID_TRACKS:
    case GST_MATROSKA_ID_SEEKHEAD:
    case GST_MATROSKA_ID_SEGMENTINFO:
    case GST_MATROSKA_ID_ATTACHMENTS:
    case GST_MATROSKA_ID_CHAPTERS:
    {
      guint level_up = demux->level_up;
      guint64 before_pos, length;
      GstEbmlLevel *level;

      /* remember */
      length = gst_ebml_read_get_length (ebml);
      before_pos = ebml->offset;

      /* check for validity */
      if (seek_pos + demux->ebml_segment_start + 12 >= length) {
        GST_WARNING_OBJECT (demux,
            "SeekHead reference lies outside file!" " (%"
            G_GUINT64_FORMAT "+%" G_GUINT64_FORMAT "+12 >= %"
            G_GUINT64_FORMAT ")", seek_pos, demux->ebml_segment_start, length);
        break;
      }

      /* seek */
      if (gst_ebml_read_seek (ebml, seek_pos + demux->ebml_segment_start) !=
          GST_FLOW_OK)
        break;

      /* we don't want to lose our seekhead level, so we add
       * a dummy. This is a crude hack. */
      level = g_slice_new (GstEbmlLevel);
      level->start = 0;
      level->length = G_MAXUINT64;
      ebml->level = g_list_prepend (ebml->level, level);

      /* check ID */
      if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
        goto finish;

      if (id != seek_id) {
        GST_WARNING_OBJECT (demux,
            "We looked for ID=0x%x but got ID=0x%x (pos=%" G_GUINT64_FORMAT ")",
            seek_id, id, seek_pos + demux->ebml_segment_start);
        goto finish;
      }

      /* read master + parse */
      switch (id) {
        case GST_MATROSKA_ID_CUES:
          if (!demux->index_parsed) {
            ret = gst_matroska_demux_parse_index (demux);
          }
          break;
        case GST_MATROSKA_ID_TAGS:
          ret = gst_matroska_demux_parse_metadata (demux);
          break;
        case GST_MATROSKA_ID_TRACKS:
          if (!demux->tracks_parsed) {
            ret = gst_matroska_demux_parse_tracks (demux);
          }
          break;

        case GST_MATROSKA_ID_SEGMENTINFO:
          if (!demux->segmentinfo_parsed) {
            ret = gst_matroska_demux_parse_info (demux);
          }
          break;
        case GST_MATROSKA_ID_SEEKHEAD:
        {
          GList *l;

          DEBUG_ELEMENT_START (demux, ebml, "SeekHead");
          if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
            goto finish;

          /* Prevent infinite recursion if there's a cycle from
           * one seekhead to the same again. Simply break if
           * we already had this seekhead, finish will clean up
           * everything. */
          for (l = ebml->level; l; l = l->next) {
            GstEbmlLevel *level = (GstEbmlLevel *) l->data;

            if (level->start == ebml->offset && l->prev)
              goto finish;
          }

          ret = gst_matroska_demux_parse_contents (demux);
          break;
        }
        case GST_MATROSKA_ID_ATTACHMENTS:
          if (!demux->attachments_parsed) {
            ret = gst_matroska_demux_parse_attachments (demux);
          }
          break;
        case GST_MATROSKA_ID_CHAPTERS:
          ret = gst_matroska_demux_parse_chapters (demux);
          break;
      }

    finish:
      /* remove dummy level */
      while (ebml->level) {
        guint64 length;

        level = ebml->level->data;
        ebml->level = g_list_delete_link (ebml->level, ebml->level);
        length = level->length;
        g_slice_free (GstEbmlLevel, level);
        if (length == G_MAXUINT64)
          break;
      }

      /* seek back */
      (void) gst_ebml_read_seek (ebml, before_pos);
      demux->level_up = level_up;
      break;
    }

    default:
      GST_DEBUG_OBJECT (demux, "Ignoring Seek entry for ID=0x%x", seek_id);
      break;
  }
  DEBUG_ELEMENT_STOP (demux, ebml, "Seek", ret);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_contents (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SEEKENTRY:
      {
        ret = gst_matroska_demux_parse_contents_seekentry (demux);
        /* Ignore EOS and errors here */
        if (ret != GST_FLOW_OK) {
          GST_DEBUG_OBJECT (demux, "Ignoring %s", gst_flow_get_name (ret));
          ret = GST_FLOW_OK;
        }
        break;
      }

      default:
        GST_WARNING ("Unknown SeekHead subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  DEBUG_ELEMENT_STOP (demux, ebml, "SeekHead", ret);

  return ret;
}

/* returns FALSE on error, otherwise TRUE */
static GstFlowReturn
gst_matroska_demux_loop_stream_parse_id (GstMatroskaDemux * demux,
    guint32 id, gboolean * p_run_loop)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret = GST_FLOW_OK;

  switch (id) {
      /* stream info 
       * Can exist more than once but following occurences
       * must have the same content so ignore them */
    case GST_MATROSKA_ID_SEGMENTINFO:
      if (!demux->segmentinfo_parsed) {
        if ((ret = gst_matroska_demux_parse_info (demux)) != GST_FLOW_OK)
          return ret;
      } else {
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          return ret;
      }
      break;

      /* track info headers
       * Can exist more than once but following occurences
       * must have the same content so ignore them */
    case GST_MATROSKA_ID_TRACKS:
    {
      if (!demux->tracks_parsed) {
        if ((ret = gst_matroska_demux_parse_tracks (demux)) != GST_FLOW_OK)
          return ret;
      } else {
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          return ret;
      }
      break;
    }

      /* cues - seek table
       * Either exists exactly one time or never but ignore
       * following occurences for the sake of sanity */
    case GST_MATROSKA_ID_CUES:
    {
      if (!demux->index_parsed) {
        if ((ret = gst_matroska_demux_parse_index (demux)) != GST_FLOW_OK)
          return ret;
      } else {
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          return ret;
      }
      break;
    }

      /* metadata
       * can exist more than one time with different content */
    case GST_MATROSKA_ID_TAGS:
    {
      if ((ret = gst_matroska_demux_parse_metadata (demux)) != GST_FLOW_OK)
        return ret;
      break;
    }

      /* file index (if seekable, seek to Cues/Tags/etc to parse it) */
    case GST_MATROSKA_ID_SEEKHEAD:
    {
      DEBUG_ELEMENT_START (demux, ebml, "SeekHead");
      if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
        return ret;
      if ((ret = gst_matroska_demux_parse_contents (demux)) != GST_FLOW_OK)
        return ret;
      break;
    }

      /* cluster - contains the payload */
    case GST_MATROSKA_ID_CLUSTER:
    {
      if (demux->state != GST_MATROSKA_DEMUX_STATE_DATA) {
        /* We need a Tracks element first before we can output anything.
         * Search it!
         */
        if (!demux->tracks_parsed) {
          GstEbmlLevel *level;
          guint32 iid;
          guint level_up;
          guint64 before_pos;

          GST_WARNING_OBJECT (demux,
              "Found Cluster element before Tracks, searching Tracks");

          /* remember */
          level_up = demux->level_up;
          before_pos = ebml->offset;

          /* we don't want to lose our seekhead level, so we add
           * a dummy. This is a crude hack. */

          level = g_slice_new (GstEbmlLevel);
          level->start = 0;
          level->length = G_MAXUINT64;
          ebml->level = g_list_prepend (ebml->level, level);

          /* Search Tracks element */
          while (TRUE) {
            if (gst_ebml_read_skip (ebml) != GST_FLOW_OK)
              break;

            if (gst_ebml_peek_id (ebml, &demux->level_up, &iid) != GST_FLOW_OK)
              break;

            if (iid != GST_MATROSKA_ID_TRACKS)
              continue;

            gst_matroska_demux_parse_tracks (demux);
            break;
          }

          if (!demux->tracks_parsed) {
            GST_ERROR_OBJECT (demux, "No Tracks element found");
            ret = GST_FLOW_ERROR;
          }

          /* remove dummy level */
          while (ebml->level) {
            guint64 length;

            level = ebml->level->data;
            ebml->level = g_list_delete_link (ebml->level, ebml->level);
            length = level->length;
            g_slice_free (GstEbmlLevel, level);
            if (length == G_MAXUINT64)
              break;
          }

          /* seek back */
          gst_ebml_read_seek (ebml, before_pos);
          demux->level_up = level_up;
        }

        if (ret != GST_FLOW_OK)
          return ret;

        demux->state = GST_MATROSKA_DEMUX_STATE_DATA;
        GST_DEBUG_OBJECT (demux, "signaling no more pads");
        gst_element_no_more_pads (GST_ELEMENT (demux));
        /* send initial discont */
        gst_matroska_demux_send_event (demux,
            gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
      } else {
        /* The idea is that we parse one cluster per loop and
         * then break out of the loop here. In the next call
         * of the loopfunc, we will get back here with the
         * next cluster. If an error occurs, we didn't
         * actually push a buffer, but we still want to break
         * out of the loop to handle a possible error. We'll
         * get back here if it's recoverable. */
        if ((ret = gst_matroska_demux_parse_cluster (demux)) != GST_FLOW_OK)
          return ret;
        *p_run_loop = FALSE;
      }
      break;
    }

      /* attachments - contains files attached to the mkv container
       * like album art, etc */
    case GST_MATROSKA_ID_ATTACHMENTS:{
      if (!demux->attachments_parsed) {
        if ((ret = gst_matroska_demux_parse_attachments (demux)) != GST_FLOW_OK)
          return ret;
      } else {
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          return ret;
      }
      break;
    }

      /* chapters - contains meta information about how to group
       * the file into chapters, similar to DVD */
    case GST_MATROSKA_ID_CHAPTERS:{
      if ((ret = gst_matroska_demux_parse_chapters (demux)) != GST_FLOW_OK)
        return ret;
      break;
    }

    default:
      GST_WARNING_OBJECT (demux, "Unknown Segment subelement 0x%x at %"
          G_GUINT64_FORMAT " - ignoring", id, GST_EBML_READ (demux)->offset);
      if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
        return ret;
      break;
  }
  return ret;
}

static GstFlowReturn
gst_matroska_demux_loop_stream (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean run_loop = TRUE;
  guint32 id;

  /* we've found our segment, start reading the different contents in here */
  while (run_loop && ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    ret = gst_matroska_demux_loop_stream_parse_id (demux, id, &run_loop);

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }
  return ret;
}

static void
gst_matroska_demux_loop (GstPad * pad)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (GST_PAD_PARENT (pad));
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstFlowReturn ret;

  /* first, if we're to start, let's actually get starting */
  if (G_UNLIKELY (demux->state == GST_MATROSKA_DEMUX_STATE_START)) {
    ret = gst_matroska_demux_init_stream (demux);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (demux, "init stream failed!");
      goto pause;
    }
    demux->state = GST_MATROSKA_DEMUX_STATE_HEADER;
  }

  /* If we have to close a segment, send a new segment to do this now */

  if (G_LIKELY (demux->state == GST_MATROSKA_DEMUX_STATE_DATA)) {
    if (G_UNLIKELY (demux->close_segment)) {
      gst_matroska_demux_send_event (demux, demux->close_segment);
      demux->close_segment = NULL;
    }
    if (G_UNLIKELY (demux->new_segment)) {
      gst_matroska_demux_send_event (demux, demux->new_segment);
      demux->new_segment = NULL;
    }
  }

  ret = gst_matroska_demux_loop_stream (demux);
  if (ret != GST_FLOW_OK)
    goto pause;

  /* check if we're at the end of a configured segment */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (demux->segment.stop))) {
    guint i;

    g_assert (demux->num_streams == demux->src->len);
    for (i = 0; i < demux->src->len; i++) {
      GstMatroskaTrackContext *context = g_ptr_array_index (demux->src, i);
      if (context->pos >= demux->segment.stop) {
        GST_INFO_OBJECT (demux, "Reached end of segment (%" G_GUINT64_FORMAT
            "-%" G_GUINT64_FORMAT ") on pad %s:%s", demux->segment.start,
            demux->segment.stop, GST_DEBUG_PAD_NAME (context->pad));
        ret = GST_FLOW_UNEXPECTED;
        goto pause;
      }
    }
  }

  if (G_UNLIKELY (ebml->offset == gst_ebml_read_get_length (ebml))) {
    GST_LOG_OBJECT (demux, "Reached end of stream, sending EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto pause;
  }

  return;

  /* ERRORS */
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_LOG_OBJECT (demux, "pausing task, reason %s", reason);
    demux->segment_running = FALSE;
    gst_pad_pause_task (demux->sinkpad);

    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      gboolean push_eos = TRUE;

      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */

        /* Close the segment, i.e. update segment stop with the duration
         * if no stop was set */
        if (GST_CLOCK_TIME_IS_VALID (demux->last_stop_end) &&
            !GST_CLOCK_TIME_IS_VALID (demux->segment.stop)) {
          GstEvent *event =
              gst_event_new_new_segment_full (TRUE, demux->segment.rate,
              demux->segment.applied_rate, demux->segment.format,
              demux->segment.start,
              MAX (demux->last_stop_end, demux->segment.start),
              demux->segment.time);
          gst_matroska_demux_send_event (demux, event);
        }

        if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gint64 stop;

          /* for segment playback we need to post when (in stream time)
           * we stopped, this is either stop (when set) or the duration. */
          if ((stop = demux->segment.stop) == -1)
            stop = demux->last_stop_end;

          GST_LOG_OBJECT (demux, "Sending segment done, at end of segment");
          gst_element_post_message (GST_ELEMENT (demux),
              gst_message_new_segment_done (GST_OBJECT (demux), GST_FORMAT_TIME,
                  stop));
          push_eos = FALSE;
        }
      } else {
        /* for fatal errors we post an error message */
        GST_ELEMENT_ERROR (demux, STREAM, FAILED, (NULL),
            ("stream stopped, reason %s", reason));
      }
      if (push_eos) {
        /* send EOS, and prevent hanging if no streams yet */
        GST_LOG_OBJECT (demux, "Sending EOS, at end of stream");
        if (!gst_matroska_demux_send_event (demux, gst_event_new_eos ()) &&
            (ret == GST_FLOW_UNEXPECTED)) {
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
              (NULL), ("got eos but no streams (yet)"));
        }
      }
    }
    return;
  }
}

static inline void
gst_matroska_demux_take (GstMatroskaDemux * demux, guint bytes)
{
  GstBuffer *buffer;

  GST_LOG_OBJECT (demux, "caching %d bytes for parsing", bytes);
  buffer = gst_adapter_take_buffer (demux->adapter, bytes);
  gst_ebml_read_reset_cache (GST_EBML_READ (demux), buffer, demux->offset);
  demux->offset += bytes;
}

static inline void
gst_matroska_demux_flush (GstMatroskaDemux * demux, guint flush)
{
  GST_LOG_OBJECT (demux, "skipping %d bytes", flush);
  gst_adapter_flush (demux->adapter, flush);
  demux->offset += flush;
}

static GstFlowReturn
gst_matroska_demux_peek_id_length (GstMatroskaDemux * demux, guint32 * _id,
    guint64 * _length, guint * _needed)
{
  guint avail, needed;
  const guint8 *buf;
  gint len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
  guint64 total;
  guint8 b;

  g_return_val_if_fail (_id != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (_length != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (_needed != NULL, GST_FLOW_ERROR);

  /* well ... */
  *_id = (guint32) GST_EBML_SIZE_UNKNOWN;
  *_length = GST_EBML_SIZE_UNKNOWN;

  /* read element id */
  needed = 2;
  avail = gst_adapter_available (demux->adapter);
  if (avail < needed)
    goto exit;

  buf = gst_adapter_peek (demux->adapter, 1);
  b = GST_READ_UINT8 (buf);

  total = (guint64) b;
  while (read <= 4 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (G_UNLIKELY (read > 4))
    goto invalid_id;
  /* need id and at least something for subsequent length */
  if ((needed = read + 1) > avail)
    goto exit;

  buf = gst_adapter_peek (demux->adapter, needed);
  while (n < read) {
    b = GST_READ_UINT8 (buf + n);
    total = (total << 8) | b;
    ++n;
  }
  *_id = (guint32) total;

  /* read element length */
  b = GST_READ_UINT8 (buf + n);
  total = (guint64) b;
  len_mask = 0x80;
  read = 1;
  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (G_UNLIKELY (read > 8))
    goto invalid_length;
  if ((needed += read - 1) > avail)
    goto exit;
  if ((total &= (len_mask - 1)) == len_mask - 1)
    num_ffs++;

  buf = gst_adapter_peek (demux->adapter, needed);
  buf += (needed - read);
  n = 1;
  while (n < read) {
    guint8 b = GST_READ_UINT8 (buf + n);

    if (G_UNLIKELY (b == 0xff))
      num_ffs++;
    total = (total << 8) | b;
    ++n;
  }

  if (G_UNLIKELY (read == num_ffs))
    *_length = G_MAXUINT64;
  else
    *_length = total;
  *_length = total;

exit:
  *_needed = needed;

  return GST_FLOW_OK;

  /* ERRORS */
invalid_id:
  {
    GST_ERROR_OBJECT (demux,
        "Invalid EBML ID size tag (0x%x) at position %" G_GUINT64_FORMAT " (0x%"
        G_GINT64_MODIFIER "x)", (guint) b, demux->offset, demux->offset);
    return GST_FLOW_ERROR;
  }
invalid_length:
  {
    GST_ERROR_OBJECT (demux,
        "Invalid EBML length size tag (0x%x) at position %" G_GUINT64_FORMAT
        " (0x%" G_GINT64_MODIFIER "x)", (guint) b, demux->offset,
        demux->offset);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_matroska_demux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (GST_PAD_PARENT (pad));
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint available;
  GstFlowReturn ret = GST_FLOW_OK;
  guint needed = 0;
  guint32 id;
  guint64 length;
  gchar *name;

  gst_adapter_push (demux->adapter, buffer);
  buffer = NULL;

next:
  available = gst_adapter_available (demux->adapter);

  ret = gst_matroska_demux_peek_id_length (demux, &id, &length, &needed);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto parse_failed;

  GST_LOG_OBJECT (demux, "Offset %" G_GUINT64_FORMAT ", Element id 0x%x, "
      "size %" G_GUINT64_FORMAT ", needed %d, available %d", demux->offset, id,
      length, needed, available);

  if (needed > available)
    return GST_FLOW_OK;

  /* only a few blocks are expected/allowed to be large,
   * and will be recursed into, whereas others must fit */
  if (G_LIKELY (id != GST_MATROSKA_ID_SEGMENT && id != GST_MATROSKA_ID_CLUSTER)) {
    if (needed + length > available)
      return GST_FLOW_OK;
    /* probably happens with 'large pieces' at the end, so consider it EOS */
    if (G_UNLIKELY (length > 10 * 1024 * 1024)) {
      GST_WARNING_OBJECT (demux, "forcing EOS due to size %" G_GUINT64_FORMAT,
          length);
      return GST_FLOW_UNEXPECTED;
    }
  }

  switch (demux->state) {
    case GST_MATROSKA_DEMUX_STATE_START:
      switch (id) {
        case GST_EBML_ID_HEADER:
          gst_matroska_demux_take (demux, length + needed);
          ret = gst_matroska_demux_parse_header (demux);
          if (ret != GST_FLOW_OK)
            goto parse_failed;
          demux->state = GST_MATROSKA_DEMUX_STATE_HEADER;
          break;
        default:
          goto invalid_header;
          break;
      }
      break;
    case GST_MATROSKA_DEMUX_STATE_HEADER:
    case GST_MATROSKA_DEMUX_STATE_DATA:
      switch (id) {
        case GST_MATROSKA_ID_SEGMENT:
          /* eat segment prefix */
          gst_matroska_demux_flush (demux, needed);
          GST_DEBUG_OBJECT (demux,
              "Found Segment start at offset %" G_GUINT64_FORMAT, ebml->offset);
          /* seeks are from the beginning of the segment,
           * after the segment ID/length */
          demux->ebml_segment_start = demux->offset;
          break;
        case GST_MATROSKA_ID_SEGMENTINFO:
          if (!demux->segmentinfo_parsed) {
            gst_matroska_demux_take (demux, length + needed);
            ret = gst_matroska_demux_parse_info (demux);
          } else {
            gst_matroska_demux_flush (demux, needed + length);
          }
          break;
        case GST_MATROSKA_ID_TRACKS:
          if (!demux->tracks_parsed) {
            gst_matroska_demux_take (demux, length + needed);
            ret = gst_matroska_demux_parse_tracks (demux);
          } else {
            gst_matroska_demux_flush (demux, needed + length);
          }
          break;
        case GST_MATROSKA_ID_CLUSTER:
          if (G_UNLIKELY (!demux->tracks_parsed)) {
            GST_DEBUG_OBJECT (demux, "Cluster before Track");
            goto not_streamable;
          } else if (demux->state == GST_MATROSKA_DEMUX_STATE_HEADER) {
            demux->state = GST_MATROSKA_DEMUX_STATE_DATA;
            GST_DEBUG_OBJECT (demux, "signaling no more pads");
            gst_element_no_more_pads (GST_ELEMENT (demux));
            /* send initial newsegment */
            gst_matroska_demux_send_event (demux,
                gst_event_new_new_segment (FALSE, 1.0,
                    GST_FORMAT_TIME, 0,
                    (demux->segment.duration >
                        0) ? demux->segment.duration : -1, 0));
          }
          demux->cluster_time = GST_CLOCK_TIME_NONE;
          demux->cluster_offset = demux->parent.offset;
          /* eat cluster prefix */
          gst_matroska_demux_flush (demux, needed);
          break;
        case GST_MATROSKA_ID_CLUSTERTIMECODE:
        {
          guint64 num;

          gst_matroska_demux_take (demux, length + needed);
          if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
            goto parse_failed;
          GST_DEBUG_OBJECT (demux, "ClusterTimeCode: %" G_GUINT64_FORMAT, num);
          demux->cluster_time = num;
          break;
        }
        case GST_MATROSKA_ID_BLOCKGROUP:
          gst_matroska_demux_take (demux, length + needed);
          DEBUG_ELEMENT_START (demux, ebml, "BlockGroup");
          if ((ret = gst_ebml_read_master (ebml, &id)) == GST_FLOW_OK) {
            ret = gst_matroska_demux_parse_blockgroup_or_simpleblock (demux,
                demux->cluster_time, demux->cluster_offset, FALSE);
          }
          DEBUG_ELEMENT_STOP (demux, ebml, "BlockGroup", ret);
          if (ret != GST_FLOW_OK)
            goto exit;
          break;
        case GST_MATROSKA_ID_SIMPLEBLOCK:
          gst_matroska_demux_take (demux, length + needed);
          DEBUG_ELEMENT_START (demux, ebml, "SimpleBlock");
          ret = gst_matroska_demux_parse_blockgroup_or_simpleblock (demux,
              demux->cluster_time, demux->cluster_offset, TRUE);
          DEBUG_ELEMENT_STOP (demux, ebml, "SimpleBlock", ret);
          if (ret != GST_FLOW_OK)
            goto exit;
          break;
        case GST_MATROSKA_ID_ATTACHMENTS:
          if (!demux->attachments_parsed) {
            gst_matroska_demux_take (demux, length + needed);
            ret = gst_matroska_demux_parse_attachments (demux);
          } else {
            gst_matroska_demux_flush (demux, needed + length);
          }
          break;
        case GST_MATROSKA_ID_TAGS:
          gst_matroska_demux_take (demux, length + needed);
          ret = gst_matroska_demux_parse_metadata (demux);
          break;
        case GST_MATROSKA_ID_CHAPTERS:
          name = "Cues";
          goto skip;
        case GST_MATROSKA_ID_SEEKHEAD:
          name = "SeekHead";
          goto skip;
        case GST_MATROSKA_ID_CUES:
          name = "Cues";
          goto skip;
        default:
          name = "Unknown";
        skip:
          GST_DEBUG_OBJECT (demux, "skipping Element 0x%x (%s)", id, name);
          gst_matroska_demux_flush (demux, needed + length);
          break;
      }
      break;
  }

  if (ret != GST_FLOW_OK)
    goto parse_failed;
  else
    goto next;

exit:
  return ret;

  /* ERRORS */
parse_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("Failed to parse Element 0x%x", id));
    return GST_FLOW_ERROR;
  }
not_streamable:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("File layout does not permit streaming"));
    return GST_FLOW_ERROR;
  }
invalid_header:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL), ("Invalid header"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_matroska_demux_handle_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (demux,
      "have event type %s: %p on sink pad", GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time = 0;
      gboolean update;
      GstSegment segment;

      /* some debug output */
      gst_segment_init (&segment, GST_FORMAT_UNDEFINED);
      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);
      gst_segment_set_newsegment_full (&segment, update, rate, arate, format,
          start, stop, time);
      GST_DEBUG_OBJECT (demux,
          "received format %d newsegment %" GST_SEGMENT_FORMAT, format,
          &segment);

      /* chain will send initial newsegment after pads have been added */
      GST_DEBUG_OBJECT (demux, "eating event");
      gst_event_unref (event);
      res = TRUE;
      break;
    }
    case GST_EVENT_EOS:
    {
      if (demux->state != GST_MATROSKA_DEMUX_STATE_DATA) {
        gst_event_unref (event);
        GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
            (NULL), ("got eos and didn't receive a complete header object"));
      } else if (demux->num_streams == 0) {
        GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
            (NULL), ("got eos but no streams (yet)"));
      } else {
        gst_matroska_demux_send_event (demux, event);
      }
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}

static gboolean
gst_matroska_demux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    GST_DEBUG ("going to pull mode");
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    GST_DEBUG ("going to push (streaming) mode");
    return gst_pad_activate_push (sinkpad, TRUE);
  }

  return FALSE;
}

static gboolean
gst_matroska_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (GST_PAD_PARENT (sinkpad));

  if (active) {
    /* if we have a scheduler we can start the task */
    demux->segment_running = TRUE;
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_matroska_demux_loop,
        sinkpad);
  } else {
    demux->segment_running = FALSE;
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

static GstCaps *
gst_matroska_demux_video_caps (GstMatroskaTrackVideoContext *
    videocontext, const gchar * codec_id, guint8 * data, guint size,
    gchar ** codec_name)
{
  GstMatroskaTrackContext *context = (GstMatroskaTrackContext *) videocontext;
  GstCaps *caps = NULL;

  g_assert (videocontext != NULL);
  g_assert (codec_name != NULL);

  context->send_xiph_headers = FALSE;
  context->send_flac_headers = FALSE;
  context->send_speex_headers = FALSE;

  /* TODO: check if we have all codec types from matroska-ids.h
   *       check if we have to do more special things with codec_private
   *
   * Add support for
   *  GST_MATROSKA_CODEC_ID_VIDEO_QUICKTIME
   *  GST_MATROSKA_CODEC_ID_VIDEO_SNOW
   */

  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC)) {
    gst_riff_strf_vids *vids = NULL;

    if (data) {
      GstBuffer *buf = NULL;

      vids = (gst_riff_strf_vids *) data;

      /* assure size is big enough */
      if (size < 24) {
        GST_WARNING ("Too small BITMAPINFOHEADER (%d bytes)", size);
        return NULL;
      }
      if (size < sizeof (gst_riff_strf_vids)) {
        vids = g_new (gst_riff_strf_vids, 1);
        memcpy (vids, data, size);
      }

      /* little-endian -> byte-order */
      vids->size = GUINT32_FROM_LE (vids->size);
      vids->width = GUINT32_FROM_LE (vids->width);
      vids->height = GUINT32_FROM_LE (vids->height);
      vids->planes = GUINT16_FROM_LE (vids->planes);
      vids->bit_cnt = GUINT16_FROM_LE (vids->bit_cnt);
      vids->compression = GUINT32_FROM_LE (vids->compression);
      vids->image_size = GUINT32_FROM_LE (vids->image_size);
      vids->xpels_meter = GUINT32_FROM_LE (vids->xpels_meter);
      vids->ypels_meter = GUINT32_FROM_LE (vids->ypels_meter);
      vids->num_colors = GUINT32_FROM_LE (vids->num_colors);
      vids->imp_colors = GUINT32_FROM_LE (vids->imp_colors);

      if (size > sizeof (gst_riff_strf_vids)) { /* some extra_data */
        buf = gst_buffer_new_and_alloc (size - sizeof (gst_riff_strf_vids));
        memcpy (GST_BUFFER_DATA (buf),
            (guint8 *) vids + sizeof (gst_riff_strf_vids),
            GST_BUFFER_SIZE (buf));
      }

      caps = gst_riff_create_video_caps (vids->compression, NULL, vids,
          buf, NULL, codec_name);

      if (buf)
        gst_buffer_unref (buf);

      if (vids != (gst_riff_strf_vids *) data)
        g_free (vids);
    }
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED)) {
    guint32 fourcc = 0;

    switch (videocontext->fourcc) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        *codec_name = g_strdup ("Raw planar YUV 4:2:0");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        *codec_name = g_strdup ("Raw packed YUV 4:2:2");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
        *codec_name = g_strdup ("Raw packed YUV 4:2:0");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        *codec_name = g_strdup ("Raw packed YUV 4:2:2");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
        *codec_name = g_strdup ("Raw packed YUV 4:4:4 with alpha channel");
        fourcc = videocontext->fourcc;
        break;

      default:
        GST_DEBUG ("Unknown fourcc %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (videocontext->fourcc));
        return NULL;
    }

    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fourcc, NULL);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP)) {
    caps = gst_caps_new_simple ("video/x-divx",
        "divxversion", G_TYPE_INT, 4, NULL);
    *codec_name = g_strdup ("MPEG-4 simple profile");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AP)) {
#if 0
    caps = gst_caps_new_full (gst_structure_new ("video/x-divx",
            "divxversion", G_TYPE_INT, 5, NULL),
        gst_structure_new ("video/x-xvid", NULL),
        gst_structure_new ("video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL), NULL);
#endif
    caps = gst_caps_new_simple ("video/mpeg",
        "mpegversion", G_TYPE_INT, 4,
        "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    if (data) {
      GstBuffer *priv = gst_buffer_new_and_alloc (size);

      memcpy (GST_BUFFER_DATA (priv), data, size);
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
      gst_buffer_unref (priv);
    }
    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP))
      *codec_name = g_strdup ("MPEG-4 advanced simple profile");
    else
      *codec_name = g_strdup ("MPEG-4 advanced profile");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3)) {
#if 0
    caps = gst_caps_new_full (gst_structure_new ("video/x-divx",
            "divxversion", G_TYPE_INT, 3, NULL),
        gst_structure_new ("video/x-msmpeg",
            "msmpegversion", G_TYPE_INT, 43, NULL), NULL);
#endif
    caps = gst_caps_new_simple ("video/x-msmpeg",
        "msmpegversion", G_TYPE_INT, 43, NULL);
    *codec_name = g_strdup ("Microsoft MPEG-4 v.3");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2)) {
    gint mpegversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1))
      mpegversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2))
      mpegversion = 2;
    else
      g_assert_not_reached ();

    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "mpegversion", G_TYPE_INT, mpegversion, NULL);
    *codec_name = g_strdup_printf ("MPEG-%d video", mpegversion);
    context->postprocess_frame = gst_matroska_demux_add_mpeg_seq_header;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MJPEG)) {
    caps = gst_caps_new_simple ("image/jpeg", NULL);
    *codec_name = g_strdup ("Motion-JPEG");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AVC)) {
    caps = gst_caps_new_simple ("video/x-h264", NULL);
    if (data) {
      GstBuffer *priv = gst_buffer_new_and_alloc (size);

      memcpy (GST_BUFFER_DATA (priv), data, size);
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
      gst_buffer_unref (priv);

    }
    *codec_name = g_strdup ("H264");
  } else if ((!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO1)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO2)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO3)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO4))) {
    gint rmversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO1))
      rmversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO2))
      rmversion = 2;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO3))
      rmversion = 3;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO4))
      rmversion = 4;

    caps = gst_caps_new_simple ("video/x-pn-realvideo",
        "rmversion", G_TYPE_INT, rmversion, NULL);
    GST_DEBUG ("data:%p, size:0x%x", data, size);
    /* We need to extract the extradata ! */
    if (data && (size >= 0x22)) {
      GstBuffer *priv;
      guint rformat;
      guint subformat;

      subformat = GST_READ_UINT32_BE (data + 0x1a);
      rformat = GST_READ_UINT32_BE (data + 0x1e);

      priv = gst_buffer_new_and_alloc (size - 0x1a);

      memcpy (GST_BUFFER_DATA (priv), data + 0x1a, size - 0x1a);
      gst_caps_set_simple (caps,
          "codec_data", GST_TYPE_BUFFER, priv,
          "format", G_TYPE_INT, rformat,
          "subformat", G_TYPE_INT, subformat, NULL);
      gst_buffer_unref (priv);

    }
    *codec_name = g_strdup_printf ("RealVideo %d.0", rmversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_THEORA)) {
    caps = gst_caps_new_simple ("video/x-theora", NULL);
    context->send_xiph_headers = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_DIRAC)) {
    caps = gst_caps_new_simple ("video/x-dirac", NULL);
    context->send_xiph_headers = FALSE;
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps", codec_id);
    return NULL;
  }

  if (caps != NULL) {
    int i;
    GstStructure *structure;

    for (i = 0; i < gst_caps_get_size (caps); i++) {
      structure = gst_caps_get_structure (caps, i);

      /* FIXME: use the real unit here! */
      GST_DEBUG ("video size %dx%d, target display size %dx%d (any unit)",
          videocontext->pixel_width,
          videocontext->pixel_height,
          videocontext->display_width, videocontext->display_height);

      /* pixel width and height are the w and h of the video in pixels */
      if (videocontext->pixel_width > 0 && videocontext->pixel_height > 0) {
        gint w = videocontext->pixel_width;

        gint h = videocontext->pixel_height;

        gst_structure_set (structure,
            "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, NULL);
      }

      if (videocontext->display_width > 0 && videocontext->display_height > 0) {
        int n, d;

        /* calculate the pixel aspect ratio using the display and pixel w/h */
        n = videocontext->display_width * videocontext->pixel_height;
        d = videocontext->display_height * videocontext->pixel_width;
        GST_DEBUG ("setting PAR to %d/%d", n, d);
        gst_structure_set (structure, "pixel-aspect-ratio",
            GST_TYPE_FRACTION,
            videocontext->display_width * videocontext->pixel_height,
            videocontext->display_height * videocontext->pixel_width, NULL);
      }

      if (videocontext->default_fps > 0.0) {
        GValue fps_double = { 0, };
        GValue fps_fraction = { 0, };

        g_value_init (&fps_double, G_TYPE_DOUBLE);
        g_value_init (&fps_fraction, GST_TYPE_FRACTION);
        g_value_set_double (&fps_double, videocontext->default_fps);
        g_value_transform (&fps_double, &fps_fraction);

        GST_DEBUG ("using default fps %f", videocontext->default_fps);

        gst_structure_set_value (structure, "framerate", &fps_fraction);
        g_value_unset (&fps_double);
        g_value_unset (&fps_fraction);
      } else if (context->default_duration > 0) {
        GValue fps_double = { 0, };
        GValue fps_fraction = { 0, };

        g_value_init (&fps_double, G_TYPE_DOUBLE);
        g_value_init (&fps_fraction, GST_TYPE_FRACTION);
        g_value_set_double (&fps_double, (gdouble) GST_SECOND /
            gst_guint64_to_gdouble (context->default_duration));
        g_value_transform (&fps_double, &fps_fraction);

        GST_DEBUG ("using default duration %" G_GUINT64_FORMAT,
            context->default_duration);

        gst_structure_set_value (structure, "framerate", &fps_fraction);
        g_value_unset (&fps_double);
        g_value_unset (&fps_fraction);
      } else {
        /* sort of a hack to get most codecs to support,
         * even if the default_duration is missing */
        gst_structure_set (structure, "framerate", GST_TYPE_FRACTION,
            25, 1, NULL);
      }
    }

    gst_caps_do_simplify (caps);
  }

  return caps;
}

/*
 * Some AAC specific code... *sigh*
 * FIXME: maybe we should use '15' and code the sample rate explicitly
 * if the sample rate doesn't match the predefined rates exactly? (tpm)
 */

static gint
aac_rate_idx (gint rate)
{
  if (92017 <= rate)
    return 0;
  else if (75132 <= rate)
    return 1;
  else if (55426 <= rate)
    return 2;
  else if (46009 <= rate)
    return 3;
  else if (37566 <= rate)
    return 4;
  else if (27713 <= rate)
    return 5;
  else if (23004 <= rate)
    return 6;
  else if (18783 <= rate)
    return 7;
  else if (13856 <= rate)
    return 8;
  else if (11502 <= rate)
    return 9;
  else if (9391 <= rate)
    return 10;
  else
    return 11;
}

static gint
aac_profile_idx (const gchar * codec_id)
{
  gint profile;

  if (strlen (codec_id) <= 12)
    profile = 3;
  else if (!strncmp (&codec_id[12], "MAIN", 4))
    profile = 0;
  else if (!strncmp (&codec_id[12], "LC", 2))
    profile = 1;
  else if (!strncmp (&codec_id[12], "SSR", 3))
    profile = 2;
  else
    profile = 3;

  return profile;
}

#define AAC_SYNC_EXTENSION_TYPE 0x02b7

static GstCaps *
gst_matroska_demux_audio_caps (GstMatroskaTrackAudioContext *
    audiocontext, const gchar * codec_id, guint8 * data, guint size,
    gchar ** codec_name)
{
  GstMatroskaTrackContext *context = (GstMatroskaTrackContext *) audiocontext;
  GstCaps *caps = NULL;

  g_assert (audiocontext != NULL);
  g_assert (codec_name != NULL);

  context->send_xiph_headers = FALSE;
  context->send_flac_headers = FALSE;
  context->send_speex_headers = FALSE;

  /* TODO: check if we have all codec types from matroska-ids.h
   *       check if we have to do more special things with codec_private
   *       check if we need bitdepth in different places too
   *       implement channel position magic
   * Add support for:
   *  GST_MATROSKA_CODEC_ID_AUDIO_AC3_BSID9
   *  GST_MATROSKA_CODEC_ID_AUDIO_AC3_BSID10
   *  GST_MATROSKA_CODEC_ID_AUDIO_QUICKTIME_QDMC
   *  GST_MATROSKA_CODEC_ID_AUDIO_QUICKTIME_QDM2
   */

  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3)) {
    gint layer = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1))
      layer = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2))
      layer = 2;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3))
      layer = 3;
    else
      g_assert_not_reached ();

    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, layer, NULL);
    *codec_name = g_strdup_printf ("MPEG-1 layer %d", layer);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE)) {
    gint endianness = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE))
      endianness = G_BIG_ENDIAN;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE))
      endianness = G_LITTLE_ENDIAN;
    else
      g_assert_not_reached ();

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, audiocontext->bitdepth,
        "depth", G_TYPE_INT, audiocontext->bitdepth,
        "signed", G_TYPE_BOOLEAN, audiocontext->bitdepth != 8,
        "endianness", G_TYPE_INT, endianness, NULL);

    *codec_name = g_strdup_printf ("Raw %d-bit PCM audio",
        audiocontext->bitdepth);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT)) {
    caps = gst_caps_new_simple ("audio/x-raw-float",
        "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
        "width", G_TYPE_INT, audiocontext->bitdepth, NULL);
    *codec_name = g_strdup_printf ("Raw %d-bit floating-point audio",
        audiocontext->bitdepth);
  } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AC3,
          strlen (GST_MATROSKA_CODEC_ID_AUDIO_AC3))) {
    caps = gst_caps_new_simple ("audio/x-ac3", NULL);
    *codec_name = g_strdup ("AC-3 audio");
  } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_EAC3,
          strlen (GST_MATROSKA_CODEC_ID_AUDIO_EAC3))) {
    caps = gst_caps_new_simple ("audio/x-eac3", NULL);
    *codec_name = g_strdup ("E-AC-3 audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_DTS)) {
    caps = gst_caps_new_simple ("audio/x-dts", NULL);
    *codec_name = g_strdup ("DTS audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_VORBIS)) {
    caps = gst_caps_new_simple ("audio/x-vorbis", NULL);
    context->send_xiph_headers = TRUE;
    /* vorbis decoder does tags */
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_FLAC)) {
    caps = gst_caps_new_simple ("audio/x-flac", NULL);
    context->send_flac_headers = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_SPEEX)) {
    caps = gst_caps_new_simple ("audio/x-speex", NULL);
    context->send_speex_headers = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_ACM)) {
    gst_riff_strf_auds auds;

    if (data) {
      GstBuffer *codec_data = gst_buffer_new ();

      /* little-endian -> byte-order */
      auds.format = GST_READ_UINT16_LE (data);
      auds.channels = GST_READ_UINT16_LE (data + 2);
      auds.rate = GST_READ_UINT32_LE (data + 4);
      auds.av_bps = GST_READ_UINT32_LE (data + 8);
      auds.blockalign = GST_READ_UINT16_LE (data + 12);
      auds.size = GST_READ_UINT16_LE (data + 16);

      /* 18 is the waveformatex size */
      gst_buffer_set_data (codec_data, data + 18, auds.size);

      caps = gst_riff_create_audio_caps (auds.format, NULL, &auds, NULL,
          codec_data, codec_name);
      gst_buffer_unref (codec_data);
    }
  } else if (g_str_has_prefix (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC)) {
    GstBuffer *priv = NULL;
    gint mpegversion = -1;
    gint rate_idx, profile;
    guint8 *data = NULL;

    /* unspecified AAC profile with opaque private codec data */
    if (strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC) == 0) {
      if (context->codec_priv_size >= 2) {
        guint obj_type, freq_index, explicit_freq_bytes = 0;

        codec_id = GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4;
        freq_index = (GST_READ_UINT16_BE (context->codec_priv) & 0x780) >> 7;
        obj_type = (GST_READ_UINT16_BE (context->codec_priv) & 0xF800) >> 11;
        if (freq_index == 15)
          explicit_freq_bytes = 3;
        GST_DEBUG ("obj_type = %u, freq_index = %u", obj_type, freq_index);
        priv = gst_buffer_new_and_alloc (context->codec_priv_size);
        memcpy (GST_BUFFER_DATA (priv), context->codec_priv,
            context->codec_priv_size);
        /* assume SBR if samplerate <= 24kHz */
        if (obj_type == 5 || (freq_index >= 6 && freq_index != 15) ||
            (context->codec_priv_size == (5 + explicit_freq_bytes))) {
          audiocontext->samplerate *= 2;
        }
      } else {
        GST_WARNING ("Opaque A_AAC codec ID, but no codec private data");
        /* just try this and see what happens ... */
        codec_id = GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4;
      }
    }

    /* make up decoder-specific data if it is not supplied */
    if (priv == NULL) {
      priv = gst_buffer_new_and_alloc (5);
      data = GST_BUFFER_DATA (priv);
      rate_idx = aac_rate_idx (audiocontext->samplerate);
      profile = aac_profile_idx (codec_id);

      data[0] = ((profile + 1) << 3) | ((rate_idx & 0xE) >> 1);
      data[1] = ((rate_idx & 0x1) << 7) | (audiocontext->channels << 3);
      GST_BUFFER_SIZE (priv) = 2;
    }

    if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG2,
            strlen (GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG2))) {
      mpegversion = 2;
    } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4,
            strlen (GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4))) {
      mpegversion = 4;

      if (g_strrstr (codec_id, "SBR")) {
        /* HE-AAC (aka SBR AAC) */
        audiocontext->samplerate *= 2;
        rate_idx = aac_rate_idx (audiocontext->samplerate);
        data[2] = AAC_SYNC_EXTENSION_TYPE >> 3;
        data[3] = ((AAC_SYNC_EXTENSION_TYPE & 0x07) << 5) | 5;
        data[4] = (1 << 7) | (rate_idx << 3);
        GST_BUFFER_SIZE (priv) = 5;
      }
    } else {
      g_assert_not_reached ();
    }

    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, mpegversion,
        "framed", G_TYPE_BOOLEAN, TRUE, NULL);
    if (priv) {
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
    }
    *codec_name = g_strdup_printf ("MPEG-%d AAC audio", mpegversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_TTA)) {
    caps = gst_caps_new_simple ("audio/x-tta",
        "width", G_TYPE_INT, audiocontext->bitdepth, NULL);
    *codec_name = g_strdup ("TTA audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_WAVPACK4)) {
    caps = gst_caps_new_simple ("audio/x-wavpack",
        "width", G_TYPE_INT, audiocontext->bitdepth,
        "framed", G_TYPE_BOOLEAN, TRUE, NULL);
    *codec_name = g_strdup ("Wavpack audio");
    context->postprocess_frame = gst_matroska_demux_add_wvpk_header;
    audiocontext->wvpk_block_index = 0;
  } else if ((!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_COOK))) {
    gint raversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4))
      raversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_COOK))
      raversion = 8;
    else
      raversion = 2;

    caps = gst_caps_new_simple ("audio/x-pn-realaudio",
        "raversion", G_TYPE_INT, raversion, NULL);
    /* Extract extra information from caps, mapping varies based on codec */
    if (data && (size >= 0x50)) {
      GstBuffer *priv;
      guint flavor;
      guint packet_size;
      guint height;
      guint leaf_size;
      guint sample_width;
      guint extra_data_size;

      GST_ERROR ("real audio raversion:%d", raversion);
      if (raversion == 8) {
        /* COOK */
        flavor = GST_READ_UINT16_BE (data + 22);
        packet_size = GST_READ_UINT32_BE (data + 24);
        height = GST_READ_UINT16_BE (data + 40);
        leaf_size = GST_READ_UINT16_BE (data + 44);
        sample_width = GST_READ_UINT16_BE (data + 58);
        extra_data_size = GST_READ_UINT32_BE (data + 74);

        GST_ERROR
            ("flavor:%d, packet_size:%d, height:%d, leaf_size:%d, sample_width:%d, extra_data_size:%d",
            flavor, packet_size, height, leaf_size, sample_width,
            extra_data_size);
        gst_caps_set_simple (caps, "flavor", G_TYPE_INT, flavor, "packet_size",
            G_TYPE_INT, packet_size, "height", G_TYPE_INT, height, "leaf_size",
            G_TYPE_INT, leaf_size, "width", G_TYPE_INT, sample_width, NULL);

        if ((size - 78) >= extra_data_size) {
          priv = gst_buffer_new_and_alloc (extra_data_size);
          memcpy (GST_BUFFER_DATA (priv), data + 78, extra_data_size);
          gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
          gst_buffer_unref (priv);
        }
      }
    }

    *codec_name = g_strdup_printf ("RealAudio %d.0", raversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_SIPR)) {
    caps = gst_caps_new_simple ("audio/x-sipro", NULL);
    *codec_name = g_strdup ("Sipro/ACELP.NET Voice Codec");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_RALF)) {
    caps = gst_caps_new_simple ("audio/x-ralf-mpeg4-generic", NULL);
    *codec_name = g_strdup ("Real Audio Lossless");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_ATRC)) {
    caps = gst_caps_new_simple ("audio/x-vnd.sony.atrac3", NULL);
    *codec_name = g_strdup ("Sony ATRAC3");
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps", codec_id);
    return NULL;
  }

  if (caps != NULL) {
    if (audiocontext->samplerate > 0 && audiocontext->channels > 0) {
      gint i;

      for (i = 0; i < gst_caps_get_size (caps); i++) {
        gst_structure_set (gst_caps_get_structure (caps, i),
            "channels", G_TYPE_INT, audiocontext->channels,
            "rate", G_TYPE_INT, audiocontext->samplerate, NULL);
      }
    }

    gst_caps_do_simplify (caps);
  }

  return caps;
}

static GstCaps *
gst_matroska_demux_subtitle_caps (GstMatroskaTrackSubtitleContext *
    subtitlecontext, const gchar * codec_id, gpointer data, guint size)
{
  GstCaps *caps = NULL;
  GstMatroskaTrackContext *context =
      (GstMatroskaTrackContext *) subtitlecontext;

  /* for backwards compatibility */
  if (!g_ascii_strcasecmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_ASCII))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8;
  else if (!g_ascii_strcasecmp (codec_id, "S_SSA"))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_SSA;
  else if (!g_ascii_strcasecmp (codec_id, "S_ASS"))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_ASS;
  else if (!g_ascii_strcasecmp (codec_id, "S_USF"))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_USF;

  /* TODO: Add GST_MATROSKA_CODEC_ID_SUBTITLE_BMP support
   * Check if we have to do something with codec_private */
  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8)) {
    caps = gst_caps_new_simple ("text/plain", NULL);
    context->postprocess_frame = gst_matroska_demux_check_subtitle_buffer;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_SSA)) {
    caps = gst_caps_new_simple ("application/x-ssa", NULL);
    context->postprocess_frame = gst_matroska_demux_check_subtitle_buffer;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_ASS)) {
    caps = gst_caps_new_simple ("application/x-ass", NULL);
    context->postprocess_frame = gst_matroska_demux_check_subtitle_buffer;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_USF)) {
    caps = gst_caps_new_simple ("application/x-usf", NULL);
    context->postprocess_frame = gst_matroska_demux_check_subtitle_buffer;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_VOBSUB)) {
    caps = gst_caps_new_simple ("video/x-dvd-subpicture", NULL);
    ((GstMatroskaTrackContext *) subtitlecontext)->send_dvd_event = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_HDMVPGS)) {
    caps = gst_caps_new_simple ("subpicture/x-pgs", NULL);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_KATE)) {
    caps = gst_caps_new_simple ("subtitle/x-kate", NULL);
    context->send_xiph_headers = TRUE;
  } else {
    GST_DEBUG ("Unknown subtitle stream: codec_id='%s'", codec_id);
    caps = gst_caps_new_simple ("application/x-subtitle-unknown", NULL);
  }

  if (data != NULL && size > 0) {
    GstBuffer *buf;

    buf = gst_buffer_new_and_alloc (size);
    memcpy (GST_BUFFER_DATA (buf), data, size);
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, buf, NULL);
    gst_buffer_unref (buf);
  }

  return caps;
}

static void
gst_matroska_demux_set_index (GstElement * element, GstIndex * index)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);

  GST_OBJECT_LOCK (demux);
  if (demux->element_index)
    gst_object_unref (demux->element_index);
  demux->element_index = index ? gst_object_ref (index) : NULL;
  GST_OBJECT_UNLOCK (demux);
  GST_DEBUG_OBJECT (demux, "Set index %" GST_PTR_FORMAT, demux->element_index);
}

static GstIndex *
gst_matroska_demux_get_index (GstElement * element)
{
  GstIndex *result = NULL;
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);

  GST_OBJECT_LOCK (demux);
  if (demux->element_index)
    result = gst_object_ref (demux->element_index);
  GST_OBJECT_UNLOCK (demux);

  GST_DEBUG_OBJECT (demux, "Returning index %" GST_PTR_FORMAT, result);

  return result;
}

static GstStateChangeReturn
gst_matroska_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  /* handle upwards state changes here */
  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /* handle downwards state changes */
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_matroska_demux_reset (GST_ELEMENT (demux));
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_matroska_demux_plugin_init (GstPlugin * plugin)
{
  gst_riff_init ();

  /* create an elementfactory for the matroska_demux element */
  if (!gst_element_register (plugin, "matroskademux",
          GST_RANK_PRIMARY, GST_TYPE_MATROSKA_DEMUX))
    return FALSE;

  return TRUE;
}
