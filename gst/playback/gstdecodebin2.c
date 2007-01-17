/* GStreamer
 * Copyright (C) <2006> Edward Hervey <edward@fluendo.com>
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
 * SECTION:element-decodebin2
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/utils/base-utils.h>

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

typedef struct _GstDecodeGroup GstDecodeGroup;
typedef struct _GstDecodePad GstDecodePad;
typedef struct _GstDecodeBin GstDecodeBin;
typedef struct _GstDecodeBinClass GstDecodeBinClass;

#define GST_TYPE_DECODE_BIN             (gst_decode_bin_get_type())
#define GST_DECODE_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECODE_BIN,GstDecodeBin))
#define GST_DECODE_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECODE_BIN,GstDecodeBinClass))
#define GST_IS_DECODE_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECODE_BIN))
#define GST_IS_DECODE_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECODE_BIN))

struct _GstDecodeBin
{
  GstBin bin;                   /* we extend GstBin */

  GstElement *typefind;         /* this holds the typefind object */
  GstElement *fakesink;

  GMutex *lock;                 /* Protects activegroup and groups */
  GstDecodeGroup *activegroup;  /* group currently active */
  GList *groups;                /* List of non-active GstDecodeGroups, sorted in
                                 * order of creation. */
  gint nbpads;                  /* unique identifier for source pads */
  GstCaps *caps;                /* caps on which to stop decoding */

  GList *factories;             /* factories we can use for selecting elements */
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
  /* signal fired to know if we continue trying to decode the given caps */
    gboolean (*autoplug_continue) (GstElement * element, GstCaps * caps);
  /* signal fired to reorder the proposed list of factories */
    gboolean (*autoplug_sort) (GstElement * element, GstCaps * caps,
      GList ** list);
};

/* signals */
enum
{
  SIGNAL_NEW_DECODED_PAD,
  SIGNAL_REMOVED_DECODED_PAD,
  SIGNAL_UNKNOWN_TYPE,
  SIGNAL_AUTOPLUG_CONTINUE,
  SIGNAL_AUTOPLUG_SORT,
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_CAPS,
};

static GstBinClass *parent_class;
static guint gst_decode_bin_signals[LAST_SIGNAL] = { 0 };

static const GstElementDetails gst_decode_bin_details =
GST_ELEMENT_DETAILS ("Decoder Bin",
    "Generic/Bin/Decoder",
    "Autoplug and decode to raw media",
    "Edward Hervey <edward@fluendo.com>");


static void add_fakesink (GstDecodeBin * decode_bin);
static void remove_fakesink (GstDecodeBin * decode_bin);

static void type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin);

static gboolean gst_decode_bin_autoplug_continue (GstElement * element,
    GstCaps * caps);
static gboolean gst_decode_bin_autoplug_sort (GstElement * element,
    GstCaps * caps, GList ** list);
static void gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_decode_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_decode_bin_set_caps (GstDecodeBin * dbin, GstCaps * caps);
static GstCaps *gst_decode_bin_get_caps (GstDecodeBin * dbin);

static GstPad *find_sink_pad (GstElement * element);
static GstStateChangeReturn gst_decode_bin_change_state (GstElement * element,
    GstStateChange transition);

#define DECODE_BIN_LOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "locking from thread %p",				\
		    g_thread_self ());					\
    g_mutex_lock (dbin->lock);						\
    GST_LOG_OBJECT (dbin,						\
		    "locked from thread %p",				\
		    g_thread_self ());					\
} G_STMT_END

#define DECODE_BIN_UNLOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "unlocking from thread %p",				\
		    g_thread_self ());					\
    g_mutex_unlock (dbin->lock);					\
} G_STMT_END

/* GstDecodeGroup
 *
 * Streams belonging to the same group/chain of a media file
 *
 */

struct _GstDecodeGroup
{
  GstDecodeBin *dbin;
  GMutex *lock;
  GstElement *multiqueue;
  gboolean exposed;             /* TRUE if this group is exposed */
  gboolean drained;             /* TRUE if EOS went throug all endpads */
  gboolean blocked;             /* TRUE if all endpads are blocked */
  gboolean complete;            /* TRUE if we are not expecting anymore streams 
                                 * on this group */
  GList *endpads;               /* List of GstDecodePad of source pads to be exposed */
  GList *ghosts;                /* List of GstGhostPad for the endpads */
};

#define GROUP_MUTEX_LOCK(group) G_STMT_START {				\
    GST_LOG_OBJECT (group->dbin,					\
		    "locking group %p from thread %p",			\
		    group, g_thread_self ());				\
    g_mutex_lock (group->lock);						\
    GST_LOG_OBJECT (group->dbin,					\
		    "locked group %p from thread %p",			\
		    group, g_thread_self ());				\
} G_STMT_END

#define GROUP_MUTEX_UNLOCK(group) G_STMT_START {                        \
    GST_LOG_OBJECT (group->dbin,					\
		    "unlocking group %p from thread %p",		\
		    group, g_thread_self ());				\
    g_mutex_unlock (group->lock);					\
} G_STMT_END


static GstDecodeGroup *gst_decode_group_new (GstDecodeBin * decode_bin);
static GstPad *gst_decode_group_control_demuxer_pad (GstDecodeGroup * group,
    GstPad * pad);
static gboolean gst_decode_group_control_source_pad (GstDecodeGroup * group,
    GstPad * pad);
static gboolean gst_decode_group_expose (GstDecodeGroup * group);
static void gst_decode_group_check_if_blocked (GstDecodeGroup * group);
static void gst_decode_group_set_complete (GstDecodeGroup * group);
static void gst_decode_group_hide (GstDecodeGroup * group);
static void gst_decode_group_free (GstDecodeGroup * group);

/* GstDecodePad
 *
 * GstPad private used for source pads of groups
 */

struct _GstDecodePad
{
  GstPad *pad;
  GstDecodeGroup *group;
  gboolean blocked;
  gboolean drained;
};

