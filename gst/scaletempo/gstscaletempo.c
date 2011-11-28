/*
 * GStreamer
 * Copyright (C) 2008 Rov Juvano <rovjuvano@users.sourceforge.net>
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

/**
 * SECTION:element-scaletempo
 *
 * Scale tempo while maintaining pitch
 * (WSOLA-like technique with cross correlation)
 * Inspired by SoundTouch library by Olli Parviainen
 *
 * Use Sceletempo to apply playback rates without the chipmunk effect.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * <para>
 * |[
 * filesrc location=media.ext ! decodebin name=d \
 *     d. ! queue ! audioconvert ! audioresample ! scaletempo ! audioconvert ! audioresample ! autoaudiosink \
 *     d. ! queue ! ffmpegcolorspace ! autovideosink
 * ]|
 * OR
 * |[
 * playbin uri=... audio_sink="scaletempo ! audioconvert ! audioresample ! autoaudiosink"
 * ]|
 * When an application sends a seek event with rate != 1.0, Scaletempo applies
 * the rate change by scaling the tempo without scaling the pitch.
 *
 * Scaletempo works by producing audio in constant sized chunks
 * (#GstScaletempo:stride) but consuming chunks proportional to the playback
 * rate.
 *
 * Scaletempo then smooths the output by blending the end of one stride with
 * the next (#GstScaletempo:overlap).
 *
 * Scaletempo smooths the overlap further by searching within the input buffer
 * for the best overlap position.  Scaletempo uses a statistical cross
 * correlation (roughly a dot-product).  Scaletempo consumes most of its CPU
 * cycles here. One can use the #GstScaletempo:search propery to tune how far
 * the algoritm looks.
 * </para>
 * </refsect2>
 */

/*
 * Note: frame = audio key unit (i.e. one sample for each channel)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <string.h>             /* for memset */

#include "gstscaletempo.h"

GST_DEBUG_CATEGORY_STATIC (gst_scaletempo_debug);
#define GST_CAT_DEFAULT gst_scaletempo_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_RATE,
  PROP_STRIDE,
  PROP_OVERLAP,
  PROP_SEARCH,
};

