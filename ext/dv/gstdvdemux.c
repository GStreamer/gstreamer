/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2005> Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>

#include <gst/audio/audio.h>
#include "gstdvdemux.h"
#include "gstsmptetimecode.h"

/**
 * SECTION:element-dvdemux
 *
 * dvdemux splits raw DV into its audio and video components. The audio will be
 * decoded raw samples and the video will be encoded DV video.
 *
 * This element can operate in both push and pull mode depending on the
 * capabilities of the upstream peer.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.dv ! dvdemux name=demux ! queue ! audioconvert ! alsasink demux. ! queue ! dvdec ! xvimagesink
 * ]| This pipeline decodes and renders the raw DV stream to an audio and a videosink.
 * </refsect2>
 *
 * Last reviewed on 2006-02-27 (0.10.3)
 */

/* DV output has two modes, normal and wide. The resolution is the same in both
 * cases: 720 pixels wide by 576 pixels tall in PAL format, and 720x480 for
 * NTSC.
 *
 * Each of the modes has its own pixel aspect ratio, which is fixed in practice
 * by ITU-R BT.601 (also known as "CCIR-601" or "Rec.601"). Or so claims a
 * reference that I culled from the reliable "internet",
 * http://www.mir.com/DMG/aspect.html. Normal PAL is 59/54 and normal NTSC is
 * 10/11. Because the pixel resolution is the same for both cases, we can get
 * the pixel aspect ratio for wide recordings by multiplying by the ratio of
 * display aspect ratios, 16/9 (for wide) divided by 4/3 (for normal):
 *
 * Wide NTSC: 10/11 * (16/9)/(4/3) = 40/33
 * Wide PAL: 59/54 * (16/9)/(4/3) = 118/81
 *
 * However, the pixel resolution coming out of a DV source does not combine with
 * the standard pixel aspect ratios to give a proper display aspect ratio. An
 * image 480 pixels tall, with a 4:3 display aspect ratio, will be 768 pixels
 * wide. But, if we take the normal PAL aspect ratio of 59/54, and multiply it
 * with the width of the DV image (720 pixels), we get 786.666..., which is
 * nonintegral and too wide. The camera is not outputting a 4:3 image.
 * 
 * If the video sink for this stream has fixed dimensions (such as for
 * fullscreen playback, or for a java applet in a web page), you then have two
 * choices. Either you show the whole image, but pad the image with black
 * borders on the top and bottom (like watching a widescreen video on a 4:3
 * device), or you crop the video to the proper ratio. Apparently the latter is
 * the standard practice.
 *
 * For its part, GStreamer is concerned with accuracy and preservation of
 * information. This element outputs the 720x576 or 720x480 video that it
 * recieves, noting the proper aspect ratio. This should not be a problem for
 * windowed applications, which can change size to fit the video. Applications
 * with fixed size requirements should decide whether to crop or pad which
 * an element such as videobox can do.
 */

#define NTSC_HEIGHT 480
#define NTSC_BUFFER 120000
#define NTSC_FRAMERATE_NUMERATOR 30000
#define NTSC_FRAMERATE_DENOMINATOR 1001

#define PAL_HEIGHT 576
#define PAL_BUFFER 144000
#define PAL_FRAMERATE_NUMERATOR 25
#define PAL_FRAMERATE_DENOMINATOR 1

#define PAL_NORMAL_PAR_X        59
#define PAL_NORMAL_PAR_Y        54
#define PAL_WIDE_PAR_X          118
#define PAL_WIDE_PAR_Y          81

#define NTSC_NORMAL_PAR_X       10
#define NTSC_NORMAL_PAR_Y       11
#define NTSC_WIDE_PAR_X         40
#define NTSC_WIDE_PAR_Y         33

GST_DEBUG_CATEGORY_STATIC (dvdemux_debug);
#define GST_CAT_DEFAULT dvdemux_debug

static GstStaticPadTemplate sink_temp = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dv, systemstream = (boolean) true")
    );

static GstStaticPadTemplate video_src_temp = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-dv, systemstream = (boolean) false")
    );

static GstStaticPadTemplate audio_src_temp = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "depth = (int) 16, "
        "width = (int) 16, "
        "signed = (boolean) TRUE, "
        "channels = (int) {2, 4}, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "rate = (int) { 32000, 44100, 48000 }")
    );


GST_BOILERPLATE (GstDVDemux, gst_dvdemux, GstElement, GST_TYPE_ELEMENT);

static void gst_dvdemux_finalize (GObject * object);

/* query functions */
static const GstQueryType *gst_dvdemux_get_src_query_types (GstPad * pad);
static gboolean gst_dvdemux_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_dvdemux_get_sink_query_types (GstPad * pad);
static gboolean gst_dvdemux_sink_query (GstPad * pad, GstQuery * query);

/* convert functions */
static gboolean gst_dvdemux_sink_convert (GstDVDemux * demux,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value);
static gboolean gst_dvdemux_src_convert (GstDVDemux * demux, GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value);

/* event functions */
static gboolean gst_dvdemux_send_event (GstElement * element, GstEvent * event);
static gboolean gst_dvdemux_handle_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_dvdemux_handle_sink_event (GstPad * pad, GstEvent * event);

/* scheduling functions */
static void gst_dvdemux_loop (GstPad * pad);
static GstFlowReturn gst_dvdemux_flush (GstDVDemux * dvdemux);
static GstFlowReturn gst_dvdemux_chain (GstPad * pad, GstBuffer * buffer);

/* state change functions */
static gboolean gst_dvdemux_sink_activate (GstPad * sinkpad);
static gboolean gst_dvdemux_sink_activate_push (GstPad * sinkpad,
    gboolean active);
static gboolean gst_dvdemux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static GstStateChangeReturn gst_dvdemux_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_dvdemux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_temp));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_temp));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_temp));

  gst_element_class_set_details_simple (element_class,
      "DV system stream demuxer", "Codec/Demuxer",
      "Uses libdv to separate DV audio from DV video (libdv.sourceforge.net)",
      "Erik Walthinsen <omega@cse.ogi.edu>, Wim Taymans <wim@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (dvdemux_debug, "dvdemux", 0, "DV demuxer element");
}

static void
gst_dvdemux_class_init (GstDVDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_dvdemux_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_dvdemux_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_dvdemux_send_event);
}

static void
gst_dvdemux_init (GstDVDemux * dvdemux, GstDVDemuxClass * g_class)
{
  gint i;

  dvdemux->sinkpad = gst_pad_new_from_static_template (&sink_temp, "sink");
  /* we can operate in pull and push mode so we install
   * a custom activate function */
  gst_pad_set_activate_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_sink_activate));
  /* the function to activate in push mode */
  gst_pad_set_activatepush_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_sink_activate_push));
  /* the function to activate in pull mode */
  gst_pad_set_activatepull_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_sink_activate_pull));
  /* for push mode, this is the chain function */
  gst_pad_set_chain_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_chain));
  /* handling events (in push mode only) */
  gst_pad_set_event_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_handle_sink_event));
  /* query functions */
  gst_pad_set_query_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_sink_query));
  gst_pad_set_query_type_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_get_sink_query_types));

  /* now add the pad */
  gst_element_add_pad (GST_ELEMENT (dvdemux), dvdemux->sinkpad);

  dvdemux->adapter = gst_adapter_new ();

  /* we need 4 temp buffers for audio decoding which are of a static
   * size and which we can allocate here */
  for (i = 0; i < 4; i++) {
    dvdemux->audio_buffers[i] =
        (gint16 *) g_malloc (DV_AUDIO_MAX_SAMPLES * sizeof (gint16));
  }
}

