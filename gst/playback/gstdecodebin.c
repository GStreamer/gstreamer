/* GStreamer
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
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

#define GST_TYPE_DECODE_BIN 		(gst_decode_bin_get_type())
#define GST_DECODE_BIN(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECODE_BIN,GstDecodeBin))
#define GST_DECODE_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECODE_BIN,GstDecodeBinClass))
#define GST_IS_DECODE_BIN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECODE_BIN))
#define GST_IS_DECODE_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECODE_BIN))

typedef struct _GstDecodeBin GstDecodeBin;
typedef struct _GstDecodeBinClass GstDecodeBinClass;

struct _GstDecodeBin
{
  GstBin bin;                   /* we extend GstBin */

  GstElement *typefind;         /* this holds the typefind object */

  gboolean threaded;            /* indicating threaded execution is desired */
  GList *dynamics;              /* list of dynamic connections */

  GList *factories;             /* factories we can use for selecting elements */
  gint numpads;

  GList *elements;              /* elements we added in autoplugging */

  guint have_type_id;           /* signal id for the typefind element */
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

/* props */
enum
{
  ARG_0,
  ARG_THREADED,
};

/* signals */
enum
{
  SIGNAL_NEW_DECODED_PAD,
  SIGNAL_REMOVED_DECODED_PAD,
  SIGNAL_UNKNOWN_TYPE,
  LAST_SIGNAL
};

/* this structure is created for all dynamic pads that could get created
 * at runtime */
typedef struct
{
  gint np_sig_id;               /* signal id of new_pad */
  gint unlink_sig_id;           /* signal id of unlinked */
  gint nmp_sig_id;              /* signal id of no_more_pads */
  GstElement *element;          /* the element sending the signal */
  GstDecodeBin *decode_bin;     /* pointer to ourself */
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
    GstCaps * caps, GstDecodeBin * decode_bin, gboolean more);
static void unlinked (GstPad * pad, GstPad * peerpad,
    GstDecodeBin * decode_bin);

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

  gst_decode_bin_signals[SIGNAL_NEW_DECODED_PAD] =
      g_signal_new ("new-decoded-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDecodeBinClass, new_decoded_pad), NULL, NULL,
      gst_play_marshal_VOID__OBJECT_BOOLEAN, G_TYPE_NONE, 2, GST_TYPE_PAD,
      G_TYPE_BOOLEAN);
  gst_decode_bin_signals[SIGNAL_REMOVED_DECODED_PAD] =
      g_signal_new ("removed-decoded-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDecodeBinClass, removed_decoded_pad), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE] =
      g_signal_new ("unknown-type", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, unknown_type),
      NULL, NULL, gst_marshal_VOID__OBJECT_BOXED, G_TYPE_NONE, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_decode_bin_dispose);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&decoder_bin_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&decoder_bin_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_decode_bin_details);

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
  /* only demuxers and decoders can play */
  if (strstr (klass, "Demux") == NULL &&
      strstr (klass, "Decoder") == NULL && strstr (klass, "Parse") == NULL) {
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

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;
  return strcmp (gst_plugin_feature_get_name (f2),
      gst_plugin_feature_get_name (f1));
}

static void
print_feature (GstPluginFeature * feature)
{
  GST_DEBUG ("%s", gst_plugin_feature_get_name (feature));
}

