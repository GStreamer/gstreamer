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


#ifndef __GST_OVERLAY_H__
#define __GST_OVERLAY_H__

#include <gst/gst.h>

#define GST_TYPE_OVERLAY \
  (gst_overlay_get_type())
#define GST_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OVERLAY,GstOverlay))
#define GST_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OVERLAY,GstOverlay))
#define GST_IS_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OVERLAY))
#define GST_IS_OVERLAY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OVERLAY))

typedef struct _GstOverlay GstOverlay;
typedef struct _GstOverlayClass GstOverlayClass;

struct _GstOverlay {
  GstElement 	 element;

  GstPad 	*srcpad;
  GstPad	*sinkpad1;
  GstPad	*sinkpad2;
  GstPad	*sinkpad3;

  gint 		 format;
  gint 		 width;
  gint 		 height;

  gint 		 duration;
  gint 		 position;

  gint 		 type;
  gint 		 fps;
  gint 		 border;
  gint 		 depth;

  gdouble	 framerate;
};

struct _GstOverlayClass {
  GstElementClass parent_class;
};

#endif /* __GST_OVERLAY_H__ */
