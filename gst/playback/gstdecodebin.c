/* GStreamer
 * Copyright (C) <2004> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2011 Hewlett-Packard Development Company, L.P.
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

/**
 * SECTION:element-decodebin
 *
 * #GstBin that auto-magically constructs a decoding pipeline using available
 * decoders and demuxers via auto-plugging.
 *
 * When using decodebin in your application, connect a signal handler to
 * #GstDecodeBin::new-decoded-pad and connect your sinks from within the
 * callback function.
 *
 * <note>
 * This element is deprecated and no longer supported. You should use the
 * #uridecodebin or #decodebin2 element instead (or, even better: #playbin2).
 * </note>
 *
 * Deprecated: use uridecodebin or decodebin2 instead.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst-i18n-plugin.h>

#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include "gst/glib-compat-private.h"

#include "gstplay-marshal.h"

/* generic templates */
static GstStaticPadTemplate decoder_bin_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate decoder_bin_src_template =
GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_decode_bin_debug);
#define GST_CAT_DEFAULT gst_decode_bin_debug

#define GST_TYPE_DECODE_BIN             (gst_decode_bin_get_type())
#define GST_DECODE_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECODE_BIN,GstDecodeBin))
#define GST_DECODE_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECODE_BIN,GstDecodeBinClass))
#define GST_IS_DECODE_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECODE_BIN))
#define GST_IS_DECODE_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECODE_BIN))

typedef struct _GstDecodeBin GstDecodeBin;
typedef struct _GstDecodeBinClass GstDecodeBinClass;

/**
 * GstDecodeBin:
 *
 * Auto-plugging decoder element structure
 */
struct _GstDecodeBin
{
  GstBin bin;                   /* we extend GstBin */

  GstElement *typefind;         /* this holds the typefind object */
  GstElement *fakesink;

  GList *dynamics;              /* list of dynamic connections */

  GList *queues;                /* list of demuxer-decoder queues */

  GList *probes;                /* list of PadProbeData */

  GList *factories;             /* factories we can use for selecting elements */
  gint numpads;
  gint numwaiting;

  gboolean have_type;
  guint have_type_id;           /* signal id for the typefind element */

  gboolean shutting_down;       /* stop pluggin if we're shutting down */

  GType queue_type;             /* store the GType of queues, to aid in recognising them */

  GMutex *cb_mutex;             /* Mutex for multi-threaded callbacks, such as removing the fakesink */
};

struct _GstDecodeBinClass
{
  GstBinClass parent_class;

  /* signal we fire when a new pad has been decoded into raw audio/video */
  void (*new_decoded_pad) (GstElement * element, GstPad * pad, gboolean last);
  /* signal we fire when a pad has been removed */
  void (*removed_decoded_pad) (GstElement * element, GstPad * pad);
  /* signal fired when we found a pad that we cannot decode */
  void (*unknown_type) (GstElement * element, GstPad * pad, GstCaps * caps);
};

/* signals */
enum
{
  SIGNAL_NEW_DECODED_PAD,
  SIGNAL_REMOVED_DECODED_PAD,
  SIGNAL_UNKNOWN_TYPE,
  SIGNAL_REDIRECT,
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_SINK_CAPS,
};


typedef struct
{
  GstPad *pad;
  gulong sigid;
  gboolean done;
} PadProbeData;

/* this structure is created for all dynamic pads that could get created
 * at runtime */
typedef struct
{
  GstDecodeBin *decode_bin;     /* pointer to ourself */

  GstElement *element;          /* the element sending the signal */
  gint np_sig_id;               /* signal id of new_pad */
  gint nmp_sig_id;              /* signal id of no_more_pads */

  GstPad *pad;                  /* the pad sending the signal */
  gint caps_sig_id;             /* signal id of caps */
}
GstDynamic;

static void gst_decode_bin_class_init (GstDecodeBinClass * klass);
static void gst_decode_bin_init (GstDecodeBin * decode_bin);
static void gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_decode_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_decode_bin_dispose (GObject * object);
static void gst_decode_bin_finalize (GObject * object);

static GstStateChangeReturn gst_decode_bin_change_state (GstElement * element,
    GstStateChange transition);

static gboolean add_fakesink (GstDecodeBin * decode_bin);
static void remove_fakesink (GstDecodeBin * decode_bin);

static void dynamic_free (GstDynamic * dyn);
static void free_dynamics (GstDecodeBin * decode_bin);
static void type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin);
static GstElement *try_to_link_1 (GstDecodeBin * decode_bin,
    GstElement * origelement, GstPad * pad, GList * factories);
static void close_link (GstElement * element, GstDecodeBin * decode_bin);
static void close_pad_link (GstElement * element, GstPad * pad,
    GstCaps * caps, GstDecodeBin * decode_bin, gboolean more);
static void unlinked (GstPad * pad, GstPad * peerpad,
    GstDecodeBin * decode_bin);
static void new_pad (GstElement * element, GstPad * pad, GstDynamic * dynamic);
static void no_more_pads (GstElement * element, GstDynamic * dynamic);
static void new_caps (GstPad * pad, GParamSpec * unused, GstDynamic * dynamic);

static void queue_filled_cb (GstElement * queue, GstDecodeBin * decode_bin);
static void queue_underrun_cb (GstElement * queue, GstDecodeBin * decode_bin);

static gboolean is_demuxer_element (GstElement * srcelement);

static GstElementClass *parent_class;
static guint gst_decode_bin_signals[LAST_SIGNAL] = { 0 };


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

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_decode_bin_set_property;
  gobject_klass->get_property = gst_decode_bin_get_property;
  gobject_klass->dispose = gst_decode_bin_dispose;
  gobject_klass->finalize = gst_decode_bin_finalize;

  /**
   * GstDecodeBin::new-decoded-pad:
   * @bin: The decodebin
   * @pad: The newly created pad
   * @islast: #TRUE if this is the last pad to be added. Deprecated.
   *
   * This signal gets emitted as soon as a new pad of the same type as one of
   * the valid 'raw' types is added.
   */
  gst_decode_bin_signals[SIGNAL_NEW_DECODED_PAD] =
      g_signal_new ("new-decoded-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDecodeBinClass, new_decoded_pad), NULL, NULL,
      gst_play_marshal_VOID__OBJECT_BOOLEAN, G_TYPE_NONE, 2, GST_TYPE_PAD,
      G_TYPE_BOOLEAN);
  /**
   * GstDecodeBin::removed-decoded-pad:
   * @bin: The decodebin
   * @pad: The pad that was removed
   *
   * This signal is emitted when a 'final' caps pad has been removed.
   */
  gst_decode_bin_signals[SIGNAL_REMOVED_DECODED_PAD] =
      g_signal_new ("removed-decoded-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDecodeBinClass, removed_decoded_pad), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  /**
   * GstDecodeBin::unknown-type:
   * @bin: The decodebin
   * @pad: The new pad containing caps that cannot be resolved to a 'final'
   *       stream type.
   * @caps: The #GstCaps of the pad that cannot be resolved.
   *
   * This signal is emitted when a pad for which there is no further possible
   * decoding is added to the decodebin.
   */
  gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE] =
      g_signal_new ("unknown-type", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, unknown_type),
      NULL, NULL, gst_marshal_VOID__OBJECT_BOXED, G_TYPE_NONE, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  g_object_class_install_property (gobject_klass, PROP_SINK_CAPS,
      g_param_spec_boxed ("sink-caps", "Sink Caps",
          "The caps of the input data. (NULL = use typefind element)",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_klass,
      &decoder_bin_sink_template);
  gst_element_class_add_static_pad_template (gstelement_klass,
      &decoder_bin_src_template);

  gst_element_class_set_details_simple (gstelement_klass,
      "Decoder Bin", "Generic/Bin/Decoder",
      "Autoplug and decode to raw media",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_decode_bin_change_state);
}

/* check if the bin is dynamic.
 *
 * If there are no outstanding dynamic connections, the bin is
 * considered to be non-dynamic.
 */
