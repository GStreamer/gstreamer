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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstjpegenc.h"
#include <gst/video/video.h>

/* elementfactory information */
GstElementDetails gst_jpegenc_details = {
  "JPEG image encoder",
  "Codec/Encoder/Image",
  "Encode images in JPEG format",
  "Wim Taymans <wim.taymans@tvd.be>",
};

/* JpegEnc signals and args */
enum {
  FRAME_ENCODED,
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_QUALITY,
  ARG_SMOOTHING,
  /* FILL ME */
};

static void             gst_jpegenc_base_init   (gpointer g_class);
static void		gst_jpegenc_class_init	(GstJpegEnc *klass);
static void		gst_jpegenc_init	(GstJpegEnc *jpegenc);

static void		gst_jpegenc_chain	(GstPad *pad, GstData *_data);
static GstPadLinkReturn	gst_jpegenc_link	(GstPad *pad, const GstCaps *caps);
static GstCaps * gst_jpegenc_getcaps (GstPad *pad);

static void		gst_jpegenc_resync	(GstJpegEnc *jpegenc);
static void gst_jpegenc_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_jpegenc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;
static guint gst_jpegenc_signals[LAST_SIGNAL] = { 0 };

GType
gst_jpegenc_get_type (void)
{
  static GType jpegenc_type = 0;

  if (!jpegenc_type) {
    static const GTypeInfo jpegenc_info = {
      sizeof(GstJpegEnc),
      gst_jpegenc_base_init,
      NULL,
      (GClassInitFunc)gst_jpegenc_class_init,
      NULL,
      NULL,
      sizeof(GstJpegEnc),
      0,
      (GInstanceInitFunc)gst_jpegenc_init,
    };
    jpegenc_type = g_type_register_static(GST_TYPE_ELEMENT, "GstJpegEnc", &jpegenc_info, 0);
  }
  return jpegenc_type;
}

static GstStaticPadTemplate gst_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV("I420"))
);

static GstStaticPadTemplate gst_jpegenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
      "width = (int) [ 16, 4096 ], "
      "height = (int) [ 16, 4096 ], "
      "framerate = (double) [ 1, MAX ]"
    )
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
gst_jpegenc_class_init (GstJpegEnc *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gst_jpegenc_signals[FRAME_ENCODED] =
    g_signal_new ("frame-encoded", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstJpegEncClass, frame_encoded), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  g_object_class_install_property (gobject_class, ARG_QUALITY,
      g_param_spec_int ("quality", "Quality", "Quality of encoding",
        0, 100, 85, G_PARAM_READWRITE));
#if 0
  /* disabled, since it doesn't seem to work */
  g_object_class_install_property (gobject_class, ARG_SMOOTHING,
      g_param_spec_int ("smoothing", "Smoothing", "Smoothing factor",
        0, 100, 0, G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_jpegenc_set_property;
  gobject_class->get_property = gst_jpegenc_get_property;
}

static void
gst_jpegenc_init_destination (j_compress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegenc_chain: init_destination");
}

static gboolean
gst_jpegenc_flush_destination (j_compress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegenc_chain: flush_destination: buffer too small !!!");
  return TRUE;
}

static void gst_jpegenc_term_destination (j_compress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegenc_chain: term_source");
}

static void
gst_jpegenc_init (GstJpegEnc *jpegenc)
{
  /* create the sink and src pads */
  jpegenc->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_jpegenc_sink_pad_template), "sink");
  gst_pad_set_chain_function(jpegenc->sinkpad,gst_jpegenc_chain);
  gst_pad_set_getcaps_function(jpegenc->sinkpad, gst_jpegenc_getcaps);
  gst_pad_set_link_function(jpegenc->sinkpad, gst_jpegenc_link);
  gst_element_add_pad(GST_ELEMENT(jpegenc),jpegenc->sinkpad);

  jpegenc->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_jpegenc_src_pad_template), "src");
  gst_pad_set_getcaps_function(jpegenc->sinkpad, gst_jpegenc_getcaps);
  gst_pad_set_link_function(jpegenc->sinkpad, gst_jpegenc_link);
  gst_element_add_pad(GST_ELEMENT(jpegenc),jpegenc->srcpad);

  /* reset the initial video state */
  jpegenc->width = -1;
  jpegenc->height = -1;

  /* setup jpeglib */
  memset(&jpegenc->cinfo, 0, sizeof(jpegenc->cinfo));
  memset(&jpegenc->jerr, 0, sizeof(jpegenc->jerr));
  jpegenc->cinfo.err = jpeg_std_error(&jpegenc->jerr);
  jpeg_create_compress(&jpegenc->cinfo);

  GST_DEBUG ("gst_jpegenc_init: setting line buffers");
  jpegenc->line[0] = NULL;
  jpegenc->line[1] = NULL;
  jpegenc->line[2] = NULL;

  gst_jpegenc_resync(jpegenc);

  jpegenc->jdest.init_destination = gst_jpegenc_init_destination;
  jpegenc->jdest.empty_output_buffer = gst_jpegenc_flush_destination;
  jpegenc->jdest.term_destination = gst_jpegenc_term_destination;
  jpegenc->cinfo.dest = &jpegenc->jdest;

  jpegenc->quality = 85;
  jpegenc->smoothing = 0;
}

