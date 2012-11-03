/* GStreamer NUV demuxer
 * Copyright (C) <2006> Renato Araujo Oliveira Filho <renato.filho@indt.org.br>
 *                      Rosfran Borges <rosfran.borges@indt.org.br>
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
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-nuvdemux
 * @see_also: mythtvsrc
 *
 * Demuxes MythTVs NuppelVideo .nuv file into raw or compressed audio and/or
 * video streams.
 * 
 * This element currently only supports pull-based scheduling.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc test.nuv ! nuvdemux name=demux  demux.audio_00 ! decodebin ! audioconvert ! audioresample ! autoaudiosink   demux.video_00 ! queue ! decodebin ! videoconvert ! videoscale ! autovideosink
 * ]| Play (parse and decode) an .nuv file and try to output it to
 * an automatically detected soundcard and videosink. If the NUV file contains
 * compressed audio or video data, this will only work if you have the
 * right decoder elements/plugins installed.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gsterror.h>
#include <gst/gstplugin.h>
#include <string.h>

#include "gst/gst-i18n-plugin.h"
#include "gstnuvdemux.h"

GST_DEBUG_CATEGORY_STATIC (nuvdemux_debug);
#define GST_CAT_DEFAULT nuvdemux_debug


#define GST_FLOW_ERROR_NO_DATA  -101

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_EVENT);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-nuv"));

static GstStaticPadTemplate audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

/* NUV Demux indexes init/dispose callers */
static void gst_nuv_demux_finalize (GObject * object);
static GstStateChangeReturn gst_nuv_demux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_nuv_demux_loop (GstPad * pad);
static GstFlowReturn gst_nuv_demux_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_nuv_demux_play (GstPad * pad);
static gboolean gst_nuv_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_nuv_demux_sink_activate (GstPad * sinkpad);
static GstFlowReturn gst_nuv_demux_read_bytes (GstNuvDemux * nuv, guint64 size,
    gboolean move, GstBuffer ** buffer);
static void gst_nuv_demux_reset (GstNuvDemux * nuv);
static gboolean gst_nuv_demux_handle_sink_event (GstPad * sinkpad,
    GstEvent * event);
static void gst_nuv_demux_destoy_src_pad (GstNuvDemux * nuv);
static void gst_nuv_demux_send_eos (GstNuvDemux * nuv);

/* GObject methods */
GST_BOILERPLATE (GstNuvDemux, gst_nuv_demux, GstElement, GST_TYPE_ELEMENT);

#if G_BYTE_ORDER == G_BIG_ENDIAN
static inline gdouble
_gdouble_swap_le_be (gdouble * d)
{
  union
  {
    guint64 i;
    gdouble d;
  } u;

  u.d = *d;
  u.i = GUINT64_SWAP_LE_BE (u.i);
  return u.d;
}

#define READ_DOUBLE_FROM_LE(d) (_gdouble_swap_le_be((gdouble* ) d))
#else /* G_BYTE_ORDER != G_BIG_ENDIAN */
#define READ_DOUBLE_FROM_LE(d) *((gdouble* ) (d))
#endif /* G_BYTE_ORDER != G_BIG_ENDIAN */


static void
gst_nuv_demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_static_metadata (element_class, "Nuv demuxer",
      "Codec/Demuxer",
      "Demultiplex a MythTV NuppleVideo .nuv file into audio and video",
      "Renato Araujo Oliveira Filho <renato.filho@indt.org.br>,"
      "Rosfran Borges <rosfran.borges@indt.org.br>");
}

static void
gst_nuv_demux_class_init (GstNuvDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_nuv_demux_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_nuv_demux_change_state);
}

static void
gst_nuv_demux_init (GstNuvDemux * nuv, GstNuvDemuxClass * nuv_class)
{
  nuv->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");

  gst_pad_set_activate_function (nuv->sinkpad, gst_nuv_demux_sink_activate);

  gst_pad_set_activatepull_function (nuv->sinkpad,
      gst_nuv_demux_sink_activate_pull);

  gst_pad_set_chain_function (nuv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_nuv_demux_chain));

  gst_pad_set_event_function (nuv->sinkpad, gst_nuv_demux_handle_sink_event);

  gst_element_add_pad (GST_ELEMENT (nuv), nuv->sinkpad);

  nuv->adapter = NULL;
  nuv->mpeg_buffer = NULL;
  nuv->h = NULL;
  nuv->eh = NULL;
  nuv->fh = NULL;
  gst_nuv_demux_reset (nuv);
}