#define SUPPORTED_CAPS \
GST_STATIC_CAPS ( \
    "audio/x-raw-float, " \
      "rate = (int) [ 1, MAX ], "       \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 32;" \
    "audio/x-raw-int, " \
      "rate = (int) [ 1, MAX ], " \
      "channels = (int) [ 1, MAX ], " \
      "endianness = (int) BYTE_ORDER, " \
      "width = (int) 16, " \
      "depth = (int) 16, " \
      "signed = (boolean) true;" \
)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    SUPPORTED_CAPS);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    SUPPORTED_CAPS);

#define DEBUG_INIT(bla) GST_DEBUG_CATEGORY_INIT (gst_scaletempo_debug, "scaletempo", 0, "scaletempo element");

GST_BOILERPLATE_FULL (GstScaletempo, gst_scaletempo, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

typedef struct _GstScaletempoPrivate
{
  gdouble scale;
  /* parameters */
  guint ms_stride;
  gdouble percent_overlap;
  guint ms_search;
  /* caps */
  gboolean use_int;
  guint samples_per_frame;      /* AKA number of channels */
  guint bytes_per_sample;
  guint bytes_per_frame;
  guint sample_rate;
  /* stride */
  gdouble frames_stride_scaled;
  gdouble frames_stride_error;
  guint bytes_stride;
  gdouble bytes_stride_scaled;
  guint bytes_queue_max;
  guint bytes_queued;
  guint bytes_to_slide;
  gint8 *buf_queue;
  /* overlap */
  guint samples_overlap;
  guint samples_standing;
  guint bytes_overlap;
  guint bytes_standing;
  gpointer buf_overlap;
  gpointer table_blend;
  void (*output_overlap) (GstScaletempo * scaletempo, gpointer out_buf,
      guint bytes_off);
  /* best overlap */
  guint frames_search;
  gpointer buf_pre_corr;
  gpointer table_window;
    guint (*best_overlap_offset) (GstScaletempo * scaletempo);
  /* gstreamer */
  gint64 segment_start;
  /* threads */
  gboolean reinit_buffers;
} GstScaletempoPrivate;
#define GST_SCALETEMPO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_SCALETEMPO, GstScaletempoPrivate))


static guint
best_overlap_offset_float (GstScaletempo * scaletempo)
{
  GstScaletempoPrivate *p = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  gfloat *pw, *po, *ppc, *search_start;
  gfloat best_corr = G_MININT;
  guint best_off = 0;
  gint i, off;

  pw = p->table_window;
  po = p->buf_overlap;
  po += p->samples_per_frame;
  ppc = p->buf_pre_corr;
  for (i = p->samples_per_frame; i < p->samples_overlap; i++) {
    *ppc++ = *pw++ * *po++;
  }

  search_start = (gfloat *) p->buf_queue + p->samples_per_frame;
  for (off = 0; off < p->frames_search; off++) {
    gfloat corr = 0;
    gfloat *ps = search_start;
    ppc = p->buf_pre_corr;
    for (i = p->samples_per_frame; i < p->samples_overlap; i++) {
      corr += *ppc++ * *ps++;
    }
    if (corr > best_corr) {
      best_corr = corr;
      best_off = off;
    }
    search_start += p->samples_per_frame;
  }

  return best_off * p->bytes_per_frame;
}

/* buffer padding for loop optimization: sizeof(gint32) * (loop_size - 1) */
#define UNROLL_PADDING (4*3)
static guint
best_overlap_offset_s16 (GstScaletempo * scaletempo)
{
  GstScaletempoPrivate *p = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  gint32 *pw, *ppc;
  gint16 *po, *search_start;
  gint64 best_corr = G_MININT64;
  guint best_off = 0;
  guint off;
  glong i;

  pw = p->table_window;
  po = p->buf_overlap;
  po += p->samples_per_frame;
  ppc = p->buf_pre_corr;
  for (i = p->samples_per_frame; i < p->samples_overlap; i++) {
    *ppc++ = (*pw++ * *po++) >> 15;
  }

  search_start = (gint16 *) p->buf_queue + p->samples_per_frame;
  for (off = 0; off < p->frames_search; off++) {
    gint64 corr = 0;
    gint16 *ps = search_start;
    ppc = p->buf_pre_corr;
    ppc += p->samples_overlap - p->samples_per_frame;
    ps += p->samples_overlap - p->samples_per_frame;
    i = -((glong) p->samples_overlap - (glong) p->samples_per_frame);
    do {
      corr += ppc[i + 0] * ps[i + 0];
      corr += ppc[i + 1] * ps[i + 1];
      corr += ppc[i + 2] * ps[i + 2];
      corr += ppc[i + 3] * ps[i + 3];
      i += 4;
    } while (i < 0);
    if (corr > best_corr) {
      best_corr = corr;
      best_off = off;
    }
    search_start += p->samples_per_frame;
  }

  return best_off * p->bytes_per_frame;
}

static void
output_overlap_float (GstScaletempo * scaletempo,
    gpointer buf_out, guint bytes_off)
{
  GstScaletempoPrivate *p = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  gfloat *pout = buf_out;
  gfloat *pb = p->table_blend;
  gfloat *po = p->buf_overlap;
  gfloat *pin = (gfloat *) (p->buf_queue + bytes_off);
  gint i;
  for (i = 0; i < p->samples_overlap; i++) {
    *pout++ = *po - *pb++ * (*po - *pin++);
    po++;
  }
}

static void
output_overlap_s16 (GstScaletempo * scaletempo,
    gpointer buf_out, guint bytes_off)
{
  GstScaletempoPrivate *p = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  gint16 *pout = buf_out;
  gint32 *pb = p->table_blend;
  gint16 *po = p->buf_overlap;
  gint16 *pin = (gint16 *) (p->buf_queue + bytes_off);
  gint i;
  for (i = 0; i < p->samples_overlap; i++) {
    *pout++ = *po - ((*pb++ * (*po - *pin++)) >> 16);
    po++;
  }
}

static guint
fill_queue (GstScaletempo * scaletempo, GstBuffer * buf_in, guint offset)
{
  GstScaletempoPrivate *p = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  guint bytes_in = GST_BUFFER_SIZE (buf_in) - offset;
  guint offset_unchanged = offset;

  if (p->bytes_to_slide > 0) {
    if (p->bytes_to_slide < p->bytes_queued) {
      guint bytes_in_move = p->bytes_queued - p->bytes_to_slide;
      memmove (p->buf_queue, p->buf_queue + p->bytes_to_slide, bytes_in_move);
      p->bytes_to_slide = 0;
      p->bytes_queued = bytes_in_move;
    } else {
      guint bytes_in_skip;
      p->bytes_to_slide -= p->bytes_queued;
      bytes_in_skip = MIN (p->bytes_to_slide, bytes_in);
      p->bytes_queued = 0;
      p->bytes_to_slide -= bytes_in_skip;
      offset += bytes_in_skip;
      bytes_in -= bytes_in_skip;
    }
  }

  if (bytes_in > 0) {
    guint bytes_in_copy = MIN (p->bytes_queue_max - p->bytes_queued, bytes_in);
    memcpy (p->buf_queue + p->bytes_queued,
        GST_BUFFER_DATA (buf_in) + offset, bytes_in_copy);
    p->bytes_queued += bytes_in_copy;
    offset += bytes_in_copy;
  }

  return offset - offset_unchanged;
}

static void
reinit_buffers (GstScaletempo * scaletempo)
{
  GstScaletempoPrivate *p = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  gint i, j;
  guint frames_overlap;
  guint new_size;

  guint frames_stride = p->ms_stride * p->sample_rate / 1000.0;
  p->bytes_stride = frames_stride * p->bytes_per_frame;

  /* overlap */
  frames_overlap = frames_stride * p->percent_overlap;
  if (frames_overlap < 1) {     /* if no overlap */
    p->bytes_overlap = 0;
    p->bytes_standing = p->bytes_stride;
    p->samples_standing = p->bytes_standing / p->bytes_per_sample;
    p->output_overlap = NULL;
  } else {
    guint prev_overlap = p->bytes_overlap;
    p->bytes_overlap = frames_overlap * p->bytes_per_frame;
    p->samples_overlap = frames_overlap * p->samples_per_frame;
    p->bytes_standing = p->bytes_stride - p->bytes_overlap;
    p->samples_standing = p->bytes_standing / p->bytes_per_sample;
    p->buf_overlap = g_realloc (p->buf_overlap, p->bytes_overlap);
    p->table_blend = g_realloc (p->table_blend, p->samples_overlap * 4);        /* sizeof (gint32|gfloat) */
    if (p->bytes_overlap > prev_overlap) {
      memset ((guint8 *) p->buf_overlap + prev_overlap, 0,
          p->bytes_overlap - prev_overlap);
    }
    if (p->use_int) {
      gint32 *pb = p->table_blend;
      gint64 blend = 0;
      for (i = 0; i < frames_overlap; i++) {
        gint32 v = blend / frames_overlap;
        for (j = 0; j < p->samples_per_frame; j++) {
          *pb++ = v;
        }
        blend += 65535;         /* 2^16 */
      }
      p->output_overlap = output_overlap_s16;
    } else {
      gfloat *pb = p->table_blend;
      gfloat t = (gfloat) frames_overlap;
      for (i = 0; i < frames_overlap; i++) {
        gfloat v = i / t;
        for (j = 0; j < p->samples_per_frame; j++) {
          *pb++ = v;
        }
      }
      p->output_overlap = output_overlap_float;
    }
  }

  /* best overlap */
  p->frames_search =
      (frames_overlap <= 1) ? 0 : p->ms_search * p->sample_rate / 1000.0;
  if (p->frames_search < 1) {   /* if no search */
    p->best_overlap_offset = NULL;
  } else {
    guint bytes_pre_corr = (p->samples_overlap - p->samples_per_frame) * 4;     /* sizeof (gint32|gfloat) */
    p->buf_pre_corr =
        g_realloc (p->buf_pre_corr, bytes_pre_corr + UNROLL_PADDING);
    p->table_window = g_realloc (p->table_window, bytes_pre_corr);
    if (p->use_int) {
      gint64 t = frames_overlap;
      gint32 n = 8589934588LL / (t * t);        /* 4 * (2^31 - 1) / t^2 */
      gint32 *pw;

      memset ((guint8 *) p->buf_pre_corr + bytes_pre_corr, 0, UNROLL_PADDING);
      pw = p->table_window;
      for (i = 1; i < frames_overlap; i++) {
        gint32 v = (i * (t - i) * n) >> 15;
        for (j = 0; j < p->samples_per_frame; j++) {
          *pw++ = v;
        }
      }
      p->best_overlap_offset = best_overlap_offset_s16;
    } else {
      gfloat *pw = p->table_window;
      for (i = 1; i < frames_overlap; i++) {
        gfloat v = i * (frames_overlap - i);
        for (j = 0; j < p->samples_per_frame; j++) {
          *pw++ = v;
        }
      }
      p->best_overlap_offset = best_overlap_offset_float;
    }
  }

  new_size =
      (p->frames_search + frames_stride + frames_overlap) * p->bytes_per_frame;
  if (p->bytes_queued > new_size) {
    if (p->bytes_to_slide > p->bytes_queued) {
      p->bytes_to_slide -= p->bytes_queued;
      p->bytes_queued = 0;
    } else {
      guint new_queued = MIN (p->bytes_queued - p->bytes_to_slide, new_size);
      memmove (p->buf_queue,
          p->buf_queue + p->bytes_queued - new_queued, new_queued);
      p->bytes_to_slide = 0;
      p->bytes_queued = new_queued;
    }
  }
  p->bytes_queue_max = new_size;
  p->buf_queue = g_realloc (p->buf_queue, p->bytes_queue_max);

  p->bytes_stride_scaled = p->bytes_stride * p->scale;
  p->frames_stride_scaled = p->bytes_stride_scaled / p->bytes_per_frame;

  GST_DEBUG
      ("%.3f scale, %.3f stride_in, %i stride_out, %i standing, %i overlap, %i search, %i queue, %s mode",
      p->scale, p->frames_stride_scaled,
      (gint) (p->bytes_stride / p->bytes_per_frame),
      (gint) (p->bytes_standing / p->bytes_per_frame),
      (gint) (p->bytes_overlap / p->bytes_per_frame), p->frames_search,
      (gint) (p->bytes_queue_max / p->bytes_per_frame),
      (p->use_int ? "s16" : "float"));

  p->reinit_buffers = FALSE;
}


/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_scaletempo_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstScaletempo *scaletempo = GST_SCALETEMPO (trans);
  GstScaletempoPrivate *p = GST_SCALETEMPO_GET_PRIVATE (scaletempo);

  gint8 *pout = (gint8 *) GST_BUFFER_DATA (outbuf);
  guint offset_in = fill_queue (scaletempo, inbuf, 0);
  guint bytes_out = 0;
  while (p->bytes_queued >= p->bytes_queue_max) {
    guint bytes_off = 0;
    gdouble frames_to_slide;
    guint frames_to_stride_whole;

    // output stride
    if (p->output_overlap) {
      if (p->best_overlap_offset) {
        bytes_off = p->best_overlap_offset (scaletempo);
      }
      p->output_overlap (scaletempo, pout, bytes_off);
    }
    memcpy (pout + p->bytes_overlap,
        p->buf_queue + bytes_off + p->bytes_overlap, p->bytes_standing);
    pout += p->bytes_stride;
    bytes_out += p->bytes_stride;

    // input stride
    memcpy (p->buf_overlap,
        p->buf_queue + bytes_off + p->bytes_stride, p->bytes_overlap);
    frames_to_slide = p->frames_stride_scaled + p->frames_stride_error;
    frames_to_stride_whole = (gint) frames_to_slide;
    p->bytes_to_slide = frames_to_stride_whole * p->bytes_per_frame;
    p->frames_stride_error = frames_to_slide - frames_to_stride_whole;

    offset_in += fill_queue (scaletempo, inbuf, offset_in);
  }

  GST_BUFFER_SIZE (outbuf) = bytes_out;
  GST_BUFFER_TIMESTAMP (outbuf) =
      (GST_BUFFER_TIMESTAMP (outbuf) - p->segment_start) / p->scale +
      p->segment_start;
  //GST_BUFFER_DURATION (outbuf)  = bytes_out * GST_SECOND / (p->bytes_per_frame * p->sample_rate);
  return GST_FLOW_OK;
}

static gboolean
gst_scaletempo_transform_size (GstBaseTransform * trans,
    GstPadDirection direction,
    GstCaps * caps, guint size, GstCaps * othercaps, guint * othersize)
{
  if (direction == GST_PAD_SINK) {
    GstScaletempo *scaletempo = GST_SCALETEMPO (trans);
    GstScaletempoPrivate *priv = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
    gint bytes_to_out;

    if (priv->reinit_buffers)
      reinit_buffers (scaletempo);

    bytes_to_out = size + priv->bytes_queued - priv->bytes_to_slide;
    if (bytes_to_out < (gint) priv->bytes_queue_max) {
      *othersize = 0;
    } else {
      /* while (total_buffered - stride_length * n >= queue_max) n++ */
      *othersize = priv->bytes_stride * ((guint) (
              (bytes_to_out - priv->bytes_queue_max +
                  /* rounding protection */ priv->bytes_per_frame)
              / priv->bytes_stride_scaled) + 1);
    }

    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_scaletempo_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    GstScaletempo *scaletempo = GST_SCALETEMPO (trans);
    GstScaletempoPrivate *priv = GST_SCALETEMPO_GET_PRIVATE (scaletempo);

    gboolean update;
    gdouble rate, applied_rate;
    GstFormat format;
    gint64 start, stop, position;

    gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
        &format, &start, &stop, &position);

    if (priv->scale != rate) {
      if (ABS (rate - 1.0) < 1e-10) {
        priv->scale = 1.0;
        gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (scaletempo),
            TRUE);
      } else {
        gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (scaletempo),
            FALSE);
        priv->scale = rate;
        priv->bytes_stride_scaled = priv->bytes_stride * priv->scale;
        priv->frames_stride_scaled =
            priv->bytes_stride_scaled / priv->bytes_per_frame;
        GST_DEBUG ("%.3f scale, %.3f stride_in, %i stride_out", priv->scale,
            priv->frames_stride_scaled,
            (gint) (priv->bytes_stride / priv->bytes_per_frame));

        priv->bytes_to_slide = 0;
      }
    }

    if (priv->scale != 1.0) {
      priv->segment_start = start;
      applied_rate = priv->scale;
      rate = 1.0;
      //gst_event_unref (event);

      if (stop != -1) {
        stop = (stop - start) / applied_rate + start;
      }

      event = gst_event_new_new_segment_full (update, rate, applied_rate,
          format, start, stop, position);
      gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (trans), event);
      return FALSE;
    }
  }
  return parent_class->event (trans, event);
}

