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

#include "paint.h"

void
gst_smpte_paint_vbox (guint32 *dest, gint stride, 
		      gint x0, gint y0, gint c0, 
		      gint x1, gint y1, gint c1)
{
  gint i, j;
  gint width, height;

  width = x1 - x0;
  height = y1 - y0;
  
  g_assert (width > 0);
  g_assert (height > 0);

  dest = dest + y0 * stride + x0;
        
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      dest[j] = (c1 * j + c0 * (width - j)) / width;
    }
    dest += stride;
  }
}

void
gst_smpte_paint_hbox (guint32 *dest, gint stride, 
		      gint x0, gint y0, gint c0, 
		      gint x1, gint y1, gint c1)
{
  gint i, j;
  gint width, height;

  width = x1 - x0;
  height = y1 - y0;
  
  g_assert (width > 0);
  g_assert (height > 0);

  g_print ("vbox: %d %d %d %d %d %d\n", x0, y0, c0, x1, y1, c1);

  dest = dest + y0 * stride + x0;

  for (i = 0; i < height; i++) {
    guint32 value  = (c1 * i + c0 * (height - i)) / height;
    for (j = 0; j < width; j++) {
      dest[j] = value;
    }
    dest += stride;
  }
}

#define STEP_3D_LINE(dxabs,dyabs,dzabs,sdx,sdy,sdz,xr,yr,zr,px,py,pz) 		\
G_STMT_START { 						\
  if (dxabs >= dyabs && dxabs >= dzabs) {		\
    yr += dyabs;					\
    zr += dzabs;					\
    if (yr >= dxabs) {					\
      py += sdy;					\
      yr -= dxabs;					\
    }							\
    if (zr >= dzabs) {					\
      pz += sdz;					\
      zr -= dxabs;					\
    }							\
    px += sdx;						\
  } else if (dyabs >= dxabs && dyabs >= dzabs) {	\
    xr += dxabs;					\
    zr += dzabs;					\
    if (xr >= dyabs) {					\
      px += sdx;					\
      xr -= dyabs;					\
    }							\
    if (zr >= dzabs) {					\
      pz += sdz;					\
      zr -= dyabs;					\
    }							\
    py += sdy;						\
  } else {						\
    yr += dyabs;					\
    xr += dxabs;					\
    if (yr >= dyabs) {					\
      py += sdy;					\
      yr -= dzabs;					\
    }							\
    if (xr >= dyabs) {					\
      px += sdx;					\
      xr -= dzabs;					\
    }							\
    pz += sdz;						\
  }							\
} G_STMT_END

#define SWAP(a,b) 		\
G_STMT_START { 			\
  typeof (a) tmp;		\
  tmp = (a);			\
  (a) = (b);			\
  (b) = (tmp);			\
} G_STMT_END

#define SIGN(a) ((a) < 0 ? -1 : 1)

#define PREPARE_3D_LINE(x0,y0,z0,x1,y1,z1,dxabs,dyabs,dzabs,sdx,sdy,sdz,xr,yr,zr,px,py,pz)\
G_STMT_START { 			\
  typeof (x0) dx, dy, dz;	\
  dx = x1 - x0;			\
  dy = y1 - y0;			\
  dz = z1 - z0;			\
  dxabs = abs (dx);		\
  dyabs = abs (dy);		\
  dzabs = abs (dz);		\
  sdx = SIGN (dx);		\
  sdy = SIGN (dy);		\
  sdz = SIGN (dz);		\
  xr = dxabs >> 1;		\
  yr = dyabs >> 1;		\
  zr = dzabs >> 1;		\
  px = x0;			\
  py = y0;			\
  pz = z0;			\
} G_STMT_END

void
gst_smpte_paint_triangle_linear (guint32 *dest, gint stride,
				 gint x0, gint y0, gint c0,
				 gint x1, gint y1, gint c1, gint x2, gint y2, gint c2)
{
  gint sdxl, sdyl, sdcl, dxlabs, dylabs, dclabs, xrl, yrl, crl, pxl, pyl, pcl;
  gint sdxr, sdyr, sdcr, dxrabs, dyrabs, dcrabs, xrr, yrr, crr, pxr, pyr, pcr;
  gint i, j, k, seg_start, seg_end;

  if (y0 > y1) { SWAP (x0, x1); SWAP (y0, y1); SWAP (c0, c1); }
  if (y0 > y2) { SWAP (x0, x2); SWAP (y0, y2); SWAP (c0, c2); }
  if (y1 > y2) { SWAP (x1, x2); SWAP (y1, y2); SWAP (c1, c2); }
  
  PREPARE_3D_LINE (x0,y0,c0,x2,y2,c2,
		   dxlabs,dylabs,dclabs,
		   sdxl, sdyl,sdcl,
		   xrl,yrl,crl,
		   pxl,pyl,pcl);

  PREPARE_3D_LINE (x0,y0,c0,x1,y1,c1,
		   dxrabs,dyrabs,dcrabs,
		   sdxr, sdyr,sdcr,
		   xrr,yrr,crr,
		   pxr,pyr,pcr);

  dest = dest + stride * y0;
  seg_start = y0;
  seg_end = y1;

  /* do two passes */
  for (k = 0; k < 2; k++) {
    for (i = seg_start; i < seg_end; i++) {
      gint s = pxl, e = pxr, sc = pcl, ec = pcr;
      gint sign = SIGN (e - s);

      e += sign;

      for (j = s; j != e; j+=sign) {
       dest[j] = (ec * (j - s) + sc * (e - j)) / (e - s);
      }
      while (pyr == i) {
        STEP_3D_LINE (dxrabs, dyrabs, dcrabs, sdxr, sdyr, sdcr, 
	              xrr, yrr, crr, pxr, pyr, pcr);
      }
      while (pyl == i) {
        STEP_3D_LINE (dxlabs, dylabs, dclabs, sdxl, sdyl, sdcl, 
	              xrl, yrl, crl, pxl, pyl, pcl);
      }
      dest += stride;
    }

    PREPARE_3D_LINE (x1,y1,c1,x2,y2,c2,
		     dxrabs,dyrabs,dcrabs,
		     sdxr, sdyr,sdcr,
		     xrr,yrr,crr,
		     pxr,pyr,pcr);

    seg_start = y1;
    seg_end = y2;
  }
}
