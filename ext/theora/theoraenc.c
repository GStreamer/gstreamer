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

  gint video_bitrate;           /* bitrate target for Theora video */
  gint video_quality;           /* Theora quality selector 0 = low, 63 = high */

  gint width, height;

  guint packetno;
  guint64 granulepos;
};

struct _GstTheoraEncClass
{
  GstElementClass parent_class;
};

#define THEORA_DEF_BITRATE 0
#define THEORA_DEF_QUALITY 16
enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_QUALITY
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

  /* general encoding stream options */
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate", "Compressed video bitrate (kbps)",
          0, 2000, THEORA_DEF_BITRATE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_QUALITY,
      g_param_spec_int ("quality", "Quality", "Video quality",
          0, 63, THEORA_DEF_QUALITY, (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->change_state = theora_enc_change_state;
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

  GST_FLAG_SET (enc, GST_ELEMENT_EVENT_AWARE);
}

static GstPadLinkReturn
theora_enc_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstTheoraEnc *enc = GST_THEORA_ENC (gst_pad_get_parent (pad));
  gdouble fps;

  if (!gst_caps_is_fixed (caps))
    return GST_PAD_LINK_DELAYED;

  gst_structure_get_int (structure, "width", &enc->width);
  gst_structure_get_int (structure, "height", &enc->height);
  gst_structure_get_double (structure, "framerate", &fps);

  /* Theora has a divisible-by-sixteen restriction for the encoded video size */
  if ((enc->width & 0x0f) != 0 || (enc->height & 0x0f) != 0)
    return GST_PAD_LINK_REFUSED;

  theora_info_init (&enc->info);
  enc->info.width = enc->width;
  enc->info.height = enc->height;
  enc->info.frame_width = enc->width;
  enc->info.frame_height = enc->height;
  enc->info.offset_x = 0;
  enc->info.offset_y = 0;

  /* fixme, not done correctly */
  enc->info.fps_numerator = fps;
  enc->info.fps_denominator = 1;
  enc->info.aspect_numerator = 1;
  enc->info.aspect_denominator = 1;
  /* */

  enc->info.colorspace = OC_CS_UNSPECIFIED;
  enc->info.target_bitrate = enc->video_bitrate;
  enc->info.quality = enc->video_quality;

  enc->info.dropframes_p = 0;
  enc->info.quick_p = 1;
  enc->info.keyframe_auto_p = 1;
  enc->info.keyframe_frequency = 64;
  enc->info.keyframe_frequency_force = 64;
  enc->info.keyframe_data_target_bitrate = enc->video_bitrate * 1.5;
  enc->info.keyframe_auto_threshold = 80;
  enc->info.keyframe_mindistance = 8;
  enc->info.noise_sensitivity = 1;

  theora_encode_init (&enc->state, &enc->info);

  return GST_PAD_LINK_OK;
}

static void
theora_enc_event (GstTheoraEnc * enc, GstEvent * event)
{
  GST_LOG_OBJECT (enc, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }
  gst_pad_event_default (enc->sinkpad, event);
}

static void
theora_push_packet (GstTheoraEnc * enc, ogg_packet * packet)
{
  GstBuffer *buf;

  buf = gst_pad_alloc_buffer (enc->srcpad,
      GST_BUFFER_OFFSET_NONE, packet->bytes);
  memcpy (GST_BUFFER_DATA (buf), packet->packet, packet->bytes);
  GST_BUFFER_OFFSET_END (buf) = packet->granulepos;
  if (GST_PAD_IS_USABLE (enc->srcpad))
    gst_pad_push (enc->srcpad, GST_DATA (buf));

  enc->packetno++;
}

static void
theora_enc_chain (GstPad * pad, GstData * data)
{
  GstTheoraEnc *enc;
  ogg_packet op;

  enc = GST_THEORA_ENC (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (data)) {
    theora_enc_event (enc, GST_EVENT (data));
    return;
  }

  /* no packets written yet, setup headers */
  if (enc->packetno == 0) {
    GstCaps *caps;

    caps = gst_caps_new_simple ("video/x-theora", NULL);
    gst_pad_set_explicit_caps (enc->srcpad, caps);
    gst_caps_free (caps);

    /* first packet will get its own page automatically */
    theora_encode_header (&enc->state, &op);
    theora_push_packet (enc, &op);

    /* create the remaining theora headers */
    theora_comment_init (&enc->comment);
    theora_encode_comment (&enc->comment, &op);
    theora_push_packet (enc, &op);
    theora_encode_tables (&enc->state, &op);
    theora_push_packet (enc, &op);
  }

  yuv_buffer yuv;
  gint y_size;
  guchar *pixels;

  pixels = GST_BUFFER_DATA (GST_BUFFER (data));

  yuv.y_width = enc->width;
  yuv.y_height = enc->height;
  yuv.y_stride = enc->width;

  yuv.uv_width = enc->width / 2;
  yuv.uv_height = enc->height / 2;
  yuv.uv_stride = yuv.uv_width;

  y_size = enc->width * enc->height;

  yuv.y = pixels;
  yuv.u = pixels + y_size;
  yuv.v = pixels + y_size * 5 / 4;

  theora_encode_YUVin (&enc->state, &yuv);
  theora_encode_packetout (&enc->state, 0, &op);
  theora_push_packet (enc, &op);

  gst_data_unref (data);
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
      enc->granulepos = 0;
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
      g_assert_not_reached ();
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
    case ARG_BITRATE:
      enc->video_bitrate = g_value_get_int (value) * 1000;
      enc->video_quality = 0;
      break;
    case ARG_QUALITY:
      enc->video_quality = g_value_get_int (value);
      enc->video_bitrate = 0;
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
    case ARG_BITRATE:
      g_value_set_int (value, enc->video_bitrate);
      break;
    case ARG_QUALITY:
      g_value_set_int (value, enc->video_quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
