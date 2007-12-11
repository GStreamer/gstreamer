/* GStreamer H264 encoder plugin
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 * Copyright (C) 2005 Josef Zlomek <josef.zlomek@itonis.tv>
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

#include "gstx264enc.h"

#include <string.h>
#include <stdlib.h>


enum
{
  ARG_0,
  ARG_THREADS,
  ARG_PASS,
  ARG_STATS_FILE,
  ARG_BYTE_STREAM,
  ARG_BITRATE,
  ARG_VBV_BUF_CAPACITY,
  ARG_ME,
  ARG_SUBME,
  ARG_ANALYSE,
  ARG_DCT8x8,
  ARG_REF,
  ARG_BFRAMES,
  ARG_B_PYRAMID,
  ARG_WEIGHTB,
  ARG_SPS_ID,
  ARG_TRELLIS,
  ARG_KEYINT_MAX,
  ARG_CABAC
};

#define ARG_THREADS_DEFAULT 1
#define ARG_PASS_DEFAULT 0
#define ARG_STATS_FILE_DEFAULT "x264.log"
#define ARG_BYTE_STREAM_DEFAULT FALSE
#define ARG_BITRATE_DEFAULT (2 * 1024)
#define ARG_VBV_BUF_CAPACITY_DEFAULT 600
#define ARG_ME_DEFAULT X264_ME_HEX
#define ARG_SUBME_DEFAULT 1
#define ARG_ANALYSE_DEFAULT 0
#define ARG_DCT8x8_DEFAULT FALSE
#define ARG_REF_DEFAULT 1
#define ARG_BFRAMES_DEFAULT 0
#define ARG_B_PYRAMID_DEFAULT FALSE
#define ARG_WEIGHTB_DEFAULT FALSE
#define ARG_SPS_ID_DEFAULT 0
#define ARG_TRELLIS_DEFAULT TRUE
#define ARG_KEYINT_MAX_DEFAULT 0
#define ARG_CABAC_DEFAULT TRUE

#define GST_X264_ENC_ME_TYPE (gst_x264_enc_me_get_type())
static GType
gst_x264_enc_me_get_type (void)
{
  static GType me_type = 0;
  static const GEnumValue me_types[] = {
    {X264_ME_DIA, "diamond search, radius 1 (fast)", "dia"},
    {X264_ME_HEX, "hexagonal search, radius 2", "hex"},
    {X264_ME_UMH, "uneven multi-hexagon search", "umh"},
    {X264_ME_ESA, "exhaustive search (slow)", "esa"},
    {0, NULL, NULL}
  };

  if (!me_type) {
    me_type = g_enum_register_static ("GstX264EncMe", me_types);
  }
  return me_type;
}

#define GST_X264_ENC_ANALYSE_TYPE (gst_x264_enc_analyse_get_type())
static GType
gst_x264_enc_analyse_get_type (void)
{
  static GType analyse_type = 0;
  static const GFlagsValue analyse_types[] = {
    {X264_ANALYSE_I4x4, "i4x4", "i4x4"},
    {X264_ANALYSE_I8x8, "i8x8", "i8x8"},
    {X264_ANALYSE_PSUB16x16, "p8x8", "p8x8"},
    {X264_ANALYSE_PSUB8x8, "p4x4", "p4x4"},
    {X264_ANALYSE_BSUB16x16, "b8x8", "b8x8"},
    {0, NULL, NULL},
  };

  if (!analyse_type) {
    analyse_type = g_flags_register_static ("GstX264EncAnalyse", analyse_types);
  }
  return analyse_type;
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264")
    );

GST_BOILERPLATE (GstX264Enc, gst_x264_enc, GstElement, GST_TYPE_ELEMENT);

static void gst_x264_enc_dispose (GObject * object);

static gboolean gst_x264_enc_init_encoder (GstX264Enc * encoder);
static void gst_x264_enc_close_encoder (GstX264Enc * encoder);

static gboolean gst_x264_enc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_x264_enc_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_x264_enc_encode_frame (GstX264Enc * encoder,
    x264_picture_t * pic_in, int *i_nal);
static GstStateChangeReturn gst_x264_enc_change_state (GstElement * element,
    GstStateChange transition);

static void gst_x264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_x264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_x264_enc_timestamp_queue_init (GstX264Enc * encoder)
{
  encoder->timestamp_queue_size = (2 + encoder->bframes + encoder->threads) * 2;
  encoder->timestamp_queue_head = 0;
  encoder->timestamp_queue_tail = 0;
  encoder->timestamp_queue =
      g_new (GstClockTime, encoder->timestamp_queue_size);
  encoder->timestamp_queue_dur =
      g_new (GstClockTime, encoder->timestamp_queue_size);
}

static void
gst_x264_enc_timestamp_queue_free (GstX264Enc * encoder)
{
  if (encoder->timestamp_queue) {
    g_free (encoder->timestamp_queue);
    encoder->timestamp_queue = NULL;
  }
  if (encoder->timestamp_queue_dur) {
    g_free (encoder->timestamp_queue_dur);
    encoder->timestamp_queue_dur = NULL;
  }

  encoder->timestamp_queue_size = 0;
  encoder->timestamp_queue_head = 0;
  encoder->timestamp_queue_tail = 0;
}

static void
gst_x264_enc_timestamp_queue_put (GstX264Enc * encoder, GstClockTime clock_time,
    GstClockTime duration)
{
  encoder->timestamp_queue[encoder->timestamp_queue_tail] = clock_time;
  encoder->timestamp_queue_dur[encoder->timestamp_queue_tail] = duration;
  encoder->timestamp_queue_tail++;
  encoder->timestamp_queue_tail %= encoder->timestamp_queue_size;

  if (encoder->timestamp_queue_tail == encoder->timestamp_queue_head) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Timestamp queue overflow."), ("FIX CODE"));
  }
}

static void
gst_x264_enc_timestamp_queue_get (GstX264Enc * encoder,
    GstClockTime * clock_time, GstClockTime * duration)
{
  if (encoder->timestamp_queue_head == encoder->timestamp_queue_tail) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Timestamp queue empty or after overflow."), ("FIX CODE"));
    *clock_time = GST_CLOCK_TIME_NONE;
    *duration = GST_CLOCK_TIME_NONE;
    return;
  }

  *clock_time = encoder->timestamp_queue[encoder->timestamp_queue_head];
  *duration = encoder->timestamp_queue_dur[encoder->timestamp_queue_head];
  encoder->timestamp_queue_head++;
  encoder->timestamp_queue_head %= encoder->timestamp_queue_size;
}

/*
 * Returns: Buffer with the stream headers.
 */
