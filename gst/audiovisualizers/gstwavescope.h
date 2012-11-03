/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstwavescope.h: simple oscilloscope
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifndef __GST_WAVE_SCOPE_H__
#define __GST_WAVE_SCOPE_H__

#include "gstaudiovisualizer.h"

G_BEGIN_DECLS
#define GST_TYPE_WAVE_SCOPE            (gst_wave_scope_get_type())
#define GST_WAVE_SCOPE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAVE_SCOPE,GstWaveScope))
#define GST_WAVE_SCOPE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAVE_SCOPE,GstWaveScopeClass))
#define GST_IS_WAVE_SCOPE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAVE_SCOPE))
#define GST_IS_WAVE_SCOPE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAVE_SCOPE))
typedef struct _GstWaveScope GstWaveScope;
typedef struct _GstWaveScopeClass GstWaveScopeClass;

typedef void (*GstWaveScopeProcessFunc) (GstAudioVisualizer *, guint32 *, gint16 *, guint);

struct _GstWaveScope
{
  GstAudioVisualizer parent;
  
  /* < private > */
  GstWaveScopeProcessFunc process;
  gint style;

  /* filter specific data */
  gdouble *flt;
};

struct _GstWaveScopeClass
{
  GstAudioVisualizerClass parent_class;
};

GType gst_wave_scope_get_type (void);
gboolean gst_wave_scope_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_WAVE_SCOPE_H__ */