/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Tim-Philipp Müller <tim centricular net>
 * (c) 2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * matroska-parse.c: matroska file/stream parser
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
 * TODO: check if parseing is done correct for all codecs according to spec
 * TODO: seeking with incomplete or without CUE
 */

/**
 * SECTION:element-matroskaparse
 *
 * matroskaparse parsees a Matroska file into the different contained streams.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=/path/to/mkv ! matroskaparse ! vorbisdec ! audioconvert ! audioresample ! autoaudiosink
 * ]| This pipeline parsees a Matroska file and outputs the contained Vorbis audio.
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>
#include <glib/gprintf.h>

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

#include <gst/pbutils/pbutils.h>

#include "lzo.h"

#include "matroska-parse.h"
#include "matroska-ids.h"

GST_DEBUG_CATEGORY_STATIC (matroskaparse_debug);
#define GST_CAT_DEFAULT matroskaparse_debug

#define DEBUG_ELEMENT_START(parse, ebml, element) \
    GST_DEBUG_OBJECT (parse, "Parsing " element " element at offset %" \
        G_GUINT64_FORMAT, gst_ebml_read_get_pos (ebml))

#define DEBUG_ELEMENT_STOP(parse, ebml, element, ret) \
    GST_DEBUG_OBJECT (parse, "Parsing " element " element " \
        " finished with '%s'", gst_flow_get_name (ret))

enum
{
  ARG_0,
  ARG_METADATA,
  ARG_STREAMINFO
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska; video/webm")
    );

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska; video/webm")
    );

static GstFlowReturn gst_matroska_parse_parse_id (GstMatroskaParse * parse,
    guint32 id, guint64 length, guint needed);

/* element functions */
//static void gst_matroska_parse_loop (GstPad * pad);

static gboolean gst_matroska_parse_element_send_event (GstElement * element,
    GstEvent * event);
static gboolean gst_matroska_parse_element_query (GstElement * element,
    GstQuery * query);

/* pad functions */
static gboolean gst_matroska_parse_handle_seek_event (GstMatroskaParse * parse,
    GstPad * pad, GstEvent * event);
static gboolean gst_matroska_parse_handle_src_event (GstPad * pad,
    GstEvent * event);
static const GstQueryType *gst_matroska_parse_get_src_query_types (GstPad *
    pad);
static gboolean gst_matroska_parse_handle_src_query (GstPad * pad,
    GstQuery * query);

static gboolean gst_matroska_parse_handle_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_matroska_parse_chain (GstPad * pad,
    GstBuffer * buffer);

static GstStateChangeReturn
gst_matroska_parse_change_state (GstElement * element,
    GstStateChange transition);
static void
gst_matroska_parse_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_matroska_parse_get_index (GstElement * element);

/* stream methods */
static void gst_matroska_parse_reset (GstElement * element);
static gboolean perform_seek_to_offset (GstMatroskaParse * parse,
    guint64 offset);

GType gst_matroska_parse_get_type (void);
GST_BOILERPLATE (GstMatroskaParse, gst_matroska_parse, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_matroska_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));

  gst_element_class_set_details_simple (element_class, "Matroska parser",
      "Codec/Parser",
      "Parses Matroska/WebM streams into video/audio/subtitles",
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");
}

static void
gst_matroska_parse_finalize (GObject * object)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (object);

  if (parse->src) {
    g_ptr_array_free (parse->src, TRUE);
    parse->src = NULL;
  }

  if (parse->global_tags) {
    gst_tag_list_free (parse->global_tags);
    parse->global_tags = NULL;
  }

  g_object_unref (parse->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_matroska_parse_class_init (GstMatroskaParseClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (matroskaparse_debug, "matroskaparse", 0,
      "Matroska parser");

  gobject_class->finalize = gst_matroska_parse_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_matroska_parse_change_state);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_matroska_parse_element_send_event);
  gstelement_class->query =
      GST_DEBUG_FUNCPTR (gst_matroska_parse_element_query);

  gstelement_class->set_index =
      GST_DEBUG_FUNCPTR (gst_matroska_parse_set_index);
  gstelement_class->get_index =
      GST_DEBUG_FUNCPTR (gst_matroska_parse_get_index);
}

static void
gst_matroska_parse_init (GstMatroskaParse * parse,
    GstMatroskaParseClass * klass)
{
  parse->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_chain_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_parse_chain));
  gst_pad_set_event_function (parse->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_parse_handle_sink_event));
  gst_element_add_pad (GST_ELEMENT (parse), parse->sinkpad);

  parse->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_matroska_parse_handle_src_event));
  gst_pad_set_query_type_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_matroska_parse_get_src_query_types));
  gst_pad_set_query_function (parse->srcpad,
      GST_DEBUG_FUNCPTR (gst_matroska_parse_handle_src_query));
  gst_pad_use_fixed_caps (parse->srcpad);

  gst_element_add_pad (GST_ELEMENT (parse), parse->srcpad);

  /* initial stream no. */
  parse->src = NULL;

  parse->writing_app = NULL;
  parse->muxing_app = NULL;
  parse->index = NULL;
  parse->global_tags = NULL;

  parse->adapter = gst_adapter_new ();

  /* finish off */
  gst_matroska_parse_reset (GST_ELEMENT (parse));
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

static void
gst_matroska_parse_free_parsed_el (gpointer mem, gpointer user_data)
{
  g_slice_free (guint64, mem);
}

static void
gst_matroska_parse_reset (GstElement * element)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (element);
  guint i;

  GST_DEBUG_OBJECT (parse, "Resetting state");

  /* reset input */
  parse->state = GST_MATROSKA_PARSE_STATE_START;

  /* clean up existing streams */
  if (parse->src) {
    g_assert (parse->src->len == parse->num_streams);
    for (i = 0; i < parse->src->len; i++) {
      GstMatroskaTrackContext *context = g_ptr_array_index (parse->src, i);

      gst_caps_replace (&context->caps, NULL);
      gst_matroska_track_free (context);
    }
    g_ptr_array_free (parse->src, TRUE);
  }
  parse->src = g_ptr_array_new ();

  parse->num_streams = 0;
  parse->num_a_streams = 0;
  parse->num_t_streams = 0;
  parse->num_v_streams = 0;

  /* reset media info */
  g_free (parse->writing_app);
  parse->writing_app = NULL;
  g_free (parse->muxing_app);
  parse->muxing_app = NULL;

  /* reset indexes */
  if (parse->index) {
    g_array_free (parse->index, TRUE);
    parse->index = NULL;
  }

  /* reset timers */
  parse->clock = NULL;
  parse->time_scale = 1000000;
  parse->created = G_MININT64;

  parse->index_parsed = FALSE;
  parse->tracks_parsed = FALSE;
  parse->segmentinfo_parsed = FALSE;
  parse->attachments_parsed = FALSE;

  g_list_foreach (parse->tags_parsed,
      (GFunc) gst_matroska_parse_free_parsed_el, NULL);
  g_list_free (parse->tags_parsed);
  parse->tags_parsed = NULL;

  g_list_foreach (parse->seek_parsed,
      (GFunc) gst_matroska_parse_free_parsed_el, NULL);
  g_list_free (parse->seek_parsed);
  parse->seek_parsed = NULL;

  gst_segment_init (&parse->segment, GST_FORMAT_TIME);
  parse->last_stop_end = GST_CLOCK_TIME_NONE;
  parse->seek_block = 0;

  parse->offset = 0;
  parse->cluster_time = GST_CLOCK_TIME_NONE;
  parse->cluster_offset = 0;
  parse->next_cluster_offset = 0;
  parse->index_offset = 0;
  parse->seekable = FALSE;
  parse->need_newsegment = FALSE;
  parse->building_index = FALSE;
  if (parse->seek_event) {
    gst_event_unref (parse->seek_event);
    parse->seek_event = NULL;
  }

  parse->seek_index = NULL;
  parse->seek_entry = 0;

  if (parse->close_segment) {
    gst_event_unref (parse->close_segment);
    parse->close_segment = NULL;
  }

  if (parse->new_segment) {
    gst_event_unref (parse->new_segment);
    parse->new_segment = NULL;
  }

  if (parse->element_index) {
    gst_object_unref (parse->element_index);
    parse->element_index = NULL;
  }
  parse->element_index_writer_id = -1;

  if (parse->global_tags) {
    gst_tag_list_free (parse->global_tags);
  }
  parse->global_tags = gst_tag_list_new ();

  if (parse->cached_buffer) {
    gst_buffer_unref (parse->cached_buffer);
    parse->cached_buffer = NULL;
  }
}

/*
 * Calls pull_range for (offset,size) without advancing our offset
 */
static GstFlowReturn
gst_matroska_parse_peek_bytes (GstMatroskaParse * parse, guint64 offset,
    guint size, GstBuffer ** p_buf, guint8 ** bytes)
{
  GstFlowReturn ret;

  /* Caching here actually makes much less difference than one would expect.
   * We do it mainly to avoid pulling buffers of 1 byte all the time */
  if (parse->cached_buffer) {
    guint64 cache_offset = GST_BUFFER_OFFSET (parse->cached_buffer);
    guint cache_size = GST_BUFFER_SIZE (parse->cached_buffer);

    if (cache_offset <= parse->offset &&
        (parse->offset + size) <= (cache_offset + cache_size)) {
      if (p_buf)
        *p_buf = gst_buffer_create_sub (parse->cached_buffer,
            parse->offset - cache_offset, size);
      if (bytes)
        *bytes = GST_BUFFER_DATA (parse->cached_buffer) + parse->offset -
            cache_offset;
      return GST_FLOW_OK;
    }
    /* not enough data in the cache, free cache and get a new one */
    gst_buffer_unref (parse->cached_buffer);
    parse->cached_buffer = NULL;
  }

  /* refill the cache */
  ret = gst_pad_pull_range (parse->sinkpad, parse->offset,
      MAX (size, 64 * 1024), &parse->cached_buffer);
  if (ret != GST_FLOW_OK) {
    parse->cached_buffer = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (parse->cached_buffer) >= size) {
    if (p_buf)
      *p_buf = gst_buffer_create_sub (parse->cached_buffer, 0, size);
    if (bytes)
      *bytes = GST_BUFFER_DATA (parse->cached_buffer);
    return GST_FLOW_OK;
  }

  /* Not possible to get enough data, try a last time with
   * requesting exactly the size we need */
  gst_buffer_unref (parse->cached_buffer);
  parse->cached_buffer = NULL;

  ret =
      gst_pad_pull_range (parse->sinkpad, parse->offset, size,
      &parse->cached_buffer);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (parse, "pull_range returned %d", ret);
    if (p_buf)
      *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return ret;
  }

  if (GST_BUFFER_SIZE (parse->cached_buffer) < size) {
    GST_WARNING_OBJECT (parse, "Dropping short buffer at offset %"
        G_GUINT64_FORMAT ": wanted %u bytes, got %u bytes", parse->offset,
        size, GST_BUFFER_SIZE (parse->cached_buffer));

    gst_buffer_unref (parse->cached_buffer);
    parse->cached_buffer = NULL;
    if (p_buf)
      *p_buf = NULL;
    if (bytes)
      *bytes = NULL;
    return GST_FLOW_UNEXPECTED;
  }

  if (p_buf)
    *p_buf = gst_buffer_create_sub (parse->cached_buffer, 0, size);
  if (bytes)
    *bytes = GST_BUFFER_DATA (parse->cached_buffer);

  return GST_FLOW_OK;
}

static const guint8 *
gst_matroska_parse_peek_pull (GstMatroskaParse * parse, guint peek)
{
  guint8 *data = NULL;

  gst_matroska_parse_peek_bytes (parse, parse->offset, peek, NULL, &data);
  return data;
}

static GstFlowReturn
gst_matroska_parse_peek_id_length_pull (GstMatroskaParse * parse, guint32 * _id,
    guint64 * _length, guint * _needed)
{
  return gst_ebml_peek_id_length (_id, _length, _needed,
      (GstPeekData) gst_matroska_parse_peek_pull, (gpointer) parse,
      GST_ELEMENT_CAST (parse), parse->offset);
}

static gint64
gst_matroska_parse_get_length (GstMatroskaParse * parse)
{
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 end = -1;

  if (!gst_pad_query_peer_duration (parse->sinkpad, &fmt, &end) ||
      fmt != GST_FORMAT_BYTES || end < 0)
    GST_DEBUG_OBJECT (parse, "no upstream length");

  return end;
}

static gint
gst_matroska_parse_stream_from_num (GstMatroskaParse * parse, guint track_num)
{
  guint n;

  g_assert (parse->src->len == parse->num_streams);
  for (n = 0; n < parse->src->len; n++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (parse->src, n);

    if (context->num == track_num) {
      return n;
    }
  }

  if (n == parse->num_streams)
    GST_WARNING_OBJECT (parse,
        "Failed to find corresponding pad for tracknum %d", track_num);

  return -1;
}

