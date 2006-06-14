/* GStreamer Wavpack encoder plugin
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackdec.c: Wavpack audio encoder
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

/*
 * TODO: - add multichannel handling. channel_mask is:
 *                  front left
 *                  front right
 *                  center
 *                  LFE
 *                  back left
 *                  back right
 *                  front left center
 *                  front right center
 *                  back left
 *                  back center
 *                  side left
 *                  side right
 *                  ...
 *        - add 32 bit float mode. CONFIG_FLOAT_DATA
 */

#include <string.h>
#include <gst/gst.h>
#include <glib/gprintf.h>

#include <wavpack/wavpack.h>
#include "gstwavpackenc.h"
#include "gstwavpackcommon.h"
#include "md5.h"

static GstFlowReturn gst_wavpack_enc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_wavpack_enc_sink_set_caps (GstPad * pad, GstCaps * caps);
static int gst_wavpack_enc_push_block (void *id, void *data, int32_t count);
static gboolean gst_wavpack_enc_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_wavpack_enc_change_state (GstElement * element,
    GstStateChange transition);
static void gst_wavpack_enc_dispose (GObject * object);
static void gst_wavpack_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wavpack_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  ARG_0,
  ARG_MODE,
  ARG_BITRATE,
  ARG_BITSPERSAMPLE,
  ARG_CORRECTION_MODE,
  ARG_MD5,
  ARG_EXTRA_PROCESSING,
  ARG_JOINT_STEREO_MODE,
};

GST_DEBUG_CATEGORY_STATIC (gst_wavpack_enc_debug);
#define GST_CAT_DEFAULT gst_wavpack_enc_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "endianness = (int) LITTLE_ENDIAN, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ]," "signed = (boolean) TRUE;"
        "audio/x-raw-int, "
        "width = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) LITTLE_ENDIAN, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ]," "signed = (boolean) TRUE;"
        "audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) LITTLE_ENDIAN, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ]," "signed = (boolean) TRUE;"
        "audio/x-raw-int, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "endianness = (int) LITTLE_ENDIAN, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ]," "signed = (boolean) TRUE")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) { 8, 16, 24, 32 }, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) FALSE")
    );

static GstStaticPadTemplate wvcsrc_factory = GST_STATIC_PAD_TEMPLATE ("wvcsrc",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-wavpack-correction, " "framed = (boolean) FALSE")
    );

#define DEFAULT_MODE 1
#define GST_TYPE_WAVPACK_ENC_MODE (gst_wavpack_enc_mode_get_type ())
static GType
gst_wavpack_enc_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {0, "Fast Compression", "0"},
      {1, "Default", "1"},
      {2, "High Compression", "2"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstWavpackEncMode", values);
  }
  return qtype;
}

#define DEFAULT_CORRECTION_MODE 0
#define GST_TYPE_WAVPACK_ENC_CORRECTION_MODE (gst_wavpack_enc_correction_mode_get_type ())
static GType
gst_wavpack_enc_correction_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {0, "Create no correction file (default)", "0"},
      {1, "Create correction file", "1"},
      {2, "Create optimized correction file", "2"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstWavpackEncCorrectionMode", values);
  }
  return qtype;
}

#define DEFAULT_JS_MODE 0
#define GST_TYPE_WAVPACK_ENC_JOINT_STEREO_MODE (gst_wavpack_enc_joint_stereo_mode_get_type ())
static GType
gst_wavpack_enc_joint_stereo_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {0, "auto (default)", "0"},
      {1, "left/right", "1"},
      {2, "mid/side", "2"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstWavpackEncJSMode", values);
  }
  return qtype;
}

GST_BOILERPLATE (GstWavpackEnc, gst_wavpack_enc, GstElement, GST_TYPE_ELEMENT);

