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


#include <config.h>
#include <gst/gst.h>

#include "ladspa.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
#define GST_TYPE_LADSPA \
  (gst_ladspa_get_type())
#define GST_LADSPA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LADSPA,GstLADSPA))
#define GST_LADSPA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LADSPA,GstLADSPA))
#define GST_IS_LADSPA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LADSPA))
#define GST_IS_LADSPA_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LADSPA))
*/

typedef struct _ladspa_control_info {
  gchar *name;
  gfloat lowerbound, upperbound;
  gboolean lower,upper,samplerate;
  gboolean toggled, logarithmic, integer, writable;
} ladspa_control_info;

typedef struct _GstLADSPA GstLADSPA;
typedef struct _GstLADSPAClass GstLADSPAClass;

struct _GstLADSPA {
  GstElement element;

  LADSPA_Descriptor *descriptor;
  LADSPA_Handle *handle;

  gfloat *controls;
  
  GstPad **sinkpads, 
         **srcpads;
         
  GstBuffer **buffers;

  gboolean loopbased, newcaps, activated;

  gint samplerate, buffersize;
  gulong timestamp;

};

struct _GstLADSPAClass {
  GstElementClass parent_class;

  LADSPA_Descriptor *descriptor;

  gint numports,
       numsinkpads, 
       numsrcpads, 
       numcontrols;

  gint *sinkpad_portnums, 
       *srcpad_portnums, 
       *control_portnums;

  ladspa_control_info *control_info;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_LADSPA_H__ */
