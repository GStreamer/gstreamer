/*
 * gstfluidsynth - fluidsynth plugin for gstreamer
 *
 * Copyright 2007 Wouter Paesen <wouter@blue-gate.be>
 * Copyright 2013 Wim Taymans <wim.taymans@gmail.be>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_FLUIDSYNTH_H__
#define __GST_FLUIDSYNTH_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <fluidsynth.h>

G_BEGIN_DECLS

#define GST_TYPE_FLUIDSYNTH \
  (gst_fluidsynth_get_type())
#define GST_FLUIDSYNTH(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FLUIDSYNTH,GstFluidsynth))
#define GST_FLUIDSYNTH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FLUIDSYNTH,GstFluidsynthClass))
#define GST_IS_FLUIDSYNTH(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FLUIDSYNTH))
#define GST_IS_FLUIDSYNTH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FLUIDSYNTH))

typedef struct _GstFluidsynth GstFluidsynth;
typedef struct _GstFluidsynthClass GstFluidsynthClass;

struct _GstFluidsynth
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* properties */
  gchar *soundfont;
  gboolean synth_chorus;
  gboolean synth_reverb;
  gdouble synth_gain;
  gint synth_polyphony;

  fluid_settings_t* settings;
  fluid_synth_t* synth;
  int sf;

  GstSegment segment;
  GstClockTime last_pts;
  guint64 last_sample;
};

struct _GstFluidsynthClass
{
    GstElementClass parent_class;
};

GType gst_fluidsynth_get_type (void);

G_END_DECLS

#endif /* __GST_FLUIDSYNTH_H__ */