static GstCaps *
gst_jpegenc_getcaps (GstPad *pad)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *caps;
  const char *name;
  int i;
  GstStructure *structure;

  otherpad = (pad == jpegenc->srcpad) ? jpegenc->sinkpad : jpegenc->srcpad;
  caps = gst_pad_get_allowed_caps (otherpad);
  if (pad == jpegenc->srcpad) {
    name = "image/jpeg";
  } else {
    name = "video/x-raw-yuv";
  }
  for (i=0;i<gst_caps_get_size (caps); i++){
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set_name (structure, name);
    gst_structure_remove_field (structure, "format");
  }

  return caps;
}

static GstPadLinkReturn
gst_jpegenc_link (GstPad *pad, const GstCaps *caps)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (gst_pad_get_parent (pad));
  GstStructure *structure;
  GstPadLinkReturn ret;
  GstCaps *othercaps;
  GstPad *otherpad;

  otherpad = (pad == jpegenc->srcpad) ? jpegenc->sinkpad : jpegenc->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_double (structure, "framerate", &jpegenc->fps);
  gst_structure_get_int (structure, "width",     &jpegenc->width);
  gst_structure_get_int (structure, "height",    &jpegenc->height);
  
  othercaps = gst_caps_copy (gst_pad_get_pad_template_caps (otherpad));
  gst_caps_set_simple (othercaps,
      "width",     G_TYPE_INT, jpegenc->width,
      "height",    G_TYPE_INT, jpegenc->height,
      "framerate", G_TYPE_DOUBLE, jpegenc->fps,
      NULL);

  ret = gst_pad_try_set_caps (jpegenc->srcpad, othercaps);
  gst_caps_free(othercaps);

  if (GST_PAD_LINK_SUCCESSFUL (ret)) {
    gst_jpegenc_resync (jpegenc);
  }

  return ret;
}

static void
gst_jpegenc_resync (GstJpegEnc *jpegenc)
{
  guint size = 0;
  gint width, height;

  GST_DEBUG ("gst_jpegenc_resync: resync");

  jpegenc->cinfo.image_width = width = jpegenc->width;
  jpegenc->cinfo.image_height = height = jpegenc->height;
  jpegenc->cinfo.input_components = 3;

  GST_DEBUG ("gst_jpegenc_resync: wdith %d, height %d", width, height);

  jpeg_set_defaults(&jpegenc->cinfo);
  jpegenc->cinfo.dct_method = JDCT_FASTEST;
  /*jpegenc->cinfo.dct_method = JDCT_DEFAULT;*/
  /*jpegenc->cinfo.smoothing_factor = jpegenc->smoothing; */
  jpeg_set_quality(&jpegenc->cinfo, jpegenc->quality, TRUE);

#if 0
  switch (jpegenc->format) {
    case GST_COLORSPACE_RGB24:
      size = 3;
      GST_DEBUG ("gst_jpegenc_resync: setting format to RGB24");
      jpegenc->cinfo.in_color_space = JCS_RGB;
      jpegenc->cinfo.raw_data_in = FALSE;
      break;
    case GST_COLORSPACE_YUV420P:
#endif
      size = 2;
      jpegenc->cinfo.raw_data_in = TRUE;
      jpegenc->cinfo.in_color_space = JCS_YCbCr;
      GST_DEBUG ("gst_jpegenc_resync: setting format to YUV420P");
      jpegenc->cinfo.comp_info[0].h_samp_factor = 2;
      jpegenc->cinfo.comp_info[0].v_samp_factor = 2;
      jpegenc->cinfo.comp_info[1].h_samp_factor = 1;
      jpegenc->cinfo.comp_info[1].v_samp_factor = 1;
      jpegenc->cinfo.comp_info[2].h_samp_factor = 1;
      jpegenc->cinfo.comp_info[2].v_samp_factor = 1;

      if (height != -1) {
        jpegenc->line[0] = g_realloc(jpegenc->line[0], height*sizeof(char*));
        jpegenc->line[1] = g_realloc(jpegenc->line[1], height*sizeof(char*)/2);
        jpegenc->line[2] = g_realloc(jpegenc->line[2], height*sizeof(char*)/2);
      }

      GST_DEBUG ("gst_jpegenc_resync: setting format done");
#if 0
      break;
    default:
      printf("gst_jpegenc_resync: unsupported colorspace, using RGB\n");
      size = 3;
      jpegenc->cinfo.in_color_space = JCS_RGB;
      break;
  }
#endif
  jpegenc->bufsize = jpegenc->width*jpegenc->height*size;

  jpeg_suppress_tables(&jpegenc->cinfo, TRUE);
  //jpeg_suppress_tables(&jpegenc->cinfo, FALSE);

  jpegenc->buffer = NULL;
  GST_DEBUG ("gst_jpegenc_resync: resync done");
}

