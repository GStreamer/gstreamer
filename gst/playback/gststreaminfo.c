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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include "gststreaminfo.h"

/* props */
enum
{
  ARG_0,
  ARG_PAD,
  ARG_TYPE,
  ARG_DECODER,
};

/* signals */
enum
{
  LAST_SIGNAL
};

#define GST_TYPE_STREAM_TYPE (gst_stream_type_get_type())
static GType
gst_stream_type_get_type (void)
{
  static GType stream_type_type = 0;
  static GEnumValue stream_type[] = {
    {GST_STREAM_TYPE_UNKNOWN, "GST_STREAM_TYPE_UNKNOWN", "Unknown stream"},
    {GST_STREAM_TYPE_AUDIO, "GST_STREAM_TYPE_AUDIO", "Audio stream"},
    {GST_STREAM_TYPE_VIDEO, "GST_STREAM_TYPE_VIDEO", "Video stream"},
    {0, NULL, NULL},
  };

  if (!stream_type_type) {
    stream_type_type = g_enum_register_static ("GstStreamType", stream_type);
  }
  return stream_type_type;
}

static void gst_stream_info_class_init (GstStreamInfoClass * klass);
static void gst_stream_info_init (GstStreamInfo * stream_info);
static void gst_stream_info_dispose (GObject * object);

static void gst_stream_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_stream_info_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static GObjectClass *parent_class;

//static guint gst_stream_info_signals[LAST_SIGNAL] = { 0 };

GType
gst_stream_info_get_type (void)
{
  static GType gst_stream_info_type = 0;

  if (!gst_stream_info_type) {
    static const GTypeInfo gst_stream_info_info = {
      sizeof (GstStreamInfoClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_stream_info_class_init,
      NULL,
      NULL,
      sizeof (GstStreamInfo),
      0,
      (GInstanceInitFunc) gst_stream_info_init,
      NULL
    };

    gst_stream_info_type = g_type_register_static (G_TYPE_OBJECT,
        "GstStreamInfo", &gst_stream_info_info, 0);
  }

  return gst_stream_info_type;
}

static void
gst_stream_info_class_init (GstStreamInfoClass * klass)
{
  GObjectClass *gobject_klass;

  gobject_klass = (GObjectClass *) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

  gobject_klass->set_property = gst_stream_info_set_property;
  gobject_klass->get_property = gst_stream_info_get_property;

  g_object_class_install_property (gobject_klass, ARG_PAD,
      g_param_spec_object ("pad", "Pad", "Source Pad of the stream",
          GST_TYPE_PAD, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_TYPE,
      g_param_spec_enum ("type", "Type", "Type of the stream",
          GST_TYPE_STREAM_TYPE, GST_STREAM_TYPE_UNKNOWN, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_DECODER,
      g_param_spec_string ("decoder", "Decoder",
          "The decoder used to decode the stream", NULL, G_PARAM_READABLE));

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_stream_info_dispose);
}


static void
gst_stream_info_init (GstStreamInfo * stream_info)
{
  stream_info->pad = NULL;
  stream_info->type = GST_STREAM_TYPE_UNKNOWN;
  stream_info->decoder = NULL;
}

GstStreamInfo *
gst_stream_info_new (GstPad * pad, GstStreamType type, gchar * decoder)
{
  GstStreamInfo *info;

  info = g_object_new (GST_TYPE_STREAM_INFO, NULL);

  gst_object_ref (GST_OBJECT (pad));
  info->pad = pad;
  info->type = type;
  info->decoder = g_strdup (decoder);

  return info;
}

static void
gst_stream_info_dispose (GObject * object)
{
  GstStreamInfo *stream_info;

  stream_info = GST_STREAM_INFO (object);

  gst_object_unref (GST_OBJECT (stream_info->pad));
  stream_info->pad = NULL;
  stream_info->type = GST_STREAM_TYPE_UNKNOWN;
  g_free (stream_info->decoder);

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}


static void
gst_stream_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstStreamInfo *stream_info;

  g_return_if_fail (GST_IS_STREAM_INFO (object));

  stream_info = GST_STREAM_INFO (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_stream_info_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstStreamInfo *stream_info;

  g_return_if_fail (GST_IS_STREAM_INFO (object));

  stream_info = GST_STREAM_INFO (object);

  switch (prop_id) {
    case ARG_PAD:
      g_value_set_object (value, stream_info->pad);
      break;
    case ARG_TYPE:
      g_value_set_enum (value, stream_info->type);
      break;
    case ARG_DECODER:
      g_value_set_string (value, stream_info->decoder);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
