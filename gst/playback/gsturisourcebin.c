/* GStreamer
 * Copyright (C) <2015> Jan Schmidt <jan@centricular.com>
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

/**
 * SECTION:element-urisourcebin
 * @title: urisourcebin
 *
 * urisourcebin is an element for accessing URIs in a uniform manner.
 *
 * It handles selecting a URI source element and potentially download
 * buffering for network sources. It produces one or more source pads,
 * depending on the input source, for feeding to decoding chains or decodebin.
 *
 * The main configuration is via the #GstURISourceBin:uri property.
 *
 * <emphasis>urisourcebin is still experimental API and a technology preview.
 * Its behaviour and exposed API is subject to change.</emphasis>
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>
#include <gst/pbutils/missing-plugins.h>

#include "gstplay-enum.h"
#include "gstrawcaps.h"
#include "gstplayback.h"
#include "gstplaybackutils.h"

#define GST_TYPE_URI_SOURCE_BIN \
  (gst_uri_source_bin_get_type())
#define GST_URI_SOURCE_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_URI_SOURCE_BIN,GstURISourceBin))
#define GST_URI_SOURCE_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_URI_SOURCE_BIN,GstURISourceBinClass))
#define GST_IS_URI_SOURCE_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_URI_SOURCE_BIN))
#define GST_IS_URI_SOURCE_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_URI_SOURCE_BIN))
#define GST_URI_SOURCE_BIN_CAST(obj) ((GstURISourceBin *) (obj))

typedef struct _GstURISourceBin GstURISourceBin;
typedef struct _GstURISourceBinClass GstURISourceBinClass;
typedef struct _ChildSrcPadInfo ChildSrcPadInfo;
typedef struct _OutputSlotInfo OutputSlotInfo;

#define GST_URI_SOURCE_BIN_LOCK(dec) (g_mutex_lock(&((GstURISourceBin*)(dec))->lock))
#define GST_URI_SOURCE_BIN_UNLOCK(dec) (g_mutex_unlock(&((GstURISourceBin*)(dec))->lock))

#define BUFFERING_LOCK(ubin) G_STMT_START {				\
    GST_LOG_OBJECT (ubin,						\
		    "buffering locking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_lock (&GST_URI_SOURCE_BIN_CAST(ubin)->buffering_lock);		\
    GST_LOG_OBJECT (ubin,						\
		    "buffering lock from thread %p",			\
		    g_thread_self ());					\
} G_STMT_END

#define BUFFERING_UNLOCK(ubin) G_STMT_START {				\
    GST_LOG_OBJECT (ubin,						\
		    "buffering unlocking from thread %p",		\
		    g_thread_self ());					\
    g_mutex_unlock (&GST_URI_SOURCE_BIN_CAST(ubin)->buffering_lock);		\
} G_STMT_END

/* Track a source pad from a child that
 * is linked or needs linking to an output
 * slot */
struct _ChildSrcPadInfo
{
  guint blocking_probe_id;
  guint event_probe_id;
  GstPad *demux_src_pad;
  GstCaps *cur_caps;            /* holds ref */

  /* Configured output slot, if any */
  OutputSlotInfo *output_slot;
};

struct _OutputSlotInfo
{
  ChildSrcPadInfo *linked_info; /* demux source pad info feeding this slot, if any */
  GstElement *queue;            /* queue2 or downloadbuffer */
  GstPad *sinkpad;              /* Sink pad of the queue eleemnt */
  GstPad *srcpad;               /* Output ghost pad */
  gboolean is_eos;              /* Did EOS get fed into the buffering element */
};

/**
 * GstURISourceBin
 *
 * urisourcebin element struct
 */
struct _GstURISourceBin
{
  GstBin parent_instance;

  GMutex lock;                  /* lock for constructing */

  GMutex factories_lock;
  guint32 factories_cookie;
  GList *factories;             /* factories we can use for selecting elements */

  gchar *uri;
  guint64 connection_speed;

  gboolean is_stream;
  gboolean is_adaptive;
  gboolean need_queue;
  guint64 buffer_duration;      /* When buffering, buffer duration (ns) */
  guint buffer_size;            /* When buffering, buffer size (bytes) */
  gboolean download;
  gboolean use_buffering;

  GstElement *source;
  GList *typefinds;             /* list of typefind element */

  GstElement *demuxer;          /* Adaptive demuxer if any */
  GSList *out_slots;

  GHashTable *streams;
  guint numpads;

  /* for dynamic sources */
  guint src_np_sig_id;          /* new-pad signal id */

  gboolean async_pending;       /* async-start has been emitted */

  guint64 ring_buffer_max_size; /* 0 means disabled */

  GList *pending_pads;          /* Pads we have blocked pending assignment
                                   to an output source pad */
  GList *inactive_output_pads;  /* output pads that were unghosted */

  GList *buffering_status;      /* element currently buffering messages */
  gint last_buffering_pct;      /* Avoid sending buffering over and over */
  GMutex buffering_lock;
  GMutex buffering_post_lock;
};

struct _GstURISourceBinClass
{
  GstBinClass parent_class;

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
  /* signal fired when a autoplugged element that is not linked downstream
   * or exposed wants to query something */
    gboolean (*autoplug_query) (GstElement * element, GstPad * pad,
      GstQuery * query);

  /* emitted when all data is decoded */
  void (*drained) (GstElement * element);
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticCaps default_raw_caps = GST_STATIC_CAPS (DEFAULT_RAW_CAPS);

GST_DEBUG_CATEGORY_STATIC (gst_uri_source_bin_debug);
#define GST_CAT_DEFAULT gst_uri_source_bin_debug

/* signals */
enum
{
  SIGNAL_UNKNOWN_TYPE,
  SIGNAL_AUTOPLUG_CONTINUE,
  SIGNAL_AUTOPLUG_FACTORIES,
  SIGNAL_AUTOPLUG_SELECT,
  SIGNAL_AUTOPLUG_SORT,
  SIGNAL_AUTOPLUG_QUERY,
  SIGNAL_DRAINED,
  SIGNAL_SOURCE_SETUP,
  LAST_SIGNAL
};

/* properties */
#define DEFAULT_PROP_URI            NULL
#define DEFAULT_PROP_SOURCE         NULL
#define DEFAULT_CONNECTION_SPEED    0
#define DEFAULT_BUFFER_DURATION     -1
#define DEFAULT_BUFFER_SIZE         -1
#define DEFAULT_DOWNLOAD            FALSE
#define DEFAULT_USE_BUFFERING       TRUE
#define DEFAULT_RING_BUFFER_MAX_SIZE 0

#define DEFAULT_CAPS (gst_static_caps_get (&default_raw_caps))
enum
{
  PROP_0,
  PROP_URI,
  PROP_SOURCE,
  PROP_CONNECTION_SPEED,
  PROP_BUFFER_SIZE,
  PROP_BUFFER_DURATION,
  PROP_DOWNLOAD,
  PROP_USE_BUFFERING,
  PROP_RING_BUFFER_MAX_SIZE
};

static void post_missing_plugin_error (GstElement * dec,
    const gchar * element_name);

static guint gst_uri_source_bin_signals[LAST_SIGNAL] = { 0 };

GType gst_uri_source_bin_get_type (void);
#define gst_uri_source_bin_parent_class parent_class
G_DEFINE_TYPE (GstURISourceBin, gst_uri_source_bin, GST_TYPE_BIN);

static void gst_uri_source_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_uri_source_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_uri_source_bin_finalize (GObject * obj);

static void handle_message (GstBin * bin, GstMessage * msg);

static gboolean gst_uri_source_bin_query (GstElement * element,
    GstQuery * query);
static GstStateChangeReturn gst_uri_source_bin_change_state (GstElement *
    element, GstStateChange transition);

static void remove_demuxer (GstURISourceBin * bin);
static void expose_output_pad (GstURISourceBin * urisrc, GstPad * pad);
static OutputSlotInfo *get_output_slot (GstURISourceBin * urisrc,
    gboolean do_download, gboolean is_adaptive, GstCaps * caps);
static void free_output_slot (OutputSlotInfo * slot, GstURISourceBin * urisrc);
static void free_output_slot_async (GstURISourceBin * urisrc,
    OutputSlotInfo * slot);
static GstPad *create_output_pad (GstURISourceBin * urisrc, GstPad * pad);
static void remove_buffering_msgs (GstURISourceBin * bin, GstObject * src);

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

static gboolean
_gst_boolean_or_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gboolean myboolean;
  gboolean retboolean;

  myboolean = g_value_get_boolean (handler_return);
  retboolean = g_value_get_boolean (return_accu);

  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_boolean (return_accu, myboolean || retboolean);

  return TRUE;
}

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

  /* Call the next handler in the chain (if any) when the current callback
   * returns TRY. This makes it possible to register separate autoplug-select
   * handlers that implement different TRY/EXPOSE/SKIP strategies.
   */
  if (res == GST_AUTOPLUG_SELECT_TRY)
    return TRUE;

  return FALSE;
}

static gboolean
_gst_array_hasvalue_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gpointer array;

  array = g_value_get_boxed (handler_return);
  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_boxed (return_accu, array);

  if (array != NULL)
    return FALSE;

  return TRUE;
}

static gboolean
gst_uri_source_bin_autoplug_continue (GstElement * element, GstPad * pad,
    GstCaps * caps)
{
  /* by default we always continue */
  return TRUE;
}

/* Must be called with factories lock! */
static void
gst_uri_source_bin_update_factories_list (GstURISourceBin * dec)
{
  guint32 cookie;

  cookie = gst_registry_get_feature_list_cookie (gst_registry_get ());
  if (!dec->factories || dec->factories_cookie != cookie) {
    if (dec->factories)
      gst_plugin_feature_list_free (dec->factories);
    dec->factories =
        gst_element_factory_list_get_elements
        (GST_ELEMENT_FACTORY_TYPE_DECODABLE, GST_RANK_MARGINAL);
    dec->factories =
        g_list_sort (dec->factories, gst_playback_utils_compare_factories_func);
    dec->factories_cookie = cookie;
  }
}

static GValueArray *
gst_uri_source_bin_autoplug_factories (GstElement * element, GstPad * pad,
    GstCaps * caps)
{
  GList *list, *tmp;
  GValueArray *result;
  GstURISourceBin *dec = GST_URI_SOURCE_BIN_CAST (element);

  GST_DEBUG_OBJECT (element, "finding factories");

  /* return all compatible factories for caps */
  g_mutex_lock (&dec->factories_lock);
  gst_uri_source_bin_update_factories_list (dec);
  list =
      gst_element_factory_list_filter (dec->factories, caps, GST_PAD_SINK,
      gst_caps_is_fixed (caps));
  g_mutex_unlock (&dec->factories_lock);

  result = g_value_array_new (g_list_length (list));
  for (tmp = list; tmp; tmp = tmp->next) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY_CAST (tmp->data);
    GValue val = { 0, };

    g_value_init (&val, G_TYPE_OBJECT);
    g_value_set_object (&val, factory);
    g_value_array_append (result, &val);
    g_value_unset (&val);
  }
  gst_plugin_feature_list_free (list);

  GST_DEBUG_OBJECT (element, "autoplug-factories returns %p", result);

  return result;
}

static GValueArray *
gst_uri_source_bin_autoplug_sort (GstElement * element, GstPad * pad,
    GstCaps * caps, GValueArray * factories)
{
  return NULL;
}

static GstAutoplugSelectResult
gst_uri_source_bin_autoplug_select (GstElement * element, GstPad * pad,
    GstCaps * caps, GstElementFactory * factory)
{
  GST_DEBUG_OBJECT (element, "default autoplug-select returns TRY");

  /* Try factory. */
  return GST_AUTOPLUG_SELECT_TRY;
}

static gboolean
gst_uri_source_bin_autoplug_query (GstElement * element, GstPad * pad,
    GstQuery * query)
{
  /* No query handled here */
  return FALSE;
}

