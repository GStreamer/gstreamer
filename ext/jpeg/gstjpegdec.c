/* Gnome-Streamer
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


#include <string.h>

//#define DEBUG_ENABLED
#include "gstjpegdec.h"

extern GstPadTemplate *jpegdec_src_template, *jpegdec_sink_template;

/* elementfactory information */
GstElementDetails gst_jpegdec_details = {
  "jpeg image decoder",
  "Filter/Decoder/Image",
  ".jpeg",
  VERSION,
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 2000",
};

/* JpegDec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void	gst_jpegdec_class_init	(GstJpegDec *klass);
static void	gst_jpegdec_init	(GstJpegDec *jpegdec);

static void	gst_jpegdec_chain	(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
//static guint gst_jpegdec_signals[LAST_SIGNAL] = { 0 };

GType
gst_jpegdec_get_type(void) {
  static GType jpegdec_type = 0;

  if (!jpegdec_type) {
    static const GTypeInfo jpegdec_info = {
      sizeof(GstJpegDec),      NULL,
      NULL,
      (GClassInitFunc)gst_jpegdec_class_init,
      NULL,
      NULL,
      sizeof(GstJpegDec),
      0,
      (GInstanceInitFunc)gst_jpegdec_init,
    };
    jpegdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstJpegDec", &jpegdec_info, 0);
  }
  return jpegdec_type;
}

static void
gst_jpegdec_class_init (GstJpegDec *klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}

static void
gst_jpegdec_init_source (j_decompress_ptr cinfo)
{
  GST_DEBUG (0,"gst_jpegdec_chain: init_source\n");
}
static gboolean
gst_jpegdec_fill_input_buffer (j_decompress_ptr cinfo)
{
  GST_DEBUG (0,"gst_jpegdec_chain: fill_input_buffer\n");
  return TRUE;
}

static void
gst_jpegdec_skip_input_data (j_decompress_ptr cinfo, glong num_bytes)
{
  GST_DEBUG (0,"gst_jpegdec_chain: skip_input_data\n");
}

static gboolean
gst_jpegdec_resync_to_restart (j_decompress_ptr cinfo, gint desired)
{
  GST_DEBUG (0,"gst_jpegdec_chain: resync_to_start\n");
  return TRUE;
}

static void
gst_jpegdec_term_source (j_decompress_ptr cinfo)
{
  GST_DEBUG (0,"gst_jpegdec_chain: term_source\n");
}

static void
gst_jpegdec_init (GstJpegDec *jpegdec)
{
  GST_DEBUG (0,"gst_jpegdec_init: initializing\n");
  /* create the sink and src pads */
  jpegdec->sinkpad = gst_pad_new_from_template (jpegdec_sink_template, "sink");
  gst_element_add_pad(GST_ELEMENT(jpegdec),jpegdec->sinkpad);
  gst_pad_set_chain_function(jpegdec->sinkpad,gst_jpegdec_chain);
  jpegdec->srcpad = gst_pad_new_from_template (jpegdec_src_template, "src");
  gst_element_add_pad(GST_ELEMENT(jpegdec),jpegdec->srcpad);

  /* initialize the jpegdec decoder state */
  jpegdec->next_time = 0;

  // reset the initial video state
  jpegdec->format = -1;
  jpegdec->width = -1;
  jpegdec->height = -1;

  jpegdec->line[0] = NULL;
  jpegdec->line[1] = NULL;
  jpegdec->line[2] = NULL;

  /* setup jpeglib */
  memset(&jpegdec->cinfo, 0, sizeof(jpegdec->cinfo));
  memset(&jpegdec->jerr, 0, sizeof(jpegdec->jerr));
  jpegdec->cinfo.err = jpeg_std_error(&jpegdec->jerr);
  jpeg_create_decompress(&jpegdec->cinfo);

  jpegdec->jsrc.init_source = gst_jpegdec_init_source;
  jpegdec->jsrc.fill_input_buffer = gst_jpegdec_fill_input_buffer;
  jpegdec->jsrc.skip_input_data = gst_jpegdec_skip_input_data;
  jpegdec->jsrc.resync_to_restart = gst_jpegdec_resync_to_restart;
  jpegdec->jsrc.term_source = gst_jpegdec_term_source;
  jpegdec->cinfo.src = &jpegdec->jsrc;

}

