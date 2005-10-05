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
#define NTSC_FRAMERATE 30000/1001.

#define PAL_HEIGHT 576
#define PAL_BUFFER 144000
#define PAL_FRAMERATE 25.0

#define PAL_NORMAL_PAR_X	59
#define PAL_NORMAL_PAR_Y	54
#define PAL_WIDE_PAR_X		118
#define PAL_WIDE_PAR_Y		81

#define NTSC_NORMAL_PAR_X	10
#define NTSC_NORMAL_PAR_Y	11
#define NTSC_WIDE_PAR_X		40
#define NTSC_WIDE_PAR_Y		33

static GstElementDetails dvdemux_details =
GST_ELEMENT_DETAILS ("DV system stream demuxer",
    "Codec/Demuxer",
    "Uses libdv to separate DV audio from DV video",
    "Erik Walthinsen <omega@cse.ogi.edu>, Wim Taymans <wim@fluendo.com>");

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


static const GstQueryType *gst_dvdemux_get_src_query_types (GstPad * pad);
static gboolean gst_dvdemux_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_dvdemux_get_sink_query_types (GstPad * pad);
static gboolean gst_dvdemux_sink_query (GstPad * pad, GstQuery * query);

static gboolean gst_dvdemux_sink_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_dvdemux_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static gboolean gst_dvdemux_handle_src_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_dvdemux_flush (GstDVDemux * dvdemux);
static GstFlowReturn gst_dvdemux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_dvdemux_handle_sink_event (GstPad * pad, GstEvent * event);

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

  gst_element_class_set_details (element_class, &dvdemux_details);
}

static void
gst_dvdemux_class_init (GstDVDemuxClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state = gst_dvdemux_change_state;

  /* table initialization, only do once */
  dv_init (0, 0);
}

static void
gst_dvdemux_init (GstDVDemux * dvdemux, GstDVDemuxClass * g_class)
{
  gint i;

  dvdemux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_temp),
      "sink");
  gst_pad_set_chain_function (dvdemux->sinkpad, gst_dvdemux_chain);
  gst_pad_set_event_function (dvdemux->sinkpad, gst_dvdemux_handle_sink_event);
  gst_pad_set_query_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_sink_query));
  gst_pad_set_query_type_function (dvdemux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_get_sink_query_types));
  gst_element_add_pad (GST_ELEMENT (dvdemux), dvdemux->sinkpad);

  dvdemux->adapter = gst_adapter_new ();

  dvdemux->timestamp = 0LL;

  dvdemux->start_timestamp = -1LL;
  dvdemux->stop_timestamp = -1LL;
  dvdemux->need_discont = FALSE;
  dvdemux->new_media = FALSE;
  dvdemux->framerate = 0;
  dvdemux->height = 0;
  dvdemux->frequency = 0;
  dvdemux->channels = 0;
  dvdemux->wide = FALSE;

  for (i = 0; i < 4; i++) {
    dvdemux->audio_buffers[i] =
        (gint16 *) g_malloc (DV_AUDIO_MAX_SAMPLES * sizeof (gint16));
  }
}

static void
gst_dvdemux_add_pads (GstDVDemux * dvdemux)
{
  dvdemux->videosrcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&video_src_temp),
      "video");
  gst_pad_set_query_function (dvdemux->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_src_query));
  gst_pad_set_query_type_function (dvdemux->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_get_src_query_types));
  gst_pad_set_event_function (dvdemux->videosrcpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_handle_src_event));
  gst_pad_use_fixed_caps (dvdemux->videosrcpad);
  gst_element_add_pad (GST_ELEMENT (dvdemux), dvdemux->videosrcpad);

  dvdemux->audiosrcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&audio_src_temp),
      "audio");
  gst_pad_set_query_function (dvdemux->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_src_query));
  gst_pad_set_query_type_function (dvdemux->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_get_src_query_types));
  gst_pad_set_event_function (dvdemux->audiosrcpad,
      GST_DEBUG_FUNCPTR (gst_dvdemux_handle_src_event));
  gst_pad_use_fixed_caps (dvdemux->audiosrcpad);
  gst_element_add_pad (GST_ELEMENT (dvdemux), dvdemux->audiosrcpad);
}

