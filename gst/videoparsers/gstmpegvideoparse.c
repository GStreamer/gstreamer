/* GStreamer
 * Copyright (C) <2007> Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) <2011> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2011> Collabora Multimedia
 * Copyright (C) <2011> Nokia Corporation
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

#include "gstmpegvideoparse.h"

GST_DEBUG_CATEGORY (mpegv_parse_debug);
#define GST_CAT_DEFAULT mpegv_parse_debug

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [1, 2], "
        "parsed = (boolean) true, " "systemstream = (boolean) false")
    );

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [1, 2], "
        "parsed = (boolean) false, " "systemstream = (boolean) false")
    );

/* Properties */
#define DEFAULT_PROP_DROP       TRUE
#define DEFAULT_PROP_GOP_SPLIT  FALSE

enum
{
  PROP_0,
  PROP_DROP,
  PROP_GOP_SPLIT,
  PROP_LAST
};

GST_BOILERPLATE (GstMpegvParse, gst_mpegv_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);

static gboolean gst_mpegv_parse_start (GstBaseParse * parse);
static gboolean gst_mpegv_parse_stop (GstBaseParse * parse);
static gboolean gst_mpegv_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize);
static GstFlowReturn gst_mpegv_parse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static gboolean gst_mpegv_parse_set_caps (GstBaseParse * parse, GstCaps * caps);

static void gst_mpegv_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpegv_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_mpegv_parse_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class,
      "MPEG video elementary stream parser",
      "Codec/Parser/Video",
      "Parses and frames MPEG-1 and MPEG-2 elementary video streams",
      "Wim Taymans <wim.taymans@ccollabora.co.uk>, "
      "Jan Schmidt <thaytan@mad.scientist.com>, "
      "Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (mpegv_parse_debug, "mpegvideoparse", 0,
      "MPEG-1/2 video parser");
}