static gint
gst_matroska_parse_encoding_cmp (GstMatroskaTrackEncoding * a,
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
gst_matroska_parse_encoding_order_unique (GArray * encodings, guint64 order)
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
gst_matroska_parse_read_track_encoding (GstMatroskaParse * parse,
    GstEbmlRead * ebml, GstMatroskaTrackContext * context)
{
  GstMatroskaTrackEncoding enc = { 0, };
  GstFlowReturn ret;
  guint32 id;

  DEBUG_ELEMENT_START (parse, ebml, "ContentEncoding");
  /* Set default values */
  enc.scope = 1;
  /* All other default values are 0 */

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "ContentEncoding", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_CONTENTENCODINGORDER:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (!gst_matroska_parse_encoding_order_unique (context->encodings, num)) {
          GST_ERROR_OBJECT (parse, "ContentEncodingOrder %" G_GUINT64_FORMAT
              "is not unique for track %d", num, context->num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (parse, "ContentEncodingOrder: %" G_GUINT64_FORMAT,
            num);
        enc.order = num;
        break;
      }
      case GST_MATROSKA_ID_CONTENTENCODINGSCOPE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > 7 && num == 0) {
          GST_ERROR_OBJECT (parse, "Invalid ContentEncodingScope %"
              G_GUINT64_FORMAT, num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (parse, "ContentEncodingScope: %" G_GUINT64_FORMAT,
            num);
        enc.scope = num;

        break;
      }
      case GST_MATROSKA_ID_CONTENTENCODINGTYPE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num > 1) {
          GST_ERROR_OBJECT (parse, "Invalid ContentEncodingType %"
              G_GUINT64_FORMAT, num);
          ret = GST_FLOW_ERROR;
          break;
        } else if (num != 0) {
          GST_ERROR_OBJECT (parse, "Encrypted tracks are not supported yet");
          ret = GST_FLOW_ERROR;
          break;
        }
        GST_DEBUG_OBJECT (parse, "ContentEncodingType: %" G_GUINT64_FORMAT,
            num);
        enc.type = num;
        break;
      }
      case GST_MATROSKA_ID_CONTENTCOMPRESSION:{

        DEBUG_ELEMENT_START (parse, ebml, "ContentCompression");

        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          break;

        while (ret == GST_FLOW_OK &&
            gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
          if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
            break;

          switch (id) {
            case GST_MATROSKA_ID_CONTENTCOMPALGO:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }
              if (num > 3) {
                GST_ERROR_OBJECT (parse, "Invalid ContentCompAlgo %"
                    G_GUINT64_FORMAT, num);
                ret = GST_FLOW_ERROR;
                break;
              }
              GST_DEBUG_OBJECT (parse, "ContentCompAlgo: %" G_GUINT64_FORMAT,
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
              GST_DEBUG_OBJECT (parse,
                  "ContentCompSettings of size %" G_GUINT64_FORMAT, size);
              break;
            }
            default:
              GST_WARNING_OBJECT (parse,
                  "Unknown ContentCompression subelement 0x%x - ignoring", id);
              ret = gst_ebml_read_skip (ebml);
              break;
          }
        }
        DEBUG_ELEMENT_STOP (parse, ebml, "ContentCompression", ret);
        break;
      }

      case GST_MATROSKA_ID_CONTENTENCRYPTION:
        GST_ERROR_OBJECT (parse, "Encrypted tracks not yet supported");
        gst_ebml_read_skip (ebml);
        ret = GST_FLOW_ERROR;
        break;
      default:
        GST_WARNING_OBJECT (parse,
            "Unknown ContentEncoding subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "ContentEncoding", ret);
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
    GST_ERROR ("invalid compression algorithm %d", algo);
    ret = FALSE;
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

static GstFlowReturn
gst_matroska_decode_content_encodings (GArray * encodings)
{
  gint i;

  if (encodings == NULL)
    return GST_FLOW_OK;

  for (i = 0; i < encodings->len; i++) {
    GstMatroskaTrackEncoding *enc =
        &g_array_index (encodings, GstMatroskaTrackEncoding, i);
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
gst_matroska_parse_read_track_encodings (GstMatroskaParse * parse,
    GstEbmlRead * ebml, GstMatroskaTrackContext * context)
{
  GstFlowReturn ret;
  guint32 id;

  DEBUG_ELEMENT_START (parse, ebml, "ContentEncodings");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "ContentEncodings", ret);
    return ret;
  }

  context->encodings =
      g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaTrackEncoding), 1);

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_CONTENTENCODING:
        ret = gst_matroska_parse_read_track_encoding (parse, ebml, context);
        break;
      default:
        GST_WARNING_OBJECT (parse,
            "Unknown ContentEncodings subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "ContentEncodings", ret);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED)
    return ret;

  /* Sort encodings according to their order */
  g_array_sort (context->encodings,
      (GCompareFunc) gst_matroska_parse_encoding_cmp);

  return gst_matroska_decode_content_encodings (context->encodings);
}

static gboolean
gst_matroska_parse_tracknumber_unique (GstMatroskaParse * parse, guint64 num)
{
  gint i;

  g_assert (parse->src->len == parse->num_streams);
  for (i = 0; i < parse->src->len; i++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (parse->src, i);

    if (context->num == num)
      return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_matroska_parse_add_stream (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  GstMatroskaTrackContext *context;
  GstFlowReturn ret;
  guint32 id;

  DEBUG_ELEMENT_START (parse, ebml, "TrackEntry");

  /* start with the master */
  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "TrackEntry", ret);
    return ret;
  }

  /* allocate generic... if we know the type, we'll g_renew()
   * with the precise type */
  context = g_new0 (GstMatroskaTrackContext, 1);
  g_ptr_array_add (parse->src, context);
  context->index = parse->num_streams;
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
  context->to_offset = G_MAXINT64;
  parse->num_streams++;
  g_assert (parse->src->len == parse->num_streams);

  GST_DEBUG_OBJECT (parse, "Stream number %d", context->index);

  /* try reading the trackentry headers */
  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* track number (unique stream ID) */
      case GST_MATROSKA_ID_TRACKNUMBER:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          GST_ERROR_OBJECT (parse, "Invalid TrackNumber 0");
          ret = GST_FLOW_ERROR;
          break;
        } else if (!gst_matroska_parse_tracknumber_unique (parse, num)) {
          GST_ERROR_OBJECT (parse, "TrackNumber %" G_GUINT64_FORMAT
              " is not unique", num);
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (parse, "TrackNumber: %" G_GUINT64_FORMAT, num);
        context->num = num;
        break;
      }
        /* track UID (unique identifier) */
      case GST_MATROSKA_ID_TRACKUID:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          GST_ERROR_OBJECT (parse, "Invalid TrackUID 0");
          ret = GST_FLOW_ERROR;
          break;
        }

        GST_DEBUG_OBJECT (parse, "TrackUID: %" G_GUINT64_FORMAT, num);
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
          GST_WARNING_OBJECT (parse,
              "More than one tracktype defined in a TrackEntry - skipping");
          break;
        } else if (track_type < 1 || track_type > 254) {
          GST_WARNING_OBJECT (parse, "Invalid TrackType %" G_GUINT64_FORMAT,
              track_type);
          break;
        }

        GST_DEBUG_OBJECT (parse, "TrackType: %" G_GUINT64_FORMAT, track_type);

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
            GST_WARNING_OBJECT (parse,
                "Unknown or unsupported TrackType %" G_GUINT64_FORMAT,
                track_type);
            context->type = 0;
            break;
        }
        g_ptr_array_index (parse->src, parse->num_streams - 1) = context;
        break;
      }

        /* tracktype specific stuff for video */
      case GST_MATROSKA_ID_TRACKVIDEO:{
        GstMatroskaTrackVideoContext *videocontext;

        DEBUG_ELEMENT_START (parse, ebml, "TrackVideo");

        if (!gst_matroska_track_init_video_context (&context)) {
          GST_WARNING_OBJECT (parse,
              "TrackVideo element in non-video track - ignoring track");
          ret = GST_FLOW_ERROR;
          break;
        } else if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
          break;
        }
        videocontext = (GstMatroskaTrackVideoContext *) context;
        g_ptr_array_index (parse->src, parse->num_streams - 1) = context;

        while (ret == GST_FLOW_OK &&
            gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
          if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
            break;

          switch (id) {
              /* Should be one level up but some broken muxers write it here. */
            case GST_MATROSKA_ID_TRACKDEFAULTDURATION:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (parse, "Invalid TrackDefaultDuration 0");
                break;
              }

              GST_DEBUG_OBJECT (parse,
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
                GST_WARNING_OBJECT (parse, "Invalid TrackVideoFPS %lf", num);
                break;
              }

              GST_DEBUG_OBJECT (parse, "TrackVideoFrameRate: %lf", num);
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
                GST_WARNING_OBJECT (parse, "Invalid TrackVideoDisplayWidth 0");
                break;
              }

              GST_DEBUG_OBJECT (parse,
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
                GST_WARNING_OBJECT (parse, "Invalid TrackVideoDisplayHeight 0");
                break;
              }

              GST_DEBUG_OBJECT (parse,
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
                GST_WARNING_OBJECT (parse, "Invalid TrackVideoPixelWidth 0");
                break;
              }

              GST_DEBUG_OBJECT (parse,
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
                GST_WARNING_OBJECT (parse, "Invalid TrackVideoPixelHeight 0");
                break;
              }

              GST_DEBUG_OBJECT (parse,
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
              GST_DEBUG_OBJECT (parse, "TrackVideoInterlaced: %d",
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
                GST_WARNING_OBJECT (parse,
                    "Unknown TrackVideoAspectRatioType 0x%x", (guint) num);
                break;
              }
              GST_DEBUG_OBJECT (parse,
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
                GST_WARNING_OBJECT (parse,
                    "Invalid TrackVideoColourSpace length %" G_GUINT64_FORMAT,
                    datalen);
                break;
              }

              memcpy (&videocontext->fourcc, data, 4);
              GST_DEBUG_OBJECT (parse,
                  "TrackVideoColourSpace: %" GST_FOURCC_FORMAT,
                  GST_FOURCC_ARGS (videocontext->fourcc));
              g_free (data);
              break;
            }

            default:
              GST_WARNING_OBJECT (parse,
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
        }

        DEBUG_ELEMENT_STOP (parse, ebml, "TrackVideo", ret);
        break;
      }

        /* tracktype specific stuff for audio */
      case GST_MATROSKA_ID_TRACKAUDIO:{
        GstMatroskaTrackAudioContext *audiocontext;

        DEBUG_ELEMENT_START (parse, ebml, "TrackAudio");

        if (!gst_matroska_track_init_audio_context (&context)) {
          GST_WARNING_OBJECT (parse,
              "TrackAudio element in non-audio track - ignoring track");
          ret = GST_FLOW_ERROR;
          break;
        }

        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          break;

        audiocontext = (GstMatroskaTrackAudioContext *) context;
        g_ptr_array_index (parse->src, parse->num_streams - 1) = context;

        while (ret == GST_FLOW_OK &&
            gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
          if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
            break;

          switch (id) {
              /* samplerate */
            case GST_MATROSKA_ID_AUDIOSAMPLINGFREQ:{
              gdouble num;

              if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK)
                break;


              if (num <= 0.0) {
                GST_WARNING_OBJECT (parse,
                    "Invalid TrackAudioSamplingFrequency %lf", num);
                break;
              }

              GST_DEBUG_OBJECT (parse, "TrackAudioSamplingFrequency: %lf", num);
              audiocontext->samplerate = num;
              break;
            }

              /* bitdepth */
            case GST_MATROSKA_ID_AUDIOBITDEPTH:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
                break;

              if (num == 0) {
                GST_WARNING_OBJECT (parse, "Invalid TrackAudioBitDepth 0");
                break;
              }

              GST_DEBUG_OBJECT (parse, "TrackAudioBitDepth: %" G_GUINT64_FORMAT,
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
                GST_WARNING_OBJECT (parse, "Invalid TrackAudioChannels 0");
                break;
              }

              GST_DEBUG_OBJECT (parse, "TrackAudioChannels: %" G_GUINT64_FORMAT,
                  num);
              audiocontext->channels = num;
              break;
            }

            default:
              GST_WARNING_OBJECT (parse,
                  "Unknown TrackAudio subelement 0x%x - ignoring", id);
              /* fall through */
            case GST_MATROSKA_ID_AUDIOCHANNELPOSITIONS:
            case GST_MATROSKA_ID_AUDIOOUTPUTSAMPLINGFREQ:
              ret = gst_ebml_read_skip (ebml);
              break;
          }
        }

        DEBUG_ELEMENT_STOP (parse, ebml, "TrackAudio", ret);

        break;
      }

        /* codec identifier */
      case GST_MATROSKA_ID_CODECID:{
        gchar *text;

        if ((ret = gst_ebml_read_ascii (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "CodecID: %s", GST_STR_NULL (text));
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

        GST_DEBUG_OBJECT (parse, "CodecPrivate of size %" G_GUINT64_FORMAT,
            size);
        break;
      }

        /* name of the codec */
      case GST_MATROSKA_ID_CODECNAME:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "CodecName: %s", GST_STR_NULL (text));
        context->codec_name = text;
        break;
      }

        /* name of this track */
      case GST_MATROSKA_ID_TRACKNAME:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        context->name = text;
        GST_DEBUG_OBJECT (parse, "TrackName: %s", GST_STR_NULL (text));
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

        GST_DEBUG_OBJECT (parse, "TrackLanguage: %s",
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

        GST_DEBUG_OBJECT (parse, "TrackEnabled: %d",
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

        GST_DEBUG_OBJECT (parse, "TrackDefault: %d",
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

        GST_DEBUG_OBJECT (parse, "TrackForced: %d",
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

        GST_DEBUG_OBJECT (parse, "TrackLacing: %d",
            (context->flags & GST_MATROSKA_TRACK_ENABLED) ? 1 : 0);
        break;
      }

        /* default length (in time) of one data block in this track */
      case GST_MATROSKA_ID_TRACKDEFAULTDURATION:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;


        if (num == 0) {
          GST_WARNING_OBJECT (parse, "Invalid TrackDefaultDuration 0");
          break;
        }

        GST_DEBUG_OBJECT (parse, "TrackDefaultDuration: %" G_GUINT64_FORMAT,
            num);
        context->default_duration = num;
        break;
      }

      case GST_MATROSKA_ID_CONTENTENCODINGS:{
        ret = gst_matroska_parse_read_track_encodings (parse, ebml, context);
        break;
      }

      case GST_MATROSKA_ID_TRACKTIMECODESCALE:{
        gdouble num;

        if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num <= 0.0) {
          GST_WARNING_OBJECT (parse, "Invalid TrackTimeCodeScale %lf", num);
          break;
        }

        GST_DEBUG_OBJECT (parse, "TrackTimeCodeScale: %lf", num);
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
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "TrackEntry", ret);

  /* Decode codec private data if necessary */
  if (context->encodings && context->encodings->len > 0 && context->codec_priv
      && context->codec_priv_size > 0) {
    if (!gst_matroska_decode_data (context->encodings,
            &context->codec_priv, &context->codec_priv_size,
            GST_MATROSKA_TRACK_ENCODING_SCOPE_CODEC_DATA, TRUE)) {
      GST_WARNING_OBJECT (parse, "Decoding codec private data failed");
      ret = GST_FLOW_ERROR;
    }
  }

  if (context->type == 0 || context->codec_id == NULL || (ret != GST_FLOW_OK
          && ret != GST_FLOW_UNEXPECTED)) {
    if (ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED)
      GST_WARNING_OBJECT (ebml, "Unknown stream/codec in track entry header");

    parse->num_streams--;
    g_ptr_array_remove_index (parse->src, parse->num_streams);
    g_assert (parse->src->len == parse->num_streams);
    if (context) {
      gst_matroska_track_free (context);
    }

    return ret;
  }

  if ((context->language == NULL || *context->language == '\0') &&
      (context->type == GST_MATROSKA_TRACK_TYPE_AUDIO ||
          context->type == GST_MATROSKA_TRACK_TYPE_SUBTITLE)) {
    GST_LOG ("stream %d: language=eng (assuming default)", context->index);
    context->language = g_strdup ("eng");
  }


  /* tadaah! */
  return ret;
}

