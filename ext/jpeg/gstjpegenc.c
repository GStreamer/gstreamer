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
  /* FILL ME */
};

static void             gst_jpegenc_base_init   (gpointer g_class);
static void		gst_jpegenc_class_init	(GstJpegEnc *klass);
static void		gst_jpegenc_init	(GstJpegEnc *jpegenc);

static void		gst_jpegenc_chain	(GstPad *pad, GstData *_data);
static GstPadLinkReturn	gst_jpegenc_link	(GstPad *pad, const GstCaps *caps);

static GstData	*gst_jpegenc_get	(GstPad *pad);

static void		gst_jpegenc_resync	(GstJpegEnc *jpegenc);

static GstElementClass *parent_class = NULL;
static guint gst_jpegenc_signals[LAST_SIGNAL] = { 0 };
static GstPadTemplate *jpegenc_src_template, *jpegenc_sink_template;

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

static GstCaps*
jpeg_caps_factory (void) 
{
  return gst_caps_new_simple ("video/x-jpeg",
      "width",     GST_TYPE_INT_RANGE, 16, 4096,
      "height",    GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE,
      NULL);
}

static GstCaps*
raw_caps_factory (void)
{
  return gst_caps_from_string (GST_VIDEO_YUV_PAD_TEMPLATE_CAPS ("I420"));
}

static void
gst_jpegenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *jpeg_caps;
  
  raw_caps = raw_caps_factory ();
  jpeg_caps = jpeg_caps_factory ();

  jpegenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
						GST_PAD_ALWAYS, 
						raw_caps);
  jpegenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
					       GST_PAD_ALWAYS, 
					       jpeg_caps);

  gst_element_class_add_pad_template (element_class, jpegenc_sink_template);
  gst_element_class_add_pad_template (element_class, jpegenc_src_template);
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
    g_signal_new ("frame_encoded", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstJpegEncClass, frame_encoded), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

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
  jpegenc->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(jpegenc),jpegenc->sinkpad);
  gst_pad_set_chain_function(jpegenc->sinkpad,gst_jpegenc_chain);
  gst_pad_set_link_function(jpegenc->sinkpad, gst_jpegenc_link);
  gst_pad_set_get_function(jpegenc->sinkpad,gst_jpegenc_get);
  jpegenc->srcpad = gst_pad_new("src",GST_PAD_SRC);
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

}

static GstPadLinkReturn
gst_jpegenc_link (GstPad *pad, const GstCaps *caps)
{
  GstJpegEnc *jpegenc = GST_JPEGENC (gst_pad_get_parent (pad));
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_double (structure, "framerate", &jpegenc->fps);
  gst_structure_get_int (structure, "width",     &jpegenc->width);
  gst_structure_get_int (structure, "height",    &jpegenc->height);
  
  caps = gst_caps_new_simple ("video/x-jpeg",
      "width",     G_TYPE_INT, jpegenc->width,
      "height",    G_TYPE_INT, jpegenc->height,
      "framerate", G_TYPE_DOUBLE, jpegenc->fps,
      NULL);

  return gst_pad_try_set_caps (jpegenc->srcpad, caps);
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
  /*jpegenc->cinfo.dct_method = JDCT_DEFAULT; */
  /*jpegenc->cinfo.smoothing_factor = 10; */
  jpeg_set_quality(&jpegenc->cinfo, 85, TRUE);

  /*
  switch (jpegenc->format) {
    case GST_COLORSPACE_RGB24:
      size = 3;
      GST_DEBUG ("gst_jpegenc_resync: setting format to RGB24");
      jpegenc->cinfo.in_color_space = JCS_RGB;
      jpegenc->cinfo.raw_data_in = FALSE;
      break;
    case GST_COLORSPACE_YUV420P:
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
      break;
    default:
      printf("gst_jpegenc_resync: unsupported colorspace, using RGB\n");
      size = 3;
      jpegenc->cinfo.in_color_space = JCS_RGB;
      break;
  }
*/
  jpegenc->bufsize = jpegenc->width*jpegenc->height*size;
  jpegenc->row_stride = width * size;

  jpeg_suppress_tables(&jpegenc->cinfo, TRUE);

  jpegenc->buffer = NULL;
  GST_DEBUG ("gst_jpegenc_resync: resync done");
}

static GstData*
gst_jpegenc_get (GstPad *pad)
{
  GstJpegEnc *jpegenc;
  GstBuffer *newbuf;

  GST_DEBUG ("gst_jpegenc_chain: pull buffer");

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  jpegenc = GST_JPEGENC (GST_OBJECT_PARENT (pad));

  if (jpegenc->buffer == NULL || GST_BUFFER_REFCOUNT_VALUE(jpegenc->buffer) != 1) {
    if (jpegenc->buffer) gst_buffer_unref(jpegenc->buffer);
    GST_DEBUG ("gst_jpegenc_chain: new buffer");
    newbuf = jpegenc->buffer = gst_buffer_new();
    GST_BUFFER_DATA(newbuf) = g_malloc(jpegenc->bufsize);
    GST_BUFFER_SIZE(newbuf) = jpegenc->bufsize;
  }
  gst_buffer_ref(jpegenc->buffer);

  return GST_DATA (jpegenc->buffer);
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