static GstDecodePad *gst_decode_pad_new (GstDecodeGroup * group, GstPad * pad,
    gboolean block);
static void source_pad_blocked_cb (GstPad * pad, gboolean blocked,
    GstDecodePad * dpad);

/* TempPadStruct
 * Internal structure used for pads which have more than one structure.
 */
typedef struct _TempPadStruct
{
  GstDecodeBin *dbin;
  GstDecodeGroup *group;
} TempPadStruct;

/********************************
 * Standard GObject boilerplate *
 ********************************/

static void gst_decode_bin_class_init (GstDecodeBinClass * klass);
static void gst_decode_bin_init (GstDecodeBin * decode_bin);
static void gst_decode_bin_dispose (GObject * object);
static void gst_decode_bin_finalize (GObject * object);

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
        g_type_register_static (GST_TYPE_BIN, "GstDecodeBin2",
        &gst_decode_bin_info, 0);
  }

  return gst_decode_bin_type;
}

static gboolean
_gst_boolean_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gboolean myboolean;

  myboolean = g_value_get_boolean (handler_return);
  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_boolean (return_accu, myboolean);

  /* stop emission if FALSE */
  return myboolean;
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

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_decode_bin_dispose);
  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_decode_bin_finalize);
  gobject_klass->set_property = GST_DEBUG_FUNCPTR (gst_decode_bin_set_property);
  gobject_klass->get_property = GST_DEBUG_FUNCPTR (gst_decode_bin_get_property);

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
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_CONTINUE] =
      g_signal_new ("autoplug-continue", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_continue),
      _gst_boolean_accumulator, NULL, gst_play_marshal_BOOLEAN__OBJECT,
      G_TYPE_BOOLEAN, 1, GST_TYPE_CAPS);
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_SORT] =
      g_signal_new ("autoplug-sort", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_sort),
      _gst_boolean_accumulator, NULL, gst_play_marshal_BOOLEAN__OBJECT_POINTER,
      G_TYPE_BOOLEAN, 2, GST_TYPE_CAPS, G_TYPE_POINTER);

  g_object_class_install_property (gobject_klass, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps", "The caps on which to stop decoding.",
          GST_TYPE_CAPS, G_PARAM_READWRITE));

  klass->autoplug_continue =
      GST_DEBUG_FUNCPTR (gst_decode_bin_autoplug_continue);
  klass->autoplug_sort = GST_DEBUG_FUNCPTR (gst_decode_bin_autoplug_sort);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&decoder_bin_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&decoder_bin_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_decode_bin_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_decode_bin_change_state);
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
    GstPad *gpad;

    /* add the typefind element */
    if (!gst_bin_add (GST_BIN (decode_bin), decode_bin->typefind)) {
      g_warning ("Could not add typefind element, decodebin will not work");
      gst_object_unref (decode_bin->typefind);
      decode_bin->typefind = NULL;
    }

    /* get the sinkpad */
    pad = gst_element_get_pad (decode_bin->typefind, "sink");

    /* ghost the sink pad to ourself */
    gpad = gst_ghost_pad_new ("sink", pad);
    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (decode_bin), gpad);

    gst_object_unref (pad);

    /* connect a signal to find out when the typefind element found
     * a type */
    g_signal_connect (G_OBJECT (decode_bin->typefind), "have_type",
        G_CALLBACK (type_found), decode_bin);
  }

  decode_bin->lock = g_mutex_new ();
  decode_bin->activegroup = NULL;
  decode_bin->groups = NULL;

  decode_bin->caps =
      gst_caps_from_string ("video/x-raw-yuv;video/x-raw-rgb;video/x-raw-gray;"
      "audio/x-raw-int;audio/x-raw-float;" "text/plain;text/x-pango-markup");

  add_fakesink (decode_bin);

  /* FILLME */
}

