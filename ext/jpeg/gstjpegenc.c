/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * SECTION:element-jpegenc
 *
 * Encodes jpeg images.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc num-buffers=50 ! video/x-raw-yuv, framerate='(fraction)'5/1 ! jpegenc ! avimux ! filesink location=mjpeg.avi
 * ]| a pipeline to mux 5 JPEG frames per second into a 10 sec. long motion jpeg
 * avi.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstjpegenc.h"
#include "gstjpeg.h"
#include <gst/video/video.h>

/* experimental */
/* setting smoothig seems to have no effect in libjepeg
#define ENABLE_SMOOTHING 1
*/

GST_DEBUG_CATEGORY_STATIC (jpegenc_debug);
#define GST_CAT_DEFAULT jpegenc_debug

#define JPEG_DEFAULT_QUALITY 85
#define JPEG_DEFAULT_SMOOTHING 0
#define JPEG_DEFAULT_IDCT_METHOD	JDCT_FASTEST

/* JpegEnc signals and args */
enum
{
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_SMOOTHING,
  PROP_IDCT_METHOD
};

static void gst_jpegenc_reset (GstJpegEnc * enc);
static void gst_jpegenc_base_init (gpointer g_class);
static void gst_jpegenc_class_init (GstJpegEnc * klass);
static void gst_jpegenc_init (GstJpegEnc * jpegenc);
static void gst_jpegenc_finalize (GObject * object);

static GstFlowReturn gst_jpegenc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_jpegenc_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_jpegenc_getcaps (GstPad * pad);

static void gst_jpegenc_resync (GstJpegEnc * jpegenc);
static void gst_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_jpegenc_change_state (GstElement * element,
    GstStateChange transition);


static GstElementClass *parent_class = NULL;
static guint gst_jpegenc_signals[LAST_SIGNAL] = { 0 };

GType
gst_jpegenc_get_type (void)
{
  static GType jpegenc_type = 0;

  if (!jpegenc_type) {
    static const GTypeInfo jpegenc_info = {
      sizeof (GstJpegEnc),
      (GBaseInitFunc) gst_jpegenc_base_init,
      NULL,
      (GClassInitFunc) gst_jpegenc_class_init,
      NULL,
      NULL,
      sizeof (GstJpegEnc),
      0,
      (GInstanceInitFunc) gst_jpegenc_init,
    };

    jpegenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstJpegEnc", &jpegenc_info,
        0);
  }
  return jpegenc_type;
}

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{ I420, YV12, YUY2, UYVY, Y41B, Y42B, YVYU, Y444 }") "; "
        GST_VIDEO_CAPS_RGB "; " GST_VIDEO_CAPS_BGR "; "
        GST_VIDEO_CAPS_RGBx "; " GST_VIDEO_CAPS_xRGB "; "
        GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_xBGR "; "
        GST_VIDEO_CAPS_GRAY8)
    );
/* *INDENT-ON* */

static GstStaticPadTemplate gst_jpegenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 16, 65535 ], "
        "height = (int) [ 16, 65535 ], " "framerate = (fraction) [ 0/1, MAX ]")
    );

static void
gst_jpegenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpegenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpegenc_src_pad_template));
  gst_element_class_set_details_simple (element_class, "JPEG image encoder",
      "Codec/Encoder/Image",
      "Encode images in JPEG format", "Wim Taymans <wim.taymans@tvd.be>");
}