static void
gst_uri_source_bin_class_init (GstURISourceBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);

  gobject_class->set_property = gst_uri_source_bin_set_property;
  gobject_class->get_property = gst_uri_source_bin_get_property;
  gobject_class->finalize = gst_uri_source_bin_finalize;

  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI to decode",
          DEFAULT_PROP_URI, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SOURCE,
      g_param_spec_object ("source", "Source", "Source object used",
          GST_TYPE_ELEMENT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONNECTION_SPEED,
      g_param_spec_uint64 ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXUINT64 / 1000, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_int ("buffer-size", "Buffer size (bytes)",
          "Buffer size when buffering streams (-1 default value)",
          -1, G_MAXINT, DEFAULT_BUFFER_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BUFFER_DURATION,
      g_param_spec_int64 ("buffer-duration", "Buffer duration (ns)",
          "Buffer duration when buffering streams (-1 default value)",
          -1, G_MAXINT64, DEFAULT_BUFFER_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstURISourceBin::download:
   *
   * For certain media type, enable download buffering.
   */
  g_object_class_install_property (gobject_class, PROP_DOWNLOAD,
      g_param_spec_boolean ("download", "Download",
          "Attempt download buffering when buffering network streams",
          DEFAULT_DOWNLOAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstURISourceBin::use-buffering:
   *
   * Perform buffering using a queue2 element, and emit BUFFERING
   * messages based on low-/high-percent thresholds of streaming data,
   * such as adaptive-demuxer streams.
   *
   * When download buffering is activated and used for the current media
   * type, this property does nothing.
   *
   */
  g_object_class_install_property (gobject_class, PROP_USE_BUFFERING,
      g_param_spec_boolean ("use-buffering", "Use Buffering",
          "Perform buffering on demuxed/parsed media",
          DEFAULT_USE_BUFFERING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstURISourceBin::ring-buffer-max-size
   *
   * The maximum size of the ring buffer in kilobytes. If set to 0, the ring
   * buffer is disabled. Default is 0.
   *
   */
  g_object_class_install_property (gobject_class, PROP_RING_BUFFER_MAX_SIZE,
      g_param_spec_uint64 ("ring-buffer-max-size",
          "Max. ring buffer size (bytes)",
          "Max. amount of data in the ring buffer (bytes, 0 = ring buffer disabled)",
          0, G_MAXUINT, DEFAULT_RING_BUFFER_MAX_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstURISourceBin::unknown-type:
   * @bin: The urisourcebin.
   * @pad: the new pad containing caps that cannot be resolved to a 'final'.
   * stream type.
   * @caps: the #GstCaps of the pad that cannot be resolved.
   *
   * This signal is emitted when a pad for which there is no further possible
   * decoding is added to the urisourcebin.
   */
  gst_uri_source_bin_signals[SIGNAL_UNKNOWN_TYPE] =
      g_signal_new ("unknown-type", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstURISourceBinClass, unknown_type),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstURISourceBin::autoplug-continue:
   * @bin: The urisourcebin.
   * @pad: The #GstPad.
   * @caps: The #GstCaps found.
   *
   * This signal is emitted whenever urisourcebin finds a new stream. It is
   * emitted before looking for any elements that can handle that stream.
   *
   * >   Invocation of signal handlers stops after the first signal handler
   * >   returns #FALSE. Signal handlers are invoked in the order they were
   * >   connected in.
   *
   * Returns: #TRUE if you wish urisourcebin to look for elements that can
   * handle the given @caps. If #FALSE, those caps will be considered as
   * final and the pad will be exposed as such (see 'pad-added' signal of
   * #GstElement).
   */
  gst_uri_source_bin_signals[SIGNAL_AUTOPLUG_CONTINUE] =
      g_signal_new ("autoplug-continue", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstURISourceBinClass,
          autoplug_continue), _gst_boolean_accumulator, NULL,
      g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 2, GST_TYPE_PAD,
      GST_TYPE_CAPS);

  /**
   * GstURISourceBin::autoplug-factories:
   * @bin: The urisourcebin.
   * @pad: The #GstPad.
   * @caps: The #GstCaps found.
   *
   * This function is emitted when an array of possible factories for @caps on
   * @pad is needed. urisourcebin will by default return an array with all
   * compatible factories, sorted by rank.
   *
   * If this function returns NULL, @pad will be exposed as a final caps.
   *
   * If this function returns an empty array, the pad will be considered as
   * having an unhandled type media type.
   *
   * >   Only the signal handler that is connected first will ever by invoked.
   * >   Don't connect signal handlers with the #G_CONNECT_AFTER flag to this
   * >   signal, they will never be invoked!
   *
   * Returns: a #GValueArray* with a list of factories to try. The factories are
   * by default tried in the returned order or based on the index returned by
   * "autoplug-select".
   */
  gst_uri_source_bin_signals[SIGNAL_AUTOPLUG_FACTORIES] =
      g_signal_new ("autoplug-factories", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstURISourceBinClass,
          autoplug_factories), _gst_array_accumulator, NULL,
      g_cclosure_marshal_generic, G_TYPE_VALUE_ARRAY, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstURISourceBin::autoplug-sort:
   * @bin: The urisourcebin.
   * @pad: The #GstPad.
   * @caps: The #GstCaps.
   * @factories: A #GValueArray of possible #GstElementFactory to use.
   *
   * Once decodebin has found the possible #GstElementFactory objects to try
   * for @caps on @pad, this signal is emited. The purpose of the signal is for
   * the application to perform additional sorting or filtering on the element
   * factory array.
   *
   * The callee should copy and modify @factories or return #NULL if the
   * order should not change.
   *
   * >   Invocation of signal handlers stops after one signal handler has
   * >   returned something else than #NULL. Signal handlers are invoked in
   * >   the order they were connected in.
   * >   Don't connect signal handlers with the #G_CONNECT_AFTER flag to this
   * >   signal, they will never be invoked!
   *
   * Returns: A new sorted array of #GstElementFactory objects.
   *
   * Since: 0.10.33
   */
  gst_uri_source_bin_signals[SIGNAL_AUTOPLUG_SORT] =
      g_signal_new ("autoplug-sort", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstURISourceBinClass, autoplug_sort),
      _gst_array_hasvalue_accumulator, NULL,
      g_cclosure_marshal_generic, G_TYPE_VALUE_ARRAY, 3, GST_TYPE_PAD,
      GST_TYPE_CAPS, G_TYPE_VALUE_ARRAY | G_SIGNAL_TYPE_STATIC_SCOPE);

  /**
   * GstURISourceBin::autoplug-select:
   * @bin: The urisourcebin.
   * @pad: The #GstPad.
   * @caps: The #GstCaps.
   * @factory: A #GstElementFactory to use.
   *
   * This signal is emitted once urisourcebin has found all the possible
   * #GstElementFactory that can be used to handle the given @caps. For each of
   * those factories, this signal is emitted.
   *
   * The signal handler should return a #GST_TYPE_AUTOPLUG_SELECT_RESULT enum
   * value indicating what decodebin should do next.
   *
   * A value of #GST_AUTOPLUG_SELECT_TRY will try to autoplug an element from
   * @factory.
   *
   * A value of #GST_AUTOPLUG_SELECT_EXPOSE will expose @pad without plugging
   * any element to it.
   *
   * A value of #GST_AUTOPLUG_SELECT_SKIP will skip @factory and move to the
   * next factory.
   *
   * >   The signal handler will not be invoked if any of the previously
   * >   registered signal handlers (if any) return a value other than
   * >   GST_AUTOPLUG_SELECT_TRY. Which also means that if you return
   * >   GST_AUTOPLUG_SELECT_TRY from one signal handler, handlers that get
   * >   registered next (again, if any) can override that decision.
   *
   * Returns: a #GST_TYPE_AUTOPLUG_SELECT_RESULT that indicates the required
   * operation. The default handler will always return
   * #GST_AUTOPLUG_SELECT_TRY.
   */
  gst_uri_source_bin_signals[SIGNAL_AUTOPLUG_SELECT] =
      g_signal_new ("autoplug-select", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstURISourceBinClass,
          autoplug_select), _gst_select_accumulator, NULL,
      g_cclosure_marshal_generic,
      GST_TYPE_AUTOPLUG_SELECT_RESULT, 3, GST_TYPE_PAD, GST_TYPE_CAPS,
      GST_TYPE_ELEMENT_FACTORY);

  /**
   * GstDecodeBin::autoplug-query:
   * @bin: The decodebin.
   * @child: The child element doing the query
   * @pad: The #GstPad.
   * @query: The #GstQuery.
   *
   * This signal is emitted whenever an autoplugged element that is
   * not linked downstream yet and not exposed does a query. It can
   * be used to tell the element about the downstream supported caps
   * for example.
   *
   * Returns: #TRUE if the query was handled, #FALSE otherwise.
   */
  gst_uri_source_bin_signals[SIGNAL_AUTOPLUG_QUERY] =
      g_signal_new ("autoplug-query", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstURISourceBinClass, autoplug_query),
      _gst_boolean_or_accumulator, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 3, GST_TYPE_PAD, GST_TYPE_ELEMENT,
      GST_TYPE_QUERY | G_SIGNAL_TYPE_STATIC_SCOPE);

  /**
   * GstURISourceBin::drained:
   *
   * This signal is emitted when the data for the current uri is played.
   */
  gst_uri_source_bin_signals[SIGNAL_DRAINED] =
      g_signal_new ("drained", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstURISourceBinClass, drained), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstURISourceBin::source-setup:
   * @bin: the urisourcebin.
   * @source: source element
   *
   * This signal is emitted after the source element has been created, so
   * it can be configured by setting additional properties (e.g. set a
   * proxy server for an http source, or set the device and read speed for
   * an audio cd source). This is functionally equivalent to connecting to
   * the notify::source signal, but more convenient.
   *
   * Since: 1.6.1
   */
  gst_uri_source_bin_signals[SIGNAL_SOURCE_SETUP] =
      g_signal_new ("source-setup", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_static_metadata (gstelement_class,
      "URI reader", "Generic/Bin/Source",
      "Download and buffer a URI as needed",
      "Jan Schmidt <jan@centricular.com>");

  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_uri_source_bin_query);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_uri_source_bin_change_state);

  gstbin_class->handle_message = GST_DEBUG_FUNCPTR (handle_message);

  klass->autoplug_continue =
      GST_DEBUG_FUNCPTR (gst_uri_source_bin_autoplug_continue);
  klass->autoplug_factories =
      GST_DEBUG_FUNCPTR (gst_uri_source_bin_autoplug_factories);
  klass->autoplug_sort = GST_DEBUG_FUNCPTR (gst_uri_source_bin_autoplug_sort);
  klass->autoplug_select =
      GST_DEBUG_FUNCPTR (gst_uri_source_bin_autoplug_select);
  klass->autoplug_query = GST_DEBUG_FUNCPTR (gst_uri_source_bin_autoplug_query);
}

static void
gst_uri_source_bin_init (GstURISourceBin * urisrc)
{
  /* first filter out the interesting element factories */
  g_mutex_init (&urisrc->factories_lock);

  g_mutex_init (&urisrc->lock);

  g_mutex_init (&urisrc->buffering_lock);
  g_mutex_init (&urisrc->buffering_post_lock);

  urisrc->uri = g_strdup (DEFAULT_PROP_URI);
  urisrc->connection_speed = DEFAULT_CONNECTION_SPEED;

  urisrc->buffer_duration = DEFAULT_BUFFER_DURATION;
  urisrc->buffer_size = DEFAULT_BUFFER_SIZE;
  urisrc->download = DEFAULT_DOWNLOAD;
  urisrc->use_buffering = DEFAULT_USE_BUFFERING;
  urisrc->ring_buffer_max_size = DEFAULT_RING_BUFFER_MAX_SIZE;
  urisrc->last_buffering_pct = -1;

  GST_OBJECT_FLAG_SET (urisrc, GST_ELEMENT_FLAG_SOURCE);
  gst_bin_set_suppressed_flags (GST_BIN (urisrc),
      GST_ELEMENT_FLAG_SOURCE | GST_ELEMENT_FLAG_SINK);
}

static void
gst_uri_source_bin_finalize (GObject * obj)
{
  GstURISourceBin *urisrc = GST_URI_SOURCE_BIN (obj);

  remove_demuxer (urisrc);
  g_mutex_clear (&urisrc->lock);
  g_mutex_clear (&urisrc->factories_lock);
  g_free (urisrc->uri);
  if (urisrc->factories)
    gst_plugin_feature_list_free (urisrc->factories);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_uri_source_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstURISourceBin *dec = GST_URI_SOURCE_BIN (object);

  switch (prop_id) {
    case PROP_URI:
      GST_OBJECT_LOCK (dec);
      g_free (dec->uri);
      dec->uri = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (dec);
      break;
    case PROP_CONNECTION_SPEED:
      GST_OBJECT_LOCK (dec);
      dec->connection_speed = g_value_get_uint64 (value) * 1000;
      GST_OBJECT_UNLOCK (dec);
      break;
    case PROP_BUFFER_SIZE:
      dec->buffer_size = g_value_get_int (value);
      break;
    case PROP_BUFFER_DURATION:
      dec->buffer_duration = g_value_get_int64 (value);
      break;
    case PROP_DOWNLOAD:
      dec->download = g_value_get_boolean (value);
      break;
    case PROP_USE_BUFFERING:
      dec->use_buffering = g_value_get_boolean (value);
      break;
    case PROP_RING_BUFFER_MAX_SIZE:
      dec->ring_buffer_max_size = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_uri_source_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstURISourceBin *dec = GST_URI_SOURCE_BIN (object);

  switch (prop_id) {
    case PROP_URI:
      GST_OBJECT_LOCK (dec);
      g_value_set_string (value, dec->uri);
      GST_OBJECT_UNLOCK (dec);
      break;
    case PROP_SOURCE:
      GST_OBJECT_LOCK (dec);
      g_value_set_object (value, dec->source);
      GST_OBJECT_UNLOCK (dec);
      break;
    case PROP_CONNECTION_SPEED:
      GST_OBJECT_LOCK (dec);
      g_value_set_uint64 (value, dec->connection_speed / 1000);
      GST_OBJECT_UNLOCK (dec);
      break;
    case PROP_BUFFER_SIZE:
      GST_OBJECT_LOCK (dec);
      g_value_set_int (value, dec->buffer_size);
      GST_OBJECT_UNLOCK (dec);
      break;
    case PROP_BUFFER_DURATION:
      GST_OBJECT_LOCK (dec);
      g_value_set_int64 (value, dec->buffer_duration);
      GST_OBJECT_UNLOCK (dec);
      break;
    case PROP_DOWNLOAD:
      g_value_set_boolean (value, dec->download);
      break;
    case PROP_USE_BUFFERING:
      g_value_set_boolean (value, dec->use_buffering);
      break;
    case PROP_RING_BUFFER_MAX_SIZE:
      g_value_set_uint64 (value, dec->ring_buffer_max_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
do_async_start (GstURISourceBin * dbin)
{
  GstMessage *message;

  dbin->async_pending = TRUE;

  message = gst_message_new_async_start (GST_OBJECT_CAST (dbin));
  GST_BIN_CLASS (parent_class)->handle_message (GST_BIN_CAST (dbin), message);
}

static void
do_async_done (GstURISourceBin * dbin)
{
  GstMessage *message;

  if (dbin->async_pending) {
    GST_DEBUG_OBJECT (dbin, "posting ASYNC_DONE");
    message =
        gst_message_new_async_done (GST_OBJECT_CAST (dbin),
        GST_CLOCK_TIME_NONE);
    GST_BIN_CLASS (parent_class)->handle_message (GST_BIN_CAST (dbin), message);

    dbin->async_pending = FALSE;
  }
}

#define DEFAULT_QUEUE_SIZE          (3 * GST_SECOND)
#define DEFAULT_QUEUE_MIN_THRESHOLD ((DEFAULT_QUEUE_SIZE * 30) / 100)
#define DEFAULT_QUEUE_THRESHOLD     ((DEFAULT_QUEUE_SIZE * 95) / 100)

static gboolean
copy_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstPad *gpad = GST_PAD_CAST (user_data);

  GST_DEBUG_OBJECT (gpad, "store sticky event %" GST_PTR_FORMAT, *event);
  gst_pad_store_sticky_event (gpad, *event);

  return TRUE;
}

static GstPadProbeReturn
pending_pad_blocked (GstPad * pad, GstPadProbeInfo * info, gpointer user_data);

static GstPadProbeReturn
demux_pad_events (GstPad * pad, GstPadProbeInfo * info, gpointer user_data);

static void
free_child_src_pad_info (ChildSrcPadInfo * info)
{
  if (info->cur_caps)
    gst_caps_unref (info->cur_caps);
  g_free (info);
}

/* Called by the signal handlers when a demuxer has produced a new stream */
static void
new_demuxer_pad_added_cb (GstElement * element, GstPad * pad,
    GstURISourceBin * urisrc)
{
  ChildSrcPadInfo *info;

  info = g_new0 (ChildSrcPadInfo, 1);
  info->demux_src_pad = pad;
  info->cur_caps = gst_pad_get_current_caps (pad);
  if (info->cur_caps == NULL)
    info->cur_caps = gst_pad_query_caps (pad, NULL);

  g_object_set_data_full (G_OBJECT (pad), "urisourcebin.srcpadinfo",
      info, (GDestroyNotify) free_child_src_pad_info);

  GST_DEBUG_OBJECT (element, "new demuxer pad, name: <%s>. "
      "Added as pending pad with caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (pad), info->cur_caps);

  GST_URI_SOURCE_BIN_LOCK (urisrc);
  urisrc->pending_pads = g_list_prepend (urisrc->pending_pads, pad);
  GST_URI_SOURCE_BIN_UNLOCK (urisrc);

  /* Block the pad. On the first data on that pad if it hasn't
   * been linked to an output slot, we'll create one */
  info->blocking_probe_id =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      pending_pad_blocked, urisrc, NULL);
  info->event_probe_id =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
      GST_PAD_PROBE_TYPE_EVENT_FLUSH, demux_pad_events, urisrc, NULL);
}

static GstPadProbeReturn
pending_pad_blocked (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  ChildSrcPadInfo *child_info;
  OutputSlotInfo *slot;
  GstURISourceBin *urisrc = GST_URI_SOURCE_BIN (user_data);
  GstCaps *caps;

  if (!(child_info =
          g_object_get_data (G_OBJECT (pad), "urisourcebin.srcpadinfo")))
    goto done;

  GST_LOG_OBJECT (urisrc, "Removing pad %" GST_PTR_FORMAT " from pending list",
      pad);

  GST_URI_SOURCE_BIN_LOCK (urisrc);

  /* Once blocked, this pad is no longer pending, one way or another */
  urisrc->pending_pads = g_list_remove (urisrc->pending_pads, pad);

  /* If already linked to a slot, nothing more to do */
  if (child_info->output_slot) {
    GST_LOG_OBJECT (urisrc, "Pad %" GST_PTR_FORMAT " is linked to queue %"
        GST_PTR_FORMAT " on slot %p", pad, child_info->output_slot->queue,
        child_info->output_slot);
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
    goto done;
  }

  caps = gst_pad_get_current_caps (pad);
  if (caps == NULL)
    caps = gst_pad_query_caps (pad, NULL);

  slot = get_output_slot (urisrc, FALSE, TRUE, caps);

  gst_caps_unref (caps);

  if (slot == NULL) {
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
    goto done;
  }

  GST_LOG_OBJECT (urisrc, "Pad %" GST_PTR_FORMAT " linked to slot %p", pad,
      slot);

  child_info->output_slot = slot;
  slot->linked_info = child_info;
  GST_URI_SOURCE_BIN_UNLOCK (urisrc);

  gst_pad_link (pad, slot->sinkpad);

  expose_output_pad (urisrc, slot->srcpad);

done:
  return GST_PAD_PROBE_REMOVE;
}

/* Called with LOCK held */
/* Looks for a suitable pending pad to connect onto this
 * finishing output slot that's about to EOS */
static gboolean
link_pending_pad_to_output (GstURISourceBin * urisrc, OutputSlotInfo * slot)
{
  GList *cur;
  ChildSrcPadInfo *in_info = slot->linked_info;
  ChildSrcPadInfo *out_info = NULL;
  gboolean res = FALSE;
  GstCaps *cur_caps;

  /* Look for a suitable pending pad */
  cur_caps = gst_pad_get_current_caps (slot->sinkpad);

  GST_DEBUG_OBJECT (urisrc,
      "Looking for a pending pad with caps %" GST_PTR_FORMAT, cur_caps);

  for (cur = urisrc->pending_pads; cur != NULL; cur = g_list_next (cur)) {
    GstPad *pending = (GstPad *) (cur->data);
    ChildSrcPadInfo *cur_info = NULL;
    if ((cur_info =
            g_object_get_data (G_OBJECT (pending),
                "urisourcebin.srcpadinfo"))) {
      /* Don't re-link to the same pad in case of EOS while still pending */
      if (in_info == cur_info)
        continue;
      if (cur_caps == NULL || gst_caps_is_equal (cur_caps, cur_info->cur_caps)) {
        GST_DEBUG_OBJECT (urisrc, "Found suitable pending pad %" GST_PTR_FORMAT
            " with caps %" GST_PTR_FORMAT " to link to this output slot",
            cur_info->demux_src_pad, cur_info->cur_caps);
        out_info = cur_info;
        break;
      }
    }
  }

  if (cur_caps)
    gst_caps_unref (cur_caps);

  if (out_info) {
    /* Block any upstream stuff while we switch out the pad */
    guint block_id =
        gst_pad_add_probe (slot->sinkpad, GST_PAD_PROBE_TYPE_BLOCK_UPSTREAM,
        NULL, NULL, NULL);
    GST_DEBUG_OBJECT (urisrc, "Linking pending pad %" GST_PTR_FORMAT
        " to existing output slot %p", out_info->demux_src_pad, slot);

    if (in_info) {
      gst_pad_unlink (in_info->demux_src_pad, slot->sinkpad);
      in_info->output_slot = NULL;
      slot->linked_info = NULL;
    }

    if (gst_pad_link (out_info->demux_src_pad,
            slot->sinkpad) == GST_PAD_LINK_OK) {
      out_info->output_slot = slot;
      slot->linked_info = out_info;

      BUFFERING_LOCK (urisrc);
      /* A re-linked slot is no longer EOS */
      slot->is_eos = FALSE;
      BUFFERING_UNLOCK (urisrc);
      res = TRUE;
      urisrc->pending_pads =
          g_list_remove (urisrc->pending_pads, out_info->demux_src_pad);
    } else {
      GST_ERROR_OBJECT (urisrc,
          "Failed to link new demuxer pad to the output slot we tried");
    }
    gst_pad_remove_probe (slot->sinkpad, block_id);
  }

  return res;
}

static GstPadProbeReturn
demux_pad_events (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstURISourceBin *urisrc = GST_URI_SOURCE_BIN (user_data);
  ChildSrcPadInfo *child_info;
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);

  if (!(child_info =
          g_object_get_data (G_OBJECT (pad), "urisourcebin.srcpadinfo")))
    goto done;

  GST_URI_SOURCE_BIN_LOCK (urisrc);
  /* If not linked to a slot, nothing more to do */
  if (child_info->output_slot == NULL) {
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
    goto done;
  }

  switch (GST_EVENT_TYPE (ev)) {
    case GST_EVENT_EOS:
    {
      GstEvent *event;
      GstStructure *s;
      guint32 seqnum = gst_event_get_seqnum (ev);

      GST_LOG_OBJECT (urisrc, "EOS on pad %" GST_PTR_FORMAT, pad);

      /* never forward actual EOS to slot */
      ret = GST_PAD_PROBE_DROP;

      if ((urisrc->pending_pads &&
              link_pending_pad_to_output (urisrc, child_info->output_slot))) {
        /* Found a new source pad to give this slot data - no need to send EOS */
        GST_URI_SOURCE_BIN_UNLOCK (urisrc);
        goto done;
      }

      BUFFERING_LOCK (urisrc);
      /* Mark that we fed an EOS to this slot */
      child_info->output_slot->is_eos = TRUE;
      BUFFERING_UNLOCK (urisrc);

      /* EOS means this element is no longer buffering */
      remove_buffering_msgs (urisrc,
          GST_OBJECT_CAST (child_info->output_slot->queue));

      /* Actually feed a custom EOS event to avoid marking pads as EOSed */
      s = gst_structure_new_empty ("urisourcebin-custom-eos");
      event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);
      gst_event_set_seqnum (event, seqnum);
      gst_pad_send_event (child_info->output_slot->sinkpad, event);
    }
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gst_event_parse_caps (ev, &caps);
      gst_caps_replace (&child_info->cur_caps, caps);
    }
      break;
    case GST_EVENT_STREAM_START:
    case GST_EVENT_FLUSH_STOP:
      BUFFERING_LOCK (urisrc);
      child_info->output_slot->is_eos = FALSE;
      BUFFERING_UNLOCK (urisrc);
      break;
    default:
      break;
  }

  GST_URI_SOURCE_BIN_UNLOCK (urisrc);

done:
  return ret;
}

/* Called with lock held */
static OutputSlotInfo *
get_output_slot (GstURISourceBin * urisrc, gboolean do_download,
    gboolean is_adaptive, GstCaps * caps)
{
  OutputSlotInfo *slot;
  GstPad *srcpad;
  GstElement *queue;
  const gchar *elem_name;

  /* If we have caps, iterate the existing slots and look for an
   * unlinked one that can be used */
  if (caps && gst_caps_is_fixed (caps)) {
    GSList *cur;
    GstCaps *cur_caps;

    for (cur = urisrc->out_slots; cur != NULL; cur = g_slist_next (cur)) {
      slot = (OutputSlotInfo *) (cur->data);
      if (slot->linked_info == NULL) {
        cur_caps = gst_pad_get_current_caps (slot->sinkpad);
        if (cur_caps == NULL || gst_caps_is_equal (caps, cur_caps)) {
          GST_LOG_OBJECT (urisrc, "Found existing slot %p to link to", slot);
          gst_caps_unref (cur_caps);
          return slot;
        }
        gst_caps_unref (cur_caps);
      }
    }
  }

  /* Otherwise create the new slot */
#if 0                           /* There's no downloadbuffer in 1.2 */
  if (do_download)
    elem_name = "downloadbuffer";
  else
#endif
    elem_name = "queue2";

  queue = gst_element_factory_make (elem_name, NULL);
  if (!queue)
    goto no_buffer_element;

  slot = g_new0 (OutputSlotInfo, 1);
  slot->queue = queue;

  /* Set the slot onto the queue (needed in buffering msg handling) */
  g_object_set_data (G_OBJECT (queue), "urisourcebin.slotinfo", slot);

  if (do_download) {
    gchar *temp_template, *filename;
    const gchar *tmp_dir, *prgname;

    tmp_dir = g_get_user_cache_dir ();
    prgname = g_get_prgname ();
    if (prgname == NULL)
      prgname = "GStreamer";

    filename = g_strdup_printf ("%s-XXXXXX", prgname);

    /* build our filename */
    temp_template = g_build_filename (tmp_dir, filename, NULL);

    GST_DEBUG_OBJECT (urisrc, "enable download buffering in %s (%s, %s, %s)",
        temp_template, tmp_dir, prgname, filename);

    /* configure progressive download for selected media types */
    g_object_set (queue, "temp-template", temp_template, NULL);

    g_free (filename);
    g_free (temp_template);
  } else {
    if (is_adaptive) {
      GST_LOG_OBJECT (urisrc, "Adding queue for adaptive streaming stream");
      g_object_set (queue, "use-buffering", urisrc->use_buffering,
          "use-tags-bitrate", TRUE, "use-rate-estimate", FALSE, NULL);
    } else {
      GST_LOG_OBJECT (urisrc, "Adding queue for buffering");
      g_object_set (queue, "use-buffering", urisrc->use_buffering, NULL);
    }
    g_object_set (queue, "ring-buffer-max-size",
        urisrc->ring_buffer_max_size, NULL);
    /* Disable max-size-buffers - queue based on data rate to the default time limit */
    g_object_set (queue, "max-size-buffers", 0, NULL);
  }

  /* If buffer size or duration are set, set them on the element */
  if (urisrc->buffer_size != -1)
    g_object_set (queue, "max-size-bytes", urisrc->buffer_size, NULL);
  if (urisrc->buffer_duration != -1)
    g_object_set (queue, "max-size-time", urisrc->buffer_duration, NULL);
#if 0
  /* Disabled because this makes initial startup slower for radio streams */
  else {
    /* Buffer 4 seconds by default - some extra headroom over the
     * core default, because we trigger playback sooner */
    //g_object_set (queue, "max-size-time", 4 * GST_SECOND, NULL);
  }
#endif

  /* Don't start buffering until the queue is empty (< 1%).
   * Start playback when the queue is 60% full, leaving a bit more room
   * for upstream to push more without getting bursty */
  g_object_set (queue, "low-percent", 1, "high-percent", 60, NULL);

  /* save queue pointer so we can remove it later */
  urisrc->out_slots = g_slist_prepend (urisrc->out_slots, slot);

  gst_bin_add (GST_BIN_CAST (urisrc), queue);
  gst_element_sync_state_with_parent (queue);

  slot->sinkpad = gst_element_get_static_pad (queue, "sink");

  /* get the new raw srcpad */
  srcpad = gst_element_get_static_pad (queue, "src");
  g_object_set_data (G_OBJECT (srcpad), "urisourcebin.slotinfo", slot);

  slot->srcpad = create_output_pad (urisrc, srcpad);

  gst_object_unref (srcpad);

  return slot;

no_buffer_element:
  {
    post_missing_plugin_error (GST_ELEMENT_CAST (urisrc), elem_name);
    return NULL;
  }
}

static GstPadProbeReturn
source_pad_event_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstURISourceBin *urisrc = user_data;

  GST_LOG_OBJECT (pad, "%s, urisrc %p", GST_EVENT_TYPE_NAME (event), event);

  if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM &&
      gst_event_has_name (event, "urisourcebin-custom-eos")) {
    OutputSlotInfo *slot;
    GST_DEBUG_OBJECT (pad, "we received EOS");

    GST_URI_SOURCE_BIN_LOCK (urisrc);

    slot = g_object_get_data (G_OBJECT (pad), "urisourcebin.slotinfo");

    if (slot) {
      GstEvent *eos;
      guint32 seqnum;

      if (slot->linked_info) {
        /* Do not clear output slot yet. A new input was
         * connected. We should just drop this EOS */
        GST_URI_SOURCE_BIN_UNLOCK (urisrc);
        return GST_PAD_PROBE_DROP;
      }

      seqnum = gst_event_get_seqnum (event);
      eos = gst_event_new_eos ();
      gst_event_set_seqnum (eos, seqnum);
      gst_pad_push_event (slot->srcpad, eos);
      free_output_slot_async (urisrc, slot);
    }

    /* FIXME: Only emit drained if all output pads are done and there's no
     * pending pads */
    g_signal_emit (urisrc, gst_uri_source_bin_signals[SIGNAL_DRAINED], 0, NULL);

    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
    return GST_PAD_PROBE_DROP;
  }
  /* never drop events */
  return GST_PAD_PROBE_OK;
}

/* called when we found a raw pad to expose. We set up a
 * padprobe to detect EOS before exposing the pad.
 * Called with LOCK held. */
static GstPad *
create_output_pad (GstURISourceBin * urisrc, GstPad * pad)
{
  GstPad *newpad;
  GstPadTemplate *pad_tmpl;
  gchar *padname;

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      source_pad_event_probe, urisrc, NULL);

  pad_tmpl = gst_static_pad_template_get (&srctemplate);

  padname = g_strdup_printf ("src_%u", urisrc->numpads);
  urisrc->numpads++;

  newpad = gst_ghost_pad_new_from_template (padname, pad, pad_tmpl);
  gst_object_unref (pad_tmpl);
  g_free (padname);

  return newpad;
}