static void
gst_dvdemux_finalize (GObject * object)
{
  GstDVDemux *dvdemux;
  gint i;

  dvdemux = GST_DVDEMUX (object);

  g_object_unref (dvdemux->adapter);

  /* clean up temp audio buffers */
  for (i = 0; i < 4; i++) {
    g_free (dvdemux->audio_buffers[i]);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* reset to default values before starting streaming */
static void
gst_dvdemux_reset (GstDVDemux * dvdemux)
{
  dvdemux->frame_offset = 0;
  dvdemux->audio_offset = 0;
  dvdemux->video_offset = 0;
  dvdemux->framecount = 0;
  g_atomic_int_set (&dvdemux->found_header, 0);
  dvdemux->frame_len = -1;
  dvdemux->need_segment = FALSE;
  dvdemux->new_media = FALSE;
  dvdemux->framerate_numerator = 0;
  dvdemux->framerate_denominator = 0;
  dvdemux->height = 0;
  dvdemux->frequency = 0;
  dvdemux->channels = 0;
  dvdemux->wide = FALSE;
  gst_segment_init (&dvdemux->byte_segment, GST_FORMAT_BYTES);
  gst_segment_init (&dvdemux->time_segment, GST_FORMAT_TIME);
}

static GstPad *
gst_dvdemux_add_pad (GstDVDemux * dvdemux, GstStaticPadTemplate * template)
{
  gboolean no_more_pads;
  GstPad *pad;

  pad = gst_pad_new_from_static_template (template, template->name_template);

  gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_dvdemux_src_query));

  gst_pad_set_query_type_function (pad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_get_src_query_types));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_handle_src_event));
  gst_pad_use_fixed_caps (pad);
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (dvdemux), pad);

  no_more_pads =
      (dvdemux->videosrcpad != NULL && template == &audio_src_temp) ||
      (dvdemux->audiosrcpad != NULL && template == &video_src_temp);

  if (no_more_pads)
    gst_element_no_more_pads (GST_ELEMENT (dvdemux));

  gst_pad_push_event (pad, gst_event_new_new_segment (FALSE,
          dvdemux->byte_segment.rate, GST_FORMAT_TIME,
          dvdemux->time_segment.start, dvdemux->time_segment.stop,
          dvdemux->time_segment.start));

  if (no_more_pads) {
    gst_element_found_tags (GST_ELEMENT (dvdemux),
        gst_tag_list_new_full (GST_TAG_CONTAINER_FORMAT, "DV", NULL));
  }

  return pad;
}

static void
gst_dvdemux_remove_pads (GstDVDemux * dvdemux)
{
  if (dvdemux->videosrcpad) {
    gst_element_remove_pad (GST_ELEMENT (dvdemux), dvdemux->videosrcpad);
    dvdemux->videosrcpad = NULL;
  }
  if (dvdemux->audiosrcpad) {
    gst_element_remove_pad (GST_ELEMENT (dvdemux), dvdemux->audiosrcpad);
    dvdemux->audiosrcpad = NULL;
  }
}

static gboolean
gst_dvdemux_src_convert (GstDVDemux * dvdemux, GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value)
{
  gboolean res = TRUE;

  if (*dest_format == src_format || src_value == -1) {
    *dest_value = src_value;
    goto done;
  }

  if (dvdemux->frame_len <= 0)
    goto error;

  if (dvdemux->decoder == NULL)
    goto error;

  GST_INFO_OBJECT (pad,
      "src_value:%" G_GINT64_FORMAT ", src_format:%d, dest_format:%d",
      src_value, src_format, *dest_format);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (pad == dvdemux->videosrcpad)
            *dest_value = src_value / dvdemux->frame_len;
          else if (pad == dvdemux->audiosrcpad)
            *dest_value = src_value / (2 * dvdemux->channels);
          break;
        case GST_FORMAT_TIME:
          *dest_format = GST_FORMAT_TIME;
          if (pad == dvdemux->videosrcpad)
            *dest_value = gst_util_uint64_scale (src_value,
                GST_SECOND * dvdemux->framerate_denominator,
                dvdemux->frame_len * dvdemux->framerate_numerator);
          else if (pad == dvdemux->audiosrcpad)
            *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
                2 * dvdemux->frequency * dvdemux->channels);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          if (pad == dvdemux->videosrcpad)
            *dest_value = gst_util_uint64_scale (src_value,
                dvdemux->frame_len * dvdemux->framerate_numerator,
                dvdemux->framerate_denominator * GST_SECOND);
          else if (pad == dvdemux->audiosrcpad)
            *dest_value = gst_util_uint64_scale_int (src_value,
                2 * dvdemux->frequency * dvdemux->channels, GST_SECOND);
          break;
        case GST_FORMAT_DEFAULT:
          if (pad == dvdemux->videosrcpad) {
            if (src_value)
              *dest_value = gst_util_uint64_scale (src_value,
                  dvdemux->framerate_numerator,
                  dvdemux->framerate_denominator * GST_SECOND);
            else
              *dest_value = 0;
          } else if (pad == dvdemux->audiosrcpad) {
            *dest_value = gst_util_uint64_scale (src_value,
                dvdemux->frequency, GST_SECOND);
          }
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          if (pad == dvdemux->videosrcpad) {
            *dest_value = gst_util_uint64_scale (src_value,
                GST_SECOND * dvdemux->framerate_denominator,
                dvdemux->framerate_numerator);
          } else if (pad == dvdemux->audiosrcpad) {
            if (src_value)
              *dest_value = gst_util_uint64_scale (src_value,
                  GST_SECOND, dvdemux->frequency);
            else
              *dest_value = 0;
          }
          break;
        case GST_FORMAT_BYTES:
          if (pad == dvdemux->videosrcpad) {
            *dest_value = src_value * dvdemux->frame_len;
          } else if (pad == dvdemux->audiosrcpad) {
            *dest_value = src_value * 2 * dvdemux->channels;
          }
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

done:
  GST_INFO_OBJECT (pad,
      "Result : dest_format:%d, dest_value:%" G_GINT64_FORMAT ", res:%d",
      *dest_format, *dest_value, res);
  return res;

  /* ERRORS */
error:
  {
    GST_INFO ("source conversion failed");
    return FALSE;
  }
}