static gboolean
gst_scaletempo_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps)
{
  GstScaletempo *scaletempo = GST_SCALETEMPO (trans);
  GstScaletempoPrivate *priv = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  GstStructure *s = gst_caps_get_structure (incaps, 0);

  gint width, bps, nch, rate;
  gboolean use_int;
  const gchar *type = gst_structure_get_name (s);
  if (g_str_equal (type, "audio/x-raw-int")) {
    use_int = TRUE;
    gst_structure_get_int (s, "depth", &width);
  } else if (g_str_equal (type, "audio/x-raw-float")) {
    use_int = FALSE;
    gst_structure_get_int (s, "width", &width);
  } else {
    return FALSE;
  }
  bps = width / 8;

  gst_structure_get_int (s, "channels", &nch);
  gst_structure_get_int (s, "rate", &rate);

  GST_DEBUG ("caps: %s seek, "
      "%5" G_GUINT32_FORMAT " rate, "
      "%2" G_GUINT32_FORMAT " nch, "
      "%2" G_GUINT32_FORMAT " bps", type, rate, nch, bps);

  if (rate != priv->sample_rate
      || nch != priv->samples_per_frame
      || bps != priv->bytes_per_sample || use_int != priv->use_int) {
    priv->sample_rate = rate;
    priv->samples_per_frame = nch;
    priv->bytes_per_sample = bps;
    priv->bytes_per_frame = nch * bps;
    priv->use_int = use_int;
    priv->reinit_buffers = TRUE;
  }

  return TRUE;
}