static const GstQueryType *
gst_matroska_parse_get_src_query_types (GstPad * pad)
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
gst_matroska_parse_query (GstMatroskaParse * parse, GstPad * pad,
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
        GST_OBJECT_LOCK (parse);
        if (context)
          gst_query_set_position (query, GST_FORMAT_TIME, context->pos);
        else
          gst_query_set_position (query, GST_FORMAT_TIME,
              parse->segment.last_stop);
        GST_OBJECT_UNLOCK (parse);
      } else if (format == GST_FORMAT_DEFAULT && context
          && context->default_duration) {
        GST_OBJECT_LOCK (parse);
        gst_query_set_position (query, GST_FORMAT_DEFAULT,
            context->pos / context->default_duration);
        GST_OBJECT_UNLOCK (parse);
      } else {
        GST_DEBUG_OBJECT (parse,
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
        GST_OBJECT_LOCK (parse);
        gst_query_set_duration (query, GST_FORMAT_TIME,
            parse->segment.duration);
        GST_OBJECT_UNLOCK (parse);
      } else if (format == GST_FORMAT_DEFAULT && context
          && context->default_duration) {
        GST_OBJECT_LOCK (parse);
        gst_query_set_duration (query, GST_FORMAT_DEFAULT,
            parse->segment.duration / context->default_duration);
        GST_OBJECT_UNLOCK (parse);
      } else {
        GST_DEBUG_OBJECT (parse,
            "only duration query in TIME and DEFAULT format is supported");
      }

      res = TRUE;
      break;
    }

    case GST_QUERY_SEEKING:
    {
      GstFormat fmt;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_TIME) {
        gboolean seekable;

        /* assuming we'll be able to get an index ... */
        seekable = parse->seekable;

        gst_query_set_seeking (query, GST_FORMAT_TIME, seekable,
            0, parse->segment.duration);
        res = TRUE;
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
gst_matroska_parse_element_query (GstElement * element, GstQuery * query)
{
  return gst_matroska_parse_query (GST_MATROSKA_PARSE (element), NULL, query);
}

static gboolean
gst_matroska_parse_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean ret;
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (gst_pad_get_parent (pad));

  ret = gst_matroska_parse_query (parse, pad, query);

  gst_object_unref (parse);

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
gst_matroskaparse_do_index_seek (GstMatroskaParse * parse,
    GstMatroskaTrackContext * track, gint64 seek_pos, GArray ** _index,
    gint * _entry_index)
{
  GstMatroskaIndex *entry = NULL;
  GArray *index;

  if (!parse->index || !parse->index->len)
    return NULL;

  /* find entry just before or at the requested position */
  if (track && track->index_table)
    index = track->index_table;
  else
    index = parse->index;

  entry =
      gst_util_array_binary_search (index->data, index->len,
      sizeof (GstMatroskaIndex),
      (GCompareDataFunc) gst_matroska_index_seek_find, GST_SEARCH_MODE_BEFORE,
      &seek_pos, NULL);

  if (entry == NULL)
    entry = &g_array_index (index, GstMatroskaIndex, 0);

  if (_index)
    *_index = index;
  if (_entry_index)
    *_entry_index = entry - (GstMatroskaIndex *) index->data;

  return entry;
}

/* takes ownership of taglist */
static void
gst_matroska_parse_found_global_tag (GstMatroskaParse * parse,
    GstTagList * taglist)
{
  if (parse->global_tags) {
    /* nothing sent yet, add to cache */
    gst_tag_list_insert (parse->global_tags, taglist, GST_TAG_MERGE_APPEND);
    gst_tag_list_free (taglist);
  } else {
    /* hm, already sent, no need to cache and wait anymore */
    GST_DEBUG_OBJECT (parse, "Sending late global tags %" GST_PTR_FORMAT,
        taglist);
    gst_element_found_tags (GST_ELEMENT (parse), taglist);
  }
}

/* returns FALSE if there are no pads to deliver event to,
 * otherwise TRUE (whatever the outcome of event sending),
 * takes ownership of the passed event! */
static gboolean
gst_matroska_parse_send_event (GstMatroskaParse * parse, GstEvent * event)
{
  gboolean ret = FALSE;

  g_return_val_if_fail (event != NULL, FALSE);

  GST_DEBUG_OBJECT (parse, "Sending event of type %s to all source pads",
      GST_EVENT_TYPE_NAME (event));

  gst_pad_push_event (parse->srcpad, event);

  return ret;
}

static gboolean
gst_matroska_parse_element_send_event (GstElement * element, GstEvent * event)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (element);
  gboolean res;

  g_return_val_if_fail (event != NULL, FALSE);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
    res = gst_matroska_parse_handle_seek_event (parse, NULL, event);
  } else {
    GST_WARNING_OBJECT (parse, "Unhandled event of type %s",
        GST_EVENT_TYPE_NAME (event));
    res = FALSE;
  }
  gst_event_unref (event);
  return res;
}

/* determine track to seek in */
static GstMatroskaTrackContext *
gst_matroska_parse_get_seek_track (GstMatroskaParse * parse,
    GstMatroskaTrackContext * track)
{
  gint i;

  if (track && track->type == GST_MATROSKA_TRACK_TYPE_VIDEO)
    return track;

  for (i = 0; i < parse->src->len; i++) {
    GstMatroskaTrackContext *stream;

    stream = g_ptr_array_index (parse->src, i);
    if (stream->type == GST_MATROSKA_TRACK_TYPE_VIDEO && stream->index_table)
      track = stream;
  }

  return track;
}

static void
gst_matroska_parse_reset_streams (GstMatroskaParse * parse, GstClockTime time,
    gboolean full)
{
  gint i;

  GST_DEBUG_OBJECT (parse, "resetting stream state");

  g_assert (parse->src->len == parse->num_streams);
  for (i = 0; i < parse->src->len; i++) {
    GstMatroskaTrackContext *context = g_ptr_array_index (parse->src, i);
    context->pos = time;
    context->set_discont = TRUE;
    context->eos = FALSE;
    context->from_time = GST_CLOCK_TIME_NONE;
    if (full)
      context->last_flow = GST_FLOW_OK;
    if (context->type == GST_MATROSKA_TRACK_TYPE_VIDEO) {
      GstMatroskaTrackVideoContext *videocontext =
          (GstMatroskaTrackVideoContext *) context;
      /* parse object lock held by caller */
      videocontext->earliest_time = GST_CLOCK_TIME_NONE;
    }
  }
}

/* searches for a cluster start from @pos,
 * return GST_FLOW_OK and cluster position in @pos if found */
static GstFlowReturn
gst_matroska_parse_search_cluster (GstMatroskaParse * parse, gint64 * pos)
{
  gint64 newpos = *pos;
  gint64 orig_offset;
  GstFlowReturn ret = GST_FLOW_OK;
  const guint chunk = 64 * 1024;
  GstBuffer *buf = NULL;
  guint64 length;
  guint32 id;
  guint needed;

  orig_offset = parse->offset;

  /* read in at newpos and scan for ebml cluster id */
  while (1) {
    GstByteReader reader;
    gint cluster_pos;

    ret = gst_pad_pull_range (parse->sinkpad, newpos, chunk, &buf);
    if (ret != GST_FLOW_OK)
      break;
    GST_DEBUG_OBJECT (parse, "read buffer size %d at offset %" G_GINT64_FORMAT,
        GST_BUFFER_SIZE (buf), newpos);
    gst_byte_reader_init_from_buffer (&reader, buf);
    cluster_pos = 0;
  resume:
    cluster_pos = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff,
        GST_MATROSKA_ID_CLUSTER, cluster_pos,
        GST_BUFFER_SIZE (buf) - cluster_pos);
    if (cluster_pos >= 0) {
      newpos += cluster_pos;
      GST_DEBUG_OBJECT (parse,
          "found cluster ebml id at offset %" G_GINT64_FORMAT, newpos);
      /* extra checks whether we really sync'ed to a cluster:
       * - either it is the first and only cluster
       * - either there is a cluster after this one
       * - either cluster length is undefined
       */
      /* ok if first cluster (there may not a subsequent one) */
      if (newpos == parse->first_cluster_offset) {
        GST_DEBUG_OBJECT (parse, "cluster is first cluster -> OK");
        break;
      }
      parse->offset = newpos;
      ret =
          gst_matroska_parse_peek_id_length_pull (parse, &id, &length, &needed);
      if (ret != GST_FLOW_OK)
        goto resume;
      g_assert (id == GST_MATROSKA_ID_CLUSTER);
      GST_DEBUG_OBJECT (parse, "cluster size %" G_GUINT64_FORMAT ", prefix %d",
          length, needed);
      /* ok if undefined length or first cluster */
      if (length == G_MAXUINT64) {
        GST_DEBUG_OBJECT (parse, "cluster has undefined length -> OK");
        break;
      }
      /* skip cluster */
      parse->offset += length + needed;
      ret =
          gst_matroska_parse_peek_id_length_pull (parse, &id, &length, &needed);
      if (ret != GST_FLOW_OK)
        goto resume;
      GST_DEBUG_OBJECT (parse, "next element is %scluster",
          id == GST_MATROSKA_ID_CLUSTER ? "" : "not ");
      if (id == GST_MATROSKA_ID_CLUSTER)
        break;
      /* not ok, resume */
      goto resume;
    } else {
      /* partial cluster id may have been in tail of buffer */
      newpos += MAX (GST_BUFFER_SIZE (buf), 4) - 3;
      gst_buffer_unref (buf);
      buf = NULL;
    }
  }

  if (buf) {
    gst_buffer_unref (buf);
    buf = NULL;
  }

  parse->offset = orig_offset;
  *pos = newpos;
  return ret;
}


static gboolean
gst_matroska_parse_handle_seek_event (GstMatroskaParse * parse,
    GstPad * pad, GstEvent * event)
{
  GstMatroskaIndex *entry = NULL;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  GstFormat format;
  gdouble rate;
  gint64 cur, stop;
  GstMatroskaTrackContext *track = NULL;
  GstSegment seeksegment = { 0, };
  gboolean update;

  if (pad)
    track = gst_pad_get_element_private (pad);

  track = gst_matroska_parse_get_seek_track (parse, track);

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  /* we can only seek on time */
  if (format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (parse, "Can only seek on TIME");
    return FALSE;
  }

  /* copy segment, we need this because we still need the old
   * segment when we close the current segment. */
  memcpy (&seeksegment, &parse->segment, sizeof (GstSegment));

  if (event) {
    GST_DEBUG_OBJECT (parse, "configuring seek");
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (parse, "New segment %" GST_SEGMENT_FORMAT, &seeksegment);

  /* check sanity before we start flushing and all that */
  GST_OBJECT_LOCK (parse);
  if ((entry = gst_matroskaparse_do_index_seek (parse, track,
              seeksegment.last_stop, &parse->seek_index, &parse->seek_entry)) ==
      NULL) {
    /* pull mode without index can scan later on */
    GST_DEBUG_OBJECT (parse, "No matching seek entry in index");
    GST_OBJECT_UNLOCK (parse);
    return FALSE;
  }
  GST_DEBUG_OBJECT (parse, "Seek position looks sane");
  GST_OBJECT_UNLOCK (parse);

  /* need to seek to cluster start to pick up cluster time */
  /* upstream takes care of flushing and all that
   * ... and newsegment event handling takes care of the rest */
  return perform_seek_to_offset (parse, entry->pos + parse->ebml_segment_start);
}

/*
 * Handle whether we can perform the seek event or if we have to let the chain
 * function handle seeks to build the seek indexes first.
 */
static gboolean
gst_matroska_parse_handle_seek_push (GstMatroskaParse * parse, GstPad * pad,
    GstEvent * event)
{
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  GstFormat format;
  gdouble rate;
  gint64 cur, stop;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  /* sanity checks */

  /* we can only seek on time */
  if (format != GST_FORMAT_TIME) {
    GST_DEBUG_OBJECT (parse, "Can only seek on TIME");
    return FALSE;
  }

  if (stop_type != GST_SEEK_TYPE_NONE && stop != GST_CLOCK_TIME_NONE) {
    GST_DEBUG_OBJECT (parse, "Seek end-time not supported in streaming mode");
    return FALSE;
  }

  if (!(flags & GST_SEEK_FLAG_FLUSH)) {
    GST_DEBUG_OBJECT (parse,
        "Non-flushing seek not supported in streaming mode");
    return FALSE;
  }

  if (flags & GST_SEEK_FLAG_SEGMENT) {
    GST_DEBUG_OBJECT (parse, "Segment seek not supported in streaming mode");
    return FALSE;
  }

  /* check for having parsed index already */
  if (!parse->index_parsed) {
    gboolean building_index;
    guint64 offset = 0;

    if (!parse->index_offset) {
      GST_DEBUG_OBJECT (parse, "no index (location); no seek in push mode");
      return FALSE;
    }

    GST_OBJECT_LOCK (parse);
    /* handle the seek event in the chain function */
    parse->state = GST_MATROSKA_PARSE_STATE_SEEK;
    /* no more seek can be issued until state reset to _DATA */

    /* copy the event */
    if (parse->seek_event)
      gst_event_unref (parse->seek_event);
    parse->seek_event = gst_event_ref (event);

    /* set the building_index flag so that only one thread can setup the
     * structures for index seeking. */
    building_index = parse->building_index;
    if (!building_index) {
      parse->building_index = TRUE;
      offset = parse->index_offset;
    }
    GST_OBJECT_UNLOCK (parse);

    if (!building_index) {
      /* seek to the first subindex or legacy index */
      GST_INFO_OBJECT (parse, "Seeking to Cues at %" G_GUINT64_FORMAT, offset);
      return perform_seek_to_offset (parse, offset);
    }

    /* well, we are handling it already */
    return TRUE;
  }

  /* delegate to tweaked regular seek */
  return gst_matroska_parse_handle_seek_event (parse, pad, event);
}

static gboolean
gst_matroska_parse_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* no seeking until we are (safely) ready */
      if (parse->state != GST_MATROSKA_PARSE_STATE_DATA) {
        GST_DEBUG_OBJECT (parse, "not ready for seeking yet");
        return FALSE;
      }
      res = gst_matroska_parse_handle_seek_push (parse, pad, event);
      gst_event_unref (event);
      break;

    case GST_EVENT_QOS:
    {
      GstMatroskaTrackContext *context = gst_pad_get_element_private (pad);
      if (context->type == GST_MATROSKA_TRACK_TYPE_VIDEO) {
        GstMatroskaTrackVideoContext *videocontext =
            (GstMatroskaTrackVideoContext *) context;
        gdouble proportion;
        GstClockTimeDiff diff;
        GstClockTime timestamp;

        gst_event_parse_qos (event, &proportion, &diff, &timestamp);

        GST_OBJECT_LOCK (parse);
        videocontext->earliest_time = timestamp + diff;
        GST_OBJECT_UNLOCK (parse);
      }
      res = TRUE;
      gst_event_unref (event);
      break;
    }

      /* events we don't need to handle */
    case GST_EVENT_NAVIGATION:
      gst_event_unref (event);
      res = FALSE;
      break;

    case GST_EVENT_LATENCY:
    default:
      res = gst_pad_push_event (parse->sinkpad, event);
      break;
  }

  gst_object_unref (parse);

  return res;
}


