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
  GstElement *fakesink;

  gboolean threaded;            /* indicating threaded execution is desired */
  GList *dynamics;              /* list of dynamic connections */

  GList *factories;             /* factories we can use for selecting elements */
  gint numpads;
  gint numwaiting;

  GList *elements;              /* elements we added in autoplugging */

  guint have_type_id;           /* signal id for the typefind element */

  gboolean shutting_down;       /* stop pluggin if we're shutting down */
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

#define DEFAULT_THREADED	FALSE

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
  SIGNAL_REDIRECT,
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
static GstStateChangeReturn gst_decode_bin_change_state (GstElement * element,
    GstStateChange transition);

static void free_dynamics (GstDecodeBin * decode_bin);
static void type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin);
static GstElement *try_to_link_1 (GstDecodeBin * decode_bin, GstPad * pad,
    GList * factories);
static void close_link (GstElement * element, GstDecodeBin * decode_bin);
static void close_pad_link (GstElement * element, GstPad * pad,
    GstCaps * caps, GstDecodeBin * decode_bin, gboolean more);
static void unlinked (GstPad * pad, GstPad * peerpad,
    GstDecodeBin * decode_bin);
static void new_pad (GstElement * element, GstPad * pad, GstDynamic * dynamic);
static void no_more_pads (GstElement * element, GstDynamic * dynamic);

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
          DEFAULT_THREADED, G_PARAM_READWRITE));

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
      NULL, NULL, gst_marshal_VOID__OBJECT_OBJECT, G_TYPE_NONE, 2,
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
    GstPad *pad;

    /* add the typefind element */
    if (!gst_bin_add (GST_BIN (decode_bin), decode_bin->typefind)) {
      g_warning ("Could not add typefind element, decodebin will not work");
      gst_object_unref (decode_bin->typefind);
      decode_bin->typefind = NULL;
    }

    /* get the sinkpad */
    pad = gst_element_get_pad (decode_bin->typefind, "sink");

    /* ghost the sink pad to ourself */
    gst_element_add_pad (GST_ELEMENT (decode_bin),
        gst_ghost_pad_new ("sink", pad));

    gst_object_unref (pad);

    /* connect a signal to find out when the typefind element found
     * a type */
    decode_bin->have_type_id =
        g_signal_connect (G_OBJECT (decode_bin->typefind), "have_type",
        G_CALLBACK (type_found), decode_bin);
  }
  decode_bin->fakesink = gst_element_factory_make ("fakesink", "fakesink");
  if (!decode_bin->fakesink) {
    g_warning ("can't find fakesink element, decodebin will not work");
  } else {
    if (!gst_bin_add (GST_BIN (decode_bin), decode_bin->fakesink)) {
      g_warning ("Could not add fakesink element, decodebin will not work");
      gst_object_unref (decode_bin->fakesink);
      decode_bin->fakesink = NULL;
    }
  }

  decode_bin->threaded = DEFAULT_THREADED;
  decode_bin->dynamics = NULL;
}

static void dynamic_free (GstDynamic * dyn);

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

static GstDynamic *
dynamic_create (GstElement * element, GstDecodeBin * decode_bin)
{
  GstDynamic *dyn;

  GST_DEBUG_OBJECT (element, "dynamic create");

  /* take refs */
  gst_object_ref (element);
  gst_object_ref (decode_bin);

  dyn = g_new0 (GstDynamic, 1);
  dyn->element = element;
  dyn->decode_bin = decode_bin;
  dyn->np_sig_id = g_signal_connect (G_OBJECT (element), "pad-added",
      G_CALLBACK (new_pad), dyn);
  dyn->nmp_sig_id = g_signal_connect (G_OBJECT (element), "no-more-pads",
      G_CALLBACK (no_more_pads), dyn);

  return dyn;
}