static void
expose_output_pad (GstURISourceBin * urisrc, GstPad * pad)
{
  GstPad *target;

  if (gst_object_has_as_parent (GST_OBJECT (pad), GST_OBJECT (urisrc)))
    return;                     /* Pad is already exposed */

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));

  gst_pad_sticky_events_foreach (target, copy_sticky_events, pad);
  gst_object_unref (target);

  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (urisrc), pad);

  /* Once we expose a pad, we're no longer async */
  do_async_done (urisrc);
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, GstURISourceBin * urisrc)
{
  ChildSrcPadInfo *info;

  GST_DEBUG_OBJECT (element, "pad removed name: <%s:%s>",
      GST_DEBUG_PAD_NAME (pad));

  /* we only care about srcpads */
  if (!GST_PAD_IS_SRC (pad))
    return;

  if (!(info = g_object_get_data (G_OBJECT (pad), "urisourcebin.srcpadinfo")))
    goto no_info;

  GST_URI_SOURCE_BIN_LOCK (urisrc);
  /* Make sure this isn't in the pending pads list */
  urisrc->pending_pads = g_list_remove (urisrc->pending_pads, pad);

  /* Send EOS to the output slot if the demuxer didn't already */
  if (info->output_slot) {
    GstStructure *s;
    GstEvent *event;
    OutputSlotInfo *slot;

    slot = info->output_slot;

    if (!slot->is_eos && urisrc->pending_pads &&
        link_pending_pad_to_output (urisrc, slot)) {
      /* Found a new source pad to give this slot data - no need to send EOS */
      GST_URI_SOURCE_BIN_UNLOCK (urisrc);
      return;
    }

    BUFFERING_LOCK (urisrc);
    /* Unlink this pad from its output slot and send a fake EOS event
     * to drain the queue */
    slot->is_eos = TRUE;
    BUFFERING_UNLOCK (urisrc);

    remove_buffering_msgs (urisrc, GST_OBJECT_CAST (slot->queue));

    slot->linked_info = NULL;

    info->output_slot = NULL;

    GST_LOG_OBJECT (element,
        "Pad %" GST_PTR_FORMAT " was removed without EOS. Sending.", pad);

    s = gst_structure_new_empty ("urisourcebin-custom-eos");
    event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);
    gst_pad_send_event (slot->sinkpad, event);
  } else {
    GST_LOG_OBJECT (urisrc, "Removed pad has no output slot");
  }
  GST_URI_SOURCE_BIN_UNLOCK (urisrc);

  return;

  /* ERRORS */