static void
gst_jpegenc_class_init (GstJpegEnc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gst_jpegenc_signals[FRAME_ENCODED] =
      g_signal_new ("frame-encoded", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstJpegEncClass, frame_encoded), NULL,
      NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->set_property = gst_jpegenc_set_property;
  gobject_class->get_property = gst_jpegenc_get_property;


  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Quality of encoding",
          0, 100, JPEG_DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#ifdef ENABLE_SMOOTHING
  /* disabled, since it doesn't seem to work */
  g_object_class_install_property (gobject_class, PROP_SMOOTHING,
      g_param_spec_int ("smoothing", "Smoothing", "Smoothing factor",
          0, 100, JPEG_DEFAULT_SMOOTHING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_IDCT_METHOD,
      g_param_spec_enum ("idct-method", "IDCT Method",
          "The IDCT algorithm to use", GST_TYPE_IDCT_METHOD,
          JPEG_DEFAULT_IDCT_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_jpegenc_change_state;

  gobject_class->finalize = gst_jpegenc_finalize;

  GST_DEBUG_CATEGORY_INIT (jpegenc_debug, "jpegenc", 0,
      "JPEG encoding element");
}

static void
gst_jpegenc_init_destination (j_compress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegenc_chain: init_destination");
}

static boolean
gst_jpegenc_flush_destination (j_compress_ptr cinfo)
{
  GstBuffer *overflow_buffer;
  guint32 old_buffer_size;
  GstJpegEnc *jpegenc = (GstJpegEnc *) (cinfo->client_data);
  GST_DEBUG_OBJECT (jpegenc,
      "gst_jpegenc_chain: flush_destination: buffer too small");

  /* Our output buffer wasn't big enough.
   * Make a new buffer that's twice the size, */
  old_buffer_size = GST_BUFFER_SIZE (jpegenc->output_buffer);
  gst_pad_alloc_buffer_and_set_caps (jpegenc->srcpad,
      GST_BUFFER_OFFSET_NONE, old_buffer_size * 2,
      GST_PAD_CAPS (jpegenc->srcpad), &overflow_buffer);
  memcpy (GST_BUFFER_DATA (overflow_buffer),
      GST_BUFFER_DATA (jpegenc->output_buffer), old_buffer_size);

  gst_buffer_copy_metadata (overflow_buffer, jpegenc->output_buffer,
      GST_BUFFER_COPY_TIMESTAMPS);

  /* drop it into place, */
  gst_buffer_unref (jpegenc->output_buffer);
  jpegenc->output_buffer = overflow_buffer;

  /* and last, update libjpeg on where to work. */
  jpegenc->jdest.next_output_byte =
      GST_BUFFER_DATA (jpegenc->output_buffer) + old_buffer_size;
  jpegenc->jdest.free_in_buffer =
      GST_BUFFER_SIZE (jpegenc->output_buffer) - old_buffer_size;

  return TRUE;
}

static void
gst_jpegenc_term_destination (j_compress_ptr cinfo)
{
  GstJpegEnc *jpegenc = (GstJpegEnc *) (cinfo->client_data);
  GST_DEBUG_OBJECT (jpegenc, "gst_jpegenc_chain: term_source");

  /* Trim the buffer size and push it. */
  GST_BUFFER_SIZE (jpegenc->output_buffer) =
      GST_BUFFER_SIZE (jpegenc->output_buffer) - jpegenc->jdest.free_in_buffer;

  g_signal_emit (G_OBJECT (jpegenc), gst_jpegenc_signals[FRAME_ENCODED], 0);

  jpegenc->last_ret = gst_pad_push (jpegenc->srcpad, jpegenc->output_buffer);
  jpegenc->output_buffer = NULL;
}

static void
gst_jpegenc_init (GstJpegEnc * jpegenc)
{
  /* create the sink and src pads */
  jpegenc->sinkpad =
      gst_pad_new_from_static_template (&gst_jpegenc_sink_pad_template, "sink");
  gst_pad_set_chain_function (jpegenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpegenc_chain));
  gst_pad_set_getcaps_function (jpegenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpegenc_getcaps));
  gst_pad_set_setcaps_function (jpegenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpegenc_setcaps));
  gst_element_add_pad (GST_ELEMENT (jpegenc), jpegenc->sinkpad);

  jpegenc->srcpad =
      gst_pad_new_from_static_template (&gst_jpegenc_src_pad_template, "src");
  gst_pad_use_fixed_caps (jpegenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (jpegenc), jpegenc->srcpad);

  /* reset the initial video state */
  jpegenc->width = -1;
  jpegenc->height = -1;

  /* setup jpeglib */
  memset (&jpegenc->cinfo, 0, sizeof (jpegenc->cinfo));
  memset (&jpegenc->jerr, 0, sizeof (jpegenc->jerr));
  jpegenc->cinfo.err = jpeg_std_error (&jpegenc->jerr);
  jpeg_create_compress (&jpegenc->cinfo);

  jpegenc->jdest.init_destination = gst_jpegenc_init_destination;
  jpegenc->jdest.empty_output_buffer = gst_jpegenc_flush_destination;
  jpegenc->jdest.term_destination = gst_jpegenc_term_destination;
  jpegenc->cinfo.dest = &jpegenc->jdest;
  jpegenc->cinfo.client_data = jpegenc;

  /* init properties */
  jpegenc->quality = JPEG_DEFAULT_QUALITY;
  jpegenc->smoothing = JPEG_DEFAULT_SMOOTHING;
  jpegenc->idct_method = JPEG_DEFAULT_IDCT_METHOD;

  gst_jpegenc_reset (jpegenc);
}

