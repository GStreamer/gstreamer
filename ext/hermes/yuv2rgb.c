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

#include "config.h"

#include <math.h>
#include <stdlib.h>

#include "yuv2rgb.h"

/* #define HAVE_LIBMMX */

#ifdef HAVE_LIBMMX 
#include <mmx.h>
#endif

#define CB_BASE 1
#define CR_BASE (CB_BASE*CB_RANGE)
#define LUM_BASE (CR_BASE*CR_RANGE)

#define Min(x,y) (((x) < (y)) ? (x) : (y))
#define Max(x,y) (((x) > (y)) ? (x) : (y))

#define GAMMA_CORRECTION(x) ((int)(pow((x) / 255.0, 1.0 / gammaCorrect) * 255.0))
#define CHROMA_CORRECTION256(x) ((x) >= 128 \
	                        ? 128 + Min(127, (int)(((x) - 128.0) * chromaCorrect)) \
	                        : 128 - Min(128, (int)((128.0 - (x)) * chromaCorrect)))
#define CHROMA_CORRECTION128(x) ((x) >= 0 \
	                        ? Min(127,  (int)(((x) * chromaCorrect))) \
	                        : Max(-128, (int)(((x) * chromaCorrect))))
#define CHROMA_CORRECTION256D(x) ((x) >= 128 \
	                        ? 128.0 + Min(127.0, (((x) - 128.0) * chromaCorrect)) \
	                        : 128.0 - Min(128.0, (((128.0 - (x)) * chromaCorrect))))
#define CHROMA_CORRECTION128D(x) ((x) >= 0 \
	                        ? Min(127.0,  ((x) * chromaCorrect)) \
	                        : Max(-128.0, ((x) * chromaCorrect)))


static void gst_colorspace_I420_to_rgb16	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
static void gst_colorspace_I420_to_rgb24	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
static void gst_colorspace_I420_to_rgb32	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
#ifdef HAVE_LIBMMX
static void gst_colorspace_I420_to_bgr16_mmx	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
static void gst_colorspace_I420_to_bgr32_mmx	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
#endif

static void gst_colorspace_YV12_to_rgb16	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
static void gst_colorspace_YV12_to_rgb24	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
static void gst_colorspace_YV12_to_rgb32	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
#ifdef HAVE_LIBMMX
static void gst_colorspace_YV12_to_bgr16_mmx	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
static void gst_colorspace_YV12_to_bgr32_mmx	(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest);
#endif

static void gst_colorspace_yuv_to_rgb16(GstColorSpaceYUVTables *tables,
	    				unsigned char *lum,
	      				unsigned char *cr,
	        			unsigned char *cb,
		  			unsigned char *out,
		    			int cols, int rows);
static void gst_colorspace_yuv_to_rgb24(GstColorSpaceYUVTables *tables,
	    				unsigned char *lum,
	      				unsigned char *cr,
	        			unsigned char *cb,
		  			unsigned char *out,
		    			int cols, int rows);
static void gst_colorspace_yuv_to_rgb32(GstColorSpaceYUVTables *tables,
	    				unsigned char *lum,
	      				unsigned char *cr,
	        			unsigned char *cb,
		  			unsigned char *out,
		    			int cols, int rows);
#ifdef HAVE_LIBMMX
static void gst_colorspace_yuv_to_bgr32_mmx(GstColorSpaceYUVTables *tables,
	    				unsigned char *lum,
	      				unsigned char *cr,
	        			unsigned char *cb,
		  			unsigned char *out,
		    			int cols, int rows);
extern void gst_colorspace_yuv_to_bgr16_mmx(GstColorSpaceYUVTables *tables,
	    				unsigned char *lum,
	      				unsigned char *cr,
	        			unsigned char *cb,
		  			unsigned char *out,
		    			int cols, int rows);
#endif

static GstColorSpaceYUVTables * gst_colorspace_init_yuv(long depth, 
						long red_mask, long green_mask, long blue_mask);