static void
gst_wavpack_enc_base_init (gpointer klass)
{
  static GstElementDetails element_details = {
    "Wavpack audio encoder",
    "Codec/Encoder/Audio",
    "Encodes audio with the Wavpack lossless/lossy audio codec",
    "Sebastian Dröge <slomo@circular-chaos.org>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* add pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&wvcsrc_factory));

  /* set element details */
  gst_element_class_set_details (element_class, &element_details);
}


static void
gst_wavpack_enc_class_init (GstWavpackEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  /* set state change handler */
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_change_state);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_wavpack_enc_dispose);

  /* set property handlers */
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_get_property);

  /* install all properties */
  g_object_class_install_property (gobject_class, ARG_MODE,
      g_param_spec_enum ("mode", "Encoding mode",
          "Speed versus compression tradeoff.",
          GST_TYPE_WAVPACK_ENC_MODE, DEFAULT_MODE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_double ("bitrate", "Bitrate",
          "Try to encode with this average bitrate (bits/sec). "
          "This enables lossy encoding! A value smaller than 24000.0 disables this.",
          0.0, 9600000.0, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BITSPERSAMPLE,
      g_param_spec_double ("bits-per-sample", "Bits per sample",
          "Try to encode with this amount of bits per sample. "
          "This enables lossy encoding! A value smaller than 2.0 disables this.",
          0.0, 24.0, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_CORRECTION_MODE,
      g_param_spec_enum ("correction_mode", "Correction file mode",
          "Use this mode for correction file creation. Only works in lossy mode!",
          GST_TYPE_WAVPACK_ENC_CORRECTION_MODE, DEFAULT_CORRECTION_MODE,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MD5,
      g_param_spec_boolean ("md5", "MD5",
          "Store MD5 hash of raw samples within the file.", FALSE,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_EXTRA_PROCESSING,
      g_param_spec_boolean ("extra_processing", "Extra processing",
          "Extra encode processing.", FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_JOINT_STEREO_MODE,
      g_param_spec_enum ("joint_stereo_mode", "Joint-Stereo mode",
          "Use this joint-stereo mode.", GST_TYPE_WAVPACK_ENC_JOINT_STEREO_MODE,
          DEFAULT_JS_MODE, G_PARAM_READWRITE));
}

static void
gst_wavpack_enc_init (GstWavpackEnc * wavpack_enc, GstWavpackEncClass * gclass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (wavpack_enc);

  /* setup sink pad, add handlers */
  wavpack_enc->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_setcaps_function (wavpack_enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_sink_set_caps));
  gst_pad_set_chain_function (wavpack_enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_chain));
  gst_pad_set_event_function (wavpack_enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_sink_event));
  gst_element_add_pad (GST_ELEMENT (wavpack_enc),
      GST_DEBUG_FUNCPTR (wavpack_enc->sinkpad));

  /* setup src pad */
  wavpack_enc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (wavpack_enc),
      GST_DEBUG_FUNCPTR (wavpack_enc->srcpad));

  /* initialize object attributes */
  wavpack_enc->wp_config = NULL;
  wavpack_enc->wp_context = NULL;
  wavpack_enc->first_block = NULL;
  wavpack_enc->first_block_size = 0;
  wavpack_enc->md5_context = NULL;
  wavpack_enc->samplerate = 0;
  wavpack_enc->width = 0;
  wavpack_enc->channels = 0;

  wavpack_enc->wv_id = (write_id *) g_malloc0 (sizeof (write_id));
  wavpack_enc->wv_id->correction = FALSE;
  wavpack_enc->wv_id->wavpack_enc = wavpack_enc;
  wavpack_enc->wvc_id = (write_id *) g_malloc0 (sizeof (write_id));
  wavpack_enc->wvc_id->correction = TRUE;
  wavpack_enc->wvc_id->wavpack_enc = wavpack_enc;

  /* set default values of params */
  wavpack_enc->mode = 1;
  wavpack_enc->bitrate = 0.0;
  wavpack_enc->correction_mode = 0;
  wavpack_enc->md5 = FALSE;
  wavpack_enc->extra_processing = FALSE;
  wavpack_enc->joint_stereo_mode = 0;
}

static void
gst_wavpack_enc_dispose (GObject * object)
{
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (object);

  /* free the blockout helpers */
  g_free (wavpack_enc->wv_id);
  g_free (wavpack_enc->wvc_id);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_wavpack_enc_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  int depth = 0;

  /* check caps and put relevant parts into our object attributes */
  if ((!gst_structure_get_int (structure, "channels", &wavpack_enc->channels))
      || (!gst_structure_get_int (structure, "rate", &wavpack_enc->samplerate))
      || (!gst_structure_get_int (structure, "width", &wavpack_enc->width))
      || (!(gst_structure_get_int (structure, "depth", &depth))
          || depth != wavpack_enc->width)) {
    GST_ELEMENT_ERROR (wavpack_enc, LIBRARY, INIT, (NULL),
        ("got invalid caps: %", GST_PTR_FORMAT, caps));
    gst_object_unref (wavpack_enc);
    return FALSE;
  }

  /* set fixed src pad caps now that we know what we will get */
  caps = gst_caps_new_simple ("audio/x-wavpack",
      "channels", G_TYPE_INT, wavpack_enc->channels,
      "rate", G_TYPE_INT, wavpack_enc->samplerate,
      "width", G_TYPE_INT, wavpack_enc->width,
      "framed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (!gst_pad_set_caps (wavpack_enc->srcpad, caps)) {
    GST_ELEMENT_ERROR (wavpack_enc, LIBRARY, INIT, (NULL),
        ("setting caps failed: %", GST_PTR_FORMAT, caps));
    gst_caps_unref (caps);
    gst_object_unref (wavpack_enc);
    return FALSE;
  }
  gst_pad_use_fixed_caps (wavpack_enc->srcpad);

  gst_caps_unref (caps);
  gst_object_unref (wavpack_enc);
  return TRUE;
}

static void
gst_wavpack_enc_set_wp_config (GstWavpackEnc * wavpack_enc)
{
  wavpack_enc->wp_config = (WavpackConfig *) g_malloc0 (sizeof (WavpackConfig));
  /* set general stream informations in the WavpackConfig */
  wavpack_enc->wp_config->bytes_per_sample = (wavpack_enc->width + 7) >> 3;
  wavpack_enc->wp_config->bits_per_sample = wavpack_enc->width;
  wavpack_enc->wp_config->num_channels = wavpack_enc->channels;

  /* TODO: handle more than 2 channels correctly! */
  if (wavpack_enc->channels == 1) {
    wavpack_enc->wp_config->channel_mask = 0x4;
  } else if (wavpack_enc->channels == 2) {
    wavpack_enc->wp_config->channel_mask = 0x2 | 0x1;
  }
  wavpack_enc->wp_config->sample_rate = wavpack_enc->samplerate;

  /*
   * Set parameters in WavpackConfig
   */

  /* Encoding mode */
  switch (wavpack_enc->mode) {
    case 0:
      wavpack_enc->wp_config->flags |= CONFIG_FAST_FLAG;
      break;
    case 1:                    /* default */
      break;
    case 2:
      wavpack_enc->wp_config->flags |= CONFIG_HIGH_FLAG;
      break;
  }

  /* Bitrate, enables lossy mode */
  if (wavpack_enc->bitrate >= 2.0) {
    wavpack_enc->wp_config->flags |= CONFIG_HYBRID_FLAG;
    if (wavpack_enc->bitrate >= 24000.0) {
      wavpack_enc->wp_config->bitrate = wavpack_enc->bitrate / 1000.0;
      wavpack_enc->wp_config->flags |= CONFIG_BITRATE_KBPS;
    } else {
      wavpack_enc->wp_config->bitrate = wavpack_enc->bitrate;
    }
  }

  /* Correction Mode, only in lossy mode */
  if (wavpack_enc->wp_config->flags & CONFIG_HYBRID_FLAG) {
    if (wavpack_enc->correction_mode > 0) {
      wavpack_enc->wvcsrcpad =
          gst_pad_new_from_template (gst_element_class_get_pad_template
          (GST_ELEMENT_GET_CLASS (wavpack_enc), "wvcsrc"), "wvcsrc");

      /* try to add correction src pad, don't set correction mode on failure */
      if (gst_element_add_pad (GST_ELEMENT (wavpack_enc),
              GST_DEBUG_FUNCPTR (wavpack_enc->wvcsrcpad))) {
        GstCaps *caps = gst_caps_new_simple ("audio/x-wavpack-correction",
            "framed", G_TYPE_BOOLEAN, FALSE, NULL);

        gst_element_no_more_pads (GST_ELEMENT (wavpack_enc));

        if (!gst_pad_set_caps (wavpack_enc->wvcsrcpad, caps)) {
          wavpack_enc->correction_mode = 0;
          GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, INIT, (NULL),
              ("setting correction caps failed: %", GST_PTR_FORMAT, caps));
        } else {
          gst_pad_use_fixed_caps (wavpack_enc->wvcsrcpad);
          wavpack_enc->wp_config->flags |= CONFIG_CREATE_WVC;
          if (wavpack_enc->correction_mode == 2) {
            wavpack_enc->wp_config->flags |= CONFIG_OPTIMIZE_WVC;
          }
        }
        gst_caps_unref (caps);
      } else {
        wavpack_enc->correction_mode = 0;
        GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, INIT, (NULL),
            ("add correction pad failed. no correction file will be created."));
      }
    }
  } else {
    if (wavpack_enc->correction_mode > 0) {
      wavpack_enc->correction_mode = 0;
      GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, SETTINGS, (NULL),
          ("settings correction mode only has effect if a bitrate is provided."));
    }
  }

  /* MD5, setup MD5 context */
  if ((wavpack_enc->md5) && !(wavpack_enc->md5_context)) {
    wavpack_enc->wp_config->flags |= CONFIG_MD5_CHECKSUM;
    wavpack_enc->md5_context = (MD5_CTX *) g_malloc0 (sizeof (MD5_CTX));
    MD5Init (wavpack_enc->md5_context);
  }

  /* Extra encode processing */
  if (wavpack_enc->extra_processing) {
    wavpack_enc->wp_config->flags |= CONFIG_EXTRA_MODE;
  }

  /* Joint stereo mode */
  switch (wavpack_enc->joint_stereo_mode) {
    case 0:                    /* default */
      break;
    case 1:
      wavpack_enc->wp_config->flags |= CONFIG_JOINT_OVERRIDE;
      wavpack_enc->wp_config->flags &= ~CONFIG_JOINT_STEREO;
      break;
    case 2:
      wavpack_enc->wp_config->flags |=
          (CONFIG_JOINT_OVERRIDE | CONFIG_JOINT_STEREO);
      break;
  }
}

