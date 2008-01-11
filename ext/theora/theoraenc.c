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
 * <refsect2>
 * <para>
 * This element encodes raw video into a Theora stream.
 * <ulink url="http://www.theora.org/">Theora</ulink> is a royalty-free
 * video codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>, based on the VP3 codec.
 * </para>
 * <para>
 * The theora codec internally only supports encoding of images that are a
 * multiple of 16 pixels in both X and Y direction. It is however perfectly
 * possible to encode images with other dimensions because an arbitrary
 * rectangular cropping region can be set up. This element will automatically
 * set up a correct cropping region if the dimensions are not multiples of 16
 * pixels. The "border" and "center" properties control how this cropping
 * region will be set up.
 * </para>
 * <para>
 * To control the quality of the encoding, the "bitrate" and "quality"
 * properties can be used. These two properties are mutualy exclusive. Setting
 * the bitrate property will produce a constant bitrate (CBR) stream while
 * setting the quality property will produce a variable bitrate (VBR) stream.
 * </para>
 * <title>Example pipeline</title>
 * <programlisting>
 * gst-launch -v videotestsrc num-buffers=1000 ! theoraenc ! oggmux ! filesink location=videotestsrc.ogg
 * </programlisting>
 * This example pipeline will encode a test video source to theora muxed in an
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

#define GST_CAT_DEFAULT theoraenc_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* With libtheora-1.0beta1 the granulepos scheme was changed:
 * where earlier the granulepos refered to the index/beginning
 * of a frame, it now refers to the end, which matches the use
 * in vorbis/speex. There don't seem to be defines for the
 * theora version we're compiling against, so we'll just use
 * a run-time check for now. See theora_enc_get_ogg_packet_end_time().
 */
static gboolean use_old_granulepos;

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

#define THEORA_DEF_CENTER               TRUE
#define THEORA_DEF_BORDER               BORDER_BLACK
#define THEORA_DEF_BITRATE              0
#define THEORA_DEF_QUALITY              16
#define THEORA_DEF_QUICK                TRUE
#define THEORA_DEF_KEYFRAME_AUTO        TRUE
#define THEORA_DEF_KEYFRAME_FREQ        64
#define THEORA_DEF_KEYFRAME_FREQ_FORCE  64
#define THEORA_DEF_KEYFRAME_THRESHOLD   80
#define THEORA_DEF_KEYFRAME_MINDISTANCE 8
#define THEORA_DEF_NOISE_SENSITIVITY    1
#define THEORA_DEF_SHARPNESS            0
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
  /* FILL ME */
};

/* this function does a straight granulepos -> timestamp conversion */
static GstClockTime
granulepos_to_timestamp (GstTheoraEnc * theoraenc, ogg_int64_t granulepos)
{
  guint64 iframe, pframe;
  int shift = theoraenc->granule_shift;

  if (granulepos < 0)
    return GST_CLOCK_TIME_NONE;

  iframe = granulepos >> shift;
  pframe = granulepos - (iframe << shift);

  /* num and den are 32 bit, so we can safely multiply with GST_SECOND */
  return gst_util_uint64_scale ((guint64) (iframe + pframe),
      GST_SECOND * theoraenc->info.fps_denominator,
      theoraenc->info.fps_numerator);
}