/* GObject vmethod implementations */
static void
gst_scaletempo_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstScaletempo *scaletempo = GST_SCALETEMPO (object);
  GstScaletempoPrivate *priv = GST_SCALETEMPO_GET_PRIVATE (scaletempo);

  switch (prop_id) {
    case PROP_RATE:
      g_value_set_double (value, priv->scale);
      break;
    case PROP_STRIDE:
      g_value_set_uint (value, priv->ms_stride);
      break;
    case PROP_OVERLAP:
      g_value_set_double (value, priv->percent_overlap);
      break;
    case PROP_SEARCH:
      g_value_set_uint (value, priv->ms_search);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_scaletempo_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstScaletempo *scaletempo = GST_SCALETEMPO (object);
  GstScaletempoPrivate *priv = GST_SCALETEMPO_GET_PRIVATE (scaletempo);

  switch (prop_id) {
    case PROP_STRIDE:{
      guint new_value = g_value_get_uint (value);
      if (priv->ms_stride != new_value) {
        priv->ms_stride = new_value;
        priv->reinit_buffers = TRUE;
      }
      break;
    }
    case PROP_OVERLAP:{
      gdouble new_value = g_value_get_double (value);
      if (priv->percent_overlap != new_value) {
        priv->percent_overlap = new_value;
        priv->reinit_buffers = TRUE;
      }
      break;
    }
    case PROP_SEARCH:{
      guint new_value = g_value_get_uint (value);
      if (priv->ms_search != new_value) {
        priv->ms_search = new_value;
        priv->reinit_buffers = TRUE;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_scaletempo_base_init (gpointer klass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
  gst_element_class_set_details_simple (element_class, "Scaletempo",
      "Filter/Effect/Rate",
      "Sync audio tempo with playback rate",
      "Rov Juvano <rovjuvano@users.sourceforge.net>");
}

static void
gst_scaletempo_class_init (GstScaletempoClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *basetransform_class = GST_BASE_TRANSFORM_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstScaletempoPrivate));

  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_scaletempo_get_property);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_scaletempo_set_property);

  g_object_class_install_property (gobject_class, PROP_RATE,
      g_param_spec_double ("rate", "Playback Rate", "Current playback rate",
          G_MININT, G_MAXINT, 1.0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STRIDE,
      g_param_spec_uint ("stride", "Stride Length",
          "Length in milliseconds to output each stride", 1, 5000, 30,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OVERLAP,
      g_param_spec_double ("overlap", "Overlap Length",
          "Percentage of stride to overlap", 0, 1, .2,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SEARCH,
      g_param_spec_uint ("search", "Search Length",
          "Length in milliseconds to search for best overlap position", 0, 500,
          14, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  basetransform_class->event = GST_DEBUG_FUNCPTR (gst_scaletempo_sink_event);
  basetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_scaletempo_set_caps);
  basetransform_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_scaletempo_transform_size);
  basetransform_class->transform = GST_DEBUG_FUNCPTR (gst_scaletempo_transform);
}

static void
gst_scaletempo_init (GstScaletempo * scaletempo, GstScaletempoClass * klass)
{
  GstScaletempoPrivate *priv = GST_SCALETEMPO_GET_PRIVATE (scaletempo);
  /* defaults */
  priv->ms_stride = 30;
  priv->percent_overlap = .2;
  priv->ms_search = 14;

  /* uninitialized */
  priv->scale = 0;
  priv->sample_rate = 0;
  priv->frames_stride_error = 0;
  priv->bytes_stride = 0;
  priv->bytes_queued = 0;
  priv->bytes_to_slide = 0;
  priv->segment_start = 0;
}
