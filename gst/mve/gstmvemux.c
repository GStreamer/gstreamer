/* Interplay MVE multiplexer plugin for GStreamer
 * Copyright (C) 2006 Jens Granseuer <jensgr@gmx.net>
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

/*
gst-launch-0.10 filesrc location=movie.mve ! mvedemux name=d !
    video/x-raw-rgb ! mvemux quick=true name=m !
    filesink location=test.mve d. ! audio/x-raw-int ! m.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/glib-compat-private.h>
#include "gstmvemux.h"
#include "mve.h"

GST_DEBUG_CATEGORY_STATIC (mvemux_debug);
#define GST_CAT_DEFAULT mvemux_debug

static const char mve_preamble[] = MVE_PREAMBLE;

enum
{
  ARG_0,
  ARG_AUDIO_COMPRESSION,
  ARG_VIDEO_QUICK_ENCODING,
  ARG_VIDEO_SCREEN_WIDTH,
  ARG_VIDEO_SCREEN_HEIGHT
};

#define MVE_MUX_DEFAULT_COMPRESSION    FALSE
#define MVE_MUX_DEFAULT_SCREEN_WIDTH   640
#define MVE_MUX_DEFAULT_SCREEN_HEIGHT  480

enum MveMuxState
{
  MVE_MUX_STATE_INITIAL,        /* initial state */
  MVE_MUX_STATE_CONNECTED,      /* linked, caps set, header not written */
  MVE_MUX_STATE_PREBUFFER,      /* prebuffering audio data */
  MVE_MUX_STATE_MOVIE,          /* writing the movie */
  MVE_MUX_STATE_EOS
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mve")
    );

static GstStaticPadTemplate video_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "width = (int) [ 24, 1600 ], "
        "height = (int) [ 24, 1200 ], "
        "framerate = (fraction) [ 1, MAX ], "
        "bpp = (int) 16, "
        "depth = (int) 15, "
        "endianness = (int) BYTE_ORDER, "
        "red_mask = (int) 31744, "
        "green_mask = (int) 992, "
        "blue_mask = (int) 31; "
        "video/x-raw-rgb, "
        "bpp = (int) 8, "
        "depth = (int) 8, "
        "width = (int) [ 24, 1600 ], "
        "height = (int) [ 24, 1200 ], "
        "framerate = (fraction) [ 1, MAX ], " "endianness = (int) BYTE_ORDER"));

static GstStaticPadTemplate audio_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 8, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ], "
        "depth = (int) 8, "
        "signed = (boolean) false; "
        "audio/x-raw-int, "
        "width = (int) 16, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ], "
        "depth = (int) 16, "
        "signed = (boolean) true, " "endianness = (int) BYTE_ORDER"));

static void gst_mve_mux_base_init (GstMveMuxClass * klass);
static void gst_mve_mux_class_init (GstMveMuxClass * klass);
static void gst_mve_mux_init (GstMveMux * mvemux);

static GstElementClass *parent_class = NULL;

static void
gst_mve_mux_reset (GstMveMux * mvemux)
{
  mvemux->state = MVE_MUX_STATE_INITIAL;
  mvemux->stream_time = 0;
  mvemux->stream_offset = 0;
  mvemux->timer = 0;

  mvemux->frame_duration = GST_CLOCK_TIME_NONE;
  mvemux->width = 0;
  mvemux->height = 0;
  mvemux->screen_width = MVE_MUX_DEFAULT_SCREEN_WIDTH;
  mvemux->screen_height = MVE_MUX_DEFAULT_SCREEN_HEIGHT;
  mvemux->bpp = 0;
  mvemux->video_frames = 0;
  mvemux->pal_changed = FALSE;
  mvemux->pal_first_color = 0;
  mvemux->pal_colors = MVE_PALETTE_COUNT;
  mvemux->quick_encoding = TRUE;

  mvemux->bps = 0;
  mvemux->rate = 0;
  mvemux->channels = 0;
  mvemux->compression = MVE_MUX_DEFAULT_COMPRESSION;
  mvemux->next_ts = 0;
  mvemux->max_ts = 0;
  mvemux->spf = 0;
  mvemux->lead_frames = 0;
  mvemux->audio_frames = 0;

  mvemux->chunk_has_palette = FALSE;
  mvemux->chunk_has_audio = FALSE;

  mvemux->audio_pad_eos = TRUE;
  mvemux->video_pad_eos = TRUE;

  g_free (mvemux->chunk_code_map);
  mvemux->chunk_code_map = NULL;

  if (mvemux->chunk_video != NULL) {
    g_byte_array_free (mvemux->chunk_video, TRUE);
    mvemux->chunk_video = NULL;
  }

  if (mvemux->chunk_audio != NULL) {
    g_byte_array_free (mvemux->chunk_audio, TRUE);
    mvemux->chunk_audio = NULL;
  }

  if (mvemux->last_frame != NULL) {
    gst_buffer_unref (mvemux->last_frame);
    mvemux->last_frame = NULL;
  }

  if (mvemux->second_last_frame != NULL) {
    gst_buffer_unref (mvemux->second_last_frame);
    mvemux->second_last_frame = NULL;
  }

  if (mvemux->audio_buffer != NULL) {
    g_queue_foreach (mvemux->audio_buffer, (GFunc) gst_mini_object_unref, NULL);
    g_queue_free (mvemux->audio_buffer);
  }
  mvemux->audio_buffer = g_queue_new ();

  if (mvemux->video_buffer != NULL) {
    g_queue_foreach (mvemux->video_buffer, (GFunc) gst_mini_object_unref, NULL);
    g_queue_free (mvemux->video_buffer);
  }
  mvemux->video_buffer = g_queue_new ();
}

static void
gst_mve_mux_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  GstMveMux *mvemux = GST_MVE_MUX (data);

  if (pad == mvemux->audiosink) {
    mvemux->audio_pad_connected = TRUE;
  } else if (pad == mvemux->videosink) {
    mvemux->video_pad_connected = TRUE;
  } else {
    g_assert_not_reached ();
  }

  GST_DEBUG_OBJECT (mvemux, "pad '%s' connected", GST_PAD_NAME (pad));
}