static void
gst_decode_bin_dispose (GObject * object)
{
  GstDecodeBin *decode_bin;

  decode_bin = GST_DECODE_BIN (object);

  if (decode_bin->factories)
    gst_plugin_feature_list_free (decode_bin->factories);
  decode_bin->factories = NULL;

  /* FILLME */
  if (decode_bin->caps)
    gst_caps_unref (decode_bin->caps);
  decode_bin->caps = NULL;
  remove_fakesink (decode_bin);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decode_bin_finalize (GObject * object)
{

  /* FILLME */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecodeBin *dbin;

  dbin = GST_DECODE_BIN (object);

  switch (prop_id) {
    case PROP_CAPS:
      gst_decode_bin_set_caps (dbin, (GstCaps *) g_value_dup_boxed (value));
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
    case PROP_CAPS:{
      g_value_take_boxed (value, gst_decode_bin_get_caps (dbin));
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

/* _set_caps
 * Changes the caps on which decodebin will stop decoding.
 * Will unref the previously set one. The refcount of the given caps will be
 * taken.
 * @caps can be NULL.
 *
 * MT-safe
 */

static void
gst_decode_bin_set_caps (GstDecodeBin * dbin, GstCaps * caps)
{
  GST_DEBUG_OBJECT (dbin, "Setting new caps: %" GST_PTR_FORMAT, caps);

  DECODE_BIN_LOCK (dbin);
  if (dbin->caps)
    gst_caps_unref (dbin->caps);
  dbin->caps = caps;
  DECODE_BIN_UNLOCK (dbin);
}

/* _get_caps
 * Returns the currently configured caps on which decodebin will stop decoding.
 * The returned caps (if not NULL), will have its refcount incremented.
 *
 * MT-safe
 */

static GstCaps *
gst_decode_bin_get_caps (GstDecodeBin * dbin)
{
  GstCaps *caps;

  GST_DEBUG_OBJECT (dbin, "Getting currently set caps");

  DECODE_BIN_LOCK (dbin);
  caps = dbin->caps;
  if (caps)
    gst_caps_ref (caps);
  DECODE_BIN_UNLOCK (dbin);

  return caps;
}

/*****
 * Default autoplug signal handlers
 *****/

static gboolean
gst_decode_bin_autoplug_continue (GstElement * element, GstCaps * caps)
{
  return TRUE;
}

static gboolean
gst_decode_bin_autoplug_sort (GstElement * element, GstCaps * caps,
    GList ** list)
{
  return TRUE;
}



/********
 * Discovery methods
 *****/

static gboolean are_raw_caps (GstDecodeBin * dbin, GstCaps * caps);
static gboolean is_demuxer_element (GstElement * srcelement);
static GList *find_compatibles (GstDecodeBin * decode_bin,
    const GstCaps * caps);

static gboolean connect_pad (GstDecodeBin * dbin, GstElement * src,
    GstPad * pad, GList * factories, GstDecodeGroup * group);
static gboolean connect_element (GstDecodeBin * dbin, GstElement * element,
    GstDecodeGroup * group);
static void expose_pad (GstDecodeBin * dbin, GstElement * src, GstPad * pad,
    GstDecodeGroup * group);

static void pad_added_group_cb (GstElement * element, GstPad * pad,
    GstDecodeGroup * group);
static void pad_removed_group_cb (GstElement * element, GstPad * pad,
    GstDecodeGroup * group);
static void no_more_pads_group_cb (GstElement * element,
    GstDecodeGroup * group);
static void pad_added_cb (GstElement * element, GstPad * pad,
    GstDecodeBin * dbin);
static void pad_removed_cb (GstElement * element, GstPad * pad,
    GstDecodeBin * dbin);
static void no_more_pads_cb (GstElement * element, GstDecodeBin * dbin);

static GstDecodeGroup *get_current_group (GstDecodeBin * dbin);

static void
analyze_new_pad (GstDecodeBin * dbin, GstElement * src, GstPad * pad,
    GstCaps * caps, GstDecodeGroup * group)
{
  gboolean apcontinue = TRUE;
  GList *factories = NULL;
  gboolean apsort = TRUE;

  GST_DEBUG_OBJECT (dbin, "Pad %s:%s caps:%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  if ((caps == NULL) || gst_caps_is_empty (caps))
    goto unknown_type;

  if (gst_caps_is_any (caps))
    goto any_caps;

  /* 1. Emit 'autoplug-continue' */
  g_signal_emit (G_OBJECT (dbin),
      gst_decode_bin_signals[SIGNAL_AUTOPLUG_CONTINUE], 0, caps, &apcontinue);

  /* 1.a if autoplug-continue is FALSE or caps is a raw format, goto pad_is_final */
  if ((!apcontinue) || are_raw_caps (dbin, caps))
    goto expose_pad;

  /* 1.b else if there's no compatible factory or 'autoplug-sort' returned FALSE, goto pad_not_used */
  if ((factories = find_compatibles (dbin, caps))) {
    /* emit autoplug-sort */
    g_signal_emit (G_OBJECT (dbin),
        gst_decode_bin_signals[SIGNAL_AUTOPLUG_SORT],
        0, caps, &factories, &apsort);
    if (!apsort) {
      g_list_free (factories);
      /* User doesn't want that pad */
      goto pad_not_wanted;
    }
  } else {
    /* no compatible factories */
    goto unknown_type;
  }

  /* 1.c else goto pad_is_valid */
  GST_LOG_OBJECT (pad, "Let's continue discovery on this pad");

  connect_pad (dbin, src, pad, factories, group);
  g_list_free (factories);

  return;

expose_pad:
  {
    GST_LOG_OBJECT (dbin, "Pad is final. autoplug-continue:%d", apcontinue);
    expose_pad (dbin, src, pad, group);
    return;
  }

pad_not_wanted:
  {
    GST_LOG_OBJECT (pad, "User doesn't want this pad, stopping discovery");
    return;
  }

unknown_type:
  {
    GST_LOG_OBJECT (pad, "Unknown type, firing signal");
    g_signal_emit (G_OBJECT (dbin),
        gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, pad, caps);

    /* Check if there are no pending groups, if so, remove fakesink */
    if (dbin->groups == NULL)
      remove_fakesink (dbin);

    gst_element_post_message (GST_ELEMENT_CAST (dbin),
        gst_missing_decoder_message_new (GST_ELEMENT_CAST (dbin), caps));
    return;
  }

any_caps:
  {
    GST_WARNING_OBJECT (pad,
        "pad has ANY caps, not able to autoplug to anything");
    /* FIXME : connect to caps notification */
    return;
  }
}


/** connect_pad:
 *
 * Try to connect the given pad to an element created from one of the factories,
 * and recursively.
 *
 * Returns TRUE if an element was properly created and linked
 */

static gboolean
connect_pad (GstDecodeBin * dbin, GstElement * src, GstPad * pad,
    GList * factories, GstDecodeGroup * group)
{
  gboolean res = FALSE;
  GList *tmp;

  g_return_val_if_fail (factories != NULL, FALSE);
  GST_DEBUG_OBJECT (dbin, "pad %s:%s , group:%p",
      GST_DEBUG_PAD_NAME (pad), group);

  /* 1. is element demuxer or parser */
  if (is_demuxer_element (src)) {
    GstPad *mqpad;

    GST_LOG_OBJECT (src, "is a demuxer, connecting the pad through multiqueue");

    if (!group)
      if (!(group = get_current_group (dbin))) {
        group = gst_decode_group_new (dbin);
        DECODE_BIN_LOCK (dbin);
        dbin->groups = g_list_append (dbin->groups, group);
        DECODE_BIN_UNLOCK (dbin);
      }

    if (!(mqpad = gst_decode_group_control_demuxer_pad (group, pad)))
      goto beach;
    pad = mqpad;
  }

  /* 2. Try to create an element and link to it */
  for (tmp = factories; tmp; tmp = g_list_next (tmp)) {
    GstElementFactory *factory = (GstElementFactory *) tmp->data;
    GstElement *element;
    GstPad *sinkpad;

    /* 2.1. Try to create an element */
    if ((element = gst_element_factory_create (factory, NULL)) == NULL) {
      GST_WARNING_OBJECT (dbin, "Could not create an element from %s",
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
      continue;
    }

    /* 2.3. Find its sink pad */
    if (!(sinkpad = find_sink_pad (element))) {
      GST_WARNING_OBJECT (dbin, "Element %s doesn't have a sink pad",
          GST_ELEMENT_NAME (element));
      gst_object_unref (element);
      continue;
    }

    /* 2.4 add it ... */
    if (!(gst_bin_add (GST_BIN (dbin), element))) {
      GST_WARNING_OBJECT (dbin, "Couldn't add %s to the bin",
          GST_ELEMENT_NAME (element));
      gst_object_unref (sinkpad);
      gst_object_unref (element);
      continue;
    }

    /* ... activate it ... */
    if ((gst_element_set_state (element,
                GST_STATE_READY)) == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (dbin, "Couldn't set %s to READY",
          GST_ELEMENT_NAME (element));
      gst_object_unref (sinkpad);
      gst_bin_remove (GST_BIN (dbin), element);
      continue;
    }

    /* 2.5 ...and try to link */
    if ((gst_pad_link (pad, sinkpad)) != GST_PAD_LINK_OK) {
      GST_WARNING_OBJECT (dbin, "Link failed on pad %s:%s",
          GST_DEBUG_PAD_NAME (sinkpad));
      gst_element_set_state (element, GST_STATE_NULL);
      gst_object_unref (sinkpad);
      gst_bin_remove (GST_BIN (dbin), element);
      continue;
    }

    GST_LOG_OBJECT (dbin, "linked on pad %s:%s", GST_DEBUG_PAD_NAME (pad));

    /* link this element further */
    connect_element (dbin, element, group);

    /* Bring the element to the state of the parent */
    if ((gst_element_set_state (element,
                GST_STATE_PAUSED)) == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (dbin, "Couldn't set %s to PAUSED",
          GST_ELEMENT_NAME (element));
      gst_element_set_state (element, GST_STATE_NULL);
      gst_object_unref (sinkpad);
      gst_bin_remove (GST_BIN (dbin), element);
      continue;
    }

    res = TRUE;
    break;
  }

beach:
  return res;
}

static gboolean
connect_element (GstDecodeBin * dbin, GstElement * element,
    GstDecodeGroup * group)
{
  GList *pads;
  gboolean res = TRUE;
  gboolean dynamic = FALSE;
  GList *to_connect = NULL;

  GST_DEBUG_OBJECT (dbin, "Attempting to connect element %s [group:%p] further",
      GST_ELEMENT_NAME (element), group);

  /* 1. Loop over pad templates, grabbing existing pads along the way */
  for (pads = GST_ELEMENT_GET_CLASS (element)->padtemplates; pads;
      pads = g_list_next (pads)) {
    GstPadTemplate *templ = GST_PAD_TEMPLATE (pads->data);
    const gchar *templ_name;

    /* we are only interested in source pads */
    if (GST_PAD_TEMPLATE_DIRECTION (templ) != GST_PAD_SRC)
      continue;

    templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);
    GST_DEBUG_OBJECT (dbin, "got a source pad template %s", templ_name);

    /* figure out what kind of pad this is */
    switch (GST_PAD_TEMPLATE_PRESENCE (templ)) {
      case GST_PAD_ALWAYS:
      {
        /* get the pad that we need to autoplug */
        GstPad *pad = gst_element_get_pad (element, templ_name);

        if (pad) {
          GST_DEBUG_OBJECT (dbin, "got the pad for always template %s",
              templ_name);
          /* here is the pad, we need to autoplug it */
          to_connect = g_list_prepend (to_connect, pad);
        } else {
          /* strange, pad is marked as always but it's not
           * there. Fix the element */
          GST_WARNING_OBJECT (dbin,
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
          GST_DEBUG_OBJECT (dbin, "got the pad for sometimes template %s",
              templ_name);
          /* the pad is created, we need to autoplug it */
          to_connect = g_list_prepend (to_connect, pad);
        } else {
          GST_DEBUG_OBJECT (dbin,
              "did not get the sometimes pad of template %s", templ_name);
          /* we have an element that will create dynamic pads */
          dynamic = TRUE;
        }
        break;
      }
      case GST_PAD_REQUEST:
        /* ignore request pads */
        GST_DEBUG_OBJECT (dbin, "ignoring request padtemplate %s", templ_name);
        break;
    }
  }

  /* 2. if there are more potential pads, connect to relevent signals */
  if (dynamic) {
    if (group) {
      g_signal_connect (G_OBJECT (element), "pad-added",
          G_CALLBACK (pad_added_group_cb), group);
      g_signal_connect (G_OBJECT (element), "pad-removed",
          G_CALLBACK (pad_removed_group_cb), group);
      g_signal_connect (G_OBJECT (element), "no-more-pads",
          G_CALLBACK (no_more_pads_group_cb), group);
    } else {
      /* This is a non-grouped element, the handlers are different */
      g_signal_connect (G_OBJECT (element), "pad-added",
          G_CALLBACK (pad_added_cb), dbin);
      g_signal_connect (G_OBJECT (element), "pad-removed",
          G_CALLBACK (pad_removed_cb), dbin);
      g_signal_connect (G_OBJECT (element), "no-more-pads",
          G_CALLBACK (no_more_pads_cb), dbin);
    }
  }

  /* 3. for every available pad, connect it */
  for (pads = to_connect; pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD_CAST (pads->data);
    GstCaps *caps;

    caps = gst_pad_get_caps (pad);
    analyze_new_pad (dbin, element, pad, caps, group);
    if (caps)
      gst_caps_unref (caps);

    gst_object_unref (pad);
  }
  g_list_free (to_connect);

  return res;
}

/* expose_pad:
 *
 * Expose the given pad on the group as a decoded pad.
 * If group is NULL, a GstDecodeGroup will be created and setup properly.
 */
static void
expose_pad (GstDecodeBin * dbin, GstElement * src, GstPad * pad,
    GstDecodeGroup * group)
{
  gboolean newgroup = FALSE;
  gboolean isdemux;

  GST_DEBUG_OBJECT (dbin, "pad %s:%s, group:%p",
      GST_DEBUG_PAD_NAME (pad), group);

  if (!group)
    if (!(group = get_current_group (dbin))) {
      group = gst_decode_group_new (dbin);
      DECODE_BIN_LOCK (dbin);
      dbin->groups = g_list_append (dbin->groups, group);
      DECODE_BIN_UNLOCK (dbin);
      newgroup = TRUE;
    }

  isdemux = is_demuxer_element (src);

  if (isdemux || newgroup) {
    GstPad *mqpad;

    GST_LOG_OBJECT (src, "is a demuxer, connecting the pad through multiqueue");

    if (!(mqpad = gst_decode_group_control_demuxer_pad (group, pad)))
      goto beach;
    pad = mqpad;
  }

  gst_decode_group_control_source_pad (group, pad);

  if (newgroup && !isdemux) {
    /* If we have discovered a raw pad and it doesn't belong to any group,
     * that means there wasn't any demuxer. In that case, we consider the
     * group as being complete. */
    gst_decode_group_set_complete (group);
  }
beach:
  return;
}

static void
type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin)
{
  GstPad *pad;

  GST_STATE_LOCK (decode_bin);

  GST_DEBUG_OBJECT (decode_bin, "typefind found caps %" GST_PTR_FORMAT, caps);

  pad = gst_element_get_pad (typefind, "src");

  analyze_new_pad (decode_bin, typefind, pad, caps, NULL);

  gst_object_unref (pad);

  GST_STATE_UNLOCK (decode_bin);
  return;
}

static void
pad_added_group_cb (GstElement * element, GstPad * pad, GstDecodeGroup * group)
{
  GST_LOG_OBJECT (pad, "pad added, group:%p", group);

  /* FIXME : FILLME */
}

static void
pad_removed_group_cb (GstElement * element, GstPad * pad,
    GstDecodeGroup * group)
{
  GST_LOG_OBJECT (pad, "pad removed, group:%p", group);

  /* In fact, we don't have to do anything here, the active group will be
   * removed when the group's multiqueue is drained */
}

static void
no_more_pads_group_cb (GstElement * element, GstDecodeGroup * group)
{
  GST_LOG_OBJECT (element, "no more pads, setting group %p to complete", group);

  /* FIXME : FILLME */
  gst_decode_group_set_complete (group);
}

static void
pad_added_cb (GstElement * element, GstPad * pad, GstDecodeBin * dbin)
{
  GstCaps *caps;

  GST_LOG_OBJECT (pad, "Pad added to non-grouped element");

  caps = gst_pad_get_caps (pad);
  analyze_new_pad (dbin, element, pad, caps, NULL);
  if (caps)
    gst_caps_unref (caps);
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, GstDecodeBin * dbin)
{
  GST_LOG_OBJECT (pad, "Pad removed from non-grouped element");
}

static void
no_more_pads_cb (GstElement * element, GstDecodeBin * dbin)
{
  GstDecodeGroup *group;

  GST_LOG_OBJECT (element, "No more pads, setting current group to complete");

  /* Find the non-complete group, there should only be one */
  if (!(group = get_current_group (dbin)))
    goto no_group;

  gst_decode_group_set_complete (group);
  return;

no_group:
  {
    GST_WARNING_OBJECT (dbin, "We couldn't find a non-completed group !!");
    return;
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
    templates = gst_element_factory_get_static_pad_templates (factory);
    for (walk = (GList *) templates; walk; walk = g_list_next (walk)) {
      GstStaticPadTemplate *templ = walk->data;

      /* we only care about the sink templates */
      if (templ->direction == GST_PAD_SINK) {
        GstCaps *intersect;
        GstCaps *tmpl_caps;

        /* try to intersect the caps with the caps of the template */
        tmpl_caps = gst_static_caps_get (&templ->static_caps);

        intersect = gst_caps_intersect (caps, tmpl_caps);
        gst_caps_unref (tmpl_caps);

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

/* Decide whether an element is a demuxer based on the 
 * klass and number/type of src pad templates it has */
static gboolean
is_demuxer_element (GstElement * srcelement)
{
  GstElementFactory *srcfactory;
  GstElementClass *elemclass;
  GList *templates, *walk;
  const gchar *klass;
  gint potential_src_pads = 0;

  srcfactory = gst_element_get_factory (srcelement);
  klass = gst_element_factory_get_klass (srcfactory);

  /* Can't be a demuxer unless it has Demux in the klass name */
  if (!strstr (klass, "Demux"))
    return FALSE;

  /* Walk the src pad templates and count how many the element
   * might produce */
  elemclass = GST_ELEMENT_GET_CLASS (srcelement);

  walk = templates = gst_element_class_get_pad_template_list (elemclass);
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

/* Returns TRUE if the caps are raw, or if they are compatible with the caps 
 * specified in the 'caps' property 
 * 
 * The decodebin_lock should be taken !
 */
static gboolean
are_raw_caps (GstDecodeBin * dbin, GstCaps * caps)
{
  GstCaps *intersection;
  gboolean res;

  GST_LOG_OBJECT (dbin, "Checking with caps %" GST_PTR_FORMAT, caps);

  intersection = gst_caps_intersect (dbin->caps, caps);

  res = (!(gst_caps_is_empty (intersection)));

  gst_caps_unref (intersection);

  GST_LOG_OBJECT (dbin, "Caps are %sfinal caps", res ? "" : "not ");

  return res;
}


/****
 * GstDecodeGroup functions
 ****/

static void
multi_queue_overrun_cb (GstElement * queue, GstDecodeGroup * group)
{
  GST_LOG_OBJECT (group->dbin, "multiqueue is full");

  /* if we haven't exposed the group, do it */
  DECODE_BIN_LOCK (group->dbin);
  gst_decode_group_expose (group);
  DECODE_BIN_UNLOCK (group->dbin);
}

static void
multi_queue_underrun_cb (GstElement * queue, GstDecodeGroup * group)
{
  GstDecodeBin *dbin = group->dbin;

  GST_LOG_OBJECT (dbin, "multiqueue is empty for group %p", group);

  /* Check if we need to activate another group */
  DECODE_BIN_LOCK (dbin);
  if ((group == dbin->activegroup) && dbin->groups) {
    GST_DEBUG_OBJECT (dbin, "Switching to new group");
    /* unexpose current active */
    gst_decode_group_hide (group);
    gst_decode_group_free (group);

    /* expose first group of groups */
    gst_decode_group_expose ((GstDecodeGroup *) dbin->groups->data);
  }
  DECODE_BIN_UNLOCK (dbin);
}

/* gst_decode_group_new
 *
 * Creates a new GstDecodeGroup. It is up to the caller to add it to the list
 * of groups.
 */
static GstDecodeGroup *
gst_decode_group_new (GstDecodeBin * dbin)
{
  GstDecodeGroup *group;
  GstElement *mq;

  GST_LOG_OBJECT (dbin, "Creating new group");

  if (!(mq = gst_element_factory_make ("multiqueue", NULL))) {
    GST_WARNING ("Couldn't create multiqueue element");
    return NULL;
  }

  g_object_set (G_OBJECT (mq),
      "max-size-bytes", 1 * 1024 * 1024,
      "max-size-time", G_GINT64_CONSTANT (0), "max-size-buffers", 0, NULL);

  group = g_new0 (GstDecodeGroup, 1);
  group->lock = g_mutex_new ();
  group->dbin = dbin;
  group->multiqueue = mq;
  group->exposed = FALSE;
  group->drained = FALSE;
  group->blocked = FALSE;
  group->complete = FALSE;
  group->endpads = NULL;

  g_signal_connect (G_OBJECT (mq), "overrun",
      G_CALLBACK (multi_queue_overrun_cb), group);
  g_signal_connect (G_OBJECT (mq), "underrun",
      G_CALLBACK (multi_queue_underrun_cb), group);

  gst_bin_add (GST_BIN (dbin), group->multiqueue);
  gst_element_set_state (group->multiqueue, GST_STATE_PAUSED);

  GST_LOG_OBJECT (dbin, "Returning new group %p", group);

  return group;
}

/** get_current_group:
 *
 * Returns the current non-completed group.
 *
 * Returns NULL if no groups are available, or all groups are completed.
 */
static GstDecodeGroup *
get_current_group (GstDecodeBin * dbin)
{
  GList *tmp;
  GstDecodeGroup *group = NULL;

  DECODE_BIN_LOCK (dbin);
  for (tmp = dbin->groups; tmp; tmp = g_list_next (tmp)) {
    GstDecodeGroup *this = (GstDecodeGroup *) tmp->data;

    if (!this->complete) {
      group = this;
      break;
    }
  }
  DECODE_BIN_UNLOCK (dbin);

  GST_LOG_OBJECT (dbin, "Returning group %p", group);

  return group;
}

static gboolean
group_demuxer_event_probe (GstPad * pad, GstEvent * event,
    GstDecodeGroup * group)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GST_DEBUG_OBJECT (group->dbin,
        "Got EOS on group input pads, exposing group if it wasn't before");
    gst_decode_group_expose (group);
  }
  return TRUE;
}

/* gst_decode_group_control_demuxer_pad
 *
 * Adds a new demuxer srcpad to the given group.
 *
 * Returns the srcpad of the multiqueue corresponding the given pad.
 * Returns NULL if there was an error.
 */
static GstPad *
gst_decode_group_control_demuxer_pad (GstDecodeGroup * group, GstPad * pad)
{
  GstPad *srcpad, *sinkpad;
  gchar *nb, *sinkname, *srcname;

  GST_LOG ("group:%p pad %s:%s", group, GST_DEBUG_PAD_NAME (pad));

  srcpad = NULL;

  if (!(sinkpad = gst_element_get_pad (group->multiqueue, "sink%d"))) {
    GST_ERROR ("Couldn't get sinkpad from multiqueue");
    return NULL;
  }

  if ((gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)) {
    GST_ERROR ("Couldn't link demuxer and multiqueue");
    goto beach;
  }

  sinkname = gst_pad_get_name (sinkpad);
  nb = sinkname + 4;
  srcname = g_strdup_printf ("src%s", nb);
  g_free (sinkname);

  GROUP_MUTEX_LOCK (group);

  if (!(srcpad = gst_element_get_pad (group->multiqueue, srcname))) {
    GST_ERROR ("Couldn't get srcpad %s from multiqueue", srcname);
    goto chiringuito;
  }

  /* connect event handler on pad to intercept EOS events */
  gst_pad_add_event_probe (pad, G_CALLBACK (group_demuxer_event_probe), group);

chiringuito:
  g_free (srcname);
  GROUP_MUTEX_UNLOCK (group);

beach:
  gst_object_unref (sinkpad);
  return srcpad;
}

static gboolean
gst_decode_group_control_source_pad (GstDecodeGroup * group, GstPad * pad)
{
  GstDecodePad *dpad;

  g_return_val_if_fail (group != NULL, FALSE);

  GST_LOG ("group:%p , pad %s:%s", group, GST_DEBUG_PAD_NAME (pad));

  /* FIXME : check if pad is already controlled */

  GROUP_MUTEX_LOCK (group);

  /* Create GstDecodePad for the pad */
  dpad = gst_decode_pad_new (group, pad, TRUE);

  group->endpads = g_list_append (group->endpads, dpad);

  GROUP_MUTEX_UNLOCK (group);

  return TRUE;
}

/* gst_decode_group_check_if_blocked:
 *
 * Call this when one of the pads blocked status has changed.
 * If the group is complete and blocked, the group will be marked as blocked
 * and will ghost/expose all pads on decodebin if the group is the current one.
 */
static void
gst_decode_group_check_if_blocked (GstDecodeGroup * group)
{
  GList *tmp;
  gboolean blocked = TRUE;

  GST_LOG ("group : %p", group);

  /* 1. don't do anything if group is not complete */
  if (!group->complete) {
    GST_DEBUG_OBJECT (group->dbin, "Group isn't complete yet");
    return;
  }

  for (tmp = group->endpads; tmp; tmp = g_list_next (tmp)) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;

    if (!dpad->blocked) {
      blocked = FALSE;
      break;
    }
  }

  /* 2. Update status of group */
  group->blocked = blocked;
  GST_LOG ("group is blocked:%d", blocked);

  /* 3. don't do anything if not blocked completely */
  if (!blocked)
    return;

  /* 4. if we're the current group, expose pads */
  DECODE_BIN_LOCK (group->dbin);
  if (!gst_decode_group_expose (group))
    GST_WARNING_OBJECT (group->dbin, "Couldn't expose group");
  DECODE_BIN_UNLOCK (group->dbin);
}

static void
gst_decode_group_check_if_drained (GstDecodeGroup * group)
{
  GList *tmp;
  GstDecodeBin *dbin = group->dbin;
  gboolean drained = TRUE;

  GST_LOG ("group : %p", group);

  for (tmp = group->endpads; tmp; tmp = g_list_next (tmp)) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;

    GST_LOG ("testing dpad %p", dpad);

    if (!dpad->drained) {
      drained = FALSE;
      break;
    }
  }

  group->drained = drained;
  GST_LOG ("group is drained");

  if (!drained)
    return;

  DECODE_BIN_LOCK (dbin);
  if ((group == dbin->activegroup) && dbin->groups) {
    GST_DEBUG_OBJECT (dbin, "Switching to new group");

    gst_decode_group_hide (group);
    gst_decode_group_free (group);

    gst_decode_group_expose ((GstDecodeGroup *) dbin->groups->data);
  }
  DECODE_BIN_UNLOCK (dbin);
}

/* gst_decode_group_expose:
 *
 * Expose this group's pads.
 *
 * Not MT safe, please take the group lock
 */

static gboolean
gst_decode_group_expose (GstDecodeGroup * group)
{
  GList *tmp;
  GList *next = NULL;

  if (group->dbin->activegroup) {
    GST_DEBUG_OBJECT (group->dbin, "A group is already active and exposed");
    DECODE_BIN_UNLOCK (group->dbin);
    return TRUE;
  }

  if (group->dbin->activegroup == group) {
    GST_WARNING ("Group %p is already exposed", group);
    DECODE_BIN_UNLOCK (group->dbin);
    return TRUE;
  }

  if (!group->dbin->groups
      || (group != (GstDecodeGroup *) group->dbin->groups->data)) {
    GST_WARNING ("Group %p is not the first group to expose", group);
    DECODE_BIN_UNLOCK (group->dbin);
    return FALSE;
  }

  GST_LOG ("Exposing group %p", group);

  /* Expose pads */

  for (tmp = group->endpads; tmp; tmp = next) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;
    gchar *padname;
    GstPad *ghost;

    next = g_list_next (tmp);

    /* 1. ghost pad */
    padname = g_strdup_printf ("src%d", group->dbin->nbpads);
    group->dbin->nbpads++;

    GST_LOG_OBJECT (group->dbin, "About to expose pad %s:%s",
        GST_DEBUG_PAD_NAME (dpad->pad));

    ghost = gst_ghost_pad_new (padname, dpad->pad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (GST_ELEMENT (group->dbin), ghost);
    group->ghosts = g_list_append (group->ghosts, ghost);

    g_free (padname);

    /* 2. emit signal */
    GST_DEBUG_OBJECT (group->dbin, "emitting new-decoded-pad");
    g_signal_emit (G_OBJECT (group->dbin),
        gst_decode_bin_signals[SIGNAL_NEW_DECODED_PAD], 0, ghost,
        (next == NULL));
    GST_DEBUG_OBJECT (group->dbin, "emitted new-decoded-pad");

    /* 3. Unblock internal  pad */
    GST_DEBUG_OBJECT (dpad->pad, "unblocking");
    gst_pad_set_blocked_async (dpad->pad, FALSE,
        (GstPadBlockCallback) source_pad_blocked_cb, dpad);
    GST_DEBUG_OBJECT (dpad->pad, "unblocked");

  }

  group->dbin->activegroup = group;

  /* pop off the first group */
  group->dbin->groups =
      g_list_delete_link (group->dbin->groups, group->dbin->groups);

  remove_fakesink (group->dbin);

  group->exposed = TRUE;
  GST_LOG_OBJECT (group->dbin, "Group %p exposed", group);
  return TRUE;
}

static void
gst_decode_group_hide (GstDecodeGroup * group)
{
  GList *tmp;

  GST_LOG ("Hiding group %p", group);

  if (group != group->dbin->activegroup) {
    GST_WARNING ("This group is not the active one, aborting");
    return;
  }

  GROUP_MUTEX_LOCK (group);

  /* Remove ghost pads */
  for (tmp = group->ghosts; tmp; tmp = g_list_next (tmp))
    gst_element_remove_pad (GST_ELEMENT (group->dbin), (GstPad *) tmp->data);

  g_list_free (group->ghosts);
  group->ghosts = NULL;

  group->exposed = FALSE;

  GROUP_MUTEX_UNLOCK (group);

  group->dbin->activegroup = NULL;
}

static void
gst_decode_group_free (GstDecodeGroup * group)
{
  GList *tmp;

  GST_LOG ("group %p", group);

  GROUP_MUTEX_LOCK (group);
  /* Clear all GstDecodePad */
  for (tmp = group->endpads; tmp; tmp = g_list_next (tmp)) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;

    g_free (dpad);
  }
  g_list_free (group->endpads);
  group->endpads = NULL;

  GROUP_MUTEX_UNLOCK (group);

  g_free (group->lock);
  g_free (group);
}

/* gst_decode_group_set_complete:
 *
 * Mark the group as complete. This means no more streams will be controlled
 * through this group.
 *
 * MT safe
 */
static void
gst_decode_group_set_complete (GstDecodeGroup * group)
{
  GST_LOG_OBJECT (group->dbin, "Setting group %p to COMPLETE", group);

  GROUP_MUTEX_LOCK (group);
  group->complete = TRUE;
  gst_decode_group_check_if_blocked (group);
  GROUP_MUTEX_UNLOCK (group);
}



/*************************
 * GstDecodePad functions
 *************************/

static void
source_pad_blocked_cb (GstPad * pad, gboolean blocked, GstDecodePad * dpad)
{
  GST_LOG_OBJECT (pad, "blocked:%d , dpad:%p, dpad->group:%p",
      blocked, dpad, dpad->group);

  /* Update this GstDecodePad status */
  dpad->blocked = blocked;

  if (blocked) {
    GROUP_MUTEX_LOCK (dpad->group);
    gst_decode_group_check_if_blocked (dpad->group);
    GROUP_MUTEX_UNLOCK (dpad->group);
  }
}

static gboolean
source_pad_event_probe (GstPad * pad, GstEvent * event, GstDecodePad * dpad)
{
  GST_LOG_OBJECT (pad, "%s dpad:%p", GST_EVENT_TYPE_NAME (event), dpad);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    /* Set our pad as drained */
    dpad->drained = TRUE;

    /* Check if all pads are drained */
    gst_decode_group_check_if_drained (dpad->group);
  }

  return TRUE;
}

/** gst_decode_pad_new:
 *
 * Creates a new GstDecodePad for the given pad.
 * If block is TRUE, Sets the pad blocking asynchronously
 */

static GstDecodePad *
gst_decode_pad_new (GstDecodeGroup * group, GstPad * pad, gboolean block)
{
  GstDecodePad *dpad;

  dpad = g_new0 (GstDecodePad, 1);
  dpad->pad = pad;
  dpad->group = group;
  dpad->blocked = FALSE;
  dpad->drained = TRUE;

  if (block)
    gst_pad_set_blocked_async (pad, TRUE,
        (GstPadBlockCallback) source_pad_blocked_cb, dpad);
  gst_pad_add_event_probe (pad, G_CALLBACK (source_pad_event_probe), dpad);
  return dpad;
}


/*****
 * Element add/remove
 *****/

/*
 * add_fakesink / remove_fakesink
 *
 * We use a sink so that the parent ::change_state returns GST_STATE_CHANGE_ASYNC
 * when that sink is present (since it's not connected to anything it will 
 * always return GST_STATE_CHANGE_ASYNC).
 *
 * But this is an ugly way of achieving this goal.
 * Ideally, we shouldn't use a sink and just return GST_STATE_CHANGE_ASYNC in
 * our ::change_state if we have not exposed the active group.
 * We also need to override ::get_state to fake the asynchronous behaviour.
 * Once the active group is exposed, we would then post a
 * GST_MESSAGE_STATE_DIRTY and return GST_STATE_CHANGE_SUCCESS (which will call
 * ::get_state .
 */

static void
add_fakesink (GstDecodeBin * decode_bin)
{
  GST_DEBUG_OBJECT (decode_bin, "Adding the fakesink");

  if (decode_bin->fakesink)
    return;

  decode_bin->fakesink =
      gst_element_factory_make ("fakesink", "async-fakesink");
  if (!decode_bin->fakesink)
    goto no_fakesink;

  /* hacky, remove sink flag, we don't want our decodebin to become a sink
   * just because we add a fakesink element to make us ASYNC */
  GST_OBJECT_FLAG_UNSET (decode_bin->fakesink, GST_ELEMENT_IS_SINK);

  if (!gst_bin_add (GST_BIN (decode_bin), decode_bin->fakesink))
    goto could_not_add;

  return;

  /* ERRORS */
no_fakesink:
  {
    g_warning ("can't find fakesink element, decodebin will not work");
    return;
  }
could_not_add:
  {
    g_warning ("Could not add fakesink to decodebin, decodebin will not work");
    gst_object_unref (decode_bin->fakesink);
    decode_bin->fakesink = NULL;
    return;
  }
}

static void
remove_fakesink (GstDecodeBin * decode_bin)
{
  if (decode_bin->fakesink == NULL)
    return;

  GST_DEBUG_OBJECT (decode_bin, "Removing the fakesink");

  gst_element_set_state (decode_bin->fakesink, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (decode_bin), decode_bin->fakesink);
  decode_bin->fakesink = NULL;

  gst_element_post_message (GST_ELEMENT_CAST (decode_bin),
      gst_message_new_state_dirty (GST_OBJECT_CAST (decode_bin)));
}

/*****
 * convenience functions
 *****/

/** find_sink_pad
 *
 * Returns the first sink pad of the given element, or NULL if it doesn't have
 * any.
 */

static GstPad *
find_sink_pad (GstElement * element)
{
  GstIterator *it;
  GstPad *pad = NULL;
  gpointer point;

  it = gst_element_iterate_sink_pads (element);

  if ((gst_iterator_next (it, &point)) == GST_ITERATOR_OK)
    pad = (GstPad *) point;

  gst_iterator_free (it);

  return pad;
}

static GstStateChangeReturn
gst_decode_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDecodeBin *dbin = GST_DECODE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* catch fatal errors that may have occured in the init function */
      if (dbin->typefind == NULL || dbin->fakesink == NULL) {
        GST_ELEMENT_ERROR (dbin, CORE, MISSING_PLUGIN, (NULL), (NULL));
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      add_fakesink (dbin);
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /* FIXME : put some cleanup functions here.. if needed */

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_decode_bin_debug, "decodebin2", 0,
      "decoder bin");

  return gst_element_register (plugin, "decodebin2", GST_RANK_NONE,
      GST_TYPE_DECODE_BIN);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "decodebin2",
    "decoder bin newer version", plugin_init, VERSION, GST_LICENSE,
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