static void
gst_nuv_demux_finalize (GObject * object)
{
  GstNuvDemux *nuv = GST_NUV_DEMUX (object);

  if (nuv->mpeg_buffer != NULL) {
    gst_buffer_unref (nuv->mpeg_buffer);
  }

  gst_nuv_demux_destoy_src_pad (nuv);
  gst_nuv_demux_reset (nuv);
  if (nuv->adapter != NULL) {
    g_object_unref (nuv->adapter);
    nuv->adapter = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*****************************************************************************
 * Utils functions
 *****************************************************************************/

static gboolean
gst_nuv_demux_handle_sink_event (GstPad * sinkpad, GstEvent * event)
{
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      res = TRUE;
      break;
    default:
      return gst_pad_event_default (sinkpad, event);
      break;
  }

  gst_event_unref (event);
  return res;
}


/* HeaderLoad:
 */
static GstFlowReturn
gst_nuv_demux_header_load (GstNuvDemux * nuv, nuv_header ** h_ret)
{
  GstBuffer *buffer = NULL;
  GstFlowReturn res;
  nuv_header *h;

  res = gst_nuv_demux_read_bytes (nuv, 72, TRUE, &buffer);
  if (res != GST_FLOW_OK)
    return res;

  h = g_new0 (nuv_header, 1);

  memcpy (h->id, buffer->data, 12);
  memcpy (h->version, buffer->data + 12, 5);
  h->i_width = GST_READ_UINT32_LE (&buffer->data[20]);
  h->i_height = GST_READ_UINT32_LE (&buffer->data[24]);
  h->i_width_desired = GST_READ_UINT32_LE (&buffer->data[28]);
  h->i_height_desired = GST_READ_UINT32_LE (&buffer->data[32]);
  h->i_mode = buffer->data[36];
  h->d_aspect = READ_DOUBLE_FROM_LE (&buffer->data[40]);
  h->d_fps = READ_DOUBLE_FROM_LE (&buffer->data[48]);
  h->i_video_blocks = GST_READ_UINT32_LE (&buffer->data[56]);
  h->i_audio_blocks = GST_READ_UINT32_LE (&buffer->data[60]);
  h->i_text_blocks = GST_READ_UINT32_LE (&buffer->data[64]);
  h->i_keyframe_distance = GST_READ_UINT32_LE (&buffer->data[68]);

  GST_DEBUG_OBJECT (nuv,
      "nuv: h=%s v=%s %dx%d a=%f fps=%f v=%d a=%d t=%d kfd=%d", h->id,
      h->version, h->i_width, h->i_height, h->d_aspect, h->d_fps,
      h->i_video_blocks, h->i_audio_blocks, h->i_text_blocks,
      h->i_keyframe_distance);

  *h_ret = h;
  gst_buffer_unref (buffer);
  return res;
}

static GstFlowReturn
gst_nuv_demux_stream_header_data (GstNuvDemux * nuv)
{
  GstFlowReturn res = gst_nuv_demux_header_load (nuv, &nuv->h);

  if (res == GST_FLOW_OK)
    nuv->state = GST_NUV_DEMUX_EXTRA_DATA;
  return res;
}

/*
 * Read NUV file tag
 */
static GstFlowReturn
gst_nuv_demux_stream_file_header (GstNuvDemux * nuv)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstBuffer *file_header = NULL;

  res = gst_nuv_demux_read_bytes (nuv, 12, FALSE, &file_header);
  if (res != GST_FLOW_OK) {
    return res;
  } else {
    if (strncmp ((gchar *) file_header->data, "MythTVVideo", 11) ||
        strncmp ((gchar *) file_header->data, "NuppelVideo", 11)) {
      nuv->state = GST_NUV_DEMUX_HEADER_DATA;
    } else {
      GST_DEBUG_OBJECT (nuv, "error parsing file header");
      nuv->state = GST_NUV_DEMUX_INVALID_DATA;
      res = GST_FLOW_ERROR;
    }
  }
  if (file_header != NULL) {
    gst_buffer_unref (file_header);
  }
  return res;
}

