/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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

/*
 * This file was (probably) generated from gstnavseek.c,
 * gstnavseek.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gstnavseek.h>
#include <string.h>
#include <math.h>

/* GstNavSeek signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SEEKOFFSET
      /* FILL ME */
};

GstStaticPadTemplate navseek_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstStaticPadTemplate navseek_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_navseek_base_init (gpointer g_class);
static void gst_navseek_class_init (gpointer g_class, gpointer class_data);
static void gst_navseek_init (GTypeInstance * instance, gpointer g_class);

static gboolean gst_navseek_handle_src_event (GstPad * pad, GstEvent * event);
static void gst_navseek_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_navseek_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_navseek_chain (GstPad * pad, GstData * _data);

static GType
gst_navseek_get_type (void)
{
  static GType navseek_type = 0;

  if (!navseek_type) {
    static const GTypeInfo navseek_info = {
      sizeof (GstNavSeekClass),
      gst_navseek_base_init,
      NULL,
      gst_navseek_class_init,
      NULL,
      NULL,
      sizeof (GstNavSeek),
      0,
      gst_navseek_init,
    };

    navseek_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstNavSeek", &navseek_info, 0);
  }
  return navseek_type;
}

static void
gst_navseek_base_init (gpointer g_class)
{
  static GstElementDetails navseek_details =
      GST_ELEMENT_DETAILS ("Seek based on left-right arrows",
      "Filter/Video",
      "Seek based on navigation keys left-right",
      "Jan Schmidt <thaytan@mad.scientist.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&navseek_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&navseek_src_template));

  gst_element_class_set_details (element_class, &navseek_details);
}

static void
gst_navseek_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (g_class);

  g_object_class_install_property (G_OBJECT_CLASS (g_class),
      ARG_SEEKOFFSET, g_param_spec_double ("seek-offset", "Seek Offset",
          "Time in seconds to seek by", 0.0, G_MAXDOUBLE, 5.0,
          G_PARAM_READWRITE));

  gobject_class->set_property = gst_navseek_set_property;
  gobject_class->get_property = gst_navseek_get_property;
}

static void
gst_navseek_init (GTypeInstance * instance, gpointer g_class)
{
  GstNavSeek *navseek = GST_NAVSEEK (instance);

  navseek->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&navseek_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (navseek), navseek->sinkpad);
  gst_pad_set_chain_function (navseek->sinkpad, gst_navseek_chain);
  gst_pad_set_link_function (navseek->sinkpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (navseek->sinkpad, gst_pad_proxy_getcaps);

  navseek->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&navseek_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (navseek), navseek->srcpad);
  gst_pad_set_link_function (navseek->srcpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (navseek->srcpad, gst_pad_proxy_getcaps);
  gst_pad_set_event_function (navseek->srcpad, gst_navseek_handle_src_event);

  navseek->seek_offset = 5.0;
}

static void
gst_navseek_seek (GstNavSeek * navseek, gint64 offset)
{
  /* Query for the current time then attempt to set to time + offset */
  gint64 peer_value;
  GstFormat peer_format = GST_FORMAT_TIME;

  if (gst_pad_query (gst_pad_get_peer (navseek->sinkpad),
          GST_QUERY_POSITION, &peer_format, &peer_value)) {
    if (peer_format != GST_FORMAT_TIME)
      return;

    peer_value += offset;
    if (peer_value < 0)
      peer_value = 0;

    gst_element_seek (GST_ELEMENT (navseek),
        GST_SEEK_METHOD_SET | GST_FORMAT_TIME | GST_SEEK_FLAG_ACCURATE |
        GST_SEEK_FLAG_FLUSH, peer_value);
  }
}

static gboolean
gst_navseek_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstNavSeek *navseek;

  navseek = GST_NAVSEEK (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      /* Check for a keyup and convert left/right to a seek event */
    {
      GstStructure *structure;
      const gchar *event_type;

      structure = event->event_data.structure.structure;
      event_type = gst_structure_get_string (structure, "event");

      g_return_val_if_fail (event != NULL, FALSE);

      if (strcmp (event_type, "key-press") == 0) {
        const char *key = gst_structure_get_string (structure, "key");

        g_assert (key != NULL);
        if (strcmp (key, "Left") == 0) {
          /* Seek backward by 5 secs */
          gst_navseek_seek (navseek, -1.0 * navseek->seek_offset * GST_SECOND);
        } else if (strcmp (key, "Right") == 0) {
          /* Seek forward */
          gst_navseek_seek (navseek, navseek->seek_offset * GST_SECOND);
        }
      } else {
        break;
      }
      gst_event_unref (event);
      event = NULL;
    }
      break;
    default:
      break;
  }
  if (event) {
    return gst_pad_send_event (gst_pad_get_peer (navseek->sinkpad), event);
  }
  return TRUE;
}

static void
gst_navseek_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNavSeek *src;

  g_return_if_fail (GST_IS_NAVSEEK (object));
  src = GST_NAVSEEK (object);

  switch (prop_id) {
    case ARG_SEEKOFFSET:
      src->seek_offset = g_value_get_double (value);
      break;
    default:
      break;
  }
}

static void
gst_navseek_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNavSeek *src;

  g_return_if_fail (GST_IS_NAVSEEK (object));
  src = GST_NAVSEEK (object);

  switch (prop_id) {
    case ARG_SEEKOFFSET:
      g_value_set_double (value, src->seek_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_navseek_chain (GstPad * pad, GstData * _data)
{
  GstNavSeek *navseek;

  navseek = GST_NAVSEEK (gst_pad_get_parent (pad));
  gst_pad_push (navseek->srcpad, _data);
}

gboolean
gst_navseek_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "navseek", GST_RANK_NONE,
      GST_TYPE_NAVSEEK);
}