static gboolean
gst_dvdemux_sink_convert (GstDVDemux * dvdemux, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (dvdemux, "%d -> %d", src_format, *dest_format);
  GST_INFO_OBJECT (dvdemux,
      "src_value:%" G_GINT64_FORMAT ", src_format:%d, dest_format:%d",
      src_value, src_format, *dest_format);

  if (*dest_format == GST_FORMAT_DEFAULT)
    *dest_format = GST_FORMAT_TIME;

  if (*dest_format == src_format || src_value == -1) {
    *dest_value = src_value;
    goto done;
  }

  if (dvdemux->frame_len <= 0)
    goto error;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
        {
          guint64 frame;

          /* get frame number, rounds down so don't combine this
           * line and the next line. */
          frame = src_value / dvdemux->frame_len;

          *dest_value = gst_util_uint64_scale (frame,
              GST_SECOND * dvdemux->framerate_denominator,
              dvdemux->framerate_numerator);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
        {
          guint64 frame;

          /* calculate the frame */
          frame =
              gst_util_uint64_scale (src_value, dvdemux->framerate_numerator,
              dvdemux->framerate_denominator * GST_SECOND);

          /* calculate the offset from the rounded frame */
          *dest_value = frame * dvdemux->frame_len;
          break;
        }
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  GST_INFO_OBJECT (dvdemux,
      "Result : dest_format:%d, dest_value:%" G_GINT64_FORMAT ", res:%d",
      *dest_format, *dest_value, res);

done:
  return res;

error:
  {
    GST_INFO_OBJECT (dvdemux, "sink conversion failed");
    return FALSE;
  }
}

static const GstQueryType *
gst_dvdemux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return src_query_types;
}

static gboolean
gst_dvdemux_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstDVDemux *dvdemux;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 cur;

      /* get target format */
      gst_query_parse_position (query, &format, NULL);

      /* bring the position to the requested format. */
      if (!(res = gst_dvdemux_src_convert (dvdemux, pad,
                  GST_FORMAT_TIME, dvdemux->time_segment.last_stop,
                  &format, &cur)))
        goto error;
      gst_query_set_position (query, format, cur);
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      GstFormat format2;
      gint64 end;

      /* First ask the peer in the original format */
      if (!gst_pad_peer_query (dvdemux->sinkpad, query)) {
        /* get target format */
        gst_query_parse_duration (query, &format, NULL);

        /* change query to bytes to perform on peer */
        gst_query_set_duration (query, GST_FORMAT_BYTES, -1);

        /* Now ask the peer in BYTES format and try to convert */
        if (!gst_pad_peer_query (dvdemux->sinkpad, query)) {
          goto error;
        }

        /* get peer total length */
        gst_query_parse_duration (query, NULL, &end);

        /* convert end to requested format */
        if (end != -1) {
          format2 = format;
          if (!(res = gst_dvdemux_sink_convert (dvdemux,
                      GST_FORMAT_BYTES, end, &format2, &end))) {
            goto error;
          }
          gst_query_set_duration (query, format, end);
        }
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_dvdemux_src_convert (dvdemux, pad, src_fmt, src_val,
                  &dest_fmt, &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (dvdemux);

  return res;

  /* ERRORS */
error:
  {
    gst_object_unref (dvdemux);
    GST_DEBUG ("error source query");
    return FALSE;
  }
}

static const GstQueryType *
gst_dvdemux_get_sink_query_types (GstPad * pad)
{
  static const GstQueryType sink_query_types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return sink_query_types;
}

static gboolean
gst_dvdemux_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstDVDemux *dvdemux;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_dvdemux_sink_convert (dvdemux, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (dvdemux);

  return res;

  /* ERRORS */
error:
  {
    gst_object_unref (dvdemux);
    GST_DEBUG ("error handling sink query");
    return FALSE;
  }
}

/* takes ownership of the event */
static gboolean
gst_dvdemux_push_event (GstDVDemux * dvdemux, GstEvent * event)
{
  gboolean res = FALSE;

  if (dvdemux->videosrcpad) {
    gst_event_ref (event);
    res |= gst_pad_push_event (dvdemux->videosrcpad, event);
  }

  if (dvdemux->audiosrcpad)
    res |= gst_pad_push_event (dvdemux->audiosrcpad, event);
  else
    gst_event_unref (event);

  return res;
}

static gboolean
gst_dvdemux_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstDVDemux *dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      /* we are not blocking on anything exect the push() calls
       * to the peer which will be unblocked by forwarding the
       * event.*/
      res = gst_dvdemux_push_event (dvdemux, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (dvdemux->adapter);
      GST_DEBUG ("cleared adapter");
      gst_segment_init (&dvdemux->byte_segment, GST_FORMAT_BYTES);
      gst_segment_init (&dvdemux->time_segment, GST_FORMAT_TIME);
      res = gst_dvdemux_push_event (dvdemux, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 start, stop, time;

      /* parse byte start and stop positions */
      gst_event_parse_new_segment (event, &update, &rate, &format,
          &start, &stop, &time);

      switch (format) {
        case GST_FORMAT_BYTES:
          gst_segment_set_newsegment (&dvdemux->byte_segment, update,
              rate, format, start, stop, time);

          /* the update can always be sent */
          if (update) {
            GstEvent *update;

            update = gst_event_new_new_segment (TRUE,
                dvdemux->time_segment.rate, dvdemux->time_segment.format,
                dvdemux->time_segment.start, dvdemux->time_segment.last_stop,
                dvdemux->time_segment.time);

            gst_dvdemux_push_event (dvdemux, update);
          } else {
            /* and queue a SEGMENT before sending the next set of buffers, we
             * cannot convert to time yet as we might not know the size of the
             * frames, etc.. */
            dvdemux->need_segment = TRUE;
          }
          gst_event_unref (event);
          break;
        case GST_FORMAT_TIME:
          gst_segment_set_newsegment (&dvdemux->time_segment, update,
              rate, format, start, stop, time);

          /* and we can just forward this time event */
          res = gst_dvdemux_push_event (dvdemux, event);
          break;
        default:
          gst_event_unref (event);
          /* cannot accept this format */
          res = FALSE;
          break;
      }
      break;
    }
    case GST_EVENT_EOS:
      /* flush any pending data, should be nothing left. */
      gst_dvdemux_flush (dvdemux);
      /* forward event */
      res = gst_dvdemux_push_event (dvdemux, event);
      /* and clear the adapter */
      gst_adapter_clear (dvdemux->adapter);
      break;
    default:
      res = gst_dvdemux_push_event (dvdemux, event);
      break;
  }

  gst_object_unref (dvdemux);

  return res;
}

/* convert a pair of values on the given srcpad */
static gboolean
gst_dvdemux_convert_src_pair (GstDVDemux * dvdemux, GstPad * pad,
    GstFormat src_format, gint64 src_start, gint64 src_stop,
    GstFormat dst_format, gint64 * dst_start, gint64 * dst_stop)
{
  gboolean res;

  GST_INFO ("starting conversion of start");
  /* bring the format to time on srcpad. */
  if (!(res = gst_dvdemux_src_convert (dvdemux, pad,
              src_format, src_start, &dst_format, dst_start))) {
    goto done;
  }
  GST_INFO ("Finished conversion of start: %" G_GINT64_FORMAT, *dst_start);

  GST_INFO ("starting conversion of stop");
  /* bring the format to time on srcpad. */
  if (!(res = gst_dvdemux_src_convert (dvdemux, pad,
              src_format, src_stop, &dst_format, dst_stop))) {
    /* could not convert seek format to time offset */
    goto done;
  }
  GST_INFO ("Finished conversion of stop: %" G_GINT64_FORMAT, *dst_stop);
done:
  return res;
}

