/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
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
#  include "config.h"
#endif

#include <gst/gst.h>
#include <theora/theora.h>
#include <string.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY (theoraenc_debug);
#define GST_CAT_DEFAULT theoraenc_debug

#define GST_TYPE_THEORA_ENC \
  (gst_theora_enc_get_type())
#define GST_THEORA_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_THEORA_ENC,GstTheoraEnc))
#define GST_THEORA_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_THEORA_ENC,GstTheoraEnc))
#define GST_IS_THEORA_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_THEORA_ENC))
#define GST_IS_THEORA_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_THEORA_ENC))

typedef struct _GstTheoraEnc GstTheoraEnc;
typedef struct _GstTheoraEncClass GstTheoraEncClass;

struct _GstTheoraEnc
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  ogg_stream_state to;

  theora_state state;
  theora_info info;
  theora_comment comment;

  gboolean center;

  gint video_bitrate;           /* bitrate target for Theora video */
  gint video_quality;           /* Theora quality selector 0 = low, 63 = high */
  gboolean quick;
  gboolean keyframe_auto;
  gint keyframe_freq;
  gint keyframe_force;
  gint keyframe_threshold;
  gint keyframe_mindistance;
  gint noise_sensitivity;

  gint info_width, info_height;
  gint width, height;
  gint offset_x, offset_y;
  gdouble fps;

  guint packetno;
  guint64 bytes_out;
  guint64 next_ts;
};

struct _GstTheoraEncClass
{
  GstElementClass parent_class;
};

#define ROUND_UP_2(x) (((x) + 1) & ~1)
#define ROUND_UP_4(x) (((x) + 3) & ~3)
#define ROUND_UP_8(x) (((x) + 7) & ~7)

#define THEORA_DEF_CENTER 		TRUE
#define THEORA_DEF_BITRATE 		0
#define THEORA_DEF_QUALITY 		16
#define THEORA_DEF_QUICK		TRUE
#define THEORA_DEF_KEYFRAME_AUTO	TRUE
#define THEORA_DEF_KEYFRAME_FREQ	64
#define THEORA_DEF_KEYFRAME_FREQ_FORCE	64
#define THEORA_DEF_KEYFRAME_THRESHOLD	80
#define THEORA_DEF_KEYFRAME_MINDISTANCE	8
#define THEORA_DEF_NOISE_SENSITIVITY	1

enum
{
  ARG_0,
  ARG_CENTER,
  ARG_BITRATE,
  ARG_QUALITY,
  ARG_QUICK,
  ARG_KEYFRAME_AUTO,
  ARG_KEYFRAME_FREQ,
  ARG_KEYFRAME_FREQ_FORCE,
  ARG_KEYFRAME_THRESHOLD,
  ARG_KEYFRAME_MINDISTANCE,
  ARG_NOISE_SENSITIVITY,
  /* FILL ME */
};

static GstElementDetails theora_enc_details = {
  "TheoraEnc",
  "Codec/Encoder/Video",
  "encode raw YUV video to a theora stream",
  "Wim Taymans <wim@fluendo.com>",
};

static GstStaticPadTemplate theora_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "framerate = (double) [0, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate theora_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

GST_BOILERPLATE (GstTheoraEnc, gst_theora_enc, GstElement, GST_TYPE_ELEMENT);

static void theora_enc_chain (GstPad * pad, GstData * data);
static GstElementStateReturn theora_enc_change_state (GstElement * element);
static GstPadLinkReturn theora_enc_sink_link (GstPad * pad,
    const GstCaps * caps);
static void theora_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void theora_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void
gst_theora_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_enc_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_enc_sink_factory));
  gst_element_class_set_details (element_class, &theora_enc_details);
}