static void
dynamic_free (GstDynamic * dyn)
{
  GST_DEBUG_OBJECT (dyn->decode_bin, "dynamic free");

  /* disconnect signals */
  g_signal_handler_disconnect (G_OBJECT (dyn->element), dyn->np_sig_id);
  g_signal_handler_disconnect (G_OBJECT (dyn->element), dyn->nmp_sig_id);

  gst_object_unref (dyn->element);
  gst_object_unref (dyn->decode_bin);
  dyn->element = NULL;
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
        GstCaps *intersect;

        /* try to intersect the caps with the caps of the template */
        intersect = gst_caps_intersect (caps,
            gst_static_caps_get (&templ->static_caps));
        /* check if the intersection is empty */
        if (!gst_caps_is_empty (intersect)) {
          /* non empty intersection, we can use this element */
          to_try = g_list_prepend (to_try, factory);
          gst_caps_unref (intersect);
          break;
        }
        gst_caps_unref (intersect);
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
      g_str_has_prefix (mimetype, "text/plain");
}

static void
pad_unblocked (GstPad * pad, gboolean blocked, GstDecodeBin * decode_bin)
{
}

static void
pad_blocked (GstPad * pad, gboolean blocked, GstDecodeBin * decode_bin)
{
  decode_bin->numwaiting--;
  if (decode_bin->numwaiting == 0) {
    gst_object_ref (decode_bin->fakesink);
    gst_bin_remove (GST_BIN (decode_bin), decode_bin->fakesink);

    gst_element_set_state (decode_bin->fakesink, GST_STATE_NULL);
    gst_element_get_state (decode_bin->fakesink, NULL, NULL,
        GST_CLOCK_TIME_NONE);

    gst_object_unref (decode_bin->fakesink);
    decode_bin->fakesink = NULL;

    gst_element_post_message (GST_ELEMENT_CAST (decode_bin),
        gst_message_new_state_dirty (GST_OBJECT_CAST (decode_bin)));
  }
  gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback) pad_unblocked,
      NULL);
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
   * create a ghostpad for this pad. */
  if (mimetype_is_raw (mimetype)) {
    gchar *padname;
    GstPad *ghost;

    /* make a unique name for this new pad */
    padname = g_strdup_printf ("src%d", decode_bin->numpads);
    decode_bin->numpads++;
    decode_bin->numwaiting++;

    /* make it a ghostpad */
    ghost = gst_ghost_pad_new (padname, pad);
    gst_element_add_pad (GST_ELEMENT (decode_bin), ghost);

    gst_pad_set_blocked_async (pad, TRUE, (GstPadBlockCallback) pad_blocked,
        decode_bin);

    GST_LOG_OBJECT (element, "closed pad %s", padname);

    /* our own signal with an extra flag that this is the only pad */
    GST_DEBUG_OBJECT (decode_bin, "emitting new-decoded-pad");
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_NEW_DECODED_PAD], 0, ghost, !more);
    GST_DEBUG_OBJECT (decode_bin, "emitted new-decoded-pad");

    g_free (padname);
  } else {
    GList *to_try;

    /* if the caps has many types, we need to delay */
    if (gst_caps_get_size (caps) != 1)
      goto many_types;

    /* continue plugging, first find all compatible elements */
    to_try = find_compatibles (decode_bin, caps);
    if (to_try == NULL)
      /* no compatible elements, we cannot go on */
      goto unknown_type;

    try_to_link_1 (decode_bin, pad, to_try);
    /* can free the list again now */
    g_list_free (to_try);
  }
  return;

unknown_type:
  {
    GST_LOG_OBJECT (pad, "unkown type found, fire signal");
    g_signal_emit (G_OBJECT (decode_bin),
        gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, pad, caps);
    return;
  }
dont_know_yet:
  {
    GST_LOG_OBJECT (pad, "type is not known yet, waiting to close link");
    return;
  }
many_types:
  {
    GST_LOG_OBJECT (pad, "many possible types, waiting to close link");
    return;
  }
}

/*
 * given a list of element factories, try to link one of the factories
 * to the given pad.
 *
 * The function returns the element that was successfully linked to the
 * pad.
 */
