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


#include <math.h>
#include <stdlib.h>

//#define DEBUG_ENABLED
#include <gst/gst.h>
#include <gstcolorspace.h>

#include "yuv2rgb.h"

static GstBuffer *gst_colorspace_yuv422P_to_rgb24(GstBuffer *src, GstColorSpaceParameters *params);
static GstBuffer *gst_colorspace_yuv422P_to_rgb16(GstBuffer *src, GstColorSpaceParameters *params);

static void gst_colorspace_yuv_to_rgb16(GstColorSpaceYUVTables *tables,
	    				unsigned char *lum,
	      				unsigned char *cr,
	        			unsigned char *cb,
		  			unsigned char *out,
		    			int cols, int rows);

static GstColorSpaceYUVTables * gst_colorspace_init_yuv(long depth, 
						long red_mask, long green_mask, long blue_mask);

GstColorSpaceConverter gst_colorspace_yuv2rgb_get_converter(GstColorSpace src, GstColorSpace dest) {
  DEBUG("gst_colorspace_yuv2rgb_get_converter\n");
  switch(src) {
    case GST_COLORSPACE_YUV422P:
      switch(dest) {
        case GST_COLORSPACE_RGB24:
          return gst_colorspace_yuv422P_to_rgb24;
        case GST_COLORSPACE_RGB555:
        case GST_COLORSPACE_RGB565:
          return gst_colorspace_yuv422P_to_rgb16;
	default:
	  break;
      }
      break;
    default:
      break;
  }
  return NULL;
}

static GstBuffer *gst_colorspace_yuv422P_to_rgb24(GstBuffer *src, GstColorSpaceParameters *params) {
  DEBUG("gst_colorspace_yuv422P_to_rgb24\n");

  return src;
}

static GstBuffer *gst_colorspace_yuv422P_to_rgb16(GstBuffer *src, GstColorSpaceParameters *params) {
  static GstColorSpaceYUVTables *color_tables = NULL;
  DEBUG("gst_colorspace_yuv422P_to_rgb16\n");

  g_return_val_if_fail(params != NULL, NULL);

  if (color_tables == NULL) {
    color_tables = gst_colorspace_init_yuv(16, 0xF800, 0x07E0, 0x001F);
  }

  gst_colorspace_yuv_to_rgb16(color_tables,
			GST_BUFFER_DATA(src),                                  // Y component
                        GST_BUFFER_DATA(src)+params->width*params->height,     // cr component
			GST_BUFFER_DATA(src)+params->width*params->height+
	                              (params->width*params->height)/4,   // cb component
                        params->outbuf,
			params->height,
			params->width);

  return src;
}


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
 * Shift the 0s in the least significant end out of the longword.
 * Low performance, do not call often.
 */
static unsigned long
shifted_down(a)
unsigned long a;
{
    if(!a) return 0;
    if(a & 1) return a;
    return a >> 1;
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
      if(!(depth == 32)) {

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
gst_colorspace_yuv_to_rgb16(tables, lum, cr, cb, out, rows, cols)
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
    int cr_g;
    int cb_g;
    int cb_b;
    int cols_2 = cols/2;

    row1 = (unsigned short *)out;
    row2 = row1 + cols_2 + cols_2;
    lum2 = lum + cols_2 + cols_2;

    for (y=0; y<rows; y+=2) {
	for (x=0; x<cols_2; x++) {
	    int R, G, B;

	    CR = *cr++;
	    CB = *cb++;
	    cr_r = tables->Cr_r_tab[CR];
	    cr_g = tables->Cr_g_tab[CR];
	    cb_g = tables->Cb_g_tab[CB];
	    cb_b = tables->Cb_b_tab[CB];

            L = tables->L_tab[(int) *lum++];

	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row1++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);

#ifdef INTERPOLATE
            if(x != cols_2 - 1) {
	      CR = (CR + *cr) >> 1;
	      CB = (CB + *cb) >> 1;
	      cr_r = tables->Cr_r_tab[CR];
	      cr_g = tables->Cr_g_tab[CR];
	      cb_g = tables->Cb_g_tab[CB];
	      cb_b = tables->Cb_b_tab[CB];
            }
#endif

            L = tables->L_tab[(int) *lum++];

	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row1++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);

	    /*
	     * Now, do second row.
	     */
#ifdef INTERPOLATE
            if(y != rows - 2) {
	      CR = (CR + *(cr + cols_2 - 1)) >> 1;
	      CB = (CB + *(cb + cols_2 - 1)) >> 1;
	      cr_r = tables->Cr_r_tab[CR];
	      cr_g = tables->Cr_g_tab[CR];
	      cb_g = tables->Cb_g_tab[CB];
	      cb_b = tables->Cb_b_tab[CB];
            }
#endif

	    L = tables->L_tab[(int) *lum2++];
	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row2++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);

	    L = tables->L_tab[(int) *lum2++];
	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row2++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);
	}
        /*
         * These values are at the start of the next line, (due
         * to the ++'s above),but they need to be at the start
         * of the line after that.
         */
	lum += cols_2 + cols_2;
	lum2 += cols_2 + cols_2;
	row1 += cols_2 + cols_2;
	row2 += cols_2 + cols_2;
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
gst_colorspace_yuv_to_rgb32(tables, lum, cr, cb, out, rows, cols)
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
    int cr_g;
    int cb_g;
    int cb_b;
    int cols_2 = cols / 2;

    row1 = (unsigned int *)out;
    row2 = row1 + cols_2 + cols_2;
    lum2 = lum + cols_2 + cols_2;
    for (y=0; y<rows; y+=2) {
	for (x=0; x<cols_2; x++) {
	    int R, G, B;

	    CR = *cr++;
	    CB = *cb++;
	    cr_r = tables->Cr_r_tab[CR];
	    cr_g = tables->Cr_g_tab[CR];
	    cb_g = tables->Cb_g_tab[CB];
	    cb_b = tables->Cb_b_tab[CB];

            L = tables->L_tab[(int) *lum++];

	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row1++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);

#ifdef INTERPOLATE
            if(x != cols_2 - 1) {
	      CR = (CR + *cr) >> 1;
	      CB = (CB + *cb) >> 1;
	      cr_r = tables->Cr_r_tab[CR];
	      cr_g = tables->Cr_g_tab[CR];
	      cb_g = tables->Cb_g_tab[CB];
	      cb_b = tables->Cb_b_tab[CB];
            }
#endif

            L = tables->L_tab[(int) *lum++];

	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row1++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);

	    /*
	     * Now, do second row.
	     */

#ifdef INTERPOLATE
            if(y != rows - 2) {
	      CR = (CR + *(cr + cols_2 - 1)) >> 1;
	      CB = (CB + *(cb + cols_2 - 1)) >> 1;
	      cr_r = tables->Cr_r_tab[CR];
	      cr_g = tables->Cr_g_tab[CR];
	      cb_g = tables->Cb_g_tab[CB];
	      cb_b = tables->Cb_b_tab[CB];
            }
#endif

	    L = tables->L_tab [(int) *lum2++];
	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row2++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);

	    L = tables->L_tab [(int) *lum2++];
	    R = L + cr_r;
	    G = L + cr_g + cb_g;
	    B = L + cb_b;

	    *row2++ = (tables->r_2_pix[R] | tables->g_2_pix[G] | tables->b_2_pix[B]);
	}
	lum += cols_2 + cols_2;
	lum2 += cols_2 + cols_2;
	row1 += cols_2 + cols_2;
	row2 += cols_2 + cols_2;
    }
}