GstColorSpaceConverter* 
gst_colorspace_yuv2rgb_get_converter (const GstCaps *from, const GstCaps *to) 
{
  guint32 from_space;
  GstColorSpaceConverter *new;
  gint to_bpp;
  GstStructure *struct_from, *struct_to;
  
  GST_DEBUG ("gst_colorspace_yuv2rgb_get_converter");

  new = g_malloc (sizeof (GstColorSpaceConverter));

  struct_from = gst_caps_get_structure (from, 0);
  struct_to = gst_caps_get_structure (to, 0);

  gst_structure_get_int (struct_from, "width", &new->width);
  gst_structure_get_int (struct_from, "height", &new->height);
  new->color_tables = NULL;

  gst_structure_get_fourcc (struct_from, "format", &from_space);
  gst_structure_get_int  (struct_to, "bpp", &to_bpp);

  /* FIXME we leak new here. */

  switch(from_space) {
    case GST_MAKE_FOURCC ('Y','V','1','2'):
    case GST_MAKE_FOURCC ('I','4','2','0'):
    {
      gint red_mask;
      gint green_mask;
      gint blue_mask;

      gst_structure_get_int (struct_to, "red_mask",   &red_mask);
      gst_structure_get_int (struct_to, "green_mask", &green_mask);
      gst_structure_get_int (struct_to, "blue_mask",  &blue_mask);

      GST_INFO ( "red_mask    %08x", red_mask);
      GST_INFO ( "green_mask  %08x", green_mask);
      GST_INFO ( "blue_mask   %08x", blue_mask);

      new->insize 	   = new->width * new->height + new->width * new->height/2;
      new->color_tables    = gst_colorspace_init_yuv (to_bpp, red_mask, green_mask, blue_mask);
      new->outsize 	   = new->width * new->height * (to_bpp/8);

      switch(to_bpp) {
        case 32:
#ifdef HAVE_LIBMMX
	  if (red_mask == 0xff0000 && green_mask == 0x00ff00 && blue_mask == 0x0000ff &&
			  (gst_cpu_get_flags () & GST_CPU_FLAG_MMX) ) {
	    if (from_space == GST_STR_FOURCC ("I420"))
              new->convert =  gst_colorspace_I420_to_bgr32_mmx;
	    else
              new->convert =  gst_colorspace_YV12_to_bgr32_mmx;
	  }
	  else
#endif
	    if (from_space == GST_STR_FOURCC ("I420"))
              new->convert =  gst_colorspace_I420_to_rgb32;
	    else
              new->convert =  gst_colorspace_YV12_to_rgb32;
	  break;
        case 24:
	  if (from_space == GST_STR_FOURCC ("I420"))
            new->convert =  gst_colorspace_I420_to_rgb24;
	  else
            new->convert =  gst_colorspace_YV12_to_rgb24;
	  break;
        case 15:
        case 16:
#ifdef HAVE_LIBMMX
	  if (red_mask == 0xf800 && green_mask == 0x07e0 && blue_mask == 0x001f &&
			  (gst_cpu_get_flags () & GST_CPU_FLAG_MMX) ) {
	    if (from_space == GST_STR_FOURCC ("I420"))
              new->convert =  gst_colorspace_I420_to_bgr16_mmx;
	    else
              new->convert =  gst_colorspace_YV12_to_bgr16_mmx;
	  }
	  else
#endif
	    if (from_space == GST_STR_FOURCC ("I420"))
              new->convert =  gst_colorspace_I420_to_rgb16;
	    else
              new->convert =  gst_colorspace_YV12_to_rgb16;
	  break;
	default:
          g_print("gst_colorspace_yuv2rgb not implemented\n");
	  g_free (new);
	  new = NULL;
      }
      break;
    }
    default:
      g_print("gst_colorspace_yuv2rgb not implemented\n");
      g_free (new);
      new = NULL;
  }
  return new;
}

void 
gst_colorspace_converter_destroy (GstColorSpaceConverter *conv) 
{
  if (conv)
    g_free (conv);
}

static void gst_colorspace_I420_to_rgb32(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) 
{
  int size;
  GST_DEBUG ("gst_colorspace_I420_to_rgb32");

  size = space->width * space->height;

  gst_colorspace_yuv_to_rgb32(space->color_tables,
			src,                                  	/* Y component */
                        src+size,     				/* cr component */
			src+size+(size>>2),		   	/* cb component */
                        dest,
			space->height,
			space->width);

}

static void gst_colorspace_I420_to_rgb24(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_I420_to_rgb24");

  size = space->width * space->height;

  gst_colorspace_yuv_to_rgb24(space->color_tables,
			src,                                  	/* Y component */
                        src+size,     				/* cr component */
			src+size+(size>>2),		   	/* cb component */
                        dest,
			space->height,
			space->width);

}

