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

/*#define DEBUG_ENABLED */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoscale.h"

/* scalers */
void gst_videoscale_generate_rowbytes_x86 (unsigned char *copy_row, int src_w,
    int dst_w, int bpp);
void gst_videoscale_scale_nearest_x86 (GstVideoscale * scale,
    unsigned char *src, unsigned char *dest, int sw, int sh, int dw, int dh);

#define PREFIX16        0x66
#define STORE_BYTE      0xAA
#define STORE_WORD      0xAB
#define LOAD_BYTE       0xAC
#define LOAD_WORD       0xAD
#define RETURN          0xC3

void
gst_videoscale_generate_rowbytes_x86 (unsigned char *copy_row, int src_w,
    int dst_w, int bpp)
{
  int i;
  int pos, inc;
  unsigned char *eip;
  unsigned char load, store;

  GST_DEBUG ("videoscale: setup scaling %p", copy_row);

  switch (bpp) {
    case 1:
      load = LOAD_BYTE;
      store = STORE_BYTE;
      break;
    case 2:
    case 4:
      load = LOAD_WORD;
      store = STORE_WORD;
      break;
    default:
      return;
  }
  pos = 0x10000;
  inc = (src_w << 16) / dst_w;
  eip = copy_row;
  for (i = 0; i < dst_w; ++i) {
    while (pos >= 0x10000L) {
      if (bpp == 2) {
        *eip++ = PREFIX16;
      }
      *eip++ = load;
      pos -= 0x10000L;
    }
    if (bpp == 2) {
      *eip++ = PREFIX16;
    }
    *eip++ = store;
    pos += inc;
  }
  *eip++ = RETURN;
  GST_DEBUG ("scaler start/end %p %p %p", copy_row, eip,
      (void *) (eip - copy_row));
}