/* skip unknown or alike element */
static GstFlowReturn
gst_matroska_parse_parse_skip (GstMatroskaParse * parse, GstEbmlRead * ebml,
    const gchar * parent_name, guint id)
{
  if (id == GST_EBML_ID_VOID) {
    GST_DEBUG_OBJECT (parse, "Skipping EBML Void element");
  } else if (id == GST_EBML_ID_CRC32) {
    GST_DEBUG_OBJECT (parse, "Skipping EBML CRC32 element");
  } else {
    GST_WARNING_OBJECT (parse,
        "Unknown %s subelement 0x%x - ignoring", parent_name, id);
  }

  return gst_ebml_read_skip (ebml);
}

static GstFlowReturn
gst_matroska_parse_parse_header (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  GstFlowReturn ret;
  gchar *doctype;
  guint version;
  guint32 id;

  /* this function is the first to be called */

  /* default init */
  doctype = NULL;
  version = 1;

  ret = gst_ebml_peek_id (ebml, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_DEBUG_OBJECT (parse, "id: %08x", id);

  if (id != GST_EBML_ID_HEADER) {
    GST_ERROR_OBJECT (parse, "Failed to read header");
    goto exit;
  }

  ret = gst_ebml_read_master (ebml, &id);
  if (ret != GST_FLOW_OK)
    return ret;

  while (gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    ret = gst_ebml_peek_id (ebml, &id);
    if (ret != GST_FLOW_OK)
      return ret;

    switch (id) {
        /* is our read version uptodate? */
      case GST_EBML_ID_EBMLREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        if (num != GST_EBML_VERSION) {
          GST_ERROR_OBJECT (ebml, "Unsupported EBML version %" G_GUINT64_FORMAT,
              num);
          return GST_FLOW_ERROR;
        }

        GST_DEBUG_OBJECT (ebml, "EbmlReadVersion: %" G_GUINT64_FORMAT, num);
        break;
      }

        /* we only handle 8 byte lengths at max */
      case GST_EBML_ID_EBMLMAXSIZELENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        if (num > sizeof (guint64)) {
          GST_ERROR_OBJECT (ebml,
              "Unsupported EBML maximum size %" G_GUINT64_FORMAT, num);
          return GST_FLOW_ERROR;
        }
        GST_DEBUG_OBJECT (ebml, "EbmlMaxSizeLength: %" G_GUINT64_FORMAT, num);
        break;
      }

        /* we handle 4 byte IDs at max */
      case GST_EBML_ID_EBMLMAXIDLENGTH:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        if (num > sizeof (guint32)) {
          GST_ERROR_OBJECT (ebml,
              "Unsupported EBML maximum ID %" G_GUINT64_FORMAT, num);
          return GST_FLOW_ERROR;
        }
        GST_DEBUG_OBJECT (ebml, "EbmlMaxIdLength: %" G_GUINT64_FORMAT, num);
        break;
      }

      case GST_EBML_ID_DOCTYPE:{
        gchar *text;

        ret = gst_ebml_read_ascii (ebml, &id, &text);
        if (ret != GST_FLOW_OK)
          return ret;

        GST_DEBUG_OBJECT (ebml, "EbmlDocType: %s", GST_STR_NULL (text));

        if (doctype)
          g_free (doctype);
        doctype = text;
        break;
      }

      case GST_EBML_ID_DOCTYPEREADVERSION:{
        guint64 num;

        ret = gst_ebml_read_uint (ebml, &id, &num);
        if (ret != GST_FLOW_OK)
          return ret;
        version = num;
        GST_DEBUG_OBJECT (ebml, "EbmlReadVersion: %" G_GUINT64_FORMAT, num);
        break;
      }

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "EBML header", id);
        if (ret != GST_FLOW_OK)
          return ret;
        break;

        /* we ignore these two, as they don't tell us anything we care about */
      case GST_EBML_ID_EBMLVERSION:
      case GST_EBML_ID_DOCTYPEVERSION:
        ret = gst_ebml_read_skip (ebml);
        if (ret != GST_FLOW_OK)
          return ret;
        break;
    }
  }

exit:

  if ((doctype != NULL && !strcmp (doctype, GST_MATROSKA_DOCTYPE_MATROSKA)) ||
      (doctype != NULL && !strcmp (doctype, GST_MATROSKA_DOCTYPE_WEBM)) ||
      (doctype == NULL)) {
    if (version <= 2) {
      if (doctype) {
        GST_INFO_OBJECT (parse, "Input is %s version %d", doctype, version);
      } else {
        GST_WARNING_OBJECT (parse, "Input is EBML without doctype, assuming "
            "matroska (version %d)", version);
      }
      ret = GST_FLOW_OK;
    } else {
      GST_ELEMENT_ERROR (parse, STREAM, DEMUX, (NULL),
          ("Parser version (2) is too old to read %s version %d",
              GST_STR_NULL (doctype), version));
      ret = GST_FLOW_ERROR;
    }
    g_free (doctype);
  } else {
    GST_ELEMENT_ERROR (parse, STREAM, WRONG_TYPE, (NULL),
        ("Input is not a matroska stream (doctype=%s)", doctype));
    ret = GST_FLOW_ERROR;
    g_free (doctype);
  }

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_tracks (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;

  DEBUG_ELEMENT_START (parse, ebml, "Tracks");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "Tracks", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* one track within the "all-tracks" header */
      case GST_MATROSKA_ID_TRACKENTRY:
        ret = gst_matroska_parse_add_stream (parse, ebml);
        break;

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "Track", id);
        break;
    }
  }
  DEBUG_ELEMENT_STOP (parse, ebml, "Tracks", ret);

  parse->tracks_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_index_cuetrack (GstMatroskaParse * parse,
    GstEbmlRead * ebml, guint * nentries)
{
  guint32 id;
  GstFlowReturn ret;
  GstMatroskaIndex idx;

  idx.pos = (guint64) - 1;
  idx.track = 0;
  idx.time = GST_CLOCK_TIME_NONE;
  idx.block = 1;

  DEBUG_ELEMENT_START (parse, ebml, "CueTrackPositions");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "CueTrackPositions", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* track number */
      case GST_MATROSKA_ID_CUETRACK:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num == 0) {
          idx.track = 0;
          GST_WARNING_OBJECT (parse, "Invalid CueTrack 0");
          break;
        }

        GST_DEBUG_OBJECT (parse, "CueTrack: %" G_GUINT64_FORMAT, num);
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
          GST_WARNING_OBJECT (parse, "CueClusterPosition %" G_GUINT64_FORMAT
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
          GST_WARNING_OBJECT (parse, "Invalid CueBlockNumber 0");
          break;
        }

        GST_DEBUG_OBJECT (parse, "CueBlockNumber: %" G_GUINT64_FORMAT, num);
        idx.block = num;

        /* mild sanity check, disregard strange cases ... */
        if (idx.block > G_MAXUINT16) {
          GST_DEBUG_OBJECT (parse, "... looks suspicious, ignoring");
          idx.block = 1;
        }
        break;
      }

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "CueTrackPositions",
            id);
        break;

      case GST_MATROSKA_ID_CUECODECSTATE:
      case GST_MATROSKA_ID_CUEREFERENCE:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "CueTrackPositions", ret);

  if ((ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED)
      && idx.pos != (guint64) - 1 && idx.track > 0) {
    g_array_append_val (parse->index, idx);
    (*nentries)++;
  } else if (ret == GST_FLOW_OK || ret == GST_FLOW_UNEXPECTED) {
    GST_DEBUG_OBJECT (parse, "CueTrackPositions without valid content");
  }

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_index_pointentry (GstMatroskaParse * parse,
    GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  guint nentries = 0;

  DEBUG_ELEMENT_START (parse, ebml, "CuePoint");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "CuePoint", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_CUETIME:
      {
        if ((ret = gst_ebml_read_uint (ebml, &id, &time)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "CueTime: %" G_GUINT64_FORMAT, time);
        time = time * parse->time_scale;
        break;
      }

        /* position in the file + track to which it belongs */
      case GST_MATROSKA_ID_CUETRACKPOSITIONS:
      {
        if ((ret =
                gst_matroska_parse_parse_index_cuetrack (parse, ebml,
                    &nentries)) != GST_FLOW_OK)
          break;
        break;
      }

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "CuePoint", id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "CuePoint", ret);

  if (nentries > 0) {
    if (time == GST_CLOCK_TIME_NONE) {
      GST_WARNING_OBJECT (parse, "CuePoint without valid time");
      g_array_remove_range (parse->index, parse->index->len - nentries,
          nentries);
    } else {
      gint i;

      for (i = parse->index->len - nentries; i < parse->index->len; i++) {
        GstMatroskaIndex *idx =
            &g_array_index (parse->index, GstMatroskaIndex, i);

        idx->time = time;
        GST_DEBUG_OBJECT (parse, "Index entry: pos=%" G_GUINT64_FORMAT
            ", time=%" GST_TIME_FORMAT ", track=%u, block=%u", idx->pos,
            GST_TIME_ARGS (idx->time), (guint) idx->track, (guint) idx->block);
      }
    }
  } else {
    GST_DEBUG_OBJECT (parse, "Empty CuePoint");
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
gst_matroska_parse_parse_index (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;
  guint i;

  if (parse->index)
    g_array_free (parse->index, TRUE);
  parse->index =
      g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaIndex), 128);

  DEBUG_ELEMENT_START (parse, ebml, "Cues");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "Cues", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_POINTENTRY:
        ret = gst_matroska_parse_parse_index_pointentry (parse, ebml);
        break;

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "Cues", id);
        break;
    }
  }
  DEBUG_ELEMENT_STOP (parse, ebml, "Cues", ret);

  /* Sort index by time, smallest time first, for easier searching */
  g_array_sort (parse->index, (GCompareFunc) gst_matroska_index_compare);

  /* Now sort the track specific index entries into their own arrays */
  for (i = 0; i < parse->index->len; i++) {
    GstMatroskaIndex *idx = &g_array_index (parse->index, GstMatroskaIndex, i);
    gint track_num;
    GstMatroskaTrackContext *ctx;

    if (parse->element_index) {
      gint writer_id;

      if (idx->track != 0 &&
          (track_num =
              gst_matroska_parse_stream_from_num (parse, idx->track)) != -1) {
        ctx = g_ptr_array_index (parse->src, track_num);

        if (ctx->index_writer_id == -1)
          gst_index_get_writer_id (parse->element_index, GST_OBJECT (ctx->pad),
              &ctx->index_writer_id);
        writer_id = ctx->index_writer_id;
      } else {
        if (parse->element_index_writer_id == -1)
          gst_index_get_writer_id (parse->element_index, GST_OBJECT (parse),
              &parse->element_index_writer_id);
        writer_id = parse->element_index_writer_id;
      }

      GST_LOG_OBJECT (parse, "adding association %" GST_TIME_FORMAT "-> %"
          G_GUINT64_FORMAT " for writer id %d", GST_TIME_ARGS (idx->time),
          idx->pos, writer_id);
      gst_index_add_association (parse->element_index, writer_id,
          GST_ASSOCIATION_FLAG_KEY_UNIT, GST_FORMAT_TIME, idx->time,
          GST_FORMAT_BYTES, idx->pos + parse->ebml_segment_start, NULL);
    }

    if (idx->track == 0)
      continue;

    track_num = gst_matroska_parse_stream_from_num (parse, idx->track);
    if (track_num == -1)
      continue;

    ctx = g_ptr_array_index (parse->src, track_num);

    if (ctx->index_table == NULL)
      ctx->index_table =
          g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaIndex), 128);

    g_array_append_vals (ctx->index_table, idx, 1);
  }

  parse->index_parsed = TRUE;

  /* sanity check; empty index normalizes to no index */
  if (parse->index->len == 0) {
    g_array_free (parse->index, TRUE);
    parse->index = NULL;
  }

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_info (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gdouble dur_f = -1.0;
  guint32 id;

  DEBUG_ELEMENT_START (parse, ebml, "SegmentInfo");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "SegmentInfo", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_TIMECODESCALE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;


        GST_DEBUG_OBJECT (parse, "TimeCodeScale: %" G_GUINT64_FORMAT, num);
        parse->time_scale = num;
        break;
      }

      case GST_MATROSKA_ID_DURATION:{
        if ((ret = gst_ebml_read_float (ebml, &id, &dur_f)) != GST_FLOW_OK)
          break;

        if (dur_f <= 0.0) {
          GST_WARNING_OBJECT (parse, "Invalid duration %lf", dur_f);
          break;
        }

        GST_DEBUG_OBJECT (parse, "Duration: %lf", dur_f);
        break;
      }

      case GST_MATROSKA_ID_WRITINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "WritingApp: %s", GST_STR_NULL (text));
        parse->writing_app = text;
        break;
      }

      case GST_MATROSKA_ID_MUXINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "MuxingApp: %s", GST_STR_NULL (text));
        parse->muxing_app = text;
        break;
      }

      case GST_MATROSKA_ID_DATEUTC:{
        gint64 time;

        if ((ret = gst_ebml_read_date (ebml, &id, &time)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "DateUTC: %" G_GINT64_FORMAT, time);
        parse->created = time;
        break;
      }

      case GST_MATROSKA_ID_TITLE:{
        gchar *text;
        GstTagList *taglist;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "Title: %s", GST_STR_NULL (text));
        taglist = gst_tag_list_new ();
        gst_tag_list_add (taglist, GST_TAG_MERGE_APPEND, GST_TAG_TITLE, text,
            NULL);
        gst_matroska_parse_found_global_tag (parse, taglist);
        g_free (text);
        break;
      }

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "SegmentInfo", id);
        break;

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
  }

  if (dur_f > 0.0) {
    GstClockTime dur_u;

    dur_u = gst_gdouble_to_guint64 (dur_f *
        gst_guint64_to_gdouble (parse->time_scale));
    if (GST_CLOCK_TIME_IS_VALID (dur_u) && dur_u <= G_MAXINT64)
      gst_segment_set_duration (&parse->segment, GST_FORMAT_TIME, dur_u);
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "SegmentInfo", ret);

  parse->segmentinfo_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_metadata_id_simple_tag (GstMatroskaParse * parse,
    GstEbmlRead * ebml, GstTagList ** p_taglist)
{
  /* FIXME: check if there are more useful mappings */
  struct
  {
    const gchar *matroska_tagname;
    const gchar *gstreamer_tagname;
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
  GstFlowReturn ret;
  guint32 id;
  gchar *value = NULL;
  gchar *tag = NULL;

  DEBUG_ELEMENT_START (parse, ebml, "SimpleTag");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "SimpleTag", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_TAGNAME:
        g_free (tag);
        tag = NULL;
        ret = gst_ebml_read_ascii (ebml, &id, &tag);
        GST_DEBUG_OBJECT (parse, "TagName: %s", GST_STR_NULL (tag));
        break;

      case GST_MATROSKA_ID_TAGSTRING:
        g_free (value);
        value = NULL;
        ret = gst_ebml_read_utf8 (ebml, &id, &value);
        GST_DEBUG_OBJECT (parse, "TagString: %s", GST_STR_NULL (value));
        break;

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "SimpleTag", id);
        break;
        /* fall-through */

      case GST_MATROSKA_ID_TAGLANGUAGE:
      case GST_MATROSKA_ID_TAGDEFAULT:
      case GST_MATROSKA_ID_TAGBINARY:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "SimpleTag", ret);

  if (tag && value) {
    guint i;

    for (i = 0; i < G_N_ELEMENTS (tag_conv); i++) {
      const gchar *tagname_gst = tag_conv[i].gstreamer_tagname;

      const gchar *tagname_mkv = tag_conv[i].matroska_tagname;

      if (strcmp (tagname_mkv, tag) == 0) {
        GValue dest = { 0, };
        GType dest_type = gst_tag_get_type (tagname_gst);

        /* Ensure that any date string is complete */
        if (dest_type == GST_TYPE_DATE) {
          guint year = 1901, month = 1, day = 1;

          /* Dates can be yyyy-MM-dd, yyyy-MM or yyyy, but we need
           * the first type */
          if (sscanf (value, "%04u-%02u-%02u", &year, &month, &day) != 0) {
            g_free (value);
            value = g_strdup_printf ("%04u-%02u-%02u", year, month, day);
          }
        }

        g_value_init (&dest, dest_type);
        if (gst_value_deserialize (&dest, value)) {
          gst_tag_list_add_values (*p_taglist, GST_TAG_MERGE_APPEND,
              tagname_gst, &dest, NULL);
        } else {
          GST_WARNING_OBJECT (parse, "Can't transform tag '%s' with "
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
gst_matroska_parse_parse_metadata_id_tag (GstMatroskaParse * parse,
    GstEbmlRead * ebml, GstTagList ** p_taglist)
{
  guint32 id;
  GstFlowReturn ret;

  DEBUG_ELEMENT_START (parse, ebml, "Tag");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "Tag", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_SIMPLETAG:
        ret = gst_matroska_parse_parse_metadata_id_simple_tag (parse, ebml,
            p_taglist);
        break;

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "Tag", id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "Tag", ret);

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_metadata (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  GstTagList *taglist;
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;
  GList *l;
  guint64 curpos;

  curpos = gst_ebml_read_get_pos (ebml);

  /* Make sure we don't parse a tags element twice and
   * post it's tags twice */
  curpos = gst_ebml_read_get_pos (ebml);
  for (l = parse->tags_parsed; l; l = l->next) {
    guint64 *pos = l->data;

    if (*pos == curpos) {
      GST_DEBUG_OBJECT (parse, "Skipping already parsed Tags at offset %"
          G_GUINT64_FORMAT, curpos);
      return GST_FLOW_OK;
    }
  }

  parse->tags_parsed =
      g_list_prepend (parse->tags_parsed, g_slice_new (guint64));
  *((guint64 *) parse->tags_parsed->data) = curpos;
  /* fall-through */

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "Tags", ret);
    return ret;
  }

  taglist = gst_tag_list_new ();

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_TAG:
        ret = gst_matroska_parse_parse_metadata_id_tag (parse, ebml, &taglist);
        break;

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "Tags", id);
        break;
        /* FIXME: Use to limit the tags to specific pads */
      case GST_MATROSKA_ID_TARGETS:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "Tags", ret);

  gst_matroska_parse_found_global_tag (parse, taglist);

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_attached_file (GstMatroskaParse * parse,
    GstEbmlRead * ebml, GstTagList * taglist)
{
  guint32 id;
  GstFlowReturn ret;
  gchar *description = NULL;
  gchar *filename = NULL;
  gchar *mimetype = NULL;
  guint8 *data = NULL;
  guint64 datalen = 0;

  DEBUG_ELEMENT_START (parse, ebml, "AttachedFile");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "AttachedFile", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    /* read all sub-entries */

    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_FILEDESCRIPTION:
        if (description) {
          GST_WARNING_OBJECT (parse, "FileDescription can only appear once");
          break;
        }

        ret = gst_ebml_read_utf8 (ebml, &id, &description);
        GST_DEBUG_OBJECT (parse, "FileDescription: %s",
            GST_STR_NULL (description));
        break;
      case GST_MATROSKA_ID_FILENAME:
        if (filename) {
          GST_WARNING_OBJECT (parse, "FileName can only appear once");
          break;
        }

        ret = gst_ebml_read_utf8 (ebml, &id, &filename);

        GST_DEBUG_OBJECT (parse, "FileName: %s", GST_STR_NULL (filename));
        break;
      case GST_MATROSKA_ID_FILEMIMETYPE:
        if (mimetype) {
          GST_WARNING_OBJECT (parse, "FileMimeType can only appear once");
          break;
        }

        ret = gst_ebml_read_ascii (ebml, &id, &mimetype);
        GST_DEBUG_OBJECT (parse, "FileMimeType: %s", GST_STR_NULL (mimetype));
        break;
      case GST_MATROSKA_ID_FILEDATA:
        if (data) {
          GST_WARNING_OBJECT (parse, "FileData can only appear once");
          break;
        }

        ret = gst_ebml_read_binary (ebml, &id, &data, &datalen);
        GST_DEBUG_OBJECT (parse, "FileData of size %" G_GUINT64_FORMAT,
            datalen);
        break;

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "AttachedFile", id);
        break;
      case GST_MATROSKA_ID_FILEUID:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "AttachedFile", ret);

  if (filename && mimetype && data && datalen > 0) {
    GstTagImageType image_type = GST_TAG_IMAGE_TYPE_NONE;
    GstBuffer *tagbuffer = NULL;
    GstCaps *caps;
    gchar *filename_lc = g_utf8_strdown (filename, -1);

    GST_DEBUG_OBJECT (parse, "Creating tag for attachment with filename '%s', "
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

    GST_DEBUG_OBJECT (parse,
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
gst_matroska_parse_parse_attachments (GstMatroskaParse * parse,
    GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;
  GstTagList *taglist;

  DEBUG_ELEMENT_START (parse, ebml, "Attachments");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "Attachments", ret);
    return ret;
  }

  taglist = gst_tag_list_new ();

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_ATTACHEDFILE:
        ret = gst_matroska_parse_parse_attached_file (parse, ebml, taglist);
        break;

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "Attachments", id);
        break;
    }
  }
  DEBUG_ELEMENT_STOP (parse, ebml, "Attachments", ret);

  if (gst_structure_n_fields (GST_STRUCTURE (taglist)) > 0) {
    GST_DEBUG_OBJECT (parse, "Storing attachment tags");
    gst_matroska_parse_found_global_tag (parse, taglist);
  } else {
    GST_DEBUG_OBJECT (parse, "No valid attachments found");
    gst_tag_list_free (taglist);
  }

  parse->attachments_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_chapters (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  guint32 id;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_WARNING_OBJECT (parse, "Parsing of chapters not implemented yet");

  /* TODO: implement parsing of chapters */

  DEBUG_ELEMENT_START (parse, ebml, "Chapters");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "Chapters", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      default:
        ret = gst_ebml_read_skip (ebml);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "Chapters", ret);
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

