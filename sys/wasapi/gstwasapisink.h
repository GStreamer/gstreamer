/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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

#ifndef __GST_WASAPI_SINK_H__
#define __GST_WASAPI_SINK_H__

#include "gstwasapiutil.h"

G_BEGIN_DECLS

#define GST_TYPE_WASAPI_SINK \
  (gst_wasapi_sink_get_type ())
#define GST_WASAPI_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WASAPI_SINK, GstWasapiSink))
#define GST_WASAPI_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_WASAPI_SINK, GstWasapiSinkClass))
#define GST_IS_WASAPI_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WASAPI_SINK))
#define GST_IS_WASAPI_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_WASAPI_SINK))

typedef struct _GstWasapiSink      GstWasapiSink;
typedef struct _GstWasapiSinkClass GstWasapiSinkClass;

struct _GstWasapiSink
{
  GstAudioSink parent;

  GstAudioInfo info;

  IAudioClient * client;
  IAudioRenderClient * render_client;
  HANDLE event_handle;
};

struct _GstWasapiSinkClass
{
  GstAudioSinkClass parent_class;
};

GType gst_wasapi_sink_get_type (void);

G_END_DECLS

#endif /* __GST_WASAPI_SINK_H__ */

