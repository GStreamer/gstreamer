/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#ifndef __YUV2RGB_H__
#define __YUV2RGB_H__

#include <gst/gst.h>
#include <gstcolorspace.h>

G_BEGIN_DECLS

#if 0
typedef struct _GstColorspaceYUVTables GstColorspaceYUVTables;

struct _GstColorspaceYUVTables {
  int gammaCorrectFlag;
  double gammaCorrect;
  int chromaCorrectFlag;
  double chromaCorrect;

  int *L_tab, *Cr_r_tab, *Cr_g_tab, *Cb_g_tab, *Cb_b_tab;

  /*
   *  We define tables that convert a color value between -256 and 512
   *  into the R, G and B parts of the pixel. The normal range is 0-255.
   **/

  long *r_2_pix;
  long *g_2_pix;
  long *b_2_pix;
};


typedef struct _GstColorspaceConverter GstColorspaceConverter;
typedef void (*GstColorspaceConvertFunction) (GstColorspaceConverter *space, guchar *src, guchar *dest);

struct _GstColorspaceConverter {
  guint width;
  guint height;
  guint insize;
  guint outsize;
  /* private */
  GstColorspaceYUVTables *color_tables;
  GstColorspaceConvertFunction convert;
};
#endif

void gst_colorspace_table_init (GstColorspace *space);

void gst_colorspace_I420_to_rgb32(GstColorspace *space,
    unsigned char *src, unsigned char *dest);
void gst_colorspace_I420_to_rgb24(GstColorspace *space,
    unsigned char *src, unsigned char *dest);
void gst_colorspace_I420_to_rgb16(GstColorspace *space,
    unsigned char *src, unsigned char *dest);
void gst_colorspace_YV12_to_rgb32(GstColorspace *space,
    unsigned char *src, unsigned char *dest);
void gst_colorspace_YV12_to_rgb24(GstColorspace *space,
    unsigned char *src, unsigned char *dest);
void gst_colorspace_YV12_to_rgb16(GstColorspace *space,
    unsigned char *src, unsigned char *dest);

#if 0
GstColorspaceYUVTables * gst_colorspace_init_yuv(long depth,
    long red_mask, long green_mask, long blue_mask);
#endif


#if 0
GstColorspaceConverter* 	gst_colorspace_yuv2rgb_get_converter	(const GstCaps *from, const GstCaps *to);
#define 			gst_colorspace_convert(converter, src, dest) \
								(converter)->convert((converter), (src), (dest))
void 				gst_colorspace_converter_destroy	(GstColorspaceConverter *space);
#endif

G_END_DECLS

#endif