static GstFlowReturn
gst_matroska_parse_parse_blockgroup_or_simpleblock (GstMatroskaParse * parse,
    GstEbmlRead * ebml, guint64 cluster_time, guint64 cluster_offset,
    gboolean is_simpleblock)
{
  GstMatroskaTrackContext *stream = NULL;
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

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if (!is_simpleblock) {
      if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK) {
        goto data_error;
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
        if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0)
          goto data_error;
        data += n;
        size -= n;

        /* fetch stream from num */
        stream_num = gst_matroska_parse_stream_from_num (parse, num);
        if (G_UNLIKELY (size < 3)) {
          GST_WARNING_OBJECT (parse, "Invalid size %u", size);
          /* non-fatal, try next block(group) */
          ret = GST_FLOW_OK;
          goto done;
        } else if (G_UNLIKELY (stream_num < 0 ||
                stream_num >= parse->num_streams)) {
          /* let's not give up on a stray invalid track number */
          GST_WARNING_OBJECT (parse,
              "Invalid stream %d for track number %" G_GUINT64_FORMAT
              "; ignoring block", stream_num, num);
          goto done;
        }

        stream = g_ptr_array_index (parse->src, stream_num);

        /* time (relative to cluster time) */
        time = ((gint16) GST_READ_UINT16_BE (data));
        data += 2;
        size -= 2;
        flags = GST_READ_UINT8 (data);
        data += 1;
        size -= 1;

        GST_LOG_OBJECT (parse, "time %" G_GUINT64_FORMAT ", flags %d", time,
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
            if (size == 0)
              goto invalid_lacing;
            laces = GST_READ_UINT8 (data) + 1;
            data += 1;
            size -= 1;
            lace_size = g_new0 (gint, laces);

            switch ((flags & 0x06) >> 1) {
              case 0x1:        /* xiph lacing */  {
                guint temp, total = 0;

                for (n = 0; ret == GST_FLOW_OK && n < laces - 1; n++) {
                  while (1) {
                    if (size == 0)
                      goto invalid_lacing;
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

                if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0)
                  goto data_error;
                data += n;
                size -= n;
                total = lace_size[0] = num;
                for (n = 1; ret == GST_FLOW_OK && n < laces - 1; n++) {
                  gint64 snum;
                  gint r;

                  if ((r = gst_matroska_ebmlnum_sint (data, size, &snum)) < 0)
                    goto data_error;
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

        if (ret != GST_FLOW_OK)
          break;

        readblock = TRUE;
        break;
      }

      case GST_MATROSKA_ID_BLOCKDURATION:{
        ret = gst_ebml_read_uint (ebml, &id, &block_duration);
        GST_DEBUG_OBJECT (parse, "BlockDuration: %" G_GUINT64_FORMAT,
            block_duration);
        break;
      }

      case GST_MATROSKA_ID_REFERENCEBLOCK:{
        ret = gst_ebml_read_sint (ebml, &id, &referenceblock);
        GST_DEBUG_OBJECT (parse, "ReferenceBlock: %" G_GINT64_FORMAT,
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

        if (G_UNLIKELY (stream == NULL)) {
          GST_WARNING_OBJECT (parse,
              "Unexpected CodecState subelement - ignoring");
          break;
        }

        g_free (stream->codec_state);
        stream->codec_state = data;
        stream->codec_state_size = data_len;

        break;
      }

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "BlockGroup", id);
        break;

      case GST_MATROSKA_ID_BLOCKVIRTUAL:
      case GST_MATROSKA_ID_BLOCKADDITIONS:
      case GST_MATROSKA_ID_REFERENCEPRIORITY:
      case GST_MATROSKA_ID_REFERENCEVIRTUAL:
      case GST_MATROSKA_ID_SLICES:
        GST_DEBUG_OBJECT (parse,
            "Skipping BlockGroup subelement 0x%x - ignoring", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (is_simpleblock)
      break;
  }

  /* reading a number or so could have failed */
  if (ret != GST_FLOW_OK)
    goto data_error;

  if (ret == GST_FLOW_OK && readblock) {
    guint64 duration = 0;
    gint64 lace_time = 0;
    gboolean delta_unit;

    stream = g_ptr_array_index (parse->src, stream_num);

    if (cluster_time != GST_CLOCK_TIME_NONE) {
      /* FIXME: What to do with negative timestamps? Give timestamp 0 or -1?
       * Drop unless the lace contains timestamp 0? */
      if (time < 0 && (-time) > cluster_time) {
        lace_time = 0;
      } else {
        if (stream->timecodescale == 1.0)
          lace_time = (cluster_time + time) * parse->time_scale;
        else
          lace_time =
              gst_util_guint64_to_gdouble ((cluster_time + time) *
              parse->time_scale) * stream->timecodescale;
      }
    } else {
      lace_time = GST_CLOCK_TIME_NONE;
    }

    if (lace_time != GST_CLOCK_TIME_NONE) {
      parse->last_timestamp = lace_time;
    }
    /* need to refresh segment info ASAP */
    if (GST_CLOCK_TIME_IS_VALID (lace_time) && parse->need_newsegment) {
      GST_DEBUG_OBJECT (parse,
          "generating segment starting at %" GST_TIME_FORMAT,
          GST_TIME_ARGS (lace_time));
      /* pretend we seeked here */
      gst_segment_set_seek (&parse->segment, parse->segment.rate,
          GST_FORMAT_TIME, 0, GST_SEEK_TYPE_SET, lace_time,
          GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE, NULL);
      /* now convey our segment notion downstream */
      gst_matroska_parse_send_event (parse, gst_event_new_new_segment (FALSE,
              parse->segment.rate, parse->segment.format, parse->segment.start,
              parse->segment.stop, parse->segment.start));
      parse->need_newsegment = FALSE;
    }

    if (block_duration) {
      if (stream->timecodescale == 1.0)
        duration = gst_util_uint64_scale (block_duration, parse->time_scale, 1);
      else
        duration =
            gst_util_gdouble_to_guint64 (gst_util_guint64_to_gdouble
            (gst_util_uint64_scale (block_duration, parse->time_scale,
                    1)) * stream->timecodescale);
    } else if (stream->default_duration) {
      duration = stream->default_duration * laces;
    }
    /* else duration is diff between timecode of this and next block */

    /* For SimpleBlock, look at the keyframe bit in flags. Otherwise,
       a ReferenceBlock implies that this is not a keyframe. In either
       case, it only makes sense for video streams. */
    delta_unit = stream->type == GST_MATROSKA_TRACK_TYPE_VIDEO &&
        ((is_simpleblock && !(flags & 0x80)) || referenceblock);

    if (delta_unit && stream->set_discont) {
      /* When doing seeks or such, we need to restart on key frames or
       * decoders might choke. */
      GST_DEBUG_OBJECT (parse, "skipping delta unit");
      goto done;
    }

    for (n = 0; n < laces; n++) {
      if (G_UNLIKELY (lace_size[n] > size)) {
        GST_WARNING_OBJECT (parse, "Invalid lace size");
        break;
      }

      /* QoS for video track with an index. the assumption is that
         index entries point to keyframes, but if that is not true we
         will instad skip until the next keyframe. */
      if (GST_CLOCK_TIME_IS_VALID (lace_time) &&
          stream->type == GST_MATROSKA_TRACK_TYPE_VIDEO &&
          stream->index_table && parse->segment.rate > 0.0) {
        GstMatroskaTrackVideoContext *videocontext =
            (GstMatroskaTrackVideoContext *) stream;
        GstClockTime earliest_time;
        GstClockTime earliest_stream_time;

        GST_OBJECT_LOCK (parse);
        earliest_time = videocontext->earliest_time;
        GST_OBJECT_UNLOCK (parse);
        earliest_stream_time = gst_segment_to_position (&parse->segment,
            GST_FORMAT_TIME, earliest_time);

        if (GST_CLOCK_TIME_IS_VALID (lace_time) &&
            GST_CLOCK_TIME_IS_VALID (earliest_stream_time) &&
            lace_time <= earliest_stream_time) {
          /* find index entry (keyframe) <= earliest_stream_time */
          GstMatroskaIndex *entry =
              gst_util_array_binary_search (stream->index_table->data,
              stream->index_table->len, sizeof (GstMatroskaIndex),
              (GCompareDataFunc) gst_matroska_index_seek_find,
              GST_SEARCH_MODE_BEFORE, &earliest_stream_time, NULL);

          /* if that entry (keyframe) is after the current the current
             buffer, we can skip pushing (and thus decoding) all
             buffers until that keyframe. */
          if (entry && GST_CLOCK_TIME_IS_VALID (entry->time) &&
              entry->time > lace_time) {
            GST_LOG_OBJECT (parse, "Skipping lace before late keyframe");
            stream->set_discont = TRUE;
            goto next_lace;
          }
        }
      }
#if 0
      sub = gst_buffer_create_sub (buf,
          GST_BUFFER_SIZE (buf) - size, lace_size[n]);
      GST_DEBUG_OBJECT (parse, "created subbuffer %p", sub);

      if (delta_unit)
        GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
      else
        GST_BUFFER_FLAG_UNSET (sub, GST_BUFFER_FLAG_DELTA_UNIT);

      if (stream->encodings != NULL && stream->encodings->len > 0)
        sub = gst_matroska_decode_buffer (stream, sub);

      if (sub == NULL) {
        GST_WARNING_OBJECT (parse, "Decoding buffer failed");
        goto next_lace;
      }

      GST_BUFFER_TIMESTAMP (sub) = lace_time;

      if (GST_CLOCK_TIME_IS_VALID (lace_time)) {
        GstClockTime last_stop_end;

        /* Check if this stream is after segment stop */
        if (GST_CLOCK_TIME_IS_VALID (parse->segment.stop) &&
            lace_time >= parse->segment.stop) {
          GST_DEBUG_OBJECT (parse,
              "Stream %d after segment stop %" GST_TIME_FORMAT, stream->index,
              GST_TIME_ARGS (parse->segment.stop));
          gst_buffer_unref (sub);
          goto eos;
        }
        if (offset >= stream->to_offset) {
          GST_DEBUG_OBJECT (parse, "Stream %d after playback section",
              stream->index);
          gst_buffer_unref (sub);
          goto eos;
        }

        /* handle gaps, e.g. non-zero start-time, or an cue index entry
         * that landed us with timestamps not quite intended */
        if (GST_CLOCK_TIME_IS_VALID (parse->segment.last_stop) &&
            parse->segment.rate > 0.0) {
          GstClockTimeDiff diff;

          /* only send newsegments with increasing start times,
           * otherwise if these go back and forth downstream (sinks) increase
           * accumulated time and running_time */
          diff = GST_CLOCK_DIFF (parse->segment.last_stop, lace_time);
          if (diff > 2 * GST_SECOND && lace_time > parse->segment.start &&
              (!GST_CLOCK_TIME_IS_VALID (parse->segment.stop) ||
                  lace_time < parse->segment.stop)) {
            GST_DEBUG_OBJECT (parse,
                "Gap of %" G_GINT64_FORMAT " ns detected in"
                "stream %d (%" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT "). "
                "Sending updated NEWSEGMENT events", diff,
                stream->index, GST_TIME_ARGS (stream->pos),
                GST_TIME_ARGS (lace_time));
            /* send newsegment events such that the gap is not accounted in
             * accum time, hence running_time */
            /* close ahead of gap */
            gst_matroska_parse_send_event (parse,
                gst_event_new_new_segment (TRUE, parse->segment.rate,
                    parse->segment.format, parse->segment.last_stop,
                    parse->segment.last_stop, parse->segment.last_stop));
            /* skip gap */
            gst_matroska_parse_send_event (parse,
                gst_event_new_new_segment (FALSE, parse->segment.rate,
                    parse->segment.format, lace_time, parse->segment.stop,
                    lace_time));
            /* align segment view with downstream,
             * prevents double-counting accum when closing segment */
            gst_segment_set_newsegment (&parse->segment, FALSE,
                parse->segment.rate, parse->segment.format, lace_time,
                parse->segment.stop, lace_time);
            parse->segment.last_stop = lace_time;
          }
        }

        if (!GST_CLOCK_TIME_IS_VALID (parse->segment.last_stop)
            || parse->segment.last_stop < lace_time) {
          parse->segment.last_stop = lace_time;
        }

        last_stop_end = lace_time;
        if (duration) {
          GST_BUFFER_DURATION (sub) = duration / laces;
          last_stop_end += GST_BUFFER_DURATION (sub);
        }

        if (!GST_CLOCK_TIME_IS_VALID (parse->last_stop_end) ||
            parse->last_stop_end < last_stop_end)
          parse->last_stop_end = last_stop_end;

        if (parse->segment.duration == -1 ||
            parse->segment.duration < lace_time) {
          gst_segment_set_duration (&parse->segment, GST_FORMAT_TIME,
              last_stop_end);
          gst_element_post_message (GST_ELEMENT_CAST (parse),
              gst_message_new_duration (GST_OBJECT_CAST (parse),
                  GST_FORMAT_TIME, GST_CLOCK_TIME_NONE));
        }
      }

      stream->pos = lace_time;

      gst_matroska_parse_sync_streams (parse);

      if (stream->set_discont) {
        GST_DEBUG_OBJECT (parse, "marking DISCONT");
        GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DISCONT);
        stream->set_discont = FALSE;
      }

      /* reverse playback book-keeping */
      if (!GST_CLOCK_TIME_IS_VALID (stream->from_time))
        stream->from_time = lace_time;
      if (stream->from_offset == -1)
        stream->from_offset = offset;

      GST_DEBUG_OBJECT (parse,
          "Pushing lace %d, data of size %d for stream %d, time=%"
          GST_TIME_FORMAT " and duration=%" GST_TIME_FORMAT, n,
          GST_BUFFER_SIZE (sub), stream_num,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (sub)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (sub)));

      if (parse->element_index) {
        if (stream->index_writer_id == -1)
          gst_index_get_writer_id (parse->element_index,
              GST_OBJECT (stream->pad), &stream->index_writer_id);

        GST_LOG_OBJECT (parse, "adding association %" GST_TIME_FORMAT "-> %"
            G_GUINT64_FORMAT " for writer id %d",
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (sub)), cluster_offset,
            stream->index_writer_id);
        gst_index_add_association (parse->element_index,
            stream->index_writer_id, GST_BUFFER_FLAG_IS_SET (sub,
                GST_BUFFER_FLAG_DELTA_UNIT) ? 0 : GST_ASSOCIATION_FLAG_KEY_UNIT,
            GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (sub), GST_FORMAT_BYTES,
            cluster_offset, NULL);
      }

      gst_buffer_set_caps (sub, GST_PAD_CAPS (parse->srcpad));

      /* Postprocess the buffers depending on the codec used */
      if (stream->postprocess_frame) {
        GST_LOG_OBJECT (parse, "running post process");
        ret = stream->postprocess_frame (GST_ELEMENT (parse), stream, &sub);
      }

      ret = gst_pad_push (stream->pad, sub);
      if (parse->segment.rate < 0) {
        if (lace_time > parse->segment.stop && ret == GST_FLOW_UNEXPECTED) {
          /* In reverse playback we can get a GST_FLOW_UNEXPECTED when
           * we are at the end of the segment, so we just need to jump
           * back to the previous section. */
          GST_DEBUG_OBJECT (parse, "downstream has reached end of segment");
          ret = GST_FLOW_OK;
        }
      }
      /* combine flows */
      ret = gst_matroska_parse_combine_flows (parse, stream, ret);
#endif

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

  /* EXITS */
invalid_lacing:
  {
    GST_ELEMENT_WARNING (parse, STREAM, DEMUX, (NULL), ("Invalid lacing size"));
    /* non-fatal, try next block(group) */
    ret = GST_FLOW_OK;
    goto done;
  }
data_error:
  {
    GST_ELEMENT_WARNING (parse, STREAM, DEMUX, (NULL), ("Data error"));
    /* non-fatal, try next block(group) */
    ret = GST_FLOW_OK;
    goto done;
  }
}

/* return FALSE if block(group) should be skipped (due to a seek) */
static inline gboolean
gst_matroska_parse_seek_block (GstMatroskaParse * parse)
{
  if (G_UNLIKELY (parse->seek_block)) {
    if (!(--parse->seek_block)) {
      return TRUE;
    } else {
      GST_LOG_OBJECT (parse, "should skip block due to seek");
      return FALSE;
    }
  } else {
    return TRUE;
  }
}

static GstFlowReturn
gst_matroska_parse_parse_contents_seekentry (GstMatroskaParse * parse,
    GstEbmlRead * ebml)
{
  GstFlowReturn ret;
  guint64 seek_pos = (guint64) - 1;
  guint32 seek_id = 0;
  guint32 id;

  DEBUG_ELEMENT_START (parse, ebml, "Seek");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "Seek", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_SEEKID:
      {
        guint64 t;

        if ((ret = gst_ebml_read_uint (ebml, &id, &t)) != GST_FLOW_OK)
          break;

        GST_DEBUG_OBJECT (parse, "SeekID: %" G_GUINT64_FORMAT, t);
        seek_id = t;
        break;
      }

      case GST_MATROSKA_ID_SEEKPOSITION:
      {
        guint64 t;

        if ((ret = gst_ebml_read_uint (ebml, &id, &t)) != GST_FLOW_OK)
          break;

        if (t > G_MAXINT64) {
          GST_WARNING_OBJECT (parse,
              "Too large SeekPosition %" G_GUINT64_FORMAT, t);
          break;
        }

        GST_DEBUG_OBJECT (parse, "SeekPosition: %" G_GUINT64_FORMAT, t);
        seek_pos = t;
        break;
      }

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "SeekHead", id);
        break;
    }
  }

  if (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED)
    return ret;

  if (!seek_id || seek_pos == (guint64) - 1) {
    GST_WARNING_OBJECT (parse, "Incomplete seekhead entry (0x%x/%"
        G_GUINT64_FORMAT ")", seek_id, seek_pos);
    return GST_FLOW_OK;
  }

  switch (seek_id) {
    case GST_MATROSKA_ID_SEEKHEAD:
    {
    }
    case GST_MATROSKA_ID_CUES:
    case GST_MATROSKA_ID_TAGS:
    case GST_MATROSKA_ID_TRACKS:
    case GST_MATROSKA_ID_SEGMENTINFO:
    case GST_MATROSKA_ID_ATTACHMENTS:
    case GST_MATROSKA_ID_CHAPTERS:
    {
      guint64 length;

      /* remember */
      length = gst_matroska_parse_get_length (parse);

      if (length == (guint64) - 1) {
        GST_DEBUG_OBJECT (parse, "no upstream length, skipping SeakHead entry");
        break;
      }

      /* check for validity */
      if (seek_pos + parse->ebml_segment_start + 12 >= length) {
        GST_WARNING_OBJECT (parse,
            "SeekHead reference lies outside file!" " (%"
            G_GUINT64_FORMAT "+%" G_GUINT64_FORMAT "+12 >= %"
            G_GUINT64_FORMAT ")", seek_pos, parse->ebml_segment_start, length);
        break;
      }

      /* only pick up index location when streaming */
      if (seek_id == GST_MATROSKA_ID_CUES) {
        parse->index_offset = seek_pos + parse->ebml_segment_start;
        GST_DEBUG_OBJECT (parse, "Cues located at offset %" G_GUINT64_FORMAT,
            parse->index_offset);
      }
      break;
    }

    default:
      GST_DEBUG_OBJECT (parse, "Ignoring Seek entry for ID=0x%x", seek_id);
      break;
  }
  DEBUG_ELEMENT_STOP (parse, ebml, "Seek", ret);

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_contents (GstMatroskaParse * parse, GstEbmlRead * ebml)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 id;

  DEBUG_ELEMENT_START (parse, ebml, "SeekHead");

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    DEBUG_ELEMENT_STOP (parse, ebml, "SeekHead", ret);
    return ret;
  }

  while (ret == GST_FLOW_OK && gst_ebml_read_has_remaining (ebml, 1, TRUE)) {
    if ((ret = gst_ebml_peek_id (ebml, &id)) != GST_FLOW_OK)
      break;

    switch (id) {
      case GST_MATROSKA_ID_SEEKENTRY:
      {
        ret = gst_matroska_parse_parse_contents_seekentry (parse, ebml);
        /* Ignore EOS and errors here */
        if (ret != GST_FLOW_OK) {
          GST_DEBUG_OBJECT (parse, "Ignoring %s", gst_flow_get_name (ret));
          ret = GST_FLOW_OK;
        }
        break;
      }

      default:
        ret = gst_matroska_parse_parse_skip (parse, ebml, "SeekHead", id);
        break;
    }
  }

  DEBUG_ELEMENT_STOP (parse, ebml, "SeekHead", ret);

  return ret;
}