static void
gst_theora_enc_class_init (GstTheoraEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = theora_enc_set_property;
  gobject_class->get_property = theora_enc_get_property;

  g_object_class_install_property (gobject_class, ARG_CENTER,
      g_param_spec_boolean ("center", "Center",
          "Center image when sizes not multiple of 16", THEORA_DEF_CENTER,
          (GParamFlags) G_PARAM_READWRITE));
  /* general encoding stream options */
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate", "Compressed video bitrate (kbps)",
          0, 2000, THEORA_DEF_BITRATE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QUALITY,
      g_param_spec_int ("quality", "Quality", "Video quality",
          0, 63, THEORA_DEF_QUALITY, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QUICK,
      g_param_spec_boolean ("quick", "Quick", "Quick encoding",
          THEORA_DEF_QUICK, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_AUTO,
      g_param_spec_boolean ("keyframe-auto", "Keyframe Auto",
          "Automatic keyframe detection", THEORA_DEF_KEYFRAME_AUTO,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_FREQ,
      g_param_spec_int ("keyframe-freq", "Keyframe frequency",
          "Keyframe frequency", 1, 32768, THEORA_DEF_KEYFRAME_FREQ,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_FREQ_FORCE,
      g_param_spec_int ("keyframe-force", "Keyframe force",
          "Force keyframe every N frames", 1, 32768,
          THEORA_DEF_KEYFRAME_FREQ_FORCE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_THRESHOLD,
      g_param_spec_int ("keyframe-threshold", "Keyframe threshold",
          "Keyframe threshold", 0, 32768, THEORA_DEF_KEYFRAME_THRESHOLD,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_MINDISTANCE,
      g_param_spec_int ("keyframe-mindistance", "Keyframe mindistance",
          "Keyframe mindistance", 1, 32768, THEORA_DEF_KEYFRAME_MINDISTANCE,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_NOISE_SENSITIVITY,
      g_param_spec_int ("noise-sensitivity", "Noise sensitivity",
          "Noise sensitivity", 0, 32768, THEORA_DEF_NOISE_SENSITIVITY,
          (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->change_state = theora_enc_change_state;
  GST_DEBUG_CATEGORY_INIT (theoraenc_debug, "theoraenc", 0, "Theora encoder");
}

static void
gst_theora_enc_init (GstTheoraEnc * enc)
{
  enc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&theora_enc_sink_factory), "sink");
  gst_pad_set_chain_function (enc->sinkpad, theora_enc_chain);
  gst_pad_set_link_function (enc->sinkpad, theora_enc_sink_link);
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  enc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&theora_enc_src_factory), "src");
  gst_pad_use_explicit_caps (enc->srcpad);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->video_bitrate = THEORA_DEF_BITRATE;
  enc->video_quality = THEORA_DEF_QUALITY;
  enc->quick = THEORA_DEF_QUICK;
  enc->keyframe_auto = THEORA_DEF_KEYFRAME_AUTO;
  enc->keyframe_freq = THEORA_DEF_KEYFRAME_FREQ;
  enc->keyframe_force = THEORA_DEF_KEYFRAME_FREQ_FORCE;
  enc->keyframe_threshold = THEORA_DEF_KEYFRAME_THRESHOLD;
  enc->keyframe_mindistance = THEORA_DEF_KEYFRAME_MINDISTANCE;
  enc->noise_sensitivity = THEORA_DEF_NOISE_SENSITIVITY;

  GST_FLAG_SET (enc, GST_ELEMENT_EVENT_AWARE);
}

static GstPadLinkReturn
theora_enc_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstTheoraEnc *enc = GST_THEORA_ENC (gst_pad_get_parent (pad));
  const GValue *par;
  GValue fps = { 0 };
  GValue framerate = { 0 };

  if (!gst_caps_is_fixed (caps))
    return GST_PAD_LINK_DELAYED;

  gst_structure_get_int (structure, "width", &enc->width);
  gst_structure_get_int (structure, "height", &enc->height);
  gst_structure_get_double (structure, "framerate", &enc->fps);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  theora_info_init (&enc->info);
  /* Theora has a divisible-by-sixteen restriction for the encoded video size but
   * we can define a visible area using the frame_width/frame_height */
  enc->info_width = enc->info.width = (enc->width + 15) & ~15;
  enc->info_height = enc->info.height = (enc->height + 15) & ~15;
  enc->info.frame_width = enc->width;
  enc->info.frame_height = enc->height;

  /* center image if needed */
  if (enc->center) {
    /* make sure offset is even, for easier decoding */
    enc->offset_x = ROUND_UP_2 (enc->info_width - enc->width) / 2;
    enc->offset_y = ROUND_UP_2 (enc->info_height - enc->height) / 2;
  } else {
    enc->offset_x = 0;
    enc->offset_y = 0;
  }
  enc->info.offset_x = enc->offset_x;
  enc->info.offset_y = enc->offset_y;

  /* convert double to fraction for the framerate */
  g_value_init (&fps, G_TYPE_DOUBLE);
  g_value_init (&framerate, GST_TYPE_FRACTION);
  g_value_set_double (&fps, enc->fps);
  g_value_transform (&fps, &framerate);

  enc->info.fps_numerator = gst_value_get_fraction_numerator (&framerate);
  enc->info.fps_denominator = gst_value_get_fraction_denominator (&framerate);

  if (par) {
    enc->info.aspect_numerator = gst_value_get_fraction_numerator (par);
    enc->info.aspect_denominator = gst_value_get_fraction_denominator (par);
  } else {
    /* setting them to 0 indicates that the decoder can chose a good aspect
     * ratio, defaulting to 1/1 */
    enc->info.aspect_numerator = 0;
    enc->info.aspect_denominator = 0;
  }

  enc->info.colorspace = OC_CS_UNSPECIFIED;
  enc->info.target_bitrate = enc->video_bitrate;
  enc->info.quality = enc->video_quality;

  enc->info.dropframes_p = 0;
  enc->info.quick_p = (enc->quick ? 1 : 0);
  enc->info.keyframe_auto_p = (enc->keyframe_auto ? 1 : 0);
  enc->info.keyframe_frequency = enc->keyframe_freq;
  enc->info.keyframe_frequency_force = enc->keyframe_force;
  enc->info.keyframe_data_target_bitrate = enc->video_bitrate * 1.5;
  enc->info.keyframe_auto_threshold = enc->keyframe_threshold;
  enc->info.keyframe_mindistance = enc->keyframe_mindistance;
  enc->info.noise_sensitivity = enc->noise_sensitivity;

  theora_encode_init (&enc->state, &enc->info);

  return GST_PAD_LINK_OK;
}

/* prepare a buffer for transmission by passing data through libtheora */
static GstBuffer *
theora_buffer_from_packet (GstTheoraEnc * enc, ogg_packet * packet,
    GstClockTime timestamp, GstClockTime duration)
{
  GstBuffer *buf;

  buf = gst_pad_alloc_buffer (enc->srcpad,
      GST_BUFFER_OFFSET_NONE, packet->bytes);
  memcpy (GST_BUFFER_DATA (buf), packet->packet, packet->bytes);
  GST_BUFFER_OFFSET (buf) = enc->bytes_out;
  GST_BUFFER_OFFSET_END (buf) = packet->granulepos;
  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;

  /* the second most significant bit of the first data byte is cleared
   * for keyframes */
  if ((packet->packet[0] & 40) == 0) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_KEY_UNIT);
  }

  enc->packetno++;

  return buf;
}

/* push out the buffer and do internal bookkeeping */
static void
theora_push_buffer (GstTheoraEnc * enc, GstBuffer * buffer)
{
  enc->bytes_out += GST_BUFFER_SIZE (buffer);

  if (GST_PAD_IS_USABLE (enc->srcpad)) {
    gst_pad_push (enc->srcpad, GST_DATA (buffer));
  } else {
    gst_buffer_unref (buffer);
  }
}

static void
theora_push_packet (GstTheoraEnc * enc, ogg_packet * packet,
    GstClockTime timestamp, GstClockTime duration)
{
  GstBuffer *buf;

  buf = theora_buffer_from_packet (enc, packet, timestamp, duration);
  theora_push_buffer (enc, buf);
}

static void
theora_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2, GstBuffer * buf3)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GValue list = { 0 };
  GValue value = { 0 };

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf3, GST_BUFFER_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&list, GST_TYPE_FIXED_LIST);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf1);
  gst_value_list_append_value (&list, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf2);
  gst_value_list_append_value (&list, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  g_value_set_boxed (&value, buf3);
  gst_value_list_append_value (&list, &value);
  gst_structure_set_value (structure, "streamheader", &list);
  g_value_unset (&value);
  g_value_unset (&list);
}