static void
gst_decode_bin_init (GstDecodeBin * decode_bin)
{
  GList *factories;

  /* first filter out the interesting element factories */
  factories = gst_registry_pool_feature_filter (
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
    /* add the typefind element */
    gst_bin_add (GST_BIN (decode_bin), decode_bin->typefind);
    /* ghost the sink pad to ourself */
    gst_element_add_ghost_pad (GST_ELEMENT (decode_bin),
        gst_element_get_pad (decode_bin->typefind, "sink"), "sink");

    /* connect a signal to find out when the typefind element found
     * a type */
    decode_bin->have_type_id =
        g_signal_connect (G_OBJECT (decode_bin->typefind), "have_type",
        G_CALLBACK (type_found), decode_bin);
  }

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
    templates = gst_element_factory_get_pad_templates (factory);
    for (walk = (GList *) templates; walk; walk = g_list_next (walk)) {
      GstPadTemplate *templ = GST_PAD_TEMPLATE (walk->data);

      /* we only care about the sink templates */
      if (templ->direction == GST_PAD_SINK) {
        GstCaps *intersect;

        /* try to intersect the caps with the caps of the template */
        intersect =
            gst_caps_intersect (caps, gst_pad_template_get_caps (templ));
        /* check if the intersection is empty */
        if (!gst_caps_is_empty (intersect)) {
          /* non empty intersection, we can use this element */
          to_try = g_list_append (to_try, factory);
          gst_caps_free (intersect);
          break;
        }
        gst_caps_free (intersect);
      }
    }
  }
  return to_try;
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
  GList *to_try;
  GstStructure *structure;
  const gchar *mimetype;

  if (!strncmp (gst_pad_get_name (pad), "current_", 8))
    return;

  /* the caps is empty, this means the pad has no type, we can only
   * decide to fire the unknown_type signal. */
  if (caps == NULL || gst_caps_is_empty (caps)) {
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, pad, caps);
    return;
  }

  /* the caps is any, this means the pad can be anything and
   * we don't know yet */
  if (gst_caps_is_any (caps)) {
    return;
  }

  GST_LOG_OBJECT (element, "trying to close %" GST_PTR_FORMAT, caps);

  /* FIXME, iterate over more structures? I guess it is possible that
   * this pad has some encoded and some raw pads. This code will fail
   * then if the first structure is not the raw type... */
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* first see if this is raw. If the type is raw, we can
   * create a ghostpad for this pad. */
  if (g_str_has_prefix (mimetype, "video/x-raw") ||
      g_str_has_prefix (mimetype, "audio/x-raw") ||
      g_str_has_prefix (mimetype, "text/plain")) {
    gchar *padname;
    GstPad *ghost;

    /* make a unique name for this new pad */
    padname = g_strdup_printf ("src%d", decode_bin->numpads);
    decode_bin->numpads++;

    /* make it a ghostpad */
    ghost = gst_element_add_ghost_pad (GST_ELEMENT (decode_bin), pad, padname);

    GST_LOG_OBJECT (element, "closed pad %s", padname);

    /* our own signal with an extra flag that this is the only pad */
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_NEW_DECODED_PAD], 0, ghost, !more);

    g_free (padname);
    return;
  }

  if (gst_caps_get_size (caps) == 1) {
    /* then continue plugging, first find all compatible elements */
    to_try = find_compatibles (decode_bin, caps);
    if (to_try == NULL) {
      /* no compatible elements, fire the unknown_type signal, we cannot go
       * on */
      g_signal_emit (G_OBJECT (decode_bin),
          gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, pad, caps);
      return;
    }
    try_to_link_1 (decode_bin, pad, to_try);
  } else {
    GST_LOG_OBJECT (element, "multiple possibilities, delaying");
  }
}

/* given a list of element factories, try to link one of the factories
 * to the given pad */
