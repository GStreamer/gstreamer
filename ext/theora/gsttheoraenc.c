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

/**
 * SECTION:element-theoraenc
 * @see_also: theoradec, oggmux
 *
 * This element encodes raw video into a Theora stream.
 * <ulink url="http://www.theora.org/">Theora</ulink> is a royalty-free
 * video codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>, based on the VP3 codec.
 *
 * The theora codec internally only supports encoding of images that are a
 * multiple of 16 pixels in both X and Y direction. It is however perfectly
 * possible to encode images with other dimensions because an arbitrary
 * rectangular cropping region can be set up. This element will automatically
 * set up a correct cropping region if the dimensions are not multiples of 16
 * pixels.
 *
 * To control the quality of the encoding, the #GstTheoraEnc::bitrate and
 * #GstTheoraEnc::quality properties can be used. These two properties are
 * mutualy exclusive. Setting the bitrate property will produce a constant
 * bitrate (CBR) stream while setting the quality property will produce a
 * variable bitrate (VBR) stream.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v videotestsrc num-buffers=1000 ! theoraenc ! oggmux ! filesink location=videotestsrc.ogg
 * ]| This example pipeline will encode a test video source to theora muxed in an
 * ogg container. Refer to the theoradec documentation to decode the create
 * stream.
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsttheoraenc.h"

#include <string.h>
#include <stdlib.h>             /* free */

#include <gst/tag/tag.h>
#include <gst/video/video.h>

#define GST_CAT_DEFAULT theoraenc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_TYPE_BORDER_MODE (gst_border_mode_get_type())
static GType
gst_border_mode_get_type (void)
{
  static GType border_mode_type = 0;
  static const GEnumValue border_mode[] = {
    {BORDER_NONE, "No Border", "none"},
    {BORDER_BLACK, "Black Border", "black"},
    {BORDER_MIRROR, "Mirror image in borders", "mirror"},
    {0, NULL, NULL},
  };

  if (!border_mode_type) {
    border_mode_type =
        g_enum_register_static ("GstTheoraEncBorderMode", border_mode);
  }
  return border_mode_type;
}

/* taken from theora/lib/toplevel.c */
static int
_ilog (unsigned int v)
{
  int ret = 0;

  while (v) {
    ret++;
    v >>= 1;
  }
  return (ret);
}

#define THEORA_DEF_BITRATE              0
#define THEORA_DEF_QUALITY              48
#define THEORA_DEF_KEYFRAME_AUTO        TRUE
#define THEORA_DEF_KEYFRAME_FREQ        64
#define THEORA_DEF_KEYFRAME_FREQ_FORCE  64
#define THEORA_DEF_SPEEDLEVEL           1
#define THEORA_DEF_VP3_COMPATIBLE       FALSE
#define THEORA_DEF_DROP_FRAMES          TRUE
#define THEORA_DEF_CAP_OVERFLOW         TRUE
#define THEORA_DEF_CAP_UNDERFLOW        FALSE
#define THEORA_DEF_RATE_BUFFER          0
enum
{
  ARG_0,
  ARG_CENTER,
  ARG_BORDER,
  ARG_BITRATE,
  ARG_QUALITY,
  ARG_QUICK,
  ARG_KEYFRAME_AUTO,
  ARG_KEYFRAME_FREQ,
  ARG_KEYFRAME_FREQ_FORCE,
  ARG_KEYFRAME_THRESHOLD,
  ARG_KEYFRAME_MINDISTANCE,
  ARG_NOISE_SENSITIVITY,
  ARG_SHARPNESS,
  ARG_SPEEDLEVEL,
  ARG_VP3_COMPATIBLE,
  ARG_DROP_FRAMES,
  ARG_CAP_OVERFLOW,
  ARG_CAP_UNDERFLOW,
  ARG_RATE_BUFFER,
  /* FILL ME */
};

/* this function does a straight granulepos -> timestamp conversion */
static GstClockTime
granulepos_to_timestamp (GstTheoraEnc * theoraenc, ogg_int64_t granulepos)
{
  guint64 iframe, pframe;
  int shift = theoraenc->info.keyframe_granule_shift;

  if (granulepos < 0)
    return GST_CLOCK_TIME_NONE;

  iframe = granulepos >> shift;
  pframe = granulepos - (iframe << shift);

  /* num and den are 32 bit, so we can safely multiply with GST_SECOND */
  return gst_util_uint64_scale ((guint64) (iframe + pframe),
      GST_SECOND * theoraenc->info.fps_denominator,
      theoraenc->info.fps_numerator);
}

