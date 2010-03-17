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
/*#define ENABLE_COLORSPACE_RGB 1 */

/* elementfactory information */
static const GstElementDetails gst_jpegenc_details =
GST_ELEMENT_DETAILS ("JPEG image encoder",
    "Codec/Encoder/Image",
    "Encode images in JPEG format",
    "Wim Taymans <wim.taymans@tvd.be>");

GST_DEBUG_CATEGORY_STATIC (jpegenc_debug);
#define GST_CAT_DEFAULT jpegenc_debug

#define JPEG_DEFAULT_QUALITY 85
#define JPEG_DEFAULT_SMOOTHING 0
#define JPEG_DEFAULT_IDCT_METHOD	JDCT_FASTEST

/* These macros are adapted from videotestsrc.c 
 *  and/or gst-plugins/gst/games/gstvideoimage.c */

/* I420 */
#define I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(I420_Y_ROWSTRIDE(width)))/2)

#define I420_Y_OFFSET(w,h) (0)
#define I420_U_OFFSET(w,h) (I420_Y_OFFSET(w,h)+(I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define I420_V_OFFSET(w,h) (I420_U_OFFSET(w,h)+(I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define I420_SIZE(w,h)     (I420_V_OFFSET(w,h)+(I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

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

static GstStaticPadTemplate gst_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_jpegenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) [ 0/1, MAX ]")
    );

static void
gst_jpegenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpegenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_jpegenc_src_pad_template));
  gst_element_class_set_details (element_class, &gst_jpegenc_details);
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
          0, 100, JPEG_DEFAULT_QUALITY, G_PARAM_READWRITE));

#if ENABLE_SMOOTHING
  /* disabled, since it doesn't seem to work */
  g_object_class_install_property (gobject_class, PROP_SMOOTHING,
      g_param_spec_int ("smoothing", "Smoothing", "Smoothing factor",
          0, 100, JPEG_DEFAULT_SMOOTHING, G_PARAM_READWRITE));
#endif

  g_object_class_install_property (gobject_class, PROP_IDCT_METHOD,
      g_param_spec_enum ("idct-method", "IDCT Method",
          "The IDCT algorithm to use", GST_TYPE_IDCT_METHOD,
          JPEG_DEFAULT_IDCT_METHOD, G_PARAM_READWRITE));

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
      GST_ROUND_UP_4 (GST_BUFFER_SIZE (jpegenc->output_buffer) -
      jpegenc->jdest.free_in_buffer);

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
  gst_pad_set_getcaps_function (jpegenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_jpegenc_getcaps));
  /*gst_pad_set_setcaps_function (jpegenc->sinkpad, gst_jpegenc_setcaps); */
  gst_pad_use_fixed_caps (jpegenc->sinkpad);
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
  GstPad *otherpad;
  GstCaps *caps;
  const char *name;
  int i;
  GstStructure *structure = NULL;

  /* we want to proxy properties like width, height and framerate from the
     other end of the element */
  otherpad = (pad == jpegenc->srcpad) ? jpegenc->sinkpad : jpegenc->srcpad;

  caps = gst_pad_peer_get_caps (otherpad);
  if (caps == NULL)
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  else
    caps = gst_caps_make_writable (caps);

  if (pad == jpegenc->srcpad) {
    name = "image/jpeg";
  } else {
    name = "video/x-raw-yuv";
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set_name (structure, name);
    gst_structure_remove_field (structure, "format");
    /* ... but for the sink pad, we only do I420 anyway, so add that */
    if (pad == jpegenc->sinkpad) {
      gst_structure_set (structure, "format", GST_TYPE_FOURCC,
          GST_STR_FOURCC ("I420"), NULL);
    }
  }
  gst_object_unref (jpegenc);

  return caps;
}

static gboolean
gst_jpegenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (gst_pad_get_parent (pad));
  GstStructure *structure;
  GstCaps *othercaps;
  GstPad *otherpad;
  gboolean ret;
  const GValue *framerate;

  otherpad = (pad == jpegenc->srcpad) ? jpegenc->sinkpad : jpegenc->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  framerate = gst_structure_get_value (structure, "framerate");
  gst_structure_get_int (structure, "width", &jpegenc->width);
  gst_structure_get_int (structure, "height", &jpegenc->height);

  othercaps = gst_caps_copy (gst_pad_get_pad_template_caps (otherpad));
  if (framerate) {
    gst_caps_set_simple (othercaps,
        "width", G_TYPE_INT, jpegenc->width,
        "height", G_TYPE_INT, jpegenc->height,
        "framerate", GST_TYPE_FRACTION,
        gst_value_get_fraction_numerator (framerate),
        gst_value_get_fraction_denominator (framerate), NULL);
  } else {
    gst_caps_set_simple (othercaps,
        "width", G_TYPE_INT, jpegenc->width,
        "height", G_TYPE_INT, jpegenc->height, NULL);
  }

  ret = gst_pad_set_caps (jpegenc->srcpad, othercaps);
  gst_caps_unref (othercaps);

  if (ret)
    gst_jpegenc_resync (jpegenc);

  gst_object_unref (jpegenc);

  return ret;
}