static GstElement *
try_to_link_1 (GstDecodeBin * decode_bin, GstPad * pad, GList * factories)
{
  GList *walk;
  GstElement *result = NULL;

  /* loop over the factories */
  for (walk = factories; walk; walk = g_list_next (walk)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (walk->data);
    GstElement *element;
    GstPadLinkReturn ret;
    GstPad *sinkpad;

    GST_DEBUG_OBJECT (decode_bin, "trying to link %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

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
    if ((sinkpad = gst_element_get_pad (element, "sink")) == NULL) {
      /* if no pad is found we can't do anything */
      GST_WARNING_OBJECT (decode_bin, "could not find sinkpad in element");
      continue;
    }

    /* now add the element to the bin first */
    GST_DEBUG_OBJECT (decode_bin, "adding %s", GST_OBJECT_NAME (element));
    gst_bin_add (GST_BIN (decode_bin), element);

    /* set to ready first so it is ready */
    gst_element_set_state (element, GST_STATE_READY);

    /* keep our own list of elements */
    decode_bin->elements = g_list_prepend (decode_bin->elements, element);

    if ((ret = gst_pad_link (pad, sinkpad)) != GST_PAD_LINK_OK) {
      GST_DEBUG_OBJECT (decode_bin, "link failed on pad %s:%s, reason %d",
          GST_DEBUG_PAD_NAME (pad), ret);
      /* get rid of the sinkpad */
      gst_object_unref (sinkpad);
      /* this element did not work, remove it again and continue trying
       * other elements, the element will be disposed. */
      gst_element_set_state (element, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (decode_bin), element);
    } else {
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
      gst_element_set_state (element, GST_STATE_PAUSED);

      result = element;

      /* get rid of the sinkpad now */
      gst_object_unref (sinkpad);

      /* and exit */
      goto done;
    }
  }
done:
  return result;
}

static GstPad *
get_our_ghost_pad (GstDecodeBin * decode_bin, GstPad * pad)
{
#if 0
  GList *ghostpads;

  if (pad == NULL || !GST_PAD_IS_SRC (pad)) {
    GST_DEBUG_OBJECT (decode_bin, "pad NULL or not SRC pad");
    return NULL;
  }

  if (GST_IS_GHOST_PAD (pad)) {
    GstElement *parent = gst_pad_get_parent (pad);

    GST_DEBUG_OBJECT (decode_bin, "pad parent %s", GST_ELEMENT_NAME (parent));

    if (parent == GST_ELEMENT (decode_bin)) {
      GST_DEBUG_OBJECT (decode_bin, "pad is our ghostpad");
      gst_object_unref (parent);
      return pad;
    } else {
      GST_DEBUG_OBJECT (decode_bin, "pad is ghostpad but not ours");
      gst_object_unref (parent);
      return NULL;
    }
  }

  GST_DEBUG_OBJECT (decode_bin, "looping over ghostpads");
  ghostpads = GST_REAL_PAD (pad)->ghostpads;
  while (ghostpads) {
    GstPad *ghostpad;

    ghostpad = get_our_ghost_pad (decode_bin, GST_PAD (ghostpads->data));
    if (ghostpad)
      return ghostpad;

    ghostpads = g_list_next (ghostpads);
  }
  GST_DEBUG_OBJECT (decode_bin, "done looping over ghostpads, nothing found");
#endif

  return NULL;
}

/* remove all downstream elements starting from the given pad.
 * Also make sure to remove the ghostpad we created for the raw 
 * decoded stream.
 */
static void
remove_element_chain (GstDecodeBin * decode_bin, GstPad * pad)
{
  GList *int_links, *walk;
  GstElement *elem = GST_ELEMENT (GST_OBJECT_PARENT (pad));

  while (GST_OBJECT_PARENT (elem) &&
      GST_OBJECT_PARENT (elem) != GST_OBJECT (decode_bin))
    elem = GST_ELEMENT (GST_OBJECT_PARENT (elem));

  GST_DEBUG_OBJECT (decode_bin, "%s:%s", GST_DEBUG_PAD_NAME (pad));
  int_links = gst_pad_get_internal_links (pad);

  /* remove all elements linked to this pad up to the ghostpad 
   * that we created for this stream */
  for (walk = int_links; walk; walk = g_list_next (walk)) {
    GstPad *pad;
    GstPad *ghostpad;
    GstPad *peer;

    pad = GST_PAD (walk->data);
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

    {
      GstElement *parent = gst_pad_get_parent_element (peer);

      if (parent) {
        if (parent != GST_ELEMENT (decode_bin)) {
          GST_DEBUG_OBJECT (decode_bin, "dead end pad %s:%s",
              GST_DEBUG_PAD_NAME (peer));
        } else {
          GST_DEBUG_OBJECT (decode_bin, "recursing element %s on pad %s:%s",
              GST_ELEMENT_NAME (elem), GST_DEBUG_PAD_NAME (pad));
          remove_element_chain (decode_bin, peer);
        }
        gst_object_unref (parent);
      }
    }
    gst_object_unref (peer);
  }
  GST_DEBUG_OBJECT (decode_bin, "removing %s", GST_ELEMENT_NAME (elem));

  g_list_free (int_links);

  gst_element_set_state (elem, GST_STATE_NULL);

  gst_bin_remove (GST_BIN (decode_bin), elem);
}

/* This function will be called when a dynamic pad is created on an element.
 * We try to continue autoplugging on this new pad. */
static void
new_pad (GstElement * element, GstPad * pad, GstDynamic * dynamic)
{
  GstDecodeBin *decode_bin = dynamic->decode_bin;
  GstCaps *caps;

  GST_LOCK (decode_bin);
  if (decode_bin->shutting_down)
    goto shutting_down1;
  GST_UNLOCK (decode_bin);

  GST_STATE_LOCK (decode_bin);
  if (decode_bin->shutting_down)
    goto shutting_down2;

  /* see if any more pending dynamic connections exist */
  gboolean more = gst_decode_bin_is_dynamic (decode_bin);

  caps = gst_pad_get_caps (pad);
  close_pad_link (element, pad, caps, decode_bin, more);
  if (caps)
    gst_caps_unref (caps);
  GST_STATE_UNLOCK (decode_bin);

  return;

shutting_down1:
  GST_UNLOCK (decode_bin);
  return;

shutting_down2:
  GST_STATE_UNLOCK (decode_bin);
  return;
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
      GST_ELEMENT_NAME (element));

  /* remove the element from the list of dynamic elements */
  decode_bin->dynamics = g_list_remove (decode_bin->dynamics, dynamic);
  dynamic_free (dynamic);

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

/* This function will be called when a pad is disconnected for some reason */
static void
unlinked (GstPad * pad, GstPad * peerpad, GstDecodeBin * decode_bin)
{
  GstDynamic *dyn;
  GstElement *element, *peer;

  /* inactivate pad */
  gst_pad_set_active (pad, GST_ACTIVATE_NONE);

  element = gst_pad_get_parent_element (pad);
  peer = gst_pad_get_parent_element (peerpad);

  if (!is_our_kid (peer, decode_bin))
    goto exit;

  /* remove all elements linked to the peerpad */
  remove_element_chain (decode_bin, peerpad);

  /* if an element removes two pads, then we don't want this twice */
  if (g_list_find (decode_bin->dynamics, element) != NULL)
    goto exit;

  GST_DEBUG_OBJECT (decode_bin, "pad removal while alive - chained?");

  dyn = dynamic_create (element, decode_bin);
  /* and add this element to the dynamic elements */
  decode_bin->dynamics = g_list_prepend (decode_bin->dynamics, dyn);

exit:
  gst_object_unref (element);
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

    dyn = dynamic_create (element, decode_bin);
    /* and add this element to the dynamic elements */
    decode_bin->dynamics = g_list_prepend (decode_bin->dynamics, dyn);
  }

  /* Check if this is an element with more than 1 pad. If this element
   * has more than 1 pad, we need to be carefull not to signal the 
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

  GST_STATE_LOCK (decode_bin);
  if (decode_bin->shutting_down)
    goto shutting_down;

  GST_DEBUG_OBJECT (decode_bin, "typefind found caps %" GST_PTR_FORMAT, caps);

  /* autoplug the new pad with the caps that the signal gave us. */
  pad = gst_element_get_pad (typefind, "src");
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

shutting_down:
  GST_STATE_UNLOCK (decode_bin);
  return;
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
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LOCK (decode_bin);
      decode_bin->shutting_down = FALSE;
      GST_UNLOCK (decode_bin);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOCK (decode_bin);
      decode_bin->shutting_down = TRUE;
      GST_UNLOCK (decode_bin);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      free_dynamics (decode_bin);
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
    "decoder bin", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