no_info:
  {
    GST_WARNING_OBJECT (element, "no info found for pad");
    return;
  }
}

/* helper function to lookup stuff in lists */
static gboolean
array_has_value (const gchar * values[], const gchar * value)
{
  gint i;

  for (i = 0; values[i]; i++) {
    if (g_str_has_prefix (value, values[i]))
      return TRUE;
  }
  return FALSE;
}

static gboolean
array_has_uri_value (const gchar * values[], const gchar * value)
{
  gint i;

  for (i = 0; values[i]; i++) {
    if (!g_ascii_strncasecmp (value, values[i], strlen (values[i])))
      return TRUE;
  }
  return FALSE;
}

/* list of URIs that we consider to be streams and that need buffering.
 * We have no mechanism yet to figure this out with a query. */
static const gchar *stream_uris[] = { "http://", "https://", "mms://",
  "mmsh://", "mmsu://", "mmst://", "fd://", "myth://", "ssh://",
  "ftp://", "sftp://",
  NULL
};

/* list of URIs that need a queue because they are pretty bursty */
static const gchar *queue_uris[] = { "cdda://", NULL };

/* blacklisted URIs, we know they will always fail. */
static const gchar *blacklisted_uris[] = { NULL };

/* media types that use adaptive streaming */
static const gchar *adaptive_media[] = {
  "application/x-hls", "application/vnd.ms-sstr+xml",
  "application/dash+xml", NULL
};