/* FrameHeaderLoad:
 */
static GstFlowReturn
gst_nuv_demux_frame_header_load (GstNuvDemux * nuv, nuv_frame_header ** h_ret)
{
  unsigned char *data;
  nuv_frame_header *h;
  GstBuffer *buf = NULL;

  GstFlowReturn res = gst_nuv_demux_read_bytes (nuv, 12, TRUE, &buf);

  if (res != GST_FLOW_OK) {
    if (buf != NULL) {
      gst_buffer_unref (buf);
    }
    return res;
  }

  h = g_new0 (nuv_frame_header, 1);
  data = buf->data;

  h->i_type = data[0];
  h->i_compression = data[1];
  h->i_keyframe = data[2];
  h->i_filters = data[3];

  h->i_timecode = GST_READ_UINT32_LE (&data[4]);
  h->i_length = GST_READ_UINT32_LE (&data[8]);
  GST_DEBUG_OBJECT (nuv, "frame hdr: t=%c c=%c k=%d f=0x%x timecode=%d l=%d",
      h->i_type,
      h->i_compression ? h->i_compression : ' ',
      h->i_keyframe ? h->i_keyframe : ' ',
      h->i_filters, h->i_timecode, h->i_length);

  *h_ret = h;
  gst_buffer_unref (buf);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nuv_demux_extended_header_load (GstNuvDemux * nuv,
    nuv_extended_header ** h_ret)
{
  unsigned char *data;
  GstBuffer *buff = NULL;
  nuv_extended_header *h;


  GstFlowReturn res = gst_nuv_demux_read_bytes (nuv, 512, TRUE, &buff);

  if (res != GST_FLOW_OK) {
    if (buff != NULL) {
      gst_buffer_unref (buff);
    }
    return res;
  }

  h = g_new0 (nuv_extended_header, 1);
  data = buff->data;
  h->i_version = GST_READ_UINT32_LE (&data[0]);
  h->i_video_fcc = GST_MAKE_FOURCC (data[4], data[5], data[6], data[7]);
  h->i_audio_fcc = GST_MAKE_FOURCC (data[8], data[9], data[10], data[11]);
  h->i_audio_sample_rate = GST_READ_UINT32_LE (&data[12]);
  h->i_audio_bits_per_sample = GST_READ_UINT32_LE (&data[16]);
  h->i_audio_channels = GST_READ_UINT32_LE (&data[20]);
  h->i_audio_compression_ratio = GST_READ_UINT32_LE (&data[24]);
  h->i_audio_quality = GST_READ_UINT32_LE (&data[28]);
  h->i_rtjpeg_quality = GST_READ_UINT32_LE (&data[32]);
  h->i_rtjpeg_luma_filter = GST_READ_UINT32_LE (&data[36]);
  h->i_rtjpeg_chroma_filter = GST_READ_UINT32_LE (&data[40]);
  h->i_lavc_bitrate = GST_READ_UINT32_LE (&data[44]);
  h->i_lavc_qmin = GST_READ_UINT32_LE (&data[48]);
  h->i_lavc_qmin = GST_READ_UINT32_LE (&data[52]);
  h->i_lavc_maxqdiff = GST_READ_UINT32_LE (&data[56]);
  h->i_seekable_offset = GST_READ_UINT64_LE (&data[60]);
  h->i_keyframe_adjust_offset = GST_READ_UINT64_LE (&data[68]);

  GST_DEBUG_OBJECT (nuv,
      "ex hdr: v=%d vffc=%4.4s afcc=%4.4s %dHz %dbits ach=%d acr=%d aq=%d"
      "rtjpeg q=%d lf=%d lc=%d lavc br=%d qmin=%d qmax=%d maxqdiff=%d seekableoff=%"
      G_GINT64_FORMAT " keyfao=%" G_GINT64_FORMAT, h->i_version,
      (gchar *) & h->i_video_fcc, (gchar *) & h->i_audio_fcc,
      h->i_audio_sample_rate, h->i_audio_bits_per_sample, h->i_audio_channels,
      h->i_audio_compression_ratio, h->i_audio_quality, h->i_rtjpeg_quality,
      h->i_rtjpeg_luma_filter, h->i_rtjpeg_chroma_filter, h->i_lavc_bitrate,
      h->i_lavc_qmin, h->i_lavc_qmax, h->i_lavc_maxqdiff, h->i_seekable_offset,
      h->i_keyframe_adjust_offset);

  *h_ret = h;
  gst_buffer_unref (buff);
  return res;
}

static gboolean
gst_nuv_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gst_event_unref (event);
  return FALSE;
}