static int32_t *
gst_wavpack_enc_format_samples (const uchar * src_data, uint32_t sample_count,
    guint width)
{
  int32_t *data = (int32_t *) g_malloc0 (sizeof (int32_t) * sample_count);

  /* put all samples into an int32_t*, no matter what
   * width we have and convert them from little endian
   * to host byte order */

  switch (width) {
      int i;

    case 8:
      for (i = 0; i < sample_count; i++)
        data[i] = (int32_t) (int8_t) src_data[i];
      break;
    case 16:
      for (i = 0; i < sample_count; i++)
        data[i] = (int32_t) src_data[2 * i]
            | ((int32_t) (int8_t) src_data[2 * i + 1] << 8);
      break;
    case 24:
      for (i = 0; i < sample_count; i++)
        data[i] = (int32_t) src_data[3 * i]
            | ((int32_t) src_data[3 * i + 1] << 8)
            | ((int32_t) (int8_t) src_data[3 * i + 2] << 16);
      break;
    case 32:
      for (i = 0; i < sample_count; i++)
        data[i] = (int32_t) src_data[4 * i]
            | ((int32_t) src_data[4 * i + 1] << 8)
            | ((int32_t) src_data[4 * i + 2] << 16)
            | ((int32_t) (int8_t) src_data[4 * i + 3] << 24);
      break;
  }

  return data;
}

