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

/*#define DEBUG_ENABLED*/
#include "gstjpegdec.h"
#include <gst/video/video.h>

static GstPadTemplate *jpegdec_src_template, *jpegdec_sink_template;

/* elementfactory information */
GstElementDetails gst_jpegdec_details = {
  "JPEG image decoder",
  "Codec/Decoder/Image",
  "Decode images from JPEG format",
  "Wim Taymans <wim.taymans@tvd.be>",
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

static void     gst_jpegdec_base_init   (gpointer g_class);
static void	gst_jpegdec_class_init	(GstJpegDec *klass);
static void	gst_jpegdec_init	(GstJpegDec *jpegdec);

static void	gst_jpegdec_chain	(GstPad *pad, GstData *_data);
static GstPadLinkReturn
		gst_jpegdec_link	(GstPad *pad, const GstCaps *caps);

static GstElementClass *parent_class = NULL;
/*static guint gst_jpegdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_jpegdec_get_type(void) {
  static GType jpegdec_type = 0;
  
  if (!jpegdec_type) {
    static const GTypeInfo jpegdec_info = {
      sizeof(GstJpegDec),
      gst_jpegdec_base_init,
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

static GstCaps*
jpeg_caps_factory (void) 
{
  return gst_caps_new_simple ("image/jpeg",
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
gst_jpegdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *jpeg_caps;
  
  raw_caps = raw_caps_factory ();
  jpeg_caps = jpeg_caps_factory ();
  
  jpegdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, 
						GST_PAD_ALWAYS, 
						jpeg_caps);
  jpegdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
					       GST_PAD_ALWAYS, 
					       raw_caps);
  gst_element_class_add_pad_template (element_class, jpegdec_sink_template);
  gst_element_class_add_pad_template (element_class, jpegdec_src_template);
  gst_element_class_set_details (element_class, &gst_jpegdec_details);
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
  GST_DEBUG ("gst_jpegdec_chain: init_source");
}
static gboolean
gst_jpegdec_fill_input_buffer (j_decompress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegdec_chain: fill_input_buffer");
  return TRUE;
}

static void
gst_jpegdec_skip_input_data (j_decompress_ptr cinfo, glong num_bytes)
{
  GST_DEBUG ("gst_jpegdec_chain: skip_input_data");
}

static gboolean
gst_jpegdec_resync_to_restart (j_decompress_ptr cinfo, gint desired)
{
  GST_DEBUG ("gst_jpegdec_chain: resync_to_start");
  return TRUE;
}

static void
gst_jpegdec_term_source (j_decompress_ptr cinfo)
{
  GST_DEBUG ("gst_jpegdec_chain: term_source");
}

static void
gst_jpegdec_init (GstJpegDec *jpegdec)
{
  GST_DEBUG ("gst_jpegdec_init: initializing");
  /* create the sink and src pads */
  jpegdec->sinkpad = gst_pad_new_from_template (jpegdec_sink_template, "sink");
  gst_element_add_pad(GST_ELEMENT(jpegdec),jpegdec->sinkpad);
  gst_pad_set_chain_function(jpegdec->sinkpad,gst_jpegdec_chain);
  gst_pad_set_link_function(jpegdec->sinkpad, gst_jpegdec_link);
  jpegdec->srcpad = gst_pad_new_from_template (jpegdec_src_template, "src");
  gst_pad_use_explicit_caps (jpegdec->srcpad);
  gst_element_add_pad(GST_ELEMENT(jpegdec),jpegdec->srcpad);

  /* initialize the jpegdec decoder state */
  jpegdec->next_time = 0;

  /* reset the initial video state */
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

static GstPadLinkReturn
gst_jpegdec_link (GstPad *pad, const GstCaps *caps)
{
  GstJpegDec *jpegdec = GST_JPEGDEC (gst_pad_get_parent (pad));
  GstStructure *structure;
  GstCaps *srccaps;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_double (structure, "framerate", &jpegdec->fps);
  gst_structure_get_int (structure, "width",     &jpegdec->width);
  gst_structure_get_int (structure, "height",    &jpegdec->height);

  srccaps = gst_caps_new_simple ("video/x-raw-yuv",
      "format",    GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I','4','2','0'),
      "width",     G_TYPE_INT, jpegdec->width,
      "height",    G_TYPE_INT, jpegdec->height,
      "framerate", G_TYPE_DOUBLE, jpegdec->fps,
      NULL);

  /* at this point, we're pretty sure that this will be the output
   * format, so we'll set it. */
  gst_pad_set_explicit_caps (jpegdec->srcpad, srccaps);

  return GST_PAD_LINK_OK;
}

/* shamelessly ripped from jpegutils.c in mjpegtools */
static void add_huff_table (j_decompress_ptr dinfo,
			    JHUFF_TBL **htblptr, 
			    const UINT8 *bits, const UINT8 *val)
/* Define a Huffman table */
{
  int nsymbols, len;

  if (*htblptr == NULL)
    *htblptr = jpeg_alloc_huff_table((j_common_ptr) dinfo);

  /* Copy the number-of-symbols-of-each-code-length counts */
  memcpy((*htblptr)->bits, bits, sizeof((*htblptr)->bits));

  /* Validate the counts.  We do this here mainly so we can copy the right
   * number of symbols from the val[] array, without risking marching off
   * the end of memory.  jchuff.c will do a more thorough test later.
   */
  nsymbols = 0;
  for (len = 1; len <= 16; len++)
    nsymbols += bits[len];
  if (nsymbols < 1 || nsymbols > 256)
    g_error("jpegutils.c:  add_huff_table failed badly. ");

  memcpy((*htblptr)->huffval, val, nsymbols * sizeof(UINT8));
}



static void std_huff_tables (j_decompress_ptr dinfo)
/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
{
  static const UINT8 bits_dc_luminance[17] =
    { /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_luminance[] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
  
  static const UINT8 bits_dc_chrominance[17] =
    { /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
  static const UINT8 val_dc_chrominance[] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
  
  static const UINT8 bits_ac_luminance[17] =
    { /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
  static const UINT8 val_ac_luminance[] =
    { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
      0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
      0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
      0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
      0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
      0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
      0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
      0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
      0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
      0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
      0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
      0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
      0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
      0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
      0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
      0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
      0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
      0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
      0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa };
  
  static const UINT8 bits_ac_chrominance[17] =
    { /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
  static const UINT8 val_ac_chrominance[] =
    { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
      0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
      0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
      0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
      0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
      0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
      0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
      0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
      0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
      0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
      0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
      0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
      0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
      0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
      0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
      0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
      0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
      0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
      0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
      0xf9, 0xfa };
  
  add_huff_table(dinfo, &dinfo->dc_huff_tbl_ptrs[0],
		 bits_dc_luminance, val_dc_luminance);
  add_huff_table(dinfo, &dinfo->ac_huff_tbl_ptrs[0],
		 bits_ac_luminance, val_ac_luminance);
  add_huff_table(dinfo, &dinfo->dc_huff_tbl_ptrs[1],
		 bits_dc_chrominance, val_dc_chrominance);
  add_huff_table(dinfo, &dinfo->ac_huff_tbl_ptrs[1],
		 bits_ac_chrominance, val_ac_chrominance);
}



static void guarantee_huff_tables(j_decompress_ptr dinfo)
{
  if ( (dinfo->dc_huff_tbl_ptrs[0] == NULL) &&
       (dinfo->dc_huff_tbl_ptrs[1] == NULL) &&
       (dinfo->ac_huff_tbl_ptrs[0] == NULL) &&
       (dinfo->ac_huff_tbl_ptrs[1] == NULL) ) {
    GST_DEBUG (
	       "Generating standard Huffman tables for this frame.");
    std_huff_tables(dinfo);
  }
}

static void
gst_jpegdec_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstJpegDec *jpegdec;
  guchar *data, *outdata;
  gulong size, outsize;
  GstBuffer *outbuf;
  /*GstMeta *meta;*/
  gint width, height, width2;
  guchar *base[3];
  gint i,j, k;
  gint r_h, r_v;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  /*g_return_if_fail(GST_IS_BUFFER(buf));*/

  jpegdec = GST_JPEGDEC (GST_OBJECT_PARENT (pad));

  if (!GST_PAD_IS_LINKED (jpegdec->srcpad)) {
    gst_buffer_unref (buf);
    return;
  }

  data = (guchar *)GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);
  GST_DEBUG ("gst_jpegdec_chain: got buffer of %ld bytes in '%s'",size,
          GST_OBJECT_NAME (jpegdec));

  jpegdec->jsrc.next_input_byte = data;
  jpegdec->jsrc.bytes_in_buffer = size;
		                  

  GST_DEBUG ("gst_jpegdec_chain: reading header %08lx", *(gulong *)data);
  jpeg_read_header(&jpegdec->cinfo, TRUE);

  r_h = jpegdec->cinfo.cur_comp_info[0]->h_samp_factor;
  r_v = jpegdec->cinfo.cur_comp_info[0]->v_samp_factor;

  /*g_print ("%d %d\n", r_h, r_v);*/
  /*g_print ("%d %d\n", jpegdec->cinfo.cur_comp_info[1]->h_samp_factor, jpegdec->cinfo.cur_comp_info[1]->v_samp_factor);*/
  /*g_print ("%d %d\n", jpegdec->cinfo.cur_comp_info[2]->h_samp_factor, jpegdec->cinfo.cur_comp_info[2]->v_samp_factor);*/

  jpegdec->cinfo.do_fancy_upsampling = FALSE;
  jpegdec->cinfo.do_block_smoothing = FALSE;
  jpegdec->cinfo.out_color_space = JCS_YCbCr;
  jpegdec->cinfo.dct_method = JDCT_IFAST;
  jpegdec->cinfo.raw_data_out = TRUE;
  GST_DEBUG ("gst_jpegdec_chain: starting decompress");
  guarantee_huff_tables(&jpegdec->cinfo);
  jpeg_start_decompress(&jpegdec->cinfo);
  width = jpegdec->cinfo.output_width;
  height = jpegdec->cinfo.output_height;
  GST_DEBUG ("gst_jpegdec_chain: width %d, height %d", width, height);

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

    gst_pad_set_explicit_caps (jpegdec->srcpad,
        gst_caps_new_simple ("video/x-raw-yuv",
          "format",    GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I','4','2','0'),
          "width",     G_TYPE_INT, width,
          "height",    G_TYPE_INT, height,
          "framerate", G_TYPE_DOUBLE, jpegdec->fps,
          NULL));
  }

  /* mind the swap, jpeglib outputs blue chroma first */
  base[0] = outdata;
  base[1] = base[0]+width*height;
  base[2] = base[1]+width*height/4;

  width2 = width >> 1;

  GST_DEBUG ("gst_jpegdec_chain: decompressing %u", jpegdec->cinfo.rec_outbuf_height);
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
    /*g_print ("%d\n", jpegdec->cinfo.output_scanline);*/
    jpeg_read_raw_data(&jpegdec->cinfo, jpegdec->line, r_v*DCTSIZE);
  }

  GST_DEBUG ("gst_jpegdec_chain: decompressing finished");
  jpeg_finish_decompress(&jpegdec->cinfo);

  GST_DEBUG ("gst_jpegdec_chain: sending buffer");
  gst_pad_push(jpegdec->srcpad, GST_DATA (outbuf));

  gst_buffer_unref(buf);
}

