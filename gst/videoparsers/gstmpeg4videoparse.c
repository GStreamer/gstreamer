/* GStreamer
 * Copyright (C) <2008> Mindfruit B.V.
 *   @author Sjoerd Simons <sjoerd@luon.net>
 * Copyright (C) <2007> Julien Moutte <julien@fluendo.com>
 * Copyright (C) <2011> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2011> Nokia Corporation
 * Copyright (C) <2011> Intel
 * Copyright (C) <2011> Collabora Ltd.
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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
#include <gst/base/gstbytereader.h>
#include <gst/pbutils/codec-utils.h>

#include "gstmpeg4videoparse.h"

GST_DEBUG_CATEGORY (mpeg4v_parse_debug);
#define GST_CAT_DEFAULT mpeg4v_parse_debug

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 4, "
        "parsed = (boolean) true, " "systemstream = (boolean) false")
    );

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 4, " "systemstream = (boolean) false")
    );

/* Properties */
#define DEFAULT_PROP_DROP TRUE
#define DEFAULT_CONFIG_INTERVAL (0)

enum
{
  PROP_0,
  PROP_DROP,
  PROP_CONFIG_INTERVAL,
  PROP_LAST
};

GST_BOILERPLATE (GstMpeg4VParse, gst_mpeg4vparse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

static gboolean gst_mpeg4vparse_start (GstBaseParse * parse);
static gboolean gst_mpeg4vparse_stop (GstBaseParse * parse);
static gboolean gst_mpeg4vparse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize);
static GstFlowReturn gst_mpeg4vparse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static GstFlowReturn gst_mpeg4vparse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static gboolean gst_mpeg4vparse_set_caps (GstBaseParse * parse, GstCaps * caps);
static GstCaps *gst_mpeg4vparse_get_caps (GstBaseParse * parse);

static void gst_mpeg4vparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeg4vparse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_mpeg4vparse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_details_simple (element_class,
      "MPEG 4 video elementary stream parser", "Codec/Parser/Video",
      "Parses MPEG-4 Part 2 elementary video streams",
      "Julien Moutte <julien@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (mpeg4v_parse_debug, "mpeg4videoparse", 0,
      "MPEG-4 video parser");
}