static void
gst_mve_mux_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  GstMveMux *mvemux = GST_MVE_MUX (data);

  if (pad == mvemux->audiosink) {
    mvemux->audio_pad_connected = FALSE;
  } else if (pad == mvemux->videosink) {
    mvemux->video_pad_connected = FALSE;
  } else {
    g_assert_not_reached ();
  }

  GST_DEBUG_OBJECT (mvemux, "pad '%s' unlinked", GST_PAD_NAME (pad));
}

static void
gst_mve_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstMveMux *mvemux;

  g_return_if_fail (GST_IS_MVE_MUX (object));
  mvemux = GST_MVE_MUX (object);

  switch (prop_id) {
    case ARG_AUDIO_COMPRESSION:
      g_value_set_boolean (value, mvemux->compression);
      break;
    case ARG_VIDEO_QUICK_ENCODING:
      g_value_set_boolean (value, mvemux->quick_encoding);
      break;
    case ARG_VIDEO_SCREEN_WIDTH:
      g_value_set_uint (value, mvemux->screen_width);
      break;
    case ARG_VIDEO_SCREEN_HEIGHT:
      g_value_set_uint (value, mvemux->screen_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mve_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstMveMux *mvemux;

  g_return_if_fail (GST_IS_MVE_MUX (object));
  mvemux = GST_MVE_MUX (object);

  switch (prop_id) {
    case ARG_AUDIO_COMPRESSION:
      mvemux->compression = g_value_get_boolean (value);
      break;
    case ARG_VIDEO_QUICK_ENCODING:
      mvemux->quick_encoding = g_value_get_boolean (value);
      break;
    case ARG_VIDEO_SCREEN_WIDTH:
      mvemux->screen_width = g_value_get_uint (value);
      break;
    case ARG_VIDEO_SCREEN_HEIGHT:
      mvemux->screen_height = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_mve_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstMveMux *mvemux;

  g_return_val_if_fail (GST_IS_MVE_MUX (element), GST_STATE_CHANGE_FAILURE);

  mvemux = GST_MVE_MUX (element);

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    GstStateChangeReturn ret;

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mve_mux_reset (mvemux);
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static const GstBuffer *
gst_mve_mux_palette_from_buffer (GstBuffer * buf)
{
  const GstBuffer *palette = NULL;
  GstCaps *caps = GST_BUFFER_CAPS (buf);

  if (caps != NULL) {
    GstStructure *str = gst_caps_get_structure (caps, 0);
    const GValue *pal = gst_structure_get_value (str, "palette_data");

    if (pal != NULL) {
      palette = gst_value_get_buffer (pal);
      if (GST_BUFFER_SIZE (palette) < 256 * 4)
        palette = NULL;
    }
  }
  return palette;
}

static GstFlowReturn
gst_mve_mux_palette_from_current_frame (GstMveMux * mvemux,
    const GstBuffer ** pal)
{
  GstBuffer *buf = g_queue_peek_head (mvemux->video_buffer);

  /* get palette from buffer */
  *pal = gst_mve_mux_palette_from_buffer (buf);
  if (*pal == NULL) {
    GST_ERROR_OBJECT (mvemux, "video buffer has no palette data");
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

static void
gst_mve_mux_palette_analyze (GstMveMux * mvemux, const GstBuffer * pal,
    guint16 * first, guint16 * last)
{
  gint i;
  guint32 *col1;

  col1 = (guint32 *) GST_BUFFER_DATA (pal);

  /* compare current palette against last frame */
  if (mvemux->last_frame == NULL) {
    /* ignore 0,0,0 entries but make sure we get
       at least one color */
    /* FIXME: is ignoring 0,0,0 safe? possibly depends on player impl */
    for (i = 0; i < MVE_PALETTE_COUNT; ++i) {
      if (col1[i] != 0) {
        *first = i;
        break;
      }
    }
    if (i == MVE_PALETTE_COUNT) {
      *first = *last = 0;
    } else {
      for (i = MVE_PALETTE_COUNT - 1; i >= 0; --i) {
        if (col1[i] != 0) {
          *last = i;
          break;
        }
      }
    }
  } else {
    const GstBuffer *last_pal;
    guint32 *col2;

    last_pal = gst_mve_mux_palette_from_buffer (mvemux->last_frame);

    g_return_if_fail (last_pal != NULL);

    col2 = (guint32 *) GST_BUFFER_DATA (last_pal);

    for (i = 0; i < MVE_PALETTE_COUNT; ++i) {
      if (col1[i] != col2[i]) {
        *first = i;
        break;
      }
    }
    for (i = MVE_PALETTE_COUNT - 1; i >= 0; --i) {
      if (col1[i] != col2[i]) {
        *last = i;
        break;
      }
    }
  }

  GST_DEBUG_OBJECT (mvemux, "palette first:%d, last:%d", *first, *last);
}

static gboolean
gst_mve_mux_palette_changed (GstMveMux * mvemux, const GstBuffer * pal)
{
  const GstBuffer *last_pal;

  g_return_val_if_fail (mvemux->last_frame != NULL, TRUE);

  last_pal = gst_mve_mux_palette_from_buffer (mvemux->last_frame);
  if (last_pal == NULL)
    return TRUE;

  return memcmp (GST_BUFFER_DATA (last_pal), GST_BUFFER_DATA (pal),
      MVE_PALETTE_COUNT * 4) != 0;
}

static GstFlowReturn
gst_mve_mux_push_buffer (GstMveMux * mvemux, GstBuffer * buffer)
{
  GST_BUFFER_OFFSET (buffer) = mvemux->stream_offset;
  mvemux->stream_offset += GST_BUFFER_SIZE (buffer);
  GST_BUFFER_OFFSET_END (buffer) = mvemux->stream_offset;
  return gst_pad_push (mvemux->source, buffer);
}

/* returns TRUE if audio segment is complete */
static gboolean
gst_mve_mux_audio_data (GstMveMux * mvemux)
{
  gboolean complete = FALSE;

  while (!complete) {
    GstBuffer *buf;
    GstClockTime buftime;
    GstClockTime duration;
    GstClockTime t_needed;
    gint b_needed;
    gint len;

    buf = g_queue_peek_head (mvemux->audio_buffer);
    if (buf == NULL)
      return (mvemux->audio_pad_eos && mvemux->chunk_audio) ||
          (mvemux->stream_time + mvemux->frame_duration < mvemux->max_ts);

    buftime = GST_BUFFER_TIMESTAMP (buf);
    duration = GST_BUFFER_DURATION (buf);

    /* FIXME: adjust buffer timestamps using segment info */

    /* assume continuous buffers on invalid time stamps */
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (buftime)))
      buftime = mvemux->next_ts;

    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (duration)))
      duration = gst_util_uint64_scale_int (mvemux->frame_duration,
          GST_BUFFER_SIZE (buf), mvemux->spf);

    if (mvemux->chunk_audio) {
      b_needed = mvemux->spf - mvemux->chunk_audio->len;
      t_needed = (gint) gst_util_uint64_scale_int (mvemux->frame_duration,
          b_needed, mvemux->spf);
    } else {
      b_needed = mvemux->spf;
      t_needed = mvemux->frame_duration;
    }

    if (buftime > mvemux->next_ts + t_needed) {
      /* future buffer - fill chunk with silence */
      GST_DEBUG_OBJECT (mvemux, "future buffer, inserting silence");

      /* if we already have a chunk started, fill it
         otherwise we'll simply insert a silence chunk */
      if (mvemux->chunk_audio) {
        len = mvemux->chunk_audio->len;
        g_byte_array_set_size (mvemux->chunk_audio, mvemux->spf);
        memset (mvemux->chunk_audio->data + len, 0, mvemux->spf - len);
      }
      mvemux->next_ts += t_needed;
      complete = TRUE;
    } else if (buftime + duration <= mvemux->next_ts) {
      /* past buffer - drop */
      GST_DEBUG_OBJECT (mvemux, "dropping past buffer");
      g_queue_pop_head (mvemux->audio_buffer);
      gst_buffer_unref (buf);
    } else {
      /* our data starts somewhere in this buffer */
      const guint8 *bufdata = GST_BUFFER_DATA (buf);
      gint b_available = GST_BUFFER_SIZE (buf);
      gint align = (mvemux->bps / 8) * mvemux->channels - 1;
      gint offset;

      if (mvemux->chunk_audio == NULL)
        mvemux->chunk_audio = g_byte_array_sized_new (mvemux->spf);

      if (buftime >= mvemux->next_ts) {
        /* insert silence as necessary */
        len = mvemux->chunk_audio->len;
        offset = (gint) gst_util_uint64_scale_int (mvemux->spf,
            buftime - mvemux->next_ts, mvemux->frame_duration);
        offset = (offset + align) & ~align;

        if (len < offset) {
          g_byte_array_set_size (mvemux->chunk_audio, offset);
          memset (mvemux->chunk_audio->data + len, 0, offset - len);
          b_needed -= offset - len;
          mvemux->next_ts += gst_util_uint64_scale_int (mvemux->frame_duration,
              offset - len, mvemux->spf);
        }
        offset = 0;
      } else {
        offset = (gint) gst_util_uint64_scale_int (mvemux->spf,
            mvemux->next_ts - buftime, mvemux->frame_duration);
        offset = (offset + align) & ~align;
      }

      g_assert (offset <= b_available);

      bufdata += offset;
      b_available -= offset;
      if (b_needed > b_available)
        b_needed = b_available;

      if (mvemux->bps == 8) {
        g_byte_array_append (mvemux->chunk_audio, bufdata, b_needed);
      } else {
        guint i;
        gint16 *sample = (gint16 *) bufdata;
        guint8 s[2];

        len = b_needed / 2;
        for (i = 0; i < len; ++i) {
          s[0] = (*sample) & 0x00FF;
          s[1] = ((*sample) & 0xFF00) >> 8;
          g_byte_array_append (mvemux->chunk_audio, s, 2);
          ++sample;
        }
      }

      mvemux->next_ts += gst_util_uint64_scale_int (mvemux->frame_duration,
          b_needed, mvemux->spf);

      if (b_available - b_needed == 0) {
        /* consumed buffer */
        GST_LOG_OBJECT (mvemux, "popping consumed buffer");
        g_queue_pop_head (mvemux->audio_buffer);
        gst_buffer_unref (buf);
      }

      complete = (mvemux->chunk_audio->len >= mvemux->spf);
    }

    if (mvemux->max_ts < mvemux->next_ts)
      mvemux->max_ts = mvemux->next_ts;
  }

  return complete;
}

static GstFlowReturn
gst_mve_mux_start_movie (GstMveMux * mvemux)
{
  GstFlowReturn res;
  GstBuffer *buf;

  GST_DEBUG_OBJECT (mvemux, "writing movie preamble");

  res = gst_pad_alloc_buffer (mvemux->source, 0,
      MVE_PREAMBLE_SIZE, GST_PAD_CAPS (mvemux->source), &buf);

  if (res != GST_FLOW_OK)
    return res;

  gst_pad_push_event (mvemux->source,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0));

  memcpy (GST_BUFFER_DATA (buf), mve_preamble, MVE_PREAMBLE_SIZE);
  return gst_mve_mux_push_buffer (mvemux, buf);
}

