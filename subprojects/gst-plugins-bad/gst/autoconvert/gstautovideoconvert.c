/* GStreamer
 * Copyright 2010 ST-Ericsson SA
 *  @author: Benjamin Gaignard <benjamin.gaignard@stericsson.com>
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
/*
 * test autovideoconvert:
 * if rgb2bayer is present
 * gst-launch-1.0 videotestsrc num-buffers=2 ! "video/x-raw,width=100,height=100,framerate=10/1" ! autovideoconvert ! "video/x-bayer,width=100,height=100,format=bggr,framerate=10/1" ! fakesink -v
 * if bayer2rgb is present
 * gst-launch-1.0 videotestsrc num-buffers=2 ! "video/x-bayer,width=100,height=100,format=bggr,framerate=10/1" ! autovideoconvert ! "video/x-raw,width=100,height=100,framerate=10/1" ! fakesink -v
 * test with videoconvert
 * gst-launch-1.0 videotestsrc num-buffers=2 ! "video/x-raw,format=RGBx,width=100,height=100,framerate=10/1" ! autovideoconvert ! "video/x-raw,format=RGB16,width=100,height=100,framerate=10/1" ! fakesink -v
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstautovideoconvert.h"

GST_DEBUG_CATEGORY (autovideoconvert_debug);
#define GST_CAT_DEFAULT (autovideoconvert_debug)

struct _GstAutoVideoConvert
{
  GstAutoConvert parent;
};

G_DEFINE_TYPE (GstAutoVideoConvert, gst_auto_video_convert,
    GST_TYPE_AUTO_CONVERT);

GST_ELEMENT_REGISTER_DEFINE (autovideoconvert, "autovideoconvert",
    GST_RANK_NONE, gst_auto_video_convert_get_type ());

static gboolean
gst_auto_video_convert_element_filter (GstPluginFeature * feature,
    GstAutoVideoConvert * autovideoconvert)
{
  const gchar *klass;

  /* we only care about element factories */
  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  if (!g_strcmp0 (GST_OBJECT_NAME (feature), "autovideoconvert"))
    return FALSE;

  if (gst_plugin_feature_get_rank (feature) <= GST_RANK_NONE)
    return FALSE;

  klass = gst_element_factory_get_metadata (GST_ELEMENT_FACTORY_CAST (feature),
      GST_ELEMENT_METADATA_KLASS);
  /* only select color space converter */
  if (strstr (klass, "Colorspace") && strstr (klass, "Scaler") &&
      strstr (klass, "Converter") && strstr (klass, "Video")) {
    GST_DEBUG_OBJECT (autovideoconvert,
        "gst_auto_video_convert_element_filter found %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE_CAST (feature)));
    return TRUE;
  }
  return FALSE;
}

typedef struct
{
  const gchar *name;
  const gchar *bindesc;
  GstRank rank;
} KnownBin;

#define GST_AUTOCONVERT_WNBIN_QUARK g_quark_from_static_string("autovideoconvert-wn-bin-quark")
static void
gst_auto_convert_wn_bin_class_init (GstBinClass * class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (class);
  KnownBin *b = g_type_get_qdata (G_OBJECT_CLASS_TYPE (class),
      GST_AUTOCONVERT_WNBIN_QUARK);
  gchar **factory_names = g_strsplit (b->bindesc, " ! ", -1);
  gint nfactories = 0;
  GstElementFactory *factory;
  GstStaticPadTemplate *template = NULL;

  for (nfactories = 0; factory_names[nfactories + 1]; nfactories++);

  factory = gst_element_factory_find (factory_names[0]);
  for (GList * tmp =
      (GList *) gst_element_factory_get_static_pad_templates (factory); tmp;
      tmp = tmp->next) {
    if (((GstStaticPadTemplate *) tmp->data)->direction == GST_PAD_SINK) {
      template = tmp->data;
      break;
    }
  }
  gst_element_class_add_static_pad_template (element_class, template);

  template = NULL;
  factory = gst_element_factory_find (factory_names[nfactories]);
  for (GList * tmp =
      (GList *) gst_element_factory_get_static_pad_templates (factory); tmp;
      tmp = tmp->next) {
    if (((GstStaticPadTemplate *) tmp->data)->direction == GST_PAD_SRC) {
      template = tmp->data;
      break;
    }
  }
  g_assert (template);
  gst_element_class_add_static_pad_template (element_class, template);

  gst_element_class_set_metadata (element_class, b->name,
      "Scaler/Colorspace/Converter/Video", b->name,
      "Thibault Saunier <tsaunier@igalia.com>");

  g_strfreev (factory_names);
}

