/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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
 
#ifndef __GST_SWITCH_H__
#define __GST_SWITCH_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SWITCH \
  (gst_switch_get_type())
#define GST_SWITCH(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SWITCH, GstSwitch))
#define GST_SWITCH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SWITCH, GstSwitch))
#define GST_IS_SWITCH(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SWITCH))
#define GST_IS_SWITCH_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SWITCH))

typedef struct _GstSwitchPad GstSwitchPad;

typedef struct _GstSwitch GstSwitch;
typedef struct _GstSwitchClass GstSwitchClass;

struct _GstSwitchPad {
  GstPad *sinkpad;
  GstData *data;
  gboolean forwarded;
  gboolean eos;
};

struct _GstSwitch {
  GstElement element;
  
  GList *sinkpads;
  GstPad *srcpad;
  
  guint nb_sinkpads;
  guint active_sinkpad;
};

struct _GstSwitchClass {
  GstElementClass parent_class;
};

GType gst_switch_get_type (void);

G_END_DECLS

#endif /* __GST_SWITCH_H__ */