static GstFlowReturn
gst_mve_mux_end_movie (GstMveMux * mvemux)
{
  GstFlowReturn res;
  GstBuffer *buf;
  guint8 *bufdata;

  GST_DEBUG_OBJECT (mvemux, "writing movie shutdown chunk");

  res = gst_pad_alloc_buffer (mvemux->source, 0, 16,
      GST_PAD_CAPS (mvemux->source), &buf);

  if (res != GST_FLOW_OK)
    return res;

  bufdata = GST_BUFFER_DATA (buf);

  GST_WRITE_UINT16_LE (bufdata, 8);     /* shutdown chunk */
  GST_WRITE_UINT16_LE (bufdata + 2, MVE_CHUNK_SHUTDOWN);
  GST_WRITE_UINT16_LE (bufdata + 4, 0); /* end movie segment */
  bufdata[6] = MVE_OC_END_OF_STREAM;
  bufdata[7] = 0;
  GST_WRITE_UINT16_LE (bufdata + 8, 0); /* end chunk segment */
  bufdata[10] = MVE_OC_END_OF_CHUNK;
  bufdata[11] = 0;

  GST_WRITE_UINT16_LE (bufdata + 12, 0);        /* end movie chunk */
  GST_WRITE_UINT16_LE (bufdata + 14, MVE_CHUNK_END);

  return gst_mve_mux_push_buffer (mvemux, buf);
}