static void gst_colorspace_I420_to_rgb16(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_I420_to_rgb16");

  size = space->width * space->height;

  gst_colorspace_yuv_to_rgb16(space->color_tables,
			src,                                  	/* Y component */
                        src+size,     				/* cr component */
			src+size+(size>>2),		   	/* cb component */
                        dest,
			space->height,
			space->width);

}

#ifdef HAVE_LIBMMX
static void gst_colorspace_I420_to_bgr32_mmx(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_I420_to_rgb32_mmx");

  size = space->width * space->height;

  gst_colorspace_yuv_to_bgr32_mmx(NULL,
			src,                                  	/* Y component */
                        src+size,     				/* cr component */
			src+size+(size>>2),		   	/* cb component */
                        dest,
			space->height,
			space->width);

}
static void gst_colorspace_I420_to_bgr16_mmx(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_I420_to_bgr16_mmx ");

  size = space->width * space->height;

  gst_colorspace_yuv_to_bgr16_mmx(NULL,
			src,                                  	/* Y component */
                        src+size,     				/* cr component */
			src+size+(size>>2),		   	/* cb component */
                        dest,
			space->height,
			space->width);
  GST_DEBUG ("gst_colorspace_I420_to_bgr16_mmx done");

}
#endif


static void gst_colorspace_YV12_to_rgb32(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) 
{
  int size;
  GST_DEBUG ("gst_colorspace_YV12_to_rgb32");

  size = space->width * space->height;

  gst_colorspace_yuv_to_rgb32(space->color_tables,
			src,                                  	/* Y component */
			src+size+(size>>2),		   	/* cb component */
                        src+size,     				/* cr component */
                        dest,
			space->height,
			space->width);

}

static void gst_colorspace_YV12_to_rgb24(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_YV12_to_rgb24");

  size = space->width * space->height;

  gst_colorspace_yuv_to_rgb24(space->color_tables,
			src,                                  	/* Y component */
			src+size+(size>>2),		   	/* cb component */
                        src+size,     				/* cr component */
                        dest,
			space->height,
			space->width);

}

static void gst_colorspace_YV12_to_rgb16(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_YV12_to_rgb16");

  size = space->width * space->height;

  gst_colorspace_yuv_to_rgb16(space->color_tables,
			src,                                  	/* Y component */
			src+size+(size>>2),		   	/* cb component */
                        src+size,     				/* cr component */
                        dest,
			space->height,
			space->width);

}

#ifdef HAVE_LIBMMX
static void gst_colorspace_YV12_to_bgr32_mmx(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_YV12_to_rgb32_mmx");

  size = space->width * space->height;

  gst_colorspace_yuv_to_bgr32_mmx(NULL,
			src,                                  	/* Y component */
			src+size+(size>>2),		   	/* cb component */
                        src+size,     				/* cr component */
                        dest,
			space->height,
			space->width);

}
static void gst_colorspace_YV12_to_bgr16_mmx(GstColorSpaceConverter *space, unsigned char *src, unsigned char *dest) {
  int size;
  GST_DEBUG ("gst_colorspace_YV12_to_bgr16_mmx ");

  size = space->width * space->height;

  gst_colorspace_yuv_to_bgr16_mmx(NULL,
			src,                                  	/* Y component */
			src+size+(size>>2),		   	/* cb component */
                        src+size,     				/* cr component */
                        dest,
			space->height,
			space->width);
  GST_DEBUG ("gst_colorspace_YV12_to_bgr16_mmx done");

}
#endif
/*
 * How many 1 bits are there in the longword.
 * Low performance, do not call often.
 */

static int
number_of_bits_set(a)
unsigned long a;
{
    if(!a) return 0;
    if(a & 1) return 1 + number_of_bits_set(a >> 1);
    return(number_of_bits_set(a >> 1));
}

/*
 * How many 0 bits are there at most significant end of longword.
 * Low performance, do not call often.
 */
static int
free_bits_at_top(a)
unsigned long a;
{
      /* assume char is 8 bits */
    if(!a) return sizeof(unsigned long) * 8;
        /* assume twos complement */
    if(((long)a) < 0l) return 0;
    return 1 + free_bits_at_top ( a << 1);
}

/*
 * How many 0 bits are there at least significant end of longword.
 * Low performance, do not call often.
 */
static int
free_bits_at_bottom(a)
unsigned long a;
{
      /* assume char is 8 bits */
    if(!a) return sizeof(unsigned long) * 8;
    if(((long)a) & 1l) return 0;
    return 1 + free_bits_at_bottom ( a >> 1);
}

