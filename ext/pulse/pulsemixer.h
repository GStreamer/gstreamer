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

#ifndef __GST_PULSEMIXER_H__
#define __GST_PULSEMIXER_H__

#include <gst/gst.h>

#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>

#include "pulsemixerctrl.h"
#include "pulseprobe.h"

G_BEGIN_DECLS

#define GST_TYPE_PULSEMIXER \
  (gst_pulsemixer_get_type())
#define GST_PULSEMIXER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PULSEMIXER,GstPulseMixer))
#define GST_PULSEMIXER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PULSEMIXER,GstPulseMixerClass))
#define GST_IS_PULSEMIXER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PULSEMIXER))
#define GST_IS_PULSEMIXER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PULSEMIXER))

typedef struct _GstPulseMixer GstPulseMixer;
typedef struct _GstPulseMixerClass GstPulseMixerClass;

struct _GstPulseMixer
{
  GstElement parent;

  gchar *server, *device;

  GstPulseMixerCtrl *mixer;
  GstPulseProbe *probe;
};

struct _GstPulseMixerClass
{
  GstElementClass parent_class;
};

GType gst_pulsemixer_get_type (void);

G_END_DECLS

#endif /* __GST_PULSEMIXER_H__ */