static void
gst_mpegv_parse_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpegvParse *parse = GST_MPEGVIDEO_PARSE (object);

  switch (property_id) {
    case PROP_DROP:
      parse->drop = g_value_get_boolean (value);
      break;
    case PROP_GOP_SPLIT:
      parse->gop_split = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpegv_parse_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpegvParse *parse = GST_MPEGVIDEO_PARSE (object);

  switch (property_id) {
    case PROP_DROP:
      g_value_set_boolean (value, parse->drop);
      break;
    case PROP_GOP_SPLIT:
      g_value_set_boolean (value, parse->gop_split);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_mpegv_parse_class_init (GstMpegvParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_mpegv_parse_set_property;
  gobject_class->get_property = gst_mpegv_parse_get_property;

  g_object_class_install_property (gobject_class, PROP_DROP,
      g_param_spec_boolean ("drop", "drop",
          "Drop data untill valid configuration data is received either "
          "in the stream or through caps", DEFAULT_PROP_DROP,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GOP_SPLIT,
      g_param_spec_boolean ("gop-split", "gop-split",
          "Split frame when encountering GOP", DEFAULT_PROP_GOP_SPLIT,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Override BaseParse vfuncs */
  parse_class->start = GST_DEBUG_FUNCPTR (gst_mpegv_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_mpegv_parse_stop);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_mpegv_parse_check_valid_frame);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_mpegv_parse_parse_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_mpegv_parse_set_caps);
}

static void
gst_mpegv_parse_init (GstMpegvParse * parse, GstMpegvParseClass * g_class)
{
}

static void
gst_mpegv_parse_reset_frame (GstMpegvParse * mpvparse)
{
  /* done parsing; reset state */
  mpvparse->last_sc = -1;
  mpvparse->seq_offset = -1;
  mpvparse->pic_offset = -1;
}

static void
gst_mpegv_parse_reset (GstMpegvParse * mpvparse)
{
  gst_mpegv_parse_reset_frame (mpvparse);
  mpvparse->profile = 0;
  mpvparse->update_caps = TRUE;

  gst_buffer_replace (&mpvparse->config, NULL);
  memset (&mpvparse->params, 0, sizeof (mpvparse->params));
}

static gboolean
gst_mpegv_parse_start (GstBaseParse * parse)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");

  gst_mpegv_parse_reset (mpvparse);
  /* at least this much for a valid frame */
  gst_base_parse_set_min_frame_size (parse, 6);

  return TRUE;
}

static gboolean
gst_mpegv_parse_stop (GstBaseParse * parse)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");

  gst_mpegv_parse_reset (mpvparse);

  return TRUE;
}

static gboolean
gst_mpegv_parse_process_config (GstMpegvParse * mpvparse, const guint8 * data,
    gsize size)
{
  /* only do stuff if something new */
  if (mpvparse->config && size == GST_BUFFER_SIZE (mpvparse->config) &&
      memcmp (GST_BUFFER_DATA (mpvparse->config), data, size) == 0)
    return TRUE;

  if (!gst_mpeg_video_params_parse_config (&mpvparse->params, data, size)) {
    GST_DEBUG_OBJECT (mpvparse, "failed to parse config data (size %"
        G_GSSIZE_FORMAT ")", size);
    return FALSE;
  }

  GST_LOG_OBJECT (mpvparse, "accepting parsed config size %" G_GSSIZE_FORMAT,
      size);

  /* parsing ok, so accept it as new config */
  if (mpvparse->config != NULL)
    gst_buffer_unref (mpvparse->config);

  mpvparse->config = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (mpvparse->config), data, size);

  /* trigger src caps update */
  mpvparse->update_caps = TRUE;

  return TRUE;
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *
picture_start_code_name (guint8 psc)
{
  guint i;
  const struct
  {
    guint8 psc;
    const gchar *name;
  } psc_names[] = {
    {
    0x00, "Picture Start"}, {
    0xb0, "Reserved"}, {
    0xb1, "Reserved"}, {
    0xb2, "User Data Start"}, {
    0xb3, "Sequence Header Start"}, {
    0xb4, "Sequence Error"}, {
    0xb5, "Extension Start"}, {
    0xb6, "Reserved"}, {
    0xb7, "Sequence End"}, {
    0xb8, "Group Start"}, {
    0xb9, "Program End"}
  };
  if (psc < 0xB0 && psc > 0)
    return "Slice Start";

  for (i = 0; i < G_N_ELEMENTS (psc_names); i++)
    if (psc_names[i].psc == psc)
      return psc_names[i].name;

  return "UNKNOWN";
};

static const gchar *
picture_type_name (guint8 pct)
{
  guint i;
  const struct
  {
    guint8 pct;
    const gchar *name;
  } pct_names[] = {
    {
    0, "Forbidden"}, {
    1, "I Frame"}, {
    2, "P Frame"}, {
    3, "B Frame"}, {
    4, "DC Intra Coded (Shall Not Be Used!)"}
  };

  for (i = 0; i < G_N_ELEMENTS (pct_names); i++)
    if (pct_names[i].pct == pct)
      return pct_names[i].name;

  return "Reserved/Unknown";
}
#endif /* GST_DISABLE_GST_DEBUG */

/* caller guarantees at least start code in @buf at @off */
/* for off == 0 initial code; returns TRUE if code starts a frame,
 * otherwise returns TRUE if code terminates preceding frame */
static gboolean
gst_mpegv_parse_process_sc (GstMpegvParse * mpvparse, GstBuffer * buf, gint off)
{
  gboolean ret = FALSE, do_seq = TRUE;
  guint8 *data;
  guint code;

  g_return_val_if_fail (buf && GST_BUFFER_SIZE (buf) >= 4, FALSE);

  data = GST_BUFFER_DATA (buf);
  code = data[off + 3];

  GST_LOG_OBJECT (mpvparse, "process startcode %x (%s)", code,
      picture_start_code_name (code));

  switch (code) {
    case MPEG_PACKET_PICTURE:
      GST_LOG_OBJECT (mpvparse, "startcode is PICTURE");
      /* picture is aggregated with preceding sequence/gop, if any.
       * so, picture start code only ends if already a previous one */
      if (mpvparse->pic_offset < 0)
        mpvparse->pic_offset = off;
      else
        ret = TRUE;
      if (!off)
        ret = TRUE;
      break;
    case MPEG_PACKET_SEQUENCE:
      GST_LOG_OBJECT (mpvparse, "startcode is SEQUENCE");
      if (off == 0)
        mpvparse->seq_offset = off;
      ret = TRUE;
      break;
    case MPEG_PACKET_GOP:
      GST_LOG_OBJECT (mpvparse, "startcode is GOP");
      if (mpvparse->seq_offset >= 0)
        ret = mpvparse->gop_split;
      else
        ret = TRUE;
      break;
    default:
      do_seq = FALSE;
      break;
  }

  /* process config data */
  if (G_UNLIKELY (mpvparse->seq_offset >= 0 && off && do_seq)) {
    g_assert (mpvparse->seq_offset == 0);
    gst_mpegv_parse_process_config (mpvparse, GST_BUFFER_DATA (buf), off);
    /* avoid accepting again for a PICTURE sc following a GOP sc */
    mpvparse->seq_offset = -1;
  }

  /* extract some picture info if there is any in the frame being terminated */
  if (G_UNLIKELY (ret && off)) {
    if (G_LIKELY (mpvparse->pic_offset >= 0 && mpvparse->pic_offset < off)) {
      if (G_LIKELY (GST_BUFFER_SIZE (buf) >= mpvparse->pic_offset + 6)) {
        gint pct = (data[mpvparse->pic_offset + 5] >> 3) & 0x7;

        GST_LOG_OBJECT (mpvparse, "picture_coding_type %d (%s)", pct,
            picture_type_name (pct));
        mpvparse->intra_frame = (pct == MPEG_PICTURE_TYPE_I);
      } else {
        GST_WARNING_OBJECT (mpvparse, "no data following PICTURE startcode");
        mpvparse->intra_frame = FALSE;
      }
    } else {
      /* frame without picture must be some config, consider as keyframe */
      mpvparse->intra_frame = TRUE;
    }
    GST_LOG_OBJECT (mpvparse, "ending frame of size %d, is intra %d", off,
        mpvparse->intra_frame);
  }

  return ret;
}

static inline guint
scan_for_start_codes (const GstByteReader * reader, guint offset, guint size)
{
  const guint8 *data;
  guint32 state;
  guint i;

  g_return_val_if_fail (size > 0, -1);
  g_return_val_if_fail ((guint64) offset + size <= reader->size - reader->byte,
      -1);

  /* we can't find the pattern with less than 4 bytes */
  if (G_UNLIKELY (size < 4))
    return -1;

  data = reader->data + reader->byte + offset;

  /* set the state to something that does not match */
  state = 0xffffffff;

  /* now find data */
  for (i = 0; i < size; i++) {
    /* throw away one byte and move in the next byte */
    state = ((state << 8) | data[i]);
    if (G_UNLIKELY ((state & 0xffffff00) == 0x00000100)) {
      /* we have a match but we need to have skipped at
       * least 4 bytes to fill the state. */
      if (G_LIKELY (i >= 3))
        return offset + i - 3;
    }

    /* Accelerate search for start code */
    if (data[i] > 1) {
      while (i < (size - 4) && data[i] > 1) {
        if (data[i + 3] > 1)
          i += 4;
        else
          i += 1;
      }
      state = 0x00000100;
    }
  }

  /* nothing found */
  return -1;
}

/* FIXME move into baseparse, or anything equivalent;
 * see https://bugzilla.gnome.org/show_bug.cgi?id=650093 */
#define GST_BASE_PARSE_FRAME_FLAG_PARSING   0x10000

static gboolean
gst_mpegv_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);
  GstBuffer *buf = frame->buffer;
  GstByteReader reader = GST_BYTE_READER_INIT_FROM_BUFFER (buf);
  gint off = 0;
  gboolean ret;