static int
gst_wavpack_enc_push_block (void *id, void *data, int32_t count)
{
  write_id *wid = (write_id *) id;
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (wid->wavpack_enc);
  GstFlowReturn ret;
  GstBuffer *buffer;
  guchar *block = (guchar *) data;

  if (wid->correction == FALSE) {
    /* we got something that should be pushed to the (non-correction) src pad */

    /* try to allocate a buffer, compatible with the pad, fail otherwise */
    ret = gst_pad_alloc_buffer_and_set_caps (wavpack_enc->srcpad,
        GST_BUFFER_OFFSET_NONE, count, GST_PAD_CAPS (wavpack_enc->srcpad),
        &buffer);
    if (ret != GST_FLOW_OK) {
      wavpack_enc->srcpad_last_return = ret;
      GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, ENCODE, (NULL),
          ("Dropped one block (%d bytes) of encoded data while allocating buffer! Reason: '%s'\n",
              count, gst_flow_get_name (ret)));
      return FALSE;
    }

    g_memmove (GST_BUFFER_DATA (buffer), block, count);

    if ((block[0] == 'w') && (block[1] == 'v') && (block[2] == 'p')
        && (block[3] == 'k')) {
      /* if it's a Wavpack block set buffer timestamp and duration, etc */
      WavpackHeader wph;

      GST_DEBUG ("got %d bytes of encoded wavpack data", count);
      gst_wavpack_read_header (&wph, block);

      /* if it's the first wavpack block save it for later reference
       * i.e. sample count correction and send a NEW_SEGMENT event */
      if (wph.block_index == 0) {
        GstEvent *event = gst_event_new_new_segment (FALSE,
            1.0, GST_FORMAT_BYTES, 0, GST_BUFFER_OFFSET_NONE, 0);

        gst_pad_push_event (wavpack_enc->srcpad, event);
        wavpack_enc->first_block = g_malloc0 (count);
        g_memmove (wavpack_enc->first_block, block, count);
        wavpack_enc->first_block_size = count;
      }

      /* set buffer timestamp, duration, offset, offset_end from
       * the wavpack header */
      GST_BUFFER_TIMESTAMP (buffer) =
          gst_util_uint64_scale_int (GST_SECOND, wph.block_index,
          wavpack_enc->samplerate);
      GST_BUFFER_DURATION (buffer) =
          gst_util_uint64_scale_int (GST_SECOND, wph.block_samples,
          wavpack_enc->samplerate);
      GST_BUFFER_OFFSET (buffer) = wph.block_index;
      GST_BUFFER_OFFSET_END (buffer) = wph.block_index + wph.block_samples;
    } else {
      /* if it's something else set no timestamp and duration on the buffer */
      GST_DEBUG ("got %d bytes of unknown data", count);

      GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
    }

    /* push the buffer and forward errors */
    ret = gst_pad_push (wavpack_enc->srcpad, buffer);
    wavpack_enc->srcpad_last_return = ret;
    if (ret == GST_FLOW_OK) {
      return TRUE;
    } else {
      GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, ENCODE, (NULL),
          ("Dropped one block (%d bytes) of encoded data while pushing! Reason: '%s'\n",
              count, gst_flow_get_name (ret)));
      return FALSE;
    }
  } else if (wid->correction == TRUE) {
    /* we got something that should be pushed to the correction src pad */

    /* is the correction pad linked? */
    if (!gst_pad_is_linked (wavpack_enc->wvcsrcpad)) {
      GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, ENCODE, (NULL),
          ("Dropped one block (%d bytes) of encoded correction data because of unlinked pad",
              count));
      wavpack_enc->wvcsrcpad_last_return = GST_FLOW_NOT_LINKED;
      return FALSE;
    }

    /* try to allocate a buffer, compatible with the pad, fail otherwise */
    ret = gst_pad_alloc_buffer_and_set_caps (wavpack_enc->wvcsrcpad,
        GST_BUFFER_OFFSET_NONE, count,
        GST_PAD_CAPS (wavpack_enc->wvcsrcpad), &buffer);
    if (ret != GST_FLOW_OK) {
      wavpack_enc->wvcsrcpad_last_return = ret;
      GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, ENCODE, (NULL),
          ("Dropped one block (%d bytes) of encoded correction data while allocating buffer! Reason: '%s'\n",
              count, gst_flow_get_name (ret)));
      return FALSE;
    }

    g_memmove (GST_BUFFER_DATA (buffer), block, count);

    if ((block[0] == 'w') && (block[1] == 'v') && (block[2] == 'p')
        && (block[3] == 'k')) {
      /* if it's a Wavpack block set buffer timestamp and duration, etc */
      WavpackHeader wph;

      GST_DEBUG ("got %d bytes of encoded wavpack correction data", count);
      gst_wavpack_read_header (&wph, block);

      /* if it's the first wavpack block send a NEW_SEGMENT
       * event */
      if (wph.block_index == 0) {
        GstEvent *event = gst_event_new_new_segment (FALSE,
            1.0, GST_FORMAT_BYTES, 0, GST_BUFFER_OFFSET_NONE, 0);

        gst_pad_push_event (wavpack_enc->wvcsrcpad, event);
      }

      /* set buffer timestamp, duration, offset, offset_end from
       * the wavpack header */
      GST_BUFFER_TIMESTAMP (buffer) =
          gst_util_uint64_scale_int (GST_SECOND, wph.block_index,
          wavpack_enc->samplerate);
      GST_BUFFER_DURATION (buffer) =
          gst_util_uint64_scale_int (GST_SECOND, wph.block_samples,
          wavpack_enc->samplerate);
      GST_BUFFER_OFFSET (buffer) = wph.block_index;
      GST_BUFFER_OFFSET_END (buffer) = wph.block_index + wph.block_samples;
    } else {
      /* if it's something else set no timestamp and duration on the buffer */
      GST_DEBUG ("got %d bytes of unknown data", count);

      GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
    }

    /* push the buffer and forward errors */
    ret = gst_pad_push (wavpack_enc->wvcsrcpad, buffer);
    wavpack_enc->wvcsrcpad_last_return = ret;
    if (ret == GST_FLOW_OK)
      return TRUE;
    else {
      GST_ELEMENT_WARNING (wavpack_enc, LIBRARY, ENCODE, (NULL),
          ("Dropped one block (%d bytes) of encoded correction data while pushing! Reason: '%s'\n",
              count, gst_flow_get_name (ret)));
      return FALSE;
    }
  } else {
    /* (correction != TRUE) && (correction != FALSE), wtf? ignore this */
    g_assert_not_reached ();
    return TRUE;
  }
}