static void
theora_enc_chain (GstPad * pad, GstData * data)
{
  GstTheoraEnc *enc;
  ogg_packet op;
  GstBuffer *buf;
  GstClockTime in_time;

  enc = GST_THEORA_ENC (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (data)) {
    switch (GST_EVENT_TYPE (data)) {
      case GST_EVENT_EOS:
        /* push last packet with eos flag */
        while (theora_encode_packetout (&enc->state, 1, &op)) {
          GstClockTime out_time =
              theora_granule_time (&enc->state, op.granulepos) * GST_SECOND;
          theora_push_packet (enc, &op, out_time, GST_SECOND / enc->fps);
        }
      default:
        gst_pad_event_default (pad, GST_EVENT (data));
        return;
    }
  }

  buf = GST_BUFFER (data);
  in_time = GST_BUFFER_TIMESTAMP (buf);

  /* no packets written yet, setup headers */
  /* Theora streams begin with three headers; the initial header (with
     most of the codec setup parameters) which is mandated by the Ogg
     bitstream spec.  The second header holds any comment fields.  The
     third header holds the bitstream codebook.  We merely need to
     make the headers, then pass them to libtheora one at a time;
     libtheora handles the additional Ogg bitstream constraints */

  if (enc->packetno == 0) {
    GstCaps *caps;
    GstBuffer *buf1, *buf2, *buf3;

    /* first packet will get its own page automatically */
    theora_encode_header (&enc->state, &op);
    buf1 = theora_buffer_from_packet (enc, &op, 0, 0);

    /* create the remaining theora headers */
    theora_comment_init (&enc->comment);
    theora_encode_comment (&enc->comment, &op);
    buf2 = theora_buffer_from_packet (enc, &op, 0, 0);
    theora_encode_tables (&enc->state, &op);
    buf3 = theora_buffer_from_packet (enc, &op, 0, 0);

    /* mark buffers and put on caps */
    caps = gst_pad_get_caps (enc->srcpad);
    theora_set_header_on_caps (caps, buf1, buf2, buf3);

    /* negotiate with these caps */
    GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_try_set_caps (enc->srcpad, caps);

    /* push out the header buffers */
    theora_push_buffer (enc, buf1);
    theora_push_buffer (enc, buf2);
    theora_push_buffer (enc, buf3);
  }

  {
    yuv_buffer yuv;
    gint res;
    gint y_size;
    guint8 *pixels;

    yuv.y_width = enc->info_width;
    yuv.y_height = enc->info_height;
    yuv.y_stride = enc->info_width;

    yuv.uv_width = enc->info_width / 2;
    yuv.uv_height = enc->info_height / 2;
    yuv.uv_stride = yuv.uv_width;

    y_size = enc->info_width * enc->info_height;

    if (enc->width == enc->info_width && enc->height == enc->info_height) {
      /* easy case, no cropping/conversion needed */
      pixels = GST_BUFFER_DATA (buf);

      yuv.y = pixels;
      yuv.u = yuv.y + y_size;
      yuv.v = yuv.u + y_size / 4;
    } else {
      GstBuffer *newbuf;
      gint i;
      guint8 *dest_y, *src_y;
      guint8 *dest_u, *src_u;
      guint8 *dest_v, *src_v;
      gint src_y_stride, src_uv_stride;
      gint dst_y_stride, dst_uv_stride;
      gint width, height;
      gint cwidth, cheight;

      /* source width/height */
      width = enc->width;
      height = enc->height;
      /* soucre chroma width/height */
      cwidth = width / 2;
      cheight = height / 2;

      /* source strides as defined in videotestsrc */
      src_y_stride = ROUND_UP_4 (width);
      src_uv_stride = ROUND_UP_8 (width) / 2;

      /* destination strides from the real picture width */
      dst_y_stride = enc->info_width;
      dst_uv_stride = enc->info_width / 2;

      newbuf = gst_pad_alloc_buffer (enc->srcpad,
          GST_BUFFER_OFFSET_NONE, y_size * 3 / 2);

      yuv.y = GST_BUFFER_DATA (newbuf);
      yuv.u = yuv.y + y_size;
      yuv.v = yuv.u + y_size / 4;

      /* center if needed */
      dest_y = yuv.y + enc->offset_x + enc->offset_y * dst_y_stride;
      dest_u =
          yuv.u + (enc->offset_x / 2) + (enc->offset_y / 2) * dst_uv_stride;
      dest_v =
          yuv.v + (enc->offset_x / 2) + (enc->offset_y / 2) * dst_uv_stride;

      src_y = GST_BUFFER_DATA (buf);
      src_u = src_y + src_y_stride * ROUND_UP_2 (height);
      src_v = src_u + src_uv_stride * ROUND_UP_2 (height) / 2;

      /* copy Y plane */
      for (i = 0; i < height; i++) {
        memcpy (dest_y, src_y, width);

        dest_y += dst_y_stride;
        src_y += src_y_stride;
      }

      /* copy UV planes */
      for (i = 0; i < cheight; i++) {
        memcpy (dest_u, src_u, cwidth);
        memcpy (dest_v, src_v, cwidth);

        dest_u += dst_uv_stride;
        dest_v += dst_uv_stride;
        src_u += src_uv_stride;
        src_v += src_uv_stride;
      }

      gst_buffer_unref (buf);
      buf = newbuf;
    }

    res = theora_encode_YUVin (&enc->state, &yuv);
    while (theora_encode_packetout (&enc->state, 0, &op)) {
      GstClockTime out_time;

      out_time = theora_granule_time (&enc->state, op.granulepos) * GST_SECOND;
      theora_push_packet (enc, &op, out_time, GST_SECOND / enc->fps);
    }

    gst_buffer_unref (buf);
  }
}