static void
gst_nuv_demux_create_pads (GstNuvDemux * nuv)
{
  if (nuv->h->i_video_blocks != 0) {
    GstCaps *video_caps = NULL;

    nuv->src_video_pad =
        gst_pad_new_from_static_template (&video_src_template, "video_src");

    video_caps = gst_caps_new_simple ("video/x-divx",
        "divxversion", G_TYPE_INT, 4,
        "width", G_TYPE_INT, nuv->h->i_width,
        "height", G_TYPE_INT, nuv->h->i_height,
        "framerate", GST_TYPE_FRACTION, (gint) (nuv->h->d_fps * 1000.0f), 1000,
        "pixel-aspect-ratio", GST_TYPE_FRACTION,
        (gint) (nuv->h->d_aspect * 1000.0f), 1000, NULL);

    gst_pad_use_fixed_caps (nuv->src_video_pad);
    gst_pad_set_active (nuv->src_video_pad, TRUE);
    gst_pad_set_caps (nuv->src_video_pad, video_caps);

    gst_pad_set_event_function (nuv->src_video_pad,
        gst_nuv_demux_handle_src_event);
    gst_pad_set_active (nuv->src_video_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (nuv), nuv->src_video_pad);

    gst_caps_unref (video_caps);
  }

  if (nuv->h->i_audio_blocks != 0) {
    GstCaps *audio_caps = NULL;

    nuv->src_audio_pad =
        gst_pad_new_from_static_template (&audio_src_template, "audio_src");

    audio_caps = gst_caps_new_simple ("audio/mpeg",
        "rate", G_TYPE_INT, nuv->eh->i_audio_sample_rate,
        "format", GST_TYPE_FOURCC, nuv->eh->i_audio_fcc,
        "channels", G_TYPE_INT, nuv->eh->i_audio_channels,
        "mpegversion", G_TYPE_INT, nuv->eh->i_version, NULL);

    gst_pad_use_fixed_caps (nuv->src_audio_pad);
    gst_pad_set_active (nuv->src_audio_pad, TRUE);
    gst_pad_set_caps (nuv->src_audio_pad, audio_caps);
    gst_pad_set_active (nuv->src_audio_pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (nuv), nuv->src_audio_pad);

    gst_pad_set_event_function (nuv->src_audio_pad,
        gst_nuv_demux_handle_src_event);

    gst_caps_unref (audio_caps);
  }

  gst_element_no_more_pads (GST_ELEMENT (nuv));
}

static GstFlowReturn
gst_nuv_demux_read_head_frame (GstNuvDemux * nuv)
{
  GstFlowReturn ret = GST_FLOW_OK;

  ret = gst_nuv_demux_frame_header_load (nuv, &nuv->fh);
  if (ret != GST_FLOW_OK)
    return ret;

  nuv->state = GST_NUV_DEMUX_MOVI;
  return ret;
}