static GstFlowReturn
gst_wavpack_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (gst_pad_get_parent (pad));
  uint32_t sample_count =
      GST_BUFFER_SIZE (buf) / ((wavpack_enc->width + 7) >> 3);
  int32_t *data;
  GstFlowReturn ret;

  /* reset the last returns to GST_FLOW_OK. This is only set to something else
   * while WavpackPackSamples() or more specific gst_wavpack_enc_push_block()
   * so not valid anymore */
  wavpack_enc->srcpad_last_return = wavpack_enc->wvcsrcpad_last_return =
      GST_FLOW_OK;

  GST_DEBUG ("got %u raw samples", sample_count);

  /* check if we already have a valid WavpackContext, otherwise make one */
  if (!wavpack_enc->wp_context) {
    /* create raw context */
    wavpack_enc->wp_context =
        WavpackOpenFileOutput (gst_wavpack_enc_push_block, wavpack_enc->wv_id,
        (wavpack_enc->correction_mode > 0) ? wavpack_enc->wvc_id : NULL);
    if (!wavpack_enc->wp_context) {
      GST_ELEMENT_ERROR (wavpack_enc, LIBRARY, INIT, (NULL),
          ("error creating Wavpack context"));
      gst_object_unref (wavpack_enc);
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }

    /* set the WavpackConfig according to our parameters */
    gst_wavpack_enc_set_wp_config (wavpack_enc);

    /* set the configuration to the context now that we know everything
     * and initialize the encoder */
    if (!WavpackSetConfiguration (wavpack_enc->wp_context,
            wavpack_enc->wp_config, (uint32_t) (-1))
        || !WavpackPackInit (wavpack_enc->wp_context)) {
      GST_ELEMENT_ERROR (wavpack_enc, LIBRARY, SETTINGS, (NULL),
          ("error setting up wavpack encoding context"));
      WavpackCloseFile (wavpack_enc->wp_context);
      gst_object_unref (wavpack_enc);
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }
    GST_DEBUG ("setup of encoding context successfull");
  }

  /* if we want to append the MD5 sum to the stream update it here
   * with the current raw samples */
  if (wavpack_enc->md5) {
    MD5Update (wavpack_enc->md5_context, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
  }

  /* put all samples into an int32_t*, no matter what
   * width we have and convert them from little endian
   * to host byte order */
  data =
      gst_wavpack_enc_format_samples (GST_BUFFER_DATA (buf), sample_count,
      wavpack_enc->width);

  gst_buffer_unref (buf);

  /* encode and handle return values from encoding */
  if (WavpackPackSamples (wavpack_enc->wp_context, data,
          sample_count / wavpack_enc->channels)) {
    GST_DEBUG ("encoding samples successfull");
    ret = GST_FLOW_OK;
  } else {
    if ((wavpack_enc->srcpad_last_return == GST_FLOW_RESEND) ||
        (wavpack_enc->wvcsrcpad_last_return == GST_FLOW_RESEND)) {
      ret = GST_FLOW_RESEND;
    } else if ((wavpack_enc->srcpad_last_return == GST_FLOW_OK) ||
        (wavpack_enc->wvcsrcpad_last_return == GST_FLOW_OK)) {
      ret = GST_FLOW_OK;
    } else if ((wavpack_enc->srcpad_last_return == GST_FLOW_NOT_LINKED) &&
        (wavpack_enc->wvcsrcpad_last_return == GST_FLOW_NOT_LINKED)) {
      ret = GST_FLOW_NOT_LINKED;
    } else if ((wavpack_enc->srcpad_last_return == GST_FLOW_WRONG_STATE) &&
        (wavpack_enc->wvcsrcpad_last_return == GST_FLOW_WRONG_STATE)) {
      ret = GST_FLOW_WRONG_STATE;
    } else {
      GST_ELEMENT_ERROR (wavpack_enc, LIBRARY, ENCODE, (NULL),
          ("encoding samples failed"));
      ret = GST_FLOW_ERROR;
    }
  }

  g_free (data);
  gst_object_unref (wavpack_enc);
  return ret;
}