static void
gst_jpegenc_reset (GstJpegEnc * enc)
{
  gint i, j;

  g_free (enc->line[0]);
  g_free (enc->line[1]);
  g_free (enc->line[2]);
  enc->line[0] = NULL;
  enc->line[1] = NULL;
  enc->line[2] = NULL;
  for (i = 0; i < 3; i++) {
    for (j = 0; j < 4 * DCTSIZE; j++) {
      g_free (enc->row[i][j]);
      enc->row[i][j] = NULL;
    }
  }

  enc->width = -1;
  enc->height = -1;
  enc->format = GST_VIDEO_FORMAT_UNKNOWN;
  enc->fps_den = enc->par_den = 0;
  enc->height = enc->width = 0;
}

static void
gst_jpegenc_finalize (GObject * object)
{
  GstJpegEnc *filter = GST_JPEGENC (object);

  jpeg_destroy_compress (&filter->cinfo);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_jpegenc_getcaps (GstPad * pad)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (gst_pad_get_parent (pad));
  GstCaps *caps, *othercaps;
  const GstCaps *templ;
  gint i, j;
  GstStructure *structure = NULL;

  /* we want to proxy properties like width, height and framerate from the
     other end of the element */

  othercaps = gst_pad_peer_get_caps_reffed (jpegenc->srcpad);
  if (othercaps == NULL ||
      gst_caps_is_empty (othercaps) || gst_caps_is_any (othercaps)) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
    goto done;
  }

  caps = gst_caps_new_empty ();
  templ = gst_pad_get_pad_template_caps (pad);

  for (i = 0; i < gst_caps_get_size (templ); i++) {
    /* pick fields from peer caps */
    for (j = 0; j < gst_caps_get_size (othercaps); j++) {
      GstStructure *s = gst_caps_get_structure (othercaps, j);
      const GValue *val;

      structure = gst_structure_copy (gst_caps_get_structure (templ, i));
      if ((val = gst_structure_get_value (s, "width")))
        gst_structure_set_value (structure, "width", val);
      if ((val = gst_structure_get_value (s, "height")))
        gst_structure_set_value (structure, "height", val);
      if ((val = gst_structure_get_value (s, "framerate")))
        gst_structure_set_value (structure, "framerate", val);

      gst_caps_merge_structure (caps, structure);
    }
  }

done:

  gst_caps_replace (&othercaps, NULL);
  gst_object_unref (jpegenc);

  return caps;
}