static void
gst_jpegenc_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstJpegEnc *jpegenc;
  guchar *data, *outdata;
  gulong size, outsize;
  GstBuffer *outbuf;
/*  GstMeta *meta; */
  guint height, width, width2;
  guchar *base[3];
  gint i,j, k;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  /*g_return_if_fail(GST_IS_BUFFER(buf)); */

  /*usleep(10000); */
  jpegenc = GST_JPEGENC (GST_OBJECT_PARENT (pad));

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  GST_DEBUG ("gst_jpegenc_chain: got buffer of %ld bytes in '%s'",size,
          GST_OBJECT_NAME (jpegenc));

  outbuf = gst_buffer_new();
  outsize = GST_BUFFER_SIZE(outbuf) = jpegenc->bufsize;
  outdata = GST_BUFFER_DATA(outbuf) = g_malloc(outsize);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  width = jpegenc->width;
  height = jpegenc->height;

  base[0] = data;
  base[1] = base[0]+width*height;
  base[2] = base[1]+width*height/4;

  jpegenc->jdest.next_output_byte = outdata;
  jpegenc->jdest.free_in_buffer = outsize;

  jpegenc->cinfo.smoothing_factor = jpegenc->smoothing;
  jpeg_set_quality(&jpegenc->cinfo, jpegenc->quality, TRUE);
  jpeg_start_compress(&jpegenc->cinfo, TRUE);

  width2 = width>>1;
  GST_DEBUG ("gst_jpegdec_chain: compressing");

  for (i = 0; i < height; i += 2*DCTSIZE) {
    for (j=0, k=0; j<2*DCTSIZE;j+=2, k++) {
      jpegenc->line[0][j]   = base[0]; base[0] += width;
      jpegenc->line[0][j+1] = base[0]; base[0] += width;
      jpegenc->line[1][k]   = base[1]; base[1] += width2;
      jpegenc->line[2][k]   = base[2]; base[2] += width2;
    }
    jpeg_write_raw_data(&jpegenc->cinfo, jpegenc->line, 2*DCTSIZE);
  }
  jpeg_finish_compress(&jpegenc->cinfo);
  GST_DEBUG ("gst_jpegdec_chain: compressing done");

  GST_BUFFER_SIZE(outbuf) = (((outsize - jpegenc->jdest.free_in_buffer)+3)&~3);

  gst_pad_push(jpegenc->srcpad, GST_DATA (outbuf));

  g_signal_emit(G_OBJECT(jpegenc),gst_jpegenc_signals[FRAME_ENCODED], 0);

  gst_buffer_unref(buf);
}

static void
gst_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJpegEnc *jpegenc;

  g_return_if_fail (GST_IS_JPEGENC (object));
  jpegenc = GST_JPEGENC (object);
  
  switch (prop_id) {
    case ARG_QUALITY:
      jpegenc->quality = g_value_get_int (value);
      break;
    case ARG_SMOOTHING:
      jpegenc->smoothing = g_value_get_int (value);
      break;
    default:
      break;
  }
} 

static void
gst_jpegenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstJpegEnc *jpegenc;

  g_return_if_fail (GST_IS_JPEGENC (object));
  jpegenc = GST_JPEGENC (object);

  switch (prop_id) {
    case ARG_QUALITY:
      g_value_set_int (value, jpegenc->quality);
      break;
    case ARG_SMOOTHING:
      g_value_set_int (value, jpegenc->smoothing);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

