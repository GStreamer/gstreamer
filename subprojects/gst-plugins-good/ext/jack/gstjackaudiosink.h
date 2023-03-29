/* GStreamer
 * Copyright (C) 2006 Wim Taymans <wim@fluendo.com>
 *
 * gstjacksink.h:
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

#ifndef __GST_JACK_AUDIO_SINK_H__
#define __GST_JACK_AUDIO_SINK_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiobasesink.h>

#include "gstjack.h"
#include "gstjackaudioclient.h"
#include "gstjackloader.h"

G_BEGIN_DECLS

#define GST_TYPE_JACK_AUDIO_SINK (gst_jack_audio_sink_get_type())
G_DECLARE_FINAL_TYPE (GstJackAudioSink, gst_jack_audio_sink,
    GST, JACK_AUDIO_SINK, GstAudioBaseSink)

/**
 * GstJackAudioSink:
 *
 * Opaque #GstJackAudioSink.
 */
struct _GstJackAudioSink {
  GstAudioBaseSink element;

  /*< private >*/
  /* cached caps */
  GstCaps         *caps;

  /* properties */
  GstJackConnect   connect;
  gchar           *server;
  jack_client_t   *jclient;
  gchar           *client_name;
  gchar           *port_pattern;
  guint            transport;
  gboolean         low_latency;
  gchar           *port_names;

  /* our client */
  GstJackAudioClient *client;

  /* our ports */
  jack_port_t    **ports;
  int              port_count;
  sample_t       **buffers;
};

G_END_DECLS

#endif /* __GST_JACK_AUDIO_SINK_H__ */
