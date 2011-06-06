/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
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

#ifndef _GST_DECKLINK_H_
#define _GST_DECKLINK_H_

#include <gst/gst.h>
#include "DeckLinkAPI.h"


typedef enum {
  GST_DECKLINK_MODE_NTSC,
  GST_DECKLINK_MODE_PAL,
  GST_DECKLINK_MODE_1080i50,
  GST_DECKLINK_MODE_1080i60,
  GST_DECKLINK_MODE_720p50,
  GST_DECKLINK_MODE_720p60
} GstDecklinkModeEnum;
#define GST_TYPE_DECKLINK_MODE (gst_decklink_mode_get_type ())
GType gst_decklink_mode_get_type (void);


typedef struct _GstDecklinkMode GstDecklinkMode;
struct _GstDecklinkMode {
  BMDDisplayMode mode;
  int width;
  int height;
  int fps_n;
  int fps_d;
  gboolean interlaced;
};

const GstDecklinkMode * gst_decklink_get_mode (GstDecklinkModeEnum e);
GstCaps * gst_decklink_mode_get_caps (GstDecklinkModeEnum e);

#define GST_DECKLINK_MODE_CAPS(w,h,n,d,i) \
  "video/x-raw-yuv,format=(fourcc)UYVY,width=" #w ",height=" #h \
  ",framerate=" #n "/" #d ",interlaced=" #i

#define GST_DECKLINK_CAPS \
  GST_DECKLINK_MODE_CAPS(720,486,30000,1001,true) ";" \
  GST_DECKLINK_MODE_CAPS(720,576,25,1,true) ";" \
  GST_DECKLINK_MODE_CAPS(1920,1080,25,1,true) ";" \
  GST_DECKLINK_MODE_CAPS(1920,1080,30000,1001,true) ";" \
  GST_DECKLINK_MODE_CAPS(1280,720,50,1,true) ";" \
  GST_DECKLINK_MODE_CAPS(1280,720,60000,1001,true)

#if 0
  MODE(720,486,24000,1001,true) ";" \
  MODE(1920,1080,24000,1001,false) ";" \
  MODE(1920,1080,24,1,false) ";" \
  MODE(1920,1080,25,1,false) ";" \
  MODE(1920,1080,30000,1001,false) ";" \
  MODE(1920,1080,30,1,false) ";" \
  MODE(1920,1080,30,1,true) ";" \
  MODE(1280,720,60,1,true)
#endif


#endif