static GstBuffer *
gst_x264_enc_header_buf (GstX264Enc * encoder)
{
  GstBuffer *buf;
  x264_nal_t *nal;
  int i_nal;
  int header_return;
  int i_size;
  int nal_size, i_data;
  guint8 *buffer, *sps;
  gulong buffer_size;

  /* Create avcC header. */

  header_return = x264_encoder_headers (encoder->x264enc, &nal, &i_nal);
  if (header_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Encode x264 header failed."),
        ("x264_encoder_headers return code=%d", header_return));
    return NULL;
  }

  /* This should be enough for a header buffer. */
  buffer_size = 100000;
  buffer = g_malloc (buffer_size);

  if (nal[1].i_type != 7 || nal[2].i_type != 8) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Unexpected x264 header."),
        ("TODO avcC header construction for high profiles needs some work"));
    return NULL;
  }

  sps = nal[1].p_payload;

  buffer[0] = 1;                /* AVC Decoder Configuration Record ver. 1 */
  buffer[1] = sps[0];           /* profile_idc                             */
  buffer[2] = sps[1];           /* profile_compability                     */
  buffer[3] = sps[2];           /* level_idc                               */
  buffer[4] = 0xfc | (4 - 1);   /* nal_length_size_minus1                  */

  i_size = 5;

  buffer[i_size++] = 0xe0 | 1;  /* number of SPSs */

  i_data = buffer_size - i_size - 2;
  nal_size = x264_nal_encode (buffer + i_size + 2, &i_data, 0, &nal[1]);
  buffer[i_size + 0] = (nal_size >> 8) & 0xff;
  buffer[i_size + 1] = nal_size & 0xff;
  i_size += nal_size + 2;

  buffer[i_size++] = 1;         /* number of PPSs */

  i_data = buffer_size - i_size - 2;
  nal_size = x264_nal_encode (buffer + i_size + 2, &i_data, 0, &nal[2]);
  buffer[i_size + 0] = (nal_size >> 8) & 0xff;
  buffer[i_size + 1] = nal_size & 0xff;
  i_size += nal_size + 2;

  buf = gst_buffer_new_and_alloc (i_size);

  memcpy (GST_BUFFER_DATA (buf), buffer, i_size);

  free (buffer);

  return buf;
}