static GstFlowReturn
gst_nuv_demux_stream_data (GstNuvDemux * nuv)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf = NULL;
  nuv_frame_header *h = nuv->fh;

  if (h->i_type == 'R')
    goto done;

  if (h->i_length > 0) {
    ret = gst_nuv_demux_read_bytes (nuv, h->i_length, TRUE, &buf);
    if (ret != GST_FLOW_OK)
      return ret;

    if (h->i_timecode > 0)
      GST_BUFFER_TIMESTAMP (buf) = h->i_timecode * GST_MSECOND;
  }


  switch (h->i_type) {
    case 'V':
    {
      if (!buf)
        break;

      GST_BUFFER_OFFSET (buf) = nuv->video_offset;
      gst_buffer_set_caps (buf, GST_PAD_CAPS (nuv->src_video_pad));
      ret = gst_pad_push (nuv->src_video_pad, buf);
      nuv->video_offset++;
      break;
    }
    case 'A':
    {
      if (!buf)
        break;

      GST_BUFFER_OFFSET (buf) = nuv->audio_offset;
      gst_buffer_set_caps (buf, GST_PAD_CAPS (nuv->src_audio_pad));
      ret = gst_pad_push (nuv->src_audio_pad, buf);
      nuv->audio_offset++;
      break;
    }
    case 'S':
    {
      switch (h->i_compression) {
        case 'V':
          gst_pad_push_event (nuv->src_video_pad,
              gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME, 0, -1,
                  h->i_timecode));
          break;
        case 'A':
          gst_pad_push_event (nuv->src_audio_pad,
              gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
          break;
        default:
          break;
      }
    }
    default:
      if (buf != NULL)
        gst_buffer_unref (buf);

      break;
  }

done:
  nuv->state = GST_NUV_DEMUX_FRAME_HEADER;
  g_free (nuv->fh);
  nuv->fh = NULL;
  return ret;
}

static GstFlowReturn
gst_nuv_demux_stream_mpeg_data (GstNuvDemux * nuv)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* ffmpeg extra data */
  nuv->mpeg_buffer = NULL;
  ret =
      gst_nuv_demux_read_bytes (nuv, nuv->mpeg_data_size, TRUE,
      &nuv->mpeg_buffer);
  if (ret != GST_FLOW_OK) {
    return GST_FLOW_ERROR;
  }
  GST_BUFFER_SIZE (nuv->mpeg_buffer) = nuv->mpeg_data_size;
  nuv->state = GST_NUV_DEMUX_EXTEND_HEADER;
  return ret;
}

static GstFlowReturn
gst_nuv_demux_stream_extra_data (GstNuvDemux * nuv)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* Load 'D' */
  nuv_frame_header *h;

  ret = gst_nuv_demux_frame_header_load (nuv, &h);
  if (ret != GST_FLOW_OK)
    return ret;

  if (h->i_type != 'D') {
    g_free (h);
    return GST_FLOW_ERROR;
  }

  if (h->i_length > 0) {
    if (h->i_compression == 'F') {
      nuv->state = GST_NUV_DEMUX_MPEG_DATA;
    } else {
      g_free (h);
      return GST_FLOW_ERROR;
    }
  } else {
    nuv->state = GST_NUV_DEMUX_EXTEND_HEADER;
  }

  g_free (h);
  h = NULL;
  return ret;
}

static GstFlowReturn
gst_nuv_demux_stream_extend_header_data (GstNuvDemux * nuv)
{
  GstFlowReturn ret = GST_FLOW_OK;

  ret = gst_nuv_demux_extended_header_load (nuv, &nuv->eh);
  if (ret != GST_FLOW_OK)
    return ret;

  gst_nuv_demux_create_pads (nuv);
  nuv->state = GST_NUV_DEMUX_FRAME_HEADER;
  return ret;
}