static GstElement *
try_to_link_1 (GstDecodeBin * decode_bin, GstPad * pad, GList * factories)
{
  GList *walk;

  /* loop over the factories */
  for (walk = factories; walk; walk = g_list_next (walk)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (walk->data);
    GstElement *element;
    gboolean ret;

    GST_DEBUG_OBJECT (decode_bin, "trying to link %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

    /* make an element from the factory first */
    element = gst_element_factory_create (factory, NULL);
    if (element == NULL) {
      /* hmm, strange. Like with all things in live, let's move on.. */
      GST_WARNING_OBJECT (decode_bin, "could not create  an element from %s",
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
      continue;
    }

    /* now add the element to the bin first */
    GST_DEBUG_OBJECT (decode_bin, "adding %s", gst_element_get_name (element));
    gst_bin_add (GST_BIN (decode_bin), element);

    /* set to ready first so it can do negotiation */
    gst_element_set_state (element, GST_STATE_READY);

    /* keep out own list of elements */
    decode_bin->elements = g_list_prepend (decode_bin->elements, element);

    /* try to link the given pad to a sinkpad */
    /* FIXME, find the sinkpad by looping over the pads instead of 
     * looking it up by name */
    ret = gst_pad_link (pad, gst_element_get_pad (element, "sink"));
    if (ret) {
      const gchar *klass;
      GstElementFactory *factory;
      guint sig;

      GST_DEBUG_OBJECT (decode_bin, "linked on pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));

      /* The link worked, now figure out what it was that we connected */
      factory = gst_element_get_factory (element);
      klass = gst_element_factory_get_klass (factory);
      /* check if we can use threads */
      if (decode_bin->threaded) {
        if (strstr (klass, "Demux") != NULL) {
          /* FIXME, do something with threads here. Not sure that it 
           * really matters here but in general it is better to preroll
           * on encoded data from the muxer than on raw encoded streams
           * because that would consume less memory. */
        }
      }
      /* make sure we catch unlink signals */
      sig = g_signal_connect (G_OBJECT (pad), "unlinked",
          G_CALLBACK (unlinked), decode_bin);
      /* keep a ref to the signal id so that we can disconnect the signal callback */
      g_object_set_data (G_OBJECT (pad), "unlinked_id", GINT_TO_POINTER (sig));

      /* now that we added the element we can try to continue autoplugging
       * on it until we have a raw type */
      close_link (element, decode_bin);
      /* change the state of the element to that of the parent */
      gst_element_sync_state_with_parent (element);
      return element;
    } else {
      GST_DEBUG_OBJECT (decode_bin, "link failed on pad %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      /* this element did not work, remove it again and continue trying
       * other elements */
      gst_bin_remove (GST_BIN (decode_bin), element);
    }
  }
  return NULL;
}

static GstPad *
get_our_ghost_pad (GstDecodeBin * decode_bin, GstPad * pad)
{
  GList *ghostpads;

  if (pad == NULL || !GST_PAD_IS_SRC (pad)) {
    GST_DEBUG_OBJECT (decode_bin, "pad NULL or not SRC pad");
    return NULL;
  }

  if (GST_IS_GHOST_PAD (pad)) {
    GstElement *parent = gst_pad_get_parent (pad);

    GST_DEBUG_OBJECT (decode_bin, "pad parent %s",
        gst_element_get_name (parent));
    if (parent == GST_ELEMENT (decode_bin)) {
      GST_DEBUG_OBJECT (decode_bin, "pad is our ghostpad");
      return pad;
    } else {
      GST_DEBUG_OBJECT (decode_bin, "pad is ghostpad but not ours");
      return NULL;
    }
  }

  GST_DEBUG_OBJECT (decode_bin, "looping over ghostpads");
  ghostpads = gst_pad_get_ghost_pad_list (pad);
  while (ghostpads) {
    GstPad *ghostpad;

    ghostpad = get_our_ghost_pad (decode_bin, GST_PAD (ghostpads->data));
    if (ghostpad)
      return ghostpad;

    ghostpads = g_list_next (ghostpads);
  }
  GST_DEBUG_OBJECT (decode_bin, "done looping over ghostpads, nothing found");

  return NULL;
}

/* remove all downstream elements starting from the given pad.
 * Also make sure to remove the ghostpad we created for the raw 
 * decoded stream.
 */
static void
remove_element_chain (GstDecodeBin * decode_bin, GstPad * pad)
{
  GList *int_links;
  GstElement *elem = gst_pad_get_parent (pad);

  GST_DEBUG_OBJECT (decode_bin, "%s:%s", GST_DEBUG_PAD_NAME (pad));

  /* remove all elements linked to this pad up to the ghostpad 
   * that we created for this stream */
  for (int_links = gst_pad_get_internal_links (pad);
      int_links; int_links = g_list_next (int_links)) {
    GstPad *pad;
    GstPad *ghostpad;
    GstPad *peer;

    pad = GST_PAD (int_links->data);
    GST_DEBUG_OBJECT (decode_bin, "inspecting internal pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    ghostpad = get_our_ghost_pad (decode_bin, pad);
    if (ghostpad) {
      GST_DEBUG_OBJECT (decode_bin, "found our ghost pad %s:%s for %s:%s",
          GST_DEBUG_PAD_NAME (ghostpad), GST_DEBUG_PAD_NAME (pad));

      g_signal_emit (G_OBJECT (decode_bin),
          gst_decode_bin_signals[SIGNAL_REMOVED_DECODED_PAD], 0, ghostpad);

      gst_element_remove_pad (GST_ELEMENT (decode_bin), ghostpad);
      continue;
    } else {
      GST_DEBUG_OBJECT (decode_bin, "not one of our ghostpads");
    }

    peer = gst_pad_get_peer (pad);
    if (peer == NULL)
      continue;

    GST_DEBUG_OBJECT (decode_bin, "internal pad %s:%s linked to pad %s:%s",
        GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (peer));

    if (gst_pad_get_real_parent (peer) != GST_ELEMENT (decode_bin)) {
      GST_DEBUG_OBJECT (decode_bin, "dead end pad %s:%s",
          GST_DEBUG_PAD_NAME (peer));
    } else {
      GST_DEBUG_OBJECT (decode_bin, "recursing element %s on pad %s:%s",
          gst_element_get_name (elem), GST_DEBUG_PAD_NAME (pad));
      remove_element_chain (decode_bin, peer);
    }
  }
  GST_DEBUG_OBJECT (decode_bin, "removing %s", gst_element_get_name (elem));
  gst_bin_remove (GST_BIN (decode_bin), elem);
}

/* This function will be called when a dynamic pad is created on an element.
 * We try to continue autoplugging on this new pad. */
static void
new_pad (GstElement * element, GstPad * pad, GstDynamic * dynamic)
{
  GstDecodeBin *decode_bin = dynamic->decode_bin;
  GstCaps *caps;

  /* see if any more pending dynamic connections exist */
  gboolean more = gst_decode_bin_is_dynamic (decode_bin);

  caps = gst_pad_get_caps (pad);
  close_pad_link (element, pad, caps, decode_bin, more);
  if (caps)
    gst_caps_free (caps);
}

/* this signal is fired when an element signals the no_more_pads signal.
 * This means that the element will not generate more dynamic pads and
 * we can remove the element from the list of dynamic elements. When we
 * have no more dynamic elements in the pipeline, we can fire a no_more_pads
 * signal ourselves. */
static void
no_more_pads (GstElement * element, GstDynamic * dynamic)
{
  GstDecodeBin *decode_bin = dynamic->decode_bin;

  GST_DEBUG_OBJECT (decode_bin, "no more pads on element %s",
      gst_element_get_name (element));

  /* disconnect signals */
  g_signal_handler_disconnect (G_OBJECT (dynamic->element), dynamic->np_sig_id);
  g_signal_handler_disconnect (G_OBJECT (dynamic->element),
      dynamic->nmp_sig_id);

  /* remove the element from the list of dynamic elements */
  decode_bin->dynamics = g_list_remove (decode_bin->dynamics, dynamic);
  g_free (dynamic);

  /* if we have no more dynamic elements, we have no chance of creating
   * more pads, so we fire the no_more_pads signal */
  if (decode_bin->dynamics == NULL) {
    GST_DEBUG_OBJECT (decode_bin,
        "no more dynamic elements, signaling no_more_pads");
    gst_element_no_more_pads (GST_ELEMENT (decode_bin));
  } else {
    GST_DEBUG_OBJECT (decode_bin, "we have more dynamic elements");
  }
}

/* This function will be called when a pad is disconnected for some reason */
static void
unlinked (GstPad * pad, GstPad * peerpad, GstDecodeBin * decode_bin)
{
  GList *walk;
  GstDynamic *dyn;
  GstElement *element;

  /* inactivate pad */
  gst_pad_set_active (pad, FALSE);

  /* remove all elements linked to the peerpad */
  remove_element_chain (decode_bin, peerpad);

  /* if an element removes two pads, then we don't want this twice */
  element = gst_pad_get_parent (pad);
  for (walk = decode_bin->dynamics; walk != NULL; walk = walk->next) {
    dyn = walk->data;
    if (dyn->element == element)
      return;
  }

  GST_DEBUG_OBJECT (decode_bin, "pad removal while alive - chained?");

  /* re-setup dynamic plugging */
  dyn = g_new0 (GstDynamic, 1);
  dyn->np_sig_id = g_signal_connect (G_OBJECT (element), "new-pad",
      G_CALLBACK (new_pad), dyn);
  dyn->nmp_sig_id = g_signal_connect (G_OBJECT (element), "no-more-pads",
      G_CALLBACK (no_more_pads), dyn);
  dyn->element = element;
  dyn->decode_bin = decode_bin;

  /* and add this element to the dynamic elements */
  decode_bin->dynamics = g_list_prepend (decode_bin->dynamics, dyn);
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
      gst_element_get_name (element));

  /* loop over all the padtemplates */
  for (pads = gst_element_get_pad_template_list (element); pads;
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
        GstPad *pad = gst_element_get_pad (element, templ_name);

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
        GstPad *pad = gst_element_get_pad (element, templ_name);

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
    GstDynamic *dyn;

    GST_DEBUG_OBJECT (decode_bin, "got a dynamic element here");
    /* ok, this element has dynamic pads, set up the signal handlers to be
     * notified of them */
    dyn = g_new0 (GstDynamic, 1);
    dyn->np_sig_id = g_signal_connect (G_OBJECT (element), "new-pad",
        G_CALLBACK (new_pad), dyn);
    dyn->nmp_sig_id = g_signal_connect (G_OBJECT (element), "no-more-pads",
        G_CALLBACK (no_more_pads), dyn);
    dyn->element = element;
    dyn->decode_bin = decode_bin;

    /* and add this element to the dynamic elements */
    decode_bin->dynamics = g_list_prepend (decode_bin->dynamics, dyn);
  }

  /* Check if this is an element with more than 1 pad. If this element
   * has more than 1 pad, we need to be carefull not to signal the 
   * no_more_pads signal after connecting the first pad. */
  more = g_list_length (to_connect) > 1;

  /* now loop over all the pads we need to connect */
  for (pads = to_connect; pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD (pads->data);
    GstCaps *caps;

    /* we have more pads if we have more than 1 pad to connect or
     * dynamics. If we have only 1 pad and no dynamics, more will be
     * set to FALSE and the no-more-pads signal will be fired. Note
     * that this can change after the close_pad_link call. */
    more |= gst_decode_bin_is_dynamic (decode_bin);

    GST_DEBUG_OBJECT (decode_bin, "closing pad link for %s",
        gst_pad_get_name (pad));

    /* continue autoplugging on the pads */
    caps = gst_pad_get_caps (pad);
    close_pad_link (element, pad, caps, decode_bin, more);
    if (caps)
      gst_caps_free (caps);
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

  GST_DEBUG_OBJECT (decode_bin, "typefind found caps %" GST_PTR_FORMAT, caps);

  /* autoplug the new pad with the caps that the signal gave us. */
  close_pad_link (typefind, gst_element_get_pad (typefind, "src"), caps,
      decode_bin, FALSE);

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
