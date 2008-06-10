/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

#ifndef __GST_PULSEMIXERTRACK_H__
#define __GST_PULSEMIXERTRACK_H__

#include <gst/gst.h>

#include "pulsemixerctrl.h"

G_BEGIN_DECLS

#define GST_TYPE_PULSEMIXER_TRACK \
  (gst_pulsemixer_track_get_type())
#define GST_PULSEMIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PULSEMIXER_TRACK, GstPulseMixerTrack))
#define GST_PULSEMIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PULSEMIXER_TRACK, GstPulseMixerTrackClass))
#define GST_IS_PULSEMIXER_TRACK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PULSEMIXER_TRACK))
#define GST_IS_PULSEMIXER_TRACK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_PULSEMIXER_TRACK))


typedef struct _GstPulseMixerTrack
{
  GstMixerTrack parent;
  GstPulseMixerCtrl *control;
} GstPulseMixerTrack;

typedef struct _GstPulseMixerTrackClass
{
  GstMixerTrackClass parent;
} GstPulseMixerTrackClass;

GType gst_pulsemixer_track_get_type (void);

GstMixerTrack *gst_pulsemixer_track_new (GstPulseMixerCtrl * control);

G_END_DECLS

#endif
