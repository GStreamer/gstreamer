/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_MASSINK_H__
#define __GST_MASSINK_H__

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mas/mas.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_MASSINK \
  (gst_massink_get_type())
#define GST_MASSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MASSINK,GstMassink))
#define GST_MASSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MASSINK,GstMassink))
#define GST_IS_MASSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MASSINK))
#define GST_IS_MASSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MASSINK))

typedef enum {
  GST_MASSINK_OPEN            = GST_ELEMENT_FLAG_LAST,
  GST_MASSINK_FLAG_LAST       = GST_ELEMENT_FLAG_LAST+2,
} GstMasSinkFlags;

typedef struct _GstMassink GstMassink;
typedef struct _GstMassinkClass GstMassinkClass;

struct _GstMassink {
  GstElement element;

  GstPad *sinkpad;

  gboolean mute;
  gint depth;
  gint channels;
  gint frequency;
  gint endianness;

  /* MAS Data Structures */
  mas_channel_t audio_channel;
  mas_port_t    mix_sink;
  mas_port_t  	channelconv_source, channelconv_sink;
  mas_port_t    srate_source, srate_sink;
  mas_port_t    audio_source, audio_sink;
  mas_port_t    endian_sink, endian_source;
  mas_port_t    squant_sink, squant_source;
  mas_port_t    open_source; /* (!) */
  mas_device_t  channelconv;
  mas_device_t  endian;
  mas_device_t  srate;
  mas_device_t  squant;
 
  struct mas_data data; 
  GstClock *clock;
};

struct _GstMassinkClass {
  GstElementClass parent_class;
};

GType gst_massink_get_type(void);


#define MASSINK_DEFAULT_DEPTH 	  16
#define MASSINK_DEFAULT_CHANNELS  2
#define MASSINK_DEFAULT_FREQUENCY 44100
#define MASSINK_BUFFER_SIZE  	  10240


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_MASSINK_H__ */