/* convert a pair of values on the sinkpad */
static gboolean
gst_dvdemux_convert_sink_pair (GstDVDemux * dvdemux,
    GstFormat src_format, gint64 src_start, gint64 src_stop,
    GstFormat dst_format, gint64 * dst_start, gint64 * dst_stop)
{
  gboolean res;

  GST_INFO ("starting conversion of start");
  /* bring the format to time on srcpad. */
  if (!(res = gst_dvdemux_sink_convert (dvdemux,
              src_format, src_start, &dst_format, dst_start))) {
    goto done;
  }
  GST_INFO ("Finished conversion of start: %" G_GINT64_FORMAT, *dst_start);

  GST_INFO ("starting conversion of stop");
  /* bring the format to time on srcpad. */
  if (!(res = gst_dvdemux_sink_convert (dvdemux,
              src_format, src_stop, &dst_format, dst_stop))) {
    /* could not convert seek format to time offset */
    goto done;
  }
  GST_INFO ("Finished conversion of stop: %" G_GINT64_FORMAT, *dst_stop);
done:
  return res;
}

/* convert a pair of values on the srcpad to a pair of
 * values on the sinkpad 
 */
static gboolean
gst_dvdemux_convert_src_to_sink (GstDVDemux * dvdemux, GstPad * pad,
    GstFormat src_format, gint64 src_start, gint64 src_stop,
    GstFormat dst_format, gint64 * dst_start, gint64 * dst_stop)
{
  GstFormat conv;
  gboolean res;

  conv = GST_FORMAT_TIME;
  /* convert to TIME intermediate format */
  if (!(res = gst_dvdemux_convert_src_pair (dvdemux, pad,
              src_format, src_start, src_stop, conv, dst_start, dst_stop))) {
    /* could not convert format to time offset */
    goto done;
  }
  /* convert to dst format on sinkpad */
  if (!(res = gst_dvdemux_convert_sink_pair (dvdemux,
              conv, *dst_start, *dst_stop, dst_format, dst_start, dst_stop))) {
    /* could not convert format to time offset */
    goto done;
  }
done:
  return res;
}

#if 0
static gboolean
gst_dvdemux_convert_segment (GstDVDemux * dvdemux, GstSegment * src,
    GstSegment * dest)
{
  dest->rate = src->rate;
  dest->abs_rate = src->abs_rate;
  dest->flags = src->flags;

  return TRUE;
}
#endif

/* handle seek in push base mode.
 *
 * Convert the time seek to a bytes seek and send it
 * upstream
 * Does not take ownership of the event.
 */
static gboolean
gst_dvdemux_handle_push_seek (GstDVDemux * dvdemux, GstPad * pad,
    GstEvent * event)
{
  gboolean res = FALSE;
  gdouble rate;
  GstSeekFlags flags;
  GstFormat format;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 start_position, end_position;
  GstEvent *newevent;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  /* First try if upstream can handle time based seeks */
  if (format == GST_FORMAT_TIME)
    res = gst_pad_push_event (dvdemux->sinkpad, gst_event_ref (event));

  if (!res) {
    /* we convert the start/stop on the srcpad to the byte format
     * on the sinkpad and forward the event */
    res = gst_dvdemux_convert_src_to_sink (dvdemux, pad,
        format, cur, stop, GST_FORMAT_BYTES, &start_position, &end_position);
    if (!res)
      goto done;

    /* now this is the updated seek event on bytes */
    newevent = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
        cur_type, start_position, stop_type, end_position);

    res = gst_pad_push_event (dvdemux->sinkpad, newevent);
  }
done:
  return res;
}

/* position ourselves to the configured segment, used in pull mode.
 * The input segment is in TIME format. We convert the time values
 * to bytes values into our byte_segment which we use to pull data from
 * the sinkpad peer.
 */
static gboolean
gst_dvdemux_do_seek (GstDVDemux * demux, GstSegment * segment)
{
  gboolean res;
  GstFormat format;

  /* position to value configured is last_stop, this will round down
   * to the byte position where the frame containing the given 
   * timestamp can be found. */
  format = GST_FORMAT_BYTES;
  res = gst_dvdemux_sink_convert (demux,
      segment->format, segment->last_stop,
      &format, &demux->byte_segment.last_stop);
  if (!res)
    goto done;

  /* update byte segment start */
  gst_dvdemux_sink_convert (demux,
      segment->format, segment->start, &format, &demux->byte_segment.start);

  /* update byte segment stop */
  gst_dvdemux_sink_convert (demux,
      segment->format, segment->stop, &format, &demux->byte_segment.stop);

  /* update byte segment time */
  gst_dvdemux_sink_convert (demux,
      segment->format, segment->time, &format, &demux->byte_segment.time);

  /* calculate current frame number */
  format = GST_FORMAT_DEFAULT;
  gst_dvdemux_src_convert (demux, demux->videosrcpad,
      segment->format, segment->start, &format, &demux->video_offset);

  /* calculate current audio number */
  format = GST_FORMAT_DEFAULT;
  gst_dvdemux_src_convert (demux, demux->audiosrcpad,
      segment->format, segment->start, &format, &demux->audio_offset);

  /* every DV frame corresponts with one video frame */
  demux->frame_offset = demux->video_offset;

done:
  return res;
}

/* handle seek in pull base mode.
 *
 * Does not take ownership of the event.
 */