retry:
  /* at least start code and subsequent byte */
  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) - off < 5))
    return FALSE;

  /* avoid stale cached parsing state */
  if (!(frame->flags & GST_BASE_PARSE_FRAME_FLAG_PARSING)) {
    GST_LOG_OBJECT (mpvparse, "parsing new frame");
    gst_mpegv_parse_reset_frame (mpvparse);
    frame->flags |= GST_BASE_PARSE_FRAME_FLAG_PARSING;
  } else {
    GST_LOG_OBJECT (mpvparse, "resuming frame parsing");
  }

  /* if already found a previous start code, e.g. start of frame, go for next */
  if (mpvparse->last_sc >= 0) {
    off = mpvparse->last_sc;
    goto next;
  }

  off = scan_for_start_codes (&reader, off, GST_BUFFER_SIZE (buf) - off);

  GST_LOG_OBJECT (mpvparse, "possible sync at buffer offset %d", off);

  /* didn't find anything that looks like a sync word, skip */
  if (G_UNLIKELY (off < 0)) {
    *skipsize = GST_BUFFER_SIZE (buf) - 3;
    return FALSE;
  }

  /* possible frame header, but not at offset 0? skip bytes before sync */
  if (G_UNLIKELY (off > 0)) {
    *skipsize = off;
    return FALSE;
  }

  /* note: initial start code is assumed at offset 0 by subsequent code */

  /* examine start code, see if it looks like an initial start code */
  if (gst_mpegv_parse_process_sc (mpvparse, buf, 0)) {
    /* found sc */
    mpvparse->last_sc = 0;
  } else {
    off++;
    goto retry;
  }

