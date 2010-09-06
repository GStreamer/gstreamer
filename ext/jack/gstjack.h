/* GStreamer
 * Copyright (C) 2006 Wim Taymans <wim@fluendo.com>
 *
 * gstjack.h:
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

#ifndef _GST_JACK_H_
#define _GST_JACK_H_


/**
 * GstJackConnect:
 * @GST_JACK_CONNECT_NONE: Don't automatically connect to physical ports.
 *     In this mode, the element will accept any number of input channels and will
 *     create (but not connect) an output port for each channel.
 * @GST_JACK_CONNECT_AUTO: In this mode, the element will try to connect each
 *     output port to a random physical jack input pin. The sink will
 *     expose the number of physical channels on its pad caps.
 * @GST_JACK_CONNECT_AUTO_FORCED: In this mode, the element will try to connect each
 *     output port to a random physical jack input pin. The  element will accept any number
 *     of input channels.
 *
 * Specify how the output ports will be connected.
 */

typedef enum {
  GST_JACK_CONNECT_NONE,
  GST_JACK_CONNECT_AUTO,
  GST_JACK_CONNECT_AUTO_FORCED
} GstJackConnect;

typedef jack_default_audio_sample_t sample_t;

#define GST_TYPE_JACK_CONNECT (gst_jack_connect_get_type())
#define GST_TYPE_JACK_CLIENT  (gst_jack_client_get_type ())

GType gst_jack_client_get_type(void);
GType gst_jack_connect_get_type(void);

#endif  // _GST_JACK_H_
