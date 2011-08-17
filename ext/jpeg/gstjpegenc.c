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
static void gst_jpegenc_finalize (GObject * object);

static GstFlowReturn gst_jpegenc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_jpegenc_sink_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_jpegenc_getcaps (GstPad * pad, GstCaps * filter);

static void gst_jpegenc_resync (GstJpegEnc * jpegenc);
static void gst_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_jpegenc_change_state (GstElement * element,
    GstStateChange transition);

#define gst_jpegenc_parent_class parent_class
G_DEFINE_TYPE (GstJpegEnc, gst_jpegenc, GST_TYPE_ELEMENT);

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ I420, YV12, YUY2, UYVY, Y41B, Y42B, YVYU, Y444, "
         "RGB, BGR, RGBx, xRGB, BGRx, xBGR, GRAY8 }"))
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
gst_jpegenc_class_init (GstJpegEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_jpegenc_finalize;
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

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_jpegenc_sink_pad_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_jpegenc_src_pad_template));
  gst_element_class_set_details_simple (gstelement_class, "JPEG image encoder",
      "Codec/Encoder/Image",
      "Encode images in JPEG format", "Wim Taymans <wim.taymans@tvd.be>");

  GST_DEBUG_CATEGORY_INIT (jpegenc_debug, "jpegenc", 0,
      "JPEG encoding element");
}

static void
ensure_memory (GstJpegEnc * jpegenc)
{
  GstMemory *new_memory;
  gsize old_size, desired_size, new_size;
  guint8 *new_data;

  old_size = jpegenc->output_size;
  if (old_size == 0)
    desired_size = jpegenc->bufsize;
  else
    desired_size = old_size * 2;

  /* Our output memory wasn't big enough.
   * Make a new memory that's twice the size, */
  new_memory = gst_allocator_alloc (NULL, desired_size, 3);
  new_data = gst_memory_map (new_memory, &new_size, NULL, GST_MAP_READWRITE);

  /* copy previous data if any */
  if (jpegenc->output_mem) {
    memcpy (new_data, jpegenc->output_data, old_size);
    gst_memory_unmap (jpegenc->output_mem, jpegenc->output_data,
        jpegenc->output_size);
    gst_memory_unref (jpegenc->output_mem);
  }

  /* drop it into place, */
  jpegenc->output_mem = new_memory;
  jpegenc->output_data = new_data;
  jpegenc->output_size = new_size;

  /* and last, update libjpeg on where to work. */
  jpegenc->jdest.next_output_byte = new_data + old_size;
  jpegenc->jdest.free_in_buffer = new_size - old_size;
}

static void
gst_jpegenc_init_destination (j_compress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegenc_chain: init_destination");
}

static boolean
gst_jpegenc_flush_destination (j_compress_ptr cinfo)
{
  GstJpegEnc *jpegenc = (GstJpegEnc *) (cinfo->client_data);

  GST_DEBUG_OBJECT (jpegenc,
      "gst_jpegenc_chain: flush_destination: buffer too small");

  ensure_memory (jpegenc);

  return TRUE;
}

static void
gst_jpegenc_term_destination (j_compress_ptr cinfo)
{
  GstJpegEnc *jpegenc = (GstJpegEnc *) (cinfo->client_data);
  GST_DEBUG_OBJECT (jpegenc, "gst_jpegenc_chain: term_source");

  /* Trim the buffer size. we will push it in the chain function */
  gst_memory_unmap (jpegenc->output_mem, jpegenc->output_data,
      jpegenc->output_size - jpegenc->jdest.free_in_buffer);
  jpegenc->output_data = NULL;
  jpegenc->output_size = 0;
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
  gst_pad_set_event_function (jpegenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpegenc_sink_event));
  gst_element_add_pad (GST_ELEMENT (jpegenc), jpegenc->sinkpad);

  jpegenc->srcpad =
      gst_pad_new_from_static_template (&gst_jpegenc_src_pad_template, "src");
  gst_pad_use_fixed_caps (jpegenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (jpegenc), jpegenc->srcpad);

  /* reset the initial video state */
  gst_video_info_init (&jpegenc->info);

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

  gst_video_info_init (&enc->info);
}