static gboolean
gst_dvdemux_handle_pull_seek (GstDVDemux * demux, GstPad * pad,
    GstEvent * event)
{
  gboolean res;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean update;
  GstSegment seeksegment;

  GST_DEBUG_OBJECT (demux, "doing seek");

  /* first bring the event format to TIME, our native format
   * to perform the seek on */
  if (event) {
    GstFormat conv;

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* can't seek backwards yet */
    if (rate <= 0.0)
      goto wrong_rate;

    /* convert input format to TIME */
    conv = GST_FORMAT_TIME;
    if (!(gst_dvdemux_convert_src_pair (demux, pad,
                format, cur, stop, conv, &cur, &stop)))
      goto no_format;

    format = GST_FORMAT_TIME;
  } else {
    flags = 0;
  }

  flush = flags & GST_SEEK_FLAG_FLUSH;

  /* send flush start */
  if (flush)
    gst_dvdemux_push_event (demux, gst_event_new_flush_start ());
  else
    gst_pad_pause_task (demux->sinkpad);

  /* grab streaming lock, this should eventually be possible, either
   * because the task is paused or our streaming thread stopped
   * because our peer is flushing. */
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  /* make copy into temp structure, we can only update the main one
   * when the subclass actually could to the seek. */
  memcpy (&seeksegment, &demux->time_segment, sizeof (GstSegment));

  /* now configure the seek segment */
  if (event) {
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  GST_DEBUG_OBJECT (demux, "segment configured from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT ", position %" G_GINT64_FORMAT,
      seeksegment.start, seeksegment.stop, seeksegment.last_stop);

  /* do the seek, segment.last_stop contains new position. */
  res = gst_dvdemux_do_seek (demux, &seeksegment);

  /* and prepare to continue streaming */
  if (flush) {
    /* send flush stop, peer will accept data and events again. We
     * are not yet providing data as we still have the STREAM_LOCK. */
    gst_dvdemux_push_event (demux, gst_event_new_flush_stop ());
  } else if (res && demux->running) {
    /* we are running the current segment and doing a non-flushing seek,
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (demux, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, demux->time_segment.start,
        demux->time_segment.last_stop);

    gst_dvdemux_push_event (demux,
        gst_event_new_new_segment (TRUE,
            demux->time_segment.rate, demux->time_segment.format,
            demux->time_segment.start, demux->time_segment.last_stop,
            demux->time_segment.time));
  }

  /* if successfull seek, we update our real segment and push
   * out the new segment. */
  if (res) {
    memcpy (&demux->time_segment, &seeksegment, sizeof (GstSegment));

    if (demux->time_segment.flags & GST_SEEK_FLAG_SEGMENT) {
      gst_element_post_message (GST_ELEMENT_CAST (demux),
          gst_message_new_segment_start (GST_OBJECT_CAST (demux),
              demux->time_segment.format, demux->time_segment.last_stop));
    }
    if ((stop = demux->time_segment.stop) == -1)
      stop = demux->time_segment.duration;

    GST_INFO_OBJECT (demux,
        "Saving newsegment event to be sent in streaming thread");

    if (demux->pending_segment)
      gst_event_unref (demux->pending_segment);

    demux->pending_segment = gst_event_new_new_segment (FALSE,
        demux->time_segment.rate, demux->time_segment.format,
        demux->time_segment.last_stop, stop, demux->time_segment.time);

    demux->need_segment = FALSE;
  }

  demux->running = TRUE;
  /* and restart the task in case it got paused explicitely or by
   * the FLUSH_START event we pushed out. */
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_dvdemux_loop,
      demux->sinkpad);

  /* and release the lock again so we can continue streaming */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;

  /* ERRORS */
wrong_rate:
  {
    GST_DEBUG_OBJECT (demux, "negative playback rate %lf not supported.", rate);
    return FALSE;
  }
no_format:
  {
    GST_DEBUG_OBJECT (demux, "cannot convert to TIME format, seek aborted.");
    return FALSE;
  }
}

static gboolean
gst_dvdemux_send_event (GstElement * element, GstEvent * event)
{
  GstDVDemux *dvdemux = GST_DVDEMUX (element);
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      /* checking header and configuring the seek must be atomic */
      GST_OBJECT_LOCK (dvdemux);
      if (g_atomic_int_get (&dvdemux->found_header) == 0) {
        GstEvent **event_p;

        event_p = &dvdemux->seek_event;

        /* We don't have pads yet. Keep the event. */
        GST_INFO_OBJECT (dvdemux, "Keeping the seek event for later");

        gst_event_replace (event_p, event);
        GST_OBJECT_UNLOCK (dvdemux);

        res = TRUE;
      } else {
        GST_OBJECT_UNLOCK (dvdemux);

        if (dvdemux->seek_handler) {
          res = dvdemux->seek_handler (dvdemux, dvdemux->videosrcpad, event);
          gst_event_unref (event);
        }
      }
      break;
    }
    default:
      res = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;
  }

  return res;
}

/* handle an event on the source pad, it's most likely a seek */
static gboolean
gst_dvdemux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstDVDemux *dvdemux;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* seek handler is installed based on scheduling mode */
      if (dvdemux->seek_handler)
        res = dvdemux->seek_handler (dvdemux, pad, event);
      else
        res = FALSE;
      break;
    case GST_EVENT_QOS:
      /* we can't really (yet) do QoS */
      res = FALSE;
      break;
    case GST_EVENT_NAVIGATION:
      /* no navigation either... */
      res = FALSE;
      break;
    default:
      res = gst_pad_push_event (dvdemux->sinkpad, event);
      event = NULL;
      break;
  }
  if (event)
    gst_event_unref (event);

  gst_object_unref (dvdemux);

  return res;
}

/* does not take ownership of buffer */
static GstFlowReturn
gst_dvdemux_demux_audio (GstDVDemux * dvdemux, GstBuffer * buffer,
    guint64 duration)
{
  gint num_samples;
  GstFlowReturn ret;
  const guint8 *data;

  data = GST_BUFFER_DATA (buffer);

  dv_decode_full_audio (dvdemux->decoder, data, dvdemux->audio_buffers);

  if (G_LIKELY ((num_samples = dv_get_num_samples (dvdemux->decoder)) > 0)) {
    gint16 *a_ptr;
    gint i, j;
    GstBuffer *outbuf;
    gint frequency, channels;

    if (G_UNLIKELY (dvdemux->audiosrcpad == NULL))
      dvdemux->audiosrcpad = gst_dvdemux_add_pad (dvdemux, &audio_src_temp);

    /* get initial format or check if format changed */
    frequency = dv_get_frequency (dvdemux->decoder);
    channels = dv_get_num_channels (dvdemux->decoder);

    if (G_UNLIKELY ((frequency != dvdemux->frequency)
            || (channels != dvdemux->channels))) {
      GstCaps *caps;

      dvdemux->frequency = frequency;
      dvdemux->channels = channels;

      /* and set new caps */
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "rate", G_TYPE_INT, frequency,
          "depth", G_TYPE_INT, 16,
          "width", G_TYPE_INT, 16,
          "signed", G_TYPE_BOOLEAN, TRUE,
          "channels", G_TYPE_INT, channels,
          "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
      gst_pad_set_caps (dvdemux->audiosrcpad, caps);
      gst_caps_unref (caps);
    }

    outbuf = gst_buffer_new_and_alloc (num_samples *
        sizeof (gint16) * dvdemux->channels);

    a_ptr = (gint16 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < num_samples; i++) {
      for (j = 0; j < dvdemux->channels; j++) {
        *(a_ptr++) = dvdemux->audio_buffers[j][i];
      }
    }

    GST_DEBUG ("pushing audio %" GST_TIME_FORMAT,
        GST_TIME_ARGS (dvdemux->time_segment.last_stop));

    GST_BUFFER_TIMESTAMP (outbuf) = dvdemux->time_segment.last_stop;
    GST_BUFFER_DURATION (outbuf) = duration;
    GST_BUFFER_OFFSET (outbuf) = dvdemux->audio_offset;
    dvdemux->audio_offset += num_samples;
    GST_BUFFER_OFFSET_END (outbuf) = dvdemux->audio_offset;

    if (dvdemux->new_media)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (dvdemux->audiosrcpad));

    ret = gst_pad_push (dvdemux->audiosrcpad, outbuf);
  } else {
    /* no samples */
    ret = GST_FLOW_OK;
  }

  return ret;
}