static void
gst_wavpack_enc_rewrite_first_block (GstWavpackEnc * wavpack_enc)
{
  GstEvent *event = gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_BYTES,
      0, GST_BUFFER_OFFSET_NONE, 0);
  gboolean ret;

  g_return_if_fail (wavpack_enc);
  g_return_if_fail (wavpack_enc->first_block);

  /* update the sample count in the first block */
  WavpackUpdateNumSamples (wavpack_enc->wp_context, wavpack_enc->first_block);

  /* try to seek to the beginning of the output */
  ret = gst_pad_push_event (wavpack_enc->srcpad, event);
  if (ret) {
    /* try to rewrite the first block */
    ret = gst_wavpack_enc_push_block (wavpack_enc->wv_id,
        wavpack_enc->first_block, wavpack_enc->first_block_size);
    if (ret) {
      GST_DEBUG ("rewriting of first block succeeded!");
    } else {
      GST_ELEMENT_WARNING (wavpack_enc, RESOURCE, WRITE, (NULL),
          ("rewriting of first block failed while pushing!"));
    }
  } else {
    GST_ELEMENT_WARNING (wavpack_enc, RESOURCE, SEEK, (NULL),
        ("rewriting of first block failed. Seeking to first block failed!"));
  }
}

static gboolean
gst_wavpack_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  GST_DEBUG ("Received %s event on sinkpad", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* Encode all remaining samples and flush them to the src pads */
      WavpackFlushSamples (wavpack_enc->wp_context);

      /* write the MD5 sum if we have to write one */
      if ((wavpack_enc->md5) && (wavpack_enc->md5_context)) {
        guchar md5_digest[16];

        MD5Final (md5_digest, wavpack_enc->md5_context);
        WavpackStoreMD5Sum (wavpack_enc->wp_context, md5_digest);
      }

      /* Try to rewrite the first frame with the correct sample number */
      if (wavpack_enc->first_block)
        gst_wavpack_enc_rewrite_first_block (wavpack_enc);

      /* close the context if not already happened */
      if (wavpack_enc->wp_context) {
        WavpackCloseFile (wavpack_enc->wp_context);
        wavpack_enc->wp_context = NULL;
      }

      ret = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
      if (wavpack_enc->wp_context) {
        GST_ELEMENT_WARNING (wavpack_enc, RESOURCE, SEEK, (NULL),
            ("got NEWSEGMENT after encoding already started"));
      }
      /* drop NEWSEGMENT events, we create our own when pushing
       * the first buffer to the pads */
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (wavpack_enc);
  return ret;
}