static gboolean
gst_decode_bin_is_dynamic (GstDecodeBin * decode_bin)
{
  return decode_bin->dynamics != NULL;
}

/* the filter function for selecting the elements we can use in
 * autoplugging */
static gboolean
gst_decode_bin_factory_filter (GstPluginFeature * feature,
    GstDecodeBin * decode_bin)
{
  guint rank;
  const gchar *klass;

  /* we only care about element factories */
  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  klass = gst_element_factory_get_klass (GST_ELEMENT_FACTORY (feature));
  /* only demuxers, decoders and parsers can play */
  if (strstr (klass, "Demux") == NULL &&
      strstr (klass, "Decoder") == NULL && strstr (klass, "Parse") == NULL &&
      strstr (klass, "Depayloader") == NULL) {
    return FALSE;
  }

  /* only select elements with autoplugging rank */
  rank = gst_plugin_feature_get_rank (feature);
  if (rank < GST_RANK_MARGINAL)
    return FALSE;

  return TRUE;
}

/* function used to sort element features */
static gint
compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;
  const gchar *rname1, *rname2;

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;

  rname1 = gst_plugin_feature_get_name (f1);
  rname2 = gst_plugin_feature_get_name (f2);

  diff = strcmp (rname2, rname1);

  return diff;
}

static void
print_feature (GstPluginFeature * feature)
{
  const gchar *rname;

  rname = gst_plugin_feature_get_name (feature);

  GST_DEBUG ("%s", rname);
}

static void
gst_decode_bin_init (GstDecodeBin * decode_bin)
{
  GList *factories;

  decode_bin->cb_mutex = g_mutex_new ();

  /* first filter out the interesting element factories */
  factories = gst_default_registry_feature_filter (
      (GstPluginFeatureFilter) gst_decode_bin_factory_filter,
      FALSE, decode_bin);

  /* sort them according to their ranks */
  decode_bin->factories = g_list_sort (factories, (GCompareFunc) compare_ranks);
  /* do some debugging */
  g_list_foreach (decode_bin->factories, (GFunc) print_feature, NULL);

  /* we create the typefind element only once */
  decode_bin->typefind = gst_element_factory_make ("typefind", "typefind");
  if (!decode_bin->typefind) {
    g_warning ("can't find typefind element, decodebin will not work");
  } else {
    GstPad *pad, *gpad;
    GstPadTemplate *pad_tmpl;

    /* add the typefind element */
    if (!gst_bin_add (GST_BIN (decode_bin), decode_bin->typefind)) {
      g_warning ("Could not add typefind element, decodebin will not work");
      gst_object_unref (decode_bin->typefind);
      decode_bin->typefind = NULL;
    }

    /* get the sinkpad */
    pad = gst_element_get_static_pad (decode_bin->typefind, "sink");

    /* get the pad template */
    pad_tmpl = gst_static_pad_template_get (&decoder_bin_sink_template);

    /* ghost the sink pad to ourself */
    gpad = gst_ghost_pad_new_from_template ("sink", pad, pad_tmpl);
    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (decode_bin), gpad);

    gst_object_unref (pad_tmpl);
    gst_object_unref (pad);

    /* connect a signal to find out when the typefind element found
     * a type */
    decode_bin->have_type_id =
        g_signal_connect (G_OBJECT (decode_bin->typefind), "have_type",
        G_CALLBACK (type_found), decode_bin);
  }
  add_fakesink (decode_bin);

  decode_bin->dynamics = NULL;
  decode_bin->queues = NULL;
  decode_bin->probes = NULL;
}

static void
gst_decode_bin_dispose (GObject * object)
{
  GstDecodeBin *decode_bin;

  decode_bin = GST_DECODE_BIN (object);

  if (decode_bin->factories)
    gst_plugin_feature_list_free (decode_bin->factories);
  decode_bin->factories = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);

  /* our parent dispose might trigger new signals when pads are unlinked
   * etc. clean up the mess here. */
  /* FIXME do proper cleanup when going to NULL */
  free_dynamics (decode_bin);
}

static void
gst_decode_bin_set_sink_caps (GstDecodeBin * dbin, GstCaps * caps)
{
  GST_DEBUG_OBJECT (dbin, "Setting new caps: %" GST_PTR_FORMAT, caps);

  g_object_set (dbin->typefind, "force-caps", caps, NULL);
}

static GstCaps *
gst_decode_bin_get_sink_caps (GstDecodeBin * dbin)
{
  GstCaps *caps;

  GST_DEBUG_OBJECT (dbin, "Getting currently set caps");

  g_object_get (dbin->typefind, "force-caps", &caps, NULL);

  return caps;
}