static GstStaticPadTemplate theora_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) { I420, Y42B, Y444 }, "
        "framerate = (fraction) [1/MAX, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate theora_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);
}

GST_BOILERPLATE_FULL (GstTheoraEnc, gst_theora_enc, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static gboolean theora_enc_sink_event (GstPad * pad, GstEvent * event);
static gboolean theora_enc_src_event (GstPad * pad, GstEvent * event);
static GstFlowReturn theora_enc_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn theora_enc_change_state (GstElement * element,
    GstStateChange transition);
static GstCaps *theora_enc_sink_getcaps (GstPad * pad);
static gboolean theora_enc_sink_setcaps (GstPad * pad, GstCaps * caps);
static void theora_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void theora_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void theora_enc_finalize (GObject * object);

static void
gst_theora_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_enc_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_enc_sink_factory));
  gst_element_class_set_details_simple (element_class,
      "Theora video encoder", "Codec/Encoder/Video",
      "encode raw YUV video to a theora stream",
      "Wim Taymans <wim@fluendo.com>");
}

static void
gst_theora_enc_class_init (GstTheoraEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = theora_enc_set_property;
  gobject_class->get_property = theora_enc_get_property;
  gobject_class->finalize = theora_enc_finalize;

  g_object_class_install_property (gobject_class, ARG_CENTER,
      g_param_spec_boolean ("center", "Center",
          "ignored and kept for API compat only", TRUE,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_BORDER,
      g_param_spec_enum ("border", "Border",
          "ignored and kept for API compat only",
          GST_TYPE_BORDER_MODE, BORDER_BLACK,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /* general encoding stream options */
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate", "Compressed video bitrate (kbps)",
          0, (1 << 24) - 1, THEORA_DEF_BITRATE,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_QUALITY,
      g_param_spec_int ("quality", "Quality", "Video quality", 0, 63,
          THEORA_DEF_QUALITY,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_QUICK,
      g_param_spec_boolean ("quick", "Quick",
          "ignored and kept for API compat only", TRUE,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_AUTO,
      g_param_spec_boolean ("keyframe-auto", "Keyframe Auto",
          "Automatic keyframe detection", THEORA_DEF_KEYFRAME_AUTO,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_FREQ,
      g_param_spec_int ("keyframe-freq", "Keyframe frequency",
          "Keyframe frequency", 1, 32768, THEORA_DEF_KEYFRAME_FREQ,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_FREQ_FORCE,
      g_param_spec_int ("keyframe-force", "Keyframe force",
          "Force keyframe every N frames", 1, 32768,
          THEORA_DEF_KEYFRAME_FREQ_FORCE,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_THRESHOLD,
      g_param_spec_int ("keyframe-threshold", "Keyframe threshold",
          "ignored and kept for API compat only", 0, 32768, 80,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_KEYFRAME_MINDISTANCE,
      g_param_spec_int ("keyframe-mindistance", "Keyframe mindistance",
          "ignored and kept for API compat only", 1, 32768, 8,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_NOISE_SENSITIVITY,
      g_param_spec_int ("noise-sensitivity", "Noise sensitivity",
          "ignored and kept for API compat only", 0, 32768, 1,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_SHARPNESS,
      g_param_spec_int ("sharpness", "Sharpness",
          "ignored and kept for API compat only", 0, 2, 0,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_SPEEDLEVEL,
      g_param_spec_int ("speed-level", "Speed level",
          "Controls the amount of motion vector searching done while "
          "encoding.  This property requires libtheora version >= 1.0",
          0, 2, THEORA_DEF_SPEEDLEVEL,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_VP3_COMPATIBLE,
      g_param_spec_boolean ("vp3-compatible", "VP3 Compatible",
          "Disables non-VP3 compatible features."
          "  This property requires libtheora version >= 1.1",
          THEORA_DEF_VP3_COMPATIBLE,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_DROP_FRAMES,
      g_param_spec_boolean ("drop-frames", "VP3 Compatible",
          "Allow or disallow frame dropping."
          "  This property requires libtheora version >= 1.1",
          THEORA_DEF_DROP_FRAMES,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_CAP_OVERFLOW,
      g_param_spec_boolean ("cap-overflow", "VP3 Compatible",
          "Enable capping of bit reservoir overflows."
          "  This property requires libtheora version >= 1.1",
          THEORA_DEF_CAP_OVERFLOW,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_CAP_UNDERFLOW,
      g_param_spec_boolean ("cap-underflow", "VP3 Compatible",
          "Enable capping of bit reservoir underflows."
          "  This property requires libtheora version >= 1.1",
          THEORA_DEF_CAP_UNDERFLOW,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_RATE_BUFFER,
      g_param_spec_int ("rate-buffer", "Rate Control Buffer",
          "Sets the size of the rate control buffer, in units of frames.  "
          "The default value of 0 instructs the encoder to automatically "
          "select an appropriate value."
          "  This property requires libtheora version >= 1.1",
          0, 1000, THEORA_DEF_RATE_BUFFER,
          (GParamFlags) G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = theora_enc_change_state;
  GST_DEBUG_CATEGORY_INIT (theoraenc_debug, "theoraenc", 0, "Theora encoder");
}

static void
gst_theora_enc_init (GstTheoraEnc * enc, GstTheoraEncClass * g_class)
{
  enc->sinkpad =
      gst_pad_new_from_static_template (&theora_enc_sink_factory, "sink");
  gst_pad_set_chain_function (enc->sinkpad, theora_enc_chain);
  gst_pad_set_event_function (enc->sinkpad, theora_enc_sink_event);
  gst_pad_set_getcaps_function (enc->sinkpad, theora_enc_sink_getcaps);
  gst_pad_set_setcaps_function (enc->sinkpad, theora_enc_sink_setcaps);
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  enc->srcpad =
      gst_pad_new_from_static_template (&theora_enc_src_factory, "src");
  gst_pad_set_event_function (enc->srcpad, theora_enc_src_event);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  gst_segment_init (&enc->segment, GST_FORMAT_UNDEFINED);

  enc->video_bitrate = THEORA_DEF_BITRATE;
  enc->video_quality = THEORA_DEF_QUALITY;
  enc->keyframe_auto = THEORA_DEF_KEYFRAME_AUTO;
  enc->keyframe_freq = THEORA_DEF_KEYFRAME_FREQ;
  enc->keyframe_force = THEORA_DEF_KEYFRAME_FREQ_FORCE;

  enc->expected_ts = GST_CLOCK_TIME_NONE;

  enc->speed_level = THEORA_DEF_SPEEDLEVEL;
  enc->vp3_compatible = THEORA_DEF_VP3_COMPATIBLE;
  enc->drop_frames = THEORA_DEF_DROP_FRAMES;
  enc->cap_overflow = THEORA_DEF_CAP_OVERFLOW;
  enc->cap_underflow = THEORA_DEF_CAP_UNDERFLOW;
  enc->rate_buffer = THEORA_DEF_RATE_BUFFER;
}

static void
theora_enc_finalize (GObject * object)
{
  GstTheoraEnc *enc = GST_THEORA_ENC (object);

  GST_DEBUG_OBJECT (enc, "Finalizing");
  if (enc->encoder)
    th_encode_free (enc->encoder);
  th_comment_clear (&enc->comment);
  th_info_clear (&enc->info);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
theora_enc_reset (GstTheoraEnc * enc)
{
  ogg_uint32_t keyframe_force;
  int rate_flags;

  if (enc->encoder)
    th_encode_free (enc->encoder);
  enc->encoder = th_encode_alloc (&enc->info);
  /* We ensure this function cannot fail. */
  g_assert (enc->encoder != NULL);
#ifdef TH_ENCCTL_SET_SPLEVEL
  th_encode_ctl (enc->encoder, TH_ENCCTL_SET_SPLEVEL, &enc->speed_level,
      sizeof (enc->speed_level));
#endif
#ifdef TH_ENCCTL_SET_VP3_COMPATIBLE
  th_encode_ctl (enc->encoder, TH_ENCCTL_SET_VP3_COMPATIBLE,
      &enc->vp3_compatible, sizeof (enc->vp3_compatible));
#endif

  rate_flags = 0;
#ifdef TH_ENCCTL_SET_RATE_FLAGS
  if (enc->drop_frames)
    rate_flags |= TH_RATECTL_DROP_FRAMES;
  if (enc->drop_frames)
    rate_flags |= TH_RATECTL_CAP_OVERFLOW;
  if (enc->drop_frames)
    rate_flags |= TH_RATECTL_CAP_UNDERFLOW;
  th_encode_ctl (enc->encoder, TH_ENCCTL_SET_RATE_FLAGS,
      &rate_flags, sizeof (rate_flags));
#endif

#ifdef TH_ENCCTL_SET_RATE_BUFFER
  if (enc->rate_buffer) {
    th_encode_ctl (enc->encoder, TH_ENCCTL_SET_RATE_BUFFER,
        &enc->rate_buffer, sizeof (enc->rate_buffer));
  } else {
    /* FIXME */
  }
#endif

  keyframe_force = enc->keyframe_auto ?
      enc->keyframe_force : enc->keyframe_freq;
  th_encode_ctl (enc->encoder, TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE,
      &keyframe_force, sizeof (keyframe_force));
}

static void
theora_enc_clear (GstTheoraEnc * enc)
{
  enc->packetno = 0;
  enc->bytes_out = 0;
  enc->granulepos_offset = 0;
  enc->timestamp_offset = 0;

  enc->next_ts = GST_CLOCK_TIME_NONE;
  enc->next_discont = FALSE;
  enc->expected_ts = GST_CLOCK_TIME_NONE;
}

static char *
theora_enc_get_supported_formats (void)
{
  th_enc_ctx *encoder;
  th_info info;
  struct
  {
    th_pixel_fmt pixelformat;
    const char *fourcc;
  } formats[] = {
    {
    TH_PF_420, "I420"}, {
    TH_PF_422, "Y42B"}, {
    TH_PF_444, "Y444"}
  };
  GString *string = NULL;
  guint i;

  th_info_init (&info);
  info.frame_width = 16;
  info.frame_height = 16;
  info.fps_numerator = 25;
  info.fps_denominator = 1;
  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    info.pixel_fmt = formats[i].pixelformat;

    encoder = th_encode_alloc (&info);
    if (encoder == NULL)
      continue;

    GST_LOG ("format %s is supported", formats[i].fourcc);
    th_encode_free (encoder);

    if (string == NULL) {
      string = g_string_new (formats[i].fourcc);
    } else {
      g_string_append (string, ", ");
      g_string_append (string, formats[i].fourcc);
    }
  }
  th_info_clear (&info);

  return string == NULL ? NULL : g_string_free (string, FALSE);
}

static GstCaps *
theora_enc_sink_getcaps (GstPad * pad)
{
  GstCaps *caps;
  char *supported_formats, *caps_string;

  supported_formats = theora_enc_get_supported_formats ();
  if (!supported_formats) {
    GST_WARNING ("no supported formats found. Encoder disabled?");
    return gst_caps_new_empty ();
  }

  caps_string = g_strdup_printf ("video/x-raw-yuv, "
      "format = (fourcc) { %s }, "
      "framerate = (fraction) [1/MAX, MAX], "
      "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]",
      supported_formats);
  caps = gst_caps_from_string (caps_string);
  g_free (caps_string);
  g_free (supported_formats);
  GST_DEBUG ("Supported caps: %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
theora_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstTheoraEnc *enc = GST_THEORA_ENC (gst_pad_get_parent (pad));
  guint32 fourcc;
  const GValue *par;
  gint fps_n, fps_d;

  gst_structure_get_fourcc (structure, "format", &fourcc);
  gst_structure_get_int (structure, "width", &enc->width);
  gst_structure_get_int (structure, "height", &enc->height);
  gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  th_info_clear (&enc->info);
  th_info_init (&enc->info);
  /* Theora has a divisible-by-sixteen restriction for the encoded video size but
   * we can define a picture area using pic_width/pic_height */
  enc->info.frame_width = GST_ROUND_UP_16 (enc->width);
  enc->info.frame_height = GST_ROUND_UP_16 (enc->height);
  enc->info.pic_width = enc->width;
  enc->info.pic_height = enc->height;
  switch (fourcc) {
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
      enc->info.pixel_fmt = TH_PF_420;
      break;
    case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
      enc->info.pixel_fmt = TH_PF_422;
      break;
    case GST_MAKE_FOURCC ('Y', '4', '4', '4'):
      enc->info.pixel_fmt = TH_PF_444;
      break;
    default:
      g_assert_not_reached ();
  }

  enc->info.fps_numerator = enc->fps_n = fps_n;
  enc->info.fps_denominator = enc->fps_d = fps_d;
  if (par) {
    enc->info.aspect_numerator = gst_value_get_fraction_numerator (par);
    enc->info.aspect_denominator = gst_value_get_fraction_denominator (par);
  } else {
    /* setting them to 0 indicates that the decoder can chose a good aspect
     * ratio, defaulting to 1/1 */
    enc->info.aspect_numerator = 0;
    enc->info.aspect_denominator = 0;
  }

  enc->info.colorspace = TH_CS_UNSPECIFIED;
  enc->info.target_bitrate = enc->video_bitrate;
  enc->info.quality = enc->video_quality;

  /* as done in theora */
  enc->info.keyframe_granule_shift = _ilog (enc->keyframe_force - 1);
  GST_DEBUG_OBJECT (enc,
      "keyframe_frequency_force is %d, granule shift is %d",
      enc->keyframe_force, enc->info.keyframe_granule_shift);

  theora_enc_reset (enc);
  enc->initialised = TRUE;

  gst_object_unref (enc);

  return TRUE;
}

static guint64
granulepos_add (guint64 granulepos, guint64 addend, gint shift)
{
  guint64 iframe, pframe;

  iframe = granulepos >> shift;
  pframe = granulepos - (iframe << shift);
  iframe += addend;

  return (iframe << shift) + pframe;
}

/* prepare a buffer for transmission by passing data through libtheora */
static GstFlowReturn
theora_buffer_from_packet (GstTheoraEnc * enc, ogg_packet * packet,
    GstClockTime timestamp, GstClockTime running_time,
    GstClockTime duration, GstBuffer ** buffer)
{
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  buf = gst_buffer_new_and_alloc (packet->bytes);
  if (!buf) {
    GST_WARNING_OBJECT (enc, "Could not allocate buffer");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  memcpy (GST_BUFFER_DATA (buf), packet->packet, packet->bytes);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (enc->srcpad));
  /* see ext/ogg/README; OFFSET_END takes "our" granulepos, OFFSET its
   * time representation */
  GST_BUFFER_OFFSET_END (buf) =
      granulepos_add (packet->granulepos, enc->granulepos_offset,
      enc->info.keyframe_granule_shift);
  GST_BUFFER_OFFSET (buf) = granulepos_to_timestamp (enc,
      GST_BUFFER_OFFSET_END (buf));

  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;

  if (enc->next_discont) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    enc->next_discont = FALSE;
  }

  /* the second most significant bit of the first data byte is cleared
   * for keyframes */
  if (packet->bytes > 0 && (packet->packet[0] & 0x40) == 0) {
    GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }
  enc->packetno++;

done:
  *buffer = buf;
  return ret;
}

/* push out the buffer and do internal bookkeeping */
static GstFlowReturn
theora_push_buffer (GstTheoraEnc * enc, GstBuffer * buffer)
{
  GstFlowReturn ret;

  enc->bytes_out += GST_BUFFER_SIZE (buffer);

  ret = gst_pad_push (enc->srcpad, buffer);

  return ret;
}

static GstFlowReturn
theora_push_packet (GstTheoraEnc * enc, ogg_packet * packet,
    GstClockTime timestamp, GstClockTime running_time, GstClockTime duration)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  ret =
      theora_buffer_from_packet (enc, packet, timestamp, running_time, duration,
      &buf);
  if (ret == GST_FLOW_OK)
    ret = theora_push_buffer (enc, buf);

  return ret;
}

static GstCaps *
theora_set_header_on_caps (GstCaps * caps, GSList * buffers)
{
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };
  GstBuffer *buffer;
  GSList *walk;

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  /* put copies of the buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);

  for (walk = buffers; walk; walk = walk->next) {
    buffer = walk->data;

    /* mark buffer */
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_IN_CAPS);

    /* Copy buffer, because we can't use the original -
     * it creates a circular refcount with the caps<->buffers */
    buffer = gst_buffer_copy (buffer);

    g_value_init (&value, GST_TYPE_BUFFER);
    gst_value_set_buffer (&value, buffer);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);

    /* Unref our copy */
    gst_buffer_unref (buffer);
  }

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);

  return caps;
}

static void
theora_enc_force_keyframe (GstTheoraEnc * enc)
{
  GstClockTime next_ts;

  /* make sure timestamps increment after resetting the decoder */
  next_ts = enc->next_ts + enc->timestamp_offset;

  theora_enc_reset (enc);
  enc->granulepos_offset =
      gst_util_uint64_scale (next_ts, enc->fps_n, GST_SECOND * enc->fps_d);
  enc->timestamp_offset = next_ts;
  enc->next_ts = 0;
}

static gboolean
theora_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstTheoraEnc *enc;
  ogg_packet op;
  gboolean res;

  enc = GST_THEORA_ENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &time);

      gst_segment_set_newsegment_full (&enc->segment, update, rate,
          applied_rate, format, start, stop, time);

      res = gst_pad_push_event (enc->srcpad, event);
      break;
    }
    case GST_EVENT_EOS:
      if (enc->initialised) {
        /* push last packet with eos flag, should not be called */
        while (th_encode_packetout (enc->encoder, 1, &op)) {
          GstClockTime next_time =
              th_granule_time (enc->encoder, op.granulepos) * GST_SECOND;

          theora_push_packet (enc, &op, GST_CLOCK_TIME_NONE, enc->next_ts,
              next_time - enc->next_ts);
          enc->next_ts = next_time;
        }
      }
      res = gst_pad_push_event (enc->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&enc->segment, GST_FORMAT_UNDEFINED);
      res = gst_pad_push_event (enc->srcpad, event);
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *s;

      s = gst_event_get_structure (event);

      if (gst_structure_has_name (s, "GstForceKeyUnit"))
        theora_enc_force_keyframe (enc);
      res = gst_pad_push_event (enc->srcpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (enc->srcpad, event);
      break;
  }
  return res;
}

static gboolean
theora_enc_src_event (GstPad * pad, GstEvent * event)
{
  GstTheoraEnc *enc;
  gboolean res = TRUE;

  enc = GST_THEORA_ENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const GstStructure *s;

      s = gst_event_get_structure (event);

      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        GST_OBJECT_LOCK (enc);
        enc->force_keyframe = TRUE;
        GST_OBJECT_UNLOCK (enc);
        /* consume the event */
        res = TRUE;
        gst_event_unref (event);
      } else {
        res = gst_pad_push_event (enc->sinkpad, event);
      }
      break;
    }
    default:
      res = gst_pad_push_event (enc->sinkpad, event);
      break;
  }

  return res;
}

static gboolean
theora_enc_is_discontinuous (GstTheoraEnc * enc, GstClockTime timestamp,
    GstClockTime duration)
{
  GstClockTimeDiff max_diff;
  gboolean ret = FALSE;

  /* Allow 3/4 a frame off */
  max_diff = (enc->info.fps_denominator * GST_SECOND * 3) /
      (enc->info.fps_numerator * 4);

  if (timestamp != GST_CLOCK_TIME_NONE
      && enc->expected_ts != GST_CLOCK_TIME_NONE) {
    if ((GstClockTimeDiff) (timestamp - enc->expected_ts) > max_diff) {
      GST_DEBUG_OBJECT (enc, "Incoming TS %" GST_TIME_FORMAT
          " exceeds expected value %" GST_TIME_FORMAT
          " by too much, marking discontinuity",
          GST_TIME_ARGS (timestamp), GST_TIME_ARGS (enc->expected_ts));
      ret = TRUE;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (duration))
    enc->expected_ts = timestamp + duration;
  else
    enc->expected_ts = GST_CLOCK_TIME_NONE;

  return ret;
}

static void
theora_enc_init_buffer (th_ycbcr_buffer buf, th_info * info, guint8 * data)
{
  GstVideoFormat format;
  guint i;

  switch (info->pixel_fmt) {
    case TH_PF_444:
      format = GST_VIDEO_FORMAT_Y444;
      break;
    case TH_PF_420:
      format = GST_VIDEO_FORMAT_I420;
      break;
    case TH_PF_422:
      format = GST_VIDEO_FORMAT_Y42B;
      break;
    default:
      g_assert_not_reached ();
  }

  /* According to Theora developer Timothy Terriberry, the Theora 
   * encoder will not use memory outside of pic_width/height, even when
   * the frame size is bigger. The values outside this region will be encoded
   * to default values.
   * Due to this, setting the frame's width/height as the buffer width/height
   * is perfectly ok, even though it does not strictly look ok.
   */
  for (i = 0; i < 3; i++) {
    buf[i].width =
        gst_video_format_get_component_width (format, i, info->frame_width);
    buf[i].height =
        gst_video_format_get_component_height (format, i, info->frame_height);

    buf[i].data =
        data + gst_video_format_get_component_offset (format, i,
        info->pic_width, info->pic_height);
    buf[i].stride =
        gst_video_format_get_row_stride (format, i, info->pic_width);
  }
}

static GstFlowReturn
theora_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstTheoraEnc *enc;
  ogg_packet op;
  GstClockTime timestamp, duration, running_time;
  GstFlowReturn ret;
  gboolean force_keyframe;

  enc = GST_THEORA_ENC (GST_PAD_PARENT (pad));

  /* we keep track of two timelines.
   * - The timestamps from the incomming buffers, which we copy to the outgoing
   *   encoded buffers as-is. We need to do this as we simply forward the
   *   newsegment events.
   * - The running_time of the buffers, which we use to construct the granulepos
   *   in the packets.
   */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  running_time =
      gst_segment_to_running_time (&enc->segment, GST_FORMAT_TIME, timestamp);
  if ((gint64) running_time < 0) {
    GST_DEBUG_OBJECT (enc, "Dropping buffer, timestamp: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* see if we need to schedule a keyframe */
  GST_OBJECT_LOCK (enc);
  force_keyframe = enc->force_keyframe;
  enc->force_keyframe = FALSE;
  GST_OBJECT_UNLOCK (enc);

  if (force_keyframe) {
    GstClockTime stream_time;
    GstStructure *s;

    stream_time = gst_segment_to_stream_time (&enc->segment,
        GST_FORMAT_TIME, timestamp);

    s = gst_structure_new ("GstForceKeyUnit",
        "timestamp", G_TYPE_UINT64, timestamp,
        "stream-time", G_TYPE_UINT64, stream_time,
        "running-time", G_TYPE_UINT64, running_time, NULL);

    theora_enc_force_keyframe (enc);

    gst_pad_push_event (enc->srcpad,
        gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s));
  }

  /* make sure we copy the discont flag to the next outgoing buffer when it's
   * set on the incomming buffer */
  if (GST_BUFFER_IS_DISCONT (buffer)) {
    enc->next_discont = TRUE;
  }

  if (enc->packetno == 0) {
    /* no packets written yet, setup headers */
    GstCaps *caps;
    GstBuffer *buf;
    GSList *buffers = NULL;
    int result;

    enc->granulepos_offset = 0;
    enc->timestamp_offset = 0;

    GST_DEBUG_OBJECT (enc, "output headers");
    /* Theora streams begin with three headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.  The
       third header holds the bitstream codebook.  We merely need to
       make the headers, then pass them to libtheora one at a time;
       libtheora handles the additional Ogg bitstream constraints */

    /* create the remaining theora headers */
    th_comment_clear (&enc->comment);
    th_comment_init (&enc->comment);

    while ((result =
            th_encode_flushheader (enc->encoder, &enc->comment, &op)) > 0) {
      ret =
          theora_buffer_from_packet (enc, &op, GST_CLOCK_TIME_NONE,
          GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, &buf);
      if (ret != GST_FLOW_OK) {
        goto header_buffer_alloc;
      }
      buffers = g_slist_prepend (buffers, buf);
    }
    if (result < 0) {
      g_slist_foreach (buffers, (GFunc) gst_buffer_unref, NULL);
      g_slist_free (buffers);
      goto encoder_disabled;
    }

    buffers = g_slist_reverse (buffers);

    /* mark buffers and put on caps */
    caps = gst_pad_get_caps (enc->srcpad);
    caps = theora_set_header_on_caps (caps, buffers);
    GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (enc->srcpad, caps);

    g_slist_foreach (buffers, (GFunc) gst_buffer_set_caps, caps);

    gst_caps_unref (caps);

    /* push out the header buffers */
    while (buffers) {
      buf = buffers->data;
      buffers = g_slist_delete_link (buffers, buffers);
      if ((ret = theora_push_buffer (enc, buf)) != GST_FLOW_OK) {
        g_slist_foreach (buffers, (GFunc) gst_buffer_unref, NULL);
        g_slist_free (buffers);
        goto header_push;
      }
    }

    enc->granulepos_offset =
        gst_util_uint64_scale (running_time, enc->fps_n,
        GST_SECOND * enc->fps_d);
    enc->timestamp_offset = running_time;
    enc->next_ts = 0;
  }

  {
    th_ycbcr_buffer ycbcr;
    gint res;

    theora_enc_init_buffer (ycbcr, &enc->info, GST_BUFFER_DATA (buffer));

    if (theora_enc_is_discontinuous (enc, running_time, duration)) {
      theora_enc_reset (enc);
      enc->granulepos_offset =
          gst_util_uint64_scale (running_time, enc->fps_n,
          GST_SECOND * enc->fps_d);
      enc->timestamp_offset = running_time;
      enc->next_ts = 0;
      enc->next_discont = TRUE;
    }

    res = th_encode_ycbcr_in (enc->encoder, ycbcr);
    /* none of the failure cases can happen here */
    g_assert (res == 0);

    ret = GST_FLOW_OK;
    while (th_encode_packetout (enc->encoder, 0, &op)) {
      GstClockTime next_time;

      next_time = th_granule_time (enc->encoder, op.granulepos) * GST_SECOND;

      ret =
          theora_push_packet (enc, &op, timestamp, enc->next_ts,
          next_time - enc->next_ts);

      enc->next_ts = next_time;
      if (ret != GST_FLOW_OK)
        goto data_push;
    }
    gst_buffer_unref (buffer);
  }

  return ret;

  /* ERRORS */
header_buffer_alloc:
  {
    gst_buffer_unref (buffer);
    return ret;
  }
header_push:
  {
    gst_buffer_unref (buffer);
    return ret;
  }
data_push:
  {
    gst_buffer_unref (buffer);
    return ret;
  }
encoder_disabled:
  {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("libtheora has been compiled with the encoder disabled"));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

static GstStateChangeReturn
theora_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstTheoraEnc *enc;
  GstStateChangeReturn ret;

  enc = GST_THEORA_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (enc, "READY->PAUSED Initing theora state");
      th_info_init (&enc->info);
      th_comment_init (&enc->comment);
      enc->packetno = 0;
      enc->force_keyframe = FALSE;
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
      GST_DEBUG_OBJECT (enc, "PAUSED->READY Clearing theora state");
      if (enc->encoder) {
        th_encode_free (enc->encoder);
        enc->encoder = NULL;
      }
      th_comment_clear (&enc->comment);
      th_info_clear (&enc->info);

      theora_enc_clear (enc);
      enc->initialised = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
theora_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTheoraEnc *enc = GST_THEORA_ENC (object);

  switch (prop_id) {
    case ARG_CENTER:
    case ARG_BORDER:
    case ARG_QUICK:
    case ARG_KEYFRAME_THRESHOLD:
    case ARG_KEYFRAME_MINDISTANCE:
    case ARG_NOISE_SENSITIVITY:
    case ARG_SHARPNESS:
      /* kept for API compat, but ignored */
      break;
    case ARG_BITRATE:
      enc->video_bitrate = g_value_get_int (value) * 1000;
      enc->video_quality = 0;
      break;
    case ARG_QUALITY:
      enc->video_quality = g_value_get_int (value);
      enc->video_bitrate = 0;
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
    case ARG_SPEEDLEVEL:
#ifdef TH_ENCCTL_SET_SPLEVEL
      enc->speed_level = g_value_get_int (value);
#endif
      break;
    case ARG_VP3_COMPATIBLE:
#ifdef TH_ENCCTL_SET_VP3_COMPATIBLE
      enc->vp3_compatible = g_value_get_boolean (value);
#endif
      break;
    case ARG_DROP_FRAMES:
#ifdef TH_ENCCTL_SET_RATE_FLAGS
      enc->drop_frames = g_value_get_boolean (value);
#endif
      break;
    case ARG_CAP_OVERFLOW:
#ifdef TH_ENCCTL_SET_RATE_FLAGS
      enc->cap_overflow = g_value_get_boolean (value);
#endif
      break;
    case ARG_CAP_UNDERFLOW:
#ifdef TH_ENCCTL_SET_RATE_FLAGS
      enc->cap_underflow = g_value_get_boolean (value);
#endif
      break;
    case ARG_RATE_BUFFER:
#ifdef TH_ENCCTL_SET_RATE_BUFFER
      enc->rate_buffer = g_value_get_int (value);
#endif
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
      g_value_set_boolean (value, TRUE);
      break;
    case ARG_BORDER:
      g_value_set_enum (value, BORDER_BLACK);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, enc->video_bitrate / 1000);
      break;
    case ARG_QUALITY:
      g_value_set_int (value, enc->video_quality);
      break;
    case ARG_QUICK:
      g_value_set_boolean (value, TRUE);
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
      g_value_set_int (value, 80);
      break;
    case ARG_KEYFRAME_MINDISTANCE:
      g_value_set_int (value, 8);
      break;
    case ARG_NOISE_SENSITIVITY:
      g_value_set_int (value, 1);
      break;
    case ARG_SHARPNESS:
      g_value_set_int (value, 0);
      break;
    case ARG_SPEEDLEVEL:
      g_value_set_int (value, enc->speed_level);
      break;
    case ARG_VP3_COMPATIBLE:
      g_value_set_boolean (value, enc->vp3_compatible);
      break;
    case ARG_DROP_FRAMES:
      g_value_set_boolean (value, enc->drop_frames);
      break;
    case ARG_CAP_OVERFLOW:
      g_value_set_boolean (value, enc->cap_overflow);
      break;
    case ARG_CAP_UNDERFLOW:
      g_value_set_boolean (value, enc->cap_underflow);
      break;
    case ARG_RATE_BUFFER:
      g_value_set_int (value, enc->rate_buffer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