static GstFlowReturn
gst_mve_mux_init_video_chunk (GstMveMux * mvemux, const GstBuffer * pal)
{
  GstFlowReturn res;
  GstBuffer *buf;
  guint8 *bufdata;
  guint16 buf_size;
  guint16 first_col = 0, last_col = 0;
  guint pal_size = 0;

  GST_DEBUG_OBJECT (mvemux, "init-video chunk w:%d, h:%d, bpp:%d",
      mvemux->width, mvemux->height, mvemux->bpp);

  buf_size = 4;                 /* chunk header */
  buf_size += 4 + 6;            /* init video mode segment */
  buf_size += 4 + 8;            /* create video buffers segment */

  if (mvemux->bpp == 8) {
    g_return_val_if_fail (pal != NULL, GST_FLOW_ERROR);

    /* install palette segment */
    gst_mve_mux_palette_analyze (mvemux, pal, &first_col, &last_col);
    pal_size = (last_col - first_col + 1) * 3;
    buf_size += 4 + 4 + pal_size;
  }

  buf_size += 4 + 0;            /* end chunk segment */

  res = gst_pad_alloc_buffer (mvemux->source, 0, buf_size,
      GST_PAD_CAPS (mvemux->source), &buf);
  if (res != GST_FLOW_OK)
    return res;

  bufdata = GST_BUFFER_DATA (buf);

  GST_WRITE_UINT16_LE (bufdata, buf_size - 4);
  GST_WRITE_UINT16_LE (bufdata + 2, MVE_CHUNK_INIT_VIDEO);

  GST_WRITE_UINT16_LE (bufdata + 4, 6);
  bufdata[6] = MVE_OC_VIDEO_MODE;
  bufdata[7] = 0;
  GST_WRITE_UINT16_LE (bufdata + 8, mvemux->screen_width);      /* screen width */
  GST_WRITE_UINT16_LE (bufdata + 10, mvemux->screen_height);    /* screen height */
  GST_WRITE_UINT16_LE (bufdata + 12, 0);        /* ??? - flags */

  GST_WRITE_UINT16_LE (bufdata + 14, 8);
  bufdata[16] = MVE_OC_VIDEO_BUFFERS;
  bufdata[17] = 2;
  GST_WRITE_UINT16_LE (bufdata + 18, mvemux->width >> 3);       /* buffer width */
  GST_WRITE_UINT16_LE (bufdata + 20, mvemux->height >> 3);      /* buffer height */
  GST_WRITE_UINT16_LE (bufdata + 22, 1);        /* buffer count */
  GST_WRITE_UINT16_LE (bufdata + 24, (mvemux->bpp >> 3) - 1);   /* true color */

  bufdata += 26;

  if (mvemux->bpp == 8) {
    /* TODO: check whether we really need to update the entire palette (or at all) */
    gint i;
    guint32 *col;

    GST_DEBUG_OBJECT (mvemux, "installing palette");

    GST_WRITE_UINT16_LE (bufdata, 4 + pal_size);
    bufdata[2] = MVE_OC_PALETTE;
    bufdata[3] = 0;
    GST_WRITE_UINT16_LE (bufdata + 4, first_col);       /* first color index */
    GST_WRITE_UINT16_LE (bufdata + 6, last_col - first_col + 1);        /* number of colors */

    bufdata += 8;
    col = (guint32 *) GST_BUFFER_DATA (pal);
    for (i = first_col; i <= last_col; ++i) {
      /* convert from 8-bit palette to 6-bit VGA */
      guint32 rgb = col[i];

      (*bufdata) = ((rgb & 0x00FF0000) >> 16) >> 2;
      ++bufdata;
      (*bufdata) = ((rgb & 0x0000FF00) >> 8) >> 2;
      ++bufdata;
      (*bufdata) = (rgb & 0x000000FF) >> 2;
      ++bufdata;
    }

    mvemux->pal_changed = TRUE;
    mvemux->pal_first_color = first_col;
    mvemux->pal_colors = last_col - first_col + 1;
  }

  GST_WRITE_UINT16_LE (bufdata, 0);
  bufdata[2] = MVE_OC_END_OF_CHUNK;
  bufdata[3] = 0;

  return gst_mve_mux_push_buffer (mvemux, buf);
}

static GstFlowReturn
gst_mve_mux_init_audio_chunk (GstMveMux * mvemux)
{
  GstFlowReturn res;
  GstBuffer *buf;
  guint16 buf_size;
  guint8 *bufdata;
  guint16 flags = 0;
  gint align;

  GST_DEBUG_OBJECT (mvemux,
      "init-audio chunk rate:%d, chan:%d, bps:%d, comp:%d", mvemux->rate,
      mvemux->channels, mvemux->bps, mvemux->compression);

  if (G_UNLIKELY (mvemux->bps == 8 && mvemux->compression)) {
    GST_INFO_OBJECT (mvemux,
        "compression only supported for 16-bit samples, disabling");
    mvemux->compression = FALSE;
  }

  /* calculate sample data per frame */
  align = (mvemux->bps / 8) * mvemux->channels;
  mvemux->spf =
      (guint16) (gst_util_uint64_scale_int (align * mvemux->rate,
          mvemux->frame_duration, GST_SECOND) + align - 1) & ~(align - 1);

  /* prebuffer approx. 1 second of audio data */
  mvemux->lead_frames = align * mvemux->rate / mvemux->spf;
  GST_DEBUG_OBJECT (mvemux, "calculated spf:%d, lead frames:%d",
      mvemux->spf, mvemux->lead_frames);

  /* chunk header + init video mode segment + end chunk segment */
  buf_size = 4 + (4 + 10) + 4;

  res = gst_pad_alloc_buffer (mvemux->source, 0, buf_size,
      GST_PAD_CAPS (mvemux->source), &buf);
  if (res != GST_FLOW_OK)
    return res;

  bufdata = GST_BUFFER_DATA (buf);

  if (mvemux->channels == 2)
    flags |= MVE_AUDIO_STEREO;
  if (mvemux->bps == 16)
    flags |= MVE_AUDIO_16BIT;
  if (mvemux->compression)
    flags |= MVE_AUDIO_COMPRESSED;

  GST_WRITE_UINT16_LE (bufdata, buf_size - 4);
  GST_WRITE_UINT16_LE (bufdata + 2, MVE_CHUNK_INIT_AUDIO);

  GST_WRITE_UINT16_LE (bufdata + 4, 10);
  bufdata[6] = MVE_OC_AUDIO_BUFFERS;
  bufdata[7] = 1;
  GST_WRITE_UINT16_LE (bufdata + 8, 0); /* ??? */
  GST_WRITE_UINT16_LE (bufdata + 10, flags);    /* flags */
  GST_WRITE_UINT16_LE (bufdata + 12, mvemux->rate);     /* sample rate */
  GST_WRITE_UINT32_LE (bufdata + 14,    /* minimum audio buffer size */
      mvemux->spf * mvemux->lead_frames);

  GST_WRITE_UINT16_LE (bufdata + 18, 0);
  bufdata[20] = MVE_OC_END_OF_CHUNK;
  bufdata[21] = 0;

  return gst_mve_mux_push_buffer (mvemux, buf);
}