/* takes ownership of buffer */
static GstFlowReturn
gst_dvdemux_demux_video (GstDVDemux * dvdemux, GstBuffer * buffer,
    guint64 duration)
{
  GstBuffer *outbuf;
  gint height;
  gboolean wide;
  GstFlowReturn ret = GST_FLOW_OK;

  if (G_UNLIKELY (dvdemux->videosrcpad == NULL))
    dvdemux->videosrcpad = gst_dvdemux_add_pad (dvdemux, &video_src_temp);

  /* get params */
  /* framerate is already up-to-date */
  height = dvdemux->decoder->height;
  wide = dv_format_wide (dvdemux->decoder);

  /* see if anything changed */
  if (G_UNLIKELY ((dvdemux->height != height) || dvdemux->wide != wide)) {
    GstCaps *caps;
    gint par_x, par_y;

    dvdemux->height = height;
    dvdemux->wide = wide;

    if (dvdemux->decoder->system == e_dv_system_625_50) {
      if (wide) {
        par_x = PAL_WIDE_PAR_X;
        par_y = PAL_WIDE_PAR_Y;
      } else {
        par_x = PAL_NORMAL_PAR_X;
        par_y = PAL_NORMAL_PAR_Y;
      }
    } else {
      if (wide) {
        par_x = NTSC_WIDE_PAR_X;
        par_y = NTSC_WIDE_PAR_Y;
      } else {
        par_x = NTSC_NORMAL_PAR_X;
        par_y = NTSC_NORMAL_PAR_Y;
      }
    }

    caps = gst_caps_new_simple ("video/x-dv",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "width", G_TYPE_INT, 720,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, dvdemux->framerate_numerator,
        dvdemux->framerate_denominator,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_x, par_y, NULL);
    gst_pad_set_caps (dvdemux->videosrcpad, caps);
    gst_caps_unref (caps);
  }

  /* takes ownership of buffer here, we just need to modify
   * the metadata. */
  outbuf = gst_buffer_make_metadata_writable (buffer);

  GST_BUFFER_TIMESTAMP (outbuf) = dvdemux->time_segment.last_stop;
  GST_BUFFER_OFFSET (outbuf) = dvdemux->video_offset;
  GST_BUFFER_OFFSET_END (outbuf) = dvdemux->video_offset + 1;
  GST_BUFFER_DURATION (outbuf) = duration;

  if (dvdemux->new_media)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (dvdemux->videosrcpad));

  GST_DEBUG ("pushing video %" GST_TIME_FORMAT,
      GST_TIME_ARGS (dvdemux->time_segment.last_stop));

  ret = gst_pad_push (dvdemux->videosrcpad, outbuf);

  dvdemux->video_offset++;

  return ret;
}

static int
get_ssyb_offset (int dif, int ssyb)
{
  int offset;

  offset = dif * 12000;         /* to dif */
  offset += 80 * (1 + (ssyb / 6));      /* to subcode pack */
  offset += 3;                  /* past header */
  offset += 8 * (ssyb % 6);     /* to ssyb */

  return offset;
}