static void
gst_mpeg4vparse_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEO_PARSE (object);

  switch (property_id) {
    case PROP_DROP:
      parse->drop = g_value_get_boolean (value);
      break;
    case PROP_CONFIG_INTERVAL:
      parse->interval = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpeg4vparse_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpeg4VParse *parse = GST_MPEG4VIDEO_PARSE (object);

  switch (property_id) {
    case PROP_DROP:
      g_value_set_boolean (value, parse->drop);
      break;
    case PROP_CONFIG_INTERVAL:
      g_value_set_uint (value, parse->interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpeg4vparse_class_init (GstMpeg4VParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_mpeg4vparse_set_property;
  gobject_class->get_property = gst_mpeg4vparse_get_property;

  g_object_class_install_property (gobject_class, PROP_DROP,
      g_param_spec_boolean ("drop", "drop",
          "Drop data untill valid configuration data is received either "
          "in the stream or through caps", DEFAULT_PROP_DROP,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONFIG_INTERVAL,
      g_param_spec_uint ("config-interval",
          "Configuration Send Interval",
          "Send Configuration Insertion Interval in seconds (configuration headers "
          "will be multiplexed in the data stream when detected.) (0 = disabled)",
          0, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Override BaseParse vfuncs */
  parse_class->start = GST_DEBUG_FUNCPTR (gst_mpeg4vparse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_mpeg4vparse_stop);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_check_valid_frame);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_mpeg4vparse_parse_frame);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_mpeg4vparse_pre_push_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_mpeg4vparse_set_caps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_mpeg4vparse_get_caps);
}

static void
gst_mpeg4vparse_init (GstMpeg4VParse * parse, GstMpeg4VParseClass * g_class)
{
  parse->interval = DEFAULT_CONFIG_INTERVAL;
  parse->last_report = GST_CLOCK_TIME_NONE;
}

static void
gst_mpeg4vparse_reset_frame (GstMpeg4VParse * mp4vparse)
{
  /* done parsing; reset state */
  mp4vparse->last_sc = -1;
  mp4vparse->vop_offset = -1;
  mp4vparse->vo_found = FALSE;
  mp4vparse->vol_offset = -1;
}

static void
gst_mpeg4vparse_reset (GstMpeg4VParse * mp4vparse)
{
  gst_mpeg4vparse_reset_frame (mp4vparse);
  mp4vparse->update_caps = TRUE;
  mp4vparse->profile = NULL;
  mp4vparse->level = NULL;

  gst_buffer_replace (&mp4vparse->config, NULL);
  memset (&mp4vparse->vol, 0, sizeof (mp4vparse->vol));
}

static gboolean
gst_mpeg4vparse_start (GstBaseParse * parse)
{
  GstMpeg4VParse *mp4vparse = GST_MPEG4VIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");

  gst_mpeg4vparse_reset (mp4vparse);
  /* at least this much for a valid frame */
  gst_base_parse_set_min_frame_size (parse, 6);

  return TRUE;
}

static gboolean
gst_mpeg4vparse_stop (GstBaseParse * parse)
{
  GstMpeg4VParse *mp4vparse = GST_MPEG4VIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");

  gst_mpeg4vparse_reset (mp4vparse);

  return TRUE;
}

static gboolean
gst_mpeg4vparse_process_config (GstMpeg4VParse * mp4vparse,
    const guint8 * data, guint offset, gsize size)
{
  /* only do stuff if something new */
  if (mp4vparse->config && size == GST_BUFFER_SIZE (mp4vparse->config) &&
      memcmp (GST_BUFFER_DATA (mp4vparse->config), data, size) == 0)
    return TRUE;

  if (mp4vparse->vol_offset < 0) {
    GST_WARNING ("No video object Layer parsed in this frame, cannot accept "
        "config");
    return FALSE;
  }

  /* If the parsing fail, we accept the config only if we don't have
   * any config yet. */
  if (gst_mpeg4_parse_video_object_layer (&mp4vparse->vol,
          NULL, data + mp4vparse->vol_offset,
          size - mp4vparse->vol_offset) != GST_MPEG4_PARSER_OK &&
      mp4vparse->config)
    return FALSE;

  GST_LOG_OBJECT (mp4vparse, "Width/Height: %u/%u, "
      "time increment resolution: %u fixed time increment: %u",
      mp4vparse->vol.width, mp4vparse->vol.height,
      mp4vparse->vol.vop_time_increment_resolution,
      mp4vparse->vol.fixed_vop_time_increment);


  GST_LOG_OBJECT (mp4vparse, "accepting parsed config size %" G_GSSIZE_FORMAT,
      size);

  if (mp4vparse->config != NULL)
    gst_buffer_unref (mp4vparse->config);

  mp4vparse->config = gst_buffer_new_and_alloc (size);

  memcpy (GST_BUFFER_DATA (mp4vparse->config), data, size);


  /* trigger src caps update */
  mp4vparse->update_caps = TRUE;

  return TRUE;
}

/* caller guarantees at least start code in @buf at @off */
static gboolean
gst_mpeg4vparse_process_sc (GstMpeg4VParse * mp4vparse, GstMpeg4Packet * packet,
    gsize size)
{

  GST_LOG_OBJECT (mp4vparse, "process startcode %x", packet->type);

  /* if we found a VOP, next start code ends it,
   * except for final VOS end sequence code included in last VOP-frame */
  if (mp4vparse->vop_offset >= 0 &&
      packet->type != GST_MPEG4_VISUAL_OBJ_SEQ_END) {
    if (G_LIKELY (size > mp4vparse->vop_offset + 1)) {
      mp4vparse->intra_frame =
          ((packet->data[mp4vparse->vop_offset + 1] >> 6 & 0x3) == 0);
    } else {
      GST_WARNING_OBJECT (mp4vparse, "no data following VOP startcode");
      mp4vparse->intra_frame = FALSE;
    }
    GST_LOG_OBJECT (mp4vparse, "ending frame of size %d, is intra %d",
        packet->offset - 3, mp4vparse->intra_frame);
    return TRUE;
  }

  switch (packet->type) {
    case GST_MPEG4_VIDEO_OBJ_PLANE:
    case GST_MPEG4_GROUP_OF_VOP:
    {

      if (packet->type == GST_MPEG4_VIDEO_OBJ_PLANE) {
        GST_LOG_OBJECT (mp4vparse, "startcode is VOP");
        mp4vparse->vop_offset = packet->offset;
      } else {
        GST_LOG_OBJECT (mp4vparse, "startcode is GOP");
      }
      /* parse config data ending here if proper startcodes found earlier;
       * preferably start at VOS (visual object sequence),
       * otherwise at VO (video object) */
      if (mp4vparse->vo_found) {

        /*Do not take care startcode into account */
        gst_mpeg4vparse_process_config (mp4vparse,
            packet->data, packet->offset, packet->offset - 3);

        /* avoid accepting again for a VOP sc following a GOP sc */
        mp4vparse->vo_found = FALSE;
      }
      break;
    }
    case GST_MPEG4_VISUAL_OBJ_SEQ_START:
      GST_LOG_OBJECT (mp4vparse, "Visual Sequence Start");
      mp4vparse->vo_found = TRUE;
      mp4vparse->profile = gst_codec_utils_mpeg4video_get_profile (packet->data
          + packet->offset + 1, packet->offset);
      mp4vparse->level = gst_codec_utils_mpeg4video_get_level (packet->data
          + packet->offset + 1, packet->offset);
      break;
    case GST_MPEG4_VISUAL_OBJ:
      GST_LOG_OBJECT (mp4vparse, "Visual Object");
    default:
      if (packet->type >= GST_MPEG4_VIDEO_LAYER_FIRST &&
          packet->type <= GST_MPEG4_VIDEO_LAYER_LAST) {

        GST_LOG_OBJECT (mp4vparse, "Video Object Layer");

        /*  wee keep track of the offset to parse later on */
        if (mp4vparse->vol_offset < 0)
          mp4vparse->vol_offset = packet->offset;

        /* VO (video object) cases */
      } else if (packet->type <= GST_MPEG4_VIDEO_OBJ_LAST) {
        GST_LOG_OBJECT (mp4vparse, "Video object");
        mp4vparse->vo_found = TRUE;
      }
      break;
  }

  /* at least need to have a VOP in a frame */
  return FALSE;
}

/* FIXME move into baseparse, or anything equivalent;
 * see https://bugzilla.gnome.org/show_bug.cgi?id=650093 */
#define GST_BASE_PARSE_FRAME_FLAG_PARSING   0x10000

static gboolean
gst_mpeg4vparse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize)
{
  GstMpeg4VParse *mp4vparse = GST_MPEG4VIDEO_PARSE (parse);
  GstMpeg4Packet packet;
  guint8 *data = GST_BUFFER_DATA (frame->buffer);
  guint size = GST_BUFFER_SIZE (frame->buffer);
  gint off = 0;
  gboolean ret;

retry:
  /* at least start code and subsequent byte */
  if (G_UNLIKELY (size - off < 5))
    return FALSE;

  /* avoid stale cached parsing state */
  if (!(frame->flags & GST_BASE_PARSE_FRAME_FLAG_PARSING)) {
    GST_LOG_OBJECT (mp4vparse, "parsing new frame");
    gst_mpeg4vparse_reset_frame (mp4vparse);
    frame->flags |= GST_BASE_PARSE_FRAME_FLAG_PARSING;
  } else {
    GST_LOG_OBJECT (mp4vparse, "resuming frame parsing");
  }

  /* if already found a previous start code, e.g. start of frame, go for next */
  if (mp4vparse->last_sc >= 0) {
    off = mp4vparse->last_sc;
    goto next;
  }

  /* didn't find anything that looks like a sync word, skip */
  switch (gst_mpeg4_parse (&packet, TRUE, NULL, data, off, size)) {
    case (GST_MPEG4_PARSER_NO_PACKET):
    case (GST_MPEG4_PARSER_ERROR):
      *skipsize = size - 3;
      return FALSE;
    default:
      break;
  }
  off = packet.offset;

  /* possible frame header, but not at offset 0? skip bytes before sync */
  if (G_UNLIKELY (off > 3)) {
    *skipsize = off - 3;
    return FALSE;
  }

  switch (packet.type) {
    case GST_MPEG4_GROUP_OF_VOP:
    case GST_MPEG4_VISUAL_OBJ_SEQ_START:
    case GST_MPEG4_VIDEO_OBJ_PLANE:
      break;
    default:
      if (packet.type <= GST_MPEG4_VIDEO_OBJ_LAST)
        break;
      /* undesirable sc */
      GST_LOG_OBJECT (mp4vparse, "start code is no VOS, VO, VOP or GOP");
      goto retry;
  }

  /* found sc */
  mp4vparse->last_sc = 0;

  /* examine start code, which should not end frame at present */
  gst_mpeg4vparse_process_sc (mp4vparse, &packet, size);

next:
  GST_LOG_OBJECT (mp4vparse, "Looking for frame end");

  /* start is fine as of now */
  *skipsize = 0;
  /* position a bit further than last sc */
  off++;

  /* so now we have start code at start of data; locate next packet */
  switch (gst_mpeg4_parse (&packet, TRUE, NULL, data, off, size)) {
    case (GST_MPEG4_PARSER_NO_PACKET_END):
      ret = gst_mpeg4vparse_process_sc (mp4vparse, &packet, size);
      if (ret)
        break;
    case (GST_MPEG4_PARSER_NO_PACKET):
    case (GST_MPEG4_PARSER_ERROR):
      /* if draining, take all */
      if (GST_BASE_PARSE_DRAINING (parse)) {
        *framesize = size;
        return TRUE;
      } else {
        /* resume scan where we left it */
        mp4vparse->last_sc = size - 3;
        /* request best next available */
        *framesize = G_MAXUINT;
        return FALSE;
      }
    default:
      /* decide whether this startcode ends a frame */
      ret = gst_mpeg4vparse_process_sc (mp4vparse, &packet, size);
      break;
  }

  off = packet.offset;

  if (ret) {
    *framesize = off - 3;
  } else {
    goto next;
  }

  return ret;
}

static void
gst_mpeg4vparse_update_src_caps (GstMpeg4VParse * mp4vparse)
{
  GstCaps *caps = NULL;

  GST_LOG_OBJECT (mp4vparse, "Updating caps");

  /* only update if no src caps yet or explicitly triggered */
  if (G_LIKELY (GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (mp4vparse)) &&
          !mp4vparse->update_caps))
    return;

  /* carry over input caps as much as possible; override with our own stuff */
  caps = GST_PAD_CAPS (GST_BASE_PARSE_SINK_PAD (mp4vparse));
  if (caps) {
    caps = gst_caps_copy (caps);
  } else {
    caps = gst_caps_new_simple ("video/mpeg",
        "mpegversion", G_TYPE_INT, 4, NULL);
  }

  gst_caps_set_simple (caps, "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (mp4vparse->profile && mp4vparse->level) {
    gst_caps_set_simple (caps, "profile", G_TYPE_STRING, mp4vparse->profile,
        "level", G_TYPE_STRING, mp4vparse->level, NULL);
  }

  if (mp4vparse->config != NULL) {
    gst_caps_set_simple (caps, "codec_data",
        GST_TYPE_BUFFER, mp4vparse->config, NULL);
  }

  if (mp4vparse->vol.width > 0 && mp4vparse->vol.height > 0) {
    gst_caps_set_simple (caps, "width", G_TYPE_INT, mp4vparse->vol.width,
        "height", G_TYPE_INT, mp4vparse->vol.height, NULL);
  }

  /* perhaps we have  a framerate */
  if (mp4vparse->vol.fixed_vop_time_increment != 0) {
    gint fps_num = mp4vparse->vol.vop_time_increment_resolution;
    gint fps_den = mp4vparse->vol.fixed_vop_time_increment;
    GstClockTime latency = gst_util_uint64_scale (GST_SECOND, fps_den, fps_num);

    gst_caps_set_simple (caps, "framerate",
        GST_TYPE_FRACTION, fps_num, fps_den, NULL);
    gst_base_parse_set_frame_rate (GST_BASE_PARSE (mp4vparse),
        fps_num, fps_den, 0, 0);
    gst_base_parse_set_latency (GST_BASE_PARSE (mp4vparse), latency, latency);
  }

  /* or pixel-aspect-ratio */
  if (mp4vparse->vol.par_width > 0 && mp4vparse->vol.par_height > 0) {
    gst_caps_set_simple (caps, "pixel-aspect-ratio",
        GST_TYPE_FRACTION, mp4vparse->vol.par_width,
        mp4vparse->vol.par_height, NULL);
  }

  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (mp4vparse), caps);
  gst_caps_unref (caps);
}