/* gst_x264_enc_set_src_caps
 * Returns: TRUE on success.
 */
static gboolean
gst_x264_enc_set_src_caps (GstX264Enc * encoder, GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GValue header = { 0, };
  GstBuffer *buf;
  GstCaps *outcaps;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);
  structure = gst_structure_copy (structure);
  gst_structure_set_name (structure, "video/x-h264");

  if (!encoder->byte_stream) {
    buf = gst_x264_enc_header_buf (encoder);
    if (buf != NULL) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
      g_value_init (&header, GST_TYPE_BUFFER);
      gst_value_set_buffer (&header, buf);
      gst_structure_set_value (structure, "codec_data", &header);
      g_value_unset (&header);
    }
  }

  outcaps = gst_caps_new_full (structure, NULL);
  res = gst_pad_set_caps (pad, outcaps);
  gst_caps_unref (outcaps);

  return res;
}

static gboolean
gst_x264_enc_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstX264Enc *encoder = GST_X264_ENC (GST_OBJECT_PARENT (pad));
  GstStructure *structure;
  const GValue *framerate, *par;
  gint width, height;
  gint framerate_num, framerate_den;
  gint par_num, par_den;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &height))
    return FALSE;
  if (!(framerate = gst_structure_get_value (structure, "framerate")))
    return FALSE;
  framerate_num = gst_value_get_fraction_numerator (framerate);
  framerate_den = gst_value_get_fraction_denominator (framerate);
  if (!(par = gst_structure_get_value (structure, "pixel-aspect-ratio"))) {
    par_num = 1;
    par_den = 1;
  } else {
    par_num = gst_value_get_fraction_numerator (par);
    par_den = gst_value_get_fraction_denominator (par);
  }

  /* If the encoder is initialized, do not
     reinitialize it again if not necessary */
  if (encoder->x264enc) {
    gboolean caps_same = width == encoder->width
        && height == encoder->height
        && framerate_num == encoder->framerate_num
        && framerate_den == encoder->framerate_den
        && par_num == encoder->par_num && par_den == encoder->par_den;

    GstFlowReturn flow_ret;
    int i_nal;

    if (caps_same)
      /* Negotiating the same caps */
      return TRUE;

    do {
      flow_ret = gst_x264_enc_encode_frame (encoder, NULL, &i_nal);
    } while (flow_ret == GST_FLOW_OK && i_nal > 0);

    encoder->sps_id++;
  }
  encoder->width = width;
  encoder->height = height;
  encoder->framerate_num = framerate_num;
  encoder->framerate_den = framerate_den;
  encoder->par_num = par_num;
  encoder->par_den = par_den;

  /* FIXME: is this correct for odd widths/heights? (tpm) */
  encoder->stride = encoder->width;
  encoder->luma_plane_size = encoder->width * encoder->height;

  if (!gst_x264_enc_init_encoder (encoder))
    return FALSE;

  if (!gst_x264_enc_set_src_caps (encoder, encoder->srcpad, caps)) {
    gst_x264_enc_close_encoder (encoder);
    return FALSE;
  }

  return TRUE;
}

