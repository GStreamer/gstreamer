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
#  include "config.h"
#endif

#include <gst/gst.h>
#include <gst/idct/idct.h>
#include "dct.h"

static void gst_idct_int_sparse_idct(short *data);

GstIDCT *gst_idct_new(GstIDCTMethod method) 
{
  GstIDCT *new = g_malloc(sizeof(GstIDCT));

  new->need_transpose = FALSE;

  if (method == GST_IDCT_DEFAULT) {
#ifdef HAVE_LIBMMX
    if (gst_cpu_get_flags() & GST_CPU_FLAG_MMX) {
      method = GST_IDCT_MMX;
    }
    /* disabled for now 
    if (gst_cpu_get_flags() & GST_CPU_FLAG_SSE) {
      method = GST_IDCT_SSE;
    }
    */
    else
#endif /* HAVE_LIBMMX */
    {
      method = GST_IDCT_FAST_INT;
    }
  }

  new->convert_sparse = gst_idct_int_sparse_idct;

  switch (method) {
	 case GST_IDCT_FAST_INT:
		GST_INFO ( "using fast_int_idct");
	   gst_idct_init_fast_int_idct();
		new->convert = gst_idct_fast_int_idct;
		break;
	 case GST_IDCT_INT:
		GST_INFO ( "using int_idct");
		new->convert = gst_idct_int_idct;
		break;
	 case GST_IDCT_FLOAT:
		GST_INFO ( "using float_idct");
		gst_idct_init_float_idct();
		new->convert = gst_idct_float_idct;
		break;
#ifdef HAVE_LIBMMX
	 case GST_IDCT_MMX:
		GST_INFO ( "using MMX_idct");
		new->convert = gst_idct_mmx_idct;
		new->need_transpose = TRUE;
		break;
	 case GST_IDCT_MMX32:
		GST_INFO ( "using MMX32_idct");
		new->convert = gst_idct_mmx32_idct;
		new->need_transpose = TRUE;
		break;
	 case GST_IDCT_SSE:
		GST_INFO ( "using SSE_idct");
		new->convert = gst_idct_sse_idct;
		new->need_transpose = TRUE;
		break;
#endif /* HAVE_LIBMMX */
	 default:
		GST_INFO ( "method not supported");
		g_free(new);
		return NULL;
  }
  return new;
}

static void gst_idct_int_sparse_idct(short *data) 
{
  short val;
  gint32 v, *dp = (guint32 *)data;

  v = *data;

  if (v < 0) {
    val = -v;
    val += (8 >> 1);
    val /= 8;
    val = -val;
  }
  else {
    val = (v + (8 >> 1)) / 8;
  }
  v = (( val & 0xffff) | (val << 16));

  dp[0] = v;  dp[1] = v;  dp[2] = v;  dp[3] = v;
  dp[4] = v;  dp[5] = v;  dp[6] = v;  dp[7] = v;
  dp[8] = v;  dp[9] = v;  dp[10] = v; dp[11] = v;
  dp[12] = v; dp[13] = v; dp[14] = v; dp[15] = v;
  dp[16] = v; dp[17] = v; dp[18] = v; dp[19] = v;
  dp[20] = v; dp[21] = v; dp[22] = v; dp[23] = v;
  dp[24] = v; dp[25] = v; dp[26] = v; dp[27] = v;
  dp[28] = v; dp[29] = v; dp[30] = v; dp[31] = v;
}

void gst_idct_destroy(GstIDCT *idct) 
{
  g_return_if_fail(idct != NULL);

  g_free(idct);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstidct",
  "Accelerated IDCT routines",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