#define GST_FLOW_OVERFLOW   GST_FLOW_CUSTOM_ERROR

#define MAX_BLOCK_SIZE (15 * 1024 * 1024)

static inline GstFlowReturn
gst_matroska_parse_check_read_size (GstMatroskaParse * parse, guint64 bytes)
{
  if (G_UNLIKELY (bytes > MAX_BLOCK_SIZE)) {
    /* only a few blocks are expected/allowed to be large,
     * and will be recursed into, whereas others will be read and must fit */
    /* fatal in streaming case, as we can't step over easily */
    GST_ELEMENT_ERROR (parse, STREAM, DEMUX, (NULL),
        ("reading large block of size %" G_GUINT64_FORMAT " not supported; "
            "file might be corrupt.", bytes));
    return GST_FLOW_ERROR;
  } else {
    return GST_FLOW_OK;
  }
}

/* returns TRUE if we truely are in error state, and should give up */
static inline gboolean
gst_matroska_parse_check_parse_error (GstMatroskaParse * parse)
{
  gint64 pos;

  /* sigh, one last attempt above and beyond call of duty ...;
   * search for cluster mark following current pos */
  pos = parse->offset;
  GST_WARNING_OBJECT (parse, "parse error, looking for next cluster");
  if (gst_matroska_parse_search_cluster (parse, &pos) != GST_FLOW_OK) {
    /* did not work, give up */
    return TRUE;
  } else {
    GST_DEBUG_OBJECT (parse, "... found at  %" G_GUINT64_FORMAT, pos);
    /* try that position */
    parse->offset = pos;
    return FALSE;
  }
}

