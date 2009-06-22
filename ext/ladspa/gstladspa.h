/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * gstladspa.h: Header for LADSPA plugin
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


#ifndef __GST_LADSPA_H__
#define __GST_LADSPA_H__


#include <ladspa.h>

#include <gst/gst.h>

#include <gst/signalprocessor/gstsignalprocessor.h>


G_BEGIN_DECLS


typedef struct _ladspa_control_info {
  gchar *name;
  gchar *param_name;
  gfloat lowerbound, upperbound;
  gfloat def;
  gboolean lower, upper, samplerate;
  gboolean toggled, logarithmic, integer, writable;
} ladspa_control_info;


typedef struct _GstLADSPA GstLADSPA;
typedef struct _GstLADSPAClass GstLADSPAClass;


struct _GstLADSPA {
  GstSignalProcessor parent;

  LADSPA_Descriptor *descriptor;
  LADSPA_Handle *handle;

  gboolean activated;
  gboolean inplace_broken;
};

struct _GstLADSPAClass {
  GstSignalProcessorClass parent_class;

  LADSPA_Descriptor *descriptor;

  gint *audio_in_portnums;
  gint *audio_out_portnums;
  gint *control_in_portnums;
  gint *control_out_portnums;
};


G_END_DECLS


#endif /* __GST_LADSPA_H__ */