static guint8 *
gst_mve_mux_write_audio_segments (GstMveMux * mvemux, guint8 * data)
{
  GByteArray *chunk = mvemux->chunk_audio;
  guint16 silent_mask;

  GST_LOG_OBJECT (mvemux, "writing audio data");

  /* audio data */
  if (chunk) {
    guint16 len = mvemux->compression ?
        chunk->len / 2 + mvemux->channels : chunk->len;

    silent_mask = 0xFFFE;

    GST_WRITE_UINT16_LE (data, 6 + len);
    data[2] = MVE_OC_AUDIO_DATA;
    data[3] = 0;
    GST_WRITE_UINT16_LE (data + 4, mvemux->audio_frames);       /* frame number */
    GST_WRITE_UINT16_LE (data + 6, 0x0001);     /* stream mask */
    GST_WRITE_UINT16_LE (data + 8, chunk->len); /* (uncompressed) data length */
    data += 10;

    if (mvemux->compression)
      mve_compress_audio (data, chunk->data, len, mvemux->channels);
    else
      memcpy (data, chunk->data, chunk->len);
    data += len;

    g_byte_array_free (chunk, TRUE);
    mvemux->chunk_audio = NULL;
  } else
    silent_mask = 0xFFFF;

  /* audio data (silent) */
  GST_WRITE_UINT16_LE (data, 6);
  data[2] = MVE_OC_AUDIO_SILENCE;
  data[3] = 0;
  GST_WRITE_UINT16_LE (data + 4, mvemux->audio_frames++);       /* frame number */
  GST_WRITE_UINT16_LE (data + 6, silent_mask);  /* stream mask */
  GST_WRITE_UINT16_LE (data + 8, mvemux->spf);  /* (imaginary) data length */
  data += 10;

  return data;
}

static GstFlowReturn
gst_mve_mux_prebuffer_audio_chunk (GstMveMux * mvemux)
{
  GstFlowReturn ret;
  GstBuffer *chunk;
  guint16 size;
  guint8 *data;

  /* calculate chunk size */
  size = 4;                     /* chunk header */

  if (mvemux->chunk_audio) {
    size += 4 + 6 +             /* audio data */
        (mvemux->compression ?
        mvemux->chunk_audio->len / 2 + mvemux->channels :
        mvemux->chunk_audio->len);
  }
  size += 4 + 6;                /* audio data silent */
  size += 4;                    /* end chunk */

  ret = gst_pad_alloc_buffer (mvemux->source, 0, size,
      GST_PAD_CAPS (mvemux->source), &chunk);
  if (ret != GST_FLOW_OK)
    return ret;

  data = GST_BUFFER_DATA (chunk);

  /* assemble chunk */
  GST_WRITE_UINT16_LE (data, size - 4);
  GST_WRITE_UINT16_LE (data + 2, MVE_CHUNK_AUDIO_ONLY);
  data += 4;

  data = gst_mve_mux_write_audio_segments (mvemux, data);

  /* end chunk */
  GST_WRITE_UINT16_LE (data, 0);
  data[2] = MVE_OC_END_OF_CHUNK;
  data[3] = 0;

  if (mvemux->audio_frames >= mvemux->lead_frames)
    mvemux->state = MVE_MUX_STATE_MOVIE;

  mvemux->stream_time += mvemux->frame_duration;

  GST_DEBUG_OBJECT (mvemux, "pushing audio chunk");

  return gst_mve_mux_push_buffer (mvemux, chunk);
}