static void
gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecodeBin *dbin;

  dbin = GST_DECODE_BIN (object);

  switch (prop_id) {
    case PROP_SINK_CAPS:
      gst_decode_bin_set_sink_caps (dbin, g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_decode_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecodeBin *dbin;

  dbin = GST_DECODE_BIN (object);
  switch (prop_id) {
    case PROP_SINK_CAPS:
      g_value_take_boxed (value, gst_decode_bin_get_sink_caps (dbin));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_decode_bin_finalize (GObject * object)
{
  GstDecodeBin *decode_bin = GST_DECODE_BIN (object);

  g_mutex_free (decode_bin->cb_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

struct DynFind
{
  GstElement *elem;
  GstPad *pad;
};

static gint
find_dynamic (GstDynamic * dyn, struct DynFind *info)
{
  if (dyn->element == info->elem && dyn->pad == info->pad)
    return 0;
  return 1;
}

/* Add either an element (for dynamic pads/pad-added watching) or a
 * pad (for delayed caps/notify::caps watching) to the dynamic list,
 * taking care to ignore repeat entries so we don't end up handling a
 * pad twice, for example */
static void
dynamic_add (GstElement * element, GstPad * pad, GstDecodeBin * decode_bin)
{
  GstDynamic *dyn;
  struct DynFind find_info;
  GList *found;

  g_return_if_fail (element != NULL);

  /* do a search that this entry doesn't already exist */
  find_info.elem = element;
  find_info.pad = pad;
  found = g_list_find_custom (decode_bin->dynamics, &find_info,
      (GCompareFunc) find_dynamic);
  if (found != NULL)
    goto exit;

  /* take refs */
  dyn = g_new0 (GstDynamic, 1);
  dyn->element = gst_object_ref (element);
  dyn->decode_bin = gst_object_ref (decode_bin);
  if (pad) {
    dyn->pad = gst_object_ref (pad);
    GST_DEBUG_OBJECT (decode_bin, "dynamic create for pad %" GST_PTR_FORMAT,
        pad);
    dyn->caps_sig_id = g_signal_connect (G_OBJECT (pad), "notify::caps",
        G_CALLBACK (new_caps), dyn);
  } else {
    GST_DEBUG_OBJECT (decode_bin, "dynamic create for element %"
        GST_PTR_FORMAT, element);
    dyn->np_sig_id = g_signal_connect (G_OBJECT (element), "pad-added",
        G_CALLBACK (new_pad), dyn);
    dyn->nmp_sig_id = g_signal_connect (G_OBJECT (element), "no-more-pads",
        G_CALLBACK (no_more_pads), dyn);
  }

  /* and add this element to the dynamic elements */
  decode_bin->dynamics = g_list_prepend (decode_bin->dynamics, dyn);

  return;
exit:
  if (element) {
    GST_DEBUG_OBJECT (decode_bin, "Dynamic element already added: %"
        GST_PTR_FORMAT, element);
  } else {
    GST_DEBUG_OBJECT (decode_bin, "Dynamic pad already added: %"
        GST_PTR_FORMAT, pad);
  }
}

static void
dynamic_free (GstDynamic * dyn)
{
  GST_DEBUG_OBJECT (dyn->decode_bin, "dynamic free");

  /* disconnect signals */
  if (dyn->np_sig_id)
    g_signal_handler_disconnect (G_OBJECT (dyn->element), dyn->np_sig_id);
  if (dyn->nmp_sig_id)
    g_signal_handler_disconnect (G_OBJECT (dyn->element), dyn->nmp_sig_id);
  if (dyn->caps_sig_id)
    g_signal_handler_disconnect (G_OBJECT (dyn->pad), dyn->caps_sig_id);

  if (dyn->pad)
    gst_object_unref (dyn->pad);
  dyn->pad = NULL;
  if (dyn->element)
    gst_object_unref (dyn->element);
  dyn->element = NULL;

  gst_object_unref (dyn->decode_bin);
  dyn->decode_bin = NULL;

  g_free (dyn);
}

static void
free_dynamics (GstDecodeBin * decode_bin)
{
  GList *dyns;

  for (dyns = decode_bin->dynamics; dyns; dyns = g_list_next (dyns)) {
    GstDynamic *dynamic = (GstDynamic *) dyns->data;

    dynamic_free (dynamic);
  }
  g_list_free (decode_bin->dynamics);
  decode_bin->dynamics = NULL;
}

/* this function runs through the element factories and returns a list
 * of all elements that are able to sink the given caps
 */
static GList *
find_compatibles (GstDecodeBin * decode_bin, const GstCaps * caps)
{
  GList *factories;
  GList *to_try = NULL;

  /* loop over all the factories */
  for (factories = decode_bin->factories; factories;
      factories = g_list_next (factories)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (factories->data);
    const GList *templates;
    GList *walk;

    /* get the templates from the element factory */
    templates = gst_element_factory_get_static_pad_templates (factory);
    for (walk = (GList *) templates; walk; walk = g_list_next (walk)) {
      GstStaticPadTemplate *templ = walk->data;

      /* we only care about the sink templates */
      if (templ->direction == GST_PAD_SINK) {
        gboolean can_intersect;
        GstCaps *tmpl_caps;

        /* try to intersect the caps with the caps of the template */
        tmpl_caps = gst_static_caps_get (&templ->static_caps);

        can_intersect = gst_caps_can_intersect (caps, tmpl_caps);
        gst_caps_unref (tmpl_caps);

        /* check if the intersection is empty */
        if (can_intersect) {
          /* non empty intersection, we can use this element */
          to_try = g_list_prepend (to_try, factory);
          break;
        }
      }
    }
  }
  to_try = g_list_reverse (to_try);

  return to_try;
}

static gboolean
mimetype_is_raw (const gchar * mimetype)
{
  return g_str_has_prefix (mimetype, "video/x-raw") ||
      g_str_has_prefix (mimetype, "audio/x-raw") ||
      g_str_has_prefix (mimetype, "text/plain") ||
      g_str_has_prefix (mimetype, "text/x-pango-markup");
}

static void
free_pad_probes (GstDecodeBin * decode_bin)
{
  GList *tmp;

  /* Remove pad probes */
  for (tmp = decode_bin->probes; tmp; tmp = g_list_next (tmp)) {
    PadProbeData *data = (PadProbeData *) tmp->data;

    gst_pad_remove_data_probe (data->pad, data->sigid);
    g_free (data);
  }
  g_list_free (decode_bin->probes);
  decode_bin->probes = NULL;
}

/* used when we need to remove a probe because the decoder we plugged failed
 * to activate */
static void
free_pad_probe_for_element (GstDecodeBin * decode_bin, GstElement * element)
{
  GList *l;

  for (l = decode_bin->probes; l != NULL; l = g_list_next (l)) {
    PadProbeData *data = (PadProbeData *) l->data;

    if (GST_ELEMENT_CAST (GST_PAD_PARENT (data->pad)) == element) {
      gst_pad_remove_data_probe (data->pad, data->sigid);
      decode_bin->probes = g_list_delete_link (decode_bin->probes, l);
      g_free (data);
      return;
    }
  }
}

static gboolean
add_fakesink (GstDecodeBin * decode_bin)
{
  if (decode_bin->fakesink != NULL)
    return TRUE;

  g_mutex_lock (decode_bin->cb_mutex);

  decode_bin->fakesink = gst_element_factory_make ("fakesink", "fakesink");
  if (!decode_bin->fakesink)
    goto no_fakesink;

  /* hacky, remove sink flag, we don't want our decodebin to become a sink
   * just because we add a fakesink element to make us ASYNC */
  GST_OBJECT_FLAG_UNSET (decode_bin->fakesink, GST_ELEMENT_IS_SINK);

  /* takes ownership */
  if (!gst_bin_add (GST_BIN (decode_bin), decode_bin->fakesink)) {
    g_warning ("Could not add fakesink element, decodebin will not work");
    gst_object_unref (decode_bin->fakesink);
    decode_bin->fakesink = NULL;
  }
  g_mutex_unlock (decode_bin->cb_mutex);
  return TRUE;

  /* ERRORS */
no_fakesink:
  {
    g_warning ("can't find fakesink element, decodebin will not work");
    g_mutex_unlock (decode_bin->cb_mutex);
    return FALSE;
  }
}

static void
remove_fakesink (GstDecodeBin * decode_bin)
{
  gboolean removed_fakesink = FALSE;

  if (decode_bin->fakesink == NULL)
    return;

  g_mutex_lock (decode_bin->cb_mutex);
  if (decode_bin->fakesink) {
    GST_DEBUG_OBJECT (decode_bin, "Removing fakesink and marking state dirty");

    /* Lock the state to prevent it from changing state to non-NULL
     * before it's removed */
    gst_element_set_locked_state (decode_bin->fakesink, TRUE);
    /* setting the state to NULL is never async */
    gst_element_set_state (decode_bin->fakesink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (decode_bin), decode_bin->fakesink);
    decode_bin->fakesink = NULL;

    removed_fakesink = TRUE;
  }
  g_mutex_unlock (decode_bin->cb_mutex);

  if (removed_fakesink) {
    free_pad_probes (decode_bin);
  }
}

/* this should be implemented with _pad_block() */
static gboolean
pad_probe (GstPad * pad, GstMiniObject * data, GstDecodeBin * decode_bin)
{
  GList *tmp;
  gboolean alldone = TRUE;

  for (tmp = decode_bin->probes; tmp; tmp = g_list_next (tmp)) {
    PadProbeData *pdata = (PadProbeData *) tmp->data;

    if (pdata->pad == pad) {
      if (GST_IS_BUFFER (data)) {
        if (!pdata->done)
          decode_bin->numwaiting--;
        pdata->done = TRUE;
      } else if (GST_IS_EVENT (data) &&
          ((GST_EVENT_TYPE (data) == GST_EVENT_EOS) ||
              (GST_EVENT_TYPE (data) == GST_EVENT_TAG) ||
              (GST_EVENT_TYPE (data) == GST_EVENT_FLUSH_START))) {
        /* FIXME, what about NEWSEGMENT? really, use _pad_block()... */
        if (!pdata->done)
          decode_bin->numwaiting--;
        pdata->done = TRUE;
      }
    }

    if (!(pdata->done)) {
      GST_LOG_OBJECT (decode_bin, "Pad probe on pad %" GST_PTR_FORMAT
          " but pad %" GST_PTR_FORMAT " still needs data.", pad, pdata->pad);
      alldone = FALSE;
    }
  }
  if (alldone)
    remove_fakesink (decode_bin);
  return TRUE;
}

/* FIXME: this should be somehow merged with the queue code in
 * try_to_link_1() to reduce code duplication */
static GstPad *
add_raw_queue (GstDecodeBin * decode_bin, GstPad * pad)
{
  GstElement *queue = NULL;
  GstPad *queuesinkpad = NULL, *queuesrcpad = NULL;

  queue = gst_element_factory_make ("queue", NULL);
  decode_bin->queue_type = G_OBJECT_TYPE (queue);

  g_object_set (G_OBJECT (queue), "max-size-buffers", 0, NULL);
  g_object_set (G_OBJECT (queue), "max-size-time", G_GINT64_CONSTANT (0), NULL);
  g_object_set (G_OBJECT (queue), "max-size-bytes", 8192, NULL);
  gst_bin_add (GST_BIN (decode_bin), queue);
  gst_element_set_state (queue, GST_STATE_READY);
  queuesinkpad = gst_element_get_static_pad (queue, "sink");
  queuesrcpad = gst_element_get_static_pad (queue, "src");

  if (gst_pad_link (pad, queuesinkpad) != GST_PAD_LINK_OK) {
    GST_WARNING_OBJECT (decode_bin,
        "Linking queue failed, trying without queue");
    gst_element_set_state (queue, GST_STATE_NULL);
    gst_object_unref (queuesrcpad);
    gst_object_unref (queuesinkpad);
    gst_bin_remove (GST_BIN (decode_bin), queue);
    return gst_object_ref (pad);
  }

  decode_bin->queues = g_list_append (decode_bin->queues, queue);
  g_signal_connect (G_OBJECT (queue),
      "overrun", G_CALLBACK (queue_filled_cb), decode_bin);
  g_signal_connect (G_OBJECT (queue),
      "underrun", G_CALLBACK (queue_underrun_cb), decode_bin);

  gst_element_set_state (queue, GST_STATE_PAUSED);
  gst_object_unref (queuesinkpad);

  return queuesrcpad;
}

/* given a pad and a caps from an element, find the list of elements
 * that could connect to the pad
 *
 * If the pad has a raw format, this function will create a ghostpad
 * for the pad onto the decodebin.
 *
 * If no compatible elements could be found, this function will signal
 * the unknown_type signal.
 */
static void
close_pad_link (GstElement * element, GstPad * pad, GstCaps * caps,
    GstDecodeBin * decode_bin, gboolean more)
{
  GstStructure *structure;
  const gchar *mimetype;
  gchar *padname;
  gint diff;

  padname = gst_pad_get_name (pad);
  diff = strncmp (padname, "current_", 8);
  g_free (padname);

  /* hack.. ignore current pads */
  if (!diff)
    return;

  /* the caps is empty, this means the pad has no type, we can only
   * decide to fire the unknown_type signal. */
  if (caps == NULL || gst_caps_is_empty (caps))
    goto unknown_type;

  /* the caps is any, this means the pad can be anything and
   * we don't know yet */
  if (gst_caps_is_any (caps))
    goto dont_know_yet;

  GST_LOG_OBJECT (element, "trying to close %" GST_PTR_FORMAT, caps);

  /* FIXME, iterate over more structures? I guess it is possible that
   * this pad has some encoded and some raw pads. This code will fail
   * then if the first structure is not the raw type... */
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* first see if this is raw. If the type is raw, we can
   * create a ghostpad for this pad. It's possible that the caps are not
   * fixed. */
  if (mimetype_is_raw (mimetype)) {
    GstPadTemplate *tmpl;
    gchar *padname;
    GstPad *ghost;
    PadProbeData *data;

    /* If we're at a demuxer element but have raw data already
     * we have to add a queue here. For non-raw data this is done
     * in try_to_link_1() */
    if (is_demuxer_element (element)) {
      GST_DEBUG_OBJECT (decode_bin,
          "Element %s is a demuxer, inserting a queue",
          GST_OBJECT_NAME (element));

      pad = add_raw_queue (decode_bin, pad);
    }

    /* make a unique name for this new pad */
    padname = g_strdup_printf ("src%d", decode_bin->numpads);
    decode_bin->numpads++;

    /* make it a ghostpad */
    tmpl = gst_static_pad_template_get (&decoder_bin_src_template);
    ghost = gst_ghost_pad_new_from_template (padname, pad, tmpl);
    gst_object_unref (tmpl);

    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (GST_ELEMENT (decode_bin), ghost);

    data = g_new0 (PadProbeData, 1);
    data->pad = pad;
    data->done = FALSE;

    /* FIXME, use _pad_block() */
    data->sigid = gst_pad_add_data_probe (pad, G_CALLBACK (pad_probe),
        decode_bin);
    decode_bin->numwaiting++;

    decode_bin->probes = g_list_append (decode_bin->probes, data);

    GST_LOG_OBJECT (element, "closed pad %s", padname);

    /* our own signal with an extra flag that this is the only pad */
    GST_DEBUG_OBJECT (decode_bin, "emitting new-decoded-pad");
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_NEW_DECODED_PAD], 0, ghost, !more);
    GST_DEBUG_OBJECT (decode_bin, "emitted new-decoded-pad");

    g_free (padname);

    /* If we're at a demuxer element pad was set to a queue's
     * srcpad and must be unref'd here */
    if (is_demuxer_element (element))
      gst_object_unref (pad);
  } else {
    GList *to_try;

    /* if the caps has many types, we need to delay */
    if (!gst_caps_is_fixed (caps))
      goto many_types;

    /* continue plugging, first find all compatible elements */
    to_try = find_compatibles (decode_bin, caps);
    if (to_try == NULL)
      /* no compatible elements, we cannot go on */
      goto unknown_type;

    if (try_to_link_1 (decode_bin, element, pad, to_try) == NULL) {
      g_list_free (to_try);
      GST_LOG_OBJECT (pad, "none of the allegedly available elements usable");
      goto unknown_type;
    }

    /* can free the list again now */
    g_list_free (to_try);
  }
  return;

  /* ERRORS */
unknown_type:
  {
    GST_LOG_OBJECT (pad, "unknown type found, fire signal");
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, pad, caps);

    gst_element_post_message (GST_ELEMENT_CAST (decode_bin),
        gst_missing_decoder_message_new (GST_ELEMENT_CAST (decode_bin), caps));

    if (element == decode_bin->typefind) {
      gchar *desc;

      desc = gst_pb_utils_get_decoder_description (caps);
      GST_ELEMENT_ERROR (decode_bin, STREAM, CODEC_NOT_FOUND,
          (_("A %s plugin is required to play this stream, but not installed."),
              desc),
          ("No decoder to handle media type '%s'",
              gst_structure_get_name (gst_caps_get_structure (caps, 0))));
      g_free (desc);
    }

    return;
  }
dont_know_yet:
  {
    GST_LOG_OBJECT (pad, "type is not known yet");
    goto setup_caps_delay;
  }
many_types:
  {
    GST_LOG_OBJECT (pad, "many possible types");
    goto setup_caps_delay;
  }
setup_caps_delay:
  {
    GST_LOG_OBJECT (pad, "setting up a delayed link");
    dynamic_add (element, pad, decode_bin);
    return;
  }
}

/* Decide whether an element is a demuxer based on the
 * klass and number/type of src pad templates it has */
static gboolean
is_demuxer_element (GstElement * srcelement)
{
  GstElementFactory *srcfactory;
  GstElementClass *elemclass;
  GList *walk;
  const gchar *klass;
  gint potential_src_pads = 0;

  srcfactory = gst_element_get_factory (srcelement);
  klass = gst_element_factory_get_klass (srcfactory);

  /* Can't be a demuxer unless it has Demux in the klass name */
  if (klass == NULL || !strstr (klass, "Demux"))
    return FALSE;

  /* Walk the src pad templates and count how many the element
   * might produce */
  elemclass = GST_ELEMENT_GET_CLASS (srcelement);

  walk = gst_element_class_get_pad_template_list (elemclass);
  while (walk != NULL) {
    GstPadTemplate *templ;

    templ = (GstPadTemplate *) walk->data;
    if (GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC) {
      switch (GST_PAD_TEMPLATE_PRESENCE (templ)) {
        case GST_PAD_ALWAYS:
        case GST_PAD_SOMETIMES:
          if (strstr (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ), "%"))
            potential_src_pads += 2;    /* Might make multiple pads */
          else
            potential_src_pads += 1;
          break;
        case GST_PAD_REQUEST:
          potential_src_pads += 2;
          break;
      }
    }
    walk = g_list_next (walk);
  }

  if (potential_src_pads < 2)
    return FALSE;

  return TRUE;
}

/*
 * given a list of element factories, try to link one of the factories
 * to the given pad.
 *
 * The function returns the element that was successfully linked to the
 * pad.
 */
static GstElement *
try_to_link_1 (GstDecodeBin * decode_bin, GstElement * srcelement, GstPad * pad,
    GList * factories)
{
  GList *walk;
  GstElement *result = NULL;
  gboolean isdemux = FALSE;
  GstPad *queuesinkpad = NULL, *queuesrcpad = NULL;
  GstElement *queue = NULL;
  GstPad *usedsrcpad = pad;

  /* Check if the parent of the src pad is a demuxer */
  isdemux = is_demuxer_element (srcelement);

  if (isdemux && factories != NULL) {
    GstPadLinkReturn dqlink;

    /* Insert a queue between demuxer and decoder */
    GST_DEBUG_OBJECT (decode_bin,
        "Element %s is a demuxer, inserting a queue",
        GST_OBJECT_NAME (srcelement));
    queue = gst_element_factory_make ("queue", NULL);
    decode_bin->queue_type = G_OBJECT_TYPE (queue);

    g_object_set (G_OBJECT (queue), "max-size-buffers", 0, NULL);
    g_object_set (G_OBJECT (queue), "max-size-time", G_GINT64_CONSTANT (0),
        NULL);
    g_object_set (G_OBJECT (queue), "max-size-bytes", 8192, NULL);
    gst_bin_add (GST_BIN (decode_bin), queue);
    gst_element_set_state (queue, GST_STATE_READY);
    queuesinkpad = gst_element_get_static_pad (queue, "sink");
    usedsrcpad = queuesrcpad = gst_element_get_static_pad (queue, "src");

    dqlink = gst_pad_link (pad, queuesinkpad);
    g_return_val_if_fail (dqlink == GST_PAD_LINK_OK, NULL);
  }

  /* loop over the factories */
  for (walk = factories; walk; walk = g_list_next (walk)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (walk->data);
    GstElementFactory *src_factory;
    GstElement *element;
    GstPadLinkReturn ret;
    GstPad *sinkpad;

    GST_DEBUG_OBJECT (decode_bin, "trying to link %s to %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
        GST_OBJECT_NAME (srcelement));

    /* don't plug the same parser twice, but allow multiple
     * instances of other elements (e.g. id3demux) in a row */
    src_factory = gst_element_get_factory (srcelement);
    if (src_factory == factory
        && gst_element_factory_list_is_type (factory,
            GST_ELEMENT_FACTORY_TYPE_PARSER)) {
      GST_DEBUG_OBJECT (decode_bin,
          "not inserting parser element %s twice in a row, skipping",
          GST_PLUGIN_FEATURE_NAME (factory));
      continue;
    }

    /* make an element from the factory first */
    if ((element = gst_element_factory_create (factory, NULL)) == NULL) {
      /* hmm, strange. Like with all things in life, let's move on.. */
      GST_WARNING_OBJECT (decode_bin, "could not create an element from %s",
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
      continue;
    }

    /* try to link the given pad to a sinkpad */
    /* FIXME, find the sinkpad by looping over the pads instead of
     * looking it up by name */
    if ((sinkpad = gst_element_get_static_pad (element, "sink")) == NULL) {
      /* if no pad is found we can't do anything */
      GST_WARNING_OBJECT (decode_bin, "could not find sinkpad in element");
      continue;
    }

    /* now add the element to the bin first */
    GST_DEBUG_OBJECT (decode_bin, "adding %s", GST_OBJECT_NAME (element));
    gst_bin_add (GST_BIN (decode_bin), element);

    /* set to READY first so it is ready, duh. */
    if (gst_element_set_state (element,
            GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (decode_bin, "Couldn't set %s to READY",
          GST_ELEMENT_NAME (element));
      /* get rid of the sinkpad */
      gst_object_unref (sinkpad);
      /* this element did not work, remove it again and continue trying
       * other elements, the element will be disposed. */
      /* FIXME: shouldn't we do this before adding it to the bin so that no
       * error messages get through to the app? (tpm) */
      gst_bin_remove (GST_BIN (decode_bin), element);
      continue;
    }

    if ((ret = gst_pad_link (usedsrcpad, sinkpad)) != GST_PAD_LINK_OK) {
      GST_DEBUG_OBJECT (decode_bin, "link failed on pad %s:%s, reason %d",
          GST_DEBUG_PAD_NAME (pad), ret);
      /* get rid of the sinkpad */
      gst_object_unref (sinkpad);
      /* this element did not work, remove it again and continue trying
       * other elements, the element will be disposed. */
      gst_element_set_state (element, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (decode_bin), element);
    } else {
      GST_DEBUG_OBJECT (decode_bin, "linked on pad %s:%s",
          GST_DEBUG_PAD_NAME (usedsrcpad));

      /* configure the queue some more */
      if (queue != NULL) {
        decode_bin->queues = g_list_append (decode_bin->queues, queue);
        g_signal_connect (G_OBJECT (queue),
            "overrun", G_CALLBACK (queue_filled_cb), decode_bin);
        g_signal_connect (G_OBJECT (queue),
            "underrun", G_CALLBACK (queue_underrun_cb), decode_bin);
      }

      /* The link worked, now figure out what it was that we connected */

      /* make sure we catch unlink signals */
      g_signal_connect (G_OBJECT (pad), "unlinked",
          G_CALLBACK (unlinked), decode_bin);

      /* now that we added the element we can try to continue autoplugging
       * on it until we have a raw type */
      close_link (element, decode_bin);

      /* change the state of the element to that of the parent */
      if ((gst_element_set_state (element,
                  GST_STATE_PAUSED)) == GST_STATE_CHANGE_FAILURE) {
        GST_WARNING_OBJECT (decode_bin, "Couldn't set %s to PAUSED",
            GST_ELEMENT_NAME (element));
        /* close_link -> close_pad_link -> might have set up a pad probe */
        free_pad_probe_for_element (decode_bin, element);
        gst_element_set_state (element, GST_STATE_NULL);
        gst_bin_remove (GST_BIN (decode_bin), element);
        continue;
      }

      result = element;

      /* get rid of the sinkpad now */
      gst_object_unref (sinkpad);

      /* Set the queue to paused and set the pointer to NULL so we don't
       * remove it below */
      if (queue != NULL) {
        gst_element_set_state (queue, GST_STATE_PAUSED);
        queue = NULL;
        gst_object_unref (queuesrcpad);
        gst_object_unref (queuesinkpad);
      }

      /* and exit */
      goto done;
    }
  }
done:
  if (queue != NULL) {
    /* We didn't successfully connect to the queue */
    gst_pad_unlink (pad, queuesinkpad);
    gst_element_set_state (queue, GST_STATE_NULL);
    gst_object_unref (queuesrcpad);
    gst_object_unref (queuesinkpad);
    gst_bin_remove (GST_BIN (decode_bin), queue);
  }
  return result;
}

static GstPad *
get_our_ghost_pad (GstDecodeBin * decode_bin, GstPad * pad)
{
  GstIterator *pad_it = NULL;
  GstPad *db_pad = NULL;
  gboolean done = FALSE;

  if (pad == NULL || !GST_PAD_IS_SRC (pad)) {
    GST_DEBUG_OBJECT (decode_bin, "pad NULL or not SRC pad");
    return NULL;
  }

  /* our ghostpads are the sourcepads */
  pad_it = gst_element_iterate_src_pads (GST_ELEMENT (decode_bin));
  while (!done) {
    db_pad = NULL;
    switch (gst_iterator_next (pad_it, (gpointer) & db_pad)) {
      case GST_ITERATOR_OK:
        GST_DEBUG_OBJECT (decode_bin, "looking at pad %s:%s",
            GST_DEBUG_PAD_NAME (db_pad));
        if (GST_IS_GHOST_PAD (db_pad)) {
          GstPad *target_pad = NULL;

          target_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (db_pad));
          done = (target_pad == pad);
          if (target_pad)
            gst_object_unref (target_pad);

          if (done) {
            /* Found our ghost pad */
            GST_DEBUG_OBJECT (decode_bin, "found ghostpad %s:%s for pad %s:%s",
                GST_DEBUG_PAD_NAME (db_pad), GST_DEBUG_PAD_NAME (pad));
            break;
          }
        }
        /* Not the right one */
        gst_object_unref (db_pad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pad_it);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (pad_it);

  return db_pad;
}

/* remove all downstream elements starting from the given pad.
 * Also make sure to remove the ghostpad we created for the raw
 * decoded stream.
 */
static void
remove_element_chain (GstDecodeBin * decode_bin, GstPad * pad)
{
  GstIterator *iter;
  gboolean done = FALSE;
  gpointer item;
  GstElement *elem = GST_ELEMENT (GST_OBJECT_PARENT (pad));

  while (GST_OBJECT_PARENT (elem) &&
      GST_OBJECT_PARENT (elem) != GST_OBJECT (decode_bin))
    elem = GST_ELEMENT (GST_OBJECT_PARENT (elem));

  if (G_OBJECT_TYPE (elem) == decode_bin->queue_type) {
    GST_DEBUG_OBJECT (decode_bin,
        "Encountered demuxer output queue while removing element chain");
    decode_bin->queues = g_list_remove (decode_bin->queues, elem);
  }

  GST_DEBUG_OBJECT (decode_bin, "%s:%s", GST_DEBUG_PAD_NAME (pad));
  iter = gst_pad_iterate_internal_links (pad);
  if (!iter)
    goto no_iter;

  /* remove all elements linked to this pad up to the ghostpad
   * that we created for this stream */
  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:{
        GstPad *pad;
        GstPad *ghostpad;
        GstPad *peer;

        pad = GST_PAD (item);
        GST_DEBUG_OBJECT (decode_bin, "inspecting internal pad %s:%s",
            GST_DEBUG_PAD_NAME (pad));

        ghostpad = get_our_ghost_pad (decode_bin, pad);
        if (ghostpad) {
          GST_DEBUG_OBJECT (decode_bin, "found our ghost pad %s:%s for %s:%s",
              GST_DEBUG_PAD_NAME (ghostpad), GST_DEBUG_PAD_NAME (pad));

          g_signal_emit (G_OBJECT (decode_bin),
              gst_decode_bin_signals[SIGNAL_REMOVED_DECODED_PAD], 0, ghostpad);

          gst_element_remove_pad (GST_ELEMENT (decode_bin), ghostpad);
          gst_object_unref (ghostpad);
          continue;
        } else {
          GST_DEBUG_OBJECT (decode_bin, "not one of our ghostpads");
        }

        peer = gst_pad_get_peer (pad);
        if (peer) {
          GstObject *parent = gst_pad_get_parent (peer);

          GST_DEBUG_OBJECT (decode_bin,
              "internal pad %s:%s linked to pad %s:%s",
              GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (peer));

          if (parent) {
            GstObject *grandparent = gst_object_get_parent (parent);

            if (grandparent != NULL) {
              if (GST_ELEMENT (grandparent) != GST_ELEMENT (decode_bin)) {
                GST_DEBUG_OBJECT (decode_bin, "dead end pad %s:%s parent %s",
                    GST_DEBUG_PAD_NAME (peer), GST_OBJECT_NAME (grandparent));
              } else {
                GST_DEBUG_OBJECT (decode_bin,
                    "recursing element %s on pad %s:%s",
                    GST_ELEMENT_NAME (elem), GST_DEBUG_PAD_NAME (pad));
                remove_element_chain (decode_bin, peer);
              }
              gst_object_unref (grandparent);
            }
            gst_object_unref (parent);
          }
          gst_object_unref (peer);
        }
        gst_object_unref (item);
      }
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_ERROR_OBJECT (pad, "Could not iterate over internally linked pads");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  GST_DEBUG_OBJECT (decode_bin, "removing %s", GST_ELEMENT_NAME (elem));

  gst_iterator_free (iter);

no_iter:
  gst_element_set_state (elem, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (decode_bin), elem);
}

/* there are @bytes bytes in @queue, enlarge it
 *
 * Returns: new max number of bytes in @queue
 */
static guint
queue_enlarge (GstElement * queue, guint bytes, GstDecodeBin * decode_bin)
{
  /* Increase the queue size by 1Mbyte if it is over 1Mb, else double its current limit
   */
  if (bytes > 1024 * 1024)
    bytes += 1024 * 1024;
  else
    bytes *= 2;

  GST_DEBUG_OBJECT (decode_bin,
      "increasing queue %s max-size-bytes to %d", GST_ELEMENT_NAME (queue),
      bytes);
  g_object_set (G_OBJECT (queue), "max-size-bytes", bytes, NULL);

  return bytes;
}

/* this callback is called when our queues fills up or are empty
 * We then check the status of all other queues to make sure we
 * never have an empty and full queue at the same time since that
 * would block dataflow. In the case of a filled queue, we make
 * it larger.
 */
static void
queue_underrun_cb (GstElement * queue, GstDecodeBin * decode_bin)
{
  /* FIXME: we don't really do anything here for now. Ideally we should
   * see if some of the queues are filled and increase their values
   * in that case.
   * Note: be very careful with thread safety here as this underrun
   * signal is done from the streaming thread of queue srcpad which
   * is different from the pad_added (where we add the queue to the
   * list) and the overrun signals that are signalled from the
   * demuxer thread.
   */
  GST_DEBUG_OBJECT (decode_bin, "got underrun");
}

/* Make sure we don't have a full queue and empty queue situation */
static void
queue_filled_cb (GstElement * queue, GstDecodeBin * decode_bin)
{
  GList *tmp;
  gboolean increase = FALSE;
  guint bytes;

  /* get current byte level from the queue that is filled */
  g_object_get (G_OBJECT (queue), "current-level-bytes", &bytes, NULL);
  GST_DEBUG_OBJECT (decode_bin, "One of the queues is full at %d bytes", bytes);

  /* we do not buffer more than 20Mb */
  if (bytes > (20 * 1024 * 1024))
    goto too_large;

  /* check all other queue to see if one is empty, in that case
   * we need to enlarge @queue */
  for (tmp = decode_bin->queues; tmp; tmp = g_list_next (tmp)) {
    GstElement *aqueue = GST_ELEMENT (tmp->data);
    guint levelbytes = 0;

    if (aqueue != queue) {
      g_object_get (G_OBJECT (aqueue), "current-level-bytes", &levelbytes,
          NULL);
      if (levelbytes == 0) {
        /* yup, found an empty queue, we can stop the search and
         * need to enlarge the queue */
        increase = TRUE;
        break;
      }
    }
  }

  if (increase) {
    /* enlarge @queue */
    queue_enlarge (queue, bytes, decode_bin);
  } else {
    GST_DEBUG_OBJECT (decode_bin,
        "Queue is full but other queues are not empty, not doing anything");
  }
  return;

  /* errors */
too_large:
  {
    GST_WARNING_OBJECT (decode_bin,
        "Queue is bigger than 20Mbytes, something else is going wrong");
    return;
  }
}

/* This function will be called when a dynamic pad is created on an element.
 * We try to continue autoplugging on this new pad. */
static void
new_pad (GstElement * element, GstPad * pad, GstDynamic * dynamic)
{
  GstDecodeBin *decode_bin = dynamic->decode_bin;
  GstCaps *caps;
  gboolean more;

  GST_OBJECT_LOCK (decode_bin);
  if (decode_bin->shutting_down)
    goto shutting_down1;
  GST_OBJECT_UNLOCK (decode_bin);

  GST_STATE_LOCK (decode_bin);
  if (decode_bin->shutting_down)
    goto shutting_down2;

  /* see if any more pending dynamic connections exist */
  more = gst_decode_bin_is_dynamic (decode_bin);

  caps = gst_pad_get_caps (pad);
  close_pad_link (element, pad, caps, decode_bin, more);
  if (caps)
    gst_caps_unref (caps);
  GST_STATE_UNLOCK (decode_bin);

  return;

shutting_down1:
  {
    GST_DEBUG_OBJECT (decode_bin, "we are shutting down");
    GST_OBJECT_UNLOCK (decode_bin);
    return;
  }
shutting_down2:
  {
    GST_DEBUG_OBJECT (decode_bin, "we are shutting down");
    GST_STATE_UNLOCK (decode_bin);
    return;
  }
}

static void
dynamic_remove (GstDynamic * dynamic)
{
  GstDecodeBin *decode_bin = dynamic->decode_bin;

  /* remove the dynamic from the list of dynamics */
  decode_bin->dynamics = g_list_remove (decode_bin->dynamics, dynamic);
  dynamic_free (dynamic);

  /* if we have no more dynamic elements, we have no chance of creating
   * more pads, so we fire the no_more_pads signal */
  if (decode_bin->dynamics == NULL) {
    if (decode_bin->numwaiting == 0) {
      GST_DEBUG_OBJECT (decode_bin,
          "no more dynamic elements, removing fakesink");
      remove_fakesink (decode_bin);
    }
    GST_DEBUG_OBJECT (decode_bin,
        "no more dynamic elements, signaling no_more_pads");
    gst_element_no_more_pads (GST_ELEMENT (decode_bin));
  } else {
    GST_DEBUG_OBJECT (decode_bin, "we have more dynamic elements");
  }
}

/* this signal is fired when an element signals the no_more_pads signal.
 * This means that the element will not generate more dynamic pads and
 * we can remove the element from the list of dynamic elements. When we
 * have no more dynamic elements in the pipeline, we can fire a no_more_pads
 * signal ourselves. */
static void
no_more_pads (GstElement * element, GstDynamic * dynamic)
{
  GST_DEBUG_OBJECT (dynamic->decode_bin, "no more pads on element %s",
      GST_ELEMENT_NAME (element));

  dynamic_remove (dynamic);
}

static void
new_caps (GstPad * pad, GParamSpec * unused, GstDynamic * dynamic)
{
  GST_DEBUG_OBJECT (dynamic->decode_bin, "delayed link triggered");

  new_pad (dynamic->element, pad, dynamic);

  /* assume it worked and remove the dynamic */
  dynamic_remove (dynamic);

  return;
}

static gboolean
is_our_kid (GstElement * e, GstDecodeBin * decode_bin)
{
  gboolean ret;
  GstElement *parent;

  parent = (GstElement *) gst_object_get_parent ((GstObject *) e);
  ret = (parent == (GstElement *) decode_bin);

  if (parent)
    gst_object_unref ((GstObject *) parent);

  return ret;
}

static gboolean
elem_is_dynamic (GstElement * element, GstDecodeBin * decode_bin)
{
  GList *pads;

  /* loop over all the padtemplates */
  for (pads = GST_ELEMENT_GET_CLASS (element)->padtemplates; pads;
      pads = g_list_next (pads)) {
    GstPadTemplate *templ = GST_PAD_TEMPLATE (pads->data);
    const gchar *templ_name;

    /* we are only interested in source pads */
    if (GST_PAD_TEMPLATE_DIRECTION (templ) != GST_PAD_SRC)
      continue;

    templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);
    GST_DEBUG_OBJECT (decode_bin, "got a source pad template %s", templ_name);

    /* figure out what kind of pad this is */
    switch (GST_PAD_TEMPLATE_PRESENCE (templ)) {
      case GST_PAD_SOMETIMES:
      {
        /* try to get the pad to see if it is already created or
         * not */
        GstPad *pad = gst_element_get_static_pad (element, templ_name);

        if (pad) {
          GST_DEBUG_OBJECT (decode_bin, "got the pad for sometimes template %s",
              templ_name);
          gst_object_unref (pad);
        } else {
          GST_DEBUG_OBJECT (decode_bin,
              "did not get the sometimes pad of template %s", templ_name);
          /* we have an element that will create dynamic pads */
          return TRUE;
        }
        break;
      }
      default:
        /* Don't care about ALWAYS or REQUEST pads */
        break;
    }
  }
  return FALSE;
}

/* This function will be called when a pad is disconnected for some reason */
static void
unlinked (GstPad * pad, GstPad * peerpad, GstDecodeBin * decode_bin)
{
  GstElement *element, *peer;

  /* inactivate pad */
  gst_pad_set_active (pad, GST_ACTIVATE_NONE);

  peer = gst_pad_get_parent_element (peerpad);

  if (!is_our_kid (peer, decode_bin))
    goto exit;

  GST_DEBUG_OBJECT (decode_bin, "pad %s:%s removal while alive - chained?",
      GST_DEBUG_PAD_NAME (pad));

  /* remove all elements linked to the peerpad */
  remove_element_chain (decode_bin, peerpad);

  /* Re-add as a dynamic element if needed, via close_link */
  element = gst_pad_get_parent_element (pad);
  if (element) {
    if (elem_is_dynamic (element, decode_bin))
      dynamic_add (element, NULL, decode_bin);

    gst_object_unref (element);
  }

exit:
  gst_object_unref (peer);
}

/* this function inspects the given element and tries to connect something
 * on the srcpads. If there are dynamic pads, it sets up a signal handler to
 * continue autoplugging when they become available */
static void
close_link (GstElement * element, GstDecodeBin * decode_bin)
{
  GList *pads;
  gboolean dynamic = FALSE;
  GList *to_connect = NULL;
  gboolean more;

  GST_DEBUG_OBJECT (decode_bin, "closing links with element %s",
      GST_ELEMENT_NAME (element));

  /* loop over all the padtemplates */
  for (pads = GST_ELEMENT_GET_CLASS (element)->padtemplates; pads;
      pads = g_list_next (pads)) {
    GstPadTemplate *templ = GST_PAD_TEMPLATE (pads->data);
    const gchar *templ_name;

    /* we are only interested in source pads */
    if (GST_PAD_TEMPLATE_DIRECTION (templ) != GST_PAD_SRC)
      continue;

    templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);
    GST_DEBUG_OBJECT (decode_bin, "got a source pad template %s", templ_name);

    /* figure out what kind of pad this is */
    switch (GST_PAD_TEMPLATE_PRESENCE (templ)) {
      case GST_PAD_ALWAYS:
      {
        /* get the pad that we need to autoplug */
        GstPad *pad = gst_element_get_static_pad (element, templ_name);

        if (pad) {
          GST_DEBUG_OBJECT (decode_bin, "got the pad for always template %s",
              templ_name);
          /* here is the pad, we need to autoplug it */
          to_connect = g_list_prepend (to_connect, pad);
        } else {
          /* strange, pad is marked as always but it's not
           * there. Fix the element */
          GST_WARNING_OBJECT (decode_bin,
              "could not get the pad for always template %s", templ_name);
        }
        break;
      }
      case GST_PAD_SOMETIMES:
      {
        /* try to get the pad to see if it is already created or
         * not */
        GstPad *pad = gst_element_get_static_pad (element, templ_name);

        if (pad) {
          GST_DEBUG_OBJECT (decode_bin, "got the pad for sometimes template %s",
              templ_name);
          /* the pad is created, we need to autoplug it */
          to_connect = g_list_prepend (to_connect, pad);
        } else {
          GST_DEBUG_OBJECT (decode_bin,
              "did not get the sometimes pad of template %s", templ_name);
          /* we have an element that will create dynamic pads */
          dynamic = TRUE;
        }
        break;
      }
      case GST_PAD_REQUEST:
        /* ignore request pads */
        GST_DEBUG_OBJECT (decode_bin, "ignoring request padtemplate %s",
            templ_name);
        break;
    }
  }
  if (dynamic) {
    GST_DEBUG_OBJECT (decode_bin, "got a dynamic element here");
    /* ok, this element has dynamic pads, set up the signal handlers to be
     * notified of them */

    dynamic_add (element, NULL, decode_bin);
  }

  /* Check if this is an element with more than 1 pad. If this element
   * has more than 1 pad, we need to be careful not to signal the
   * no_more_pads signal after connecting the first pad. */
  more = g_list_length (to_connect) > 1;

  /* now loop over all the pads we need to connect */
  for (pads = to_connect; pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD_CAST (pads->data);
    GstCaps *caps;

    /* we have more pads if we have more than 1 pad to connect or
     * dynamics. If we have only 1 pad and no dynamics, more will be
     * set to FALSE and the no-more-pads signal will be fired. Note
     * that this can change after the close_pad_link call. */
    more |= gst_decode_bin_is_dynamic (decode_bin);

    GST_DEBUG_OBJECT (decode_bin, "closing pad link for %s",
        GST_OBJECT_NAME (pad));

    /* continue autoplugging on the pads */
    caps = gst_pad_get_caps (pad);
    close_pad_link (element, pad, caps, decode_bin, more);
    if (caps)
      gst_caps_unref (caps);

    gst_object_unref (pad);
  }
  g_list_free (to_connect);
}

/* this is the signal handler for the typefind element have_type signal.
 * It tries to continue autoplugging on the typefind src pad */
static void
type_found (GstElement * typefind, guint probability, GstCaps * caps,
    GstDecodeBin * decode_bin)
{
  gboolean dynamic;
  GstPad *pad;

  GST_DEBUG_OBJECT (decode_bin, "typefind found caps %" GST_PTR_FORMAT, caps);

  GST_OBJECT_LOCK (decode_bin);
  if (decode_bin->shutting_down)
    goto shutting_down;
  GST_OBJECT_UNLOCK (decode_bin);

  GST_STATE_LOCK (decode_bin);
  if (decode_bin->shutting_down)
    goto exit;

  /* don't need the typefind anymore if we already found a type, we're not going
   * to be able to do anything with it anyway except for generating errors */
  if (decode_bin->have_type)
    goto exit;

  decode_bin->have_type = TRUE;

  /* special-case text/plain: we only want to accept it as a raw type if it
   * comes from a subtitle parser element or a demuxer, but not if it is the
   * type of the entire stream, in which case we just want to error out */
  if (typefind == decode_bin->typefind &&
      gst_structure_has_name (gst_caps_get_structure (caps, 0), "text/plain")) {
    gst_element_no_more_pads (GST_ELEMENT (decode_bin));
    /* we can't handle this type of stream */
    GST_ELEMENT_ERROR (decode_bin, STREAM, WRONG_TYPE,
        (_("This appears to be a text file")),
        ("decodebin cannot decode plain text files"));
    goto exit;
  }

  /* autoplug the new pad with the caps that the signal gave us. */
  pad = gst_element_get_static_pad (typefind, "src");
  close_pad_link (typefind, pad, caps, decode_bin, FALSE);
  gst_object_unref (pad);

  dynamic = gst_decode_bin_is_dynamic (decode_bin);
  if (dynamic == FALSE) {
    GST_DEBUG_OBJECT (decode_bin, "we have no dynamic elements anymore");
    /* if we have no dynamic elements, we know that no new pads
     * will be created and we can signal out no_more_pads signal */
    gst_element_no_more_pads (GST_ELEMENT (decode_bin));
  } else {
    /* more dynamic elements exist that could create new pads */
    GST_DEBUG_OBJECT (decode_bin, "we have more dynamic elements");
  }

exit:
  GST_STATE_UNLOCK (decode_bin);
  return;

shutting_down:
  {
    GST_DEBUG_OBJECT (decode_bin, "we are shutting down");
    GST_OBJECT_UNLOCK (decode_bin);
    return;
  }
}

static void
disconnect_unlinked_signals (GstDecodeBin * decode_bin, GstElement * element)
{
  GstIterator *pad_it = NULL;
  gboolean done = FALSE;

  pad_it = gst_element_iterate_src_pads (element);
  while (!done) {
    GstPad *pad = NULL;

    switch (gst_iterator_next (pad_it, (gpointer) & pad)) {
      case GST_ITERATOR_OK:
        g_signal_handlers_disconnect_by_func (pad, (gpointer) unlinked,
            decode_bin);
        gst_object_unref (pad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pad_it);
        break;
      default:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (pad_it);
}


static void
cleanup_decodebin (GstDecodeBin * decode_bin)
{
  GstIterator *elem_it = NULL, *gpad_it = NULL;
  GstPad *typefind_pad = NULL;
  gboolean done = FALSE;

  g_return_if_fail (GST_IS_DECODE_BIN (decode_bin));

  GST_DEBUG_OBJECT (decode_bin, "cleaning up decodebin");

  typefind_pad = gst_element_get_static_pad (decode_bin->typefind, "src");
  if (GST_IS_PAD (typefind_pad)) {
    g_signal_handlers_block_by_func (typefind_pad, (gpointer) unlinked,
        decode_bin);
  }

  elem_it = gst_bin_iterate_elements (GST_BIN (decode_bin));
  while (!done) {
    GstElement *element = NULL;

    switch (gst_iterator_next (elem_it, (gpointer) & element)) {
      case GST_ITERATOR_OK:
        if (element != decode_bin->typefind && element != decode_bin->fakesink) {
          GST_DEBUG_OBJECT (element, "removing autoplugged element");
          disconnect_unlinked_signals (decode_bin, element);
          gst_element_set_state (element, GST_STATE_NULL);
          gst_bin_remove (GST_BIN (decode_bin), element);
        }
        gst_object_unref (element);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (elem_it);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (elem_it);

  done = FALSE;
  gpad_it = gst_element_iterate_pads (GST_ELEMENT (decode_bin));
  while (!done) {
    GstPad *pad = NULL;

    switch (gst_iterator_next (gpad_it, (gpointer) & pad)) {
      case GST_ITERATOR_OK:
        GST_DEBUG_OBJECT (pad, "inspecting pad %s:%s",
            GST_DEBUG_PAD_NAME (pad));
        if (GST_IS_GHOST_PAD (pad) && GST_PAD_IS_SRC (pad)) {
          GST_DEBUG_OBJECT (pad, "removing ghost pad");
          gst_element_remove_pad (GST_ELEMENT (decode_bin), pad);
        }
        gst_object_unref (pad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (gpad_it);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (gpad_it);

  if (GST_IS_PAD (typefind_pad)) {
    g_signal_handlers_unblock_by_func (typefind_pad, (gpointer) unlinked,
        decode_bin);
    g_signal_handlers_disconnect_by_func (typefind_pad, (gpointer) unlinked,
        decode_bin);
    gst_object_unref (typefind_pad);
  }
}

static GstStateChangeReturn
gst_decode_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDecodeBin *decode_bin;

  decode_bin = GST_DECODE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      decode_bin->numpads = 0;
      decode_bin->numwaiting = 0;
      decode_bin->dynamics = NULL;
      if (decode_bin->typefind == NULL)
        goto missing_typefind;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (decode_bin);
      decode_bin->shutting_down = FALSE;
      decode_bin->have_type = FALSE;
      GST_OBJECT_UNLOCK (decode_bin);

      if (!add_fakesink (decode_bin))
        goto missing_fakesink;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (decode_bin);
      decode_bin->shutting_down = TRUE;
      GST_OBJECT_UNLOCK (decode_bin);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      free_dynamics (decode_bin);
      free_pad_probes (decode_bin);
      cleanup_decodebin (decode_bin);
      break;
    default:
      break;
  }

  return ret;
/* ERRORS */
missing_typefind:
  {
    gst_element_post_message (element,
        gst_missing_element_message_new (element, "typefind"));
    GST_ELEMENT_ERROR (element, CORE, MISSING_PLUGIN, (NULL), ("no typefind!"));
    return GST_STATE_CHANGE_FAILURE;
  }
missing_fakesink:
  {
    gst_element_post_message (element,
        gst_missing_element_message_new (element, "fakesink"));
    GST_ELEMENT_ERROR (element, CORE, MISSING_PLUGIN, (NULL), ("no fakesink!"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_decode_bin_debug, "decodebin", 0, "decoder bin");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  return gst_element_register (plugin, "decodebin", GST_RANK_NONE,
      GST_TYPE_DECODE_BIN);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "decodebin",
    "decoder bin", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
