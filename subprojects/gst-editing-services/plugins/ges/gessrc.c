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
 * SECTION:element-gessrc
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
#include <ges/ges.h>

#include "gesbasebin.h"

GST_DEBUG_CATEGORY_STATIC (gessrc);
#define GST_CAT_DEFAULT gessrc

G_DECLARE_FINAL_TYPE (GESSrc, ges_src, GES, SRC, GESBaseBin);
struct _GESSrc
{
  GESBaseBin parent;

  gchar *uri;
};
#define GES_SRC(obj) ((GESSrc*) obj)

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
  GESTimeline *timeline = ges_base_bin_get_timeline (GES_BASE_BIN (self));

  GST_OBJECT_LOCK (self);
  if (self->uri) {
    gchar *uri = g_strdup (self->uri);

    GST_OBJECT_UNLOCK (self);

    return uri;
  }
  GST_OBJECT_UNLOCK (self);

  return ges_command_line_formatter_get_timeline_uri (timeline);
}

static gboolean
ges_src_uri_set_uri (GstURIHandler * handler, const gchar * uristr,
    GError ** error)
{
  gboolean res = FALSE;
  GESSrc *self = GES_SRC (handler);
  GstUri *uri = gst_uri_from_string (uristr);
  GESProject *project = NULL;
  GESTimeline *timeline = NULL;

  if (!gst_uri_get_path (uri)) {
    GST_INFO_OBJECT (handler, "User need to specify the timeline");
    res = TRUE;
    goto done;
  }

  project = ges_project_new (uristr);
  timeline = (GESTimeline *) ges_asset_extract (GES_ASSET (project), NULL);

  if (timeline)
    res = ges_base_bin_set_timeline (GES_BASE_BIN (handler), timeline);

done:
  gst_uri_unref (uri);
  gst_clear_object (&project);

  GST_OBJECT_LOCK (handler);
  g_free (self->uri);
  self->uri = g_strdup (uristr);
  GST_OBJECT_UNLOCK (handler);

  return res;
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

G_DEFINE_TYPE_WITH_CODE (GESSrc, ges_src, ges_base_bin_get_type (),
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, ges_src_uri_handler_init));

static void
ges_src_class_init (GESSrcClass * self_class)
{
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (self_class);

  GST_DEBUG_CATEGORY_INIT (gessrc, "gessrc", 0, "ges src element");
  gst_element_class_set_static_metadata (gstelement_klass,
      "GStreamer Editing Services based 'source'",
      "Codec/Source/Editing",
      "Source for GESTimeline.", "Thibault Saunier <tsaunier@igalia.com");
}

static void
ges_src_init (GESSrc * self)
{
  SUPRESS_UNUSED_WARNING (GES_SRC);
  SUPRESS_UNUSED_WARNING (GES_IS_SRC);
#if defined(g_autoptr)
  SUPRESS_UNUSED_WARNING (glib_autoptr_cleanup_GESSrc);
#endif
}