static GstFlowReturn
gst_mve_mux_push_chunk (GstMveMux * mvemux)
{
  GstFlowReturn ret;
  GstBuffer *chunk;
  GstBuffer *frame;
  guint32 size;
  guint16 cm_size = 0;
  guint8 *data;

  /* calculate chunk size */
  size = 4;                     /* chunk header */

  if (G_UNLIKELY (mvemux->timer == 0)) {
    /* we need to insert a timer segment */
    size += 4 + 6;
  }

  if (mvemux->audio_pad_connected) {
    if (mvemux->chunk_audio) {
      size += 4 + 6 +           /* audio data */
          (mvemux->compression ?
          mvemux->chunk_audio->len / 2 + mvemux->channels :
          mvemux->chunk_audio->len);
    }
    size += 4 + 6;              /* audio data silent */
  }

  size += 4 + 6;                /* play video */
  size += 4;                    /* play audio; present even if no audio stream */
  size += 4;                    /* end chunk */

  /* we must encode video only after we have the audio side
     covered, since only then we can tell what size limit
     the video data must adhere to */
  frame = g_queue_pop_head (mvemux->video_buffer);
  if (frame != NULL) {
    cm_size = (((mvemux->width * mvemux->height) >> 6) + 1) >> 1;
    size += 4 + cm_size;        /* code map */
    size += 4 + 14;             /* video data header */

    /* make sure frame is writable since the encoder may want to modify it */
    frame = gst_buffer_make_writable (frame);

    if (mvemux->bpp == 8) {
      const GstBuffer *pal = gst_mve_mux_palette_from_buffer (frame);

      if (pal == NULL)
        ret = GST_FLOW_ERROR;
      else
        ret = mve_encode_frame8 (mvemux, frame,
            (guint32 *) GST_BUFFER_DATA (pal), G_MAXUINT16 - size);
    } else
      ret = mve_encode_frame16 (mvemux, frame, G_MAXUINT16 - size);

    if (mvemux->second_last_frame != NULL)
      gst_buffer_unref (mvemux->second_last_frame);
    mvemux->second_last_frame = mvemux->last_frame;
    mvemux->last_frame = frame;

    if (ret != GST_FLOW_OK)
      return ret;

    size += mvemux->chunk_video->len;
  }

  if (size > G_MAXUINT16) {
    GST_ELEMENT_ERROR (mvemux, STREAM, ENCODE, (NULL),
        ("encoding frame %d failed: maximum block size exceeded (%u)",
            mvemux->video_frames + 1, size));
    return GST_FLOW_ERROR;
  }

  ret = gst_pad_alloc_buffer (mvemux->source, 0, size,
      GST_PAD_CAPS (mvemux->source), &chunk);
  if (ret != GST_FLOW_OK)
    return ret;

  data = GST_BUFFER_DATA (chunk);

  /* assemble chunk */
  GST_WRITE_UINT16_LE (data, size - 4);
  GST_WRITE_UINT16_LE (data + 2, MVE_CHUNK_VIDEO);
  data += 4;

  if (G_UNLIKELY (mvemux->timer == 0)) {
    /* insert a timer segment */
    mvemux->timer = mvemux->frame_duration / GST_USECOND / 8;

    GST_WRITE_UINT16_LE (data, 6);
    data[2] = MVE_OC_CREATE_TIMER;
    data[3] = 0;
    GST_WRITE_UINT32_LE (data + 4, mvemux->timer);      /* timer rate */
    GST_WRITE_UINT16_LE (data + 8, 8);  /* timer subdivision */
    data += 10;
  }

  /* code map */
  if (mvemux->chunk_video) {
    GST_WRITE_UINT16_LE (data, cm_size);
    data[2] = MVE_OC_CODE_MAP;
    data[3] = 0;
    memcpy (data + 4, mvemux->chunk_code_map, cm_size);
    data += 4 + cm_size;
  }

  if (mvemux->audio_pad_connected)
    data = gst_mve_mux_write_audio_segments (mvemux, data);

  if (mvemux->chunk_video) {
    GST_LOG_OBJECT (mvemux, "writing video data");

    /* video data */
    GST_WRITE_UINT16_LE (data, 14 + mvemux->chunk_video->len);
    data[2] = MVE_OC_VIDEO_DATA;
    data[3] = 0;
    GST_WRITE_UINT16_LE (data + 6, mvemux->video_frames);       /* previous frame */
    GST_WRITE_UINT16_LE (data + 4, ++mvemux->video_frames);     /* current frame */
    GST_WRITE_UINT16_LE (data + 8, 0);  /* x offset */
    GST_WRITE_UINT16_LE (data + 10, 0); /* y offset */
    GST_WRITE_UINT16_LE (data + 12, mvemux->width >> 3);        /* buffer width */
    GST_WRITE_UINT16_LE (data + 14, mvemux->height >> 3);       /* buffer height */
    GST_WRITE_UINT16_LE (data + 16,     /* flags */
        (mvemux->video_frames == 1 ? 0 : MVE_VIDEO_DELTA_FRAME));
    memcpy (data + 18, mvemux->chunk_video->data, mvemux->chunk_video->len);
    data += 18 + mvemux->chunk_video->len;

    g_byte_array_free (mvemux->chunk_video, TRUE);
    mvemux->chunk_video = NULL;
  }

  /* play audio */
  GST_WRITE_UINT16_LE (data, 0);
  data[2] = MVE_OC_PLAY_AUDIO;
  data[3] = 0;
  data += 4;

  /* play video */
  GST_WRITE_UINT16_LE (data, 6);
  data[2] = MVE_OC_PLAY_VIDEO;
  data[3] = 1;
  /* this block is only set to non-zero on palette changes in 8-bit mode */
  if (mvemux->pal_changed) {
    GST_WRITE_UINT16_LE (data + 4, mvemux->pal_first_color);    /* index of first color */
    GST_WRITE_UINT16_LE (data + 6, mvemux->pal_colors); /* number of colors */
    mvemux->pal_changed = FALSE;
  } else {
    GST_WRITE_UINT32_LE (data + 4, 0);
  }
  GST_WRITE_UINT16_LE (data + 8, 0);    /* ??? */
  data += 10;

  /* end chunk */
  GST_WRITE_UINT16_LE (data, 0);
  data[2] = MVE_OC_END_OF_CHUNK;
  data[3] = 0;

  mvemux->chunk_has_palette = FALSE;
  mvemux->chunk_has_audio = FALSE;
  mvemux->stream_time += mvemux->frame_duration;

  GST_LOG_OBJECT (mvemux, "pushing video chunk");

  return gst_mve_mux_push_buffer (mvemux, chunk);
}