static gboolean
gst_jpegenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstJpegEnc *enc = GST_JPEGENC (gst_pad_get_parent (pad));
  GstVideoFormat format;
  gint width, height;
  gint fps_num, fps_den;
  gint par_num, par_den;
  gint i;
  GstCaps *othercaps;
  gboolean ret;

  /* get info from caps */
  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    goto refuse_caps;
  /* optional; pass along if present */
  fps_num = fps_den = -1;
  par_num = par_den = -1;
  gst_video_parse_caps_framerate (caps, &fps_num, &fps_den);
  gst_video_parse_caps_pixel_aspect_ratio (caps, &par_num, &par_den);

  if (width == enc->width && height == enc->height && enc->format == format
      && fps_num == enc->fps_num && fps_den == enc->fps_den
      && par_num == enc->par_num && par_den == enc->par_den)
    return TRUE;

  /* store input description */
  enc->format = format;
  enc->width = width;
  enc->height = height;
  enc->fps_num = fps_num;
  enc->fps_den = fps_den;
  enc->par_num = par_num;
  enc->par_den = par_den;

  /* prepare a cached image description  */
  enc->channels = 3 + (gst_video_format_has_alpha (format) ? 1 : 0);
  /* ... but any alpha is disregarded in encoding */
  if (gst_video_format_is_gray (format))
    enc->channels = 1;
  else
    enc->channels = 3;
  enc->h_max_samp = 0;
  enc->v_max_samp = 0;
  for (i = 0; i < enc->channels; ++i) {
    enc->cwidth[i] = gst_video_format_get_component_width (format, i, width);
    enc->cheight[i] = gst_video_format_get_component_height (format, i, height);
    enc->offset[i] = gst_video_format_get_component_offset (format, i, width,
        height);
    enc->stride[i] = gst_video_format_get_row_stride (format, i, width);
    enc->inc[i] = gst_video_format_get_pixel_stride (format, i);
    enc->h_samp[i] = GST_ROUND_UP_4 (width) / enc->cwidth[i];
    enc->h_max_samp = MAX (enc->h_max_samp, enc->h_samp[i]);
    enc->v_samp[i] = GST_ROUND_UP_4 (height) / enc->cheight[i];
    enc->v_max_samp = MAX (enc->v_max_samp, enc->v_samp[i]);
  }
  /* samp should only be 1, 2 or 4 */
  g_assert (enc->h_max_samp <= 4);
  g_assert (enc->v_max_samp <= 4);
  /* now invert */
  /* maximum is invariant, as one of the components should have samp 1 */
  for (i = 0; i < enc->channels; ++i) {
    enc->h_samp[i] = enc->h_max_samp / enc->h_samp[i];
    enc->v_samp[i] = enc->v_max_samp / enc->v_samp[i];
  }
  enc->planar = (enc->inc[0] == 1 && enc->inc[1] == 1 && enc->inc[2] == 1);

  othercaps = gst_caps_copy (gst_pad_get_pad_template_caps (enc->srcpad));
  gst_caps_set_simple (othercaps,
      "width", G_TYPE_INT, enc->width, "height", G_TYPE_INT, enc->height, NULL);
  if (enc->fps_den > 0)
    gst_caps_set_simple (othercaps,
        "framerate", GST_TYPE_FRACTION, enc->fps_num, enc->fps_den, NULL);
  if (enc->par_den > 0)
    gst_caps_set_simple (othercaps,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, enc->par_num, enc->par_den,
        NULL);

  ret = gst_pad_set_caps (enc->srcpad, othercaps);
  gst_caps_unref (othercaps);

  if (ret)
    gst_jpegenc_resync (enc);

  gst_object_unref (enc);

  return ret;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (enc, "refused caps %" GST_PTR_FORMAT, caps);
    gst_object_unref (enc);
    return FALSE;
  }
}