/*
 *--------------------------------------------------------------
 *
 * InitColor16Dither --
 *
 *	To get rid of the multiply and other conversions in color
 *	dither, we use a lookup table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The lookup tables are initialized.
 *
 *--------------------------------------------------------------
 */

static GstColorSpaceYUVTables *
gst_colorspace_init_yuv(long depth, long red_mask, long green_mask, long blue_mask)
{
    int CR, CB, i;
    int *L_tab, *Cr_r_tab, *Cr_g_tab, *Cb_g_tab, *Cb_b_tab;
    long *r_2_pix_alloc;
    long *g_2_pix_alloc;
    long *b_2_pix_alloc;
    GstColorSpaceYUVTables *tables = g_malloc(sizeof(GstColorSpaceYUVTables));

    L_tab    = tables->L_tab = (int *)malloc(256*sizeof(int)); 
    Cr_r_tab = tables->Cr_r_tab = (int *)malloc(256*sizeof(int));
    Cr_g_tab = tables->Cr_g_tab = (int *)malloc(256*sizeof(int));
    Cb_g_tab = tables->Cb_g_tab = (int *)malloc(256*sizeof(int));
    Cb_b_tab = tables->Cb_b_tab = (int *)malloc(256*sizeof(int));

    r_2_pix_alloc = (long *)malloc(768*sizeof(long));
    g_2_pix_alloc = (long *)malloc(768*sizeof(long));
    b_2_pix_alloc = (long *)malloc(768*sizeof(long));

    if (L_tab == NULL ||
	Cr_r_tab == NULL ||
	Cr_g_tab == NULL ||
	Cb_g_tab == NULL ||
	Cb_b_tab == NULL ||
	r_2_pix_alloc == NULL ||
	g_2_pix_alloc == NULL ||
	b_2_pix_alloc == NULL) {
      fprintf(stderr, "Could not get enough memory in InitColorDither\n");
      exit(1);
    }

    for (i=0; i<256; i++) {
      L_tab[i] = i;
      /*
      if (gammaCorrectFlag) {
	L_tab[i] = GAMMA_CORRECTION(i);
      }
      */
      
      CB = CR = i;
      /*
      if (chromaCorrectFlag) {
	CB -= 128; 
	CB = CHROMA_CORRECTION128(CB);
	CR -= 128;
	CR = CHROMA_CORRECTION128(CR);
      } 
      else 
      */
      {
	CB -= 128; CR -= 128;
      }
      Cr_r_tab[i] =  (0.419/0.299) * CR;
      Cr_g_tab[i] = -(0.299/0.419) * CR;
      Cb_g_tab[i] = -(0.114/0.331) * CB; 
      Cb_b_tab[i] =  (0.587/0.331) * CB;

    }

    /* 
     * Set up entries 0-255 in rgb-to-pixel value tables.
     */
    for (i = 0; i < 256; i++) {
      r_2_pix_alloc[i + 256] = i >> (8 - number_of_bits_set(red_mask));
      r_2_pix_alloc[i + 256] <<= free_bits_at_bottom(red_mask);
      g_2_pix_alloc[i + 256] = i >> (8 - number_of_bits_set(green_mask));
      g_2_pix_alloc[i + 256] <<= free_bits_at_bottom(green_mask);
      b_2_pix_alloc[i + 256] = i >> (8 - number_of_bits_set(blue_mask));
      b_2_pix_alloc[i + 256] <<= free_bits_at_bottom(blue_mask);
      /*
       * If we have 16-bit output depth, then we double the value
       * in the top word. This means that we can write out both
       * pixels in the pixel doubling mode with one op. It is 
       * harmless in the normal case as storing a 32-bit value
       * through a short pointer will lose the top bits anyway.
       * A similar optimisation for Alpha for 64 bit has been
       * prepared for, but is not yet implemented.
       */
      if(!(depth == 32) && !(depth == 24)) {

	r_2_pix_alloc[i + 256] |= (r_2_pix_alloc[i + 256]) << 16;
	g_2_pix_alloc[i + 256] |= (g_2_pix_alloc[i + 256]) << 16;
	b_2_pix_alloc[i + 256] |= (b_2_pix_alloc[i + 256]) << 16;

      }
#ifdef SIXTYFOUR_BIT
      if(depth == 32) {

	r_2_pix_alloc[i + 256] |= (r_2_pix_alloc[i + 256]) << 32;
	g_2_pix_alloc[i + 256] |= (g_2_pix_alloc[i + 256]) << 32;
	b_2_pix_alloc[i + 256] |= (b_2_pix_alloc[i + 256]) << 32;

      }
#endif
    }

    /*
     * Spread out the values we have to the rest of the array so that
     * we do not need to check for overflow.
     */
    for (i = 0; i < 256; i++) {
      r_2_pix_alloc[i] = r_2_pix_alloc[256];
      r_2_pix_alloc[i+ 512] = r_2_pix_alloc[511];
      g_2_pix_alloc[i] = g_2_pix_alloc[256];
      g_2_pix_alloc[i+ 512] = g_2_pix_alloc[511];
      b_2_pix_alloc[i] = b_2_pix_alloc[256];
      b_2_pix_alloc[i+ 512] = b_2_pix_alloc[511];
    }

    tables->r_2_pix = r_2_pix_alloc + 256;
    tables->g_2_pix = g_2_pix_alloc + 256;
    tables->b_2_pix = b_2_pix_alloc + 256;

    return tables;

}