static GstFlowReturn
gst_mpeg4vparse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstMpeg4VParse *mp4vparse = GST_MPEG4VIDEO_PARSE (parse);
  GstBuffer *buffer = frame->buffer;

  gst_mpeg4vparse_update_src_caps (mp4vparse);

  if (mp4vparse->intra_frame)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  if (G_UNLIKELY (mp4vparse->drop && !mp4vparse->config)) {
    GST_LOG_OBJECT (mp4vparse, "dropping frame as no config yet");
    return GST_BASE_PARSE_FLOW_DROPPED;
  } else
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_mpeg4vparse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstMpeg4VParse *mp4vparse = GST_MPEG4VIDEO_PARSE (parse);
  GstBuffer *buffer = frame->buffer;

  /* periodic config sending */
  if (mp4vparse->interval > 0) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);
    guint64 diff;

    /* init */
    if (!GST_CLOCK_TIME_IS_VALID (mp4vparse->last_report)) {
      mp4vparse->last_report = timestamp;
    }

    if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
      if (timestamp > mp4vparse->last_report)
        diff = timestamp - mp4vparse->last_report;
      else
        diff = 0;

      GST_LOG_OBJECT (mp4vparse,
          "now %" GST_TIME_FORMAT ", last config %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp), GST_TIME_ARGS (mp4vparse->last_report));

      GST_LOG_OBJECT (mp4vparse,
          "interval since last config %" GST_TIME_FORMAT, GST_TIME_ARGS (diff));

      if (GST_TIME_AS_SECONDS (diff) >= mp4vparse->interval) {
        /* we need to send config now first */
        GST_LOG_OBJECT (parse, "inserting config in stream");

        /* avoid inserting duplicate config */
        if ((GST_BUFFER_SIZE (buffer) < GST_BUFFER_SIZE (mp4vparse->config)) ||
            memcmp (GST_BUFFER_DATA (buffer),
                GST_BUFFER_DATA (mp4vparse->config),
                GST_BUFFER_SIZE (mp4vparse->config))) {
          GstBuffer *superbuf;

          /* insert header */
          superbuf = gst_buffer_merge (mp4vparse->config, buffer);
          gst_buffer_copy_metadata (superbuf, buffer, GST_BUFFER_COPY_ALL);
          gst_buffer_replace (&frame->buffer, superbuf);
          gst_buffer_unref (superbuf);
        } else {
          GST_LOG_OBJECT (parse, "... but avoiding duplication");
        }

        if (G_UNLIKELY (timestamp != -1)) {
          mp4vparse->last_report = timestamp;
        }
      }
    }
  }

  return GST_FLOW_OK;
}