static gboolean
gst_dvdemux_get_timecode (GstDVDemux * dvdemux, GstBuffer * buffer,
    GstSMPTETimeCode * timecode)
{
  guint8 *data = GST_BUFFER_DATA (buffer);
  int offset;
  int dif;
  int n_difs = dvdemux->decoder->num_dif_seqs;

  for (dif = 0; dif < n_difs; dif++) {
    offset = get_ssyb_offset (dif, 3);
    if (data[offset + 3] == 0x13) {
      timecode->frames = ((data[offset + 4] >> 4) & 0x3) * 10 +
          (data[offset + 4] & 0xf);
      timecode->seconds = ((data[offset + 5] >> 4) & 0x3) * 10 +
          (data[offset + 5] & 0xf);
      timecode->minutes = ((data[offset + 6] >> 4) & 0x3) * 10 +
          (data[offset + 6] & 0xf);
      timecode->hours = ((data[offset + 7] >> 4) & 0x3) * 10 +
          (data[offset + 7] & 0xf);
      GST_DEBUG ("got timecode %" GST_SMPTE_TIME_CODE_FORMAT,
          GST_SMPTE_TIME_CODE_ARGS (timecode));
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
gst_dvdemux_is_new_media (GstDVDemux * dvdemux, GstBuffer * buffer)
{
  guint8 *data = GST_BUFFER_DATA (buffer);
  int aaux_offset;
  int dif;
  int n_difs;

  n_difs = dvdemux->decoder->num_dif_seqs;

  for (dif = 0; dif < n_difs; dif++) {
    if (dif & 1) {
      aaux_offset = (dif * 12000) + (6 + 16 * 1) * 80 + 3;
    } else {
      aaux_offset = (dif * 12000) + (6 + 16 * 4) * 80 + 3;
    }
    if (data[aaux_offset + 0] == 0x51) {
      if ((data[aaux_offset + 2] & 0x80) == 0)
        return TRUE;
    }
  }

  return FALSE;
}

/* takes ownership of buffer */
static GstFlowReturn
gst_dvdemux_demux_frame (GstDVDemux * dvdemux, GstBuffer * buffer)
{
  GstClockTime next_ts;
  GstFlowReturn aret, vret, ret;
  guint8 *data;
  guint64 duration;
  GstSMPTETimeCode timecode;
  int frame_number;

  if (G_UNLIKELY (dvdemux->need_segment)) {
    GstEvent *event;
    GstFormat format;

    /* convert to time and store as start/end_timestamp */
    format = GST_FORMAT_TIME;
    if (!(gst_dvdemux_convert_sink_pair (dvdemux,
                GST_FORMAT_BYTES, dvdemux->byte_segment.start,
                dvdemux->byte_segment.stop, format,
                &dvdemux->time_segment.start, &dvdemux->time_segment.stop)))
      goto segment_error;

    dvdemux->time_segment.rate = dvdemux->byte_segment.rate;
    dvdemux->time_segment.abs_rate = dvdemux->byte_segment.abs_rate;
    dvdemux->time_segment.last_stop = dvdemux->time_segment.start;

    /* calculate current frame number */
    format = GST_FORMAT_DEFAULT;
    if (!(gst_dvdemux_src_convert (dvdemux, dvdemux->videosrcpad,
                GST_FORMAT_TIME, dvdemux->time_segment.start,
                &format, &dvdemux->frame_offset)))
      goto segment_error;

    GST_DEBUG_OBJECT (dvdemux, "sending segment start: %" GST_TIME_FORMAT
        ", stop: %" GST_TIME_FORMAT ", time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (dvdemux->time_segment.start),
        GST_TIME_ARGS (dvdemux->time_segment.stop),
        GST_TIME_ARGS (dvdemux->time_segment.start));

    event = gst_event_new_new_segment (FALSE, dvdemux->byte_segment.rate,
        GST_FORMAT_TIME, dvdemux->time_segment.start,
        dvdemux->time_segment.stop, dvdemux->time_segment.start);
    gst_dvdemux_push_event (dvdemux, event);

    dvdemux->need_segment = FALSE;
  }

  gst_dvdemux_get_timecode (dvdemux, buffer, &timecode);
  gst_smpte_time_code_get_frame_number (
      (dvdemux->decoder->system == e_dv_system_625_50) ?
      GST_SMPTE_TIME_CODE_SYSTEM_25 : GST_SMPTE_TIME_CODE_SYSTEM_30,
      &frame_number, &timecode);

  next_ts = gst_util_uint64_scale_int (
      (dvdemux->frame_offset + 1) * GST_SECOND,
      dvdemux->framerate_denominator, dvdemux->framerate_numerator);
  duration = next_ts - dvdemux->time_segment.last_stop;

  data = GST_BUFFER_DATA (buffer);

  dv_parse_packs (dvdemux->decoder, data);
  dvdemux->new_media = FALSE;
  if (gst_dvdemux_is_new_media (dvdemux, buffer) &&
      dvdemux->frames_since_new_media > 2) {
    dvdemux->new_media = TRUE;
    dvdemux->frames_since_new_media = 0;
  }
  dvdemux->frames_since_new_media++;

  /* does not take ownership of buffer */
  aret = ret = gst_dvdemux_demux_audio (dvdemux, buffer, duration);
  if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)) {
    gst_buffer_unref (buffer);
    goto done;
  }

  /* takes ownership of buffer */
  vret = ret = gst_dvdemux_demux_video (dvdemux, buffer, duration);
  if (G_UNLIKELY (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED))
    goto done;

  /* if both are not linked, we stop */
  if (G_UNLIKELY (aret == GST_FLOW_NOT_LINKED && vret == GST_FLOW_NOT_LINKED)) {
    ret = GST_FLOW_NOT_LINKED;
    goto done;
  }

  gst_segment_set_last_stop (&dvdemux->time_segment, GST_FORMAT_TIME, next_ts);
  dvdemux->frame_offset++;

  /* check for the end of the segment */
  if (dvdemux->time_segment.stop != -1 && next_ts > dvdemux->time_segment.stop)
    ret = GST_FLOW_UNEXPECTED;
  else
    ret = GST_FLOW_OK;

done:
  return ret;

  /* ERRORS */
segment_error:
  {
    GST_DEBUG ("error generating new_segment event");
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

/* flush any remaining data in the adapter, used in chain based scheduling mode */
static GstFlowReturn
gst_dvdemux_flush (GstDVDemux * dvdemux)
{
  GstFlowReturn ret = GST_FLOW_OK;

  while (gst_adapter_available (dvdemux->adapter) >= dvdemux->frame_len) {
    const guint8 *data;
    gint length;

    /* get the accumulated bytes */
    data = gst_adapter_peek (dvdemux->adapter, dvdemux->frame_len);

    /* parse header to know the length and other params */
    if (G_UNLIKELY (dv_parse_header (dvdemux->decoder, data) < 0))
      goto parse_header_error;

    /* after parsing the header we know the length of the data */
    length = dvdemux->frame_len = dvdemux->decoder->frame_size;
    if (dvdemux->decoder->system == e_dv_system_625_50) {
      dvdemux->framerate_numerator = PAL_FRAMERATE_NUMERATOR;
      dvdemux->framerate_denominator = PAL_FRAMERATE_DENOMINATOR;
    } else {
      dvdemux->framerate_numerator = NTSC_FRAMERATE_NUMERATOR;
      dvdemux->framerate_denominator = NTSC_FRAMERATE_DENOMINATOR;
    }
    g_atomic_int_set (&dvdemux->found_header, 1);

    /* let demux_video set the height, it needs to detect when things change so
     * it can reset caps */

    /* if we still have enough for a frame, start decoding */
    if (G_LIKELY (gst_adapter_available (dvdemux->adapter) >= length)) {
      GstBuffer *buffer;

      data = gst_adapter_take (dvdemux->adapter, length);

      /* create buffer for the remainder of the code */
      buffer = gst_buffer_new ();
      GST_BUFFER_DATA (buffer) = (guint8 *) data;
      GST_BUFFER_SIZE (buffer) = length;
      GST_BUFFER_MALLOCDATA (buffer) = (guint8 *) data;

      /* and decode the buffer, takes ownership */
      ret = gst_dvdemux_demux_frame (dvdemux, buffer);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        goto done;
    }
  }
done:
  return ret;

  /* ERRORS */
parse_header_error:
  {
    GST_ELEMENT_ERROR (dvdemux, STREAM, DECODE,
        (NULL), ("Error parsing DV header"));
    return GST_FLOW_ERROR;
  }
}

/* streaming operation: 
 *
 * accumulate data until we have a frame, then decode. 
 */
static GstFlowReturn
gst_dvdemux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstDVDemux *dvdemux;
  GstFlowReturn ret;
  GstClockTime timestamp;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  /* a discontinuity in the stream, we need to get rid of
   * accumulated data in the adapter and assume a new frame
   * starts after the discontinuity */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)))
    gst_adapter_clear (dvdemux->adapter);

  /* a timestamp always should be respected */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    gst_segment_set_last_stop (&dvdemux->time_segment, GST_FORMAT_TIME,
        timestamp);
    /* FIXME, adjust frame_offset and other counters */
  }

  gst_adapter_push (dvdemux->adapter, buffer);

  /* Apparently dv_parse_header can read from the body of the frame
   * too, so it needs more than header_size bytes. Wacky!
   */
  if (G_UNLIKELY (dvdemux->frame_len == -1)) {
    /* if we don't know the length of a frame, we assume it is
     * the NTSC_BUFFER length, as this is enough to figure out 
     * if this is PAL or NTSC */
    dvdemux->frame_len = NTSC_BUFFER;
  }

  /* and try to flush pending frames */
  ret = gst_dvdemux_flush (dvdemux);

  gst_object_unref (dvdemux);

  return ret;
}

/* pull based operation.
 *
 * Read header first to figure out the frame size. Then read
 * and decode full frames.
 */