/*
 *--------------------------------------------------------------
 *
 * Color16DitherImage --
 *
 *	Converts image into 16 bit color.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static void
gst_colorspace_yuv_to_rgb16(tables, lum, cb, cr, out, rows, cols)
  GstColorSpaceYUVTables *tables;
  unsigned char *lum;
  unsigned char *cr;
  unsigned char *cb;
  unsigned char *out;
  int cols, rows;

{
    int L, CR, CB;
    unsigned short *row1, *row2;
    unsigned char *lum2;
    int x, y;
    int cr_r;
    int crb_g;
    int cb_b;
    int cols_2 = cols>>1;

    row1 = (unsigned short *)out;
    row2 = row1 + cols;
    lum2 = lum + cols;

    for (y=rows>>1; y; y--) {
	for (x=cols_2; x; x--) {

	    CR = *cr++;
	    CB = *cb++;
	    cr_r = tables->Cr_r_tab[CR];
	    crb_g = tables->Cr_g_tab[CR] + tables->Cb_g_tab[CB];
	    cb_b = tables->Cb_b_tab[CB];

            L = tables->L_tab[(int) *lum++];

	    *row1++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);

            L = tables->L_tab[(int) *lum++];

	    *row1++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);

	    /*
	     * Now, do second row.
	     */
	    L = tables->L_tab[(int) *lum2++];

	    *row2++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);

	    L = tables->L_tab[(int) *lum2++];

	    *row2++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);
	}
        /*
         * These values are at the start of the next line, (due
         * to the ++'s above),but they need to be at the start
         * of the line after that.
         */
	lum = lum2;
	row1 = row2;
	lum2 += cols;
	row2 += cols;
    }
}

static void
gst_colorspace_yuv_to_rgb24(tables, lum, cb, cr, out, rows, cols)
  GstColorSpaceYUVTables *tables;
  unsigned char *lum;
  unsigned char *cr;
  unsigned char *cb;
  unsigned char *out;
  int cols, rows;

{
    int L, CR, CB;
    unsigned char *row1, *row2;
    unsigned char *lum2;
    int x, y;
    int cr_r;
    int crb_g;
    int cb_b;
    int cols_2 = cols>>1;
    int cols_3 = cols*3;
    unsigned char pixels[4];

    row1 = out;
    row2 = row1 + cols_3;
    lum2 = lum + cols;
    for (y=rows>>1; y; y--) {
	for (x=cols_2; x; x--) {

	    CR = *cr++;
	    CB = *cb++;
	    cr_r = tables->Cr_r_tab[CR];
	    crb_g = tables->Cr_g_tab[CR] + tables->Cb_g_tab[CB];
	    cb_b = tables->Cb_b_tab[CB];

            L = tables->L_tab[(int) *lum++];

	    ((int *)pixels)[0] = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);
	    *row1++ = pixels[0]; *row1++ = pixels[1]; *row1++ = pixels[2];

            L = tables->L_tab[(int) *lum++];

	    ((int *)pixels)[0] = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);
	    *row1++ = pixels[0]; *row1++ = pixels[1]; *row1++ = pixels[2];

	    /*
	     * Now, do second row.
	     */

	    L = tables->L_tab [(int) *lum2++];

	    ((int *)pixels)[0] = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);
	    *row2++ = pixels[0]; *row2++ = pixels[1]; *row2++ = pixels[2];

	    L = tables->L_tab [(int) *lum2++];

	    ((int *)pixels)[0] = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);
	    *row2++ = pixels[0]; *row2++ = pixels[1]; *row2++ = pixels[2];
	}
	lum = lum2;
	row1 = row2;
	lum2 += cols;
	row2 += cols_3;
    }
}