static gboolean
gst_dvdemux_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstDVDemux *dvdemux;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));
  if (dvdemux->frame_len == -1)
    goto error;

  if (dvdemux->decoder == NULL)
    goto error;

  if (*dest_format == src_format) {
    *dest_value = src_value;
    goto done;
  }

  GST_INFO ("src_value:%lld, src_format:%d, dest_format:%d", src_value,
      src_format, *dest_format);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (pad == dvdemux->videosrcpad)
            *dest_value = src_value / dvdemux->frame_len;
          else if (pad == dvdemux->audiosrcpad)
            *dest_value = src_value / gst_audio_frame_byte_size (pad);
          break;
        case GST_FORMAT_TIME:
          *dest_format = GST_FORMAT_TIME;
          if (pad == dvdemux->videosrcpad)
            *dest_value = src_value * GST_SECOND /
                (dvdemux->frame_len * dvdemux->framerate);
          else if (pad == dvdemux->audiosrcpad)
            *dest_value = src_value * GST_SECOND /
                (2 * dvdemux->frequency * dvdemux->channels);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          if (pad == dvdemux->videosrcpad)
            *dest_value = src_value * dvdemux->frame_len * dvdemux->framerate
                / GST_SECOND;
          else if (pad == dvdemux->audiosrcpad)
            *dest_value = 2 * src_value * dvdemux->frequency *
                dvdemux->channels / GST_SECOND;
          break;
        case GST_FORMAT_DEFAULT:
          if (pad == dvdemux->videosrcpad) {
            if (src_value)
              *dest_value = src_value * dvdemux->framerate / GST_SECOND;
            else
              *dest_value = 0;
          } else if (pad == dvdemux->audiosrcpad) {
            *dest_value = 2 * src_value * dvdemux->frequency *
                dvdemux->channels / (GST_SECOND *
                gst_audio_frame_byte_size (pad));
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
            *dest_value = src_value * GST_SECOND * dvdemux->framerate;
          } else if (pad == dvdemux->audiosrcpad) {
            if (src_value)
              *dest_value =
                  src_value * GST_SECOND * gst_audio_frame_byte_size (pad)
                  / (2 * dvdemux->frequency * dvdemux->channels);
            else
              *dest_value = 0;
          }
          break;
        case GST_FORMAT_BYTES:
          if (pad == dvdemux->videosrcpad) {
            *dest_value = src_value * dvdemux->frame_len;
          } else if (pad == dvdemux->audiosrcpad) {
            *dest_value = src_value * gst_audio_frame_byte_size (pad);
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
  gst_object_unref (dvdemux);

  return res;

error:
  {
    gst_object_unref (dvdemux);
    return FALSE;
  }
}

