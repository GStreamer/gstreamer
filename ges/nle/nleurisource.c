/* Gnonlin
 * Copyright (C) <2005-2008> Edward Hervey <bilboed@bilboed.com>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "nle.h"
#include "nleurisource.h"

/**
 * SECTION:element-nleurisource
 *
 * NleURISource is a #NleSource which reads and decodes the contents
 * of a given file. The data in the file is decoded using any available
 * GStreamer plugins.
 */

static GstStaticPadTemplate nle_urisource_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (nleurisource);
#define GST_CAT_DEFAULT nleurisource

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (nleurisource, "nleurisource", GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin URI Source Element");
#define  nle_urisource_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (NleURISource, nle_urisource, NLE_TYPE_SOURCE,
    _do_init);

enum
{
  ARG_0,
  ARG_URI,
};

static gboolean nle_urisource_prepare (NleObject * object);

static void
nle_urisource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void
nle_urisource_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
nle_urisource_class_init (NleURISourceClass * klass)
{
  GObjectClass *gobject_class;
  NleObjectClass *nleobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  nleobject_class = (NleObjectClass *) klass;
  parent_class = g_type_class_ref (NLE_TYPE_SOURCE);

  gst_element_class_set_static_metadata (gstelement_class, "GNonLin URI Source",
      "Filter/Editor",
      "High-level URI Source element", "Edward Hervey <bilboed@bilboed.com>");

  gobject_class->set_property = GST_DEBUG_FUNCPTR (nle_urisource_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (nle_urisource_get_property);

  g_object_class_install_property (gobject_class, ARG_URI,
      g_param_spec_string ("uri", "Uri",
          "Uri of the file to use", NULL, G_PARAM_READWRITE));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&nle_urisource_src_template));

  nleobject_class->prepare = nle_urisource_prepare;
}

static void
nle_urisource_init (NleURISource * urisource)
{
  GstElement *decodebin = NULL;

  GST_OBJECT_FLAG_SET (urisource, NLE_OBJECT_SOURCE);

  /* We create a bin with source and decodebin within */
  decodebin =
      gst_element_factory_make ("uridecodebin", "internal-uridecodebin");
  g_object_set (decodebin, "expose-all-streams", FALSE, NULL);

  gst_bin_add (GST_BIN (urisource), decodebin);
}

static inline void
nle_urisource_set_uri (NleURISource * fs, const gchar * uri)
{
  g_object_set (NLE_SOURCE (fs)->element, "uri", uri, NULL);
}

static void
nle_urisource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  NleURISource *fs = (NleURISource *) object;

  switch (prop_id) {
    case ARG_URI:
      nle_urisource_set_uri (fs, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nle_urisource_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  NleURISource *fs = (NleURISource *) object;

  switch (prop_id) {
    case ARG_URI:
      g_object_get_property ((GObject *) NLE_SOURCE (fs)->element, "uri",
          value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static gboolean
nle_urisource_prepare (NleObject * object)
{
  NleSource *fs = (NleSource *) object;

  GST_DEBUG ("prepare");

  /* Set the caps on uridecodebin */
  if (!gst_caps_is_any (object->caps)) {
    GST_DEBUG_OBJECT (object, "Setting uridecodebin caps to %" GST_PTR_FORMAT,
        object->caps);
    g_object_set (fs->element, "caps", object->caps, NULL);
  }

  return NLE_OBJECT_CLASS (parent_class)->prepare (object);
}