#define IS_STREAM_URI(uri)          (array_has_uri_value (stream_uris, uri))
#define IS_QUEUE_URI(uri)           (array_has_uri_value (queue_uris, uri))
#define IS_BLACKLISTED_URI(uri)     (array_has_uri_value (blacklisted_uris, uri))
#define IS_ADAPTIVE_MEDIA(media)    (array_has_value (adaptive_media, media))

/*
 * Generate and configure a source element.
 */
static GstElement *
gen_source_element (GstURISourceBin * urisrc)
{
  GObjectClass *source_class;
  GstElement *source;
  GParamSpec *pspec;
  GstQuery *query;
  GstSchedulingFlags flags;
  GError *err = NULL;

  if (!urisrc->uri)
    goto no_uri;

  GST_LOG_OBJECT (urisrc, "finding source for %s", urisrc->uri);

  if (!gst_uri_is_valid (urisrc->uri))
    goto invalid_uri;

  if (IS_BLACKLISTED_URI (urisrc->uri))
    goto uri_blacklisted;

  source = gst_element_make_from_uri (GST_URI_SRC, urisrc->uri, "source", &err);
  if (!source)
    goto no_source;

  GST_LOG_OBJECT (urisrc, "found source type %s", G_OBJECT_TYPE_NAME (source));

  query = gst_query_new_scheduling ();
  if (gst_element_query (source, query)) {
    gst_query_parse_scheduling (query, &flags, NULL, NULL, NULL);
    urisrc->is_stream = flags & GST_SCHEDULING_FLAG_BANDWIDTH_LIMITED;
  } else
    urisrc->is_stream = IS_STREAM_URI (urisrc->uri);
  gst_query_unref (query);

  GST_LOG_OBJECT (urisrc, "source is stream: %d", urisrc->is_stream);

  urisrc->need_queue = IS_QUEUE_URI (urisrc->uri);
  GST_LOG_OBJECT (urisrc, "source needs queue: %d", urisrc->need_queue);

  source_class = G_OBJECT_GET_CLASS (source);

  pspec = g_object_class_find_property (source_class, "connection-speed");
  if (pspec != NULL) {
    guint64 speed = urisrc->connection_speed / 1000;
    gboolean wrong_type = FALSE;

    if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_UINT) {
      GParamSpecUInt *pspecuint = G_PARAM_SPEC_UINT (pspec);

      speed = CLAMP (speed, pspecuint->minimum, pspecuint->maximum);
    } else if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_INT) {
      GParamSpecInt *pspecint = G_PARAM_SPEC_INT (pspec);

      speed = CLAMP (speed, pspecint->minimum, pspecint->maximum);
    } else if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_UINT64) {
      GParamSpecUInt64 *pspecuint = G_PARAM_SPEC_UINT64 (pspec);

      speed = CLAMP (speed, pspecuint->minimum, pspecuint->maximum);
    } else if (G_PARAM_SPEC_TYPE (pspec) == G_TYPE_PARAM_INT64) {
      GParamSpecInt64 *pspecint = G_PARAM_SPEC_INT64 (pspec);

      speed = CLAMP (speed, pspecint->minimum, pspecint->maximum);
    } else {
      GST_WARNING_OBJECT (urisrc,
          "The connection speed property %" G_GUINT64_FORMAT
          " of type %s is not useful. Not setting it", speed,
          g_type_name (G_PARAM_SPEC_TYPE (pspec)));
      wrong_type = TRUE;
    }

    if (!wrong_type) {
      g_object_set (source, "connection-speed", speed, NULL);

      GST_DEBUG_OBJECT (urisrc,
          "setting connection-speed=%" G_GUINT64_FORMAT " to source element",
          speed);
    }
  }

  return source;

  /* ERRORS */
no_uri:
  {
    GST_ELEMENT_ERROR (urisrc, RESOURCE, NOT_FOUND,
        (_("No URI specified to play from.")), (NULL));
    return NULL;
  }
invalid_uri:
  {
    GST_ELEMENT_ERROR (urisrc, RESOURCE, NOT_FOUND,
        (_("Invalid URI \"%s\"."), urisrc->uri), (NULL));
    g_clear_error (&err);
    return NULL;
  }
uri_blacklisted:
  {
    GST_ELEMENT_ERROR (urisrc, RESOURCE, FAILED,
        (_("This stream type cannot be played yet.")), (NULL));
    return NULL;
  }
no_source:
  {
    /* whoops, could not create the source element, dig a little deeper to
     * figure out what might be wrong. */
    if (err != NULL && err->code == GST_URI_ERROR_UNSUPPORTED_PROTOCOL) {
      gchar *prot;

      prot = gst_uri_get_protocol (urisrc->uri);
      if (prot == NULL)
        goto invalid_uri;

      gst_element_post_message (GST_ELEMENT_CAST (urisrc),
          gst_missing_uri_source_message_new (GST_ELEMENT (urisrc), prot));

      GST_ELEMENT_ERROR (urisrc, CORE, MISSING_PLUGIN,
          (_("No URI handler implemented for \"%s\"."), prot), (NULL));

      g_free (prot);
    } else {
      GST_ELEMENT_ERROR (urisrc, RESOURCE, NOT_FOUND,
          ("%s", (err) ? err->message : "URI was not accepted by any element"),
          ("No element accepted URI '%s'", urisrc->uri));
    }

    g_clear_error (&err);
    return NULL;
  }
}

static gboolean
is_all_raw_caps (GstCaps * caps, GstCaps * rawcaps, gboolean * all_raw)
{
  GstCaps *intersection;
  gint capssize;
  gboolean res = FALSE;

  if (caps == NULL)
    return FALSE;

  capssize = gst_caps_get_size (caps);
  /* no caps, skip and move to the next pad */
  if (capssize == 0 || gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    goto done;

  intersection = gst_caps_intersect (caps, rawcaps);
  *all_raw = !gst_caps_is_empty (intersection)
      && (gst_caps_get_size (intersection) == capssize);
  gst_caps_unref (intersection);

  res = TRUE;

done:
  return res;
}

/**
 * has_all_raw_caps:
 * @pad: a #GstPad
 * @all_raw: pointer to hold the result
 *
 * check if the caps of the pad are all raw. The caps are all raw if
 * all of its structures contain audio/x-raw or video/x-raw.
 *
 * Returns: %FALSE @pad has no caps. Else TRUE and @all_raw set t the result.
 */
static gboolean
has_all_raw_caps (GstPad * pad, GstCaps * rawcaps, gboolean * all_raw)
{
  GstCaps *caps;
  gboolean res = FALSE;

  caps = gst_pad_query_caps (pad, NULL);

  GST_DEBUG_OBJECT (pad, "have caps %" GST_PTR_FORMAT, caps);

  res = is_all_raw_caps (caps, rawcaps, all_raw);

  gst_caps_unref (caps);
  return res;
}

static void
post_missing_plugin_error (GstElement * dec, const gchar * element_name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (dec, element_name);
  gst_element_post_message (dec, msg);

  GST_ELEMENT_ERROR (dec, CORE, MISSING_PLUGIN,
      (_("Missing element '%s' - check your GStreamer installation."),
          element_name), (NULL));
  do_async_done (GST_URI_SOURCE_BIN (dec));
}

/**
 * analyse_source:
 * @urisrc: a #GstURISourceBin
 * @is_raw: are all pads raw data
 * @have_out: does the source have output
 * @is_dynamic: is this a dynamic source
 * @use_queue: put a queue before raw output pads
 *
 * Check the source of @urisrc and collect information about it.
 *
 * @is_raw will be set to TRUE if the source only produces raw pads. When this
 * function returns, all of the raw pad of the source will be added
 * to @urisrc
 *
 * @have_out: will be set to TRUE if the source has output pads.
 *
 * @is_dynamic: TRUE if the element will create (more) pads dynamically later
 * on.
 *
 * Returns: FALSE if a fatal error occured while scanning.
 */
static gboolean
analyse_source (GstURISourceBin * urisrc, gboolean * is_raw,
    gboolean * have_out, gboolean * is_dynamic, gboolean use_queue)
{
  GstIterator *pads_iter;
  gboolean done = FALSE;
  gboolean res = TRUE;
  GstPad *pad;
  GValue item = { 0, };
  GstCaps *rawcaps = DEFAULT_CAPS;

  *have_out = FALSE;
  *is_raw = FALSE;
  *is_dynamic = FALSE;

  pads_iter = gst_element_iterate_src_pads (urisrc->source);
  while (!done) {
    switch (gst_iterator_next (pads_iter, &item)) {
      case GST_ITERATOR_ERROR:
        res = FALSE;
        /* FALLTROUGH */
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        /* reset results and resync */
        *have_out = FALSE;
        *is_raw = FALSE;
        *is_dynamic = FALSE;
        gst_iterator_resync (pads_iter);
        break;
      case GST_ITERATOR_OK:
        pad = g_value_dup_object (&item);
        /* we now officially have an ouput pad */
        *have_out = TRUE;

        /* if FALSE, this pad has no caps and we continue with the next pad. */
        if (!has_all_raw_caps (pad, rawcaps, is_raw)) {
          gst_object_unref (pad);
          g_value_reset (&item);
          break;
        }

        /* caps on source pad are all raw, we can add the pad */
        if (*is_raw) {
          GST_URI_SOURCE_BIN_LOCK (urisrc);
          if (use_queue) {
            OutputSlotInfo *slot = get_output_slot (urisrc, FALSE, FALSE, NULL);
            if (!slot)
              goto no_slot;

            gst_pad_link (pad, slot->sinkpad);

            /* get the new raw srcpad */
            gst_object_unref (pad);
            pad = slot->srcpad;
          } else {
            pad = create_output_pad (urisrc, pad);
          }
          GST_URI_SOURCE_BIN_UNLOCK (urisrc);
          expose_output_pad (urisrc, pad);
          gst_object_unref (pad);
        }
        gst_object_unref (pad);
        g_value_reset (&item);
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (pads_iter);
  gst_caps_unref (rawcaps);

  if (!*have_out) {
    GstElementClass *elemclass;
    GList *walk;

    /* element has no output pads, check for padtemplates that list SOMETIMES
     * pads. */
    elemclass = GST_ELEMENT_GET_CLASS (urisrc->source);

    walk = gst_element_class_get_pad_template_list (elemclass);
    while (walk != NULL) {
      GstPadTemplate *templ;

      templ = (GstPadTemplate *) walk->data;
      if (GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC) {
        if (GST_PAD_TEMPLATE_PRESENCE (templ) == GST_PAD_SOMETIMES)
          *is_dynamic = TRUE;
        break;
      }
      walk = g_list_next (walk);
    }
  }

  return res;
no_slot:
  {
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
    gst_object_unref (pad);
    g_value_unset (&item);
    gst_iterator_free (pads_iter);
    gst_caps_unref (rawcaps);

    return FALSE;
  }
}

/* Remove any adaptive demuxer element */
static void
remove_demuxer (GstURISourceBin * bin)
{
  if (bin->demuxer) {
    GST_DEBUG_OBJECT (bin, "removing old demuxer element");
    gst_element_set_state (bin->demuxer, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (bin), bin->demuxer);
    bin->demuxer = NULL;
  }
}

/* make a demuxer and connect to all the signals */
static GstElement *
make_demuxer (GstURISourceBin * urisrc, GstCaps * caps)
{
  GList *factories, *eligible, *cur;
  GstElement *demuxer = NULL;
  GParamSpec *pspec;

  GST_LOG_OBJECT (urisrc, "making new adaptive demuxer");

  /* now create the demuxer element */

  /* FIXME: Fire a signal to get the demuxer? */
  factories = gst_element_factory_list_get_elements
      (GST_ELEMENT_FACTORY_TYPE_DEMUXER, GST_RANK_MARGINAL);
  eligible =
      gst_element_factory_list_filter (factories, caps, GST_PAD_SINK,
      gst_caps_is_fixed (caps));
  gst_plugin_feature_list_free (factories);

  if (eligible == NULL)
    goto no_demuxer;

  eligible = g_list_sort (eligible, gst_plugin_feature_rank_compare_func);

  for (cur = eligible; cur != NULL; cur = g_list_next (cur)) {
    GstElementFactory *factory = (GstElementFactory *) (cur->data);
    const gchar *klass =
        gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);

    /* Can't be a demuxer unless it has Demux in the klass name */
    if (!strstr (klass, "Demux") || !strstr (klass, "Adaptive"))
      continue;

    demuxer = gst_element_factory_create (factory, NULL);
    break;
  }
  gst_plugin_feature_list_free (eligible);

  if (!demuxer)
    goto no_demuxer;

  GST_DEBUG_OBJECT (urisrc, "Created adaptive demuxer %" GST_PTR_FORMAT,
      demuxer);

  /* set up callbacks to create the links between
   * demuxer streams and output */
  g_signal_connect (demuxer,
      "pad-added", G_CALLBACK (new_demuxer_pad_added_cb), urisrc);
  g_signal_connect (demuxer,
      "pad-removed", G_CALLBACK (pad_removed_cb), urisrc);

  /* Propagate connection-speed property */
  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (demuxer),
      "connection-speed");
  if (pspec != NULL)
    g_object_set (demuxer,
        "connection-speed", urisrc->connection_speed / 1000, NULL);

  return demuxer;

  /* ERRORS */
no_demuxer:
  {
    /* FIXME: Fire the right error */
    GST_ELEMENT_ERROR (urisrc, CORE, MISSING_PLUGIN, (NULL),
        ("No demuxer element, check your installation"));
    do_async_done (urisrc);
    return NULL;
  }
}

static void
handle_new_pad (GstURISourceBin * urisrc, GstPad * srcpad, GstCaps * caps)
{
  gboolean is_raw;
  GstStructure *s;
  const gchar *media_type;
  gboolean do_download = FALSE;

  GST_URI_SOURCE_BIN_LOCK (urisrc);

  /* if this is a pad with all raw caps, we can expose it */
  if (is_all_raw_caps (caps, DEFAULT_CAPS, &is_raw) && is_raw) {
    GstPad *pad;

    GST_DEBUG_OBJECT (urisrc, "Found pad with raw caps %" GST_PTR_FORMAT
        ", exposing", caps);
    pad = create_output_pad (urisrc, srcpad);
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);

    expose_output_pad (urisrc, pad);
    return;
  }
  GST_URI_SOURCE_BIN_UNLOCK (urisrc);

  s = gst_caps_get_structure (caps, 0);
  media_type = gst_structure_get_name (s);

  urisrc->is_adaptive = IS_ADAPTIVE_MEDIA (media_type);

  if (urisrc->is_adaptive) {
    GstPad *sinkpad;
    GstPadLinkReturn link_res;

    urisrc->demuxer = make_demuxer (urisrc, caps);
    if (!urisrc->demuxer)
      goto no_demuxer;
    gst_bin_add (GST_BIN_CAST (urisrc), urisrc->demuxer);

    sinkpad = gst_element_get_static_pad (urisrc->demuxer, "sink");
    if (sinkpad == NULL)
      goto no_demuxer_sink;

    link_res = gst_pad_link (srcpad, sinkpad);

    gst_object_unref (sinkpad);
    if (link_res != GST_PAD_LINK_OK)
      goto could_not_link;

    gst_element_sync_state_with_parent (urisrc->demuxer);
  } else if (!urisrc->is_stream) {
    GstPad *pad;
    /* We don't need slot here, expose immediately */
    GST_URI_SOURCE_BIN_LOCK (urisrc);
    pad = create_output_pad (urisrc, srcpad);
    expose_output_pad (urisrc, pad);
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
  } else {
    OutputSlotInfo *slot;

    /* only enable download buffering if the upstream duration is known */
    if (urisrc->download) {
      GstQuery *query = gst_query_new_duration (GST_FORMAT_BYTES);
      if (gst_pad_query (srcpad, query)) {
        gint64 dur;
        gst_query_parse_duration (query, NULL, &dur);
        do_download = (dur != -1);
      }
      gst_object_unref (query);
    }

    GST_DEBUG_OBJECT (urisrc, "check media-type %s, %d", media_type,
        do_download);

    GST_URI_SOURCE_BIN_LOCK (urisrc);
    slot = get_output_slot (urisrc, do_download, FALSE, NULL);

    if (slot == NULL || gst_pad_link (srcpad, slot->sinkpad) != GST_PAD_LINK_OK)
      goto could_not_link;

    expose_output_pad (urisrc, slot->srcpad);
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
  }

  return;

  /* ERRORS */
no_demuxer:
  {
    /* error was posted */
    return;
  }
no_demuxer_sink:
  {
    GST_ELEMENT_ERROR (urisrc, CORE, NEGOTIATION,
        (NULL), ("Adaptive demuxer element has no 'sink' pad"));
    do_async_done (urisrc);
    return;
  }
could_not_link:
  {
    GST_URI_SOURCE_BIN_UNLOCK (urisrc);
    GST_ELEMENT_ERROR (urisrc, CORE, NEGOTIATION,
        (NULL), ("Can't link typefind to adaptive demuxer element"));
    do_async_done (urisrc);
    return;
  }
}