static GstFlowReturn
gst_nuv_demux_stream_extend_header (GstNuvDemux * nuv)
{
  GstBuffer *buf = NULL;
  GstFlowReturn res = GST_FLOW_OK;

  res = gst_nuv_demux_read_bytes (nuv, 1, FALSE, &buf);
  if (res != GST_FLOW_OK) {
    if (buf != NULL) {
      gst_buffer_unref (buf);
    }
    return res;
  }

  if (buf->data[0] == 'X') {
    nuv_frame_header *h = NULL;

    gst_buffer_unref (buf);
    buf = NULL;

    res = gst_nuv_demux_frame_header_load (nuv, &h);
    if (res != GST_FLOW_OK)
      return res;

    if (h->i_length != 512) {
      g_free (h);
      return GST_FLOW_ERROR;
    }
    g_free (h);
    h = NULL;
    nuv->state = GST_NUV_DEMUX_EXTEND_HEADER_DATA;
  } else {
    nuv->state = GST_NUV_DEMUX_INVALID_DATA;
    GST_ELEMENT_ERROR (nuv, STREAM, DEMUX, (NULL),
        ("Unsupported extended header (0x%02x)", buf->data[0]));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
  return res;
}

static GstFlowReturn
gst_nuv_demux_play (GstPad * pad)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstNuvDemux *nuv = GST_NUV_DEMUX (GST_PAD_PARENT (pad));

  switch (nuv->state) {
    case GST_NUV_DEMUX_START:
      res = gst_nuv_demux_stream_file_header (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_ERROR_NO_DATA)) {
        goto pause;
      }
      if (nuv->state != GST_NUV_DEMUX_HEADER_DATA)
        break;

    case GST_NUV_DEMUX_HEADER_DATA:
      res = gst_nuv_demux_stream_header_data (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_ERROR_NO_DATA)) {
        goto pause;
      }
      if (nuv->state != GST_NUV_DEMUX_EXTRA_DATA)
        break;

    case GST_NUV_DEMUX_EXTRA_DATA:
      res = gst_nuv_demux_stream_extra_data (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_ERROR_NO_DATA)) {
        goto pause;
      }
      if (nuv->state != GST_NUV_DEMUX_MPEG_DATA)
        break;

    case GST_NUV_DEMUX_MPEG_DATA:
      res = gst_nuv_demux_stream_mpeg_data (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_ERROR_NO_DATA)) {
        goto pause;
      }

      if (nuv->state != GST_NUV_DEMUX_EXTEND_HEADER)
        break;

    case GST_NUV_DEMUX_EXTEND_HEADER:
      res = gst_nuv_demux_stream_extend_header (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_ERROR_NO_DATA)) {
        goto pause;
      }
      if (nuv->state != GST_NUV_DEMUX_EXTEND_HEADER_DATA)
        break;

    case GST_NUV_DEMUX_EXTEND_HEADER_DATA:
      res = gst_nuv_demux_stream_extend_header_data (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_ERROR_NO_DATA)) {
        goto pause;
      }

      if (nuv->state != GST_NUV_DEMUX_FRAME_HEADER)
        break;

    case GST_NUV_DEMUX_FRAME_HEADER:
      res = gst_nuv_demux_read_head_frame (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_ERROR_NO_DATA)) {
        goto pause;
      }
      if (nuv->state != GST_NUV_DEMUX_MOVI)
        break;

    case GST_NUV_DEMUX_MOVI:
      res = gst_nuv_demux_stream_data (nuv);
      if ((res != GST_FLOW_OK) && (res != GST_FLOW_CUSTOM_ERROR)) {
        goto pause;
      }
      break;
    case GST_NUV_DEMUX_INVALID_DATA:
      goto pause;
      break;
    default:
      g_assert_not_reached ();
  }

  GST_DEBUG_OBJECT (nuv, "state: %d res:%s", nuv->state,
      gst_flow_get_name (res));

  return GST_FLOW_OK;

pause:
  GST_LOG_OBJECT (nuv, "pausing task, reason %s", gst_flow_get_name (res));
  gst_pad_pause_task (nuv->sinkpad);
  if (res == GST_FLOW_UNEXPECTED) {
    gst_nuv_demux_send_eos (nuv);
  } else if (res == GST_FLOW_NOT_LINKED || res < GST_FLOW_UNEXPECTED) {
    GST_ELEMENT_ERROR (nuv, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("streaming stopped, reason %s", gst_flow_get_name (res)));

    gst_nuv_demux_send_eos (nuv);
  }
  return res;
}

static void
gst_nuv_demux_send_eos (GstNuvDemux * nuv)
{
  gst_element_post_message (GST_ELEMENT (nuv),
      gst_message_new_segment_done (GST_OBJECT (nuv), GST_FORMAT_TIME, -1));

  if (nuv->src_video_pad)
    gst_pad_push_event (nuv->src_video_pad, gst_event_new_eos ());
  if (nuv->src_audio_pad)
    gst_pad_push_event (nuv->src_audio_pad, gst_event_new_eos ());
}