static void
gst_jpegenc_resync (GstJpegEnc * jpegenc)
{
  gint width, height;
  gint i, j;

  GST_DEBUG_OBJECT (jpegenc, "resync");

  jpegenc->cinfo.image_width = width = jpegenc->width;
  jpegenc->cinfo.image_height = height = jpegenc->height;
  jpegenc->cinfo.input_components = jpegenc->channels;

  GST_DEBUG_OBJECT (jpegenc, "width %d, height %d", width, height);
  GST_DEBUG_OBJECT (jpegenc, "format %d", jpegenc->format);

  if (gst_video_format_is_rgb (jpegenc->format)) {
    GST_DEBUG_OBJECT (jpegenc, "RGB");
    jpegenc->cinfo.in_color_space = JCS_RGB;
  } else if (gst_video_format_is_gray (jpegenc->format)) {
    GST_DEBUG_OBJECT (jpegenc, "gray");
    jpegenc->cinfo.in_color_space = JCS_GRAYSCALE;
  } else {
    GST_DEBUG_OBJECT (jpegenc, "YUV");
    jpegenc->cinfo.in_color_space = JCS_YCbCr;
  }

  /* input buffer size as max output */
  jpegenc->bufsize = gst_video_format_get_size (jpegenc->format, width, height);
  jpeg_set_defaults (&jpegenc->cinfo);
  jpegenc->cinfo.raw_data_in = TRUE;
  /* duh, libjpeg maps RGB to YUV ... and don't expect some conversion */
  if (jpegenc->cinfo.in_color_space == JCS_RGB)
    jpeg_set_colorspace (&jpegenc->cinfo, JCS_RGB);

  GST_DEBUG_OBJECT (jpegenc, "h_max_samp=%d, v_max_samp=%d",
      jpegenc->h_max_samp, jpegenc->v_max_samp);
  /* image dimension info */
  for (i = 0; i < jpegenc->channels; i++) {
    GST_DEBUG_OBJECT (jpegenc, "comp %i: h_samp=%d, v_samp=%d", i,
        jpegenc->h_samp[i], jpegenc->v_samp[i]);
    jpegenc->cinfo.comp_info[i].h_samp_factor = jpegenc->h_samp[i];
    jpegenc->cinfo.comp_info[i].v_samp_factor = jpegenc->v_samp[i];
    g_free (jpegenc->line[i]);
    jpegenc->line[i] = g_new (guchar *, jpegenc->v_max_samp * DCTSIZE);
    if (!jpegenc->planar) {
      for (j = 0; j < jpegenc->v_max_samp * DCTSIZE; j++) {
        g_free (jpegenc->row[i][j]);
        jpegenc->row[i][j] = g_malloc (width);
        jpegenc->line[i][j] = jpegenc->row[i][j];
      }
    }
  }

  /* guard against a potential error in gst_jpegenc_term_destination
     which occurs iff bufsize % 4 < free_space_remaining */
  jpegenc->bufsize = GST_ROUND_UP_4 (jpegenc->bufsize);

  jpeg_suppress_tables (&jpegenc->cinfo, TRUE);

  GST_DEBUG_OBJECT (jpegenc, "resync done");
}

static GstFlowReturn
gst_jpegenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstJpegEnc *jpegenc;
  guchar *data;
  gulong size;
  guint height;
  guchar *base[3], *end[3];
  gint i, j, k;

  jpegenc = GST_JPEGENC (GST_OBJECT_PARENT (pad));

  if (G_UNLIKELY (jpegenc->width <= 0 || jpegenc->height <= 0))
    goto not_negotiated;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_LOG_OBJECT (jpegenc, "got buffer of %lu bytes", size);

  ret =
      gst_pad_alloc_buffer_and_set_caps (jpegenc->srcpad,
      GST_BUFFER_OFFSET_NONE, jpegenc->bufsize, GST_PAD_CAPS (jpegenc->srcpad),
      &jpegenc->output_buffer);

  if (ret != GST_FLOW_OK)
    goto done;

  gst_buffer_copy_metadata (jpegenc->output_buffer, buf,
      GST_BUFFER_COPY_TIMESTAMPS);

  height = jpegenc->height;

  for (i = 0; i < jpegenc->channels; i++) {
    base[i] = data + jpegenc->offset[i];
    end[i] = base[i] + jpegenc->cheight[i] * jpegenc->stride[i];
  }

  jpegenc->jdest.next_output_byte = GST_BUFFER_DATA (jpegenc->output_buffer);
  jpegenc->jdest.free_in_buffer = GST_BUFFER_SIZE (jpegenc->output_buffer);

  /* prepare for raw input */