static GstFlowReturn
gst_mve_mux_chain (GstPad * sinkpad, GstBuffer * inbuf)
{
  GstMveMux *mvemux = GST_MVE_MUX (GST_PAD_PARENT (sinkpad));
  GstFlowReturn ret = GST_FLOW_OK;
  const GstBuffer *palette;
  gboolean audio_ok, video_ok;

  /* need to serialize the buffers */
  g_mutex_lock (mvemux->lock);

  if (G_LIKELY (inbuf != NULL)) {       /* TODO: see _sink_event... */
    if (sinkpad == mvemux->audiosink)
      g_queue_push_tail (mvemux->audio_buffer, inbuf);
    else if (sinkpad == mvemux->videosink)
      g_queue_push_tail (mvemux->video_buffer, inbuf);
    else
      g_assert_not_reached ();
  }

  /* TODO: this is gross... */
  if (G_UNLIKELY (mvemux->state == MVE_MUX_STATE_INITIAL)) {
    GST_DEBUG_OBJECT (mvemux, "waiting for caps");
    goto done;
  }

  /* now actually try to mux something */
  if (G_UNLIKELY (mvemux->state == MVE_MUX_STATE_CONNECTED)) {
    palette = NULL;

    if (mvemux->bpp == 8) {
      /* we need to add palette info to the init chunk */
      if (g_queue_is_empty (mvemux->video_buffer))
        goto done;              /* wait for more data */

      ret = gst_mve_mux_palette_from_current_frame (mvemux, &palette);
      if (ret != GST_FLOW_OK)
        goto done;
    }

    gst_mve_mux_start_movie (mvemux);
    gst_mve_mux_init_video_chunk (mvemux, palette);
    mvemux->chunk_has_palette = TRUE;

    if (mvemux->audio_pad_connected) {
      gst_mve_mux_init_audio_chunk (mvemux);

      mvemux->state = MVE_MUX_STATE_PREBUFFER;
    } else
      mvemux->state = MVE_MUX_STATE_MOVIE;
  }

  while ((mvemux->state == MVE_MUX_STATE_PREBUFFER) && (ret == GST_FLOW_OK) &&
      gst_mve_mux_audio_data (mvemux)) {
    ret = gst_mve_mux_prebuffer_audio_chunk (mvemux);
  }

  if (G_LIKELY (mvemux->state >= MVE_MUX_STATE_MOVIE)) {
    audio_ok = !mvemux->audio_pad_connected ||
        !g_queue_is_empty (mvemux->audio_buffer) ||
        (mvemux->audio_pad_eos && (mvemux->stream_time <= mvemux->max_ts));
    video_ok = !g_queue_is_empty (mvemux->video_buffer) ||
        (mvemux->video_pad_eos &&
        (!mvemux->audio_pad_eos || (mvemux->stream_time <= mvemux->max_ts)));

    while ((ret == GST_FLOW_OK) && audio_ok && video_ok) {

      if (!g_queue_is_empty (mvemux->video_buffer)) {
        if ((mvemux->bpp == 8) && !mvemux->chunk_has_palette) {
          ret = gst_mve_mux_palette_from_current_frame (mvemux, &palette);
          if (ret != GST_FLOW_OK)
            goto done;

          if (gst_mve_mux_palette_changed (mvemux, palette))
            gst_mve_mux_init_video_chunk (mvemux, palette);
          mvemux->chunk_has_palette = TRUE;
        }
      }

      /* audio data */
      if (mvemux->audio_pad_connected && !mvemux->chunk_has_audio &&
          gst_mve_mux_audio_data (mvemux))
        mvemux->chunk_has_audio = TRUE;

      if ((!g_queue_is_empty (mvemux->video_buffer) || mvemux->video_pad_eos) &&
          (mvemux->chunk_has_audio || !mvemux->audio_pad_connected
              || mvemux->audio_pad_eos)) {
        ret = gst_mve_mux_push_chunk (mvemux);
      }

      audio_ok = !mvemux->audio_pad_connected ||
          !g_queue_is_empty (mvemux->audio_buffer) ||
          (mvemux->audio_pad_eos && (mvemux->stream_time <= mvemux->max_ts));
      video_ok = !g_queue_is_empty (mvemux->video_buffer) ||
          (mvemux->video_pad_eos &&
          (!mvemux->audio_pad_eos || (mvemux->stream_time <= mvemux->max_ts)));
    }
  }

  if (G_UNLIKELY ((mvemux->state == MVE_MUX_STATE_EOS) && (ret == GST_FLOW_OK))) {
    ret = gst_mve_mux_end_movie (mvemux);
    gst_pad_push_event (mvemux->source, gst_event_new_eos ());
  }

done:
  g_mutex_unlock (mvemux->lock);
  return ret;
}

static gboolean
gst_mve_mux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMveMux *mvemux = GST_MVE_MUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (mvemux, "got %s event for pad %s",
      GST_EVENT_TYPE_NAME (event), GST_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (pad == mvemux->audiosink) {
        mvemux->audio_pad_eos = TRUE;

        if (mvemux->state == MVE_MUX_STATE_PREBUFFER)
          mvemux->state = MVE_MUX_STATE_MOVIE;
      } else if (pad == mvemux->videosink)
        mvemux->video_pad_eos = TRUE;

      /* TODO: this is evil */
      if (mvemux->audio_pad_eos && mvemux->video_pad_eos) {
        mvemux->state = MVE_MUX_STATE_EOS;
        gst_mve_mux_chain (pad, NULL);
      }
      gst_event_unref (event);
      break;
    case GST_EVENT_NEWSEGMENT:
      if (pad == mvemux->audiosink) {
        GstFormat format;
        gint64 start;
        gboolean update;

        gst_event_parse_new_segment (event, &update, NULL, &format, &start,
            NULL, NULL);
        if ((format == GST_FORMAT_TIME) && update && (start > mvemux->max_ts))
          mvemux->max_ts = start;
      }
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}

static gboolean
gst_mve_mux_vidsink_set_caps (GstPad * pad, GstCaps * vscaps)
{
  GstMveMux *mvemux;
  GstStructure *structure;
  GstClockTime duration;
  const GValue *fps;
  gint w, h, bpp;
  gboolean ret;

  mvemux = GST_MVE_MUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (mvemux, "video set_caps triggered on %s",
      GST_PAD_NAME (pad));

  structure = gst_caps_get_structure (vscaps, 0);

  ret = gst_structure_get_int (structure, "width", &w);
  ret &= gst_structure_get_int (structure, "height", &h);
  ret &= gst_structure_get_int (structure, "bpp", &bpp);
  fps = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps));

  duration = gst_util_uint64_scale_int (GST_SECOND,
      gst_value_get_fraction_denominator (fps),
      gst_value_get_fraction_numerator (fps));

  if (!ret)
    return FALSE;

  /* don't allow changing width, height, bpp, or framerate */
  if (mvemux->state != MVE_MUX_STATE_INITIAL) {
    if (mvemux->width != w || mvemux->height != h ||
        mvemux->bpp != bpp || mvemux->frame_duration != duration) {
      GST_ERROR_OBJECT (mvemux, "caps renegotiation not allowed");
      return FALSE;
    }
  } else {
    if (w % 8 != 0 || h % 8 != 0) {
      GST_ERROR_OBJECT (mvemux, "width and height must be multiples of 8");
      return FALSE;
    }

    mvemux->width = w;
    mvemux->height = h;
    mvemux->bpp = bpp;
    mvemux->frame_duration = duration;

    if (mvemux->screen_width < w) {
      GST_INFO_OBJECT (mvemux, "setting suggested screen width to %d", w);
      mvemux->screen_width = w;
    }
    if (mvemux->screen_height < h) {
      GST_INFO_OBJECT (mvemux, "setting suggested screen height to %d", h);
      mvemux->screen_height = h;
    }

    g_free (mvemux->chunk_code_map);
    mvemux->chunk_code_map = g_malloc ((((w * h) >> 6) + 1) >> 1);

    /* audio caps already initialized? */
    if (mvemux->bps != 0 || !mvemux->audio_pad_connected)
      mvemux->state = MVE_MUX_STATE_CONNECTED;
  }

  return TRUE;
}