static GstFlowReturn
gst_nuv_demux_read_bytes (GstNuvDemux * nuv, guint64 size, gboolean move,
    GstBuffer ** buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (size == 0) {
    *buffer = gst_buffer_new ();
    return ret;
  }

  if (nuv->mode == 0) {
    ret = gst_pad_pull_range (nuv->sinkpad, nuv->offset, size, buffer);
    if (ret == GST_FLOW_OK) {
      if (move) {
        nuv->offset += size;
      }
      /* got eos */
    } else if (ret == GST_FLOW_UNEXPECTED) {
      gst_nuv_demux_send_eos (nuv);
      return GST_FLOW_FLUSHING;
    }
  } else {
    if (gst_adapter_available (nuv->adapter) < size)
      return GST_FLOW_ERROR_NO_DATA;

    if (move) {
      *buffer = gst_adapter_take_buffer (nuv->adapter, size);
    } else {
      guint8 *data = NULL;

      data = (guint8 *) gst_adapter_peek (nuv->adapter, size);
      *buffer = gst_buffer_new ();
      gst_buffer_set_data (*buffer, data, size);
    }
  }
  return ret;
}

static gboolean
gst_nuv_demux_sink_activate (GstPad * sinkpad)
{
  gboolean res = TRUE;
  GstNuvDemux *nuv = GST_NUV_DEMUX (gst_pad_get_parent (sinkpad));

  if (gst_pad_check_pull_range (sinkpad)) {
    nuv->mode = 0;
    if (nuv->adapter != NULL) {
      g_object_unref (nuv->adapter);
      nuv->adapter = NULL;
    }
    res = gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    nuv->mode = 1;
    if (!nuv->adapter) {
      nuv->adapter = gst_adapter_new ();
    }
    res = gst_pad_activate_push (sinkpad, TRUE);
  }

  g_object_unref (nuv);
  return res;
}

static gboolean
gst_nuv_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstNuvDemux *nuv = GST_NUV_DEMUX (gst_pad_get_parent (sinkpad));

  if (active) {
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_nuv_demux_loop, sinkpad,
        NULL);
  } else {
    gst_pad_stop_task (sinkpad);
  }
  gst_object_unref (nuv);

  return TRUE;
}

static GstFlowReturn
gst_nuv_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstNuvDemux *nuv = GST_NUV_DEMUX (gst_pad_get_parent (pad));

  gst_adapter_push (nuv->adapter, buf);

  return gst_nuv_demux_play (pad);
}

static void
gst_nuv_demux_loop (GstPad * pad)
{
  gst_nuv_demux_play (pad);
}

static void
gst_nuv_demux_reset (GstNuvDemux * nuv)
{
  nuv->state = GST_NUV_DEMUX_START;
  nuv->mode = 0;
  nuv->offset = 0;
  nuv->video_offset = 0;
  nuv->audio_offset = 0;

  if (nuv->adapter != NULL)
    gst_adapter_clear (nuv->adapter);

  if (nuv->mpeg_buffer != NULL) {
    gst_buffer_unref (nuv->mpeg_buffer);
    nuv->mpeg_buffer = NULL;
  }

  g_free (nuv->h);
  nuv->h = NULL;

  g_free (nuv->eh);
  nuv->eh = NULL;

  g_free (nuv->fh);
  nuv->fh = NULL;
}

static void
gst_nuv_demux_destoy_src_pad (GstNuvDemux * nuv)
{
  if (nuv->src_video_pad) {
    gst_element_remove_pad (GST_ELEMENT (nuv), nuv->src_video_pad);
    nuv->src_video_pad = NULL;
  }

  if (nuv->src_audio_pad) {
    gst_element_remove_pad (GST_ELEMENT (nuv), nuv->src_audio_pad);
    nuv->src_audio_pad = NULL;
  }
}

static GstStateChangeReturn
gst_nuv_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_nuv_demux_destoy_src_pad (GST_NUV_DEMUX (element));
      gst_nuv_demux_reset (GST_NUV_DEMUX (element));
      break;
    default:
      break;
  }

done:
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (nuvdemux_debug, "nuvdemux",
      0, "Demuxer for NUV streams");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  if (!gst_element_register (plugin, "nuvdemux", GST_RANK_SECONDARY,
          GST_TYPE_NUV_DEMUX)) {
    return FALSE;
  }
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nuvdemux,
    "Demuxes MythTV NuppelVideo files",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