static gboolean
gst_dvdemux_sink_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstDVDemux *dvdemux;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  if (dvdemux->frame_len <= 0)
    goto error;

  GST_DEBUG ("%d -> %d", src_format, *dest_format);

  if (*dest_format == GST_FORMAT_DEFAULT)
    *dest_format = GST_FORMAT_TIME;

  if (*dest_format == src_format) {
    *dest_value = src_value;
    goto done;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
        {
          guint64 frame;

          /* get frame number */
          frame = src_value / dvdemux->frame_len;

          *dest_value = (frame * GST_SECOND) / dvdemux->framerate;
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
          frame = src_value * dvdemux->framerate / GST_SECOND;
          /* calculate the offset */
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

done:
  gst_object_unref (dvdemux);
  return res;

error:
  {
    gst_object_unref (dvdemux);
    return FALSE;
  }
}

static const GstQueryType *
gst_dvdemux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_query_types[] = {
    GST_QUERY_POSITION,
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
      GstFormat format2;
      gint64 cur, end;
      GstPad *peer;

      /* get target format */
      gst_query_parse_position (query, &format, NULL, NULL);

      /* change query to perform on peer */
      gst_query_set_position (query, GST_FORMAT_BYTES, -1, -1);

      if ((peer = gst_pad_get_peer (dvdemux->sinkpad))) {
        /* ask peer for total length */
        if (!(res = gst_pad_query (peer, query))) {
          gst_object_unref (peer);
          goto error;
        }

        /* get peer total length */
        gst_query_parse_position (query, NULL, NULL, &end);

        /* convert end to requested format */
        if (end != -1) {
          format2 = format;
          if (!(res = gst_pad_query_convert (dvdemux->sinkpad,
                      GST_FORMAT_BYTES, end, &format2, &end))) {
            gst_object_unref (peer);
            goto error;
          }
        }
        gst_object_unref (peer);
      } else {
        end = -1;
      }
      /* bring the position to the requested format. */
      if (!(res = gst_pad_query_convert (pad,
                  GST_FORMAT_TIME, dvdemux->timestamp, &format, &cur)))
        goto error;
      if (!(res = gst_pad_query_convert (pad, format2, end, &format, &end)))
        goto error;
      gst_query_set_position (query, format, cur, end);
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_dvdemux_src_convert (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = FALSE;
      break;
  }
  gst_object_unref (dvdemux);

  return res;

error:
  {
    gst_object_unref (dvdemux);
    GST_DEBUG ("error handling event");
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
              gst_dvdemux_sink_convert (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = FALSE;
      break;
  }
  gst_object_unref (dvdemux);

  return res;

error:
  {
    gst_object_unref (dvdemux);
    GST_DEBUG ("error handling event");
    return FALSE;
  }
}

static gboolean
gst_dvdemux_send_event (GstDVDemux * dvdemux, GstEvent * event)
{
  gboolean res = FALSE;

  gst_event_ref (event);
  if (dvdemux->videosrcpad)
    res |= gst_pad_push_event (dvdemux->videosrcpad, event);
  if (dvdemux->audiosrcpad)
    res |= gst_pad_push_event (dvdemux->audiosrcpad, event);

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
      res = gst_dvdemux_send_event (dvdemux, event);

      /* and wait till streaming stops, not strictly needed as
       * the peer calling us will do the same. */
      GST_STREAM_LOCK (pad);
      GST_STREAM_UNLOCK (pad);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_STREAM_LOCK (pad);
      gst_adapter_clear (dvdemux->adapter);
      GST_DEBUG ("cleared adapter");
      res = gst_dvdemux_send_event (dvdemux, event);
      GST_STREAM_UNLOCK (pad);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;

      GST_STREAM_LOCK (pad);

      /* parse byte start and stop positions */
      gst_event_parse_newsegment (event, NULL, &format,
          &dvdemux->start_byte, &dvdemux->stop_byte, NULL);

      /* and queue a DISCONT before sending the next set of buffers */
      dvdemux->need_discont = TRUE;
      gst_event_unref (event);
      GST_STREAM_UNLOCK (pad);
      break;
    }
    case GST_EVENT_EOS:
    default:
      GST_STREAM_LOCK (pad);
      /* flush any pending data */
      gst_dvdemux_flush (dvdemux);
      /* forward event */
      res = gst_dvdemux_send_event (dvdemux, event);
      /* and clear the adapter */
      gst_adapter_clear (dvdemux->adapter);
      GST_STREAM_UNLOCK (pad);
      break;
  }

  gst_object_unref (dvdemux);

  return res;
}

static gboolean
gst_dvdemux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstDVDemux *dvdemux;

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstEvent *newevent;
      gint64 offset;
      GstFormat format, conv;
      gint64 cur, stop;
      gdouble rate;
      GstSeekType cur_type, stop_type;
      GstSeekFlags flags;
      gint64 start_position, end_position;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      if ((offset = cur) != -1) {
        /* bring the format to time on srcpad. */
        conv = GST_FORMAT_TIME;
        if (!(res = gst_pad_query_convert (pad,
                    format, offset, &conv, &start_position))) {
          /* could not convert seek format to time offset */
          break;
        }
        /* and convert to bytes on sinkpad. */
        conv = GST_FORMAT_BYTES;
        if (!(res = gst_pad_query_convert (dvdemux->sinkpad,
                    GST_FORMAT_TIME, start_position, &conv, &start_position))) {
          /* could not convert time format to bytes offset */
          break;
        }
      } else {
        start_position = -1;
      }

      if ((offset = stop) != -1) {
        /* bring the format to time on srcpad. */
        conv = GST_FORMAT_TIME;
        if (!(res = gst_pad_query_convert (pad,
                    format, offset, &conv, &end_position))) {
          /* could not convert seek format to time offset */
          break;
        }
        conv = GST_FORMAT_BYTES;
        /* and convert to bytes on sinkpad. */
        if (!(res = gst_pad_query_convert (dvdemux->sinkpad,
                    GST_FORMAT_TIME, end_position, &conv, &end_position))) {
          /* could not convert seek format to bytes offset */
          break;
        }
      } else {
        end_position = -1;
      }
      /* now this is the updated seek event on bytes */
      newevent = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
          cur_type, start_position, stop_type, end_position);

      res = gst_pad_push_event (dvdemux->sinkpad, newevent);
      break;
    }
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);

  gst_object_unref (dvdemux);

  return res;
}

