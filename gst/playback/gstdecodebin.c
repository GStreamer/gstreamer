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

/* generic templates */
GstStaticPadTemplate decoder_bin_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstStaticPadTemplate decoder_bin_src_template =
GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_decode_bin_debug);
#define GST_CAT_DEFAULT gst_decode_bin_debug

#define GST_TYPE_DECODE_BIN 		(gst_decode_bin_get_type())
#define GST_DECODE_BIN(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECODE_BIN,GstDecodeBin))
#define GST_DECODE_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECODE_BIN,GstDecodeBinClass))
#define GST_IS_DECODE_BIN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECODE_BIN))
#define GST_IS_DECODE_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECODE_BIN))

typedef struct _GstDecodeBin GstDecodeBin;
typedef struct _GstDecodeBinClass GstDecodeBinClass;

struct _GstDecodeBin
{
  GstBin bin;

  GstElement *typefind;

  gboolean threaded;
  GList *dynamics;

  GList *factories;
  gint numpads;

  GList *elements;

  guint have_type_id;
};

struct _GstDecodeBinClass
{
  GstBinClass parent_class;

  void (*new_stream) (GstElement * element, GstPad * pad, gboolean last);
  void (*unknown_type) (GstElement * element, GstCaps * caps);
};

/* props */
enum
{
  ARG_0,
  ARG_THREADED,
};

/* signals */
enum
{
  SIGNAL_NEW_STREAM,
  SIGNAL_UNKNOWN_TYPE,
  LAST_SIGNAL
};

typedef struct
{
  gint np_sig_id;
  gint nmp_sig_id;
  GstElement *element;
  GstDecodeBin *decode_bin;
}
GstDynamic;

static void gst_decode_bin_class_init (GstDecodeBinClass * klass);
static void gst_decode_bin_init (GstDecodeBin * decode_bin);
static void gst_decode_bin_dispose (GObject * object);

static void gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_decode_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstElementStateReturn gst_decode_bin_change_state (GstElement * element);

static void type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin);
static GstElement *try_to_link_1 (GstDecodeBin * decode_bin, GstPad * pad,
    GList * factories);
static void close_link (GstElement * element, GstDecodeBin * decode_bin);
static void close_pad_link (GstElement * element, GstPad * pad,
    GstCaps * caps, GstDecodeBin * decode_bin);

static GstElementClass *parent_class;
static guint gst_decode_bin_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails gst_decode_bin_details = {
  "Decoder Bin",
  "Generic/Bin/Decoder",
  "Autoplug and decode to raw media",
  "Wim Taymans <wim@fluendo.com>"
};


static GType
gst_decode_bin_get_type (void)
{
  static GType gst_decode_bin_type = 0;

  if (!gst_decode_bin_type) {
    static const GTypeInfo gst_decode_bin_info = {
      sizeof (GstDecodeBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_decode_bin_class_init,
      NULL,
      NULL,
      sizeof (GstDecodeBin),
      0,
      (GInstanceInitFunc) gst_decode_bin_init,
      NULL
    };

    gst_decode_bin_type =
        g_type_register_static (GST_TYPE_BIN, "GstDecodeBin",
        &gst_decode_bin_info, 0);
  }

  return gst_decode_bin_type;
}

static void
gst_decode_bin_class_init (GstDecodeBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_ref (gst_bin_get_type ());

  gobject_klass->set_property = gst_decode_bin_set_property;
  gobject_klass->get_property = gst_decode_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_THREADED,
      g_param_spec_boolean ("threaded", "Threaded", "Use threads",
          FALSE, G_PARAM_READWRITE));

  gst_decode_bin_signals[SIGNAL_NEW_STREAM] =
      g_signal_new ("new-stream", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDecodeBinClass, new_stream), NULL, NULL,
      gst_marshal_VOID__OBJECT_POINTER, G_TYPE_NONE, 2, G_TYPE_OBJECT,
      G_TYPE_BOOLEAN);
  gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE] =
      g_signal_new ("unknown-type", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, unknown_type),
      NULL, NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_CAPS);

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_decode_bin_dispose);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&decoder_bin_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_decode_bin_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_decode_bin_change_state);
}

static gboolean
gst_decode_bin_is_dynamic (GstDecodeBin * decode_bin)
{
  return decode_bin->dynamics != NULL;
}

static gboolean
gst_decode_bin_factory_filter (GstPluginFeature * feature,
    GstDecodeBin * decode_bin)
{
  guint rank;
  const gchar *klass;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  klass = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (feature));
  if (strstr (klass, "Demux") == NULL && strstr (klass, "Decoder") == NULL) {
    return FALSE;
  }

  rank = gst_plugin_feature_get_rank (feature);
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  return TRUE;
}

static gint
compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  return gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
}

static void
print_feature (GstPluginFeature * feature)
{
  GST_DEBUG ("%s\n", gst_plugin_feature_get_name (feature));
}