/* signaled when we have a stream and we need to configure the download
 * buffering or regular buffering */
static void
type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstURISourceBin * urisrc)
{
  GstPad *srcpad = gst_element_get_static_pad (typefind, "src");

  GST_DEBUG_OBJECT (urisrc, "typefind found caps %" GST_PTR_FORMAT
      " on pad %" GST_PTR_FORMAT, caps, srcpad);
  handle_new_pad (urisrc, srcpad, caps);

  gst_object_unref (GST_OBJECT (srcpad));
}

/* setup typefind for any source. This will first plug a typefind element to the
 * source. After we find the type, we decide to whether to plug an adaptive
 * demuxer, or just link through queue2 (if needed) and expose the data */
static gboolean
setup_typefind (GstURISourceBin * urisrc, GstPad * srcpad)
{
  GstElement *typefind;

  /* now create the typefind element */
  typefind = gst_element_factory_make ("typefind", NULL);
  if (!typefind)
    goto no_typefind;

  /* Make sure the bin doesn't set the typefind running yet */
  gst_element_set_locked_state (typefind, TRUE);

  gst_bin_add (GST_BIN_CAST (urisrc), typefind);

  if (!srcpad) {
    if (!gst_element_link_pads (urisrc->source, NULL, typefind, "sink"))
      goto could_not_link;
  } else {
    GstPad *sinkpad = gst_element_get_static_pad (typefind, "sink");
    GstPadLinkReturn ret;

    ret = gst_pad_link (srcpad, sinkpad);
    gst_object_unref (sinkpad);
    if (ret != GST_PAD_LINK_OK)
      goto could_not_link;
  }

  urisrc->typefinds = g_list_append (urisrc->typefinds, typefind);

  /* connect a signal to find out when the typefind element found
   * a type */
  g_signal_connect (typefind, "have-type", G_CALLBACK (type_found), urisrc);

  /* Now it can start */
  gst_element_set_locked_state (typefind, FALSE);
  gst_element_sync_state_with_parent (typefind);

  return TRUE;

  /* ERRORS */
no_typefind:
  {
    post_missing_plugin_error (GST_ELEMENT_CAST (urisrc), "typefind");
    GST_ELEMENT_ERROR (urisrc, CORE, MISSING_PLUGIN, (NULL),
        ("No typefind element, check your installation"));
    do_async_done (urisrc);
    return FALSE;
  }
could_not_link:
  {
    GST_ELEMENT_ERROR (urisrc, CORE, NEGOTIATION,
        (NULL), ("Can't link source to typefind element"));
    gst_bin_remove (GST_BIN_CAST (urisrc), typefind);
    do_async_done (urisrc);
    return FALSE;
  }
}

static void
free_output_slot (OutputSlotInfo * slot, GstURISourceBin * urisrc)
{
  GST_DEBUG_OBJECT (urisrc, "removing old queue element and freeing slot %p",
      slot);
  gst_element_set_locked_state (slot->queue, TRUE);
  gst_element_set_state (slot->queue, GST_STATE_NULL);
  gst_bin_remove (GST_BIN_CAST (urisrc), slot->queue);

  gst_object_unref (slot->sinkpad);

  remove_buffering_msgs (urisrc, GST_OBJECT_CAST (slot->queue));

  /* deactivate and remove the srcpad */
  gst_pad_set_active (slot->srcpad, FALSE);
  gst_element_remove_pad (GST_ELEMENT_CAST (urisrc), slot->srcpad);

  g_free (slot);
}

static void
call_free_output_slot (GstURISourceBin * urisrc, OutputSlotInfo * slot)
{
  GST_LOG_OBJECT (urisrc, "free output slot in thread pool");
  free_output_slot (slot, urisrc);
}

/* must be called with GST_URI_SOURCE_BIN_LOCK */
static void
free_output_slot_async (GstURISourceBin * urisrc, OutputSlotInfo * slot)
{
  GST_LOG_OBJECT (urisrc, "pushing output slot on thread pool to free");
  urisrc->out_slots = g_slist_remove (urisrc->out_slots, slot);
  gst_element_call_async (GST_ELEMENT_CAST (urisrc),
      (GstElementCallAsyncFunc) call_free_output_slot, slot, NULL);
}

/* remove source and all related elements */
static void
remove_source (GstURISourceBin * urisrc)
{
  GstElement *source = urisrc->source;

  if (source) {
    GST_DEBUG_OBJECT (urisrc, "removing old src element");
    gst_element_set_state (source, GST_STATE_NULL);

    if (urisrc->src_np_sig_id) {
      g_signal_handler_disconnect (source, urisrc->src_np_sig_id);
      urisrc->src_np_sig_id = 0;
    }
    gst_bin_remove (GST_BIN_CAST (urisrc), source);
    urisrc->source = NULL;
  }
  if (urisrc->typefinds) {
    GList *iter, *next;
    GST_DEBUG_OBJECT (urisrc, "removing old typefind element");
    for (iter = urisrc->typefinds; iter; iter = next) {
      GstElement *typefind = iter->data;

      next = g_list_next (iter);

      gst_element_set_state (typefind, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (urisrc), typefind);
    }
    g_list_free (urisrc->typefinds);
    urisrc->typefinds = NULL;
  }

  GST_URI_SOURCE_BIN_LOCK (urisrc);
  g_slist_foreach (urisrc->out_slots, (GFunc) free_output_slot, urisrc);
  g_slist_free (urisrc->out_slots);
  urisrc->out_slots = NULL;
  GST_URI_SOURCE_BIN_UNLOCK (urisrc);

  if (urisrc->demuxer) {
    GST_DEBUG_OBJECT (urisrc, "removing old adaptive demux element");
    gst_element_set_state (urisrc->demuxer, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (urisrc), urisrc->demuxer);
    urisrc->demuxer = NULL;
  }
}

/* is called when a dynamic source element created a new pad. */
static void
source_new_pad (GstElement * element, GstPad * pad, GstURISourceBin * urisrc)
{
  GstCaps *caps;

  GST_DEBUG_OBJECT (urisrc, "Found new pad %s.%s in source element %s",
      GST_DEBUG_PAD_NAME (pad), GST_ELEMENT_NAME (element));
  caps = gst_pad_get_current_caps (pad);
  if (caps == NULL)
    setup_typefind (urisrc, pad);
  else {
    handle_new_pad (urisrc, pad, caps);
    gst_caps_unref (caps);
  }
}

static gboolean
is_live_source (GstElement * source)
{
  GObjectClass *source_class = NULL;
  gboolean is_live = FALSE;
  GParamSpec *pspec;

  source_class = G_OBJECT_GET_CLASS (source);
  pspec = g_object_class_find_property (source_class, "is-live");
  if (!pspec || G_PARAM_SPEC_VALUE_TYPE (pspec) != G_TYPE_BOOLEAN)
    return FALSE;

  g_object_get (G_OBJECT (source), "is-live", &is_live, NULL);

  return is_live;
}

