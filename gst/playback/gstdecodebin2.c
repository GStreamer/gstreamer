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
 * @short_description: Next-generation automatic decoding bin
 *
 * #GstBin that auto-magically constructs a decoding pipeline using available
 * decoders and demuxers via auto-plugging.
 *
 * At this stage, decodebin2 is considered UNSTABLE. The API provided in the
 * signals is expected to change in the near future. 
 *
 * To try out decodebin2, you can set the USE_DECODEBIN2 environment 
 * variable (USE_DECODEBIN2=1 for example). This will cause playbin to use
 * decodebin2 instead of the older decodebin for its internal auto-plugging.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "gstplay-marshal.h"
#include "gstplay-enum.h"
#include "gstfactorylists.h"

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
typedef struct _GstDecodeBin GstDecodeBin2;
typedef struct _GstDecodeBinClass GstDecodeBinClass;

#define GST_TYPE_DECODE_BIN             (gst_decode_bin_get_type())
#define GST_DECODE_BIN_CAST(obj)        ((GstDecodeBin*)(obj))
#define GST_DECODE_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECODE_BIN,GstDecodeBin))
#define GST_DECODE_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECODE_BIN,GstDecodeBinClass))
#define GST_IS_DECODE_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECODE_BIN))
#define GST_IS_DECODE_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECODE_BIN))

/**
 *  GstDecodeBin2:
 *
 *  The opaque #DecodeBin2 data structure
 */
struct _GstDecodeBin
{
  GstBin bin;                   /* we extend GstBin */

  /* properties */
  GstCaps *caps;                /* caps on which to stop decoding */
  gchar *encoding;              /* encoding of subtitles */

  GstElement *typefind;         /* this holds the typefind object */
  GstElement *fakesink;

  GMutex *lock;                 /* Protects activegroup and groups */
  GstDecodeGroup *activegroup;  /* group currently active */
  GList *groups;                /* List of non-active GstDecodeGroups, sorted in
                                 * order of creation. */
  GList *oldgroups;             /* List of no-longer-used GstDecodeGroups. 
                                 * Should be freed in dispose */
  gint nbpads;                  /* unique identifier for source pads */

  GValueArray *factories;       /* factories we can use for selecting elements */
  GList *subtitles;             /* List of elements with subtitle-encoding */

  gboolean have_type;           /* if we received the have_type signal */
  guint have_type_id;           /* signal id for have-type from typefind */
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
    gboolean (*autoplug_continue) (GstElement * element, GstPad * pad,
      GstCaps * caps);
  /* signal fired to get a list of factories to try to autoplug */
  GValueArray *(*autoplug_factories) (GstElement * element, GstPad * pad,
      GstCaps * caps);
  /* signal fired to sort the factories */
  GValueArray *(*autoplug_sort) (GstElement * element, GstPad * pad,
      GstCaps * caps, GValueArray * factories);
  /* signal fired to select from the proposed list of factories */
    GstAutoplugSelectResult (*autoplug_select) (GstElement * element,
      GstPad * pad, GstCaps * caps, GstElementFactory * factory);

  /* fired when the last group is drained */
  void (*drained) (GstElement * element);
};

/* signals */
enum
{
  SIGNAL_NEW_DECODED_PAD,
  SIGNAL_REMOVED_DECODED_PAD,
  SIGNAL_UNKNOWN_TYPE,
  SIGNAL_AUTOPLUG_CONTINUE,
  SIGNAL_AUTOPLUG_FACTORIES,
  SIGNAL_AUTOPLUG_SELECT,
  SIGNAL_AUTOPLUG_SORT,
  SIGNAL_DRAINED,
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_CAPS,
  PROP_SUBTITLE_ENCODING
};

static GstBinClass *parent_class;
static guint gst_decode_bin_signals[LAST_SIGNAL] = { 0 };

static const GstElementDetails gst_decode_bin_details =
GST_ELEMENT_DETAILS ("Decoder Bin",
    "Generic/Bin/Decoder",
    "Autoplug and decode to raw media",
    "Edward Hervey <edward@fluendo.com>");


static gboolean add_fakesink (GstDecodeBin * decode_bin);
static void remove_fakesink (GstDecodeBin * decode_bin);

static void type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin);

static gboolean gst_decode_bin_autoplug_continue (GstElement * element,
    GstPad * pad, GstCaps * caps);
static GValueArray *gst_decode_bin_autoplug_factories (GstElement *
    element, GstPad * pad, GstCaps * caps);
static GValueArray *gst_decode_bin_autoplug_sort (GstElement * element,
    GstPad * pad, GstCaps * caps, GValueArray * factories);
static GstAutoplugSelectResult gst_decode_bin_autoplug_select (GstElement *
    element, GstPad * pad, GstCaps * caps, GstElementFactory * factory);

static void gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_decode_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_decode_bin_set_caps (GstDecodeBin * dbin, GstCaps * caps);
static GstCaps *gst_decode_bin_get_caps (GstDecodeBin * dbin);
static void caps_notify_group_cb (GstPad * pad, GParamSpec * unused,
    GstDecodeGroup * group);
static void caps_notify_cb (GstPad * pad, GParamSpec * unused,
    GstDecodeBin * dbin);

static GstPad *find_sink_pad (GstElement * element);
static GstStateChangeReturn gst_decode_bin_change_state (GstElement * element,
    GstStateChange transition);

#define DECODE_BIN_LOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "locking from thread %p",				\
		    g_thread_self ());					\
    g_mutex_lock (GST_DECODE_BIN_CAST(dbin)->lock);			\
    GST_LOG_OBJECT (dbin,						\
		    "locked from thread %p",				\
		    g_thread_self ());					\
} G_STMT_END

