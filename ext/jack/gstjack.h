/* -*- Mode: C; c-basic-offset: 4 -*- */
/*
    Copyright (C) 2002 Andy Wingo <wingo@pobox.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __GST_JACK_H__
#define __GST_JACK_H__

#include <jack/jack.h>
#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

//#define JACK_DEBUG(str, a...) g_message (str, ##a)
#define JACK_DEBUG(str, a...)

#define GST_JACK(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_JACK, GstJack)
#define GST_JACK_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_JACK, GstJackClass)
#define GST_IS_JACK(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_JACK)
#define GST_IS_JACK_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_JACK)
#define GST_TYPE_JACK gst_jack_get_type()

#define GST_JACK_SINK(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_JACK_SINK, GstJack)
#define GST_JACK_SINK_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_JACK_SINK, GstJackClass)
#define GST_IS_JACK_SINK(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_JACK_SINK)
#define GST_IS_JACK_SINK_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_JACK_SINK)
#define GST_TYPE_JACK_SINK gst_jack_sink_get_type()

#define GST_JACK_SRC(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_JACK_SRC, GstJack)
#define GST_JACK_SRC_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_JACK_SRC, GstJackClass)
#define GST_IS_JACK_SRC(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_JACK_SRC)
#define GST_IS_JACK_SRC_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_JACK_SRC)
#define GST_TYPE_JACK_SRC gst_jack_src_get_type()

#define GST_JACK_BIN(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_JACK_BIN, GstJackBin)
#define GST_JACK_BIN_CLASS(klass) G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_JACK_BIN, GstJackClass)
#define GST_IS_JACK_BIN(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_JACK_BIN)
#define GST_IS_JACK_BIN_CLASS(klass) G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_JACK_BIN)
#define GST_TYPE_JACK_BIN gst_jack_bin_get_type()

#define GST_JACK_PAD(l) ((GstJackPad*)l->data)	/* l is a GList */


typedef struct _GstJack GstJack;
typedef struct _GstJackClass GstJackClass;
typedef struct _GstJackBin GstJackBin;
typedef struct _GstJackBinClass GstJackBinClass;
typedef GstJack GstJackSink;
typedef GstJackClass GstJackSinkClass;
typedef GstJack GstJackSrc;
typedef GstJackClass GstJackSrcClass;


enum
{
  GST_JACK_OPEN = GST_BIN_FLAG_LAST,
  GST_JACK_ACTIVE,
  GST_JACK_FLAG_LAST = GST_BIN_FLAG_LAST + 3,
};


typedef jack_default_audio_sample_t sample_t;

typedef struct
{
  GstPad *pad;
  void *data;
  const gchar *name;
  const gchar *peer_name;
  jack_port_t *port;
} GstJackPad;

struct _GstJack
{
  GstElement element;

  /* list of GstJackPads */
  GList *pads;

  /* for convenience */
  GstPadDirection direction;

  gchar *port_name_prefix;

  GstJackBin *bin;
};

struct _GstJackClass
{
  GstElementClass parent_class;
};

struct _GstJackBin
{
  GstBin bin;

  jack_client_t *client;
  gint default_new_port_number;

  /* lists of GstJackPads */
  GList *sink_pads;
  GList *src_pads;

  gchar *client_name;

  guint rate;
  jack_nframes_t nframes;
};

struct _GstJackBinClass
{
  GstBinClass parent_class;
};


GType gst_jack_get_type (void);
GType gst_jack_bin_get_type (void);
GType gst_jack_sink_get_type (void);
GType gst_jack_src_get_type (void);


#endif /* __GST_JACK_H__ */