/* construct and run the source and demuxer elements until we found
 * all the streams or until a preroll queue has been filled.
*/
static gboolean
setup_source (GstURISourceBin * urisrc)
{
  gboolean is_raw, have_out, is_dynamic;

  GST_DEBUG_OBJECT (urisrc, "setup source");

  /* delete old src */
  remove_source (urisrc);

  /* create and configure an element that can handle the uri */
  if (!(urisrc->source = gen_source_element (urisrc)))
    goto no_source;

  /* state will be merged later - if file is not found, error will be
   * handled by the application right after. */
  gst_bin_add (GST_BIN_CAST (urisrc), urisrc->source);

  /* notify of the new source used */
  g_object_notify (G_OBJECT (urisrc), "source");

  g_signal_emit (urisrc, gst_uri_source_bin_signals[SIGNAL_SOURCE_SETUP],
      0, urisrc->source);

  if (is_live_source (urisrc->source))
    urisrc->is_stream = FALSE;

  /* remove the old demuxer now, if any */
  remove_demuxer (urisrc);

  /* see if the source element emits raw audio/video all by itself,
   * if so, we can create streams for the pads and be done with it.
   * Also check that is has source pads, if not, we assume it will
   * do everything itself.  */
  if (!analyse_source (urisrc, &is_raw, &have_out, &is_dynamic,
          urisrc->need_queue && urisrc->use_buffering))
    goto invalid_source;

  if (is_raw) {
    GST_DEBUG_OBJECT (urisrc, "Source provides all raw data");
    /* source provides raw data, we added the pads and we can now signal a
     * no_more pads because we are done. */
    gst_element_no_more_pads (GST_ELEMENT_CAST (urisrc));
    do_async_done (urisrc);
    return TRUE;
  }
  if (!have_out && !is_dynamic) {
    GST_DEBUG_OBJECT (urisrc, "Source has no output pads");
    return TRUE;
  }
  if (is_dynamic) {
    GST_DEBUG_OBJECT (urisrc, "Source has dynamic output pads");
    /* connect a handler for the new-pad signal */
    urisrc->src_np_sig_id =
        g_signal_connect (urisrc->source, "pad-added",
        G_CALLBACK (source_new_pad), urisrc);
  } else {
    if (urisrc->is_stream) {
      GST_DEBUG_OBJECT (urisrc, "Setting up streaming");
      /* do the stream things here */
      if (!setup_typefind (urisrc, NULL))
        goto streaming_failed;
    } else {
      GstIterator *pads_iter;
      gboolean done = FALSE;
      pads_iter = gst_element_iterate_src_pads (urisrc->source);
      while (!done) {
        GValue item = { 0, };
        GstPad *pad;

        switch (gst_iterator_next (pads_iter, &item)) {
          case GST_ITERATOR_ERROR:
            GST_WARNING_OBJECT (urisrc,
                "Error iterating pads on source element");
            /* FALLTROUGH */
          case GST_ITERATOR_DONE:
            done = TRUE;
            break;
          case GST_ITERATOR_RESYNC:
            /* reset results and resync */
            gst_iterator_resync (pads_iter);
            break;
          case GST_ITERATOR_OK:
            pad = g_value_get_object (&item);
            if (!setup_typefind (urisrc, pad)) {
              gst_iterator_free (pads_iter);
              goto streaming_failed;
            }
            g_value_reset (&item);
            break;
        }
      }
      gst_iterator_free (pads_iter);
    }
  }
  return TRUE;

  /* ERRORS */
no_source:
  {
    /* error message was already posted */
    return FALSE;
  }
invalid_source:
  {
    GST_ELEMENT_ERROR (urisrc, CORE, FAILED,
        (_("Source element is invalid.")), (NULL));
    return FALSE;
  }
streaming_failed:
  {
    /* message was posted */
    return FALSE;
  }
}

static void
value_list_append_structure_list (GValue * list_val, GstStructure ** first,
    GList * structure_list)
{
  GList *l;

  for (l = structure_list; l != NULL; l = l->next) {
    GValue val = { 0, };

    if (*first == NULL)
      *first = gst_structure_copy ((GstStructure *) l->data);

    g_value_init (&val, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&val, gst_structure_copy ((GstStructure *) l->data));
    gst_value_list_append_value (list_val, &val);
    g_value_unset (&val);
  }
}

/* if it's a redirect message with multiple redirect locations we might
 * want to pick a different 'best' location depending on the required
 * bitrates and the connection speed */
static GstMessage *
handle_redirect_message (GstURISourceBin * dec, GstMessage * msg)
{
  const GValue *locations_list, *location_val;
  GstMessage *new_msg;
  GstStructure *new_structure = NULL;
  GList *l_good = NULL, *l_neutral = NULL, *l_bad = NULL;
  GValue new_list = { 0, };
  guint size, i;
  const GstStructure *structure;

  GST_DEBUG_OBJECT (dec, "redirect message: %" GST_PTR_FORMAT, msg);
  GST_DEBUG_OBJECT (dec, "connection speed: %" G_GUINT64_FORMAT,
      dec->connection_speed);

  structure = gst_message_get_structure (msg);
  if (dec->connection_speed == 0 || structure == NULL)
    return msg;

  locations_list = gst_structure_get_value (structure, "locations");
  if (locations_list == NULL)
    return msg;

  size = gst_value_list_get_size (locations_list);
  if (size < 2)
    return msg;

  /* maintain existing order as much as possible, just sort references
   * with too high a bitrate to the end (the assumption being that if
   * bitrates are given they are given for all interesting streams and
   * that the you-need-at-least-version-xyz redirect has the same bitrate
   * as the lowest referenced redirect alternative) */
  for (i = 0; i < size; ++i) {
    const GstStructure *s;
    gint bitrate = 0;

    location_val = gst_value_list_get_value (locations_list, i);
    s = (const GstStructure *) g_value_get_boxed (location_val);
    if (!gst_structure_get_int (s, "minimum-bitrate", &bitrate) || bitrate <= 0) {
      GST_DEBUG_OBJECT (dec, "no bitrate: %" GST_PTR_FORMAT, s);
      l_neutral = g_list_append (l_neutral, (gpointer) s);
    } else if (bitrate > dec->connection_speed) {
      GST_DEBUG_OBJECT (dec, "bitrate too high: %" GST_PTR_FORMAT, s);
      l_bad = g_list_append (l_bad, (gpointer) s);
    } else if (bitrate <= dec->connection_speed) {
      GST_DEBUG_OBJECT (dec, "bitrate OK: %" GST_PTR_FORMAT, s);
      l_good = g_list_append (l_good, (gpointer) s);
    }
  }

  g_value_init (&new_list, GST_TYPE_LIST);
  value_list_append_structure_list (&new_list, &new_structure, l_good);
  value_list_append_structure_list (&new_list, &new_structure, l_neutral);
  value_list_append_structure_list (&new_list, &new_structure, l_bad);
  gst_structure_take_value (new_structure, "locations", &new_list);

  g_list_free (l_good);
  g_list_free (l_neutral);
  g_list_free (l_bad);

  new_msg = gst_message_new_element (msg->src, new_structure);
  gst_message_unref (msg);

  GST_DEBUG_OBJECT (dec, "new redirect message: %" GST_PTR_FORMAT, new_msg);
  return new_msg;
}

static void
handle_buffering_message (GstURISourceBin * urisrc, GstMessage * msg)
{
  gint perc, msg_perc;
  gint smaller_perc = 100;
  GstMessage *smaller = NULL;
  GList *found = NULL;
  GList *iter;
  OutputSlotInfo *slot;

  /* buffering messages must be aggregated as there might be multiple
   * multiqueue in the pipeline and their independent buffering messages
   * will confuse the application
   *
   * urisourcebin keeps a list of messages received from elements that are
   * buffering.
   * Rules are:
   * 0) Ignore buffering from elements that are draining (is_eos == TRUE)
   * 1) Always post the smaller buffering %
   * 2) If an element posts a 100% buffering message, remove it from the list
   * 3) When there are no more messages on the list, post 100% message
   * 4) When an element posts a new buffering message, update the one
   *    on the list to this new value
   */
  gst_message_parse_buffering (msg, &msg_perc);
  GST_LOG_OBJECT (urisrc, "Got buffering msg from %" GST_PTR_FORMAT
      " with %d%%", GST_MESSAGE_SRC (msg), msg_perc);

  slot = g_object_get_data (G_OBJECT (GST_MESSAGE_SRC (msg)),
      "urisourcebin.slotinfo");

  BUFFERING_LOCK (urisrc);
  if (slot && slot->is_eos) {
    /* Ignore buffering messages from queues we marked as EOS,
     * we already removed those from the list of buffering
     * objects */
    BUFFERING_UNLOCK (urisrc);
    gst_message_replace (&msg, NULL);
    return;
  }


  g_mutex_lock (&urisrc->buffering_post_lock);

  /*
   * Single loop for 2 things:
   * 1) Look for a message with the same source
   *   1.1) If the received message is 100%, remove it from the list
   * 2) Find the minimum buffering from the list from elements that aren't EOS
   */
  for (iter = urisrc->buffering_status; iter;) {
    GstMessage *bufstats = iter->data;
    gboolean is_eos = FALSE;

    slot = g_object_get_data (G_OBJECT (GST_MESSAGE_SRC (bufstats)),
        "urisourcebin.slotinfo");
    if (slot)
      is_eos = slot->is_eos;

    if (GST_MESSAGE_SRC (bufstats) == GST_MESSAGE_SRC (msg)) {
      found = iter;
      if (msg_perc < 100) {
        gst_message_unref (iter->data);
        bufstats = iter->data = gst_message_ref (msg);
      } else {
        GList *current = iter;

        /* remove the element here and avoid confusing the loop */
        iter = g_list_next (iter);

        gst_message_unref (current->data);
        urisrc->buffering_status =
            g_list_delete_link (urisrc->buffering_status, current);

        continue;
      }
    }

    /* only update minimum stat for non-EOS slots */
    if (!is_eos) {
      gst_message_parse_buffering (bufstats, &perc);
      if (perc < smaller_perc) {
        smaller_perc = perc;
        smaller = bufstats;
      }
    } else {
      GST_LOG_OBJECT (urisrc, "Ignoring buffering from EOS element");
    }
    iter = g_list_next (iter);
  }

  if (found == NULL && msg_perc < 100) {
    if (msg_perc < smaller_perc) {
      smaller_perc = msg_perc;
      smaller = msg;
    }
    urisrc->buffering_status =
        g_list_prepend (urisrc->buffering_status, gst_message_ref (msg));
  }

  if (smaller_perc == urisrc->last_buffering_pct) {
    /* Don't repeat our last buffering status */
    gst_message_replace (&msg, NULL);
  } else {
    urisrc->last_buffering_pct = smaller_perc;

    /* now compute the buffering message that should be posted */
    if (smaller_perc == 100) {
      g_assert (urisrc->buffering_status == NULL);
      /* we are posting the original received msg */
    } else {
      gst_message_replace (&msg, smaller);
    }
  }
  BUFFERING_UNLOCK (urisrc);

  if (msg) {
    GST_LOG_OBJECT (urisrc, "Sending buffering msg from %" GST_PTR_FORMAT
        " with %d%%", GST_MESSAGE_SRC (msg), smaller_perc);
    GST_BIN_CLASS (parent_class)->handle_message (GST_BIN (urisrc), msg);
  } else {
    GST_LOG_OBJECT (urisrc, "Dropped buffering msg as a repeat of %d%%",
        smaller_perc);
  }
  g_mutex_unlock (&urisrc->buffering_post_lock);
}