#if JPEG_LIB_VERSION >= 70
  jpegenc->cinfo.do_fancy_downsampling = FALSE;
#endif
  jpegenc->cinfo.smoothing_factor = jpegenc->smoothing;
  jpegenc->cinfo.dct_method = jpegenc->idct_method;
  jpeg_set_quality (&jpegenc->cinfo, jpegenc->quality, TRUE);
  jpeg_start_compress (&jpegenc->cinfo, TRUE);

  GST_LOG_OBJECT (jpegenc, "compressing");

  if (jpegenc->planar) {
    for (i = 0; i < height; i += jpegenc->v_max_samp * DCTSIZE) {
      for (k = 0; k < jpegenc->channels; k++) {
        for (j = 0; j < jpegenc->v_samp[k] * DCTSIZE; j++) {
          jpegenc->line[k][j] = base[k];
          if (base[k] + jpegenc->stride[k] < end[k])
            base[k] += jpegenc->stride[k];
        }
      }
      jpeg_write_raw_data (&jpegenc->cinfo, jpegenc->line,
          jpegenc->v_max_samp * DCTSIZE);
    }
  } else {
    for (i = 0; i < height; i += jpegenc->v_max_samp * DCTSIZE) {
      for (k = 0; k < jpegenc->channels; k++) {
        for (j = 0; j < jpegenc->v_samp[k] * DCTSIZE; j++) {
          guchar *src, *dst;
          gint l;

          /* ouch, copy line */
          src = base[k];
          dst = jpegenc->line[k][j];
          for (l = jpegenc->cwidth[k]; l > 0; l--) {
            *dst = *src;
            src += jpegenc->inc[k];
            dst++;
          }
          if (base[k] + jpegenc->stride[k] < end[k])
            base[k] += jpegenc->stride[k];
        }
      }
      jpeg_write_raw_data (&jpegenc->cinfo, jpegenc->line,
          jpegenc->v_max_samp * DCTSIZE);
    }
  }

  /* This will ensure that gst_jpegenc_term_destination is called; we push
     the final output buffer from there */
  jpeg_finish_compress (&jpegenc->cinfo);
  GST_LOG_OBJECT (jpegenc, "compressing done");

done:
  gst_buffer_unref (buf);

  return ret;

/* ERRORS */
not_negotiated:
  {
    GST_WARNING_OBJECT (jpegenc, "no input format set (no caps on buffer)");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static void
gst_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (object);

  GST_OBJECT_LOCK (jpegenc);

  switch (prop_id) {
    case PROP_QUALITY:
      jpegenc->quality = g_value_get_int (value);
      break;
#ifdef ENABLE_SMOOTHING
    case PROP_SMOOTHING:
      jpegenc->smoothing = g_value_get_int (value);
      break;
#endif
    case PROP_IDCT_METHOD:
      jpegenc->idct_method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (jpegenc);
}

static void
gst_jpegenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (object);

  GST_OBJECT_LOCK (jpegenc);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_int (value, jpegenc->quality);
      break;
#ifdef ENABLE_SMOOTHING
    case PROP_SMOOTHING:
      g_value_set_int (value, jpegenc->smoothing);
      break;
#endif
    case PROP_IDCT_METHOD:
      g_value_set_enum (value, jpegenc->idct_method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (jpegenc);
}

static GstStateChangeReturn
gst_jpegenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstJpegEnc *filter = GST_JPEGENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_DEBUG_OBJECT (element, "setting line buffers");
      filter->line[0] = NULL;
      filter->line[1] = NULL;
      filter->line[2] = NULL;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_jpegenc_reset (filter);
      break;
    default:
      break;
  }

  return ret;
}