static void
gst_jpegenc_finalize (GObject * object)
{
  GstJpegEnc *filter = GST_JPEGENC (object);

  jpeg_destroy_compress (&filter->cinfo);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_jpegenc_getcaps (GstPad * pad, GstCaps * filter)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (gst_pad_get_parent (pad));
  GstCaps *caps, *othercaps;
  const GstCaps *templ;
  gint i, j;
  GstStructure *structure = NULL;

  /* we want to proxy properties like width, height and framerate from the
     other end of the element */

  othercaps = gst_pad_peer_get_caps (jpegenc->srcpad, filter);
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
gst_jpegenc_setcaps (GstJpegEnc * enc, GstCaps * caps)
{
  GstVideoInfo info;
  gint i;
  GstCaps *othercaps;
  gboolean ret;
  const GstVideoFormatInfo *vinfo;

  /* get info from caps */
  if (!gst_video_info_from_caps (&info, caps))
    goto refuse_caps;

  /* store input description */
  enc->info = info;

  vinfo = info.finfo;

  /* prepare a cached image description  */
  enc->channels = 3 + (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (vinfo) ? 1 : 0);
  /* ... but any alpha is disregarded in encoding */
  if (GST_VIDEO_FORMAT_INFO_IS_GRAY (vinfo))
    enc->channels = 1;
  else
    enc->channels = 3;

  enc->h_max_samp = 0;
  enc->v_max_samp = 0;
  for (i = 0; i < enc->channels; ++i) {
    enc->cwidth[i] = GST_VIDEO_INFO_COMP_WIDTH (&info, i);
    enc->cheight[i] = GST_VIDEO_INFO_COMP_HEIGHT (&info, i);
    enc->inc[i] = GST_VIDEO_INFO_COMP_PSTRIDE (&info, i);

    enc->h_samp[i] = GST_ROUND_UP_4 (info.width) / enc->cwidth[i];
    enc->h_max_samp = MAX (enc->h_max_samp, enc->h_samp[i]);
    enc->v_samp[i] = GST_ROUND_UP_4 (info.height) / enc->cheight[i];
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
      "width", G_TYPE_INT, info.width, "height", G_TYPE_INT, info.height, NULL);
  if (info.fps_d > 0)
    gst_caps_set_simple (othercaps,
        "framerate", GST_TYPE_FRACTION, info.fps_n, info.fps_d, NULL);
  if (info.par_d > 0)
    gst_caps_set_simple (othercaps,
        "pixel-aspect-ratio", GST_TYPE_FRACTION, info.par_n, info.par_d, NULL);

  ret = gst_pad_set_caps (enc->srcpad, othercaps);
  gst_caps_unref (othercaps);

  if (ret)
    gst_jpegenc_resync (enc);

  return ret;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (enc, "refused caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static gboolean
gst_jpegenc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstJpegEnc *enc = GST_JPEGENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_jpegenc_setcaps (enc, caps);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (enc);

  return res;
}

static void
gst_jpegenc_resync (GstJpegEnc * jpegenc)
{
  gint width, height;
  gint i, j;
  const GstVideoFormatInfo *finfo;

  GST_DEBUG_OBJECT (jpegenc, "resync");

  finfo = jpegenc->info.finfo;

  jpegenc->cinfo.image_width = width = GST_VIDEO_INFO_WIDTH (&jpegenc->info);
  jpegenc->cinfo.image_height = height = GST_VIDEO_INFO_HEIGHT (&jpegenc->info);
  jpegenc->cinfo.input_components = jpegenc->channels;

  GST_DEBUG_OBJECT (jpegenc, "width %d, height %d", width, height);
  GST_DEBUG_OBJECT (jpegenc, "format %d",
      GST_VIDEO_INFO_FORMAT (&jpegenc->info));

  if (GST_VIDEO_FORMAT_INFO_IS_RGB (finfo)) {
    GST_DEBUG_OBJECT (jpegenc, "RGB");
    jpegenc->cinfo.in_color_space = JCS_RGB;
  } else if (GST_VIDEO_FORMAT_INFO_IS_GRAY (finfo)) {
    GST_DEBUG_OBJECT (jpegenc, "gray");
    jpegenc->cinfo.in_color_space = JCS_GRAYSCALE;
  } else {
    GST_DEBUG_OBJECT (jpegenc, "YUV");
    jpegenc->cinfo.in_color_space = JCS_YCbCr;
  }

  /* input buffer size as max output */
  jpegenc->bufsize = GST_VIDEO_INFO_SIZE (&jpegenc->info);
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
  guint height, width;
  guchar *base[3], *end[3];
  guint stride[3];
  gint i, j, k;
  GstBuffer *outbuf;
  GstVideoFrame frame;

  jpegenc = GST_JPEGENC (GST_OBJECT_PARENT (pad));

  if (G_UNLIKELY (GST_VIDEO_INFO_FORMAT (&jpegenc->info) ==
          GST_VIDEO_FORMAT_UNKNOWN))
    goto not_negotiated;

  if (!gst_video_frame_map (&frame, &jpegenc->info, buf, GST_MAP_READ))
    goto invalid_frame;

  height = GST_VIDEO_FRAME_HEIGHT (&frame);
  width = GST_VIDEO_FRAME_WIDTH (&frame);

  GST_LOG_OBJECT (jpegenc, "got buffer of %lu bytes",
      gst_buffer_get_size (buf));

  for (i = 0; i < jpegenc->channels; i++) {
    base[i] = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
    stride[i] = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);
    end[i] = base[i] + GST_VIDEO_FRAME_COMP_HEIGHT (&frame, i) * stride[i];
  }

  jpegenc->output_mem = gst_allocator_alloc (NULL, jpegenc->bufsize, 3);
  jpegenc->output_data =
      gst_memory_map (jpegenc->output_mem, &jpegenc->output_size, NULL,
      GST_MAP_READWRITE);

  jpegenc->jdest.next_output_byte = jpegenc->output_data;
  jpegenc->jdest.free_in_buffer = jpegenc->output_size;

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
          if (base[k] + stride[k] < end[k])
            base[k] += stride[k];
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
          if (base[k] + stride[k] < end[k])
            base[k] += stride[k];
        }
      }
      jpeg_write_raw_data (&jpegenc->cinfo, jpegenc->line,
          jpegenc->v_max_samp * DCTSIZE);
    }
  }

  /* This will ensure that gst_jpegenc_term_destination is called */
  jpeg_finish_compress (&jpegenc->cinfo);
  GST_LOG_OBJECT (jpegenc, "compressing done");

  outbuf = gst_buffer_new ();
  gst_buffer_copy_into (outbuf, buf, GST_BUFFER_COPY_METADATA, 0, -1);
  gst_buffer_take_memory (outbuf, -1, jpegenc->output_mem);
  jpegenc->output_mem = NULL;

  ret = gst_pad_push (jpegenc->srcpad, outbuf);

  gst_video_frame_unmap (&frame);
  gst_buffer_unref (buf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_WARNING_OBJECT (jpegenc, "no input format set (no caps on buffer)");
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_frame:
  {
    GST_WARNING_OBJECT (jpegenc, "invalid frame received");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
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