static void
gst_dvdemux_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstDVDemux *dvdemux;
  GstBuffer *buffer = NULL;
  const guint8 *data;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  if (G_UNLIKELY (g_atomic_int_get (&dvdemux->found_header) == 0)) {
    GST_DEBUG_OBJECT (dvdemux, "pulling first buffer");
    /* pull in NTSC sized buffer to figure out the frame
     * length */
    ret = gst_pad_pull_range (dvdemux->sinkpad,
        dvdemux->byte_segment.last_stop, NTSC_BUFFER, &buffer);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto pause;

    /* check buffer size, don't want to read small buffers */
    if (G_UNLIKELY (GST_BUFFER_SIZE (buffer) < NTSC_BUFFER))
      goto small_buffer;

    data = GST_BUFFER_DATA (buffer);

    /* parse header to know the length and other params */
    if (G_UNLIKELY (dv_parse_header (dvdemux->decoder, data) < 0))
      goto parse_header_error;

    /* after parsing the header we know the length of the data */
    dvdemux->frame_len = dvdemux->decoder->frame_size;
    if (dvdemux->decoder->system == e_dv_system_625_50) {
      dvdemux->framerate_numerator = PAL_FRAMERATE_NUMERATOR;
      dvdemux->framerate_denominator = PAL_FRAMERATE_DENOMINATOR;
    } else {
      dvdemux->framerate_numerator = NTSC_FRAMERATE_NUMERATOR;
      dvdemux->framerate_denominator = NTSC_FRAMERATE_DENOMINATOR;
    }
    dvdemux->need_segment = TRUE;

    /* see if we need to read a larger part */
    if (dvdemux->frame_len != NTSC_BUFFER) {
      gst_buffer_unref (buffer);
      buffer = NULL;
    }

    {
      GstEvent *event;

      /* setting header and prrforming the seek must be atomic */
      GST_OBJECT_LOCK (dvdemux);
      /* got header now */
      g_atomic_int_set (&dvdemux->found_header, 1);

      /* now perform pending seek if any. */
      event = dvdemux->seek_event;
      if (event)
        gst_event_ref (event);
      GST_OBJECT_UNLOCK (dvdemux);

      if (event) {
        if (!gst_dvdemux_handle_pull_seek (dvdemux, dvdemux->videosrcpad,
                event)) {
          GST_ELEMENT_WARNING (dvdemux, STREAM, DECODE, (NULL),
              ("Error perfoming initial seek"));
        }
        gst_event_unref (event);

        /* and we need to pull a new buffer in all cases. */
        if (buffer) {
          gst_buffer_unref (buffer);
          buffer = NULL;
        }
      }
    }
  }


  if (G_UNLIKELY (dvdemux->pending_segment)) {

    /* now send the newsegment */
    GST_DEBUG_OBJECT (dvdemux, "Sending newsegment from");

    gst_dvdemux_push_event (dvdemux, dvdemux->pending_segment);
    dvdemux->pending_segment = NULL;
  }

  if (G_LIKELY (buffer == NULL)) {
    GST_DEBUG_OBJECT (dvdemux, "pulling buffer at offset %" G_GINT64_FORMAT,
        dvdemux->byte_segment.last_stop);

    ret = gst_pad_pull_range (dvdemux->sinkpad,
        dvdemux->byte_segment.last_stop, dvdemux->frame_len, &buffer);
    if (ret != GST_FLOW_OK)
      goto pause;

    /* check buffer size, don't want to read small buffers */
    if (GST_BUFFER_SIZE (buffer) < dvdemux->frame_len)
      goto small_buffer;
  }
  /* and decode the buffer */
  ret = gst_dvdemux_demux_frame (dvdemux, buffer);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pause;

  /* and position ourselves for the next buffer */
  dvdemux->byte_segment.last_stop += dvdemux->frame_len;

done:
  gst_object_unref (dvdemux);

  return;

  /* ERRORS */
parse_header_error:
  {
    GST_ELEMENT_ERROR (dvdemux, STREAM, DECODE,
        (NULL), ("Error parsing DV header"));
    gst_buffer_unref (buffer);
    dvdemux->running = FALSE;
    gst_pad_pause_task (dvdemux->sinkpad);
    gst_dvdemux_push_event (dvdemux, gst_event_new_eos ());
    goto done;
  }
small_buffer:
  {
    GST_ELEMENT_ERROR (dvdemux, STREAM, DECODE,
        (NULL), ("Error reading buffer"));
    gst_buffer_unref (buffer);
    dvdemux->running = FALSE;
    gst_pad_pause_task (dvdemux->sinkpad);
    gst_dvdemux_push_event (dvdemux, gst_event_new_eos ());
    goto done;
  }
pause:
  {
    GST_INFO_OBJECT (dvdemux, "pausing task, %s", gst_flow_get_name (ret));
    dvdemux->running = FALSE;
    gst_pad_pause_task (dvdemux->sinkpad);
    if (ret == GST_FLOW_UNEXPECTED) {
      GST_LOG_OBJECT (dvdemux, "got eos");
      /* perform EOS logic */
      if (dvdemux->time_segment.flags & GST_SEEK_FLAG_SEGMENT) {
        gst_element_post_message (GST_ELEMENT (dvdemux),
            gst_message_new_segment_done (GST_OBJECT_CAST (dvdemux),
                dvdemux->time_segment.format, dvdemux->time_segment.last_stop));
      } else {
        gst_dvdemux_push_event (dvdemux, gst_event_new_eos ());
      }
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_UNEXPECTED) {
      /* for fatal errors or not-linked we post an error message */
      GST_ELEMENT_ERROR (dvdemux, STREAM, FAILED,
          (NULL), ("streaming stopped, reason %s", gst_flow_get_name (ret)));
      gst_dvdemux_push_event (dvdemux, gst_event_new_eos ());
    }
    goto done;
  }
}

static gboolean
gst_dvdemux_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstDVDemux *demux = GST_DVDEMUX (gst_pad_get_parent (sinkpad));

  if (active) {
    demux->seek_handler = gst_dvdemux_handle_push_seek;
  } else {
    demux->seek_handler = NULL;
  }
  gst_object_unref (demux);

  return TRUE;
}

static gboolean
gst_dvdemux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstDVDemux *demux = GST_DVDEMUX (gst_pad_get_parent (sinkpad));

  if (active) {
    demux->running = TRUE;
    demux->seek_handler = gst_dvdemux_handle_pull_seek;
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_dvdemux_loop, sinkpad);
  } else {
    demux->seek_handler = NULL;
    gst_pad_stop_task (sinkpad);
    demux->running = FALSE;
  }

  gst_object_unref (demux);

  return TRUE;
};

/* decide on push or pull based scheduling */
static gboolean
gst_dvdemux_sink_activate (GstPad * sinkpad)
{
  gboolean ret;

  if (gst_pad_check_pull_range (sinkpad))
    ret = gst_pad_activate_pull (sinkpad, TRUE);
  else
    ret = gst_pad_activate_push (sinkpad, TRUE);

  return ret;
};

static GstStateChangeReturn
gst_dvdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstDVDemux *dvdemux = GST_DVDEMUX (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dvdemux->decoder = dv_decoder_new (0, FALSE, FALSE);
      dv_set_error_log (dvdemux->decoder, NULL);
      gst_dvdemux_reset (dvdemux);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (dvdemux->adapter);
      dv_decoder_free (dvdemux->decoder);
      dvdemux->decoder = NULL;

      gst_dvdemux_remove_pads (dvdemux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
      GstEvent **event_p;

      event_p = &dvdemux->seek_event;
      gst_event_replace (event_p, NULL);
      if (dvdemux->pending_segment)
        gst_event_unref (dvdemux->pending_segment);
      dvdemux->pending_segment = NULL;
      break;
    }
    default:
      break;
  }
  return ret;
}
