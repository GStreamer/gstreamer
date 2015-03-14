/* GStreamer Editing Services GStreamer plugin
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gessrc.c
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
 *

 **
 * SECTION:gessrc
 * @short_description: A GstBin subclasses use to use GESTimeline
 * as sources inside any GstPipeline.
 * @see_also: #GESTimeline
 *
 * The gessrc is a bin that will simply expose the track src pads
 * and implements the GstUriHandler interface using a custom `ges://`
 * uri scheme.
 * 
 * NOTE: That to use it inside playbin and friends you **need** to
 * set gessrc::timeline property yourself.
 * 
 * Example with #playbin:
 * 
 * {{../../examples/c/gessrc.c}}
 * 
 * Example with #GstPlayer:
 * 
 * {{../../examples/python/gst-player.py}}
 **/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gessrc.h"

GST_DEBUG_CATEGORY_STATIC (gessrc);
#define GST_CAT_DEFAULT gessrc

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw(ANY)"));

static GstStaticPadTemplate audio_src_template =
    GST_STATIC_PAD_TEMPLATE ("audio_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw(ANY);"));

enum
{
  PROP_0,
  PROP_TIMELINE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static gboolean
ges_src_set_timeline (GESSrc * self, GESTimeline * timeline)
{
  GList *tmp;
  guint naudiopad = 0, nvideopad = 0;
  GstBin *sbin = GST_BIN (self);

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  if (self->timeline) {
    GST_FIXME_OBJECT (self, "Implement changing timeline support");

    return FALSE;
  }

  self->timeline = timeline;

  gst_bin_add (sbin, GST_ELEMENT (self->timeline));
  for (tmp = self->timeline->tracks; tmp; tmp = tmp->next) {
    GstPad *gpad;
    gchar *name = NULL;
    GstElement *queue;
    GESTrack *track = GES_TRACK (tmp->data);
    GstPad *tmppad, *pad =
        ges_timeline_get_pad_for_track (self->timeline, track);
    GstStaticPadTemplate *template;

    if (!pad) {
      GST_INFO_OBJECT (self, "No pad for track: %" GST_PTR_FORMAT, track);

      continue;
    }

    if (track->type == GES_TRACK_TYPE_AUDIO) {
      name = g_strdup_printf ("audio_%u", naudiopad++);
      template = &audio_src_template;
    } else if (track->type == GES_TRACK_TYPE_VIDEO) {
      name = g_strdup_printf ("video_%u", nvideopad++);
      template = &video_src_template;
    } else {
      GST_INFO_OBJECT (self, "Track type not handled: %" GST_PTR_FORMAT, track);
      continue;
    }

    queue = gst_element_factory_make ("queue", NULL);
    /* Add queues the same way as in GESPipeline */
    g_object_set (G_OBJECT (queue), "max-size-buffers", 0,
        "max-size-bytes", 0, "max-size-time", (gint64) 2 * GST_SECOND, NULL);
    gst_bin_add (GST_BIN (self), queue);

    tmppad = gst_element_get_static_pad (queue, "sink");
    if (gst_pad_link (pad, tmppad) != GST_PAD_LINK_OK) {
      GST_ERROR ("Could not link %s:%s and %s:%s",
          GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (tmppad));

      gst_object_unref (tmppad);
      gst_object_unref (queue);
      continue;
    }

    tmppad = gst_element_get_static_pad (queue, "src");
    gpad = gst_ghost_pad_new_from_template (name, tmppad,
        gst_static_pad_template_get (template));

    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (self), gpad);
  }

  gst_element_sync_state_with_parent (GST_ELEMENT (self->timeline));

  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
ges_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
ges_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "ges", NULL };

  return protocols;
}

static gchar *
ges_src_uri_get_uri (GstURIHandler * handler)
{
  GESSrc *self = GES_SRC (handler);

  return self->timeline ? g_strdup_printf ("ges://%s",
      GST_OBJECT_NAME (self->timeline)) : NULL;
}

static gboolean
ges_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  return TRUE;
}

static void
ges_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = ges_src_uri_get_type;
  iface->get_protocols = ges_src_uri_get_protocols;
  iface->get_uri = ges_src_uri_get_uri;
  iface->set_uri = ges_src_uri_set_uri;
}

G_DEFINE_TYPE_WITH_CODE (GESSrc, ges_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, ges_src_uri_handler_init));

static void
ges_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESSrc *self = GES_SRC (object);

  switch (property_id) {
    case PROP_TIMELINE:
      g_value_set_object (value, self->timeline);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESSrc *self = GES_SRC (object);

  switch (property_id) {
    case PROP_TIMELINE:
      ges_src_set_timeline (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_src_class_init (GESSrcClass * self_class)
{
  GObjectClass *gclass = G_OBJECT_CLASS (self_class);
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (self_class);

  GST_DEBUG_CATEGORY_INIT (gessrc, "gessrc", 0, "ges src element");

  gclass->get_property = ges_src_get_property;
  gclass->set_property = ges_src_set_property;

  /**
   * GESSrc:timeline:
   *
   * Timeline to use in this src.
   */
  properties[PROP_TIMELINE] = g_param_spec_object ("timeline", "Timeline",
      "Timeline to use in this src.",
      GES_TYPE_TIMELINE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gclass, PROP_LAST, properties);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&audio_src_template));
}

static void
ges_src_init (GESSrc * self)
{
}