static void
gst_auto_convert_wn_bin_init (GstBin * self)
{
  KnownBin *b =
      g_type_get_qdata (G_OBJECT_TYPE (self), GST_AUTOCONVERT_WNBIN_QUARK);
  GError *err = NULL;
  GstElement *subbin = gst_parse_bin_from_description (b->bindesc, TRUE, &err);

  if (err)
    g_error ("%s couldn't be built?!: %s", b->name, err->message);

  gst_bin_add (self, subbin);
  gst_element_add_pad (GST_ELEMENT (self), gst_ghost_pad_new ("sink",
          subbin->sinkpads->data));
  gst_element_add_pad (GST_ELEMENT (self), gst_ghost_pad_new ("src",
          subbin->srcpads->data));
}

static void
register_well_known_bins (void)
{
  GTypeInfo typeinfo = {
    sizeof (GstBinClass),
    (GBaseInitFunc) NULL,
    NULL,
    (GClassInitFunc) gst_auto_convert_wn_bin_class_init,
    NULL,
    NULL,
    sizeof (GstBin),
    0,
    (GInstanceInitFunc) gst_auto_convert_wn_bin_init,
  };
  /* *INDENT-OFF* */
  const KnownBin subbins[] = {
    {
      .name = "auto+bayer2rgbcolorconvert",
      .bindesc = "bayer2rgb ! videoconvertscale ! videoflip method=automatic ! videoconvertscale",
      .rank = GST_RANK_SECONDARY,
    },
    {
      .name = "auto+colorconvertrgb2bayer",
      .bindesc = "videoconvertscale ! videoflip method=automatic ! videoconvertscale ! rgb2bayer",
      .rank = GST_RANK_SECONDARY,
    },
    {
      /* Fallback to only videoconvertscale if videoflip is not available */
      .name = "auto+colorconvert-fallback",
      .bindesc = "videoconvertscale",
      .rank = GST_RANK_MARGINAL,
    },
    {
      .name = "auto+colorconvert",
      .bindesc = "videoconvertscale ! videoflip method=automatic ! videoconvertscale",
      .rank = GST_RANK_SECONDARY,
    },
    {
      .name = "auto+glcolorconvert",
      .bindesc = "glcolorconvert ! glcolorscale  ! glvideoflip method=automatic ! glcolorconvert",
      .rank = GST_RANK_PRIMARY,
    },
  };
  /* *INDENT-ON* */

  for (gint i = 0; i < G_N_ELEMENTS (subbins); i++) {
    GType type = 0;
    KnownBin *b = NULL;
    GstElement *subbin = gst_parse_launch (subbins[i].bindesc, NULL);

    if (!subbin) {
      GST_INFO ("Ignoring possible Converting scaler: %s '%s'", subbins[i].name,
          subbins[i].bindesc);
      continue;
    }

    gst_object_unref (subbin);

    type = g_type_register_static (GST_TYPE_BIN, subbins[i].name, &typeinfo, 0);
    b = g_malloc0 (sizeof (KnownBin));
    b->name = g_strdup (subbins[i].name);
    b->bindesc = g_strdup (subbins[i].bindesc);
    b->rank = subbins[i].rank;

    g_type_set_qdata (type, GST_AUTOCONVERT_WNBIN_QUARK, b);

    if (!gst_element_register (NULL, b->name, b->rank, type)) {
      g_warning ("Failed to register %s", "autoglcolorconverter");
    }
  }
}

static GList *
gst_auto_video_convert_create_factory_list (GstAutoConvert * autoconvert)
{
  GList *result = NULL;
  static gpointer registered_well_known_bins = FALSE;

  if (g_once_init_enter (&registered_well_known_bins)) {
    register_well_known_bins ();
    g_once_init_leave (&registered_well_known_bins, (gpointer) TRUE);
  }

  /* get the feature list using the filter */
  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) gst_auto_video_convert_element_filter,
      FALSE, autoconvert);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);

  return result;
}

static void
gst_auto_video_convert_class_init (GstAutoVideoConvertClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  ((GstAutoConvertClass *) klass)->load_factories =
      gst_auto_video_convert_create_factory_list;
  GST_DEBUG_CATEGORY_INIT (autovideoconvert_debug, "autovideoconvert", 0,
      "Auto color space converter");

  gst_element_class_set_static_metadata (gstelement_class,
      "Select color space converter and scalers based on caps",
      "Bin/Colorspace/Scale/Video/Converter",
      "Selects the right color space converter based on the caps",
      "Benjamin Gaignard <benjamin.gaignard@stericsson.com>");
}

static void
gst_auto_video_convert_init (GstAutoVideoConvert * autovideoconvert)
{
}