static void
gst_jpegenc_resync (GstJpegEnc * jpegenc)
{
  gint width, height;

  GST_DEBUG_OBJECT (jpegenc, "resync");

  jpegenc->cinfo.image_width = width = jpegenc->width;
  jpegenc->cinfo.image_height = height = jpegenc->height;
  jpegenc->cinfo.input_components = 3;

  GST_DEBUG_OBJECT (jpegenc, "width %d, height %d", width, height);

#if ENABLE_COLORSPACE_RGB
  switch (jpegenc->format) {
    case GST_COLORSPACE_RGB24:
      jpegenc->bufsize = jpegenc->width * jpegenc->height * 3;
      GST_DEBUG ("gst_jpegenc_resync: setting format to RGB24");
      jpegenc->cinfo.in_color_space = JCS_RGB;
      jpegenc->cinfo.raw_data_in = FALSE;
      break;
    case GST_COLORSPACE_YUV420P:
#endif
      GST_DEBUG_OBJECT (jpegenc, "setting format to YUV420P");

      jpegenc->bufsize = I420_SIZE (jpegenc->width, jpegenc->height);
      jpegenc->cinfo.in_color_space = JCS_YCbCr;

      jpeg_set_defaults (&jpegenc->cinfo);
      /* these are set in _chain()
         jpeg_set_quality (&jpegenc->cinfo, jpegenc->quality, TRUE);
         jpegenc->cinfo.smoothing_factor = jpegenc->smoothing;
         jpegenc->cinfo.dct_method = jpegenc->idct_method;
       */

      jpegenc->cinfo.raw_data_in = TRUE;

      if (height != -1) {
        jpegenc->line[0] =
            g_realloc (jpegenc->line[0], height * sizeof (char *));
        jpegenc->line[1] =
            g_realloc (jpegenc->line[1], height * sizeof (char *) / 2);
        jpegenc->line[2] =
            g_realloc (jpegenc->line[2], height * sizeof (char *) / 2);
      }

      GST_DEBUG_OBJECT (jpegenc, "setting format done");
#if ENABLE_COLORSPACE_RGB
      break;
    default:
      printf ("gst_jpegenc_resync: unsupported colorspace, using RGB\n");
      jpegenc->bufsize = jpegenc->width * jpegenc->height * 3;
      jpegenc->cinfo.in_color_space = JCS_RGB;
      break;
  }
#endif

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
  guint height, width;
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

  width = jpegenc->width;
  height = jpegenc->height;

  base[0] = data + I420_Y_OFFSET (width, height);
  base[1] = data + I420_U_OFFSET (width, height);
  base[2] = data + I420_V_OFFSET (width, height);

  end[0] = base[0] + height * I420_Y_ROWSTRIDE (width);
  end[1] = base[1] + (height / 2) * I420_U_ROWSTRIDE (width);
  end[2] = base[2] + (height / 2) * I420_V_ROWSTRIDE (width);

  /* FIXME: shouldn't we also set
   * - jpegenc->cinfo.max_{v,h}_samp_factor
   * - jpegenc->cinfo.comp_info[0,1,2].{v,h}_samp_factor
   * accordingly?
   */
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

  for (i = 0; i < height; i += 2 * DCTSIZE) {
    /*g_print ("next scanline: %d\n", jpegenc->cinfo.next_scanline); */
    for (j = 0, k = 0; j < (2 * DCTSIZE); j += 2, k++) {
      jpegenc->line[0][j] = base[0];
      if (base[0] + I420_Y_ROWSTRIDE (width) < end[0])
        base[0] += I420_Y_ROWSTRIDE (width);
      jpegenc->line[0][j + 1] = base[0];
      if (base[0] + I420_Y_ROWSTRIDE (width) < end[0])
        base[0] += I420_Y_ROWSTRIDE (width);
      jpegenc->line[1][k] = base[1];
      if (base[1] + I420_U_ROWSTRIDE (width) < end[1])
        base[1] += I420_U_ROWSTRIDE (width);
      jpegenc->line[2][k] = base[2];
      if (base[2] + I420_V_ROWSTRIDE (width) < end[2])
        base[2] += I420_V_ROWSTRIDE (width);
    }
    jpeg_write_raw_data (&jpegenc->cinfo, jpegenc->line, 2 * DCTSIZE);
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
#if ENABLE_SMOOTHING
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
#if ENABLE_SMOOTHING
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
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_free (filter->line[0]);
      g_free (filter->line[1]);
      g_free (filter->line[2]);
      filter->line[0] = NULL;
      filter->line[1] = NULL;
      filter->line[2] = NULL;
      filter->width = -1;
      filter->height = -1;
      break;
    default:
      break;
  }

  return ret;
}