#define DECODE_BIN_UNLOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "unlocking from thread %p",				\
		    g_thread_self ());					\
    g_mutex_unlock (GST_DECODE_BIN_CAST(dbin)->lock);			\
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
  gulong overrunsig;
  gulong underrunsig;
  guint nbdynamic;              /* number of dynamic pads in the group. */

  GList *endpads;               /* List of GstDecodePad of source pads to be exposed */
  GList *ghosts;                /* List of GstGhostPad for the endpads */
  GList *reqpads;               /* List of RequestPads for multiqueue. */
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


static GstDecodeGroup *gst_decode_group_new (GstDecodeBin * decode_bin,
    gboolean use_queue);
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

/* we collect the first result */
static gboolean
_gst_array_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gpointer array;

  array = g_value_get_boxed (handler_return);
  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_boxed (return_accu, array);

  return FALSE;
}

static gboolean
_gst_select_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  GstAutoplugSelectResult res;

  res = g_value_get_enum (handler_return);
  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_enum (return_accu, res);

  return FALSE;
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

  /**
   * GstDecodeBin2::new-decoded-pad:
   * @pad: the newly created pad
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
   * GstDecodeBin2::removed-decoded-pad:
   * @pad: the pad that was removed
   *
   * This signal is emitted when a 'final' caps pad has been removed.
   */
  gst_decode_bin_signals[SIGNAL_REMOVED_DECODED_PAD] =
      g_signal_new ("removed-decoded-pad", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDecodeBinClass, removed_decoded_pad), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);

  /**
   * GstDecodeBin2::unknown-type:
   * @pad: the new pad containing caps that cannot be resolved to a 'final' stream type.
   * @caps: the #GstCaps of the pad that cannot be resolved.
   *
   * This signal is emitted when a pad for which there is no further possible
   * decoding is added to the decodebin.
   */
  gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE] =
      g_signal_new ("unknown-type", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, unknown_type),
      NULL, NULL, gst_marshal_VOID__OBJECT_OBJECT, G_TYPE_NONE, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstDecodeBin2::autoplug-continue:
   * @pad: The #GstPad.
   * @caps: The #GstCaps found.
   *
   * This signal is emitted whenever decodebin2 finds a new stream. It is
   * emitted before looking for any elements that can handle that stream.
   *
   * Returns: #TRUE if you wish decodebin2 to look for elements that can
   * handle the given @caps. If #FALSE, those caps will be considered as
   * final and the pad will be exposed as such (see 'new-decoded-pad'
   * signal).
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_CONTINUE] =
      g_signal_new ("autoplug-continue", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_continue),
      _gst_boolean_accumulator, NULL, gst_play_marshal_BOOLEAN__OBJECT_OBJECT,
      G_TYPE_BOOLEAN, 2, GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstDecodeBin2::autoplug-factories:
   * @pad: The #GstPad.
   * @caps: The #GstCaps found.
   *
   * This function is emited when an array of possible factories for @caps on
   * @pad is needed. Decodebin2 will by default return an array with all
   * compatible factories, sorted by rank. 
   *
   * If this function returns NULL, @pad will be exposed as a final caps.
   *
   * If this function returns an empty array, the pad will be considered as 
   * having an unhandled type media type.
   *
   * Returns: a #GValueArray* with a list of factories to try. The factories are
   * by default tried in the returned order or based on the index returned by
   * "autoplug-select".
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_FACTORIES] =
      g_signal_new ("autoplug-factories", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass,
          autoplug_factories), _gst_array_accumulator, NULL,
      gst_play_marshal_BOXED__OBJECT_OBJECT, G_TYPE_VALUE_ARRAY, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstDecodeBin2::autoplug-sort:
   * @pad: The #GstPad.
   * @caps: The #GstCaps.
   * @factories: A #GValueArray of possible #GstElementFactory to use.
   *
   * Once decodebin2 has found the possible #GstElementFactory objects to try
   * for @caps on @pad, this signal is emited. The purpose of the signal is for
   * the application to perform additional sorting or filtering on the element
   * factory array.
   *
   * The callee should copy and modify @factories.
   *
   * Returns: A new sorted array of #GstElementFactory objects.
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_SORT] =
      g_signal_new ("autoplug-sort", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_sort),
      NULL, NULL, gst_play_marshal_BOXED__OBJECT_OBJECT_BOXED,
      G_TYPE_VALUE_ARRAY, 3, GST_TYPE_PAD, GST_TYPE_CAPS, G_TYPE_VALUE_ARRAY);

  /**
   * GstDecodeBin2::autoplug-select:
   * @pad: The #GstPad.
   * @caps: The #GstCaps.
   * @factories: A #GValueArray of possible #GstElementFactory to use, sorted by
   * rank (higher ranks come first).
   *
   * This signal is emitted once decodebin2 has found all the possible
   * #GstElementFactory that can be used to handle the given @caps.
   *
   * Returns: A #gint indicating what factory index from the @factories array
   * that you wish decodebin2 to use for trying to decode the given @caps.
   * Return -1 to stop selection of a factory and expose the pad as a raw type. 
   * The default handler always returns the first possible factory (index 0).
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_SELECT] =
      g_signal_new ("autoplug-select", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_select),
      _gst_select_accumulator, NULL,
      gst_play_marshal_ENUM__OBJECT_OBJECT_OBJECT,
      GST_TYPE_AUTOPLUG_SELECT_RESULT, 3, GST_TYPE_PAD, GST_TYPE_CAPS,
      GST_TYPE_ELEMENT_FACTORY);

  /**
   * GstDecodeBin2::drained
   *
   * This signal is emitted once decodebin2 has finished decoding all the data.
   *
   * Since: 0.10.16
   */
  gst_decode_bin_signals[SIGNAL_DRAINED] =
      g_signal_new ("drained", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, drained),
      NULL, NULL, gst_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  g_object_class_install_property (gobject_klass, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps", "The caps on which to stop decoding.",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_SUBTITLE_ENCODING,
      g_param_spec_string ("subtitle-encoding", "subtitle encoding",
          "Encoding to assume if input subtitles are not in UTF-8 encoding. "
          "If not set, the GST_SUBTITLE_ENCODING environment variable will "
          "be checked for an encoding to use. If that is not set either, "
          "ISO-8859-15 will be assumed.", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->autoplug_continue =
      GST_DEBUG_FUNCPTR (gst_decode_bin_autoplug_continue);
  klass->autoplug_factories =
      GST_DEBUG_FUNCPTR (gst_decode_bin_autoplug_factories);
  klass->autoplug_sort = GST_DEBUG_FUNCPTR (gst_decode_bin_autoplug_sort);
  klass->autoplug_select = GST_DEBUG_FUNCPTR (gst_decode_bin_autoplug_select);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&decoder_bin_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&decoder_bin_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_decode_bin_details);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_decode_bin_change_state);
}