/*
 *--------------------------------------------------------------
 *
 * Color32DitherImage --
 *
 *	Converts image into 32 bit color (or 24-bit non-packed).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

/*
 * This is a copysoft version of the function above with ints instead
 * of shorts to cause a 4-byte pixel size
 */

static void
gst_colorspace_yuv_to_rgb32(tables, lum, cb, cr, out, rows, cols)
  GstColorSpaceYUVTables *tables;
  unsigned char *lum;
  unsigned char *cr;
  unsigned char *cb;
  unsigned char *out;
  int cols, rows;

{
    int L, CR, CB;
    unsigned int *row1, *row2;
    unsigned char *lum2;
    int x, y;
    int cr_r;
    int crb_g;
    int cb_b;
    int cols_2 = cols>>1;

    row1 = (guint32  *)out;
    row2 = row1 + cols;
    lum2 = lum + cols;
    for (y=rows>>1; y; y--) {
	for (x=cols_2; x; x--) {

	    CR = *cr++;
	    CB = *cb++;
	    cr_r = tables->Cr_r_tab[CR];
	    crb_g = tables->Cr_g_tab[CR] + tables->Cb_g_tab[CB];
	    cb_b = tables->Cb_b_tab[CB];

            L = tables->L_tab[(int) *lum++];

	    *row1++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);

            L = tables->L_tab[(int) *lum++];

	    *row1++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);

	    /*
	     * Now, do second row.
	     */

	    L = tables->L_tab [(int) *lum2++];

	    *row2++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);

	    L = tables->L_tab [(int) *lum2++];

	    *row2++ = (tables->r_2_pix[L+cr_r] | tables->g_2_pix[L+crb_g] | tables->b_2_pix[L+cb_b]);
	}
	lum = lum2;
	row1 = row2;
	lum2 += cols;
	row2 += cols;
    }
}

#ifdef HAVE_LIBMMX
static mmx_t MMX_80w           = (mmx_t)(long long)0x0080008000800080LL;                     /*dd    00080 0080h, 000800080h */

static mmx_t MMX_00FFw         = (mmx_t)(long long)0x00ff00ff00ff00ffLL;                     /*dd    000FF 00FFh, 000FF00FFh */
static mmx_t MMX_FF00w         = (mmx_t)(long long)0xff00ff00ff00ff00LL;                     /*dd    000FF 00FFh, 000FF00FFh */

static mmx_t MMX32_Vredcoeff     = (mmx_t)(long long)0x0059005900590059LL;  
static mmx_t MMX32_Ubluecoeff    = (mmx_t)(long long)0x0072007200720072LL;    
static mmx_t MMX32_Ugrncoeff     = (mmx_t)(long long)0xffeaffeaffeaffeaLL; 
static mmx_t MMX32_Vgrncoeff     = (mmx_t)(long long)0xffd2ffd2ffd2ffd2LL;  

static void
gst_colorspace_yuv_to_bgr32_mmx(tables, lum, cr, cb, out, rows, cols)
  GstColorSpaceYUVTables *tables;
  unsigned char *lum;
  unsigned char *cr;
  unsigned char *cb;
  unsigned char *out;
  int cols, rows;