static gboolean
gst_mpeg4vparse_set_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstMpeg4VParse *mp4vparse = GST_MPEG4VIDEO_PARSE (parse);
  GstStructure *s;
  const GValue *value;
  GstBuffer *buf;
  guint8 *data;
  gsize size;

  GstMpeg4Packet packet;
  GstMpeg4ParseResult res;

  GST_DEBUG_OBJECT (parse, "setcaps called with %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  if ((value = gst_structure_get_value (s, "codec_data")) != NULL
      && (buf = gst_value_get_buffer (value))) {
    /* best possible parse attempt,
     * src caps are based on sink caps so it will end up in there
     * whether sucessful or not */
    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);
    res = gst_mpeg4_parse (&packet, TRUE, NULL, data, 0, size);

    while (res == GST_MPEG4_PARSER_OK || res == GST_MPEG4_PARSER_NO_PACKET_END) {

      if (packet.type >= GST_MPEG4_VIDEO_LAYER_FIRST &&
          packet.type <= GST_MPEG4_VIDEO_LAYER_LAST)
        mp4vparse->vol_offset = packet.offset;

      res = gst_mpeg4_parse (&packet, TRUE, NULL, data, packet.offset, size);
    }

    /* And take it as config */
    gst_mpeg4vparse_process_config (mp4vparse, GST_BUFFER_DATA (buf),
        3, GST_BUFFER_SIZE (buf));
  }

  /* let's not interfere and accept regardless of config parsing success */
  return TRUE;
}


static GstCaps *
gst_mpeg4vparse_get_caps (GstBaseParse * parse)
{
  GstCaps *peercaps;
  GstCaps *res;

  peercaps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (parse));
  if (peercaps) {
    guint i, n;

    /* Remove the parsed field */
    peercaps = gst_caps_make_writable (peercaps);
    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);

      gst_structure_remove_field (s, "parsed");
    }

    res =
        gst_caps_intersect_full (peercaps,
        gst_pad_get_pad_template_caps (GST_BASE_PARSE_SRC_PAD (parse)),
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
  } else {
    res =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD
            (parse)));
  }

  return res;
}