static void
gst_decode_bin_init (GstDecodeBin * decode_bin)
{
  /* first filter out the interesting element factories */
  decode_bin->factories =
      gst_factory_list_get_elements (GST_FACTORY_LIST_DECODER);

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
    decode_bin->have_type_id =
        g_signal_connect (G_OBJECT (decode_bin->typefind), "have-type",
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
  GList *tmp;

  decode_bin = GST_DECODE_BIN (object);

  if (decode_bin->factories)
    g_value_array_free (decode_bin->factories);
  decode_bin->factories = NULL;

  if (decode_bin->activegroup) {
    gst_decode_group_free (decode_bin->activegroup);
    decode_bin->activegroup = NULL;
  }

  /* remove groups */
  for (tmp = decode_bin->groups; tmp; tmp = g_list_next (tmp)) {
    GstDecodeGroup *group = (GstDecodeGroup *) tmp->data;

    gst_decode_group_free (group);
  }
  g_list_free (decode_bin->groups);
  decode_bin->groups = NULL;

  for (tmp = decode_bin->oldgroups; tmp; tmp = g_list_next (tmp)) {
    GstDecodeGroup *group = (GstDecodeGroup *) tmp->data;

    gst_decode_group_free (group);
  }
  g_list_free (decode_bin->oldgroups);
  decode_bin->oldgroups = NULL;

  if (decode_bin->caps)
    gst_caps_unref (decode_bin->caps);
  decode_bin->caps = NULL;

  g_free (decode_bin->encoding);
  decode_bin->encoding = NULL;

  remove_fakesink (decode_bin);

  g_list_free (decode_bin->subtitles);
  decode_bin->subtitles = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decode_bin_finalize (GObject * object)
{
  GstDecodeBin *decode_bin;

  decode_bin = GST_DECODE_BIN (object);

  if (decode_bin->lock) {
    g_mutex_free (decode_bin->lock);
    decode_bin->lock = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* _set_caps
 * Changes the caps on which decodebin will stop decoding.
 * Will unref the previously set one. The refcount of the given caps will be
 * increased.
 * @caps can be NULL.
 *
 * MT-safe
 */
static void
gst_decode_bin_set_caps (GstDecodeBin * dbin, GstCaps * caps)
{
  GST_DEBUG_OBJECT (dbin, "Setting new caps: %" GST_PTR_FORMAT, caps);

  GST_OBJECT_LOCK (dbin);
  if (dbin->caps)
    gst_caps_unref (dbin->caps);
  if (caps)
    gst_caps_ref (caps);
  dbin->caps = caps;
  GST_OBJECT_UNLOCK (dbin);
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

  GST_OBJECT_LOCK (dbin);
  caps = dbin->caps;
  if (caps)
    gst_caps_ref (caps);
  GST_OBJECT_UNLOCK (dbin);

  return caps;
}

static void
gst_decode_bin_set_subs_encoding (GstDecodeBin * dbin, const gchar * encoding)
{
  GList *walk;

  GST_DEBUG_OBJECT (dbin, "Setting new encoding: %s", GST_STR_NULL (encoding));

  DECODE_BIN_LOCK (dbin);
  GST_OBJECT_LOCK (dbin);
  g_free (dbin->encoding);
  dbin->encoding = g_strdup (encoding);
  GST_OBJECT_UNLOCK (dbin);

  /* set the subtitle encoding on all added elements */
  for (walk = dbin->subtitles; walk; walk = g_list_next (walk)) {
    g_object_set (G_OBJECT (walk->data), "subtitle-encoding", dbin->encoding,
        NULL);
  }
  DECODE_BIN_UNLOCK (dbin);
}

static gchar *
gst_decode_bin_get_subs_encoding (GstDecodeBin * dbin)
{
  gchar *encoding;

  GST_DEBUG_OBJECT (dbin, "Getting currently set encoding");

  GST_OBJECT_LOCK (dbin);
  encoding = g_strdup (dbin->encoding);
  GST_OBJECT_UNLOCK (dbin);

  return encoding;
}

static void
gst_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecodeBin *dbin;

  dbin = GST_DECODE_BIN (object);

  switch (prop_id) {
    case PROP_CAPS:
      gst_decode_bin_set_caps (dbin, g_value_get_boxed (value));
      break;
    case PROP_SUBTITLE_ENCODING:
      gst_decode_bin_set_subs_encoding (dbin, g_value_get_string (value));
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
    case PROP_CAPS:
      g_value_take_boxed (value, gst_decode_bin_get_caps (dbin));
      break;
    case PROP_SUBTITLE_ENCODING:
      g_value_take_string (value, gst_decode_bin_get_subs_encoding (dbin));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/*****
 * Default autoplug signal handlers
 *****/
static gboolean
gst_decode_bin_autoplug_continue (GstElement * element, GstPad * pad,
    GstCaps * caps)
{
  GST_DEBUG_OBJECT (element, "autoplug-continue returns TRUE");

  /* by default we always continue */
  return TRUE;
}

static GValueArray *
gst_decode_bin_autoplug_factories (GstElement * element, GstPad * pad,
    GstCaps * caps)
{
  GValueArray *result;

  GST_DEBUG_OBJECT (element, "finding factories");

  /* return all compatible factories for caps */
  result =
      gst_factory_list_filter (GST_DECODE_BIN_CAST (element)->factories, caps);

  GST_DEBUG_OBJECT (element, "autoplug-factories returns %p", result);

  return result;
}

static GValueArray *
gst_decode_bin_autoplug_sort (GstElement * element, GstPad * pad,
    GstCaps * caps, GValueArray * factories)
{
  GValueArray *result;

  result = g_value_array_copy (factories);

  GST_DEBUG_OBJECT (element, "autoplug-sort returns %p", result);

  /* return input */
  return result;
}

static GstAutoplugSelectResult
gst_decode_bin_autoplug_select (GstElement * element, GstPad * pad,
    GstCaps * caps, GstElementFactory * factory)
{
  GST_DEBUG_OBJECT (element, "default autoplug-select returns TRY");

  /* Try factory. */
  return GST_AUTOPLUG_SELECT_TRY;
}

/********
 * Discovery methods
 *****/

static gboolean are_raw_caps (GstDecodeBin * dbin, GstCaps * caps);
static gboolean is_demuxer_element (GstElement * srcelement);

static gboolean connect_pad (GstDecodeBin * dbin, GstElement * src,
    GstPad * pad, GstCaps * caps, GValueArray * factories,
    GstDecodeGroup * group);
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

/* called when a new pad is discovered. It will perform some basic actions
 * before trying to link something to it.
 *
 *  - Check the caps, don't do anything when there are no caps or when they have
 *    no good type.
 *  - signal AUTOPLUG_CONTINUE to check if we need to continue autoplugging this
 *    pad.
 *  - if the caps are non-fixed, setup a handler to continue autoplugging when
 *    the caps become fixed (connect to notify::caps).
 *  - get list of factories to autoplug.
 *  - continue autoplugging to one of the factories.
 */
static void
analyze_new_pad (GstDecodeBin * dbin, GstElement * src, GstPad * pad,
    GstCaps * caps, GstDecodeGroup * group)
{
  gboolean apcontinue = TRUE;
  GValueArray *factories = NULL, *result = NULL;

  GST_DEBUG_OBJECT (dbin, "Pad %s:%s caps:%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  if ((caps == NULL) || gst_caps_is_empty (caps))
    goto unknown_type;

  if (gst_caps_is_any (caps))
    goto any_caps;

  /* 1. Emit 'autoplug-continue' the result will tell us if this pads needs
   * further autoplugging. */
  g_signal_emit (G_OBJECT (dbin),
      gst_decode_bin_signals[SIGNAL_AUTOPLUG_CONTINUE], 0, pad, caps,
      &apcontinue);

  /* 1.a if autoplug-continue is FALSE or caps is a raw format, goto pad_is_final */
  if ((!apcontinue) || are_raw_caps (dbin, caps))
    goto expose_pad;

  /* 1.b when the caps are not fixed yet, we can't be sure what element to
   * connect. We delay autoplugging until the caps are fixed */
  if (!gst_caps_is_fixed (caps))
    goto non_fixed;

  /* 1.c else get the factories and if there's no compatible factory goto
   * unknown_type */
  g_signal_emit (G_OBJECT (dbin),
      gst_decode_bin_signals[SIGNAL_AUTOPLUG_FACTORIES], 0, pad, caps,
      &factories);

  /* NULL means that we can expose the pad */
  if (factories == NULL)
    goto expose_pad;

  /* if the array is empty, we have an unknown type */
  if (factories->n_values == 0) {
    /* no compatible factories */
    g_value_array_free (factories);
    goto unknown_type;
  }

  /* 1.d sort some more. */
  g_signal_emit (G_OBJECT (dbin),
      gst_decode_bin_signals[SIGNAL_AUTOPLUG_SORT], 0, pad, caps, factories,
      &result);
  g_value_array_free (factories);
  factories = result;

  /* 1.e else continue autoplugging something from the list. */
  GST_LOG_OBJECT (pad, "Let's continue discovery on this pad");
  connect_pad (dbin, src, pad, caps, factories, group);

  g_value_array_free (factories);

  return;

expose_pad:
  {
    GST_LOG_OBJECT (dbin, "Pad is final. autoplug-continue:%d", apcontinue);
    expose_pad (dbin, src, pad, group);
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

    if (src == dbin->typefind) {
      gchar *desc;

      desc = gst_pb_utils_get_decoder_description (caps);
      GST_ELEMENT_ERROR (dbin, STREAM, CODEC_NOT_FOUND,
          (_("A %s plugin is required to play this stream, but not installed."),
              desc),
          ("No decoder to handle media type '%s'",
              gst_structure_get_name (gst_caps_get_structure (caps, 0))));
      g_free (desc);
    }

    gst_element_post_message (GST_ELEMENT_CAST (dbin),
        gst_missing_decoder_message_new (GST_ELEMENT_CAST (dbin), caps));
    return;
  }
non_fixed:
  {
    GST_DEBUG_OBJECT (pad, "pad has non-fixed caps delay autoplugging");
    goto setup_caps_delay;
  }
any_caps:
  {
    GST_WARNING_OBJECT (pad,
        "pad has ANY caps, not able to autoplug to anything");
    goto setup_caps_delay;
  }
setup_caps_delay:
  {
    /* connect to caps notification */
    if (group) {
      GROUP_MUTEX_LOCK (group);
      group->nbdynamic++;
      GST_LOG ("Group %p has now %d dynamic elements", group, group->nbdynamic);
      GROUP_MUTEX_UNLOCK (group);
      g_signal_connect (G_OBJECT (pad), "notify::caps",
          G_CALLBACK (caps_notify_group_cb), group);
    } else
      g_signal_connect (G_OBJECT (pad), "notify::caps",
          G_CALLBACK (caps_notify_cb), dbin);
    return;
  }
}


/* connect_pad:
 *
 * Try to connect the given pad to an element created from one of the factories,
 * and recursively.
 *
 * Returns TRUE if an element was properly created and linked
 */
static gboolean
connect_pad (GstDecodeBin * dbin, GstElement * src, GstPad * pad,
    GstCaps * caps, GValueArray * factories, GstDecodeGroup * group)
{
  gboolean res = FALSE;
  GstPad *mqpad = NULL;

  g_return_val_if_fail (factories != NULL, FALSE);
  g_return_val_if_fail (factories->n_values > 0, FALSE);

  GST_DEBUG_OBJECT (dbin, "pad %s:%s , group:%p",
      GST_DEBUG_PAD_NAME (pad), group);

  /* 1. is element demuxer or parser */
  if (is_demuxer_element (src)) {
    GST_LOG_OBJECT (src, "is a demuxer, connecting the pad through multiqueue");

    if (!group)
      if (!(group = get_current_group (dbin))) {
        group = gst_decode_group_new (dbin, TRUE);
        DECODE_BIN_LOCK (dbin);
        dbin->groups = g_list_append (dbin->groups, group);
        DECODE_BIN_UNLOCK (dbin);
      }

    if (!(mqpad = gst_decode_group_control_demuxer_pad (group, pad)))
      goto beach;
    src = group->multiqueue;
    pad = mqpad;
  }

  /* 2. Try to create an element and link to it */
  while (factories->n_values > 0) {
    GstAutoplugSelectResult ret;
    GstElementFactory *factory;
    GstElement *element;
    GstPad *sinkpad;
    gboolean subtitle;

    /* take first factory */
    factory = g_value_get_object (g_value_array_get_nth (factories, 0));
    /* Remove selected factory from the list. */
    g_value_array_remove (factories, 0);

    /* emit autoplug-select to see what we should do with it. */
    g_signal_emit (G_OBJECT (dbin),
        gst_decode_bin_signals[SIGNAL_AUTOPLUG_SELECT],
        0, pad, caps, factory, &ret);

    switch (ret) {
      case GST_AUTOPLUG_SELECT_TRY:
        GST_DEBUG_OBJECT (dbin, "autoplug select requested try");
        break;
      case GST_AUTOPLUG_SELECT_EXPOSE:
        GST_DEBUG_OBJECT (dbin, "autoplug select requested expose");
        /* expose the pad, we don't have the source element */
        expose_pad (dbin, src, pad, group);
        res = TRUE;
        goto beach;
      case GST_AUTOPLUG_SELECT_SKIP:
        GST_DEBUG_OBJECT (dbin, "autoplug select requested skip");
        continue;
      default:
        GST_WARNING_OBJECT (dbin, "autoplug select returned unhandled %d", ret);
        break;
    }

    /* 2.1. Try to create an element */
    if ((element = gst_element_factory_create (factory, NULL)) == NULL) {
      GST_WARNING_OBJECT (dbin, "Could not create an element from %s",
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
      continue;
    }

    /* ... activate it ... We do this before adding it to the bin so that we
     * don't accidentally make it post error messages that will stop
     * everything. */
    if ((gst_element_set_state (element,
                GST_STATE_READY)) == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (dbin, "Couldn't set %s to READY",
          GST_ELEMENT_NAME (element));
      gst_object_unref (element);
      continue;
    }

    /* 2.3. Find its sink pad, this should work after activating it. */
    if (!(sinkpad = find_sink_pad (element))) {
      GST_WARNING_OBJECT (dbin, "Element %s doesn't have a sink pad",
          GST_ELEMENT_NAME (element));
      gst_object_unref (element);
      continue;
    }

    /* 2.4 add it ... */
    if (!(gst_bin_add (GST_BIN_CAST (dbin), element))) {
      GST_WARNING_OBJECT (dbin, "Couldn't add %s to the bin",
          GST_ELEMENT_NAME (element));
      gst_object_unref (sinkpad);
      gst_object_unref (element);
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
    gst_object_unref (sinkpad);
    GST_LOG_OBJECT (dbin, "linked on pad %s:%s", GST_DEBUG_PAD_NAME (pad));

    /* link this element further */
    connect_element (dbin, element, group);

    /* try to configure the subtitle encoding property when we can */
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (element),
            "subtitle-encoding")) {
      GST_DEBUG_OBJECT (dbin,
          "setting subtitle-encoding=%s to element", dbin->encoding);
      g_object_set (G_OBJECT (element), "subtitle-encoding", dbin->encoding,
          NULL);
      subtitle = TRUE;
    } else
      subtitle = FALSE;

    /* Bring the element to the state of the parent */
    if ((gst_element_set_state (element,
                GST_STATE_PAUSED)) == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (dbin, "Couldn't set %s to PAUSED",
          GST_ELEMENT_NAME (element));
      gst_element_set_state (element, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (dbin), element);
      continue;
    }
    if (subtitle) {
      DECODE_BIN_LOCK (dbin);
      /* we added the element now, add it to the list of subtitle-encoding
       * elements when we can set the property */
      dbin->subtitles = g_list_prepend (dbin->subtitles, element);
      DECODE_BIN_UNLOCK (dbin);
    }

    res = TRUE;
    break;
  }

beach:
  if (mqpad)
    gst_object_unref (mqpad);

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
      GST_LOG ("Adding signals to element %s in group %p",
          GST_ELEMENT_NAME (element), group);
      GROUP_MUTEX_LOCK (group);
      group->nbdynamic++;
      GST_LOG ("Group %p has now %d dynamic elements", group, group->nbdynamic);
      GROUP_MUTEX_UNLOCK (group);
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
  GstPad *mqpad = NULL;

  GST_DEBUG_OBJECT (dbin, "pad %s:%s, group:%p",
      GST_DEBUG_PAD_NAME (pad), group);

  isdemux = is_demuxer_element (src);

  if (!group)
    if (!(group = get_current_group (dbin))) {
      group = gst_decode_group_new (dbin, isdemux);
      DECODE_BIN_LOCK (dbin);
      dbin->groups = g_list_append (dbin->groups, group);
      DECODE_BIN_UNLOCK (dbin);
      newgroup = TRUE;
    }

  if (isdemux) {
    GST_LOG_OBJECT (src, "connecting the pad through multiqueue");

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
  if (mqpad)
    gst_object_unref (mqpad);

beach:
  return;
}

static void
type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin)
{
  GstPad *pad;

  GST_DEBUG_OBJECT (decode_bin, "typefind found caps %" GST_PTR_FORMAT, caps);

  /* we can only deal with one type, we don't yet support dynamically changing
   * caps from the typefind element */
  if (decode_bin->have_type)
    goto exit;

  decode_bin->have_type = TRUE;

  pad = gst_element_get_static_pad (typefind, "src");

  analyze_new_pad (decode_bin, typefind, pad, caps, NULL);

  gst_object_unref (pad);

exit:
  return;
}

static void
pad_added_group_cb (GstElement * element, GstPad * pad, GstDecodeGroup * group)
{
  GstCaps *caps;
  gboolean expose = FALSE;

  GST_DEBUG_OBJECT (pad, "pad added, group:%p", group);

  caps = gst_pad_get_caps (pad);
  analyze_new_pad (group->dbin, element, pad, caps, group);
  if (caps)
    gst_caps_unref (caps);

  GROUP_MUTEX_LOCK (group);
  group->nbdynamic--;
  GST_LOG ("Group %p has now %d dynamic objects", group, group->nbdynamic);
  if (group->nbdynamic == 0)
    expose = TRUE;
  GROUP_MUTEX_UNLOCK (group);

  if (expose) {
    GST_LOG
        ("That was the last dynamic object, now attempting to expose the group");
    DECODE_BIN_LOCK (group->dbin);
    gst_decode_group_expose (group);
    DECODE_BIN_UNLOCK (group->dbin);
  }
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

static void
caps_notify_cb (GstPad * pad, GParamSpec * unused, GstDecodeBin * dbin)
{
  GstElement *element;

  GST_LOG_OBJECT (dbin, "Notified caps for pad %s:%s",
      GST_DEBUG_PAD_NAME (pad));

  element = GST_ELEMENT_CAST (gst_pad_get_parent (pad));

  pad_added_cb (element, pad, dbin);

  gst_object_unref (element);
}

static void
caps_notify_group_cb (GstPad * pad, GParamSpec * unused, GstDecodeGroup * group)
{
  GstElement *element;

  GST_LOG_OBJECT (pad, "Notified caps for pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  element = GST_ELEMENT_CAST (gst_pad_get_parent (pad));

  pad_added_group_cb (element, pad, group);

  gst_object_unref (element);
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

  /* lock for getting the caps */
  GST_OBJECT_LOCK (dbin);
  intersection = gst_caps_intersect (dbin->caps, caps);
  GST_OBJECT_UNLOCK (dbin);

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
gst_decode_group_new (GstDecodeBin * dbin, gboolean use_queue)
{
  GstDecodeGroup *group;
  GstElement *mq;

  GST_LOG_OBJECT (dbin, "Creating new group");

  if (use_queue) {
    if (!(mq = gst_element_factory_make ("multiqueue", NULL))) {
      GST_WARNING ("Couldn't create multiqueue element");
      return NULL;
    }
  } else {
    mq = NULL;
  }

  group = g_new0 (GstDecodeGroup, 1);
  group->lock = g_mutex_new ();
  group->dbin = dbin;
  group->multiqueue = mq;
  group->exposed = FALSE;
  group->drained = FALSE;
  group->blocked = FALSE;
  group->complete = FALSE;
  group->endpads = NULL;
  group->reqpads = NULL;

  if (mq) {
    /* we first configure the multiqueue to buffer an unlimited number of
     * buffers up to 5 seconds or, when no timestamps are present, up to 2 MB of
     * memory. When this queue overruns, we assume the group is complete and can
     * be exposed. */
    g_object_set (G_OBJECT (mq),
        "max-size-bytes", 2 * 1024 * 1024,
        "max-size-time", 5 * GST_SECOND, "max-size-buffers", 0, NULL);
    /* will expose the group */
    group->overrunsig = g_signal_connect (G_OBJECT (mq), "overrun",
        G_CALLBACK (multi_queue_overrun_cb), group);
    /* will hide the group again, this is usually called when the multiqueue is
     * drained because of EOS. */
    group->underrunsig = g_signal_connect (G_OBJECT (mq), "underrun",
        G_CALLBACK (multi_queue_underrun_cb), group);

    gst_bin_add (GST_BIN (dbin), mq);
    gst_element_set_state (mq, GST_STATE_PAUSED);
  }

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

    GST_LOG_OBJECT (dbin, "group %p, complete:%d", this, this->complete);

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
    DECODE_BIN_LOCK (group->dbin);
    gst_decode_group_expose (group);
    DECODE_BIN_UNLOCK (group->dbin);
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

  if (!(sinkpad = gst_element_get_request_pad (group->multiqueue, "sink%d"))) {
    GST_ERROR ("Couldn't get sinkpad from multiqueue");
    return NULL;
  }

  if ((gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)) {
    GST_ERROR ("Couldn't link demuxer and multiqueue");
    goto beach;
  }

  group->reqpads = g_list_append (group->reqpads, sinkpad);

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
 *
 * Call with the group lock taken ! MT safe
 */
static void
gst_decode_group_check_if_blocked (GstDecodeGroup * group)
{
  GList *tmp;
  gboolean blocked = TRUE;

  GST_LOG ("group : %p , ->complete:%d , ->nbdynamic:%d",
      group, group->complete, group->nbdynamic);

  /* 1. don't do anything if group is not complete */
  if (!group->complete || group->nbdynamic) {
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
  if (!drained)
    return;

  /* we are drained. Check if there is a next group to activate */
  DECODE_BIN_LOCK (dbin);
  if ((group == dbin->activegroup) && dbin->groups) {
    GST_DEBUG_OBJECT (dbin, "Switching to new group");

    /* hide current group */
    gst_decode_group_hide (group);
    /* expose next group */
    gst_decode_group_expose ((GstDecodeGroup *) dbin->groups->data);
    /* we're not yet drained now */
    drained = FALSE;
  }
  DECODE_BIN_UNLOCK (dbin);

  if (drained) {
    /* no more groups to activate, we're completely drained now */
    GST_LOG ("all groups drained, fire signal");
    g_signal_emit (G_OBJECT (dbin), gst_decode_bin_signals[SIGNAL_DRAINED], 0,
        NULL);
  }
}

/* sort_end_pads:
 * GCompareFunc to use with lists of GstPad.
 * Sorts pads by mime type.
 * First video (raw, then non-raw), then audio (raw, then non-raw),
 * then others.
 *
 * Return: negative if a<b, 0 if a==b, positive if a>b
 */

static gint
sort_end_pads (GstDecodePad * da, GstDecodePad * db)
{
  GstPad *a, *b;
  gint va, vb;
  GstCaps *capsa, *capsb;
  GstStructure *sa, *sb;
  const gchar *namea, *nameb;

  a = da->pad;
  b = db->pad;

  capsa = gst_pad_get_caps (a);
  capsb = gst_pad_get_caps (b);

  sa = gst_caps_get_structure ((const GstCaps *) capsa, 0);
  sb = gst_caps_get_structure ((const GstCaps *) capsb, 0);

  namea = gst_structure_get_name (sa);
  nameb = gst_structure_get_name (sb);

  if (g_strrstr (namea, "video/x-raw-"))
    va = 0;
  else if (g_strrstr (namea, "video/"))
    va = 1;
  else if (g_strrstr (namea, "audio/x-raw"))
    va = 2;
  else if (g_strrstr (namea, "audio/"))
    va = 3;
  else
    va = 4;

  if (g_strrstr (nameb, "video/x-raw-"))
    vb = 0;
  else if (g_strrstr (nameb, "video/"))
    vb = 1;
  else if (g_strrstr (nameb, "audio/x-raw"))
    vb = 2;
  else if (g_strrstr (nameb, "audio/"))
    vb = 3;
  else
    vb = 4;

  gst_caps_unref (capsa);
  gst_caps_unref (capsb);

  return va - vb;
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
    return TRUE;
  }

  if (group->dbin->activegroup == group) {
    GST_WARNING ("Group %p is already exposed", group);
    return TRUE;
  }

  if (!group->dbin->groups
      || (group != (GstDecodeGroup *) group->dbin->groups->data)) {
    GST_WARNING ("Group %p is not the first group to expose", group);
    return FALSE;
  }

  if (group->nbdynamic) {
    GST_WARNING ("Group %p still has %d dynamic objects, not exposing yet",
        group, group->nbdynamic);
    return FALSE;
  }

  GST_LOG ("Exposing group %p", group);

  if (group->multiqueue) {
    /* update runtime limits. At runtime, we try to keep the amount of buffers
     * in the queues as low as possible (but at least 5 buffers). */
    g_object_set (G_OBJECT (group->multiqueue),
        "max-size-bytes", 2 * 1024 * 1024,
        "max-size-time", 2 * GST_SECOND, "max-size-buffers", 5, NULL);
    /* we can now disconnect any overrun signal, which is used to expose the
     * group. */
    if (group->overrunsig) {
      GST_LOG ("Disconnecting overrun");
      g_signal_handler_disconnect (group->multiqueue, group->overrunsig);
      group->overrunsig = 0;
    }
  }

  /* re-order pads : video, then audio, then others */
  group->endpads = g_list_sort (group->endpads, (GCompareFunc) sort_end_pads);

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
  }

  /* signal no-more-pads. This allows the application to hook stuff to the
   * exposed pads */
  GST_LOG_OBJECT (group->dbin, "signalling no-more-pads");
  gst_element_no_more_pads (GST_ELEMENT (group->dbin));

  /* 3. Unblock internal pads. The application should have connected stuff now
   * so that streaming can continue. */
  for (tmp = group->endpads; tmp; tmp = next) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;

    next = g_list_next (tmp);

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
  group->dbin->oldgroups = g_list_append (group->dbin->oldgroups, group);
}

static void
deactivate_free_recursive (GstDecodeGroup * group, GstElement * element)
{
  GstIterator *it;
  GstIteratorResult res;
  gpointer point;
  GstDecodeBin *dbin;

  dbin = group->dbin;

  GST_LOG ("element:%s", GST_ELEMENT_NAME (element));

  /* call on downstream elements */
  it = gst_element_iterate_src_pads (element);

restart:

  while (1) {
    res = gst_iterator_next (it, &point);
    switch (res) {
      case GST_ITERATOR_DONE:
        goto done;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        goto restart;
      case GST_ITERATOR_ERROR:
      {
        GST_WARNING ("Had an error while iterating source pads of element: %s",
            GST_ELEMENT_NAME (element));
        goto beach;
      }
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD (point);
        GstPad *peerpad = NULL;

        if ((peerpad = gst_pad_get_peer (pad))) {
          GstObject *parent;

          parent = gst_pad_get_parent (peerpad);
          gst_object_unref (peerpad);

          if (parent && GST_IS_ELEMENT (parent))
            deactivate_free_recursive (group, GST_ELEMENT (parent));
          if (parent)
            gst_object_unref (parent);
        }
      }
        break;
      default:
        break;
    }
  }

done:
  gst_element_set_state (element, GST_STATE_NULL);
  DECODE_BIN_LOCK (dbin);
  /* remove possible subtitle element */
  dbin->subtitles = g_list_remove (dbin->subtitles, element);
  DECODE_BIN_UNLOCK (dbin);
  gst_bin_remove (GST_BIN (dbin), element);

beach:
  gst_iterator_free (it);

  return;
}

static void
gst_decode_group_free (GstDecodeGroup * group)
{
  GList *tmp;

  GST_LOG ("group %p", group);

  GROUP_MUTEX_LOCK (group);

  /* free ghost pads */
  if (group == group->dbin->activegroup) {
    for (tmp = group->ghosts; tmp; tmp = g_list_next (tmp))
      gst_element_remove_pad (GST_ELEMENT (group->dbin), (GstPad *) tmp->data);

    g_list_free (group->ghosts);
    group->ghosts = NULL;
  }

  /* Clear all GstDecodePad */
  for (tmp = group->endpads; tmp; tmp = g_list_next (tmp)) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;

    g_free (dpad);
  }
  g_list_free (group->endpads);
  group->endpads = NULL;

  /* release request pads */
  for (tmp = group->reqpads; tmp; tmp = g_list_next (tmp)) {
    gst_element_release_request_pad (group->multiqueue, GST_PAD (tmp->data));
  }
  g_list_free (group->reqpads);
  group->reqpads = NULL;

  /* disconnect signal handlers on multiqueue */
  if (group->multiqueue) {
    if (group->underrunsig)
      g_signal_handler_disconnect (group->multiqueue, group->underrunsig);
    if (group->overrunsig)
      g_signal_handler_disconnect (group->multiqueue, group->overrunsig);
    deactivate_free_recursive (group, group->multiqueue);
  }

  /* remove all elements */

  GROUP_MUTEX_UNLOCK (group);

  g_mutex_free (group->lock);
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

    GST_DEBUG_OBJECT (pad, "we received EOS");

    /* Check if all pads are drained. If there is a next group to expose, we
     * will remove the ghostpad of the current group first, which unlinks the
     * peer and so drops the EOS. */
    gst_decode_group_check_if_drained (dpad->group);
  }
  /* never drop events */
  return TRUE;
}

/*gst_decode_pad_new:
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

static gboolean
add_fakesink (GstDecodeBin * decode_bin)
{
  GST_DEBUG_OBJECT (decode_bin, "Adding the fakesink");

  if (decode_bin->fakesink)
    return TRUE;

  decode_bin->fakesink =
      gst_element_factory_make ("fakesink", "async-fakesink");
  if (!decode_bin->fakesink)
    goto no_fakesink;

  /* enable sync so that we force ASYNC preroll */
  g_object_set (G_OBJECT (decode_bin->fakesink), "sync", TRUE, NULL);

  /* hacky, remove sink flag, we don't want our decodebin to become a sink
   * just because we add a fakesink element to make us ASYNC */
  GST_OBJECT_FLAG_UNSET (decode_bin->fakesink, GST_ELEMENT_IS_SINK);

  if (!gst_bin_add (GST_BIN (decode_bin), decode_bin->fakesink))
    goto could_not_add;

  return TRUE;

  /* ERRORS */
no_fakesink:
  {
    g_warning ("can't find fakesink element, decodebin will not work");
    return FALSE;
  }
could_not_add:
  {
    g_warning ("Could not add fakesink to decodebin, decodebin will not work");
    gst_object_unref (decode_bin->fakesink);
    decode_bin->fakesink = NULL;
    return FALSE;
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
}

/*****
 * convenience functions
 *****/

/* find_sink_pad
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
      if (dbin->typefind == NULL)
        goto missing_typefind;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      dbin->have_type = FALSE;
      if (!add_fakesink (dbin))
        goto missing_fakesink;
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /* FIXME : put some cleanup functions here.. if needed */

  return ret;

/* ERRORS */
missing_typefind:
  {
    gst_element_post_message (element,
        gst_missing_element_message_new (element, "typefind"));
    GST_ELEMENT_ERROR (dbin, CORE, MISSING_PLUGIN, (NULL), ("no typefind!"));
    return GST_STATE_CHANGE_FAILURE;
  }
missing_fakesink:
  {
    gst_element_post_message (element,
        gst_missing_element_message_new (element, "fakesink"));
    GST_ELEMENT_ERROR (dbin, CORE, MISSING_PLUGIN, (NULL), ("no fakesink!"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

gboolean
gst_decode_bin_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_decode_bin_debug, "decodebin2", 0,
      "decoder bin");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  return gst_element_register (plugin, "decodebin2", GST_RANK_NONE,
      GST_TYPE_DECODE_BIN);
}