static void
gst_x264_enc_base_init (gpointer g_class)
{
  static GstElementDetails plugin_details = {
    "x264enc",
    "Codec/Encoder/Video",
    "H264 Encoder",
    "Josef Zlomek <josef.zlomek@itonis.tv>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_x264_enc_class_init (GstX264EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_x264_enc_set_property;
  gobject_class->get_property = gst_x264_enc_get_property;
  gobject_class->dispose = gst_x264_enc_dispose;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_x264_enc_change_state);

  g_object_class_install_property (gobject_class, ARG_THREADS,
      g_param_spec_uint ("threads", "Threads",
          "Number of threads used by the codec", 1, 4, ARG_THREADS_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PASS,
      g_param_spec_uint ("pass", "Pass",
          "Pass of multipass encoding (0=single pass; 1=first pass, 2=middle pass, 3=last pass)",
          0, 3, ARG_PASS_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_STATS_FILE,
      g_param_spec_string ("stats_file", "Stats File",
          "Filename for multipass statistics", ARG_STATS_FILE_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BYTE_STREAM,
      g_param_spec_boolean ("byte_stream", "Byte Stream",
          "Generate byte stream format of NALU", ARG_BYTE_STREAM_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          100 * 1024, ARG_BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_VBV_BUF_CAPACITY,
      g_param_spec_uint ("vbv_buf_capacity", "VBV buffer capacity",
          "Size of the VBV buffer in milliseconds", 300,
          10000, ARG_VBV_BUF_CAPACITY_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ME,
      g_param_spec_enum ("me", "Motion Estimation",
          "Integer pixel motion estimation method", GST_X264_ENC_ME_TYPE,
          ARG_ME_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SUBME,
      g_param_spec_uint ("subme", "Subpixel Motion Estimation",
          "Subpixel motion estimation and partition decision quality: 1=fast, 6=best",
          1, 6, ARG_SUBME_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_ANALYSE,
      g_param_spec_flags ("analyse", "Analyse", "Partitions to consider",
          GST_X264_ENC_ANALYSE_TYPE, ARG_ANALYSE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_DCT8x8,
      g_param_spec_boolean ("dct8x8", "DCT8x8",
          "Adaptive spatial transform size", ARG_DCT8x8_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_REF,
      g_param_spec_uint ("ref", "Reference Frames",
          "Number of reference frames", 1, 12, ARG_REF_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BFRAMES,
      g_param_spec_uint ("bframes", "B-Frames",
          "Number of B-frames between I and P", 0, 4, ARG_BFRAMES_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_B_PYRAMID,
      g_param_spec_boolean ("b_pyramid", "B-Pyramid",
          "Keep some B-frames as references", ARG_B_PYRAMID_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_WEIGHTB,
      g_param_spec_boolean ("weightb", "Weighted B-Frames",
          "Weighted prediction for B-frames", ARG_WEIGHTB_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SPS_ID,
      g_param_spec_uint ("sps_id", "SPS ID",
          "SPS and PPS ID number", 0, 31, ARG_SPS_ID_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_TRELLIS,
      g_param_spec_boolean ("trellis", "Trellis quantization",
          "Enable trellis searched quantization", ARG_TRELLIS_DEFAULT,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_KEYINT_MAX,
      g_param_spec_uint ("key_int_max", "Key-frame maximal interval",
          "Maximal distance between two key-frames (0 for automatic)", 0,
          G_MAXINT, ARG_KEYINT_MAX_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CABAC,
      g_param_spec_boolean ("cabac", "Use CABAC",
          "Enable CABAC entropy coding", ARG_CABAC_DEFAULT, G_PARAM_READWRITE));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_x264_enc_init (GstX264Enc * encoder, GstX264EncClass * klass)
{
  encoder->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (encoder->sinkpad,
      GST_DEBUG_FUNCPTR (gst_x264_enc_sink_set_caps));
  gst_pad_set_event_function (encoder->sinkpad,
      GST_DEBUG_FUNCPTR (gst_x264_enc_sink_event));
  gst_pad_set_chain_function (encoder->sinkpad,
      GST_DEBUG_FUNCPTR (gst_x264_enc_chain));
  gst_element_add_pad (GST_ELEMENT (encoder), encoder->sinkpad);

  encoder->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (encoder->srcpad);
  gst_element_add_pad (GST_ELEMENT (encoder), encoder->srcpad);

  /* initialize internals */
  encoder->x264enc = NULL;

  encoder->width = 16;
  encoder->height = 16;

  encoder->threads = ARG_THREADS_DEFAULT;
  encoder->pass = ARG_PASS_DEFAULT;
  encoder->stats_file = g_strdup (ARG_STATS_FILE_DEFAULT);
  encoder->byte_stream = ARG_BYTE_STREAM_DEFAULT;
  encoder->bitrate = ARG_BITRATE_DEFAULT;
  encoder->vbv_buf_capacity = ARG_VBV_BUF_CAPACITY_DEFAULT;
  encoder->me = ARG_ME_DEFAULT;
  encoder->subme = ARG_SUBME_DEFAULT;
  encoder->analyse = ARG_ANALYSE_DEFAULT;
  encoder->dct8x8 = ARG_DCT8x8_DEFAULT;
  encoder->ref = ARG_REF_DEFAULT;
  encoder->bframes = ARG_BFRAMES_DEFAULT;
  encoder->b_pyramid = ARG_B_PYRAMID_DEFAULT;
  encoder->weightb = ARG_WEIGHTB_DEFAULT;
  encoder->sps_id = ARG_SPS_ID_DEFAULT;
  encoder->trellis = ARG_TRELLIS_DEFAULT;
  encoder->keyint_max = ARG_KEYINT_MAX_DEFAULT;
  encoder->cabac = ARG_CABAC_DEFAULT;

  encoder->last_timestamp = GST_CLOCK_TIME_NONE;
  gst_x264_enc_timestamp_queue_init (encoder);

  encoder->buffer_size = 1040000;
  encoder->buffer = g_malloc (encoder->buffer_size);

  x264_param_default (&encoder->x264param);
}

/*
 * gst_x264_enc_init_encoder
 * @encoder:  Encoder which should be initialized.
 *
 * Initialize x264 encoder.
 *
 */
static gboolean
gst_x264_enc_init_encoder (GstX264Enc * encoder)
{
  /* make sure that the encoder is closed */
  gst_x264_enc_close_encoder (encoder);

  /* set up encoder parameters */
  encoder->x264param.i_threads = encoder->threads;
  encoder->x264param.i_fps_num = encoder->framerate_num;
  encoder->x264param.i_fps_den = encoder->framerate_den;
  encoder->x264param.i_width = encoder->width;
  encoder->x264param.i_height = encoder->height;
  if (encoder->par_den > 0) {
    encoder->x264param.vui.i_sar_width = encoder->par_num;
    encoder->x264param.vui.i_sar_height = encoder->par_den;
  }
  encoder->x264param.i_keyint_max = encoder->keyint_max ? encoder->keyint_max :
      (2 * encoder->framerate_num / encoder->framerate_den);
  encoder->x264param.b_cabac = encoder->cabac;
  encoder->x264param.b_aud = 1;
  encoder->x264param.i_sps_id = encoder->sps_id;
  if ((((encoder->height == 576) && ((encoder->width == 720)
                  || (encoder->width == 704) || (encoder->width == 352)))
          || ((encoder->height == 288) && (encoder->width == 352)))
      && (encoder->framerate_den == 1) && (encoder->framerate_num == 25)) {
    encoder->x264param.vui.i_vidformat = 1;     /* PAL */
  } else if ((((encoder->height == 480) && ((encoder->width == 720)
                  || (encoder->width == 704) || (encoder->width == 352)))
          || ((encoder->height == 240) && (encoder->width == 352)))
      && (encoder->framerate_den == 1001) && ((encoder->framerate_num == 30000)
          || (encoder->framerate_num == 24000))) {
    encoder->x264param.vui.i_vidformat = 2;     /* NTSC */
  } else
    encoder->x264param.vui.i_vidformat = 5;     /* unspecified */
  encoder->x264param.analyse.i_trellis = encoder->trellis ? 1 : 0;
  encoder->x264param.analyse.b_psnr = 0;
  /*encoder->x264param.analyse.b_ssim = 0; */
  encoder->x264param.analyse.i_me_method = encoder->me;
  encoder->x264param.analyse.i_subpel_refine = encoder->subme;
  encoder->x264param.analyse.inter = encoder->analyse;
  encoder->x264param.analyse.b_transform_8x8 = encoder->dct8x8;
  encoder->x264param.analyse.b_weighted_bipred = encoder->weightb;
  /*encoder->x264param.analyse.i_noise_reduction = 600; */
  encoder->x264param.i_frame_reference = encoder->ref;
  encoder->x264param.i_bframe = encoder->bframes;
  encoder->x264param.b_bframe_pyramid = encoder->b_pyramid;
  encoder->x264param.b_bframe_adaptive = 0;
  encoder->x264param.b_deblocking_filter = 1;
  encoder->x264param.i_deblocking_filter_alphac0 = 0;
  encoder->x264param.i_deblocking_filter_beta = 0;
#ifdef X264_RC_ABR
  encoder->x264param.rc.i_rc_method = X264_RC_ABR;
#endif
  encoder->x264param.rc.i_bitrate = encoder->bitrate;
  encoder->x264param.rc.i_vbv_max_bitrate = encoder->bitrate;
  encoder->x264param.rc.i_vbv_buffer_size
      = encoder->x264param.rc.i_vbv_max_bitrate
      * encoder->vbv_buf_capacity / 1000;

  switch (encoder->pass) {
    case 0:
      encoder->x264param.rc.b_stat_read = 0;
      encoder->x264param.rc.b_stat_write = 0;
      break;
    case 1:
      /* Turbo mode parameters. */
      encoder->x264param.i_frame_reference = (encoder->ref + 1) >> 1;
      encoder->x264param.analyse.i_subpel_refine =
          CLAMP (encoder->subme - 1, 1, 3);
      encoder->x264param.analyse.inter &= ~X264_ANALYSE_PSUB8x8;
      encoder->x264param.analyse.inter &= ~X264_ANALYSE_BSUB16x16;
      encoder->x264param.analyse.i_trellis = 0;

      encoder->x264param.rc.b_stat_read = 0;
      encoder->x264param.rc.b_stat_write = 1;
      break;
    case 2:
      encoder->x264param.rc.b_stat_read = 1;
      encoder->x264param.rc.b_stat_write = 1;
      break;
    case 3:
      encoder->x264param.rc.b_stat_read = 1;
      encoder->x264param.rc.b_stat_write = 0;
      break;
  }
  encoder->x264param.rc.psz_stat_in = encoder->stats_file;
  encoder->x264param.rc.psz_stat_out = encoder->stats_file;

  encoder->x264enc = x264_encoder_open (&encoder->x264param);
  if (!encoder->x264enc) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Can not initialize x264 encoder."), (""));
    return FALSE;
  }

  return TRUE;
}

/* gst_x264_enc_close_encoder
 * @encoder:  Encoder which should close.
 *
 * Close x264 encoder.
 */
static void
gst_x264_enc_close_encoder (GstX264Enc * encoder)
{
  if (encoder->x264enc != NULL) {
    x264_encoder_close (encoder->x264enc);
    encoder->x264enc = NULL;
  }
}

static void
gst_x264_enc_dispose (GObject * object)
{
  GstX264Enc *encoder = GST_X264_ENC (object);

  g_free (encoder->stats_file);
  encoder->stats_file = NULL;
  g_free (encoder->buffer);
  encoder->buffer = NULL;

  gst_x264_enc_timestamp_queue_free (encoder);
  gst_x264_enc_close_encoder (encoder);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_x264_enc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstX264Enc *encoder;
  GstFlowReturn flow_ret;
  int i_nal;

  encoder = GST_X264_ENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* send the rest NAL units */
      do {
        flow_ret = gst_x264_enc_encode_frame (encoder, NULL, &i_nal);
      } while (flow_ret == GST_FLOW_OK && i_nal > 0);

      /* send EOS */
      if (flow_ret == GST_FLOW_OK) {
        ret = gst_pad_push_event (encoder->srcpad, event);
      } else {
        ret = FALSE;
      }
      break;
    default:
      ret = gst_pad_push_event (encoder->srcpad, event);
      break;
  }
  gst_object_unref (encoder);
  return ret;
}

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_x264_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstX264Enc *encoder = GST_X264_ENC (GST_OBJECT_PARENT (pad));
  GstFlowReturn ret;
  x264_picture_t pic_in;
  int i_nal;

  if (G_UNLIKELY (encoder->x264enc == NULL))
    goto not_inited;

  /* create x264_picture_t from the buffer */
  /* mostly taken from mplayer (file ve_x264.c) */
  /* FIXME: this looks wrong for odd widths/heights (tpm) */
  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < encoder->luma_plane_size * 6 / 4))
    goto wrong_buffer_size;

  /* ignore duplicated packets */
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf))) {
    if (GST_CLOCK_TIME_IS_VALID (encoder->last_timestamp)) {
      GstClockTimeDiff diff =
          GST_BUFFER_TIMESTAMP (buf) - encoder->last_timestamp;
      if (diff <= 0) {
        GST_ELEMENT_WARNING (encoder, STREAM, ENCODE,
            ("Duplicated packet in input, dropping"),
            ("Time difference was -%" GST_TIME_FORMAT, GST_TIME_ARGS (-diff)));
        gst_buffer_unref (buf);
        return GST_FLOW_OK;
      }
    }
    encoder->last_timestamp = GST_BUFFER_TIMESTAMP (buf);
  }

  /* remember the timestamp and duration */
  gst_x264_enc_timestamp_queue_put (encoder, GST_BUFFER_TIMESTAMP (buf),
      GST_BUFFER_DURATION (buf));

  memset (&pic_in, 0, sizeof (x264_picture_t));

  pic_in.img.i_csp = X264_CSP_I420;
  pic_in.img.i_plane = 3;

  /* FIXME: again, this looks wrong for odd widths/heights (tpm) */
  pic_in.img.plane[0] = (guint8 *) (GST_BUFFER_DATA (buf));
  pic_in.img.i_stride[0] = encoder->stride;

  pic_in.img.plane[1] = pic_in.img.plane[0]
      + encoder->luma_plane_size;
  pic_in.img.i_stride[1] = encoder->stride / 2;

  pic_in.img.plane[2] = pic_in.img.plane[1]
      + encoder->luma_plane_size / 4;
  pic_in.img.i_stride[2] = encoder->stride / 2;

  pic_in.img.plane[3] = NULL;
  pic_in.img.i_stride[3] = 0;

  pic_in.i_type = X264_TYPE_AUTO;
  pic_in.i_pts = GST_BUFFER_TIMESTAMP (buf);

  ret = gst_x264_enc_encode_frame (encoder, &pic_in, &i_nal);
  gst_buffer_unref (buf);
  return ret;

/* ERRORS */
not_inited:
  {
    GST_ELEMENT_ERROR (encoder, CORE, NEGOTIATION, (NULL),
        ("Got buffer before pads were fully negotiated"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
wrong_buffer_size:
  {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Encode x264 frame failed."),
        ("Wrong buffer size %d (should be %d)",
            GST_BUFFER_SIZE (buf), encoder->luma_plane_size * 6 / 4));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_x264_enc_encode_frame (GstX264Enc * encoder, x264_picture_t * pic_in,
    int *i_nal)
{
  GstBuffer *out_buf = NULL;
  x264_picture_t pic_out;
  x264_nal_t *nal;
  int i_size;
  int nal_size;
  int encoder_return;
  gint i;
  GstFlowReturn ret;
  GstClockTime timestamp;
  GstClockTime duration;

  encoder_return = x264_encoder_encode (encoder->x264enc,
      &nal, i_nal, pic_in, &pic_out);

  if (encoder_return < 0) {
    GST_ELEMENT_ERROR (encoder, STREAM, ENCODE,
        ("Encode x264 frame failed."),
        ("x264_encoder_encode return code=%d", encoder_return));
    return GST_FLOW_ERROR;
  }

  if (!*i_nal) {
    return GST_FLOW_OK;
  }

  i_size = 0;
  for (i = 0; i < *i_nal; i++) {
    int i_data = encoder->buffer_size - i_size - 4;

    if (i_data < encoder->buffer_size / 2) {
      encoder->buffer_size *= 2;
      encoder->buffer = g_realloc (encoder->buffer, encoder->buffer_size);
      i_data = encoder->buffer_size - i_size;
    }

    nal_size =
        x264_nal_encode (encoder->buffer + i_size + 4, &i_data, 0, &nal[i]);
    if (encoder->byte_stream) {
      encoder->buffer[i_size + 0] = 0;
      encoder->buffer[i_size + 1] = 0;
      encoder->buffer[i_size + 2] = 0;
      encoder->buffer[i_size + 3] = 1;
    } else {
      encoder->buffer[i_size + 0] = (nal_size >> 24) & 0xff;
      encoder->buffer[i_size + 1] = (nal_size >> 16) & 0xff;
      encoder->buffer[i_size + 2] = (nal_size >> 8) & 0xff;
      encoder->buffer[i_size + 3] = nal_size & 0xff;
    }

    i_size += nal_size + 4;
  }

  ret = gst_pad_alloc_buffer (encoder->srcpad, GST_BUFFER_OFFSET_NONE,
      i_size, GST_PAD_CAPS (encoder->srcpad), &out_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  memcpy (GST_BUFFER_DATA (out_buf), encoder->buffer, i_size);
  GST_BUFFER_SIZE (out_buf) = i_size;

  gst_x264_enc_timestamp_queue_get (encoder, &timestamp, &duration);

  /* PTS */
  GST_BUFFER_TIMESTAMP (out_buf) = pic_out.i_pts;
  if (encoder->bframes) {
    /* When using B-frames, the frames will be reordered.
       Make PTS start one frame after DTS. */
    GST_BUFFER_TIMESTAMP (out_buf)
        += GST_SECOND * encoder->framerate_den / encoder->framerate_num;
  }

  GST_BUFFER_DURATION (out_buf) = duration;

  if (pic_out.i_type == X264_TYPE_IDR) {
    GST_BUFFER_FLAG_UNSET (out_buf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (out_buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  return gst_pad_push (encoder->srcpad, out_buf);
}

static GstStateChangeReturn
gst_x264_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstX264Enc *encoder = GST_X264_ENC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto out;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_x264_enc_close_encoder (encoder);
      break;
    default:
      break;
  }

out:
  return ret;
}

static void
gst_x264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstX264Enc *encoder;

  encoder = GST_X264_ENC (object);

  /* FIXME: should probably do locking or check state */
  switch (prop_id) {
    case ARG_THREADS:
      encoder->threads = g_value_get_uint (value);
      break;
    case ARG_PASS:
      encoder->pass = g_value_get_uint (value);
      break;
    case ARG_STATS_FILE:
      if (encoder->stats_file)
        g_free (encoder->stats_file);
      encoder->stats_file = g_value_dup_string (value);
      break;
    case ARG_BYTE_STREAM:
      encoder->byte_stream = g_value_get_boolean (value);
      break;
    case ARG_BITRATE:
      encoder->bitrate = g_value_get_uint (value);
      break;
    case ARG_VBV_BUF_CAPACITY:
      encoder->vbv_buf_capacity = g_value_get_uint (value);
      break;
    case ARG_ME:
      encoder->me = g_value_get_enum (value);
      break;
    case ARG_SUBME:
      encoder->subme = g_value_get_uint (value);
      break;
    case ARG_ANALYSE:
      encoder->analyse = g_value_get_flags (value);
      break;
    case ARG_DCT8x8:
      encoder->dct8x8 = g_value_get_boolean (value);
      break;
    case ARG_REF:
      encoder->ref = g_value_get_uint (value);
      break;
    case ARG_BFRAMES:
      encoder->bframes = g_value_get_uint (value);
      gst_x264_enc_timestamp_queue_free (encoder);
      gst_x264_enc_timestamp_queue_init (encoder);
      break;
    case ARG_B_PYRAMID:
      encoder->b_pyramid = g_value_get_boolean (value);
      break;
    case ARG_WEIGHTB:
      encoder->weightb = g_value_get_boolean (value);
      break;
    case ARG_SPS_ID:
      encoder->sps_id = g_value_get_uint (value);
      break;
    case ARG_TRELLIS:
      encoder->trellis = g_value_get_boolean (value);
      break;
    case ARG_KEYINT_MAX:
      encoder->keyint_max = g_value_get_uint (value);
      break;
    case ARG_CABAC:
      encoder->cabac = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_x264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstX264Enc *encoder;

  encoder = GST_X264_ENC (object);

  /* FIXME: should probably do locking or check state */
  switch (prop_id) {
    case ARG_THREADS:
      g_value_set_uint (value, encoder->threads);
      break;
    case ARG_PASS:
      g_value_set_uint (value, encoder->pass);
      break;
    case ARG_STATS_FILE:
      g_value_set_string (value, encoder->stats_file);
      break;
    case ARG_BYTE_STREAM:
      g_value_set_boolean (value, encoder->byte_stream);
      break;
    case ARG_BITRATE:
      g_value_set_uint (value, encoder->bitrate);
      break;
    case ARG_VBV_BUF_CAPACITY:
      g_value_set_uint (value, encoder->vbv_buf_capacity);
      break;
    case ARG_ME:
      g_value_set_enum (value, encoder->me);
      break;
    case ARG_SUBME:
      g_value_set_uint (value, encoder->subme);
      break;
    case ARG_ANALYSE:
      g_value_set_flags (value, encoder->analyse);
      break;
    case ARG_DCT8x8:
      g_value_set_boolean (value, encoder->dct8x8);
      break;
    case ARG_REF:
      g_value_set_uint (value, encoder->ref);
      break;
    case ARG_BFRAMES:
      g_value_set_uint (value, encoder->bframes);
      break;
    case ARG_B_PYRAMID:
      g_value_set_boolean (value, encoder->b_pyramid);
      break;
    case ARG_WEIGHTB:
      g_value_set_boolean (value, encoder->weightb);
      break;
    case ARG_SPS_ID:
      g_value_set_uint (value, encoder->sps_id);
      break;
    case ARG_TRELLIS:
      g_value_set_boolean (value, encoder->trellis);
      break;
    case ARG_KEYINT_MAX:
      g_value_set_uint (value, encoder->keyint_max);
      break;
    case ARG_CABAC:
      g_value_set_boolean (value, encoder->cabac);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "x264enc",
      GST_RANK_NONE, GST_TYPE_X264_ENC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "x264",
    "libx264-based H264 plugins",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