static void
gst_jpegdec_chain (GstPad *pad, GstBuffer *buf)
{
  GstJpegDec *jpegdec;
  guchar *data, *outdata;
  gulong size, outsize;
  GstBuffer *outbuf;
  //GstMeta *meta;
  gint width, height, width2;
  guchar *base[3];
  gint i,j, k;
  gint r_h, r_v;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  //g_return_if_fail(GST_IS_BUFFER(buf));

  jpegdec = GST_JPEGDEC (GST_OBJECT_PARENT (pad));

  if (!GST_PAD_CONNECTED (jpegdec->srcpad)) {
    gst_buffer_unref (buf);
    return;
  }

  data = (guchar *)GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);
  GST_DEBUG (0,"gst_jpegdec_chain: got buffer of %ld bytes in '%s'\n",size,
          GST_OBJECT_NAME (jpegdec));

  jpegdec->jsrc.next_input_byte = data;
  jpegdec->jsrc.bytes_in_buffer = size;
		                  

  GST_DEBUG (0,"gst_jpegdec_chain: reading header %08lx\n", *(gulong *)data);
  jpeg_read_header(&jpegdec->cinfo, TRUE);

  r_h = jpegdec->cinfo.cur_comp_info[0]->h_samp_factor;
  r_v = jpegdec->cinfo.cur_comp_info[0]->v_samp_factor;

  //g_print ("%d %d\n", r_h, r_v);
  //g_print ("%d %d\n", jpegdec->cinfo.cur_comp_info[1]->h_samp_factor, jpegdec->cinfo.cur_comp_info[1]->v_samp_factor);
  //g_print ("%d %d\n", jpegdec->cinfo.cur_comp_info[2]->h_samp_factor, jpegdec->cinfo.cur_comp_info[2]->v_samp_factor);

  jpegdec->cinfo.do_fancy_upsampling = FALSE;
  jpegdec->cinfo.do_block_smoothing = FALSE;
  jpegdec->cinfo.out_color_space = JCS_YCbCr;
  jpegdec->cinfo.dct_method = JDCT_IFAST;
  jpegdec->cinfo.raw_data_out = TRUE;
  GST_DEBUG (0,"gst_jpegdec_chain: starting decompress\n");
  jpeg_start_decompress(&jpegdec->cinfo);
  width = jpegdec->cinfo.output_width;
  height = jpegdec->cinfo.output_height;
  GST_DEBUG (0,"gst_jpegdec_chain: width %d, height %d\n", width, height);

  outbuf = gst_buffer_new();
  outsize = GST_BUFFER_SIZE(outbuf) = width*height +
  				      width*height / 2;
  outdata = GST_BUFFER_DATA(outbuf) = g_malloc(outsize);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  if (jpegdec->height != height) {
    jpegdec->line[0] = g_realloc(jpegdec->line[0], height*sizeof(char*));
    jpegdec->line[1] = g_realloc(jpegdec->line[1], height*sizeof(char*));
    jpegdec->line[2] = g_realloc(jpegdec->line[2], height*sizeof(char*));
    jpegdec->height = height;

    gst_pad_set_caps (jpegdec->srcpad, gst_caps_new (
			    "jpegdec_caps",
			    "video/raw",
			    gst_props_new (
				    "format",  GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
				    "width",   GST_PROPS_INT (width),
				    "height",  GST_PROPS_INT (height),
				    NULL)));
  }

  /* mind the swap, jpeglib outputs blue chroma first */
  base[0] = outdata;
  base[1] = base[0]+width*height;
  base[2] = base[1]+width*height/4;

  width2 = width >> 1;

  GST_DEBUG (0,"gst_jpegdec_chain: decompressing %u\n", jpegdec->cinfo.rec_outbuf_height);
  for (i = 0; i < height; i += r_v*DCTSIZE) {
    for (j=0, k=0; j< (r_v*DCTSIZE); j += r_v, k++) {
      jpegdec->line[0][j]   = base[0]; base[0] += width;
      if (r_v == 2) {
	 jpegdec->line[0][j+1] = base[0]; base[0] += width;
      }
      jpegdec->line[1][k]   = base[1]; 
      jpegdec->line[2][k]   = base[2];
      if (r_v == 2 || k&1) {
         base[1] += width2; base[2] += width2;
      }
    }
    //g_print ("%d\n", jpegdec->cinfo.output_scanline);
    jpeg_read_raw_data(&jpegdec->cinfo, jpegdec->line, r_v*DCTSIZE);
  }

  GST_DEBUG (0,"gst_jpegdec_chain: decompressing finished\n");
  jpeg_finish_decompress(&jpegdec->cinfo);

  GST_DEBUG (0,"gst_jpegdec_chain: sending buffer\n");
  gst_pad_push(jpegdec->srcpad, outbuf);

  gst_buffer_unref(buf);
}