static const GstElementDetails theora_enc_details =
GST_ELEMENT_DETAILS ("Theora video encoder",
    "Codec/Encoder/Video",
    "encode raw YUV video to a theora stream",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate theora_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate theora_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

GST_BOILERPLATE (GstTheoraEnc, gst_theora_enc, GstElement, GST_TYPE_ELEMENT);

static gboolean theora_enc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn theora_enc_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn theora_enc_change_state (GstElement * element,
    GstStateChange transition);
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
  gst_element_class_set_details (element_class, &theora_enc_details);
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
          "Center image when sizes not multiple of 16", THEORA_DEF_CENTER,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BORDER,
      g_param_spec_enum ("border", "Border",
          "Border color to add when sizes not multiple of 16",
          GST_TYPE_BORDER_MODE, THEORA_DEF_BORDER,
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
  g_object_class_install_property (gobject_class, ARG_SHARPNESS,
      g_param_spec_int ("sharpness", "Sharpness",
          "Sharpness", 0, 2, THEORA_DEF_SHARPNESS,
          (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->change_state = theora_enc_change_state;
  GST_DEBUG_CATEGORY_INIT (theoraenc_debug, "theoraenc", 0, "Theora encoder");

  use_old_granulepos = (theora_version_number () <= 0x00030200);
}

static void
gst_theora_enc_init (GstTheoraEnc * enc, GstTheoraEncClass * g_class)
{
  enc->sinkpad =
      gst_pad_new_from_static_template (&theora_enc_sink_factory, "sink");
  gst_pad_set_chain_function (enc->sinkpad, theora_enc_chain);
  gst_pad_set_event_function (enc->sinkpad, theora_enc_sink_event);
  gst_pad_set_setcaps_function (enc->sinkpad, theora_enc_sink_setcaps);
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  enc->srcpad =
      gst_pad_new_from_static_template (&theora_enc_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->center = THEORA_DEF_CENTER;
  enc->border = THEORA_DEF_BORDER;

  enc->video_bitrate = THEORA_DEF_BITRATE;
  enc->video_quality = THEORA_DEF_QUALITY;
  enc->quick = THEORA_DEF_QUICK;
  enc->keyframe_auto = THEORA_DEF_KEYFRAME_AUTO;
  enc->keyframe_freq = THEORA_DEF_KEYFRAME_FREQ;
  enc->keyframe_force = THEORA_DEF_KEYFRAME_FREQ_FORCE;
  enc->keyframe_threshold = THEORA_DEF_KEYFRAME_THRESHOLD;
  enc->keyframe_mindistance = THEORA_DEF_KEYFRAME_MINDISTANCE;
  enc->noise_sensitivity = THEORA_DEF_NOISE_SENSITIVITY;
  enc->sharpness = THEORA_DEF_SHARPNESS;

  enc->granule_shift = _ilog (enc->info.keyframe_frequency_force - 1);
  GST_DEBUG_OBJECT (enc,
      "keyframe_frequency_force is %d, granule shift is %d",
      enc->info.keyframe_frequency_force, enc->granule_shift);
  enc->expected_ts = GST_CLOCK_TIME_NONE;
}

static void
theora_enc_finalize (GObject * object)
{
  GstTheoraEnc *enc = GST_THEORA_ENC (object);

  GST_DEBUG_OBJECT (enc, "Finalizing");
  theora_clear (&enc->state);
  theora_comment_clear (&enc->comment);
  theora_info_clear (&enc->info);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
theora_enc_reset (GstTheoraEnc * enc)
{
  theora_clear (&enc->state);
  theora_encode_init (&enc->state, &enc->info);
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

static gboolean
theora_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstTheoraEnc *enc = GST_THEORA_ENC (gst_pad_get_parent (pad));
  const GValue *par;
  gint fps_n, fps_d;

  gst_structure_get_int (structure, "width", &enc->width);
  gst_structure_get_int (structure, "height", &enc->height);
  gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  theora_info_clear (&enc->info);
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
    enc->offset_x = GST_ROUND_UP_2 ((enc->info_width - enc->width) / 2);
    enc->offset_y = GST_ROUND_UP_2 ((enc->info_height - enc->height) / 2);
  } else {
    enc->offset_x = 0;
    enc->offset_y = 0;
  }
  enc->info.offset_x = enc->offset_x;
  enc->info.offset_y = enc->offset_y;

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
  enc->info.sharpness = enc->sharpness;

  /* as done in theora */
  enc->granule_shift = _ilog (enc->info.keyframe_frequency_force - 1);
  GST_DEBUG_OBJECT (enc,
      "keyframe_frequency_force is %d, granule shift is %d",
      enc->info.keyframe_frequency_force, enc->granule_shift);

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
    GstClockTime timestamp, GstClockTime duration, GstBuffer ** buffer)
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
      enc->granule_shift);
  GST_BUFFER_OFFSET (buf) = granulepos_to_timestamp (enc,
      GST_BUFFER_OFFSET_END (buf));

  GST_BUFFER_TIMESTAMP (buf) = timestamp + enc->timestamp_offset;
  GST_BUFFER_DURATION (buf) = duration;

  if (enc->next_discont) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    enc->next_discont = FALSE;
  }

  /* the second most significant bit of the first data byte is cleared
   * for keyframes */
  if ((packet->packet[0] & 0x40) == 0) {
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
    GstClockTime timestamp, GstClockTime duration)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  ret = theora_buffer_from_packet (enc, packet, timestamp, duration, &buf);
  if (ret == GST_FLOW_OK)
    ret = theora_push_buffer (enc, buf);

  return ret;
}

static GstCaps *
theora_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2, GstBuffer * buf3)
{
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf3, GST_BUFFER_FLAG_IN_CAPS);

  /* Copy buffers, because we can't use the originals -
   * it creates a circular refcount with the caps<->buffers */
  buf1 = gst_buffer_copy (buf1);
  buf2 = gst_buffer_copy (buf2);
  buf3 = gst_buffer_copy (buf3);

  /* put copies of the buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);

  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf1);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf2);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_set_buffer (&value, buf3);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);

  /* Unref our copies */
  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
  gst_buffer_unref (buf3);

  return caps;
}

