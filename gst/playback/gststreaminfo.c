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

GST_DEBUG_CATEGORY_STATIC (gst_streaminfo_debug);
#define GST_CAT_DEFAULT gst_streaminfo_debug

/* props */
enum
{
  ARG_0,
  ARG_PAD,
  ARG_TYPE,
  ARG_DECODER,
  ARG_MUTE,
  ARG_CAPS,
  ARG_LANG_CODE,
  ARG_CODEC
};

/* signals */
enum
{
  SIGNAL_MUTED,
  LAST_SIGNAL
};

static guint gst_stream_info_signals[LAST_SIGNAL] = { 0 };

#define GST_TYPE_STREAM_TYPE (gst_stream_type_get_type())
static GType
gst_stream_type_get_type (void)
{
  static GType stream_type_type = 0;
  static const GEnumValue stream_type[] = {
    {GST_STREAM_TYPE_UNKNOWN, "Unknown stream", "unknown"},
    {GST_STREAM_TYPE_AUDIO, "Audio stream", "audio"},
    {GST_STREAM_TYPE_VIDEO, "Video stream", "video"},
    {GST_STREAM_TYPE_TEXT, "Text stream", "text"},
    {GST_STREAM_TYPE_SUBPICTURE, "Subpicture stream", "subpicture"},
    {GST_STREAM_TYPE_ELEMENT,
        "Stream handled by element", "element"},
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

static void stream_info_change_state (GstElement * element,
    gint old_state, gint new_state, gpointer data);

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

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_stream_info_set_property;
  gobject_klass->get_property = gst_stream_info_get_property;

  g_object_class_install_property (gobject_klass, ARG_PAD,
      g_param_spec_object ("object", "object",
          "Source Pad or object of the stream", GST_TYPE_OBJECT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_TYPE,
      g_param_spec_enum ("type", "Type", "Type of the stream",
          GST_TYPE_STREAM_TYPE, GST_STREAM_TYPE_UNKNOWN,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_DECODER,
      g_param_spec_string ("decoder", "Decoder",
          "The decoder used to decode the stream", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute or unmute this stream", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_CAPS,
      g_param_spec_boxed ("caps", "Capabilities",
          "Capabilities (or type) of this stream", GST_TYPE_CAPS,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_LANG_CODE,
      g_param_spec_string ("language-code", "Language code",
          "Language code for this stream, conforming to ISO-639-1", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_klass, ARG_CODEC,
      g_param_spec_string ("codec", "Codec", "Codec used to encode the stream",
          NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_stream_info_signals[SIGNAL_MUTED] =
      g_signal_new ("muted", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstStreamInfoClass, muted), NULL, NULL,
      gst_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  gobject_klass->dispose = gst_stream_info_dispose;

  GST_DEBUG_CATEGORY_INIT (gst_streaminfo_debug, "streaminfo", 0,
      "Playbin Stream Info");
}


static void
gst_stream_info_init (GstStreamInfo * stream_info)
{
  stream_info->object = NULL;
  stream_info->origin = NULL;
  stream_info->type = GST_STREAM_TYPE_UNKNOWN;
  stream_info->decoder = NULL;
  stream_info->mute = FALSE;
  stream_info->caps = NULL;
}

static gboolean
cb_probe (GstPad * pad, GstEvent * e, gpointer user_data)
{
  GstStreamInfo *info = user_data;

  if (GST_EVENT_TYPE (e) == GST_EVENT_TAG) {
    gchar *codec, *lang;
    GstTagList *list;

    gst_event_parse_tag (e, &list);

    if (info->type != GST_STREAM_TYPE_AUDIO &&
        gst_tag_list_get_string (list, GST_TAG_VIDEO_CODEC, &codec)) {
      g_free (info->codec);
      info->codec = codec;
      GST_LOG_OBJECT (pad, "codec = %s (video)", codec);
      g_object_notify (G_OBJECT (info), "codec");
    } else if (info->type != GST_STREAM_TYPE_VIDEO &&
        gst_tag_list_get_string (list, GST_TAG_AUDIO_CODEC, &codec)) {
      g_free (info->codec);
      info->codec = codec;
      GST_LOG_OBJECT (pad, "codec = %s (audio)", codec);
      g_object_notify (G_OBJECT (info), "codec");
    } else if (gst_tag_list_get_string (list, GST_TAG_CODEC, &codec)) {
      g_free (info->codec);
      info->codec = codec;
      GST_LOG_OBJECT (pad, "codec = %s (generic)", codec);
      g_object_notify (G_OBJECT (info), "codec");
    }
    if (gst_tag_list_get_string (list, GST_TAG_LANGUAGE_CODE, &lang)) {
      g_free (info->langcode);
      info->langcode = lang;
      GST_LOG_OBJECT (pad, "language-code = %s", lang);
      g_object_notify (G_OBJECT (info), "language-code");
    }
  }

  return TRUE;
}

GstStreamInfo *
gst_stream_info_new (GstObject * object,
    GstStreamType type, const gchar * decoder, const GstCaps * caps)
{
  GstStreamInfo *info;

  info = g_object_new (GST_TYPE_STREAM_INFO, NULL);

  gst_object_ref (object);
  if (GST_IS_PAD (object)) {
    gst_pad_add_event_probe (GST_PAD_CAST (object),
        G_CALLBACK (cb_probe), info);
  }
  info->object = object;
  info->type = type;
  info->decoder = g_strdup (decoder);
  info->origin = object;
  if (caps) {
    info->caps = gst_caps_copy (caps);
  }

  return info;
}

static void
gst_stream_info_dispose (GObject * object)
{
  GstStreamInfo *stream_info;

  stream_info = GST_STREAM_INFO (object);

  if (stream_info->object) {
    GstElement *parent;

    parent = gst_pad_get_parent_element ((GstPad *)
        GST_PAD_CAST (stream_info->object));
    if (parent != NULL) {
      g_signal_handlers_disconnect_by_func (parent,
          (gpointer) stream_info_change_state, stream_info);
      gst_object_unref (parent);
    }

    gst_object_unref (stream_info->object);
    stream_info->object = NULL;
  }
  stream_info->origin = NULL;
  stream_info->type = GST_STREAM_TYPE_UNKNOWN;
  g_free (stream_info->decoder);
  stream_info->decoder = NULL;
  g_free (stream_info->langcode);
  stream_info->langcode = NULL;
  g_free (stream_info->codec);
  stream_info->codec = NULL;
  if (stream_info->caps) {
    gst_caps_unref (stream_info->caps);
    stream_info->caps = NULL;
  }

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}

static void
stream_info_change_state (GstElement * element,
    gint old_state, gint new_state, gpointer data)
{
  GstStreamInfo *stream_info = data;

  if (new_state == GST_STATE_PLAYING) {
    /* state change will annoy us */
    g_return_if_fail (stream_info->mute == TRUE);
    GST_DEBUG_OBJECT (stream_info, "Re-muting pads after state-change");
    //gst_pad_set_active_recursive (GST_PAD (stream_info->object), FALSE);
    g_warning ("FIXME");
  }
}

gboolean
gst_stream_info_set_mute (GstStreamInfo * stream_info, gboolean mute)
{
  g_return_val_if_fail (GST_IS_STREAM_INFO (stream_info), FALSE);

  if (stream_info->type == GST_STREAM_TYPE_ELEMENT) {
    g_warning ("cannot mute element stream");
    return FALSE;
  }

  if (mute != stream_info->mute) {
    /* nothing really happens here. it looks like gstplaybasebin installs a
     * buffer probe hat drops buffers when muting. but the this removes it self
     * after first call.
     */

    stream_info->mute = mute;
#if 0
    gst_pad_set_active ((GstPad *) GST_PAD_CAST (stream_info->object), !mute);
#endif
#if 0
    {
      GstElement *element;

      element = gst_pad_get_parent_element ((GstPad *)
          GST_PAD_CAST (stream_info->object));
      if (element) {
        if (mute) {
          g_signal_connect (element, "state-changed",
              G_CALLBACK (stream_info_change_state), stream_info);
        } else {
          g_signal_handlers_disconnect_by_func (element,
              G_CALLBACK (stream_info_change_state), stream_info);
        }
        gst_object_unref (element);
      }
    }
#endif
  }
  return TRUE;
}

gboolean
gst_stream_info_is_mute (GstStreamInfo * stream_info)
{
  g_return_val_if_fail (GST_IS_STREAM_INFO (stream_info), TRUE);

  return stream_info->mute;
}

static void
gst_stream_info_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstStreamInfo *stream_info;

  g_return_if_fail (GST_IS_STREAM_INFO (object));

  stream_info = GST_STREAM_INFO (object);

  switch (prop_id) {
    case ARG_MUTE:
      gst_stream_info_set_mute (stream_info, g_value_get_boolean (value));
      break;
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
      g_value_set_object (value, stream_info->object);
      break;
    case ARG_TYPE:
      g_value_set_enum (value, stream_info->type);
      break;
    case ARG_DECODER:
      g_value_set_string (value, stream_info->decoder);
      break;
    case ARG_MUTE:
      g_value_set_boolean (value, stream_info->mute);
      break;
    case ARG_CAPS:
      g_value_set_boxed (value, stream_info->caps);
      break;
    case ARG_LANG_CODE:
      g_value_set_string (value, stream_info->langcode);
      break;
    case ARG_CODEC:
      g_value_set_string (value, stream_info->codec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