next:
  /* start is fine as of now */
  *skipsize = 0;
  /* position a bit further than last sc */
  off++;
  /* so now we have start code at start of data; locate next start code */
  off = scan_for_start_codes (&reader, off, GST_BUFFER_SIZE (buf) - off);

  GST_LOG_OBJECT (mpvparse, "next start code at %d", off);
  if (off < 0) {
    /* if draining, take all */
    if (GST_BASE_PARSE_DRAINING (parse)) {
      off = GST_BUFFER_SIZE (buf);
      ret = TRUE;
    } else {
      /* resume scan where we left it */
      mpvparse->last_sc = GST_BUFFER_SIZE (buf) - 4;
      /* request best next available */
      *framesize = G_MAXUINT;
      return FALSE;
    }
  } else {
    /* decide whether this startcode ends a frame */
    ret = gst_mpegv_parse_process_sc (mpvparse, buf, off);
  }

  if (ret) {
    *framesize = off;
  } else {
    goto next;
  }

  return ret;
}

static void
gst_mpegv_parse_update_src_caps (GstMpegvParse * mpvparse)
{
  GstCaps *caps = NULL;

  /* only update if no src caps yet or explicitly triggered */
  if (G_LIKELY (GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (mpvparse)) &&
          !mpvparse->update_caps))
    return;

  /* carry over input caps as much as possible; override with our own stuff */
  caps = GST_PAD_CAPS (GST_BASE_PARSE_SINK_PAD (mpvparse));
  if (caps) {
    caps = gst_caps_copy (caps);
  } else {
    caps = gst_caps_new_simple ("video/mpeg", NULL);
  }

  /* typically we don't output buffers until we have properly parsed some
   * config data, so we should at least know about version.
   * If not, it means it has been requested not to drop data, and
   * upstream and/or app must know what they are doing ... */
  if (G_LIKELY (mpvparse->params.mpeg_version))
    gst_caps_set_simple (caps,
        "mpegversion", G_TYPE_INT, mpvparse->params.mpeg_version, NULL);

  gst_caps_set_simple (caps, "systemstream", G_TYPE_BOOLEAN, FALSE,
      "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (mpvparse->params.width > 0 && mpvparse->params.height > 0) {
    gst_caps_set_simple (caps, "width", G_TYPE_INT, mpvparse->params.width,
        "height", G_TYPE_INT, mpvparse->params.height, NULL);
  }

  /* perhaps we have  a framerate */
  if (mpvparse->params.fps_n > 0 && mpvparse->params.fps_d > 0) {
    gint fps_num = mpvparse->params.fps_n;
    gint fps_den = mpvparse->params.fps_d;
    GstClockTime latency = gst_util_uint64_scale (GST_SECOND, fps_den, fps_num);

    gst_caps_set_simple (caps, "framerate",
        GST_TYPE_FRACTION, fps_num, fps_den, NULL);
    gst_base_parse_set_frame_rate (GST_BASE_PARSE (mpvparse),
        fps_num, fps_den, 0, 0);
    gst_base_parse_set_latency (GST_BASE_PARSE (mpvparse), latency, latency);
  }

  /* or pixel-aspect-ratio */
  if (mpvparse->params.par_w && mpvparse->params.par_h > 0) {
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        mpvparse->params.par_w, mpvparse->params.par_h, NULL);
  }

  if (mpvparse->config != NULL) {
    gst_caps_set_simple (caps, "codec_data",
        GST_TYPE_BUFFER, mpvparse->config, NULL);
  }

  if (mpvparse->params.mpeg_version == 2) {
    const guint profile_c = mpvparse->params.profile;
    const guint level_c = mpvparse->params.level;
    const gchar *profile = NULL, *level = NULL;
    /*
     * Profile indication - 1 => High, 2 => Spatially Scalable,
     *                      3 => SNR Scalable, 4 => Main, 5 => Simple
     * 4:2:2 and Multi-view have profile = 0, with the escape bit set to 1
     */
    const gchar *profiles[] = { "high", "spatial", "snr", "main", "simple" };
    /*
     * Level indication - 4 => High, 6 => High-1440, 8 => Main, 10 => Low,
     *                    except in the case of profile = 0
     */
    const gchar *levels[] = { "high", "high-1440", "main", "low" };

    if (profile_c > 0 && profile_c < 6)
      profile = profiles[profile_c - 1];

    if ((level_c > 3) && (level_c < 11) && (level_c % 2 == 0))
      level = levels[(level_c >> 1) - 1];

    if (profile_c == 8) {
      /* Non-hierarchical profile */
      switch (level_c) {
        case 2:
          level = levels[0];
        case 5:
          level = levels[2];
          profile = "4:2:2";
          break;
        case 10:
          level = levels[0];
        case 11:
          level = levels[1];
        case 13:
          level = levels[2];
        case 14:
          level = levels[3];
          profile = "multiview";
          break;
        default:
          break;
      }
    }

    /* FIXME does it make sense to expose profile/level in the caps ? */

    if (profile)
      gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile, NULL);
    else
      GST_DEBUG_OBJECT (mpvparse, "Invalid profile - %u", profile_c);

    if (level)
      gst_caps_set_simple (caps, "level", G_TYPE_STRING, level, NULL);
    else
      GST_DEBUG_OBJECT (mpvparse, "Invalid level - %u", level_c);
  }

  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (mpvparse), caps);
  gst_caps_unref (caps);
}

