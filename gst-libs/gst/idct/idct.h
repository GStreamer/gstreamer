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


#ifndef __GST_IDCT_H__
#define __GST_IDCT_H__

#include <glib.h>

typedef enum {
  GST_IDCT_DEFAULT,    
  GST_IDCT_INT,	
  GST_IDCT_FAST_INT, 
  GST_IDCT_FLOAT,   
  GST_IDCT_MMX,	
  GST_IDCT_MMX32,
  GST_IDCT_SSE,
} GstIDCTMethod;

typedef struct _GstIDCT GstIDCT;
typedef void (*GstIDCTFunction) (gshort *block);

#define GST_IDCT_TRANSPOSE(idct) ((idct)->need_transpose)

struct _GstIDCT {
  /* private */
  GstIDCTFunction convert;
  GstIDCTFunction convert_sparse;
  gboolean need_transpose;
};


GstIDCT *gst_idct_new(GstIDCTMethod method);
#define gst_idct_convert(idct, blocks) (idct)->convert((blocks))
#define gst_idct_convert_sparse(idct, blocks) (idct)->convert_sparse((blocks))
void gst_idct_destroy(GstIDCT *idct);

#endif /* __GST_IDCT_H__ */