static GstFlowReturn
gst_dvdemux_demux_audio (GstDVDemux * dvdemux, const guint8 * data)
{
  gint num_samples;
  gint frequency, channels;
  GstFlowReturn ret;

  frequency = dv_get_frequency (dvdemux->decoder);
  channels = dv_get_num_channels (dvdemux->decoder);

  /* check if format changed */
  if ((frequency != dvdemux->frequency) || (channels != dvdemux->channels)) {
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

  dv_decode_full_audio (dvdemux->decoder, data, dvdemux->audio_buffers);

  if ((num_samples = dv_get_num_samples (dvdemux->decoder)) > 0) {
    gint16 *a_ptr;
    gint i, j;
    GstBuffer *outbuf;

    outbuf = gst_buffer_new_and_alloc (num_samples *
        sizeof (gint16) * dvdemux->channels);

    a_ptr = (gint16 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < num_samples; i++) {
      for (j = 0; j < dvdemux->channels; j++) {
        *(a_ptr++) = dvdemux->audio_buffers[j][i];
      }
    }

    GST_DEBUG ("pushing audio %" GST_TIME_FORMAT,
        GST_TIME_ARGS (dvdemux->timestamp));

    GST_BUFFER_TIMESTAMP (outbuf) = dvdemux->timestamp;
    GST_BUFFER_DURATION (outbuf) = dvdemux->duration;
    GST_BUFFER_OFFSET (outbuf) = dvdemux->audio_offset;
    dvdemux->audio_offset += num_samples;
    GST_BUFFER_OFFSET_END (outbuf) = dvdemux->audio_offset;

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (dvdemux->audiosrcpad));

    ret = gst_pad_push (dvdemux->audiosrcpad, outbuf);
  } else {
    /* no samples */
    ret = GST_FLOW_OK;
  }

  return ret;
}

static GstFlowReturn
gst_dvdemux_demux_video (GstDVDemux * dvdemux, const guint8 * data)
{
  GstBuffer *outbuf;
  gint height;
  gboolean wide;
  GstFlowReturn ret = GST_FLOW_OK;

  /* get params */
  /* framerate is already up-to-date */
  height = (dvdemux->PAL ? PAL_HEIGHT : NTSC_HEIGHT);
  wide = dv_format_wide (dvdemux->decoder);

  /* see if anything changed */
  if ((dvdemux->height != height) || dvdemux->wide != wide) {
    GstCaps *caps;
    gint par_x, par_y;

    dvdemux->height = height;
    dvdemux->wide = wide;

    if (dvdemux->PAL) {
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
        "framerate", G_TYPE_DOUBLE, dvdemux->framerate,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, par_x, par_y, NULL);
    gst_pad_set_caps (dvdemux->videosrcpad, caps);
    gst_caps_unref (caps);
  }

  outbuf = gst_buffer_new ();

  gst_buffer_set_data (outbuf, (guint8 *) data, dvdemux->frame_len);
  outbuf->malloc_data = GST_BUFFER_DATA (outbuf);

  GST_BUFFER_TIMESTAMP (outbuf) = dvdemux->timestamp;
  GST_BUFFER_OFFSET (outbuf) = dvdemux->video_offset;
  GST_BUFFER_OFFSET_END (outbuf) = dvdemux->video_offset + 1;
  GST_BUFFER_DURATION (outbuf) = dvdemux->duration;

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (dvdemux->videosrcpad));

  GST_DEBUG ("pushing video %" GST_TIME_FORMAT,
      GST_TIME_ARGS (dvdemux->timestamp));

  ret = gst_pad_push (dvdemux->videosrcpad, outbuf);

  dvdemux->video_offset++;

  return ret;
}

