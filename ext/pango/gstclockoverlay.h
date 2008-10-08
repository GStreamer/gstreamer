/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Tim-Philipp Müller <tim@centricular.net>
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


#ifndef __GST_CLOCK_OVERLAY_H__
#define __GST_CLOCK_OVERLAY_H__

#include "gsttextoverlay.h"

G_BEGIN_DECLS

#define GST_TYPE_CLOCK_OVERLAY \
  (gst_clock_overlay_get_type())
#define GST_CLOCK_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CLOCK_OVERLAY,GstClockOverlay))
#define GST_CLOCK_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CLOCK_OVERLAY,GstClockOverlayClass))
#define GST_IS_CLOCK_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CLOCK_OVERLAY))
#define GST_IS_CLOCK_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CLOCK_OVERLAY))

typedef struct _GstClockOverlay GstClockOverlay;
typedef struct _GstClockOverlayClass GstClockOverlayClass;

/**
 * GstClockOverlay:
 *
 * Opaque clockoverlay data structure.
 */
struct _GstClockOverlay {
  GstTextOverlay textoverlay;
	gchar         *format; /* as in strftime () */
};

struct _GstClockOverlayClass {
  GstTextOverlayClass parent_class;
};

GType gst_clock_overlay_get_type (void);

G_END_DECLS

#endif /* __GST_CLOCK_OVERLAY_H__ */

