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


#ifndef __GST_VIDEOSCALE__X86_H__
#define __GST_VIDEOSCALE__X86_H__


/* scalers */
void gst_videoscale_generate_rowbytes_x86 (unsigned char *copy_row, int src_w,
    int dst_w, int bpp);
void gst_videoscale_scale_nearest_x86 (GstVideoscale * scale,
    unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh);
#endif /* __GST_VIDEOSCALE__X86_H__ */