static GstFlowReturn
gst_dvdemux_demux_frame (GstDVDemux * dvdemux, const guint8 * data)
{
  GstClockTime next_ts;
  GstFlowReturn aret, vret, ret;

  if (dvdemux->need_discont) {
    GstEvent *event;
    GstFormat format;
    gboolean res;

    /* convert to time and store as start/end_timestamp */
    format = GST_FORMAT_TIME;
    if (!(res = gst_pad_query_convert (dvdemux->sinkpad,
                GST_FORMAT_BYTES,
                dvdemux->start_byte, &format, &dvdemux->start_timestamp))) {
      goto discont_error;
    }

    dvdemux->timestamp = dvdemux->start_timestamp;

    if (dvdemux->stop_byte == -1) {
      dvdemux->stop_timestamp = -1;
    } else {
      format = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_convert (dvdemux->sinkpad,
                  GST_FORMAT_BYTES,
                  dvdemux->stop_byte, &format, &dvdemux->stop_timestamp))) {
        goto discont_error;
      }
    }

    event = gst_event_new_newsegment (1.0, GST_FORMAT_TIME,
        dvdemux->start_timestamp, dvdemux->stop_timestamp, 0);
    gst_dvdemux_send_event (dvdemux, event);

    dvdemux->need_discont = FALSE;
  }

  next_ts = dvdemux->timestamp + GST_SECOND / dvdemux->framerate;
  dvdemux->duration = next_ts - dvdemux->timestamp;

  dv_parse_packs (dvdemux->decoder, data);
  if (dv_is_new_recording (dvdemux->decoder, data))
    dvdemux->new_media = TRUE;

  aret = ret = gst_dvdemux_demux_audio (dvdemux, data);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)
    goto done;

  vret = ret = gst_dvdemux_demux_video (dvdemux, data);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)
    goto done;

  if (aret == GST_FLOW_NOT_LINKED && vret == GST_FLOW_NOT_LINKED) {
    ret = GST_FLOW_NOT_LINKED;
    goto done;
  }

  ret = GST_FLOW_OK;
  dvdemux->timestamp = next_ts;

done:
  return ret;

discont_error:
  {
    GST_DEBUG ("error generating discont event");
    return GST_FLOW_ERROR;
  }
}

/* flush any remaining data in the adapter */
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
    if (dv_parse_header (dvdemux->decoder, data) < 0)
      goto parse_header_error;

    dvdemux->found_header = TRUE;

    /* after parsing the header we know the length of the data */
    dvdemux->PAL = dv_system_50_fields (dvdemux->decoder);
    length = dvdemux->frame_len = (dvdemux->PAL ? PAL_BUFFER : NTSC_BUFFER);
    dvdemux->framerate = dvdemux->PAL ? PAL_FRAMERATE : NTSC_FRAMERATE;
    /* let demux_video set the height, it needs to detect when things change so
     * it can reset caps */

    /* if we still have enough for a frame, start decoding */
    if (gst_adapter_available (dvdemux->adapter) >= length) {

      data = gst_adapter_take (dvdemux->adapter, length);

      /* and decode the data */
      ret = gst_dvdemux_demux_frame (dvdemux, data);

      if (ret != GST_FLOW_OK)
        goto done;
    }
  }
done:
  return ret;

  /* ERRORS */
parse_header_error:
  {
    GST_ELEMENT_ERROR (dvdemux, STREAM, DECODE,
        ("Error parsing DV header"), ("Error parsing DV header"));
    gst_object_unref (dvdemux);

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

  dvdemux = GST_DVDEMUX (gst_pad_get_parent (pad));

  /* temporary hack? Can't do this from the state change */
  if (!dvdemux->videosrcpad)
    gst_dvdemux_add_pads (dvdemux);

  gst_adapter_push (dvdemux->adapter, buffer);

  /* Apparently dv_parse_header can read from the body of the frame
   * too, so it needs more than header_size bytes. Wacky!
   */
  if (dvdemux->frame_len == -1) {
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
      dvdemux->audio_offset = 0;
      dvdemux->video_offset = 0;
      dvdemux->framecount = 0;
      dvdemux->found_header = FALSE;
      dvdemux->frame_len = -1;
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
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}