static GstElementStateReturn
theora_enc_change_state (GstElement * element)
{
  GstTheoraEnc *enc = GST_THEORA_ENC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      theora_info_init (&enc->info);
      theora_comment_init (&enc->comment);
      enc->packetno = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      theora_clear (&enc->state);
      theora_comment_clear (&enc->comment);
      theora_info_clear (&enc->info);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return parent_class->change_state (element);
}

static void
theora_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTheoraEnc *enc = GST_THEORA_ENC (object);

  switch (prop_id) {
    case ARG_CENTER:
      enc->center = g_value_get_boolean (value);
      break;
    case ARG_BITRATE:
      enc->video_bitrate = g_value_get_int (value) * 1000;
      enc->video_quality = 0;
      break;
    case ARG_QUALITY:
      enc->video_quality = g_value_get_int (value);
      enc->video_bitrate = 0;
      break;
    case ARG_QUICK:
      enc->quick = g_value_get_boolean (value);
      break;
    case ARG_KEYFRAME_AUTO:
      enc->keyframe_auto = g_value_get_boolean (value);
      break;
    case ARG_KEYFRAME_FREQ:
      enc->keyframe_freq = g_value_get_int (value);
      break;
    case ARG_KEYFRAME_FREQ_FORCE:
      enc->keyframe_force = g_value_get_int (value);
      break;
    case ARG_KEYFRAME_THRESHOLD:
      enc->keyframe_threshold = g_value_get_int (value);
      break;
    case ARG_KEYFRAME_MINDISTANCE:
      enc->keyframe_mindistance = g_value_get_int (value);
      break;
    case ARG_NOISE_SENSITIVITY:
      enc->noise_sensitivity = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
theora_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTheoraEnc *enc = GST_THEORA_ENC (object);

  switch (prop_id) {
    case ARG_CENTER:
      g_value_set_boolean (value, enc->center);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, enc->video_bitrate / 1000);
      break;
    case ARG_QUALITY:
      g_value_set_int (value, enc->video_quality);
      break;
    case ARG_QUICK:
      g_value_set_boolean (value, enc->quick);
      break;
    case ARG_KEYFRAME_AUTO:
      g_value_set_boolean (value, enc->keyframe_auto);
      break;
    case ARG_KEYFRAME_FREQ:
      g_value_set_int (value, enc->keyframe_freq);
      break;
    case ARG_KEYFRAME_FREQ_FORCE:
      g_value_set_int (value, enc->keyframe_force);
      break;
    case ARG_KEYFRAME_THRESHOLD:
      g_value_set_int (value, enc->keyframe_threshold);
      break;
    case ARG_KEYFRAME_MINDISTANCE:
      g_value_set_int (value, enc->keyframe_mindistance);
      break;
    case ARG_NOISE_SENSITIVITY:
      g_value_set_int (value, enc->noise_sensitivity);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