/* initializes @ebml with @bytes from input stream at current offset.
 * Returns UNEXPECTED if insufficient available,
 * ERROR if too much was attempted to read. */
static inline GstFlowReturn
gst_matroska_parse_take (GstMatroskaParse * parse, guint64 bytes,
    GstEbmlRead * ebml)
{
  GstBuffer *buffer = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (parse, "taking %" G_GUINT64_FORMAT " bytes for parsing",
      bytes);
  ret = gst_matroska_parse_check_read_size (parse, bytes);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    /* otherwise fatal */
    ret = GST_FLOW_ERROR;
    goto exit;
  }
  if (gst_adapter_available (parse->adapter) >= bytes)
    buffer = gst_adapter_take_buffer (parse->adapter, bytes);
  else
    ret = GST_FLOW_UNEXPECTED;
  if (G_LIKELY (buffer)) {
    gst_ebml_read_init (ebml, GST_ELEMENT_CAST (parse), buffer, parse->offset);
    parse->offset += bytes;
  }
exit:
  return ret;
}

static void
gst_matroska_parse_check_seekability (GstMatroskaParse * parse)
{
  GstQuery *query;
  gboolean seekable = FALSE;
  gint64 start = -1, stop = -1;

  query = gst_query_new_seeking (GST_FORMAT_BYTES);
  if (!gst_pad_peer_query (parse->sinkpad, query)) {
    GST_DEBUG_OBJECT (parse, "seeking query failed");
    goto done;
  }

  gst_query_parse_seeking (query, NULL, &seekable, &start, &stop);

  /* try harder to query upstream size if we didn't get it the first time */
  if (seekable && stop == -1) {
    GstFormat fmt = GST_FORMAT_BYTES;

    GST_DEBUG_OBJECT (parse, "doing duration query to fix up unset stop");
    gst_pad_query_peer_duration (parse->sinkpad, &fmt, &stop);
  }

  /* if upstream doesn't know the size, it's likely that it's not seekable in
   * practice even if it technically may be seekable */
  if (seekable && (start != 0 || stop <= start)) {
    GST_DEBUG_OBJECT (parse, "seekable but unknown start/stop -> disable");
    seekable = FALSE;
  }

done:
  GST_INFO_OBJECT (parse, "seekable: %d (%" G_GUINT64_FORMAT " - %"
      G_GUINT64_FORMAT ")", seekable, start, stop);
  parse->seekable = seekable;

  gst_query_unref (query);
}

#if 0
static GstFlowReturn
gst_matroska_parse_find_tracks (GstMatroskaParse * parse)
{
  guint32 id;
  guint64 before_pos;
  guint64 length;
  guint needed;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_WARNING_OBJECT (parse,
      "Found Cluster element before Tracks, searching Tracks");

  /* remember */
  before_pos = parse->offset;

  /* Search Tracks element */
  while (TRUE) {
    ret = gst_matroska_parse_peek_id_length_pull (parse, &id, &length, &needed);
    if (ret != GST_FLOW_OK)
      break;

    if (id != GST_MATROSKA_ID_TRACKS) {
      /* we may be skipping large cluster here, so forego size check etc */
      /* ... but we can't skip undefined size; force error */
      if (length == G_MAXUINT64) {
        ret = gst_matroska_parse_check_read_size (parse, length);
        break;
      } else {
        parse->offset += needed;
        parse->offset += length;
      }
      continue;
    }

    /* will lead to track parsing ... */
    ret = gst_matroska_parse_parse_id (parse, id, length, needed);
    break;
  }

  /* seek back */
  parse->offset = before_pos;

  return ret;
}
#endif

#define GST_READ_CHECK(stmt)  \
G_STMT_START { \
  if (G_UNLIKELY ((ret = (stmt)) != GST_FLOW_OK)) { \
    if (ret == GST_FLOW_OVERFLOW) { \
      ret = GST_FLOW_OK; \
    } \
    goto read_error; \
  } \
} G_STMT_END

static void
gst_matroska_parse_accumulate_streamheader (GstMatroskaParse * parse,
    GstBuffer * buffer)
{
  if (parse->streamheader) {
    GstBuffer *buf;

    buf = gst_buffer_span (parse->streamheader, 0, buffer,
        GST_BUFFER_SIZE (parse->streamheader) + GST_BUFFER_SIZE (buffer));
    gst_buffer_unref (parse->streamheader);
    parse->streamheader = buf;
  } else {
    parse->streamheader = gst_buffer_ref (buffer);
  }

  GST_DEBUG ("%d", GST_BUFFER_SIZE (parse->streamheader));
}

static GstFlowReturn
gst_matroska_parse_output (GstMatroskaParse * parse, GstBuffer * buffer,
    gboolean keyframe)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (!parse->pushed_headers) {
    GstCaps *caps;
    GstStructure *s;
    GValue streamheader = { 0 };
    GValue bufval = { 0 };
    GstBuffer *buf;

    caps = gst_caps_new_simple ("video/x-matroska", NULL);
    s = gst_caps_get_structure (caps, 0);
    g_value_init (&streamheader, GST_TYPE_ARRAY);
    g_value_init (&bufval, GST_TYPE_BUFFER);
    GST_BUFFER_FLAG_SET (parse->streamheader, GST_BUFFER_FLAG_IN_CAPS);
    gst_value_set_buffer (&bufval, parse->streamheader);
    gst_value_array_append_value (&streamheader, &bufval);
    g_value_unset (&bufval);
    gst_structure_set_value (s, "streamheader", &streamheader);
    g_value_unset (&streamheader);
    //gst_caps_replace (parse->caps, caps);
    gst_pad_set_caps (parse->srcpad, caps);

    buf = gst_buffer_make_metadata_writable (parse->streamheader);
    gst_buffer_set_caps (buf, caps);
    gst_caps_unref (caps);

    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    ret = gst_pad_push (parse->srcpad, buf);

    parse->pushed_headers = TRUE;
  }

  if (!keyframe) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE) {
    parse->last_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  } else {
    GST_BUFFER_TIMESTAMP (buffer) = parse->last_timestamp;
  }
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (parse->srcpad));
  ret = gst_pad_push (parse->srcpad, gst_buffer_ref (buffer));

  return ret;
}

static GstFlowReturn
gst_matroska_parse_parse_id (GstMatroskaParse * parse, guint32 id,
    guint64 length, guint needed)
{
  GstEbmlRead ebml = { 0, };
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 read;
  //GstBuffer *buffer;

  GST_DEBUG_OBJECT (parse, "Parsing Element id 0x%x, "
      "size %" G_GUINT64_FORMAT ", prefix %d", id, length, needed);

#if 0
  if (gst_adapter_available (parse->adapter) >= length + needed) {
    buffer = gst_adapter_take_buffer (parse->adapter, length + needed);
    gst_pad_push (parse->srcpad, buffer);
  } else {
    ret = GST_FLOW_UNEXPECTED;
  }
  //GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));

  return ret;
#endif



  /* if we plan to read and parse this element, we need prefix (id + length)
   * and the contents */
  /* mind about overflow wrap-around when dealing with undefined size */
  read = length;
  if (G_LIKELY (length != G_MAXUINT64))
    read += needed;

  switch (parse->state) {
    case GST_MATROSKA_PARSE_STATE_START:
      switch (id) {
        case GST_EBML_ID_HEADER:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          ret = gst_matroska_parse_parse_header (parse, &ebml);
          if (ret != GST_FLOW_OK)
            goto parse_failed;
          parse->state = GST_MATROSKA_PARSE_STATE_SEGMENT;
          gst_matroska_parse_check_seekability (parse);
          gst_matroska_parse_accumulate_streamheader (parse, ebml.buf);
          break;
        default:
          goto invalid_header;
          break;
      }
      break;
    case GST_MATROSKA_PARSE_STATE_SEGMENT:
      switch (id) {
        case GST_MATROSKA_ID_SEGMENT:
          /* eat segment prefix */
          GST_READ_CHECK (gst_matroska_parse_take (parse, needed, &ebml));
          GST_DEBUG_OBJECT (parse,
              "Found Segment start at offset %" G_GUINT64_FORMAT,
              parse->offset);
          /* seeks are from the beginning of the segment,
           * after the segment ID/length */
          parse->ebml_segment_start = parse->offset;
          parse->state = GST_MATROSKA_PARSE_STATE_HEADER;
          gst_matroska_parse_accumulate_streamheader (parse, ebml.buf);
          break;
        default:
          GST_WARNING_OBJECT (parse,
              "Expected a Segment ID (0x%x), but received 0x%x!",
              GST_MATROSKA_ID_SEGMENT, id);
          GST_READ_CHECK (gst_matroska_parse_take (parse, needed, &ebml));
          gst_matroska_parse_accumulate_streamheader (parse, ebml.buf);
          break;
      }
      break;
    case GST_MATROSKA_PARSE_STATE_SCANNING:
      if (id != GST_MATROSKA_ID_CLUSTER &&
          id != GST_MATROSKA_ID_CLUSTERTIMECODE)
        goto skip;
      /* fall-through */
    case GST_MATROSKA_PARSE_STATE_HEADER:
    case GST_MATROSKA_PARSE_STATE_DATA:
    case GST_MATROSKA_PARSE_STATE_SEEK:
      switch (id) {
        case GST_MATROSKA_ID_SEGMENTINFO:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          if (!parse->segmentinfo_parsed) {
            ret = gst_matroska_parse_parse_info (parse, &ebml);
          }
          gst_matroska_parse_accumulate_streamheader (parse, ebml.buf);
          break;
        case GST_MATROSKA_ID_TRACKS:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          if (!parse->tracks_parsed) {
            ret = gst_matroska_parse_parse_tracks (parse, &ebml);
          }
          gst_matroska_parse_accumulate_streamheader (parse, ebml.buf);
          break;
        case GST_MATROSKA_ID_CLUSTER:
          if (G_UNLIKELY (!parse->tracks_parsed)) {
            GST_DEBUG_OBJECT (parse, "Cluster before Track");
            goto not_streamable;
          }
          if (G_UNLIKELY (parse->state == GST_MATROSKA_PARSE_STATE_HEADER)) {
            parse->state = GST_MATROSKA_PARSE_STATE_DATA;
            parse->first_cluster_offset = parse->offset;
            GST_DEBUG_OBJECT (parse, "signaling no more pads");
          }
          parse->cluster_time = GST_CLOCK_TIME_NONE;
          parse->cluster_offset = parse->offset;
          if (G_UNLIKELY (!parse->seek_first && parse->seek_block)) {
            GST_DEBUG_OBJECT (parse, "seek target block %" G_GUINT64_FORMAT
                " not found in Cluster, trying next Cluster's first block instead",
                parse->seek_block);
            parse->seek_block = 0;
          }
          parse->seek_first = FALSE;
          /* record next cluster for recovery */
          if (read != G_MAXUINT64)
            parse->next_cluster_offset = parse->cluster_offset + read;
          /* eat cluster prefix */
          GST_READ_CHECK (gst_matroska_parse_take (parse, needed, &ebml));
          ret = gst_matroska_parse_output (parse, ebml.buf, TRUE);
          //gst_matroska_parse_accumulate_streamheader (parse, ebml.buf);
          break;
        case GST_MATROSKA_ID_CLUSTERTIMECODE:
        {
          guint64 num;

          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          if ((ret = gst_ebml_read_uint (&ebml, &id, &num)) != GST_FLOW_OK)
            goto parse_failed;
          GST_DEBUG_OBJECT (parse, "ClusterTimeCode: %" G_GUINT64_FORMAT, num);
          parse->cluster_time = num;
          if (parse->element_index) {
            if (parse->element_index_writer_id == -1)
              gst_index_get_writer_id (parse->element_index,
                  GST_OBJECT (parse), &parse->element_index_writer_id);
            GST_LOG_OBJECT (parse, "adding association %" GST_TIME_FORMAT "-> %"
                G_GUINT64_FORMAT " for writer id %d",
                GST_TIME_ARGS (parse->cluster_time), parse->cluster_offset,
                parse->element_index_writer_id);
            gst_index_add_association (parse->element_index,
                parse->element_index_writer_id, GST_ASSOCIATION_FLAG_KEY_UNIT,
                GST_FORMAT_TIME, parse->cluster_time,
                GST_FORMAT_BYTES, parse->cluster_offset, NULL);
          }
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        }
        case GST_MATROSKA_ID_BLOCKGROUP:
          if (!gst_matroska_parse_seek_block (parse))
            goto skip;
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          DEBUG_ELEMENT_START (parse, &ebml, "BlockGroup");
          if ((ret = gst_ebml_read_master (&ebml, &id)) == GST_FLOW_OK) {
            ret = gst_matroska_parse_parse_blockgroup_or_simpleblock (parse,
                &ebml, parse->cluster_time, parse->cluster_offset, FALSE);
          }
          DEBUG_ELEMENT_STOP (parse, &ebml, "BlockGroup", ret);
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        case GST_MATROSKA_ID_SIMPLEBLOCK:
          if (!gst_matroska_parse_seek_block (parse))
            goto skip;
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          DEBUG_ELEMENT_START (parse, &ebml, "SimpleBlock");
          ret = gst_matroska_parse_parse_blockgroup_or_simpleblock (parse,
              &ebml, parse->cluster_time, parse->cluster_offset, TRUE);
          DEBUG_ELEMENT_STOP (parse, &ebml, "SimpleBlock", ret);
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        case GST_MATROSKA_ID_ATTACHMENTS:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          if (!parse->attachments_parsed) {
            ret = gst_matroska_parse_parse_attachments (parse, &ebml);
          }
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        case GST_MATROSKA_ID_TAGS:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          ret = gst_matroska_parse_parse_metadata (parse, &ebml);
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        case GST_MATROSKA_ID_CHAPTERS:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          ret = gst_matroska_parse_parse_chapters (parse, &ebml);
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        case GST_MATROSKA_ID_SEEKHEAD:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          ret = gst_matroska_parse_parse_contents (parse, &ebml);
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        case GST_MATROSKA_ID_CUES:
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          if (!parse->index_parsed) {
            ret = gst_matroska_parse_parse_index (parse, &ebml);
            /* only push based; delayed index building */
            if (ret == GST_FLOW_OK
                && parse->state == GST_MATROSKA_PARSE_STATE_SEEK) {
              GstEvent *event;

              GST_OBJECT_LOCK (parse);
              event = parse->seek_event;
              parse->seek_event = NULL;
              GST_OBJECT_UNLOCK (parse);

              g_assert (event);
              /* unlikely to fail, since we managed to seek to this point */
              if (!gst_matroska_parse_handle_seek_event (parse, NULL, event))
                goto seek_failed;
              /* resume data handling, main thread clear to seek again */
              GST_OBJECT_LOCK (parse);
              parse->state = GST_MATROSKA_PARSE_STATE_DATA;
              GST_OBJECT_UNLOCK (parse);
            }
          }
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
        case GST_MATROSKA_ID_POSITION:
        case GST_MATROSKA_ID_PREVSIZE:
        case GST_MATROSKA_ID_ENCRYPTEDBLOCK:
        case GST_MATROSKA_ID_SILENTTRACKS:
          GST_DEBUG_OBJECT (parse,
              "Skipping Cluster subelement 0x%x - ignoring", id);
          /* fall-through */
        default:
        skip:
          GST_DEBUG_OBJECT (parse, "skipping Element 0x%x", id);
          GST_READ_CHECK (gst_matroska_parse_take (parse, read, &ebml));
          gst_matroska_parse_output (parse, ebml.buf, FALSE);
          break;
      }
      break;
  }

  if (ret == GST_FLOW_PARSE)
    goto parse_failed;

exit:
  gst_ebml_read_clear (&ebml);
  return ret;

  /* ERRORS */
read_error:
  {
    /* simply exit, maybe not enough data yet */
    /* no ebml to clear if read error */
    return ret;
  }
parse_failed:
  {
    GST_ELEMENT_ERROR (parse, STREAM, DEMUX, (NULL),
        ("Failed to parse Element 0x%x", id));
    ret = GST_FLOW_ERROR;
    goto exit;
  }
not_streamable:
  {
    GST_ELEMENT_ERROR (parse, STREAM, DEMUX, (NULL),
        ("File layout does not permit streaming"));
    ret = GST_FLOW_ERROR;
    goto exit;
  }
#if 0
no_tracks:
  {
    GST_ELEMENT_ERROR (parse, STREAM, DEMUX, (NULL),
        ("No Tracks element found"));
    ret = GST_FLOW_ERROR;
    goto exit;
  }
#endif
invalid_header:
  {
    GST_ELEMENT_ERROR (parse, STREAM, DEMUX, (NULL), ("Invalid header"));
    ret = GST_FLOW_ERROR;
    goto exit;
  }
seek_failed:
  {
    GST_ELEMENT_ERROR (parse, STREAM, DEMUX, (NULL), ("Failed to seek"));
    ret = GST_FLOW_ERROR;
    goto exit;
  }
}

