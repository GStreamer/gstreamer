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


#ifndef __GST_META_VIDEORAW_H__
#define __GST_META_VIDEORAW_H__

#include <gst/gst.h>
#include <gdk/gdk.h>
#include <gst/gstmeta.h>
#include <libs/colorspace/gstcolorspace.h>

typedef struct _MetaVideoRaw MetaVideoRaw;
typedef struct _MetaDGA MetaDGA;
typedef struct _MetaOverlay MetaOverlay;
typedef struct _OverlayClip OverlayClip;

struct _OverlayClip {
  int x1, x2, y1, y2;
};

struct _MetaDGA {
  // the base address of the screen
  void *base;
  // the dimensions of the screen
  int swidth, sheight;
  // the number of bytes in a line
  int bytes_per_line;
};

struct _MetaOverlay {
  // the position of the window
  int wx, wy;
  // a reference to the object sending overlay change events
  GtkWidget *overlay_element;
  // the number of overlay regions
  int clip_count;
  // the overlay regions of the display window
  struct _OverlayClip overlay_clip[32];

  gint width;
  gint height;
	
  gboolean did_overlay;
  gboolean fully_obscured;
};

struct _MetaVideoRaw {
  GstMeta meta;

  /* formatting information */
  gint format;
  GdkVisual *visual;
  // dimensions of the video buffer
  gint width;
  gint height;
  // a pointer to the overlay info if the sink supports this
  MetaOverlay *overlay_info;
  // a pointer to the DGA info if the sink supports this
  MetaDGA *dga_info;
};

#endif /* __GST_META_VIDEORAW_H__ */