static GstFlowReturn
gst_mpegv_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);
  GstBuffer *buffer = frame->buffer;

  gst_mpegv_parse_update_src_caps (mpvparse);

  if (G_UNLIKELY (mpvparse->intra_frame))
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  /* maybe only sequence in this buffer, though not recommended,
   * so mark it as such and force 0 duration */
  if (G_UNLIKELY (mpvparse->pic_offset < 0)) {
    GST_DEBUG_OBJECT (mpvparse, "frame holds no picture data");
    frame->flags |= GST_BASE_PARSE_FRAME_FLAG_NO_FRAME;
    GST_BUFFER_DURATION (buffer) = 0;
  }

  if (G_UNLIKELY (mpvparse->drop && !mpvparse->config)) {
    GST_DEBUG_OBJECT (mpvparse, "dropping frame as no config yet");
    return GST_BASE_PARSE_FLOW_DROPPED;
  } else
    return GST_FLOW_OK;
}

static gboolean
gst_mpegv_parse_set_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstMpegvParse *mpvparse = GST_MPEGVIDEO_PARSE (parse);
  GstStructure *s;
  const GValue *value;
  GstBuffer *buf;

  GST_DEBUG_OBJECT (parse, "setcaps called with %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);

  if ((value = gst_structure_get_value (s, "codec_data")) != NULL
      && (buf = gst_value_get_buffer (value))) {
    /* best possible parse attempt,
     * src caps are based on sink caps so it will end up in there
     * whether sucessful or not */
    gst_mpegv_parse_process_config (mpvparse, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
  }

  /* let's not interfere and accept regardless of config parsing success */
  return TRUE;
}