static void
gst_decode_bin_init (GstDecodeBin * decode_bin)
{
  GList *factories;

  factories = gst_registry_pool_feature_filter (
      (GstPluginFeatureFilter) gst_decode_bin_factory_filter,
      FALSE, decode_bin);

  decode_bin->factories = g_list_sort (factories, (GCompareFunc) compare_ranks);
  g_list_foreach (decode_bin->factories, (GFunc) print_feature, NULL);

  decode_bin->typefind = gst_element_factory_make ("typefind", "typefind");
  if (!decode_bin->typefind) {
    g_warning ("can't find typefind element");
  }
  gst_bin_add (GST_BIN (decode_bin), decode_bin->typefind);
  gst_element_add_ghost_pad (GST_ELEMENT (decode_bin),
      gst_element_get_pad (decode_bin->typefind, "sink"), "sink");

  decode_bin->have_type_id =
      g_signal_connect (G_OBJECT (decode_bin->typefind), "have_type",
      G_CALLBACK (type_found), decode_bin);

  decode_bin->dynamics = NULL;
}

static void
gst_decode_bin_dispose (GObject * object)
{
  GstDecodeBin *decode_bin;
  GList *dyns;

  decode_bin = GST_DECODE_BIN (object);

  g_signal_handler_disconnect (G_OBJECT (decode_bin->typefind),
      decode_bin->have_type_id);

  gst_bin_remove (GST_BIN (decode_bin), decode_bin->typefind);

  g_list_free (decode_bin->factories);

  for (dyns = decode_bin->dynamics; dyns; dyns = g_list_next (dyns)) {
    GstDynamic *dynamic = (GstDynamic *) dyns->data;

    g_free (dynamic);
  }
  g_list_free (decode_bin->dynamics);
  decode_bin->dynamics = NULL;

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}

static GList *
find_compatibles (GstDecodeBin * decode_bin, const GstCaps * caps)
{
  GList *factories;
  GList *to_try = NULL;

  for (factories = decode_bin->factories; factories;
      factories = g_list_next (factories)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (factories->data);
    const GList *templates;
    GList *walk;

    templates = gst_element_factory_get_pad_templates (factory);
    for (walk = (GList *) templates; walk; walk = g_list_next (walk)) {
      GstPadTemplate *templ = GST_PAD_TEMPLATE (walk->data);

      if (templ->direction == GST_PAD_SINK) {
        GstCaps *intersect;

        intersect =
            gst_caps_intersect (caps, gst_pad_template_get_caps (templ));
        if (!gst_caps_is_empty (intersect)) {
          to_try = g_list_append (to_try, factory);
        }
        gst_caps_free (intersect);
      }
    }
  }
  return to_try;
}

static void
close_pad_link (GstElement * element, GstPad * pad, GstCaps * caps,
    GstDecodeBin * decode_bin)
{
  GList *to_try;
  GstStructure *structure;
  const gchar *mimetype;

  if (gst_caps_is_empty (caps)) {
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, caps);
    return;
  }

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* first see if this is raw */
  if (g_str_has_prefix (mimetype, "video/x-raw") ||
      g_str_has_prefix (mimetype, "audio/x-raw")) {
    gchar *padname;
    GstPad *ghost;
    gboolean dynamic;

    padname = g_strdup_printf ("src%d", decode_bin->numpads);
    decode_bin->numpads++;

    gst_element_add_ghost_pad (GST_ELEMENT (decode_bin), pad, padname);

    ghost = gst_element_get_pad (GST_ELEMENT (decode_bin), padname);

    dynamic = gst_decode_bin_is_dynamic (decode_bin);

    /* our own signal with an extra flag that this is the only pad */
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_NEW_STREAM], 0, ghost, !dynamic);

    g_free (padname);
    return;
  }

  /* then continue plugging */
  to_try = find_compatibles (decode_bin, caps);
  if (to_try == NULL) {
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, caps);
    return;
  }

  try_to_link_1 (decode_bin, pad, to_try);
}