static gboolean
gst_mve_mux_audsink_set_caps (GstPad * pad, GstCaps * ascaps)
{
  GstMveMux *mvemux;
  GstStructure *structure;
  gboolean ret;
  gint val;

  mvemux = GST_MVE_MUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (mvemux, "audio set_caps triggered on %s",
      GST_PAD_NAME (pad));

  /* don't allow caps renegotiation for now */
  if (mvemux->state != MVE_MUX_STATE_INITIAL)
    return FALSE;

  structure = gst_caps_get_structure (ascaps, 0);

  ret = gst_structure_get_int (structure, "channels", &val);
  mvemux->channels = val;
  ret &= gst_structure_get_int (structure, "rate", &val);
  mvemux->rate = val;
  ret &= gst_structure_get_int (structure, "width", &val);
  mvemux->bps = val;

  /* video caps already initialized? */
  if (mvemux->bpp != 0)
    mvemux->state = MVE_MUX_STATE_CONNECTED;

  return ret;
}

static GstPad *
gst_mve_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstMveMux *mvemux = GST_MVE_MUX (element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstPad *pad;

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    GST_WARNING_OBJECT (mvemux, "request pad is not a SINK pad");
    return NULL;
  }

  if (templ == gst_element_class_get_pad_template (klass, "audio")) {
    if (mvemux->audiosink)
      return NULL;

    mvemux->audiosink = gst_pad_new_from_template (templ, "audio");
    gst_pad_set_setcaps_function (mvemux->audiosink,
        GST_DEBUG_FUNCPTR (gst_mve_mux_audsink_set_caps));
    mvemux->audio_pad_eos = FALSE;
    pad = mvemux->audiosink;
  } else if (templ == gst_element_class_get_pad_template (klass, "video")) {
    if (mvemux->videosink)
      return NULL;

    mvemux->videosink = gst_pad_new_from_template (templ, "video");
    gst_pad_set_setcaps_function (mvemux->videosink,
        GST_DEBUG_FUNCPTR (gst_mve_mux_vidsink_set_caps));
    mvemux->video_pad_eos = FALSE;
    pad = mvemux->videosink;
  } else {
    g_return_val_if_reached (NULL);
  }

  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_mve_mux_chain));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_mve_mux_sink_event));

  g_signal_connect (pad, "linked", G_CALLBACK (gst_mve_mux_pad_link), mvemux);
  g_signal_connect (pad, "unlinked", G_CALLBACK (gst_mve_mux_pad_unlink),
      mvemux);

  gst_element_add_pad (element, pad);
  return pad;
}

static void
gst_mve_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstMveMux *mvemux = GST_MVE_MUX (element);

  gst_element_remove_pad (element, pad);

  if (pad == mvemux->audiosink) {
    mvemux->audiosink = NULL;
    mvemux->audio_pad_connected = FALSE;
  } else if (pad == mvemux->videosink) {
    mvemux->videosink = NULL;
    mvemux->video_pad_connected = FALSE;
  }
}

static void
gst_mve_mux_base_init (GstMveMuxClass * klass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));

  gst_element_class_set_static_metadata (element_class, "MVE Multiplexer",
      "Codec/Muxer",
      "Muxes audio and video into an MVE stream",
      "Jens Granseuer <jensgr@gmx.net>");
}

static void
gst_mve_mux_finalize (GObject * object)
{
  GstMveMux *mvemux = GST_MVE_MUX (object);

  if (mvemux->lock) {
    g_mutex_free (mvemux->lock);
    mvemux->lock = NULL;
  }

  if (mvemux->audio_buffer) {
    g_queue_free (mvemux->audio_buffer);
    mvemux->audio_buffer = NULL;
  }

  if (mvemux->video_buffer) {
    g_queue_free (mvemux->video_buffer);
    mvemux->video_buffer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mve_mux_class_init (GstMveMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_mve_mux_finalize;

  gobject_class->get_property = gst_mve_mux_get_property;
  gobject_class->set_property = gst_mve_mux_set_property;

  g_object_class_install_property (gobject_class, ARG_AUDIO_COMPRESSION,
      g_param_spec_boolean ("compression", "Audio compression",
          "Whether to compress audio data", MVE_MUX_DEFAULT_COMPRESSION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_VIDEO_QUICK_ENCODING,
      g_param_spec_boolean ("quick", "Quick encoding",
          "Whether to disable expensive encoding operations", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_VIDEO_SCREEN_WIDTH,
      g_param_spec_uint ("screen-width", "Screen width",
          "Suggested screen width", 320, 1600,
          MVE_MUX_DEFAULT_SCREEN_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_VIDEO_SCREEN_HEIGHT,
      g_param_spec_uint ("screen-height", "Screen height",
          "Suggested screen height", 200, 1200,
          MVE_MUX_DEFAULT_SCREEN_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad = gst_mve_mux_request_new_pad;
  gstelement_class->release_pad = gst_mve_mux_release_pad;

  gstelement_class->change_state = gst_mve_mux_change_state;
}

static void
gst_mve_mux_init (GstMveMux * mvemux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mvemux);

  mvemux->source =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (mvemux), mvemux->source);

  mvemux->lock = g_mutex_new ();

  mvemux->audiosink = NULL;
  mvemux->videosink = NULL;
  mvemux->audio_pad_connected = FALSE;
  mvemux->video_pad_connected = FALSE;

  /* audio/video metadata initialisation */
  mvemux->last_frame = NULL;
  mvemux->second_last_frame = NULL;
  mvemux->chunk_code_map = NULL;
  mvemux->chunk_video = NULL;
  mvemux->chunk_audio = NULL;
  mvemux->audio_buffer = NULL;
  mvemux->video_buffer = NULL;

  gst_mve_mux_reset (mvemux);
}

GType
gst_mve_mux_get_type (void)
{
  static GType mvemux_type = 0;

  if (!mvemux_type) {
    static const GTypeInfo mvemux_info = {
      sizeof (GstMveMuxClass),
      (GBaseInitFunc) gst_mve_mux_base_init,
      NULL,
      (GClassInitFunc) gst_mve_mux_class_init,
      NULL,
      NULL,
      sizeof (GstMveMux),
      0,
      (GInstanceInitFunc) gst_mve_mux_init,
    };

    GST_DEBUG_CATEGORY_INIT (mvemux_debug, "mvemux",
        0, "Interplay MVE movie muxer");

    mvemux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMveMux", &mvemux_info, 0);
  }
  return mvemux_type;
}