static GstClockTime
theora_enc_get_ogg_packet_end_time (GstTheoraEnc * enc, ogg_packet * op)
{
  ogg_int64_t end_granule;

  /* FIXME: remove this hack once we depend on libtheora >= 1.0beta1 */
  if (G_UNLIKELY (use_old_granulepos)) {
    /* This is where we hack around theora's broken idea of what granulepos
     * is -- normally we wouldn't need to add the 1, because granulepos
     * should be the presentation time of the last sample in the packet, but
     * theora starts with 0 instead of 1... (update: this only applies to old
     * bitstream/theora versions, this is fixed with bitstream version 3.2.1) */
    end_granule = granulepos_add (op->granulepos, 1, enc->granule_shift);
  } else {
    end_granule = op->granulepos;
  }
  return theora_granule_time (&enc->state, end_granule) * GST_SECOND;
}

static gboolean
theora_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstTheoraEnc *enc;
  ogg_packet op;
  gboolean res;

  enc = GST_THEORA_ENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (enc->initialised) {
        /* push last packet with eos flag */
        while (theora_encode_packetout (&enc->state, 1, &op)) {
          GstClockTime next_time =
              theora_enc_get_ogg_packet_end_time (enc, &op);

          theora_push_packet (enc, &op, enc->next_ts, next_time - enc->next_ts);
          enc->next_ts = next_time;
        }
      }
      res = gst_pad_push_event (enc->srcpad, event);
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *s;

      s = gst_event_get_structure (event);

      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        GstClockTime next_ts;

        /* make sure timestamps increment after resetting the decoder */
        next_ts = enc->next_ts + enc->timestamp_offset;

        theora_enc_reset (enc);
        enc->granulepos_offset =
            gst_util_uint64_scale (next_ts, enc->fps_n,
            GST_SECOND * enc->fps_d);
        enc->timestamp_offset = next_ts;
        enc->next_ts = 0;
      }
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
theora_enc_is_discontinuous (GstTheoraEnc * enc, GstBuffer * buffer)
{
  GstClockTime ts = GST_BUFFER_TIMESTAMP (buffer);
  GstClockTimeDiff max_diff;
  gboolean ret = FALSE;

  /* Allow 3/4 a frame off */
  max_diff = (enc->info.fps_denominator * GST_SECOND * 3) /
      (enc->info.fps_numerator * 4);

  if (ts != GST_CLOCK_TIME_NONE && enc->expected_ts != GST_CLOCK_TIME_NONE) {
    if ((GstClockTimeDiff) (ts - enc->expected_ts) > max_diff) {
      GST_DEBUG_OBJECT (enc, "Incoming TS %" GST_TIME_FORMAT
          " exceeds expected value %" GST_TIME_FORMAT
          " by too much, marking discontinuity",
          GST_TIME_ARGS (ts), GST_TIME_ARGS (enc->expected_ts));
      ret = TRUE;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)))
    enc->expected_ts = ts + GST_BUFFER_DURATION (buffer);
  else
    enc->expected_ts = GST_CLOCK_TIME_NONE;

  return ret;
}