/* Remove any buffering message from the given source */
static void
remove_buffering_msgs (GstURISourceBin * urisrc, GstObject * src)
{
  GList *iter;
  gboolean removed = FALSE, post;

  BUFFERING_LOCK (urisrc);
  g_mutex_lock (&urisrc->buffering_post_lock);

  GST_DEBUG_OBJECT (urisrc, "Removing %" GST_PTR_FORMAT
      " buffering messages", src);

  for (iter = urisrc->buffering_status; iter;) {
    GstMessage *bufstats = iter->data;
    if (GST_MESSAGE_SRC (bufstats) == src) {
      gst_message_unref (bufstats);
      urisrc->buffering_status =
          g_list_delete_link (urisrc->buffering_status, iter);
      removed = TRUE;
      break;
    }
    iter = g_list_next (iter);
  }

  post = (removed && urisrc->buffering_status == NULL);
  BUFFERING_UNLOCK (urisrc);

  if (post) {
    GST_DEBUG_OBJECT (urisrc, "Last buffering element done - posting 100%%");

    /* removed the last buffering element, post 100% */
    gst_element_post_message (GST_ELEMENT_CAST (urisrc),
        gst_message_new_buffering (GST_OBJECT_CAST (urisrc), 100));
  }

  g_mutex_unlock (&urisrc->buffering_post_lock);
}

static void
handle_message (GstBin * bin, GstMessage * msg)
{
  GstURISourceBin *urisrc = GST_URI_SOURCE_BIN (bin);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ELEMENT:{
      if (gst_message_has_name (msg, "redirect")) {
        /* sort redirect messages based on the connection speed. This simplifies
         * the user of this element as it can in most cases just pick the first item
         * of the sorted list as a good redirection candidate. It can of course
         * choose something else from the list if it has a better way. */
        msg = handle_redirect_message (urisrc, msg);
      }
      break;
    }
    case GST_MESSAGE_BUFFERING:
      handle_buffering_message (urisrc, msg);
      msg = NULL;
      break;
    default:
      break;
  }

  if (msg)
    GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}

/* generic struct passed to all query fold methods
 * FIXME, move to core.
 */
typedef struct
{
  GstQuery *query;
  gint64 min;
  gint64 max;
  gboolean seekable;
  gboolean live;
} QueryFold;

typedef void (*QueryInitFunction) (GstURISourceBin * urisrc, QueryFold * fold);
typedef void (*QueryDoneFunction) (GstURISourceBin * urisrc, QueryFold * fold);

/* for duration/position we collect all durations/positions and take
 * the MAX of all valid results */
static void
decoder_query_init (GstURISourceBin * dec, QueryFold * fold)
{
  fold->min = 0;
  fold->max = -1;
  fold->seekable = TRUE;
  fold->live = 0;
}

static gboolean
decoder_query_duration_fold (const GValue * item, GValue * ret,
    QueryFold * fold)
{
  GstPad *pad = g_value_get_object (item);

  if (gst_pad_query (pad, fold->query)) {
    gint64 duration;

    g_value_set_boolean (ret, TRUE);

    gst_query_parse_duration (fold->query, NULL, &duration);

    GST_DEBUG_OBJECT (item, "got duration %" G_GINT64_FORMAT, duration);

    if (duration > fold->max)
      fold->max = duration;
  }
  return TRUE;
}

static void
decoder_query_duration_done (GstURISourceBin * dec, QueryFold * fold)
{
  GstFormat format;

  gst_query_parse_duration (fold->query, &format, NULL);
  /* store max in query result */
  gst_query_set_duration (fold->query, format, fold->max);

  GST_DEBUG ("max duration %" G_GINT64_FORMAT, fold->max);
}

static gboolean
decoder_query_position_fold (const GValue * item, GValue * ret,
    QueryFold * fold)
{
  GstPad *pad = g_value_get_object (item);

  if (gst_pad_query (pad, fold->query)) {
    gint64 position;

    g_value_set_boolean (ret, TRUE);

    gst_query_parse_position (fold->query, NULL, &position);

    GST_DEBUG_OBJECT (item, "got position %" G_GINT64_FORMAT, position);

    if (position > fold->max)
      fold->max = position;
  }

  return TRUE;
}

static void
decoder_query_position_done (GstURISourceBin * dec, QueryFold * fold)
{
  GstFormat format;

  gst_query_parse_position (fold->query, &format, NULL);
  /* store max in query result */
  gst_query_set_position (fold->query, format, fold->max);

  GST_DEBUG_OBJECT (dec, "max position %" G_GINT64_FORMAT, fold->max);
}

static gboolean
decoder_query_latency_fold (const GValue * item, GValue * ret, QueryFold * fold)
{
  GstPad *pad = g_value_get_object (item);

  if (gst_pad_query (pad, fold->query)) {
    GstClockTime min, max;
    gboolean live;

    gst_query_parse_latency (fold->query, &live, &min, &max);

    GST_DEBUG_OBJECT (pad,
        "got latency min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT
        ", live %d", GST_TIME_ARGS (min), GST_TIME_ARGS (max), live);

    if (live) {
      /* for the combined latency we collect the MAX of all min latencies and
       * the MIN of all max latencies */
      if (min > fold->min)
        fold->min = min;
      if (fold->max == -1)
        fold->max = max;
      else if (max < fold->max)
        fold->max = max;

      fold->live = TRUE;
    }
  } else {
    GST_LOG_OBJECT (pad, "latency query failed");
    g_value_set_boolean (ret, FALSE);
  }

  return TRUE;
}

static void
decoder_query_latency_done (GstURISourceBin * dec, QueryFold * fold)
{
  /* store max in query result */
  gst_query_set_latency (fold->query, fold->live, fold->min, fold->max);

  GST_DEBUG_OBJECT (dec,
      "latency min %" GST_TIME_FORMAT ", max %" GST_TIME_FORMAT
      ", live %d", GST_TIME_ARGS (fold->min), GST_TIME_ARGS (fold->max),
      fold->live);
}

/* we are seekable if all srcpads are seekable */
static gboolean
decoder_query_seeking_fold (const GValue * item, GValue * ret, QueryFold * fold)
{
  GstPad *pad = g_value_get_object (item);

  if (gst_pad_query (pad, fold->query)) {
    gboolean seekable;

    g_value_set_boolean (ret, TRUE);
    gst_query_parse_seeking (fold->query, NULL, &seekable, NULL, NULL);

    GST_DEBUG_OBJECT (item, "got seekable %d", seekable);

    if (fold->seekable)
      fold->seekable = seekable;
  }

  return TRUE;
}

static void
decoder_query_seeking_done (GstURISourceBin * dec, QueryFold * fold)
{
  GstFormat format;

  gst_query_parse_seeking (fold->query, &format, NULL, NULL, NULL);
  gst_query_set_seeking (fold->query, format, fold->seekable, 0, -1);

  GST_DEBUG_OBJECT (dec, "seekable %d", fold->seekable);
}

/* generic fold, return first valid result */
static gboolean
decoder_query_generic_fold (const GValue * item, GValue * ret, QueryFold * fold)
{
  GstPad *pad = g_value_get_object (item);
  gboolean res;

  if ((res = gst_pad_query (pad, fold->query))) {
    g_value_set_boolean (ret, TRUE);
    GST_DEBUG_OBJECT (item, "answered query %p", fold->query);
  }

  /* and stop as soon as we have a valid result */
  return !res;
}

/* we're a bin, the default query handler iterates sink elements, which we don't
 * have normally. We should just query all source pads.
 */
static gboolean
gst_uri_source_bin_query (GstElement * element, GstQuery * query)
{
  GstURISourceBin *decoder;
  gboolean res = FALSE;
  GstIterator *iter;
  GstIteratorFoldFunction fold_func;
  QueryInitFunction fold_init = NULL;
  QueryDoneFunction fold_done = NULL;
  QueryFold fold_data;
  GValue ret = { 0 };
  gboolean default_ret = FALSE;

  decoder = GST_URI_SOURCE_BIN (element);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
      /* iterate and collect durations */
      fold_func = (GstIteratorFoldFunction) decoder_query_duration_fold;
      fold_init = decoder_query_init;
      fold_done = decoder_query_duration_done;
      break;
    case GST_QUERY_POSITION:
      /* iterate and collect durations */
      fold_func = (GstIteratorFoldFunction) decoder_query_position_fold;
      fold_init = decoder_query_init;
      fold_done = decoder_query_position_done;
      break;
    case GST_QUERY_LATENCY:
      /* iterate and collect durations */
      fold_func = (GstIteratorFoldFunction) decoder_query_latency_fold;
      fold_init = decoder_query_init;
      fold_done = decoder_query_latency_done;
      default_ret = TRUE;
      break;
    case GST_QUERY_SEEKING:
      /* iterate and collect durations */
      fold_func = (GstIteratorFoldFunction) decoder_query_seeking_fold;
      fold_init = decoder_query_init;
      fold_done = decoder_query_seeking_done;
      break;
    default:
      fold_func = (GstIteratorFoldFunction) decoder_query_generic_fold;
      break;
  }

  fold_data.query = query;

  g_value_init (&ret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&ret, default_ret);

  iter = gst_element_iterate_src_pads (element);
  GST_DEBUG_OBJECT (element, "Sending query %p (type %d) to src pads",
      query, GST_QUERY_TYPE (query));

  if (fold_init)
    fold_init (decoder, &fold_data);

  while (TRUE) {
    GstIteratorResult ires;

    ires = gst_iterator_fold (iter, fold_func, &ret, &fold_data);

    switch (ires) {
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        if (fold_init)
          fold_init (decoder, &fold_data);
        g_value_set_boolean (&ret, default_ret);
        break;
      case GST_ITERATOR_OK:
      case GST_ITERATOR_DONE:
        res = g_value_get_boolean (&ret);
        if (fold_done != NULL && res)
          fold_done (decoder, &fold_data);
        goto done;
      default:
        res = FALSE;
        goto done;
    }
  }
done:
  gst_iterator_free (iter);

  return res;
}

static void
sync_slot_queue (OutputSlotInfo * slot)
{
  gst_element_sync_state_with_parent (slot->queue);
}

static GstStateChangeReturn
gst_uri_source_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstURISourceBin *urisrc = GST_URI_SOURCE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      do_async_start (urisrc);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto setup_failed;
  else if (ret == GST_STATE_CHANGE_NO_PREROLL)
    do_async_done (urisrc);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("ready to paused");
      if (!setup_source (urisrc))
        goto source_failed;

      ret = GST_STATE_CHANGE_ASYNC;

      /* And now sync the states of everything we added */
      g_slist_foreach (urisrc->out_slots, (GFunc) sync_slot_queue, NULL);
      if (urisrc->typefinds) {
        GList *iter;
        for (iter = urisrc->typefinds; iter; iter = iter->next) {
          GstElement *typefind = iter->data;
          ret = gst_element_set_state (typefind, GST_STATE_PAUSED);
          if (ret == GST_STATE_CHANGE_FAILURE)
            goto setup_failed;
        }
      }
      if (urisrc->source)
        ret = gst_element_set_state (urisrc->source, GST_STATE_PAUSED);
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto setup_failed;
      if (ret == GST_STATE_CHANGE_SUCCESS)
        ret = GST_STATE_CHANGE_ASYNC;

      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("paused to ready");
      remove_demuxer (urisrc);
      remove_source (urisrc);
      do_async_done (urisrc);
      g_list_free_full (urisrc->buffering_status,
          (GDestroyNotify) gst_message_unref);
      urisrc->buffering_status = NULL;
      urisrc->last_buffering_pct = -1;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_DEBUG ("ready to null");
      remove_demuxer (urisrc);
      remove_source (urisrc);
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
source_failed:
  {
    do_async_done (urisrc);
    return GST_STATE_CHANGE_FAILURE;
  }
setup_failed:
  {
    /* clean up leftover groups */
    do_async_done (urisrc);
    return GST_STATE_CHANGE_FAILURE;
  }
}

gboolean
gst_uri_source_bin_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_uri_source_bin_debug, "urisourcebin", 0,
      "URI source element");

  return gst_element_register (plugin, "urisourcebin", GST_RANK_NONE,
      GST_TYPE_URI_SOURCE_BIN);
}