{
    guint32 *row1 = (guint32 *)out;         /* 32 bit target */
    int cols4 = cols>>2;

    int y, x; 
    
    for (y=rows>>1; y; y--) {
      for (x=cols4; x; x--) {

        /* create Cr (result in mm1) */
        movd_m2r(*(mmx_t *)cb, mm1);      	/*         0  0  0  0  v3 v2 v1 v0 */
        pxor_r2r(mm7, mm7);      		/*         00 00 00 00 00 00 00 00 */
        movd_m2r(*(mmx_t *)lum, mm2);           /*          0  0  0  0 l3 l2 l1 l0 */
        punpcklbw_r2r(mm7, mm1); 		/*         0  v3 0  v2 00 v1 00 v0 */
        punpckldq_r2r(mm1, mm1); 		/*         00 v1 00 v0 00 v1 00 v0 */
        psubw_m2r(MMX_80w, mm1);   		/* mm1-128:r1 r1 r0 r0 r1 r1 r0 r0  */

        /* create Cr_g (result in mm0) */
        movq_r2r(mm1, mm0);           		/* r1 r1 r0 r0 r1 r1 r0 r0 */
        pmullw_m2r(MMX32_Vgrncoeff, mm0); 	/* red*-46dec=0.7136*64 */
        pmullw_m2r(MMX32_Vredcoeff, mm1); 	/* red*89dec=1.4013*64 */
        psraw_i2r(6, mm0);           		/* red=red/64 */
        psraw_i2r(6, mm1);           		/* red=red/64 */
		 
        /* create L1 L2 (result in mm2,mm4) */
        /* L2=lum+cols */
        movq_m2r(*(mmx_t *)(lum+cols),mm3);     /*    0  0  0  0 L3 L2 L1 L0 */
        punpckldq_r2r(mm3, mm2);      		/*   L3 L2 L1 L0 l3 l2 l1 l0 */
        movq_r2r(mm2, mm4);           		/*   L3 L2 L1 L0 l3 l2 l1 l0 */
        pand_m2r(MMX_FF00w, mm2);      		/*   L3 0  L1  0 l3  0 l1  0 */
        pand_m2r(MMX_00FFw, mm4);      		/*   0  L2  0 L0  0 l2  0 l0 */
        psrlw_i2r(8, mm2);             		/*   0  L3  0 L1  0 l3  0 l1 */

        /* create R (result in mm6) */
        movq_r2r(mm2, mm5);           		/*   0 L3  0 L1  0 l3  0 l1 */
        movq_r2r(mm4, mm6);           		/*   0 L2  0 L0  0 l2  0 l0 */
        paddsw_r2r(mm1, mm5);       		/* lum1+red:x R3 x R1 x r3 x r1 */
        paddsw_r2r(mm1, mm6);       		/* lum1+red:x R2 x R0 x r2 x r0 */
        packuswb_r2r(mm5, mm5);       		/*  R3 R1 r3 r1 R3 R1 r3 r1 */
        packuswb_r2r(mm6, mm6);       		/*  R2 R0 r2 r0 R2 R0 r2 r0 */
        pxor_r2r(mm7, mm7);      		/*  00 00 00 00 00 00 00 00 */
        punpcklbw_r2r(mm5, mm6);      		/*  R3 R2 R1 R0 r3 r2 r1 r0 */

        /* create Cb (result in mm1) */
        movd_m2r(*(mmx_t *)cr, mm1);      	/*         0  0  0  0  u3 u2 u1 u0 */
        punpcklbw_r2r(mm7, mm1); 		/*         0  u3 0  u2 00 u1 00 u0 */
        punpckldq_r2r(mm1, mm1); 		/*         00 u1 00 u0 00 u1 00 u0 */
        psubw_m2r(MMX_80w, mm1);   		/* mm1-128:u1 u1 u0 u0 u1 u1 u0 u0  */
        /* create Cb_g (result in mm5) */
        movq_r2r(mm1, mm5);            		/* u1 u1 u0 u0 u1 u1 u0 u0 */
        pmullw_m2r(MMX32_Ugrncoeff, mm5);  	/* blue*-109dec=1.7129*64 */
        pmullw_m2r(MMX32_Ubluecoeff, mm1); 	/* blue*114dec=1.78125*64 */
        psraw_i2r(6, mm5);            		/* blue=red/64 */
        psraw_i2r(6, mm1);            		/* blue=blue/64 */

        /* create G (result in mm7) */
        movq_r2r(mm2, mm3);      		/*   0  L3  0 L1  0 l3  0 l1 */
        movq_r2r(mm4, mm7);      		/*   0  L2  0 L0  0 l2  0 l1 */
        paddsw_r2r(mm5, mm3);  			/* lum1+Cb_g:x G3t x G1t x g3t x g1t */
        paddsw_r2r(mm5, mm7);  			/* lum1+Cb_g:x G2t x G0t x g2t x g0t */
        paddsw_r2r(mm0, mm3);  			/* lum1+Cr_g:x G3  x G1  x g3  x g1 */
        paddsw_r2r(mm0, mm7);  			/* lum1+blue:x G2  x G0  x g2  x g0 */
        packuswb_r2r(mm3, mm3);  		/* G3 G1 g3 g1 G3 G1 g3 g1 */
        packuswb_r2r(mm7, mm7);  		/* G2 G0 g2 g0 G2 G0 g2 g0 */
        punpcklbw_r2r(mm3, mm7); 		/* G3 G2 G1 G0 g3 g2 g1 g0 */

        /* create B (result in mm5) */
        movq_r2r(mm2, mm3);         		/*   0  L3  0 L1  0 l3  0 l1 */
        movq_r2r(mm4, mm5);         		/*   0  L2  0 L0  0 l2  0 l1 */
        paddsw_r2r(mm1, mm3);     		/* lum1+blue:x B3 x B1 x b3 x b1 */
        paddsw_r2r(mm1, mm5);     		/* lum1+blue:x B2 x B0 x b2 x b0 */
        packuswb_r2r(mm3, mm3);     		/* B3 B1 b3 b1 B3 B1 b3 b1 */
        packuswb_r2r(mm5, mm5);     		/* B2 B0 b2 b0 B2 B0 b2 b0 */
        punpcklbw_r2r(mm3, mm5);    		/* B3 B2 B1 B0 b3 b2 b1 b0 */

        /* fill destination row1 (needed are mm6=Rr,mm7=Gg,mm5=Bb) */

        pxor_r2r(mm2, mm2);           		/*  0  0  0  0  0  0  0  0 */
        pxor_r2r(mm4, mm4);           		/*  0  0  0  0  0  0  0  0 */
        movq_r2r(mm6, mm1);           		/* R3 R2 R1 R0 r3 r2 r1 r0 */
        movq_r2r(mm5, mm3);           		/* B3 B2 B1 B0 b3 b2 b1 b0 */
        /* process lower lum */
        punpcklbw_r2r(mm4, mm1);      		/*  0 r3  0 r2  0 r1  0 r0 */
        punpcklbw_r2r(mm4, mm3);      		/*  0 b3  0 b2  0 b1  0 b0 */
        movq_r2r(mm1, mm2);           		/*  0 r3  0 r2  0 r1  0 r0 */
        movq_r2r(mm3, mm0);           		/*  0 b3  0 b2  0 b1  0 b0 */
        punpcklwd_r2r(mm1, mm3);      		/*  0 r1  0 b1  0 r0  0 b0 */
        punpckhwd_r2r(mm2, mm0);      		/*  0 r3  0 b3  0 r2  0 b2 */

        pxor_r2r(mm2, mm2);           		/*  0  0  0  0  0  0  0  0 */
        movq_r2r(mm7, mm1);           		/* G3 G2 G1 G0 g3 g2 g1 g0 */
        punpcklbw_r2r(mm1, mm2);      		/* g3  0 g2  0 g1  0 g0  0 */
        punpcklwd_r2r(mm4, mm2);      		/*  0  0 g1  0  0  0 g0  0  */
        por_r2r(mm3, mm2);      		/*  0 r1 g1 b1  0 r0 g0 b0 */
        movq_r2m(mm2, *(mmx_t *)row1);      	/* wrote out ! row1 */

        pxor_r2r(mm2, mm2);           		/*  0  0  0  0  0  0  0  0 */
        punpcklbw_r2r(mm1, mm4);      		/* g3  0 g2  0 g1  0 g0  0 */
        punpckhwd_r2r(mm2, mm4);      		/*  0  0 g3  0  0  0 g2  0  */
        por_r2r(mm0, mm4);      		/*  0 r3 g3 b3  0 r2 g2 b2 */
        movq_r2m(mm4, *(mmx_t *)(row1+2)); 	/* wrote out ! row1 */
		 
        /* fill destination row2 (needed are mm6=Rr,mm7=Gg,mm5=Bb) */
        /* this can be done "destructive" */
        pxor_r2r(mm2, mm2);           		/*  0  0  0  0  0  0  0  0 */
        punpckhbw_r2r(mm2, mm6);      		/*  0 R3  0 R2  0 R1  0 R0 */
        punpckhbw_r2r(mm1, mm5);      		/* G3 B3 G2 B2 G1 B1 G0 B0 */
        movq_r2r(mm5, mm1);           		/* G3 B3 G2 B2 G1 B1 G0 B0 */
        punpcklwd_r2r(mm6, mm1);      		/*  0 R1 G1 B1  0 R0 G0 B0 */
        movq_r2m(mm1, *(mmx_t *)(row1+cols));	/* wrote out ! row2 */
        punpckhwd_r2r(mm6, mm5);      		/*  0 R3 G3 B3  0 R2 G2 B2 */
        movq_r2m(mm5, *(mmx_t *)(row1+cols+2)); /* wrote out ! row2 */
		 
        lum+=4;
        cr+=2;
        cb+=2;
        row1 +=4;
      }
      lum += cols;
      row1 += cols;
    }

    emms();

}
#endif