static GstStateChangeReturn
gst_wavpack_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* set the last returned GstFlowReturns of the two pads to GST_FLOW_OK
       * as they're only set to something else in WavpackPackSamples() or more
       * specific gst_wavpack_enc_push_block() and nothing happened there yet */
      wavpack_enc->srcpad_last_return = wavpack_enc->wvcsrcpad_last_return =
          GST_FLOW_OK;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* close and free everything stream related */
      if (wavpack_enc->wp_context) {
        WavpackCloseFile (wavpack_enc->wp_context);
        wavpack_enc->wp_context = NULL;
      }
      if (wavpack_enc->wp_config) {
        g_free (wavpack_enc->wp_config);
        wavpack_enc->wp_config = NULL;
      }
      if (wavpack_enc->first_block) {
        g_free (wavpack_enc->first_block);
        wavpack_enc->first_block = NULL;
        wavpack_enc->first_block_size = 0;
      }
      if (wavpack_enc->md5_context) {
        g_free (wavpack_enc->md5_context);
        wavpack_enc->md5_context = NULL;
      }

      /* reset the last returns to GST_FLOW_OK. This is only set to something else
       * while WavpackPackSamples() or more specific gst_wavpack_enc_push_block()
       * so not valid anymore */
      wavpack_enc->srcpad_last_return = wavpack_enc->wvcsrcpad_last_return =
          GST_FLOW_OK;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_wavpack_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (object);

  switch (prop_id) {
    case ARG_MODE:
      wavpack_enc->mode = g_value_get_enum (value);
      break;
    case ARG_BITRATE:{
      gdouble val = g_value_get_double (value);

      if ((val >= 24000.0) && (val <= 9600000.0)) {
        wavpack_enc->bitrate = val;
      } else {
        wavpack_enc->bitrate = 0.0;
      }
      break;
    }
    case ARG_BITSPERSAMPLE:{
      gdouble val = g_value_get_double (value);

      if ((val >= 2.0) && (val <= 24.0)) {
        wavpack_enc->bitrate = val;
      } else {
        wavpack_enc->bitrate = 0.0;
      }
      break;
    }
    case ARG_CORRECTION_MODE:
      wavpack_enc->correction_mode = g_value_get_enum (value);
      break;
    case ARG_MD5:
      wavpack_enc->md5 = g_value_get_boolean (value);
      break;
    case ARG_EXTRA_PROCESSING:
      wavpack_enc->extra_processing = g_value_get_boolean (value);
      break;
    case ARG_JOINT_STEREO_MODE:
      wavpack_enc->joint_stereo_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wavpack_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstWavpackEnc *wavpack_enc = GST_WAVPACK_ENC (object);

  switch (prop_id) {
    case ARG_MODE:
      g_value_set_enum (value, wavpack_enc->mode);
      break;
    case ARG_BITRATE:
      if (wavpack_enc->bitrate >= 24000.0) {
        g_value_set_double (value, wavpack_enc->bitrate);
      } else {
        g_value_set_double (value, 0.0);
      }
      break;
    case ARG_BITSPERSAMPLE:
      if (wavpack_enc->bitrate <= 24.0) {
        g_value_set_double (value, wavpack_enc->bitrate);
      } else {
        g_value_set_double (value, 0.0);
      }
      break;
    case ARG_CORRECTION_MODE:
      g_value_set_enum (value, wavpack_enc->correction_mode);
      break;
    case ARG_MD5:
      g_value_set_boolean (value, wavpack_enc->md5);
      break;
    case ARG_EXTRA_PROCESSING:
      g_value_set_boolean (value, wavpack_enc->extra_processing);
      break;
    case ARG_JOINT_STEREO_MODE:
      g_value_set_enum (value, wavpack_enc->joint_stereo_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_wavpack_enc_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "wavpackenc",
          GST_RANK_NONE, GST_TYPE_WAVPACK_ENC))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_wavpack_enc_debug, "wavpackenc", 0,
      "wavpack encoder");

  return TRUE;
}