static GstElement *
try_to_link_1 (GstDecodeBin * decode_bin, GstPad * pad, GList * factories)
{
  GList *walk;

  for (walk = factories; walk; walk = g_list_next (walk)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (walk->data);
    GstElement *element;
    gboolean ret;

    GST_DEBUG ("trying to link %s\n",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

    element = gst_element_factory_create (factory, NULL);
    if (element == NULL)
      continue;

    GST_DEBUG ("adding %s\n", gst_element_get_name (element));
    gst_bin_add (GST_BIN (decode_bin), element);
    decode_bin->elements = g_list_prepend (decode_bin->elements, element);

    ret = gst_pad_link (pad, gst_element_get_pad (element, "sink"));
    if (ret) {
      const gchar *klass;
      GstElementFactory *factory;

      factory = gst_element_get_factory (element);
      klass = gst_element_factory_get_klass (factory);
      if (decode_bin->threaded) {
        if (strstr (klass, "Demux") != NULL) {
          /* FIXME, do something with threads here */
        }
      }

      close_link (element, decode_bin);
      gst_element_sync_state_with_parent (element);
      return element;
    } else {
      gst_bin_remove (GST_BIN (decode_bin), element);
    }
  }
  return NULL;
}

static void
new_pad (GstElement * element, GstPad * pad, GstDynamic * dynamic)
{
  close_pad_link (element, pad, gst_pad_get_caps (pad), dynamic->decode_bin);
}

static void
no_more_pads (GstElement * element, GstDynamic * dynamic)
{
  GstDecodeBin *decode_bin = dynamic->decode_bin;

  GST_DEBUG ("decodebin: no more pads\n");

  g_signal_handler_disconnect (G_OBJECT (dynamic->element), dynamic->np_sig_id);
  g_signal_handler_disconnect (G_OBJECT (dynamic->element),
      dynamic->nmp_sig_id);

  decode_bin->dynamics = g_list_remove (decode_bin->dynamics, dynamic);
  g_free (dynamic);

  if (decode_bin->dynamics == NULL)
    gst_element_no_more_pads (GST_ELEMENT (decode_bin));
}

static void
close_link (GstElement * element, GstDecodeBin * decode_bin)
{
  GList *pads;
  gboolean dynamic = FALSE;
  GList *to_connect = NULL;

  for (pads = gst_element_get_pad_template_list (element); pads;
      pads = g_list_next (pads)) {
    GstPadTemplate *templ = GST_PAD_TEMPLATE (pads->data);

    if (GST_PAD_TEMPLATE_DIRECTION (templ) != GST_PAD_SRC)
      continue;

    switch (GST_PAD_TEMPLATE_PRESENCE (templ)) {
      case GST_PAD_ALWAYS:
      {
        GstPad *pad = gst_element_get_pad (element,
            GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));

        if (pad) {
          to_connect = g_list_prepend (to_connect, pad);
        }
        break;
      }
      case GST_PAD_SOMETIMES:
      {
        GstPad *pad = gst_element_get_pad (element,
            GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));

        if (pad) {
          to_connect = g_list_prepend (to_connect, pad);
        } else {
          dynamic = TRUE;
        }
        break;
      }
      case GST_PAD_REQUEST:
        break;
    }
  }
  if (dynamic) {
    GstDynamic *dyn;

    dyn = g_new0 (GstDynamic, 1);
    dyn->np_sig_id = g_signal_connect (G_OBJECT (element), "new-pad",
        G_CALLBACK (new_pad), dyn);
    dyn->nmp_sig_id = g_signal_connect (G_OBJECT (element), "no-more-pads",
        G_CALLBACK (no_more_pads), dyn);
    dyn->element = element;
    dyn->decode_bin = decode_bin;

    decode_bin->dynamics = g_list_prepend (decode_bin->dynamics, dyn);
  }

  for (pads = to_connect; pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD (pads->data);

    close_pad_link (element, pad, gst_pad_get_caps (pad), decode_bin);
  }
  g_list_free (to_connect);
}

static void
type_found (GstElement * typefind, guint probability, GstCaps * caps,
    GstDecodeBin * decode_bin)
{
  gboolean dynamic;

  close_pad_link (typefind, gst_element_get_pad (typefind, "src"), caps,
      decode_bin);

  dynamic = gst_decode_bin_is_dynamic (decode_bin);
  if (dynamic == FALSE) {
    gst_element_no_more_pads (GST_ELEMENT (decode_bin));
  }
}

static void
gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecodeBin *decode_bin;

  g_return_if_fail (GST_IS_DECODE_BIN (object));

  decode_bin = GST_DECODE_BIN (object);

  switch (prop_id) {
    case ARG_THREADED:
      decode_bin->threaded = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_decode_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDecodeBin *decode_bin;

  g_return_if_fail (GST_IS_DECODE_BIN (object));

  decode_bin = GST_DECODE_BIN (object);

  switch (prop_id) {
    case ARG_THREADED:
      g_value_set_boolean (value, decode_bin->threaded);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_decode_bin_change_state (GstElement * element)
{
  GstElementStateReturn ret;
  GstDecodeBin *decode_bin;
  gint transition;

  decode_bin = GST_DECODE_BIN (element);

  transition = GST_STATE_TRANSITION (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  if (ret != GST_STATE_SUCCESS) {
    return ret;
  }

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      decode_bin->numpads = 0;
      decode_bin->threaded = FALSE;
      decode_bin->dynamics = NULL;
      break;
    case GST_STATE_READY_TO_PAUSED:
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_decode_bin_debug, "decodebin", 0, "decoder bin");

  return gst_element_register (plugin, "decodebin", GST_RANK_NONE,
      GST_TYPE_DECODE_BIN);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "decodebin",
    "decoder bin", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