#if 0
static void
gst_matroska_parse_loop (GstPad * pad)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (GST_PAD_PARENT (pad));
  GstFlowReturn ret;
  guint32 id;
  guint64 length;
  guint needed;

  /* If we have to close a segment, send a new segment to do this now */
  if (G_LIKELY (parse->state == GST_MATROSKA_PARSE_STATE_DATA)) {
    if (G_UNLIKELY (parse->close_segment)) {
      gst_matroska_parse_send_event (parse, parse->close_segment);
      parse->close_segment = NULL;
    }
    if (G_UNLIKELY (parse->new_segment)) {
      gst_matroska_parse_send_event (parse, parse->new_segment);
      parse->new_segment = NULL;
    }
  }

  ret = gst_matroska_parse_peek_id_length_pull (parse, &id, &length, &needed);
  if (ret == GST_FLOW_UNEXPECTED)
    goto eos;
  if (ret != GST_FLOW_OK) {
    if (gst_matroska_parse_check_parse_error (parse))
      goto pause;
    else
      return;
  }

  GST_LOG_OBJECT (parse, "Offset %" G_GUINT64_FORMAT ", Element id 0x%x, "
      "size %" G_GUINT64_FORMAT ", needed %d", parse->offset, id,
      length, needed);

  ret = gst_matroska_parse_parse_id (parse, id, length, needed);
  if (ret == GST_FLOW_UNEXPECTED)
    goto eos;
  if (ret != GST_FLOW_OK)
    goto pause;

  /* check if we're at the end of a configured segment */
  if (G_LIKELY (parse->src->len)) {
    guint i;

    g_assert (parse->num_streams == parse->src->len);
    for (i = 0; i < parse->src->len; i++) {
      GstMatroskaTrackContext *context = g_ptr_array_index (parse->src, i);
      GST_DEBUG_OBJECT (context->pad, "pos %" GST_TIME_FORMAT,
          GST_TIME_ARGS (context->pos));
      if (context->eos == FALSE)
        goto next;
    }

    GST_INFO_OBJECT (parse, "All streams are EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto eos;
  }

next:
  if (G_UNLIKELY (parse->offset == gst_matroska_parse_get_length (parse))) {
    GST_LOG_OBJECT (parse, "Reached end of stream");
    ret = GST_FLOW_UNEXPECTED;
    goto eos;
  }

  return;

  /* ERRORS */
eos:
  {
    if (parse->segment.rate < 0.0) {
      ret = gst_matroska_parse_seek_to_previous_keyframe (parse);
      if (ret == GST_FLOW_OK)
        return;
    }
    /* fall-through */
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);
    gboolean push_eos = FALSE;

    GST_LOG_OBJECT (parse, "pausing task, reason %s", reason);
    parse->segment_running = FALSE;
    gst_pad_pause_task (parse->sinkpad);

    if (ret == GST_FLOW_UNEXPECTED) {
      /* perform EOS logic */

      /* Close the segment, i.e. update segment stop with the duration
       * if no stop was set */
      if (GST_CLOCK_TIME_IS_VALID (parse->last_stop_end) &&
          !GST_CLOCK_TIME_IS_VALID (parse->segment.stop)) {
        GstEvent *event =
            gst_event_new_new_segment_full (TRUE, parse->segment.rate,
            parse->segment.applied_rate, parse->segment.format,
            parse->segment.start,
            MAX (parse->last_stop_end, parse->segment.start),
            parse->segment.time);
        gst_matroska_parse_send_event (parse, event);
      }

      if (parse->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        gint64 stop;

        /* for segment playback we need to post when (in stream time)
         * we stopped, this is either stop (when set) or the duration. */
        if ((stop = parse->segment.stop) == -1)
          stop = parse->last_stop_end;

        GST_LOG_OBJECT (parse, "Sending segment done, at end of segment");
        gst_element_post_message (GST_ELEMENT (parse),
            gst_message_new_segment_done (GST_OBJECT (parse), GST_FORMAT_TIME,
                stop));
      } else {
        push_eos = TRUE;
      }
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_UNEXPECTED) {
      /* for fatal errors we post an error message */
      GST_ELEMENT_ERROR (parse, STREAM, FAILED, (NULL),
          ("stream stopped, reason %s", reason));
      push_eos = TRUE;
    }
    if (push_eos) {
      /* send EOS, and prevent hanging if no streams yet */
      GST_LOG_OBJECT (parse, "Sending EOS, at end of stream");
      if (!gst_matroska_parse_send_event (parse, gst_event_new_eos ()) &&
          (ret == GST_FLOW_UNEXPECTED)) {
        GST_ELEMENT_ERROR (parse, STREAM, DEMUX,
            (NULL), ("got eos but no streams (yet)"));
      }
    }
    return;
  }
}
#endif

/*
 * Create and push a flushing seek event upstream
 */
static gboolean
perform_seek_to_offset (GstMatroskaParse * parse, guint64 offset)
{
  GstEvent *event;
  gboolean res = 0;

  GST_DEBUG_OBJECT (parse, "Seeking to %" G_GUINT64_FORMAT, offset);

  event =
      gst_event_new_seek (1.0, GST_FORMAT_BYTES,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, offset,
      GST_SEEK_TYPE_NONE, -1);

  res = gst_pad_push_event (parse->sinkpad, event);

  /* newsegment event will update offset */
  return res;
}

static const guint8 *
gst_matroska_parse_peek_adapter (GstMatroskaParse * parse, guint peek)
{
  return gst_adapter_peek (parse->adapter, peek);
}

static GstFlowReturn
gst_matroska_parse_peek_id_length_push (GstMatroskaParse * parse, guint32 * _id,
    guint64 * _length, guint * _needed)
{
  return gst_ebml_peek_id_length (_id, _length, _needed,
      (GstPeekData) gst_matroska_parse_peek_adapter, (gpointer) parse,
      GST_ELEMENT_CAST (parse), parse->offset);
}

static GstFlowReturn
gst_matroska_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (GST_PAD_PARENT (pad));
  guint available;
  GstFlowReturn ret = GST_FLOW_OK;
  guint needed = 0;
  guint32 id;
  guint64 length;

  if (G_UNLIKELY (GST_BUFFER_IS_DISCONT (buffer))) {
    GST_DEBUG_OBJECT (parse, "got DISCONT");
    gst_adapter_clear (parse->adapter);
    GST_OBJECT_LOCK (parse);
    gst_matroska_parse_reset_streams (parse, GST_CLOCK_TIME_NONE, FALSE);
    GST_OBJECT_UNLOCK (parse);
  }

  gst_adapter_push (parse->adapter, buffer);
  buffer = NULL;

next:
  available = gst_adapter_available (parse->adapter);

  ret = gst_matroska_parse_peek_id_length_push (parse, &id, &length, &needed);
  if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_UNEXPECTED))
    return ret;

  GST_LOG_OBJECT (parse, "Offset %" G_GUINT64_FORMAT ", Element id 0x%x, "
      "size %" G_GUINT64_FORMAT ", needed %d, available %d", parse->offset, id,
      length, needed, available);

  if (needed > available)
    return GST_FLOW_OK;

  ret = gst_matroska_parse_parse_id (parse, id, length, needed);
  if (ret == GST_FLOW_UNEXPECTED) {
    /* need more data */
    return GST_FLOW_OK;
  } else if (ret != GST_FLOW_OK) {
    return ret;
  } else
    goto next;
}

static gboolean
gst_matroska_parse_handle_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (parse,
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
      GST_DEBUG_OBJECT (parse,
          "received format %d newsegment %" GST_SEGMENT_FORMAT, format,
          &segment);

      if (parse->state < GST_MATROSKA_PARSE_STATE_DATA) {
        GST_DEBUG_OBJECT (parse, "still starting");
        goto exit;
      }

      /* we only expect a BYTE segment, e.g. following a seek */
      if (format != GST_FORMAT_BYTES) {
        GST_DEBUG_OBJECT (parse, "unsupported segment format, ignoring");
        goto exit;
      }

      GST_DEBUG_OBJECT (parse, "clearing segment state");
      /* clear current segment leftover */
      gst_adapter_clear (parse->adapter);
      /* and some streaming setup */
      parse->offset = start;
      /* do not know where we are;
       * need to come across a cluster and generate newsegment */
      parse->segment.last_stop = GST_CLOCK_TIME_NONE;
      parse->cluster_time = GST_CLOCK_TIME_NONE;
      parse->cluster_offset = 0;
      parse->need_newsegment = TRUE;
      /* but keep some of the upstream segment */
      parse->segment.rate = rate;
    exit:
      /* chain will send initial newsegment after pads have been added,
       * or otherwise come up with one */
      GST_DEBUG_OBJECT (parse, "eating event");
      gst_event_unref (event);
      res = TRUE;
      break;
    }
    case GST_EVENT_EOS:
    {
      if (parse->state != GST_MATROSKA_PARSE_STATE_DATA) {
        gst_event_unref (event);
        GST_ELEMENT_ERROR (parse, STREAM, DEMUX,
            (NULL), ("got eos and didn't receive a complete header object"));
      } else if (parse->num_streams == 0) {
        GST_ELEMENT_ERROR (parse, STREAM, DEMUX,
            (NULL), ("got eos but no streams (yet)"));
      } else {
        gst_matroska_parse_send_event (parse, event);
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      gst_adapter_clear (parse->adapter);
      GST_OBJECT_LOCK (parse);
      gst_matroska_parse_reset_streams (parse, GST_CLOCK_TIME_NONE, TRUE);
      GST_OBJECT_UNLOCK (parse);
      parse->segment.last_stop = GST_CLOCK_TIME_NONE;
      parse->cluster_time = GST_CLOCK_TIME_NONE;
      parse->cluster_offset = 0;
      /* fall-through */
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}

static void
gst_matroska_parse_set_index (GstElement * element, GstIndex * index)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (element);

  GST_OBJECT_LOCK (parse);
  if (parse->element_index)
    gst_object_unref (parse->element_index);
  parse->element_index = index ? gst_object_ref (index) : NULL;
  GST_OBJECT_UNLOCK (parse);
  GST_DEBUG_OBJECT (parse, "Set index %" GST_PTR_FORMAT, parse->element_index);
}

static GstIndex *
gst_matroska_parse_get_index (GstElement * element)
{
  GstIndex *result = NULL;
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (element);

  GST_OBJECT_LOCK (parse);
  if (parse->element_index)
    result = gst_object_ref (parse->element_index);
  GST_OBJECT_UNLOCK (parse);

  GST_DEBUG_OBJECT (parse, "Returning index %" GST_PTR_FORMAT, result);

  return result;
}

static GstStateChangeReturn
gst_matroska_parse_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMatroskaParse *parse = GST_MATROSKA_PARSE (element);
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
      gst_matroska_parse_reset (GST_ELEMENT (parse));
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_matroska_parse_plugin_init (GstPlugin * plugin)
{
  gst_riff_init ();

  /* create an elementfactory for the matroska_parse element */
  if (!gst_element_register (plugin, "matroskaparse",
          GST_RANK_NONE, GST_TYPE_MATROSKA_PARSE))
    return FALSE;

  return TRUE;
}