static GstFlowReturn
theora_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstTheoraEnc *enc;
  ogg_packet op;
  GstClockTime in_time;
  GstFlowReturn ret;

  enc = GST_THEORA_ENC (GST_PAD_PARENT (pad));

  in_time = GST_BUFFER_TIMESTAMP (buffer);

  if (enc->packetno == 0) {
    /* no packets written yet, setup headers */
    GstCaps *caps;
    GstBuffer *buf1, *buf2, *buf3;

    enc->granulepos_offset = 0;
    enc->timestamp_offset = 0;

    GST_DEBUG_OBJECT (enc, "output headers");
    /* Theora streams begin with three headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.  The
       third header holds the bitstream codebook.  We merely need to
       make the headers, then pass them to libtheora one at a time;
       libtheora handles the additional Ogg bitstream constraints */

    /* first packet will get its own page automatically */
    if (theora_encode_header (&enc->state, &op) != 0)
      goto encoder_disabled;

    ret = theora_buffer_from_packet (enc, &op, GST_CLOCK_TIME_NONE,
        GST_CLOCK_TIME_NONE, &buf1);
    if (ret != GST_FLOW_OK) {
      goto header_buffer_alloc;
    }

    /* create the remaining theora headers */
    theora_comment_clear (&enc->comment);
    theora_comment_init (&enc->comment);

    if (theora_encode_comment (&enc->comment, &op) != 0)
      goto encoder_disabled;

    ret = theora_buffer_from_packet (enc, &op, GST_CLOCK_TIME_NONE,
        GST_CLOCK_TIME_NONE, &buf2);
    /* Theora expects us to put this packet buffer into an ogg page,
     * in which case it becomes the ogg library's responsibility to
     * free it. Since we're copying and outputting a gst_buffer,
     * we need to free it ourselves. */
    if (op.packet)
      free (op.packet);

    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buf1);
      goto header_buffer_alloc;
    }

    if (theora_encode_tables (&enc->state, &op) != 0)
      goto encoder_disabled;

    ret = theora_buffer_from_packet (enc, &op, GST_CLOCK_TIME_NONE,
        GST_CLOCK_TIME_NONE, &buf3);
    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buf1);
      gst_buffer_unref (buf2);
      goto header_buffer_alloc;
    }

    /* mark buffers and put on caps */
    caps = gst_pad_get_caps (enc->srcpad);
    caps = theora_set_header_on_caps (caps, buf1, buf2, buf3);
    GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (enc->srcpad, caps);

    gst_buffer_set_caps (buf1, caps);
    gst_buffer_set_caps (buf2, caps);
    gst_buffer_set_caps (buf3, caps);

    gst_caps_unref (caps);

    /* push out the header buffers */
    if ((ret = theora_push_buffer (enc, buf1)) != GST_FLOW_OK) {
      gst_buffer_unref (buf2);
      gst_buffer_unref (buf3);
      goto header_push;
    }
    if ((ret = theora_push_buffer (enc, buf2)) != GST_FLOW_OK) {
      gst_buffer_unref (buf3);
      goto header_push;
    }
    if ((ret = theora_push_buffer (enc, buf3)) != GST_FLOW_OK) {
      goto header_push;
    }

    enc->granulepos_offset =
        gst_util_uint64_scale (in_time, enc->fps_n, GST_SECOND * enc->fps_d);
    enc->timestamp_offset = in_time;
    enc->next_ts = 0;
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
      GST_LOG_OBJECT (enc, "no cropping/conversion needed");
      /* easy case, no cropping/conversion needed */
      pixels = GST_BUFFER_DATA (buffer);

      yuv.y = pixels;
      yuv.u = yuv.y + y_size;
      yuv.v = yuv.u + y_size / 4;
    } else {
      GstBuffer *newbuf;
      gint i;
      guchar *dest_y, *src_y;
      guchar *dest_u, *src_u;
      guchar *dest_v, *src_v;
      gint src_y_stride, src_uv_stride;
      gint dst_y_stride, dst_uv_stride;
      gint width, height;
      gint cwidth, cheight;
      gint offset_x, right_x, right_border;

      GST_LOG_OBJECT (enc, "cropping/conversion needed for strides");
      /* source width/height */
      width = enc->width;
      height = enc->height;
      /* soucre chroma width/height */
      cwidth = width / 2;
      cheight = height / 2;

      /* source strides as defined in videotestsrc */
      src_y_stride = GST_ROUND_UP_4 (width);
      src_uv_stride = GST_ROUND_UP_8 (width) / 2;

      /* destination strides from the real picture width */
      dst_y_stride = enc->info_width;
      dst_uv_stride = enc->info_width / 2;

      newbuf = gst_buffer_new_and_alloc (y_size * 3 / 2);
      if (!newbuf) {
        ret = GST_FLOW_ERROR;
        goto no_buffer;
      }
      GST_BUFFER_OFFSET (newbuf) = GST_BUFFER_OFFSET_NONE;
      gst_buffer_set_caps (newbuf, GST_PAD_CAPS (enc->srcpad));

      dest_y = yuv.y = GST_BUFFER_DATA (newbuf);
      dest_u = yuv.u = yuv.y + y_size;
      dest_v = yuv.v = yuv.u + y_size / 4;

      src_y = GST_BUFFER_DATA (buffer);
      src_u = src_y + src_y_stride * GST_ROUND_UP_2 (height);
      src_v = src_u + src_uv_stride * GST_ROUND_UP_2 (height) / 2;

      if (enc->border != BORDER_NONE) {
        /* fill top border */
        for (i = 0; i < enc->offset_y; i++) {
          memset (dest_y, 0, dst_y_stride);
          dest_y += dst_y_stride;
        }
      } else {
        dest_y += dst_y_stride * enc->offset_y;
      }

      offset_x = enc->offset_x;
      right_x = width + enc->offset_x;
      right_border = dst_y_stride - right_x;

      /* copy Y plane */
      for (i = 0; i < height; i++) {
        memcpy (dest_y + offset_x, src_y, width);
        if (enc->border != BORDER_NONE) {
          memset (dest_y, 0, offset_x);
          memset (dest_y + right_x, 0, right_border);
        }

        dest_y += dst_y_stride;
        src_y += src_y_stride;
      }

      if (enc->border != BORDER_NONE) {
        /* fill bottom border */
        for (i = height + enc->offset_y; i < enc->info.height; i++) {
          memset (dest_y, 0, dst_y_stride);
          dest_y += dst_y_stride;
        }

        /* fill top border chroma */
        for (i = 0; i < enc->offset_y / 2; i++) {
          memset (dest_u, 128, dst_uv_stride);
          memset (dest_v, 128, dst_uv_stride);
          dest_u += dst_uv_stride;
          dest_v += dst_uv_stride;
        }
      } else {
        dest_u += dst_uv_stride * enc->offset_y / 2;
        dest_v += dst_uv_stride * enc->offset_y / 2;
      }

      offset_x = enc->offset_x / 2;
      right_x = cwidth + offset_x;
      right_border = dst_uv_stride - right_x;

      /* copy UV planes */
      for (i = 0; i < cheight; i++) {
        memcpy (dest_v + offset_x, src_v, cwidth);
        memcpy (dest_u + offset_x, src_u, cwidth);

        if (enc->border != BORDER_NONE) {
          memset (dest_u, 128, offset_x);
          memset (dest_u + right_x, 128, right_border);
          memset (dest_v, 128, offset_x);
          memset (dest_v + right_x, 128, right_border);
        }

        dest_u += dst_uv_stride;
        dest_v += dst_uv_stride;
        src_u += src_uv_stride;
        src_v += src_uv_stride;
      }

      if (enc->border != BORDER_NONE) {
        /* fill bottom border */
        for (i = cheight + enc->offset_y / 2; i < enc->info_height / 2; i++) {
          memset (dest_u, 128, dst_uv_stride);
          memset (dest_v, 128, dst_uv_stride);
          dest_u += dst_uv_stride;
          dest_v += dst_uv_stride;
        }
      }

      gst_buffer_unref (buffer);
      buffer = newbuf;
    }

    if (theora_enc_is_discontinuous (enc, buffer)) {
      theora_enc_reset (enc);
      enc->granulepos_offset =
          gst_util_uint64_scale (in_time, enc->fps_n, GST_SECOND * enc->fps_d);
      enc->timestamp_offset = in_time;
      enc->next_ts = 0;
      enc->next_discont = TRUE;
    }

    res = theora_encode_YUVin (&enc->state, &yuv);

    ret = GST_FLOW_OK;
    while (theora_encode_packetout (&enc->state, 0, &op)) {
      GstClockTime next_time = theora_enc_get_ogg_packet_end_time (enc, &op);

      ret =
          theora_push_packet (enc, &op, enc->next_ts, next_time - enc->next_ts);
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
no_buffer:
  {
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
      theora_info_init (&enc->info);
      theora_comment_init (&enc->comment);
      enc->packetno = 0;
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
      theora_clear (&enc->state);
      theora_comment_clear (&enc->comment);
      theora_info_clear (&enc->info);

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
      enc->center = g_value_get_boolean (value);
      break;
    case ARG_BORDER:
      enc->border = g_value_get_enum (value);
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
    case ARG_SHARPNESS:
      enc->sharpness = g_value_get_int (value);
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
    case ARG_BORDER:
      g_value_set_enum (value, enc->border);
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
    case ARG_SHARPNESS:
      g_value_set_int (value, enc->sharpness);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
