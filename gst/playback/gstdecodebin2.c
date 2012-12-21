/* GStreamer
 * Copyright (C) <2006> Edward Hervey <edward@fluendo.com>
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2011> Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
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
 * decodebin is considered stable now and replaces the old #decodebin element.
 * #uridecodebin uses decodebin internally and is often more convenient to
 * use, as it creates a suitable source element as well.
 */

/* Implementation notes:
 *
 * The following section describes how decodebin works internally.
 *
 * The first part of decodebin is its typefind element, which tries
 * to determine the media type of the input stream. If the type is found
 * autoplugging starts.
 *
 * decodebin internally organizes the elements it autoplugged into GstDecodeChains
 * and GstDecodeGroups. A decode chain is a single chain of decoding, this
 * means that if decodebin every autoplugs an element with two+ srcpads
 * (e.g. a demuxer) this will end the chain and everything following this
 * demuxer will be put into decode groups below the chain. Otherwise,
 * if an element has a single srcpad that outputs raw data the decode chain
 * is ended too and a GstDecodePad is stored and blocked.
 *
 * A decode group combines a number of chains that are created by a
 * demuxer element. All those chains are connected through a multiqueue to
 * the demuxer. A new group for the same demuxer is only created if the
 * demuxer has signaled no-more-pads, in which case all following pads
 * create a new chain in the new group.
 *
 * This continues until the top-level decode chain is complete. A decode
 * chain is complete if it either ends with a blocked endpad, if autoplugging
 * stopped because no suitable plugins could be found or if the active group
 * is complete. A decode group on the other hand is complete if all child
 * chains are complete.
 *
 * If this happens at some point, all endpads of all active groups are exposed.
 * For this decodebin adds the endpads, signals no-more-pads and then unblocks
 * them. Now playback starts.
 *
 * If one of the chains that end on a endpad receives EOS decodebin checks
 * if all chains and groups are drained. In that case everything goes into EOS.
 * If there is a chain where the active group is drained but there exist next
 * groups, the active group is hidden (endpads are removed) and the next group
 * is exposed. This means that in some cases more pads may be created even
 * after the initial no-more-pads signal. This happens for example with
 * so-called "chained oggs", most commonly found among ogg/vorbis internet
 * radio streams.
 *
 * Note 1: If we're talking about blocked endpads this really means that the
 * *target* pads of the endpads are blocked. Pads that are exposed to the outside
 * should never ever be blocked!
 *
 * Note 2: If a group is complete and the parent's chain demuxer adds new pads
 * but never signaled no-more-pads this additional pads will be ignored!
 *
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "gstplay-enum.h"
#include "gstplayback.h"
#include "gstrawcaps.h"

/* Also used by gsturidecodebin.c */
gint _decode_bin_compare_factories_func (gconstpointer p1, gconstpointer p2);

/* generic templates */
static GstStaticPadTemplate decoder_bin_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate decoder_bin_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_decode_bin_debug);
#define GST_CAT_DEFAULT gst_decode_bin_debug

typedef struct _GstPendingPad GstPendingPad;
typedef struct _GstDecodeElement GstDecodeElement;
typedef struct _GstDecodeChain GstDecodeChain;
typedef struct _GstDecodeGroup GstDecodeGroup;
typedef struct _GstDecodePad GstDecodePad;
typedef GstGhostPadClass GstDecodePadClass;
typedef struct _GstDecodeBin GstDecodeBin;
typedef struct _GstDecodeBinClass GstDecodeBinClass;

#define GST_TYPE_DECODE_BIN             (gst_decode_bin_get_type())
#define GST_DECODE_BIN_CAST(obj)        ((GstDecodeBin*)(obj))
#define GST_DECODE_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECODE_BIN,GstDecodeBin))
#define GST_DECODE_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DECODE_BIN,GstDecodeBinClass))
#define GST_IS_DECODE_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DECODE_BIN))
#define GST_IS_DECODE_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DECODE_BIN))

/**
 *  GstDecodeBin:
 *
 *  The opaque #GstDecodeBin data structure
 */
struct _GstDecodeBin
{
  GstBin bin;                   /* we extend GstBin */

  /* properties */
  GstCaps *caps;                /* caps on which to stop decoding */
  gchar *encoding;              /* encoding of subtitles */
  gboolean use_buffering;       /* configure buffering on multiqueues */
  gint low_percent;
  gint high_percent;
  guint max_size_bytes;
  guint max_size_buffers;
  guint64 max_size_time;
  gboolean post_stream_topology;
  guint64 connection_speed;

  GstElement *typefind;         /* this holds the typefind object */

  GMutex *expose_lock;          /* Protects exposal and removal of groups */
  GstDecodeChain *decode_chain; /* Top level decode chain */
  guint nbpads;                 /* unique identifier for source pads */

  GMutex *factories_lock;
  guint32 factories_cookie;     /* Cookie from last time when factories was updated */
  GList *factories;             /* factories we can use for selecting elements */

  GMutex *subtitle_lock;        /* Protects changes to subtitles and encoding */
  GList *subtitles;             /* List of elements with subtitle-encoding,
                                 * protected by above mutex! */

  gboolean have_type;           /* if we received the have_type signal */
  guint have_type_id;           /* signal id for have-type from typefind */

  gboolean async_pending;       /* async-start has been emitted */

  GMutex *dyn_lock;             /* lock protecting pad blocking */
  gboolean shutdown;            /* if we are shutting down */
  GList *blocked_pads;          /* pads that have set to block */

  gboolean expose_allstreams;   /* Whether to expose unknow type streams or not */

  GList *filtered;              /* elements for which error messages are filtered */
};

struct _GstDecodeBinClass
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

  /* fired when the last group is drained */
  void (*drained) (GstElement * element);
};

/* signals */
enum
{
  SIGNAL_UNKNOWN_TYPE,
  SIGNAL_AUTOPLUG_CONTINUE,
  SIGNAL_AUTOPLUG_FACTORIES,
  SIGNAL_AUTOPLUG_SELECT,
  SIGNAL_AUTOPLUG_SORT,
  SIGNAL_DRAINED,
  LAST_SIGNAL
};

/* automatic sizes, while prerolling we buffer up to 2MB, we ignore time
 * and buffers in this case. */
#define AUTO_PREROLL_SIZE_BYTES                  2 * 1024 * 1024
#define AUTO_PREROLL_SIZE_BUFFERS                0
#define AUTO_PREROLL_NOT_SEEKABLE_SIZE_TIME      10 * GST_SECOND
#define AUTO_PREROLL_SEEKABLE_SIZE_TIME          0

/* whan playing, keep a max of 2MB of data but try to keep the number of buffers
 * as low as possible (try to aim for 5 buffers) */
#define AUTO_PLAY_SIZE_BYTES        2 * 1024 * 1024
#define AUTO_PLAY_SIZE_BUFFERS      5
#define AUTO_PLAY_SIZE_TIME         0

#define DEFAULT_SUBTITLE_ENCODING NULL
#define DEFAULT_USE_BUFFERING     FALSE
#define DEFAULT_LOW_PERCENT       10
#define DEFAULT_HIGH_PERCENT      99
/* by default we use the automatic values above */
#define DEFAULT_MAX_SIZE_BYTES    0
#define DEFAULT_MAX_SIZE_BUFFERS  0
#define DEFAULT_MAX_SIZE_TIME     0
#define DEFAULT_POST_STREAM_TOPOLOGY FALSE
#define DEFAULT_EXPOSE_ALL_STREAMS  TRUE
#define DEFAULT_CONNECTION_SPEED    0

/* Properties */
enum
{
  PROP_0,
  PROP_CAPS,
  PROP_SUBTITLE_ENCODING,
  PROP_SINK_CAPS,
  PROP_USE_BUFFERING,
  PROP_LOW_PERCENT,
  PROP_HIGH_PERCENT,
  PROP_MAX_SIZE_BYTES,
  PROP_MAX_SIZE_BUFFERS,
  PROP_MAX_SIZE_TIME,
  PROP_POST_STREAM_TOPOLOGY,
  PROP_EXPOSE_ALL_STREAMS,
  PROP_CONNECTION_SPEED,
  PROP_LAST
};

static GstBinClass *parent_class;
static guint gst_decode_bin_signals[LAST_SIGNAL] = { 0 };

static GstStaticCaps default_raw_caps = GST_STATIC_CAPS (DEFAULT_RAW_CAPS);

static void do_async_start (GstDecodeBin * dbin);
static void do_async_done (GstDecodeBin * dbin);

static void type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin);

static void decodebin_set_queue_size (GstDecodeBin * dbin,
    GstElement * multiqueue, gboolean preroll, gboolean seekable);

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
static void caps_notify_cb (GstPad * pad, GParamSpec * unused,
    GstDecodeChain * chain);

static GstPad *find_sink_pad (GstElement * element);
static GstStateChangeReturn gst_decode_bin_change_state (GstElement * element,
    GstStateChange transition);
static void gst_decode_bin_handle_message (GstBin * bin, GstMessage * message);

static gboolean check_upstream_seekable (GstDecodeBin * dbin, GstPad * pad);

#define EXPOSE_LOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "expose locking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_lock (GST_DECODE_BIN_CAST(dbin)->expose_lock);		\
    GST_LOG_OBJECT (dbin,						\
		    "expose locked from thread %p",			\
		    g_thread_self ());					\
} G_STMT_END

#define EXPOSE_UNLOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "expose unlocking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_unlock (GST_DECODE_BIN_CAST(dbin)->expose_lock);		\
} G_STMT_END

#define DYN_LOCK(dbin) G_STMT_START {			\
    GST_LOG_OBJECT (dbin,						\
		    "dynlocking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_lock (GST_DECODE_BIN_CAST(dbin)->dyn_lock);			\
    GST_LOG_OBJECT (dbin,						\
		    "dynlocked from thread %p",				\
		    g_thread_self ());					\
} G_STMT_END

#define DYN_UNLOCK(dbin) G_STMT_START {			\
    GST_LOG_OBJECT (dbin,						\
		    "dynunlocking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_unlock (GST_DECODE_BIN_CAST(dbin)->dyn_lock);		\
} G_STMT_END

#define SUBTITLE_LOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "subtitle locking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_lock (GST_DECODE_BIN_CAST(dbin)->subtitle_lock);		\
    GST_LOG_OBJECT (dbin,						\
		    "subtitle lock from thread %p",			\
		    g_thread_self ());					\
} G_STMT_END

#define SUBTITLE_UNLOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "subtitle unlocking from thread %p",		\
		    g_thread_self ());					\
    g_mutex_unlock (GST_DECODE_BIN_CAST(dbin)->subtitle_lock);		\
} G_STMT_END

struct _GstPendingPad
{
  GstPad *pad;
  GstDecodeChain *chain;
  gulong event_probe_id;
};

struct _GstDecodeElement
{
  GstElement *element;
  GstElement *capsfilter;       /* Optional capsfilter for Parser/Convert */
};

/* GstDecodeGroup
 *
 * Streams belonging to the same group/chain of a media file
 *
 * When changing something here lock the parent chain!
 */
struct _GstDecodeGroup
{
  GstDecodeBin *dbin;
  GstDecodeChain *parent;

  GstElement *multiqueue;       /* Used for linking all child chains */
  gulong overrunsig;            /* the overrun signal for multiqueue */

  gboolean overrun;             /* TRUE if the multiqueue signaled overrun. This
                                 * means that we should really expose the group */

  gboolean no_more_pads;        /* TRUE if the demuxer signaled no-more-pads */
  gboolean drained;             /* TRUE if the all children are drained */

  GList *children;              /* List of GstDecodeChains in this group */

  GList *reqpads;               /* List of RequestPads for multiqueue, there is
                                 * exactly one RequestPad per child chain */
};

struct _GstDecodeChain
{
  GstDecodeGroup *parent;
  GstDecodeBin *dbin;

  GMutex *lock;                 /* Protects this chain and its groups */

  GstPad *pad;                  /* srcpad that caused creation of this chain */

  gboolean drained;             /* TRUE if the all children are drained */
  gboolean demuxer;             /* TRUE if elements->data is a demuxer */
  gboolean seekable;            /* TRUE if this chain ends on a demuxer and is seekable */
  GList *elements;              /* All elements in this group, first
                                   is the latest and most downstream element */

  /* Note: there are only groups if the last element of this chain
   * is a demuxer, otherwise the chain will end with an endpad.
   * The other way around this means, that endpad only exists if this
   * chain doesn't end with a demuxer! */

  GstDecodeGroup *active_group; /* Currently active group */
  GList *next_groups;           /* head is newest group, tail is next group.
                                   a new group will be created only if the head
                                   group had no-more-pads. If it's only exposed
                                   all new pads will be ignored! */
  GList *pending_pads;          /* Pads that have no fixed caps yet */

  GstDecodePad *endpad;         /* Pad of this chain that could be exposed */
  gboolean deadend;             /* This chain is incomplete and can't be completed,
                                   e.g. no suitable decoder could be found
                                   e.g. stream got EOS without buffers
                                 */
  GstCaps *endcaps;             /* Caps that were used when linking to the endpad
                                   or that resulted in the deadend
                                 */

  /* FIXME: This should be done directly via a thread! */
  GList *old_groups;            /* Groups that should be freed later */
};

static void gst_decode_chain_free (GstDecodeChain * chain);
static GstDecodeChain *gst_decode_chain_new (GstDecodeBin * dbin,
    GstDecodeGroup * group, GstPad * pad);
static void gst_decode_group_hide (GstDecodeGroup * group);
static void gst_decode_group_free (GstDecodeGroup * group);
static GstDecodeGroup *gst_decode_group_new (GstDecodeBin * dbin,
    GstDecodeChain * chain);
static gboolean gst_decode_chain_is_complete (GstDecodeChain * chain);
static gboolean gst_decode_chain_expose (GstDecodeChain * chain,
    GList ** endpads, gboolean * missing_plugin);
static gboolean gst_decode_chain_is_drained (GstDecodeChain * chain);
static gboolean gst_decode_group_is_complete (GstDecodeGroup * group);
static GstPad *gst_decode_group_control_demuxer_pad (GstDecodeGroup * group,
    GstPad * pad);
static gboolean gst_decode_group_is_drained (GstDecodeGroup * group);

static gboolean gst_decode_bin_expose (GstDecodeBin * dbin);

#define CHAIN_MUTEX_LOCK(chain) G_STMT_START {				\
    GST_LOG_OBJECT (chain->dbin,					\
		    "locking chain %p from thread %p",			\
		    chain, g_thread_self ());				\
    g_mutex_lock (chain->lock);						\
    GST_LOG_OBJECT (chain->dbin,					\
		    "locked chain %p from thread %p",			\
		    chain, g_thread_self ());				\
} G_STMT_END

#define CHAIN_MUTEX_UNLOCK(chain) G_STMT_START {                        \
    GST_LOG_OBJECT (chain->dbin,					\
		    "unlocking chain %p from thread %p",		\
		    chain, g_thread_self ());				\
    g_mutex_unlock (chain->lock);					\
} G_STMT_END

/* GstDecodePad
 *
 * GstPad private used for source pads of chains
 */
struct _GstDecodePad
{
  GstGhostPad parent;
  GstDecodeBin *dbin;
  GstDecodeChain *chain;

  gboolean blocked;             /* the *target* pad is blocked */
  gboolean exposed;             /* the pad is exposed */
  gboolean drained;             /* an EOS has been seen on the pad */

  gulong block_id;
};

GType gst_decode_pad_get_type (void);
G_DEFINE_TYPE (GstDecodePad, gst_decode_pad, GST_TYPE_GHOST_PAD);
#define GST_TYPE_DECODE_PAD (gst_decode_pad_get_type ())
#define GST_DECODE_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DECODE_PAD,GstDecodePad))

static GstDecodePad *gst_decode_pad_new (GstDecodeBin * dbin, GstPad * pad,
    GstDecodeChain * chain);
static void gst_decode_pad_activate (GstDecodePad * dpad,
    GstDecodeChain * chain);
static void gst_decode_pad_unblock (GstDecodePad * dpad);
static void gst_decode_pad_set_blocked (GstDecodePad * dpad, gboolean blocked);

static void gst_pending_pad_free (GstPendingPad * ppad);
static GstPadProbeReturn pad_event_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer data);

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
        g_type_register_static (GST_TYPE_BIN, "GstDecodeBin",
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

  gobject_klass->dispose = gst_decode_bin_dispose;
  gobject_klass->finalize = gst_decode_bin_finalize;
  gobject_klass->set_property = gst_decode_bin_set_property;
  gobject_klass->get_property = gst_decode_bin_get_property;

  /**
   * GstDecodeBin::unknown-type:
   * @bin: The decodebin.
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
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstDecodeBin::autoplug-continue:
   * @bin: The decodebin.
   * @pad: The #GstPad.
   * @caps: The #GstCaps found.
   *
   * This signal is emitted whenever decodebin finds a new stream. It is
   * emitted before looking for any elements that can handle that stream.
   *
   * <note>
   *   Invocation of signal handlers stops after the first signal handler
   *   returns #FALSE. Signal handlers are invoked in the order they were
   *   connected in.
   * </note>
   *
   * Returns: #TRUE if you wish decodebin to look for elements that can
   * handle the given @caps. If #FALSE, those caps will be considered as
   * final and the pad will be exposed as such (see 'pad-added' signal of
   * #GstElement).
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_CONTINUE] =
      g_signal_new ("autoplug-continue", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_continue),
      _gst_boolean_accumulator, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 2, GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstDecodeBin::autoplug-factories:
   * @bin: The decodebin.
   * @pad: The #GstPad.
   * @caps: The #GstCaps found.
   *
   * This function is emited when an array of possible factories for @caps on
   * @pad is needed. Decodebin will by default return an array with all
   * compatible factories, sorted by rank.
   *
   * If this function returns NULL, @pad will be exposed as a final caps.
   *
   * If this function returns an empty array, the pad will be considered as
   * having an unhandled type media type.
   *
   * <note>
   *   Only the signal handler that is connected first will ever by invoked.
   *   Don't connect signal handlers with the #G_CONNECT_AFTER flag to this
   *   signal, they will never be invoked!
   * </note>
   *
   * Returns: a #GValueArray* with a list of factories to try. The factories are
   * by default tried in the returned order or based on the index returned by
   * "autoplug-select".
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_FACTORIES] =
      g_signal_new ("autoplug-factories", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass,
          autoplug_factories), _gst_array_accumulator, NULL,
      g_cclosure_marshal_generic, G_TYPE_VALUE_ARRAY, 2,
      GST_TYPE_PAD, GST_TYPE_CAPS);

  /**
   * GstDecodeBin::autoplug-sort:
   * @bin: The decodebin.
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
   * <note>
   *   Invocation of signal handlers stops after one signal handler has
   *   returned something else than #NULL. Signal handlers are invoked in
   *   the order they were connected in.
   *   Don't connect signal handlers with the #G_CONNECT_AFTER flag to this
   *   signal, they will never be invoked!
   * </note>
   *
   * Returns: A new sorted array of #GstElementFactory objects.
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_SORT] =
      g_signal_new ("autoplug-sort", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_sort),
      _gst_array_hasvalue_accumulator, NULL,
      g_cclosure_marshal_generic, G_TYPE_VALUE_ARRAY, 3, GST_TYPE_PAD,
      GST_TYPE_CAPS, G_TYPE_VALUE_ARRAY | G_SIGNAL_TYPE_STATIC_SCOPE);

  /**
   * GstDecodeBin::autoplug-select:
   * @bin: The decodebin.
   * @pad: The #GstPad.
   * @caps: The #GstCaps.
   * @factory: A #GstElementFactory to use.
   *
   * This signal is emitted once decodebin has found all the possible
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
   * <note>
   *   Only the signal handler that is connected first will ever by invoked.
   *   Don't connect signal handlers with the #G_CONNECT_AFTER flag to this
   *   signal, they will never be invoked!
   * </note>
   *
   * Returns: a #GST_TYPE_AUTOPLUG_SELECT_RESULT that indicates the required
   * operation. the default handler will always return
   * #GST_AUTOPLUG_SELECT_TRY.
   */
  gst_decode_bin_signals[SIGNAL_AUTOPLUG_SELECT] =
      g_signal_new ("autoplug-select", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, autoplug_select),
      _gst_select_accumulator, NULL,
      g_cclosure_marshal_generic,
      GST_TYPE_AUTOPLUG_SELECT_RESULT, 3, GST_TYPE_PAD, GST_TYPE_CAPS,
      GST_TYPE_ELEMENT_FACTORY);

  /**
   * GstDecodeBin::drained
   * @bin: The decodebin
   *
   * This signal is emitted once decodebin has finished decoding all the data.
   *
   * Since: 0.10.16
   */
  gst_decode_bin_signals[SIGNAL_DRAINED] =
      g_signal_new ("drained", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodeBinClass, drained),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);

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

  g_object_class_install_property (gobject_klass, PROP_SINK_CAPS,
      g_param_spec_boxed ("sink-caps", "Sink Caps",
          "The caps of the input data. (NULL = use typefind element)",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecodeBin::use-buffering
   *
   * Activate buffering in decodebin. This will instruct the multiqueues behind
   * decoders to emit BUFFERING messages.

   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_klass, PROP_USE_BUFFERING,
      g_param_spec_boolean ("use-buffering", "Use Buffering",
          "Emit GST_MESSAGE_BUFFERING based on low-/high-percent thresholds",
          DEFAULT_USE_BUFFERING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecodeBin:low-percent
   *
   * Low threshold percent for buffering to start.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_klass, PROP_LOW_PERCENT,
      g_param_spec_int ("low-percent", "Low percent",
          "Low threshold for buffering to start", 0, 100,
          DEFAULT_LOW_PERCENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstDecodeBin:high-percent
   *
   * High threshold percent for buffering to finish.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_klass, PROP_HIGH_PERCENT,
      g_param_spec_int ("high-percent", "High percent",
          "High threshold for buffering to finish", 0, 100,
          DEFAULT_HIGH_PERCENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecodeBin:max-size-bytes
   *
   * Max amount of bytes in the queue (0=automatic).
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_klass, PROP_MAX_SIZE_BYTES,
      g_param_spec_uint ("max-size-bytes", "Max. size (bytes)",
          "Max. amount of bytes in the queue (0=automatic)",
          0, G_MAXUINT, DEFAULT_MAX_SIZE_BYTES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstDecodeBin:max-size-buffers
   *
   * Max amount of buffers in the queue (0=automatic).
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_klass, PROP_MAX_SIZE_BUFFERS,
      g_param_spec_uint ("max-size-buffers", "Max. size (buffers)",
          "Max. number of buffers in the queue (0=automatic)",
          0, G_MAXUINT, DEFAULT_MAX_SIZE_BUFFERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstDecodeBin:max-size-time
   *
   * Max amount of time in the queue (in ns, 0=automatic).
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_klass, PROP_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max. size (ns)",
          "Max. amount of data in the queue (in ns, 0=automatic)",
          0, G_MAXUINT64,
          DEFAULT_MAX_SIZE_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecodeBin::post-stream-topology
   *
   * Post stream-topology messages on the bus every time the topology changes.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_klass, PROP_POST_STREAM_TOPOLOGY,
      g_param_spec_boolean ("post-stream-topology", "Post Stream Topology",
          "Post stream-topology messages",
          DEFAULT_POST_STREAM_TOPOLOGY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecodeBin::expose-all-streams
   *
   * Expose streams of unknown type.
   *
   * If set to %FALSE, then only the streams that can be decoded to the final
   * caps (see 'caps' property) will have a pad exposed. Streams that do not
   * match those caps but could have been decoded will not have decoder plugged
   * in internally and will not have a pad exposed.
   *
   * Since: 0.10.30
   */
  g_object_class_install_property (gobject_klass, PROP_EXPOSE_ALL_STREAMS,
      g_param_spec_boolean ("expose-all-streams", "Expose All Streams",
          "Expose all streams, including those of unknown type or that don't match the 'caps' property",
          DEFAULT_EXPOSE_ALL_STREAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecodeBin2::connection-speed
   *
   * Network connection speed in kbps (0 = unknownw)
   *
   * Since: 0.10.XX
   */
  g_object_class_install_property (gobject_klass, PROP_CONNECTION_SPEED,
      g_param_spec_uint64 ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXUINT64 / 1000, DEFAULT_CONNECTION_SPEED,
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

  gst_element_class_set_static_metadata (gstelement_klass,
      "Decoder Bin", "Generic/Bin/Decoder",
      "Autoplug and decode to raw media",
      "Edward Hervey <edward.hervey@collabora.co.uk>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_decode_bin_change_state);

  gstbin_klass->handle_message =
      GST_DEBUG_FUNCPTR (gst_decode_bin_handle_message);

  g_type_class_ref (GST_TYPE_DECODE_PAD);
}

gint
_decode_bin_compare_factories_func (gconstpointer p1, gconstpointer p2)
{
  GstPluginFeature *f1, *f2;
  gint diff;
  gboolean is_parser1, is_parser2;

  f1 = (GstPluginFeature *) p1;
  f2 = (GstPluginFeature *) p2;

  is_parser1 = gst_element_factory_list_is_type (GST_ELEMENT_FACTORY_CAST (f1),
      GST_ELEMENT_FACTORY_TYPE_PARSER);
  is_parser2 = gst_element_factory_list_is_type (GST_ELEMENT_FACTORY_CAST (f2),
      GST_ELEMENT_FACTORY_TYPE_PARSER);


  /* We want all parsers first as we always want to plug parsers
   * before decoders */
  if (is_parser1 && !is_parser2)
    return -1;
  else if (!is_parser1 && is_parser2)
    return 1;

  /* And if it's a both a parser we first sort by rank
   * and then by factory name */
  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;

  diff = strcmp (GST_OBJECT_NAME (f2), GST_OBJECT_NAME (f1));

  return diff;
}

/* Must be called with factories lock! */
static void
gst_decode_bin_update_factories_list (GstDecodeBin * dbin)
{
  guint cookie;

  cookie = gst_registry_get_feature_list_cookie (gst_registry_get ());
  if (!dbin->factories || dbin->factories_cookie != cookie) {
    if (dbin->factories)
      gst_plugin_feature_list_free (dbin->factories);
    dbin->factories =
        gst_element_factory_list_get_elements
        (GST_ELEMENT_FACTORY_TYPE_DECODABLE, GST_RANK_MARGINAL);
    dbin->factories =
        g_list_sort (dbin->factories, _decode_bin_compare_factories_func);
    dbin->factories_cookie = cookie;
  }
}

static void
gst_decode_bin_init (GstDecodeBin * decode_bin)
{
  GST_OBJECT_FLAG_SET (decode_bin, GST_BIN_FLAG_NO_RESYNC);

  /* first filter out the interesting element factories */
  decode_bin->factories_lock = g_mutex_new ();

  /* we create the typefind element only once */
  decode_bin->typefind = gst_element_factory_make ("typefind", "typefind");
  if (!decode_bin->typefind) {
    g_warning ("can't find typefind element, decodebin will not work");
  } else {
    GstPad *pad;
    GstPad *gpad;
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
        g_signal_connect (G_OBJECT (decode_bin->typefind), "have-type",
        G_CALLBACK (type_found), decode_bin);
  }

  decode_bin->expose_lock = g_mutex_new ();
  decode_bin->decode_chain = NULL;

  decode_bin->dyn_lock = g_mutex_new ();
  decode_bin->shutdown = FALSE;
  decode_bin->blocked_pads = NULL;

  decode_bin->subtitle_lock = g_mutex_new ();

  decode_bin->encoding = g_strdup (DEFAULT_SUBTITLE_ENCODING);
  decode_bin->caps = gst_static_caps_get (&default_raw_caps);
  decode_bin->use_buffering = DEFAULT_USE_BUFFERING;
  decode_bin->low_percent = DEFAULT_LOW_PERCENT;
  decode_bin->high_percent = DEFAULT_HIGH_PERCENT;

  decode_bin->max_size_bytes = DEFAULT_MAX_SIZE_BYTES;
  decode_bin->max_size_buffers = DEFAULT_MAX_SIZE_BUFFERS;
  decode_bin->max_size_time = DEFAULT_MAX_SIZE_TIME;

  decode_bin->expose_allstreams = DEFAULT_EXPOSE_ALL_STREAMS;
  decode_bin->connection_speed = DEFAULT_CONNECTION_SPEED;
}

static void
gst_decode_bin_dispose (GObject * object)
{
  GstDecodeBin *decode_bin;

  decode_bin = GST_DECODE_BIN (object);

  if (decode_bin->factories)
    gst_plugin_feature_list_free (decode_bin->factories);
  decode_bin->factories = NULL;

  if (decode_bin->decode_chain)
    gst_decode_chain_free (decode_bin->decode_chain);
  decode_bin->decode_chain = NULL;

  if (decode_bin->caps)
    gst_caps_unref (decode_bin->caps);
  decode_bin->caps = NULL;

  g_free (decode_bin->encoding);
  decode_bin->encoding = NULL;

  g_list_free (decode_bin->subtitles);
  decode_bin->subtitles = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decode_bin_finalize (GObject * object)
{
  GstDecodeBin *decode_bin;

  decode_bin = GST_DECODE_BIN (object);

  if (decode_bin->expose_lock) {
    g_mutex_free (decode_bin->expose_lock);
    decode_bin->expose_lock = NULL;
  }

  if (decode_bin->dyn_lock) {
    g_mutex_free (decode_bin->dyn_lock);
    decode_bin->dyn_lock = NULL;
  }

  if (decode_bin->subtitle_lock) {
    g_mutex_free (decode_bin->subtitle_lock);
    decode_bin->subtitle_lock = NULL;
  }

  if (decode_bin->factories_lock) {
    g_mutex_free (decode_bin->factories_lock);
    decode_bin->factories_lock = NULL;
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
  gst_caps_replace (&dbin->caps, caps);
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
gst_decode_bin_set_subs_encoding (GstDecodeBin * dbin, const gchar * encoding)
{
  GList *walk;

  GST_DEBUG_OBJECT (dbin, "Setting new encoding: %s", GST_STR_NULL (encoding));

  SUBTITLE_LOCK (dbin);
  g_free (dbin->encoding);
  dbin->encoding = g_strdup (encoding);

  /* set the subtitle encoding on all added elements */
  for (walk = dbin->subtitles; walk; walk = g_list_next (walk)) {
    g_object_set (G_OBJECT (walk->data), "subtitle-encoding", dbin->encoding,
        NULL);
  }
  SUBTITLE_UNLOCK (dbin);
}

static gchar *
gst_decode_bin_get_subs_encoding (GstDecodeBin * dbin)
{
  gchar *encoding;

  GST_DEBUG_OBJECT (dbin, "Getting currently set encoding");

  SUBTITLE_LOCK (dbin);
  encoding = g_strdup (dbin->encoding);
  SUBTITLE_UNLOCK (dbin);

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
    case PROP_SINK_CAPS:
      gst_decode_bin_set_sink_caps (dbin, g_value_get_boxed (value));
      break;
    case PROP_USE_BUFFERING:
      dbin->use_buffering = g_value_get_boolean (value);
      break;
    case PROP_LOW_PERCENT:
      dbin->low_percent = g_value_get_int (value);
      break;
    case PROP_HIGH_PERCENT:
      dbin->high_percent = g_value_get_int (value);
      break;
    case PROP_MAX_SIZE_BYTES:
      dbin->max_size_bytes = g_value_get_uint (value);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      dbin->max_size_buffers = g_value_get_uint (value);
      break;
    case PROP_MAX_SIZE_TIME:
      dbin->max_size_time = g_value_get_uint64 (value);
      break;
    case PROP_POST_STREAM_TOPOLOGY:
      dbin->post_stream_topology = g_value_get_boolean (value);
      break;
    case PROP_EXPOSE_ALL_STREAMS:
      dbin->expose_allstreams = g_value_get_boolean (value);
      break;
    case PROP_CONNECTION_SPEED:
      GST_OBJECT_LOCK (dbin);
      dbin->connection_speed = g_value_get_uint64 (value) * 1000;
      GST_OBJECT_UNLOCK (dbin);
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
    case PROP_SINK_CAPS:
      g_value_take_boxed (value, gst_decode_bin_get_sink_caps (dbin));
      break;
    case PROP_USE_BUFFERING:
      g_value_set_boolean (value, dbin->use_buffering);
      break;
    case PROP_LOW_PERCENT:
      g_value_set_int (value, dbin->low_percent);
      break;
    case PROP_HIGH_PERCENT:
      g_value_set_int (value, dbin->high_percent);
      break;
    case PROP_MAX_SIZE_BYTES:
      g_value_set_uint (value, dbin->max_size_bytes);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      g_value_set_uint (value, dbin->max_size_buffers);
      break;
    case PROP_MAX_SIZE_TIME:
      g_value_set_uint64 (value, dbin->max_size_time);
      break;
    case PROP_POST_STREAM_TOPOLOGY:
      g_value_set_boolean (value, dbin->post_stream_topology);
      break;
    case PROP_EXPOSE_ALL_STREAMS:
      g_value_set_boolean (value, dbin->expose_allstreams);
      break;
    case PROP_CONNECTION_SPEED:
      GST_OBJECT_LOCK (dbin);
      g_value_set_uint64 (value, dbin->connection_speed / 1000);
      GST_OBJECT_UNLOCK (dbin);
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
  GList *list, *tmp;
  GValueArray *result;
  GstDecodeBin *dbin = GST_DECODE_BIN_CAST (element);

  GST_DEBUG_OBJECT (element, "finding factories");

  /* return all compatible factories for caps */
  g_mutex_lock (dbin->factories_lock);
  gst_decode_bin_update_factories_list (dbin);
  list =
      gst_element_factory_list_filter (dbin->factories, caps, GST_PAD_SINK,
      FALSE);
  g_mutex_unlock (dbin->factories_lock);

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
gst_decode_bin_autoplug_sort (GstElement * element, GstPad * pad,
    GstCaps * caps, GValueArray * factories)
{
  return NULL;
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

static gboolean are_final_caps (GstDecodeBin * dbin, GstCaps * caps);
static gboolean is_demuxer_element (GstElement * srcelement);

static gboolean connect_pad (GstDecodeBin * dbin, GstElement * src,
    GstDecodePad * dpad, GstPad * pad, GstCaps * caps, GValueArray * factories,
    GstDecodeChain * chain);
static gboolean connect_element (GstDecodeBin * dbin, GstElement * element,
    GstDecodeChain * chain);
static void expose_pad (GstDecodeBin * dbin, GstElement * src,
    GstDecodePad * dpad, GstPad * pad, GstCaps * caps, GstDecodeChain * chain);

static void pad_added_cb (GstElement * element, GstPad * pad,
    GstDecodeChain * chain);
static void pad_removed_cb (GstElement * element, GstPad * pad,
    GstDecodeChain * chain);
static void no_more_pads_cb (GstElement * element, GstDecodeChain * chain);

static GstDecodeGroup *gst_decode_chain_get_current_group (GstDecodeChain *
    chain);

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
    GstCaps * caps, GstDecodeChain * chain)
{
  gboolean apcontinue = TRUE;
  GValueArray *factories = NULL, *result = NULL;
  GstDecodePad *dpad;
  GstElementFactory *factory;
  const gchar *classification;
  gboolean is_parser_converter = FALSE;
  gboolean res;

  GST_DEBUG_OBJECT (dbin, "Pad %s:%s caps:%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  if (chain->elements
      && src != ((GstDecodeElement *) chain->elements->data)->element
      && src != ((GstDecodeElement *) chain->elements->data)->capsfilter) {
    GST_ERROR_OBJECT (dbin, "New pad from not the last element in this chain");
    return;
  }

  if (chain->endpad) {
    GST_ERROR_OBJECT (dbin, "New pad in a chain that is already complete");
    return;
  }

  if (chain->demuxer) {
    GstDecodeGroup *group;
    GstDecodeChain *oldchain = chain;

    /* we are adding a new pad for a demuxer (see is_demuxer_element(),
     * start a new chain for it */
    CHAIN_MUTEX_LOCK (oldchain);
    group = gst_decode_chain_get_current_group (chain);
    if (group) {
      chain = gst_decode_chain_new (dbin, group, pad);
      group->children = g_list_prepend (group->children, chain);
    }
    CHAIN_MUTEX_UNLOCK (oldchain);
    if (!group) {
      GST_WARNING_OBJECT (dbin, "No current group");
      return;
    }
  }

  if ((caps == NULL) || gst_caps_is_empty (caps))
    goto unknown_type;

  if (gst_caps_is_any (caps))
    goto any_caps;

  dpad = gst_decode_pad_new (dbin, pad, chain);

  /* 1. Emit 'autoplug-continue' the result will tell us if this pads needs
   * further autoplugging. Only do this for fixed caps, for unfixed caps
   * we will later come here again from the notify::caps handler. The
   * problem with unfixed caps is that we can reliably tell if the output
   * is e.g. accepted by a sink because only parts of the possible final
   * caps might be accepted by the sink. */
  if (gst_caps_is_fixed (caps))
    g_signal_emit (G_OBJECT (dbin),
        gst_decode_bin_signals[SIGNAL_AUTOPLUG_CONTINUE], 0, dpad, caps,
        &apcontinue);
  else
    apcontinue = TRUE;

  /* 1.a if autoplug-continue is FALSE or caps is a raw format, goto pad_is_final */
  if ((!apcontinue) || are_final_caps (dbin, caps))
    goto expose_pad;

  /* 1.b For Parser/Converter that can output different stream formats
   * we insert a capsfilter with the sorted caps of all possible next
   * elements and continue with the capsfilter srcpad */
  factory = gst_element_get_factory (src);
  classification =
      gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);
  is_parser_converter = (strstr (classification, "Parser")
      && strstr (classification, "Converter"));

  /* 1.c when the caps are not fixed yet, we can't be sure what element to
   * connect. We delay autoplugging until the caps are fixed */
  if (!is_parser_converter && !gst_caps_is_fixed (caps))
    goto non_fixed;

  /* 1.d else get the factories and if there's no compatible factory goto
   * unknown_type */
  g_signal_emit (G_OBJECT (dbin),
      gst_decode_bin_signals[SIGNAL_AUTOPLUG_FACTORIES], 0, dpad, caps,
      &factories);

  /* NULL means that we can expose the pad */
  if (factories == NULL)
    goto expose_pad;

  /* if the array is empty, we have a type for which we have no decoder */
  if (factories->n_values == 0) {
    if (!dbin->expose_allstreams) {
      GstCaps *raw = gst_static_caps_get (&default_raw_caps);

      /* If the caps are raw, this just means we don't want to expose them */
      if (gst_caps_can_intersect (raw, caps)) {
        g_value_array_free (factories);
        gst_caps_unref (raw);
        gst_object_unref (dpad);
        goto discarded_type;
      }
      gst_caps_unref (raw);
    }

    /* if not we have a unhandled type with no compatible factories */
    g_value_array_free (factories);
    gst_object_unref (dpad);
    goto unknown_type;
  }

  /* 1.e sort some more. */
  g_signal_emit (G_OBJECT (dbin),
      gst_decode_bin_signals[SIGNAL_AUTOPLUG_SORT], 0, dpad, caps, factories,
      &result);
  if (result) {
    g_value_array_free (factories);
    factories = result;
  }

  /* At this point we have a potential decoder, but we might not need it
   * if it doesn't match the output caps  */
  if (!dbin->expose_allstreams && gst_caps_is_fixed (caps)) {
    guint i;
    const GList *tmps;
    gboolean dontuse = FALSE;

    GST_DEBUG ("Checking if we can abort early");

    /* 1.f Do an early check to see if the candidates are potential decoders, but
     * due to the fact that they decode to a mediatype that is not final we don't
     * need them */

    for (i = 0; i < factories->n_values && !dontuse; i++) {
      GstElementFactory *factory =
          g_value_get_object (g_value_array_get_nth (factories, i));
      GstCaps *tcaps;

      /* We are only interested in skipping decoders */
      if (strstr (gst_element_factory_get_metadata (factory,
                  GST_ELEMENT_METADATA_KLASS), "Decoder")) {

        GST_DEBUG ("Trying factory %s",
            gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

        /* Check the source pad template caps to see if they match raw caps but don't match
         * our final caps*/
        for (tmps = gst_element_factory_get_static_pad_templates (factory);
            tmps && !dontuse; tmps = tmps->next) {
          GstStaticPadTemplate *st = (GstStaticPadTemplate *) tmps->data;
          if (st->direction != GST_PAD_SRC)
            continue;
          tcaps = gst_static_pad_template_get_caps (st);

          apcontinue = TRUE;

          /* Emit autoplug-continue to see if the caps are considered to be raw caps */
          g_signal_emit (G_OBJECT (dbin),
              gst_decode_bin_signals[SIGNAL_AUTOPLUG_CONTINUE], 0, dpad, tcaps,
              &apcontinue);

          /* If autoplug-continue returns TRUE and the caps are not final, don't use them */
          if (apcontinue && !are_final_caps (dbin, tcaps))
            dontuse = TRUE;
          gst_caps_unref (tcaps);
        }
      }
    }

    if (dontuse) {
      gst_object_unref (dpad);
      g_value_array_free (factories);
      goto discarded_type;
    }
  }

  /* 1.g now get the factory template caps and insert the capsfilter if this
   * is a parser/converter
   */
  if (is_parser_converter) {
    GstCaps *filter_caps;
    gint i;
    GstPad *p;
    GstDecodeElement *delem;

    g_assert (chain->elements != NULL);
    delem = (GstDecodeElement *) chain->elements->data;

    filter_caps = gst_caps_new_empty ();
    for (i = 0; i < factories->n_values; i++) {
      GstElementFactory *factory =
          g_value_get_object (g_value_array_get_nth (factories, i));
      GstCaps *tcaps, *intersection;
      const GList *tmps;

      GST_DEBUG ("Trying factory %s",
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

      if (gst_element_get_factory (src) == factory) {
        GST_DEBUG ("Skipping factory");
        continue;
      }

      for (tmps = gst_element_factory_get_static_pad_templates (factory); tmps;
          tmps = tmps->next) {
        GstStaticPadTemplate *st = (GstStaticPadTemplate *) tmps->data;
        if (st->direction != GST_PAD_SINK || st->presence != GST_PAD_ALWAYS)
          continue;
        tcaps = gst_static_pad_template_get_caps (st);
        intersection =
            gst_caps_intersect_full (tcaps, caps, GST_CAPS_INTERSECT_FIRST);
        filter_caps = gst_caps_merge (filter_caps, intersection);
        gst_caps_unref (tcaps);
      }
    }

    /* Append the parser caps to prevent any not-negotiated errors */
    filter_caps = gst_caps_merge (filter_caps, gst_caps_ref (caps));

    delem->capsfilter = gst_element_factory_make ("capsfilter", NULL);
    g_object_set (G_OBJECT (delem->capsfilter), "caps", filter_caps, NULL);
    gst_caps_unref (filter_caps);
    gst_element_set_state (delem->capsfilter, GST_STATE_PAUSED);
    gst_bin_add (GST_BIN_CAST (dbin), gst_object_ref (delem->capsfilter));

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), NULL);
    p = gst_element_get_static_pad (delem->capsfilter, "sink");
    gst_pad_link_full (pad, p, GST_PAD_LINK_CHECK_NOTHING);
    gst_object_unref (p);
    p = gst_element_get_static_pad (delem->capsfilter, "src");
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), p);
    pad = p;

    if (!gst_caps_is_fixed (caps)) {
      g_value_array_free (factories);
      goto non_fixed;
    }
  }

  /* 1.h else continue autoplugging something from the list. */
  GST_LOG_OBJECT (pad, "Let's continue discovery on this pad");
  res = connect_pad (dbin, src, dpad, pad, caps, factories, chain);

  /* Need to unref the capsfilter srcpad here if
   * we inserted a capsfilter */
  if (is_parser_converter)
    gst_object_unref (pad);

  gst_object_unref (dpad);
  g_value_array_free (factories);

  if (!res)
    goto unknown_type;

  return;

expose_pad:
  {
    GST_LOG_OBJECT (dbin, "Pad is final. autoplug-continue:%d", apcontinue);
    expose_pad (dbin, src, dpad, pad, caps, chain);
    gst_object_unref (dpad);
    return;
  }

discarded_type:
  {
    GST_LOG_OBJECT (pad, "Known type, but discarded because not final caps");
    chain->deadend = TRUE;
    chain->endcaps = gst_caps_ref (caps);

    /* Try to expose anything */
    EXPOSE_LOCK (dbin);
    if (gst_decode_chain_is_complete (dbin->decode_chain)) {
      gst_decode_bin_expose (dbin);
    }
    EXPOSE_UNLOCK (dbin);
    do_async_done (dbin);

    return;
  }

unknown_type:
  {
    GST_LOG_OBJECT (pad, "Unknown type, posting message and firing signal");

    chain->deadend = TRUE;
    chain->endcaps = gst_caps_ref (caps);

    gst_element_post_message (GST_ELEMENT_CAST (dbin),
        gst_missing_decoder_message_new (GST_ELEMENT_CAST (dbin), caps));

    g_signal_emit (G_OBJECT (dbin),
        gst_decode_bin_signals[SIGNAL_UNKNOWN_TYPE], 0, pad, caps);

    /* Try to expose anything */
    EXPOSE_LOCK (dbin);
    if (gst_decode_chain_is_complete (dbin->decode_chain)) {
      gst_decode_bin_expose (dbin);
    }
    EXPOSE_UNLOCK (dbin);

    if (src == dbin->typefind) {
      gchar *desc;

      if (caps && !gst_caps_is_empty (caps)) {
        desc = gst_pb_utils_get_decoder_description (caps);
        GST_ELEMENT_ERROR (dbin, STREAM, CODEC_NOT_FOUND,
            (_("A %s plugin is required to play this stream, "
                    "but not installed."), desc),
            ("No decoder to handle media type '%s'",
                gst_structure_get_name (gst_caps_get_structure (caps, 0))));
        g_free (desc);
      } else {
        GST_ELEMENT_ERROR (dbin, STREAM, TYPE_NOT_FOUND,
            (_("Could not determine type of stream")),
            ("Stream caps %" GST_PTR_FORMAT, caps));
      }
      do_async_done (dbin);
    }
    return;
  }
non_fixed:
  {
    GST_DEBUG_OBJECT (pad, "pad has non-fixed caps delay autoplugging");
    gst_object_unref (dpad);
    goto setup_caps_delay;
  }
any_caps:
  {
    GST_DEBUG_OBJECT (pad, "pad has ANY caps, delaying auto-pluggin");
    goto setup_caps_delay;
  }
setup_caps_delay:
  {
    GstPendingPad *ppad;

    /* connect to caps notification */
    CHAIN_MUTEX_LOCK (chain);
    GST_LOG_OBJECT (dbin, "Chain %p has now %d dynamic pads", chain,
        g_list_length (chain->pending_pads));
    ppad = g_slice_new0 (GstPendingPad);
    ppad->pad = gst_object_ref (pad);
    ppad->chain = chain;
    ppad->event_probe_id =
        gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        pad_event_cb, ppad, NULL);
    chain->pending_pads = g_list_prepend (chain->pending_pads, ppad);
    g_signal_connect (G_OBJECT (pad), "notify::caps",
        G_CALLBACK (caps_notify_cb), chain);
    CHAIN_MUTEX_UNLOCK (chain);

    /* If we're here because we have a Parser/Converter
     * we have to unref the pad */
    if (is_parser_converter)
      gst_object_unref (pad);

    return;
  }
}

static void
add_error_filter (GstDecodeBin * dbin, GstElement * element)
{
  GST_OBJECT_LOCK (dbin);
  dbin->filtered = g_list_prepend (dbin->filtered, element);
  GST_OBJECT_UNLOCK (dbin);
}

static void
remove_error_filter (GstDecodeBin * dbin, GstElement * element)
{
  GST_OBJECT_LOCK (dbin);
  dbin->filtered = g_list_remove (dbin->filtered, element);
  GST_OBJECT_UNLOCK (dbin);
}

/* connect_pad:
 *
 * Try to connect the given pad to an element created from one of the factories,
 * and recursively.
 *
 * Note that dpad is ghosting pad, and so pad is linked; be sure to unset dpad's
 * target before trying to link pad.
 *
 * Returns TRUE if an element was properly created and linked
 */
static gboolean
connect_pad (GstDecodeBin * dbin, GstElement * src, GstDecodePad * dpad,
    GstPad * pad, GstCaps * caps, GValueArray * factories,
    GstDecodeChain * chain)
{
  gboolean res = FALSE;
  GstPad *mqpad = NULL;
  gboolean is_demuxer = chain->parent && !chain->elements;      /* First pad after the demuxer */

  g_return_val_if_fail (factories != NULL, FALSE);
  g_return_val_if_fail (factories->n_values > 0, FALSE);

  GST_DEBUG_OBJECT (dbin,
      "pad %s:%s , chain:%p, %d factories, caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), chain, factories->n_values, caps);

  /* 1. is element demuxer or parser */
  if (is_demuxer) {
    GST_LOG_OBJECT (src,
        "is a demuxer, connecting the pad through multiqueue '%s'",
        GST_OBJECT_NAME (chain->parent->multiqueue));

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), NULL);
    if (!(mqpad = gst_decode_group_control_demuxer_pad (chain->parent, pad)))
      goto beach;
    src = chain->parent->multiqueue;
    pad = mqpad;
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), pad);
  }

  /* 2. Try to create an element and link to it */
  while (factories->n_values > 0) {
    GstAutoplugSelectResult ret;
    GstElementFactory *factory;
    GstDecodeElement *delem;
    GstElement *element;
    GstPad *sinkpad;
    GParamSpec *pspec;
    gboolean subtitle;

    /* Set dpad target to pad again, it might've been unset
     * below but we came back here because something failed
     */
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), pad);

    /* take first factory */
    factory = g_value_get_object (g_value_array_get_nth (factories, 0));
    /* Remove selected factory from the list. */
    g_value_array_remove (factories, 0);

    GST_LOG_OBJECT (src, "trying factory %" GST_PTR_FORMAT, factory);

    /* Check if the caps are really supported by the factory. The
     * factory list is non-empty-subset filtered while caps
     * are only accepted by a pad if they are a subset of the
     * pad caps.
     *
     * FIXME: Only do this for fixed caps here. Non-fixed caps
     * can happen if a Parser/Converter was autoplugged before
     * this. We then assume that it will be able to convert to
     * everything that the decoder would want.
     *
     * A subset check will fail here because the parser caps
     * will be generic and while the decoder will only
     * support a subset of the parser caps.
     */
    if (gst_caps_is_fixed (caps)) {
      const GList *templs;
      gboolean skip = FALSE;

      templs = gst_element_factory_get_static_pad_templates (factory);

      while (templs) {
        GstStaticPadTemplate *templ = (GstStaticPadTemplate *) templs->data;

        if (templ->direction == GST_PAD_SINK) {
          GstCaps *templcaps = gst_static_caps_get (&templ->static_caps);

          if (!gst_caps_is_subset (caps, templcaps)) {
            GST_DEBUG_OBJECT (src,
                "caps %" GST_PTR_FORMAT " not subset of %" GST_PTR_FORMAT, caps,
                templcaps);
            gst_caps_unref (templcaps);
            skip = TRUE;
            break;
          }

          gst_caps_unref (templcaps);
        }
        templs = g_list_next (templs);
      }
      if (skip)
        continue;
    }

    /* If the factory is for a parser we first check if the factory
     * was already used for the current chain. If it was used already
     * we would otherwise create an infinite loop here because the
     * parser apparently accepts its own output as input.
     * This is only done for parsers because it's perfectly valid
     * to have other element classes after each other because a
     * parser is the only one that does not change the data. A
     * valid example for this would be multiple id3demux in a row.
     */
    if (strstr (gst_element_factory_get_metadata (factory,
                GST_ELEMENT_METADATA_KLASS), "Parser")) {
      gboolean skip = FALSE;
      GList *l;

      CHAIN_MUTEX_LOCK (chain);
      for (l = chain->elements; l; l = l->next) {
        GstDecodeElement *delem = (GstDecodeElement *) l->data;
        GstElement *otherelement = delem->element;

        if (gst_element_get_factory (otherelement) == factory) {
          skip = TRUE;
          break;
        }
      }
      CHAIN_MUTEX_UNLOCK (chain);
      if (skip) {
        GST_DEBUG_OBJECT (dbin,
            "Skipping factory '%s' because it was already used in this chain",
            gst_plugin_feature_get_name (GST_PLUGIN_FEATURE_CAST (factory)));
        continue;
      }
    }

    /* emit autoplug-select to see what we should do with it. */
    g_signal_emit (G_OBJECT (dbin),
        gst_decode_bin_signals[SIGNAL_AUTOPLUG_SELECT],
        0, dpad, caps, factory, &ret);

    switch (ret) {
      case GST_AUTOPLUG_SELECT_TRY:
        GST_DEBUG_OBJECT (dbin, "autoplug select requested try");
        break;
      case GST_AUTOPLUG_SELECT_EXPOSE:
        GST_DEBUG_OBJECT (dbin, "autoplug select requested expose");
        /* expose the pad, we don't have the source element */
        expose_pad (dbin, src, dpad, pad, caps, chain);
        res = TRUE;
        goto beach;
      case GST_AUTOPLUG_SELECT_SKIP:
        GST_DEBUG_OBJECT (dbin, "autoplug select requested skip");
        continue;
      default:
        GST_WARNING_OBJECT (dbin, "autoplug select returned unhandled %d", ret);
        break;
    }

    /* 2.0. Unlink pad */
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), NULL);

    /* 2.1. Try to create an element */
    if ((element = gst_element_factory_create (factory, NULL)) == NULL) {
      GST_WARNING_OBJECT (dbin, "Could not create an element from %s",
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
      continue;
    }

    /* Filter errors, this will prevent the element from causing the pipeline
     * to error while we test it using READY state. */
    add_error_filter (dbin, element);

    /* ... add it ... */
    if (!(gst_bin_add (GST_BIN_CAST (dbin), element))) {
      GST_WARNING_OBJECT (dbin, "Couldn't add %s to the bin",
          GST_ELEMENT_NAME (element));
      remove_error_filter (dbin, element);
      gst_object_unref (element);
      continue;
    }

    /* Find its sink pad. */
    if (!(sinkpad = find_sink_pad (element))) {
      GST_WARNING_OBJECT (dbin, "Element %s doesn't have a sink pad",
          GST_ELEMENT_NAME (element));
      remove_error_filter (dbin, element);
      gst_bin_remove (GST_BIN (dbin), element);
      continue;
    }

    /* ... and try to link */
    if ((gst_pad_link (pad, sinkpad)) != GST_PAD_LINK_OK) {
      GST_WARNING_OBJECT (dbin, "Link failed on pad %s:%s",
          GST_DEBUG_PAD_NAME (sinkpad));
      remove_error_filter (dbin, element);
      gst_object_unref (sinkpad);
      gst_bin_remove (GST_BIN (dbin), element);
      continue;
    }

    /* ... activate it ... */
    if ((gst_element_set_state (element,
                GST_STATE_READY)) == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (dbin, "Couldn't set %s to READY",
          GST_ELEMENT_NAME (element));
      remove_error_filter (dbin, element);
      gst_object_unref (sinkpad);
      gst_bin_remove (GST_BIN (dbin), element);
      continue;
    }

    /* Stop filtering errors. */
    remove_error_filter (dbin, element);

    gst_object_unref (sinkpad);
    GST_LOG_OBJECT (dbin, "linked on pad %s:%s", GST_DEBUG_PAD_NAME (pad));

    CHAIN_MUTEX_LOCK (chain);
    delem = g_slice_new (GstDecodeElement);
    delem->element = gst_object_ref (element);
    delem->capsfilter = NULL;
    chain->elements = g_list_prepend (chain->elements, delem);
    chain->demuxer = is_demuxer_element (element);
    CHAIN_MUTEX_UNLOCK (chain);

    /* Set connection-speed property if needed */
    if (chain->demuxer == TRUE) {
      GParamSpec *pspec;

      if ((pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (element),
                  "connection-speed"))) {
        guint64 speed = dbin->connection_speed / 1000;
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
          GST_WARNING_OBJECT (dbin,
              "The connection speed property %" G_GUINT64_FORMAT " of type %s"
              " is not usefull not setting it", speed,
              g_type_name (G_PARAM_SPEC_TYPE (pspec)));
          wrong_type = TRUE;
        }

        if (wrong_type == FALSE) {
          GST_DEBUG_OBJECT (dbin, "setting connection-speed=%" G_GUINT64_FORMAT
              " to demuxer element", speed);

          g_object_set (element, "connection-speed", speed, NULL);
        }
      }
    }

    /* link this element further */
    connect_element (dbin, element, chain);

    /* try to configure the subtitle encoding property when we can */
    pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (element),
        "subtitle-encoding");
    if (pspec && G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_STRING) {
      SUBTITLE_LOCK (dbin);
      GST_DEBUG_OBJECT (dbin,
          "setting subtitle-encoding=%s to element", dbin->encoding);
      g_object_set (G_OBJECT (element), "subtitle-encoding", dbin->encoding,
          NULL);
      SUBTITLE_UNLOCK (dbin);
      subtitle = TRUE;
    } else {
      subtitle = FALSE;
    }

    /* Bring the element to the state of the parent */
    if ((gst_element_set_state (element,
                GST_STATE_PAUSED)) == GST_STATE_CHANGE_FAILURE) {
      GstDecodeElement *dtmp = NULL;
      GstElement *tmp = NULL;

      GST_WARNING_OBJECT (dbin, "Couldn't set %s to PAUSED",
          GST_ELEMENT_NAME (element));

      /* Remove all elements in this chain that were just added. No
       * other thread could've added elements in the meantime */
      CHAIN_MUTEX_LOCK (chain);
      do {
        GList *l;

        dtmp = chain->elements->data;
        tmp = dtmp->element;

        /* Disconnect any signal handlers that might be connected
         * in connect_element() or analyze_pad() */
        g_signal_handlers_disconnect_by_func (tmp, pad_added_cb, chain);
        g_signal_handlers_disconnect_by_func (tmp, pad_removed_cb, chain);
        g_signal_handlers_disconnect_by_func (tmp, no_more_pads_cb, chain);

        for (l = chain->pending_pads; l;) {
          GstPendingPad *pp = l->data;
          GList *n;

          if (GST_PAD_PARENT (pp->pad) != tmp) {
            l = l->next;
            continue;
          }

          g_signal_handlers_disconnect_by_func (pp->pad, caps_notify_cb, chain);
          gst_pad_remove_probe (pp->pad, pp->event_probe_id);
          gst_object_unref (pp->pad);
          g_slice_free (GstPendingPad, pp);

          /* Remove element from the list, update list head and go to the
           * next element in the list */
          n = l->next;
          chain->pending_pads = g_list_delete_link (chain->pending_pads, l);
          l = n;
        }

        if (dtmp->capsfilter) {
          gst_bin_remove (GST_BIN (dbin), dtmp->capsfilter);
          gst_element_set_state (dtmp->capsfilter, GST_STATE_NULL);
          gst_object_unref (dtmp->capsfilter);
        }

        gst_bin_remove (GST_BIN (dbin), tmp);
        gst_element_set_state (tmp, GST_STATE_NULL);

        gst_object_unref (tmp);
        g_slice_free (GstDecodeElement, dtmp);

        chain->elements = g_list_delete_link (chain->elements, chain->elements);
      } while (tmp != element);
      CHAIN_MUTEX_UNLOCK (chain);

      continue;
    }
    if (subtitle) {
      SUBTITLE_LOCK (dbin);
      /* we added the element now, add it to the list of subtitle-encoding
       * elements when we can set the property */
      dbin->subtitles = g_list_prepend (dbin->subtitles, element);
      SUBTITLE_UNLOCK (dbin);
    }

    res = TRUE;
    break;
  }

beach:
  if (mqpad)
    gst_object_unref (mqpad);

  return res;
}

static GstCaps *
get_pad_caps (GstPad * pad)
{
  GstCaps *caps;

  /* first check the pad caps, if this is set, we are positively sure it is
   * fixed and exactly what the element will produce. */
  caps = gst_pad_get_current_caps (pad);

  /* then use the getcaps function if we don't have caps. These caps might not
   * be fixed in some cases, in which case analyze_new_pad will set up a
   * notify::caps signal to continue autoplugging. */
  if (caps == NULL)
    caps = gst_pad_query_caps (pad, NULL);

  return caps;
}

static gboolean
connect_element (GstDecodeBin * dbin, GstElement * element,
    GstDecodeChain * chain)
{
  GList *pads;
  gboolean res = TRUE;
  gboolean dynamic = FALSE;
  GList *to_connect = NULL;

  GST_DEBUG_OBJECT (dbin, "Attempting to connect element %s [chain:%p] further",
      GST_ELEMENT_NAME (element), chain);

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
        GstPad *pad = gst_element_get_static_pad (element, templ_name);

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
        GstPad *pad = gst_element_get_static_pad (element, templ_name);

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

  /* 2. if there are more potential pads, connect to relevant signals */
  if (dynamic) {
    GST_LOG_OBJECT (dbin, "Adding signals to element %s in chain %p",
        GST_ELEMENT_NAME (element), chain);
    g_signal_connect (G_OBJECT (element), "pad-added",
        G_CALLBACK (pad_added_cb), chain);
    g_signal_connect (G_OBJECT (element), "pad-removed",
        G_CALLBACK (pad_removed_cb), chain);
    g_signal_connect (G_OBJECT (element), "no-more-pads",
        G_CALLBACK (no_more_pads_cb), chain);
  }

  /* 3. for every available pad, connect it */
  for (pads = to_connect; pads; pads = g_list_next (pads)) {
    GstPad *pad = GST_PAD_CAST (pads->data);
    GstCaps *caps;

    caps = get_pad_caps (pad);
    analyze_new_pad (dbin, element, pad, caps, chain);
    if (caps)
      gst_caps_unref (caps);

    gst_object_unref (pad);
  }
  g_list_free (to_connect);

  return res;
}

/* expose_pad:
 *
 * Expose the given pad on the chain as a decoded pad.
 */
static void
expose_pad (GstDecodeBin * dbin, GstElement * src, GstDecodePad * dpad,
    GstPad * pad, GstCaps * caps, GstDecodeChain * chain)
{
  GstPad *mqpad = NULL;

  GST_DEBUG_OBJECT (dbin, "pad %s:%s, chain:%p",
      GST_DEBUG_PAD_NAME (pad), chain);

  /* If this is the first pad for this chain, there are no other elements
   * and the source element is not the multiqueue we must link through the
   * multiqueue.
   *
   * This is the case if a demuxer directly exposed a raw pad.
   */
  if (chain->parent && !chain->elements && src != chain->parent->multiqueue) {
    GST_LOG_OBJECT (src, "connecting the pad through multiqueue");

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), NULL);
    if (!(mqpad = gst_decode_group_control_demuxer_pad (chain->parent, pad)))
      goto beach;
    pad = mqpad;
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), pad);
  }

  gst_decode_pad_activate (dpad, chain);
  chain->endpad = gst_object_ref (dpad);
  chain->endcaps = gst_caps_ref (caps);

  EXPOSE_LOCK (dbin);
  if (gst_decode_chain_is_complete (dbin->decode_chain)) {
    gst_decode_bin_expose (dbin);
  }
  EXPOSE_UNLOCK (dbin);

  if (mqpad)
    gst_object_unref (mqpad);

beach:
  return;
}

/* check_upstream_seekable:
 *
 * Check if upstream is seekable.
 */
static gboolean
check_upstream_seekable (GstDecodeBin * dbin, GstPad * pad)
{
  GstQuery *query;
  gint64 start = -1, stop = -1;
  gboolean seekable = FALSE;

  query = gst_query_new_seeking (GST_FORMAT_BYTES);
  if (!gst_pad_peer_query (pad, query)) {
    GST_DEBUG_OBJECT (dbin, "seeking query failed");
    gst_query_unref (query);
    return FALSE;
  }

  gst_query_parse_seeking (query, NULL, &seekable, &start, &stop);

  gst_query_unref (query);

  /* try harder to query upstream size if we didn't get it the first time */
  if (seekable && stop == -1) {
    GST_DEBUG_OBJECT (dbin, "doing duration query to fix up unset stop");
    gst_pad_peer_query_duration (pad, GST_FORMAT_BYTES, &stop);
  }

  /* if upstream doesn't know the size, it's likely that it's not seekable in
   * practice even if it technically may be seekable */
  if (seekable && (start != 0 || stop <= start)) {
    GST_DEBUG_OBJECT (dbin, "seekable but unknown start/stop -> disable");
    return FALSE;
  }

  GST_DEBUG_OBJECT (dbin, "upstream seekable: %d", seekable);
  return seekable;
}

static void
type_found (GstElement * typefind, guint probability,
    GstCaps * caps, GstDecodeBin * decode_bin)
{
  GstPad *pad, *sink_pad;

  GST_DEBUG_OBJECT (decode_bin, "typefind found caps %" GST_PTR_FORMAT, caps);

  /* If the typefinder (but not something else) finds text/plain - i.e. that's
   * the top-level type of the file - then error out.
   */
  if (gst_structure_has_name (gst_caps_get_structure (caps, 0), "text/plain")) {
    GST_ELEMENT_ERROR (decode_bin, STREAM, WRONG_TYPE,
        (_("This appears to be a text file")),
        ("decodebin cannot decode plain text files"));
    goto exit;
  }

  /* FIXME: we can only deal with one type, we don't yet support dynamically changing
   * caps from the typefind element */
  if (decode_bin->have_type || decode_bin->decode_chain)
    goto exit;

  decode_bin->have_type = TRUE;

  pad = gst_element_get_static_pad (typefind, "src");
  sink_pad = gst_element_get_static_pad (typefind, "sink");

  /* need some lock here to prevent race with shutdown state change
   * which might yank away e.g. decode_chain while building stuff here.
   * In typical cases, STREAM_LOCK is held and handles that, it need not
   * be held (if called from a proxied setcaps), so grab it anyway */
  GST_PAD_STREAM_LOCK (sink_pad);
  decode_bin->decode_chain = gst_decode_chain_new (decode_bin, NULL, pad);
  analyze_new_pad (decode_bin, typefind, pad, caps, decode_bin->decode_chain);
  GST_PAD_STREAM_UNLOCK (sink_pad);

  gst_object_unref (sink_pad);
  gst_object_unref (pad);

exit:
  return;
}

static GstPadProbeReturn
pad_event_cb (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstPendingPad *ppad = (GstPendingPad *) data;
  GstDecodeChain *chain = ppad->chain;
  GstDecodeBin *dbin = chain->dbin;

  g_assert (ppad);
  g_assert (chain);
  g_assert (dbin);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (dbin, "Received EOS on a non final pad, this stream "
          "ended too early");
      chain->deadend = TRUE;
      /* we don't set the endcaps because NULL endcaps means early EOS */
      EXPOSE_LOCK (dbin);
      if (gst_decode_chain_is_complete (dbin->decode_chain))
        gst_decode_bin_expose (dbin);
      EXPOSE_UNLOCK (dbin);
      break;
    default:
      break;
  }
  return GST_PAD_PROBE_OK;
}

static void
pad_added_cb (GstElement * element, GstPad * pad, GstDecodeChain * chain)
{
  GstCaps *caps;
  GstDecodeBin *dbin;

  dbin = chain->dbin;

  GST_DEBUG_OBJECT (pad, "pad added, chain:%p", chain);

  caps = get_pad_caps (pad);
  analyze_new_pad (dbin, element, pad, caps, chain);
  if (caps)
    gst_caps_unref (caps);

  EXPOSE_LOCK (dbin);
  if (gst_decode_chain_is_complete (dbin->decode_chain)) {
    GST_LOG_OBJECT (dbin,
        "That was the last dynamic object, now attempting to expose the group");
    if (!gst_decode_bin_expose (dbin))
      GST_WARNING_OBJECT (dbin, "Couldn't expose group");
  }
  EXPOSE_UNLOCK (dbin);
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, GstDecodeChain * chain)
{
  GList *l;

  GST_LOG_OBJECT (pad, "pad removed, chain:%p", chain);

  /* In fact, we don't have to do anything here, the active group will be
   * removed when the group's multiqueue is drained */
  CHAIN_MUTEX_LOCK (chain);
  for (l = chain->pending_pads; l; l = l->next) {
    GstPendingPad *ppad = l->data;
    GstPad *opad = ppad->pad;

    if (pad == opad) {
      g_signal_handlers_disconnect_by_func (pad, caps_notify_cb, chain);
      gst_pending_pad_free (ppad);
      chain->pending_pads = g_list_delete_link (chain->pending_pads, l);
      break;
    }
  }
  CHAIN_MUTEX_UNLOCK (chain);
}

static void
no_more_pads_cb (GstElement * element, GstDecodeChain * chain)
{
  GstDecodeGroup *group = NULL;

  GST_LOG_OBJECT (element, "got no more pads");

  CHAIN_MUTEX_LOCK (chain);
  if (!chain->elements
      || ((GstDecodeElement *) chain->elements->data)->element != element) {
    GST_LOG_OBJECT (chain->dbin, "no-more-pads from old chain element '%s'",
        GST_OBJECT_NAME (element));
    CHAIN_MUTEX_UNLOCK (chain);
    return;
  } else if (!chain->demuxer) {
    GST_LOG_OBJECT (chain->dbin, "no-more-pads from a non-demuxer element '%s'",
        GST_OBJECT_NAME (element));
    CHAIN_MUTEX_UNLOCK (chain);
    return;
  }

  /* when we received no_more_pads, we can complete the pads of the chain */
  if (!chain->next_groups && chain->active_group) {
    group = chain->active_group;
  } else if (chain->next_groups) {
    group = chain->next_groups->data;
  }
  if (!group) {
    GST_ERROR_OBJECT (chain->dbin, "can't find group for element");
    CHAIN_MUTEX_UNLOCK (chain);
    return;
  }

  GST_DEBUG_OBJECT (element, "Setting group %p to complete", group);

  group->no_more_pads = TRUE;
  /* this group has prerolled enough to not need more pads,
   * we can probably set its buffering state to playing now */
  GST_DEBUG_OBJECT (group->dbin, "Setting group %p multiqueue to "
      "'playing' buffering mode", group);
  decodebin_set_queue_size (group->dbin, group->multiqueue, FALSE,
      (group->parent ? group->parent->seekable : TRUE));
  CHAIN_MUTEX_UNLOCK (chain);

  EXPOSE_LOCK (chain->dbin);
  if (gst_decode_chain_is_complete (chain->dbin->decode_chain)) {
    gst_decode_bin_expose (chain->dbin);
  }
  EXPOSE_UNLOCK (chain->dbin);
}

static void
caps_notify_cb (GstPad * pad, GParamSpec * unused, GstDecodeChain * chain)
{
  GstElement *element;
  GList *l;

  GST_LOG_OBJECT (pad, "Notified caps for pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* Disconnect this; if we still need it, we'll reconnect to this in
   * analyze_new_pad */
  g_signal_handlers_disconnect_by_func (pad, caps_notify_cb, chain);

  element = GST_ELEMENT_CAST (gst_pad_get_parent (pad));

  CHAIN_MUTEX_LOCK (chain);
  for (l = chain->pending_pads; l; l = l->next) {
    GstPendingPad *ppad = l->data;
    if (ppad->pad == pad) {
      gst_pending_pad_free (ppad);
      chain->pending_pads = g_list_delete_link (chain->pending_pads, l);
      break;
    }
  }
  CHAIN_MUTEX_UNLOCK (chain);

  pad_added_cb (element, pad, chain);

  gst_object_unref (element);
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
  klass =
      gst_element_factory_get_metadata (srcfactory, GST_ELEMENT_METADATA_KLASS);

  /* Can't be a demuxer unless it has Demux in the klass name */
  if (!strstr (klass, "Demux"))
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

/* Returns TRUE if the caps are compatible with the caps specified in the 'caps'
 * property (which by default are the raw caps)
 *
 * The decodebin_lock should be taken !
 */
static gboolean
are_final_caps (GstDecodeBin * dbin, GstCaps * caps)
{
  gboolean res;

  GST_LOG_OBJECT (dbin, "Checking with caps %" GST_PTR_FORMAT, caps);

  /* lock for getting the caps */
  GST_OBJECT_LOCK (dbin);
  res = gst_caps_can_intersect (dbin->caps, caps);
  GST_OBJECT_UNLOCK (dbin);

  GST_LOG_OBJECT (dbin, "Caps are %sfinal caps", res ? "" : "not ");

  return res;
}

/****
 * GstDecodeChain functions
 ****/

/* gst_decode_chain_get_current_group:
 *
 * Returns the current group of this chain, to which
 * new chains should be attached or NULL if the last
 * group didn't have no-more-pads.
 *
 * Not MT-safe: Call with parent chain lock!
 */
static GstDecodeGroup *
gst_decode_chain_get_current_group (GstDecodeChain * chain)
{
  GstDecodeGroup *group;

  if (!chain->next_groups && chain->active_group
      && chain->active_group->overrun && !chain->active_group->no_more_pads) {
    GST_WARNING_OBJECT (chain->dbin,
        "Currently active group %p is exposed"
        " and wants to add a new pad without having signaled no-more-pads",
        chain->active_group);
    return NULL;
  }

  if (chain->next_groups && (group = chain->next_groups->data) && group->overrun
      && !group->no_more_pads) {
    GST_WARNING_OBJECT (chain->dbin,
        "Currently newest pending group %p "
        "had overflow but didn't signal no-more-pads", group);
    return NULL;
  }

  /* Now we know that we can really return something useful */
  if (!chain->active_group) {
    chain->active_group = group = gst_decode_group_new (chain->dbin, chain);
  } else if (!chain->active_group->overrun
      && !chain->active_group->no_more_pads) {
    group = chain->active_group;
  } else if (chain->next_groups && (group = chain->next_groups->data)
      && !group->overrun && !group->no_more_pads) {
    /* group = chain->next_groups->data */
  } else {
    group = gst_decode_group_new (chain->dbin, chain);
    chain->next_groups = g_list_prepend (chain->next_groups, group);
  }

  return group;
}

static void gst_decode_group_free_internal (GstDecodeGroup * group,
    gboolean hide);

static void
gst_decode_chain_free_internal (GstDecodeChain * chain, gboolean hide)
{
  GList *l;

  CHAIN_MUTEX_LOCK (chain);

  GST_DEBUG_OBJECT (chain->dbin, "%s chain %p", (hide ? "Hiding" : "Freeing"),
      chain);

  if (chain->active_group) {
    gst_decode_group_free_internal (chain->active_group, hide);
    if (!hide)
      chain->active_group = NULL;
  }

  for (l = chain->next_groups; l; l = l->next) {
    gst_decode_group_free_internal ((GstDecodeGroup *) l->data, hide);
    if (!hide)
      l->data = NULL;
  }
  if (!hide) {
    g_list_free (chain->next_groups);
    chain->next_groups = NULL;
  }

  if (!hide) {
    for (l = chain->old_groups; l; l = l->next) {
      GstDecodeGroup *group = l->data;

      gst_decode_group_free (group);
    }
    g_list_free (chain->old_groups);
    chain->old_groups = NULL;
  }

  for (l = chain->pending_pads; l; l = l->next) {
    GstPendingPad *ppad = l->data;
    GstPad *pad = ppad->pad;

    g_signal_handlers_disconnect_by_func (pad, caps_notify_cb, chain);
    gst_pending_pad_free (ppad);
    l->data = NULL;
  }
  g_list_free (chain->pending_pads);
  chain->pending_pads = NULL;

  for (l = chain->elements; l; l = l->next) {
    GstDecodeElement *delem = l->data;
    GstElement *element = delem->element;

    g_signal_handlers_disconnect_by_func (element, pad_added_cb, chain);
    g_signal_handlers_disconnect_by_func (element, pad_removed_cb, chain);
    g_signal_handlers_disconnect_by_func (element, no_more_pads_cb, chain);

    if (delem->capsfilter) {
      if (GST_OBJECT_PARENT (delem->capsfilter) ==
          GST_OBJECT_CAST (chain->dbin))
        gst_bin_remove (GST_BIN_CAST (chain->dbin), delem->capsfilter);
      if (!hide) {
        gst_element_set_state (delem->capsfilter, GST_STATE_NULL);
      }
    }

    if (GST_OBJECT_PARENT (element) == GST_OBJECT_CAST (chain->dbin))
      gst_bin_remove (GST_BIN_CAST (chain->dbin), element);
    if (!hide) {
      gst_element_set_state (element, GST_STATE_NULL);
    }

    SUBTITLE_LOCK (chain->dbin);
    /* remove possible subtitle element */
    chain->dbin->subtitles = g_list_remove (chain->dbin->subtitles, element);
    SUBTITLE_UNLOCK (chain->dbin);

    if (!hide) {
      if (delem->capsfilter) {
        gst_object_unref (delem->capsfilter);
        delem->capsfilter = NULL;
      }

      gst_object_unref (element);
      l->data = NULL;

      g_slice_free (GstDecodeElement, delem);
    }
  }
  if (!hide) {
    g_list_free (chain->elements);
    chain->elements = NULL;
  }

  if (chain->endpad) {
    if (chain->endpad->exposed) {
      gst_element_remove_pad (GST_ELEMENT_CAST (chain->dbin),
          GST_PAD_CAST (chain->endpad));
    }

    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (chain->endpad), NULL);
    chain->endpad->exposed = FALSE;
    if (!hide) {
      gst_object_unref (chain->endpad);
      chain->endpad = NULL;
    }
  }

  if (chain->pad) {
    gst_object_unref (chain->pad);
    chain->pad = NULL;
  }

  if (chain->endcaps) {
    gst_caps_unref (chain->endcaps);
    chain->endcaps = NULL;
  }

  GST_DEBUG_OBJECT (chain->dbin, "%s chain %p", (hide ? "Hidden" : "Freed"),
      chain);
  CHAIN_MUTEX_UNLOCK (chain);
  if (!hide) {
    g_mutex_free (chain->lock);
    g_slice_free (GstDecodeChain, chain);
  }
}

/* gst_decode_chain_free:
 *
 * Completely frees and removes the chain and all
 * child groups from decodebin.
 *
 * MT-safe, don't hold the chain lock or any child chain's lock
 * when calling this!
 */
static void
gst_decode_chain_free (GstDecodeChain * chain)
{
  gst_decode_chain_free_internal (chain, FALSE);
}

/* gst_decode_chain_new:
 *
 * Creates a new decode chain and initializes it.
 *
 * It's up to the caller to add it to the list of child chains of
 * a group!
 */
static GstDecodeChain *
gst_decode_chain_new (GstDecodeBin * dbin, GstDecodeGroup * parent,
    GstPad * pad)
{
  GstDecodeChain *chain = g_slice_new0 (GstDecodeChain);

  GST_DEBUG_OBJECT (dbin, "Creating new chain %p with parent group %p", chain,
      parent);

  chain->dbin = dbin;
  chain->parent = parent;
  chain->lock = g_mutex_new ();
  chain->pad = gst_object_ref (pad);

  return chain;
}

/****
 * GstDecodeGroup functions
 ****/

/* The overrun callback is used to expose groups that have not yet had their
 * no_more_pads called while the (large) multiqueue overflowed. When this
 * happens we must assume that the no_more_pads will not arrive anymore and we
 * must expose the pads that we have.
 */
static void
multi_queue_overrun_cb (GstElement * queue, GstDecodeGroup * group)
{
  GstDecodeBin *dbin;

  dbin = group->dbin;

  GST_LOG_OBJECT (dbin, "multiqueue '%s' (%p) is full", GST_OBJECT_NAME (queue),
      queue);

  group->overrun = TRUE;
  /* this group has prerolled enough to not need more pads,
   * we can probably set its buffering state to playing now */
  GST_DEBUG_OBJECT (group->dbin, "Setting group %p multiqueue to "
      "'playing' buffering mode", group);
  decodebin_set_queue_size (group->dbin, group->multiqueue, FALSE,
      (group->parent ? group->parent->seekable : TRUE));

  /* FIXME: We should make sure that everything gets exposed now
   * even if child chains are not complete because the will never
   * be complete! Ignore any non-complete chains when exposing
   * and never expose them later
   */

  EXPOSE_LOCK (dbin);
  if (gst_decode_chain_is_complete (dbin->decode_chain)) {
    if (!gst_decode_bin_expose (dbin))
      GST_WARNING_OBJECT (dbin, "Couldn't expose group");
  }
  EXPOSE_UNLOCK (group->dbin);
}

static void
gst_decode_group_free_internal (GstDecodeGroup * group, gboolean hide)
{
  GList *l;

  GST_DEBUG_OBJECT (group->dbin, "%s group %p", (hide ? "Hiding" : "Freeing"),
      group);
  for (l = group->children; l; l = l->next) {
    GstDecodeChain *chain = (GstDecodeChain *) l->data;

    gst_decode_chain_free_internal (chain, hide);
    if (!hide)
      l->data = NULL;
  }
  if (!hide) {
    g_list_free (group->children);
    group->children = NULL;
  }

  if (!hide) {
    for (l = group->reqpads; l; l = l->next) {
      GstPad *pad = l->data;

      gst_element_release_request_pad (group->multiqueue, pad);
      gst_object_unref (pad);
      l->data = NULL;
    }
    g_list_free (group->reqpads);
    group->reqpads = NULL;
  }

  if (group->multiqueue) {
    if (group->overrunsig) {
      g_signal_handler_disconnect (group->multiqueue, group->overrunsig);
      group->overrunsig = 0;
    }

    if (GST_OBJECT_PARENT (group->multiqueue) == GST_OBJECT_CAST (group->dbin))
      gst_bin_remove (GST_BIN_CAST (group->dbin), group->multiqueue);
    if (!hide) {
      gst_element_set_state (group->multiqueue, GST_STATE_NULL);
      gst_object_unref (group->multiqueue);
      group->multiqueue = NULL;
    }
  }

  GST_DEBUG_OBJECT (group->dbin, "%s group %p", (hide ? "Hided" : "Freed"),
      group);
  if (!hide)
    g_slice_free (GstDecodeGroup, group);
}

/* gst_decode_group_free:
 *
 * Completely frees and removes the decode group and all
 * it's children.
 *
 * Never call this from any streaming thread!
 *
 * Not MT-safe, call with parent's chain lock!
 */
static void
gst_decode_group_free (GstDecodeGroup * group)
{
  gst_decode_group_free_internal (group, FALSE);
}

/* gst_decode_group_hide:
 *
 * Hide the decode group only, this means that
 * all child endpads are removed from decodebin
 * and all signals are unconnected.
 *
 * No element is set to NULL state and completely
 * unrefed here.
 *
 * Can be called from streaming threads.
 *
 * Not MT-safe, call with parent's chain lock!
 */
static void
gst_decode_group_hide (GstDecodeGroup * group)
{
  gst_decode_group_free_internal (group, TRUE);
}

/* configure queue sizes, this depends on the buffering method and if we are
 * playing or prerolling. */
static void
decodebin_set_queue_size (GstDecodeBin * dbin, GstElement * multiqueue,
    gboolean preroll, gboolean seekable)
{
  guint max_bytes, max_buffers;
  guint64 max_time;

  if (preroll || dbin->use_buffering) {
    /* takes queue limits, initially we only queue up up to the max bytes limit,
     * with a default of 2MB. we use the same values for buffering mode. */
    if ((max_bytes = dbin->max_size_bytes) == 0)
      max_bytes = AUTO_PREROLL_SIZE_BYTES;
    if ((max_buffers = dbin->max_size_buffers) == 0)
      max_buffers = AUTO_PREROLL_SIZE_BUFFERS;
    if ((max_time = dbin->max_size_time) == 0)
      max_time = seekable ? AUTO_PREROLL_SEEKABLE_SIZE_TIME :
          AUTO_PREROLL_NOT_SEEKABLE_SIZE_TIME;
  } else {
    /* update runtime limits. At runtime, we try to keep the amount of buffers
     * in the queues as low as possible (but at least 5 buffers). */
    if ((max_bytes = dbin->max_size_bytes) == 0)
      max_bytes = AUTO_PLAY_SIZE_BYTES;
    if ((max_buffers = dbin->max_size_buffers) == 0)
      max_buffers = AUTO_PLAY_SIZE_BUFFERS;
    if ((max_time = dbin->max_size_time) == 0)
      max_time = AUTO_PLAY_SIZE_TIME;
  }

  g_object_set (multiqueue,
      "max-size-bytes", max_bytes, "max-size-time", max_time,
      "max-size-buffers", max_buffers, NULL);
}

/* gst_decode_group_new:
 * @dbin: Parent decodebin
 * @parent: Parent chain or %NULL
 *
 * Creates a new GstDecodeGroup. It is up to the caller to add it to the list
 * of groups.
 */
static GstDecodeGroup *
gst_decode_group_new (GstDecodeBin * dbin, GstDecodeChain * parent)
{
  GstDecodeGroup *group = g_slice_new0 (GstDecodeGroup);
  GstElement *mq;
  gboolean seekable;

  GST_DEBUG_OBJECT (dbin, "Creating new group %p with parent chain %p", group,
      parent);

  group->dbin = dbin;
  group->parent = parent;

  mq = group->multiqueue = gst_element_factory_make ("multiqueue", NULL);
  if (G_UNLIKELY (!group->multiqueue))
    goto missing_multiqueue;

  /* default is for use-buffering is FALSE */
  if (dbin->use_buffering) {
    g_object_set (mq,
        "use-buffering", TRUE,
        "low-percent", dbin->low_percent,
        "high-percent", dbin->high_percent, NULL);
  }

  /* configure queue sizes for preroll */
  seekable = FALSE;
  if (parent && parent->demuxer) {
    GstElement *element =
        ((GstDecodeElement *) parent->elements->data)->element;
    GstPad *pad = gst_element_get_static_pad (element, "sink");
    if (pad) {
      seekable = parent->seekable = check_upstream_seekable (dbin, pad);
      gst_object_unref (pad);
    }
  }
  decodebin_set_queue_size (dbin, mq, TRUE, seekable);

  group->overrunsig = g_signal_connect (G_OBJECT (mq), "overrun",
      G_CALLBACK (multi_queue_overrun_cb), group);

  gst_bin_add (GST_BIN (dbin), gst_object_ref (mq));
  gst_element_set_state (mq, GST_STATE_PAUSED);

  return group;

  /* ERRORS */
missing_multiqueue:
  {
    gst_element_post_message (GST_ELEMENT_CAST (dbin),
        gst_missing_element_message_new (GST_ELEMENT_CAST (dbin),
            "multiqueue"));
    GST_ELEMENT_ERROR (dbin, CORE, MISSING_PLUGIN, (NULL), ("no multiqueue!"));
    g_slice_free (GstDecodeGroup, group);
    return NULL;
  }
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
  GstDecodeBin *dbin;
  GstPad *srcpad, *sinkpad;
  GstIterator *it = NULL;
  GValue item = { 0, };

  dbin = group->dbin;

  GST_LOG_OBJECT (dbin, "group:%p pad %s:%s", group, GST_DEBUG_PAD_NAME (pad));

  srcpad = NULL;

  if (G_UNLIKELY (!group->multiqueue))
    return NULL;

  if (!(sinkpad = gst_element_get_request_pad (group->multiqueue, "sink_%u"))) {
    GST_ERROR_OBJECT (dbin, "Couldn't get sinkpad from multiqueue");
    return NULL;
  }

  if ((gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)) {
    GST_ERROR_OBJECT (dbin, "Couldn't link demuxer and multiqueue");
    goto error;
  }

  it = gst_pad_iterate_internal_links (sinkpad);

  if (!it || (gst_iterator_next (it, &item)) != GST_ITERATOR_OK
      || ((srcpad = g_value_dup_object (&item)) == NULL)) {
    GST_ERROR_OBJECT (dbin,
        "Couldn't get srcpad from multiqueue for sinkpad %" GST_PTR_FORMAT,
        sinkpad);
    goto error;
  }
  CHAIN_MUTEX_LOCK (group->parent);
  group->reqpads = g_list_prepend (group->reqpads, gst_object_ref (sinkpad));
  CHAIN_MUTEX_UNLOCK (group->parent);

beach:
  g_value_unset (&item);
  if (it)
    gst_iterator_free (it);
  gst_object_unref (sinkpad);
  return srcpad;

error:
  gst_element_release_request_pad (group->multiqueue, sinkpad);
  goto beach;
}

/* gst_decode_group_is_complete:
 *
 * Checks if the group is complete, this means that
 * a) overrun of the multiqueue or no-more-pads happened
 * b) all child chains are complete
 *
 * Not MT-safe, always call with decodebin expose lock
 */
static gboolean
gst_decode_group_is_complete (GstDecodeGroup * group)
{
  GList *l;
  gboolean complete = TRUE;

  if (!group->overrun && !group->no_more_pads) {
    complete = FALSE;
    goto out;
  }

  for (l = group->children; l; l = l->next) {
    GstDecodeChain *chain = l->data;

    if (!gst_decode_chain_is_complete (chain)) {
      complete = FALSE;
      goto out;
    }
  }

out:
  GST_DEBUG_OBJECT (group->dbin, "Group %p is complete: %d", group, complete);
  return complete;
}

/* gst_decode_chain_is_complete:
 *
 * Returns TRUE if the chain is complete, this means either
 * a) This chain is a dead end, i.e. we have no suitable plugins
 * b) This chain ends in an endpad and this is blocked or exposed
 *
 * Not MT-safe, always call with decodebin expose lock
 */
static gboolean
gst_decode_chain_is_complete (GstDecodeChain * chain)
{
  gboolean complete = FALSE;

  CHAIN_MUTEX_LOCK (chain);
  if (chain->deadend) {
    complete = TRUE;
    goto out;
  }

  if (chain->endpad && (chain->endpad->blocked || chain->endpad->exposed)) {
    complete = TRUE;
    goto out;
  }

  if (chain->demuxer) {
    if (chain->active_group
        && gst_decode_group_is_complete (chain->active_group)) {
      complete = TRUE;
      goto out;
    }
  }

out:
  CHAIN_MUTEX_UNLOCK (chain);
  GST_DEBUG_OBJECT (chain->dbin, "Chain %p is complete: %d", chain, complete);
  return complete;
}

static gboolean
drain_and_switch_chains (GstDecodeChain * chain, GstDecodePad * drainpad,
    gboolean * last_group, gboolean * drained, gboolean * switched);
/* drain_and_switch_chains/groups:
 *
 * CALL WITH CHAIN LOCK (or group parent) TAKEN !
 *
 * Goes down the chains/groups until it finds the chain
 * to which the drainpad belongs.
 *
 * It marks that pad/chain as drained and then will figure
 * out which group to switch to or not.
 *
 * last_chain will be set to TRUE if the group to which the
 * pad belongs is the last one.
 *
 * drained will be set to TRUE if the chain/group is drained.
 *
 * Returns: TRUE if the chain contained the target pad */
static gboolean
drain_and_switch_group (GstDecodeGroup * group, GstDecodePad * drainpad,
    gboolean * last_group, gboolean * drained, gboolean * switched)
{
  gboolean handled = FALSE;
  GList *tmp;

  GST_DEBUG ("Checking group %p (target pad %s:%s)",
      group, GST_DEBUG_PAD_NAME (drainpad));

  /* Definitely can't be in drained groups */
  if (G_UNLIKELY (group->drained)) {
    goto beach;
  }

  /* Figure out if all our chains are drained with the
   * new information */
  group->drained = TRUE;
  for (tmp = group->children; tmp; tmp = tmp->next) {
    GstDecodeChain *chain = (GstDecodeChain *) tmp->data;
    gboolean subdrained = FALSE;

    handled |=
        drain_and_switch_chains (chain, drainpad, last_group, &subdrained,
        switched);
    if (!subdrained)
      group->drained = FALSE;
  }

beach:
  GST_DEBUG ("group %p (last_group:%d, drained:%d, switched:%d, handled:%d)",
      group, *last_group, group->drained, *switched, handled);
  *drained = group->drained;
  return handled;
}

static gboolean
drain_and_switch_chains (GstDecodeChain * chain, GstDecodePad * drainpad,
    gboolean * last_group, gboolean * drained, gboolean * switched)
{
  gboolean handled = FALSE;
  GstDecodeBin *dbin = chain->dbin;

  GST_DEBUG ("Checking chain %p (target pad %s:%s)",
      chain, GST_DEBUG_PAD_NAME (drainpad));

  CHAIN_MUTEX_LOCK (chain);

  /* Definitely can't be in drained chains */
  if (G_UNLIKELY (chain->drained)) {
    goto beach;
  }
  
  if (chain->endpad) {
    /* Check if we're reached the target endchain */
    if (chain == drainpad->chain) {
      GST_DEBUG ("Found the target chain");
      drainpad->drained = TRUE;
      handled = TRUE;
    }

    chain->drained = chain->endpad->drained;
    goto beach;
  }

  /* We known there are groups to switch to */
  if (chain->next_groups)
    *last_group = FALSE;

  /* Check the active group */
  if (chain->active_group) {
    gboolean subdrained = FALSE;
    handled = drain_and_switch_group (chain->active_group, drainpad,
        last_group, &subdrained, switched);

    /* The group is drained, see if we can switch to another */
    if (handled && subdrained && !*switched) {
      if (chain->next_groups) {
        /* Switch to next group */
        GST_DEBUG_OBJECT (dbin, "Hiding current group %p", chain->active_group);
        gst_decode_group_hide (chain->active_group);
        chain->old_groups =
            g_list_prepend (chain->old_groups, chain->active_group);
        GST_DEBUG_OBJECT (dbin, "Switching to next group %p",
            chain->next_groups->data);
        chain->active_group = chain->next_groups->data;
        chain->next_groups =
            g_list_delete_link (chain->next_groups, chain->next_groups);
        *switched = TRUE;
        chain->drained = FALSE;
      } else {
        GST_DEBUG ("Group %p was the last in chain %p", chain->active_group,
            chain);
        chain->drained = TRUE;	
        /* We're drained ! */
      }
    }
  }

beach:
  CHAIN_MUTEX_UNLOCK (chain);

  GST_DEBUG ("Chain %p (handled:%d, last_group:%d, drained:%d, switched:%d)",
      chain, handled, *last_group, chain->drained, *switched);

  *drained = chain->drained;

  if (*drained)
    g_signal_emit (dbin, gst_decode_bin_signals[SIGNAL_DRAINED], 0, NULL);

  return handled;
}

/* check if the group is drained, meaning all pads have seen an EOS
 * event.  */
static gboolean
gst_decode_pad_handle_eos (GstDecodePad * pad)
{
  gboolean last_group = TRUE;
  gboolean switched = FALSE;
  gboolean drained = FALSE;
  GstDecodeChain *chain = pad->chain;
  GstDecodeBin *dbin = chain->dbin;

  GST_LOG_OBJECT (dbin, "pad %p", pad);
  drain_and_switch_chains (dbin->decode_chain, pad, &last_group, &drained,
      &switched);

  if (switched) {
    /* If we resulted in a group switch, expose what's needed */
    EXPOSE_LOCK (dbin);
    if (gst_decode_chain_is_complete (dbin->decode_chain))
      gst_decode_bin_expose (dbin);
    EXPOSE_UNLOCK (dbin);
  }

  return last_group;
}

/* gst_decode_group_is_drained:
 *
 * Check is this group is drained and cache this result.
 * The group is drained if all child chains are drained.
 *
 * Not MT-safe, call with group->parent's lock */
static gboolean
gst_decode_group_is_drained (GstDecodeGroup * group)
{
  GList *l;
  gboolean drained = TRUE;

  if (group->drained) {
    drained = TRUE;
    goto out;
  }

  for (l = group->children; l; l = l->next) {
    GstDecodeChain *chain = l->data;

    CHAIN_MUTEX_LOCK (chain);
    if (!gst_decode_chain_is_drained (chain))
      drained = FALSE;
    CHAIN_MUTEX_UNLOCK (chain);
    if (!drained)
      goto out;
  }
  group->drained = drained;

out:
  GST_DEBUG_OBJECT (group->dbin, "Group %p is drained: %d", group, drained);
  return drained;
}

/* gst_decode_chain_is_drained:
 *
 * Check is the chain is drained, which means that
 * either
 *
 * a) it's endpad is drained
 * b) there are no pending pads, the active group is drained
 *    and there are no next groups
 *
 * Not MT-safe, call with chain lock
 */
static gboolean
gst_decode_chain_is_drained (GstDecodeChain * chain)
{
  gboolean drained = FALSE;

  if (chain->endpad) {
    drained = chain->endpad->drained;
    goto out;
  }

  if (chain->pending_pads) {
    drained = FALSE;
    goto out;
  }

  if (chain->active_group && gst_decode_group_is_drained (chain->active_group)
      && !chain->next_groups) {
    drained = TRUE;
    goto out;
  }

out:
  GST_DEBUG_OBJECT (chain->dbin, "Chain %p is drained: %d", chain, drained);
  return drained;
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
  gint va, vb;
  GstCaps *capsa, *capsb;
  GstStructure *sa, *sb;
  const gchar *namea, *nameb;

  capsa = get_pad_caps (GST_PAD_CAST (da));
  capsb = get_pad_caps (GST_PAD_CAST (db));

  sa = gst_caps_get_structure ((const GstCaps *) capsa, 0);
  sb = gst_caps_get_structure ((const GstCaps *) capsb, 0);

  namea = gst_structure_get_name (sa);
  nameb = gst_structure_get_name (sb);

  if (g_strrstr (namea, "video/x-raw"))
    va = 0;
  else if (g_strrstr (namea, "video/"))
    va = 1;
  else if (g_strrstr (namea, "audio/x-raw"))
    va = 2;
  else if (g_strrstr (namea, "audio/"))
    va = 3;
  else
    va = 4;

  if (g_strrstr (nameb, "video/x-raw"))
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

static GstCaps *
_gst_element_get_linked_caps (GstElement * src, GstElement * sink,
    GstPad ** srcpad)
{
  GstIterator *it;
  GstElement *parent;
  GstPad *pad, *peer;
  gboolean done = FALSE;
  GstCaps *caps = NULL;
  GValue item = { 0, };

  it = gst_element_iterate_src_pads (src);
  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&item);
        peer = gst_pad_get_peer (pad);
        if (peer) {
          parent = gst_pad_get_parent_element (peer);
          if (parent == sink) {
            caps = gst_pad_get_current_caps (pad);
            if (srcpad) {
              gst_object_ref (pad);
              *srcpad = pad;
            }
            done = TRUE;
          }

          if (parent)
            gst_object_unref (parent);
          gst_object_unref (peer);
        }
        g_value_reset (&item);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (it);

  return caps;
}

static GQuark topology_structure_name = 0;
static GQuark topology_caps = 0;
static GQuark topology_next = 0;
static GQuark topology_pad = 0;
static GQuark topology_element_srcpad = 0;

/* FIXME: Invent gst_structure_take_structure() to prevent all the
 * structure copying for nothing
 */
static GstStructure *
gst_decode_chain_get_topology (GstDecodeChain * chain)
{
  GstStructure *s, *u;
  GList *l;
  GstCaps *caps;

  if (G_UNLIKELY ((chain->endpad || chain->deadend)
          && (chain->endcaps == NULL))) {
    GST_WARNING ("End chain without valid caps !");
    return NULL;
  }

  u = gst_structure_new_id_empty (topology_structure_name);

  /* Now at the last element */
  if ((chain->elements || !chain->active_group) &&
      (chain->endpad || chain->deadend)) {
    s = gst_structure_new_id_empty (topology_structure_name);
    gst_structure_id_set (u, topology_caps, GST_TYPE_CAPS, chain->endcaps,
        NULL);

    if (chain->endpad) {
      gst_structure_id_set (u, topology_pad, GST_TYPE_PAD, chain->endpad, NULL);
      gst_structure_id_set (u, topology_element_srcpad, GST_TYPE_PAD,
          chain->endpad, NULL);
    }
    gst_structure_id_set (s, topology_next, GST_TYPE_STRUCTURE, u, NULL);
    gst_structure_free (u);
    u = s;
  } else if (chain->active_group) {
    GValue list = { 0, };
    GValue item = { 0, };

    g_value_init (&list, GST_TYPE_LIST);
    g_value_init (&item, GST_TYPE_STRUCTURE);
    for (l = chain->active_group->children; l; l = l->next) {
      s = gst_decode_chain_get_topology (l->data);
      if (s) {
        gst_value_set_structure (&item, s);
        gst_value_list_append_value (&list, &item);
        g_value_reset (&item);
        gst_structure_free (s);
      }
    }
    gst_structure_id_set_value (u, topology_next, &list);
    g_value_unset (&list);
    g_value_unset (&item);
  }

  /* Get caps between all elements in this chain */
  l = (chain->elements && chain->elements->next) ? chain->elements : NULL;
  for (; l && l->next; l = l->next) {
    GstDecodeElement *delem, *delem_next;
    GstElement *elem, *elem_next;
    GstCaps *caps;
    GstPad *srcpad;

    delem = l->data;
    elem = delem->element;
    delem_next = l->next->data;
    elem_next = delem_next->element;
    srcpad = NULL;

    caps = _gst_element_get_linked_caps (elem_next, elem, &srcpad);

    if (caps) {
      s = gst_structure_new_id_empty (topology_structure_name);
      gst_structure_id_set (u, topology_caps, GST_TYPE_CAPS, caps, NULL);
      gst_caps_unref (caps);

      gst_structure_id_set (s, topology_next, GST_TYPE_STRUCTURE, u, NULL);
      gst_structure_free (u);
      u = s;
    }

    if (srcpad) {
      gst_structure_id_set (u, topology_element_srcpad, GST_TYPE_PAD, srcpad,
          NULL);
      gst_object_unref (srcpad);
    }
  }

  /* Caps that resulted in this chain */
  caps = gst_pad_get_current_caps (chain->pad);
  if (!caps) {
    caps = get_pad_caps (chain->pad);
    if (G_UNLIKELY (!gst_caps_is_fixed (caps))) {
      GST_ERROR_OBJECT (chain->pad,
          "Couldn't get fixed caps, got %" GST_PTR_FORMAT, caps);
      gst_caps_unref (caps);
      caps = NULL;
    }
  }
  gst_structure_id_set (u, topology_caps, GST_TYPE_CAPS, caps, NULL);
  gst_structure_id_set (u, topology_element_srcpad, GST_TYPE_PAD, chain->pad,
      NULL);
  gst_caps_unref (caps);

  return u;
}

static void
gst_decode_bin_post_topology_message (GstDecodeBin * dbin)
{
  GstStructure *s;
  GstMessage *msg;

  s = gst_decode_chain_get_topology (dbin->decode_chain);

  msg = gst_message_new_element (GST_OBJECT (dbin), s);
  gst_element_post_message (GST_ELEMENT (dbin), msg);
}

/* Must only be called if the toplevel chain is complete and blocked! */
/* Not MT-safe, call with decodebin expose lock! */
static gboolean
gst_decode_bin_expose (GstDecodeBin * dbin)
{
  GList *tmp, *endpads = NULL;
  gboolean missing_plugin = FALSE;
  gboolean already_exposed = TRUE;

  GST_DEBUG_OBJECT (dbin, "Exposing currently active chains/groups");

  /* Don't expose if we're currently shutting down */
  DYN_LOCK (dbin);
  if (G_UNLIKELY (dbin->shutdown == TRUE)) {
    GST_WARNING_OBJECT (dbin, "Currently, shutting down, aborting exposing");
    DYN_UNLOCK (dbin);
    return FALSE;
  }
  DYN_UNLOCK (dbin);

  /* Get the pads that we're going to expose and mark things as exposed */
  if (!gst_decode_chain_expose (dbin->decode_chain, &endpads, &missing_plugin)) {
    g_list_foreach (endpads, (GFunc) gst_object_unref, NULL);
    g_list_free (endpads);
    GST_ERROR_OBJECT (dbin, "Broken chain/group tree");
    g_return_val_if_reached (FALSE);
    return FALSE;
  }
  if (endpads == NULL) {
    if (missing_plugin) {
      GST_WARNING_OBJECT (dbin, "No suitable plugins found");
      GST_ELEMENT_ERROR (dbin, CORE, MISSING_PLUGIN, (NULL),
          ("no suitable plugins found"));
    } else {
      /* in this case, the stream ended without buffers,
       * just post a warning */
      GST_WARNING_OBJECT (dbin, "All streams finished without buffers");
      GST_ELEMENT_ERROR (dbin, STREAM, FAILED, (NULL),
          ("all streams without buffers"));
    }
    return FALSE;
  }

  /* Check if this was called when everything was exposed already */
  for (tmp = endpads; tmp && already_exposed; tmp = tmp->next) {
    GstDecodePad *dpad = tmp->data;

    already_exposed &= dpad->exposed;
    if (!already_exposed)
      break;
  }
  if (already_exposed) {
    GST_DEBUG_OBJECT (dbin, "Everything was exposed already!");
    g_list_foreach (endpads, (GFunc) gst_object_unref, NULL);
    g_list_free (endpads);
    return TRUE;
  }

  /* Set all already exposed pads to blocked */
  for (tmp = endpads; tmp; tmp = tmp->next) {
    GstDecodePad *dpad = tmp->data;

    if (dpad->exposed) {
      GST_DEBUG_OBJECT (dpad, "blocking exposed pad");
      gst_decode_pad_set_blocked (dpad, TRUE);
    }
  }

  /* re-order pads : video, then audio, then others */
  endpads = g_list_sort (endpads, (GCompareFunc) sort_end_pads);

  /* Expose pads */
  for (tmp = endpads; tmp; tmp = tmp->next) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;
    gchar *padname;

    /* 1. rewrite name */
    padname = g_strdup_printf ("src_%u", dbin->nbpads);
    dbin->nbpads++;
    GST_DEBUG_OBJECT (dbin, "About to expose dpad %s as %s",
        GST_OBJECT_NAME (dpad), padname);
    gst_object_set_name (GST_OBJECT (dpad), padname);
    g_free (padname);

    /* 2. activate and add */
    if (!dpad->exposed
        && !gst_element_add_pad (GST_ELEMENT (dbin), GST_PAD_CAST (dpad))) {
      /* not really fatal, we can try to add the other pads */
      g_warning ("error adding pad to decodebin");
      continue;
    }
    dpad->exposed = TRUE;

    /* 3. emit signal */
    GST_INFO_OBJECT (dpad, "added new decoded pad");
  }

  /* 4. Signal no-more-pads. This allows the application to hook stuff to the
   * exposed pads */
  GST_LOG_OBJECT (dbin, "signaling no-more-pads");
  gst_element_no_more_pads (GST_ELEMENT (dbin));

  /* 5. Send a custom element message with the stream topology */
  if (dbin->post_stream_topology)
    gst_decode_bin_post_topology_message (dbin);

  /* 6. Unblock internal pads. The application should have connected stuff now
   * so that streaming can continue. */
  for (tmp = endpads; tmp; tmp = tmp->next) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;

    GST_DEBUG_OBJECT (dpad, "unblocking");
    gst_decode_pad_unblock (dpad);
    GST_DEBUG_OBJECT (dpad, "unblocked");
    gst_object_unref (dpad);
  }
  g_list_free (endpads);

  do_async_done (dbin);
  GST_DEBUG_OBJECT (dbin, "Exposed everything");
  return TRUE;
}

/* gst_decode_chain_expose:
 *
 * Check if the chain can be exposed and add all endpads
 * to the endpads list.
 *
 * Also update the active group's multiqueue to the
 * runtime limits.
 *
 * Not MT-safe, call with decodebin expose lock! *
 */
static gboolean
gst_decode_chain_expose (GstDecodeChain * chain, GList ** endpads,
    gboolean * missing_plugin)
{
  GstDecodeGroup *group;
  GList *l;
  GstDecodeBin *dbin;

  if (chain->deadend) {
    if (chain->endcaps)
      *missing_plugin = TRUE;
    return TRUE;
  }

  if (chain->endpad) {
    if (!chain->endpad->blocked && !chain->endpad->exposed)
      return FALSE;
    *endpads = g_list_prepend (*endpads, gst_object_ref (chain->endpad));
    return TRUE;
  }

  group = chain->active_group;
  if (!group)
    return FALSE;
  if (!group->no_more_pads && !group->overrun)
    return FALSE;

  dbin = group->dbin;

  /* configure queues for playback */
  decodebin_set_queue_size (dbin, group->multiqueue, FALSE, TRUE);

  /* we can now disconnect any overrun signal, which is used to expose the
   * group. */
  if (group->overrunsig) {
    GST_LOG_OBJECT (dbin, "Disconnecting overrun");
    g_signal_handler_disconnect (group->multiqueue, group->overrunsig);
    group->overrunsig = 0;
  }

  for (l = group->children; l; l = l->next) {
    GstDecodeChain *childchain = l->data;

    if (!gst_decode_chain_expose (childchain, endpads, missing_plugin))
      return FALSE;
  }

  return TRUE;
}

/*************************
 * GstDecodePad functions
 *************************/

static void
gst_decode_pad_class_init (GstDecodePadClass * klass)
{
}

static void
gst_decode_pad_init (GstDecodePad * pad)
{
  pad->chain = NULL;
  pad->blocked = FALSE;
  pad->exposed = FALSE;
  pad->drained = FALSE;
  gst_object_ref_sink (pad);
}

static GstPadProbeReturn
source_pad_blocked_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstDecodePad *dpad = user_data;
  GstDecodeChain *chain;
  GstDecodeBin *dbin;

  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GST_LOG_OBJECT (pad, "Seeing event '%s'",
        GST_EVENT_TYPE_NAME (GST_PAD_PROBE_INFO_EVENT (info)));
    if ((GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) != GST_EVENT_CAPS)
        && (GST_EVENT_IS_STICKY (GST_PAD_PROBE_INFO_EVENT (info))
            || !GST_EVENT_IS_SERIALIZED (GST_PAD_PROBE_INFO_EVENT (info)))) {
      /* do not block on sticky or out of band events otherwise the allocation query
         from demuxer might block the loop thread */
      GST_LOG_OBJECT (pad, "Letting event through");
      return GST_PAD_PROBE_PASS;
    }
  }
  chain = dpad->chain;
  dbin = chain->dbin;

  GST_LOG_OBJECT (dpad, "blocked: dpad->chain:%p", chain);

  dpad->blocked = TRUE;

  EXPOSE_LOCK (dbin);
  if (gst_decode_chain_is_complete (dbin->decode_chain)) {
    if (!gst_decode_bin_expose (dbin))
      GST_WARNING_OBJECT (dbin, "Couldn't expose group");
  }
  EXPOSE_UNLOCK (dbin);

  /* If we unblocked due to a caps event, let it go through */
  if ((GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) &&
      (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) == GST_EVENT_CAPS))
    return GST_PAD_PROBE_PASS;

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
source_pad_event_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstDecodePad *dpad = user_data;
  gboolean res = TRUE;

  GST_LOG_OBJECT (pad, "%s dpad:%p", GST_EVENT_TYPE_NAME (event), dpad);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GST_DEBUG_OBJECT (pad, "we received EOS");

    /* Check if all pads are drained.
     * * If there is no next group, we will let the EOS go through.
     * * If there is a next group but the current group isn't completely
     *   drained, we will drop the EOS event.
     * * If there is a next group to expose and this was the last non-drained
     *   pad for that group, we will remove the ghostpad of the current group
     *   first, which unlinks the peer and so drops the EOS. */
    res = gst_decode_pad_handle_eos (dpad);
  }
  if (res)
    return GST_PAD_PROBE_OK;
  else
    return GST_PAD_PROBE_DROP;
}

static void
gst_decode_pad_set_blocked (GstDecodePad * dpad, gboolean blocked)
{
  GstDecodeBin *dbin = dpad->dbin;
  GstPad *opad;

  DYN_LOCK (dbin);

  GST_DEBUG_OBJECT (dpad, "blocking pad: %d", blocked);

  opad = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (dpad));
  if (!opad)
    goto out;

  /* do not block if shutting down.
   * we do not consider/expect it blocked further below, but use other trick */
  if (!blocked || !dbin->shutdown) {
    if (blocked) {
      if (dpad->block_id == 0)
        dpad->block_id =
            gst_pad_add_probe (opad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
            source_pad_blocked_cb, gst_object_ref (dpad),
            (GDestroyNotify) gst_object_unref);
    } else {
      if (dpad->block_id != 0) {
        gst_pad_remove_probe (opad, dpad->block_id);
        dpad->block_id = 0;
      }
      dpad->blocked = FALSE;
    }
  }

  if (blocked) {
    if (dbin->shutdown) {
      /* deactivate to force flushing state to prevent NOT_LINKED errors */
      gst_pad_set_active (GST_PAD_CAST (dpad), FALSE);
      /* note that deactivating the target pad would have no effect here,
       * since elements are typically connected first (and pads exposed),
       * and only then brought to PAUSED state (so pads activated) */
    } else {
      gst_object_ref (dpad);
      dbin->blocked_pads = g_list_prepend (dbin->blocked_pads, dpad);
    }
  } else {
    GList *l;

    if ((l = g_list_find (dbin->blocked_pads, dpad))) {
      gst_object_unref (dpad);
      dbin->blocked_pads = g_list_delete_link (dbin->blocked_pads, l);
    }
  }
  gst_object_unref (opad);
out:
  DYN_UNLOCK (dbin);
}

static void
gst_decode_pad_add_drained_check (GstDecodePad * dpad)
{
  gst_pad_add_probe (GST_PAD_CAST (dpad), GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      source_pad_event_probe, dpad, NULL);
}

static void
gst_decode_pad_activate (GstDecodePad * dpad, GstDecodeChain * chain)
{
  g_return_if_fail (chain != NULL);

  dpad->chain = chain;
  gst_pad_set_active (GST_PAD_CAST (dpad), TRUE);
  gst_decode_pad_set_blocked (dpad, TRUE);
  gst_decode_pad_add_drained_check (dpad);
}

static void
gst_decode_pad_unblock (GstDecodePad * dpad)
{
  gst_decode_pad_set_blocked (dpad, FALSE);
}

/*gst_decode_pad_new:
 *
 * Creates a new GstDecodePad for the given pad.
 */
static GstDecodePad *
gst_decode_pad_new (GstDecodeBin * dbin, GstPad * pad, GstDecodeChain * chain)
{
  GstDecodePad *dpad;
  GstPadTemplate *pad_tmpl;

  GST_DEBUG_OBJECT (dbin, "making new decodepad");
  pad_tmpl = gst_static_pad_template_get (&decoder_bin_src_template);
  dpad =
      g_object_new (GST_TYPE_DECODE_PAD, "direction", GST_PAD_DIRECTION (pad),
      "template", pad_tmpl, NULL);
  gst_ghost_pad_construct (GST_GHOST_PAD_CAST (dpad));
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (dpad), pad);
  dpad->chain = chain;
  dpad->dbin = dbin;
  gst_object_unref (pad_tmpl);

  return dpad;
}

static void
gst_pending_pad_free (GstPendingPad * ppad)
{
  g_assert (ppad);
  g_assert (ppad->pad);

  if (ppad->event_probe_id != 0)
    gst_pad_remove_probe (ppad->pad, ppad->event_probe_id);
  gst_object_unref (ppad->pad);
  g_slice_free (GstPendingPad, ppad);
}

/*****
 * Element add/remove
 *****/

static void
do_async_start (GstDecodeBin * dbin)
{
  GstMessage *message;

  dbin->async_pending = TRUE;

  message = gst_message_new_async_start (GST_OBJECT_CAST (dbin));
  parent_class->handle_message (GST_BIN_CAST (dbin), message);
}

static void
do_async_done (GstDecodeBin * dbin)
{
  GstMessage *message;

  if (dbin->async_pending) {
    message =
        gst_message_new_async_done (GST_OBJECT_CAST (dbin),
        GST_CLOCK_TIME_NONE);
    parent_class->handle_message (GST_BIN_CAST (dbin), message);

    dbin->async_pending = FALSE;
  }
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
  GValue item = { 0, };

  it = gst_element_iterate_sink_pads (element);

  if ((gst_iterator_next (it, &item)) == GST_ITERATOR_OK)
    pad = g_value_dup_object (&item);
  g_value_unset (&item);
  gst_iterator_free (it);

  return pad;
}

/* call with dyn_lock held */
static void
unblock_pads (GstDecodeBin * dbin)
{
  GList *tmp;

  GST_LOG_OBJECT (dbin, "unblocking pads");

  for (tmp = dbin->blocked_pads; tmp; tmp = tmp->next) {
    GstDecodePad *dpad = (GstDecodePad *) tmp->data;
    GstPad *opad;

    opad = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (dpad));
    if (!opad)
      continue;

    GST_DEBUG_OBJECT (dpad, "unblocking");
    if (dpad->block_id != 0) {
      gst_pad_remove_probe (opad, dpad->block_id);
      dpad->block_id = 0;
    }
    dpad->blocked = FALSE;
    /* make flushing, prevent NOT_LINKED */
    GST_PAD_SET_FLUSHING (GST_PAD_CAST (dpad));
    gst_object_unref (dpad);
    gst_object_unref (opad);
    GST_DEBUG_OBJECT (dpad, "unblocked");
  }

  /* clear, no more blocked pads */
  g_list_free (dbin->blocked_pads);
  dbin->blocked_pads = NULL;
}

static GstStateChangeReturn
gst_decode_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstDecodeBin *dbin = GST_DECODE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (dbin->typefind == NULL)
        goto missing_typefind;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Make sure we've cleared all existing chains */
      if (dbin->decode_chain) {
        gst_decode_chain_free (dbin->decode_chain);
        dbin->decode_chain = NULL;
      }
      DYN_LOCK (dbin);
      GST_LOG_OBJECT (dbin, "clearing shutdown flag");
      dbin->shutdown = FALSE;
      DYN_UNLOCK (dbin);
      dbin->have_type = FALSE;
      ret = GST_STATE_CHANGE_ASYNC;
      do_async_start (dbin);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      DYN_LOCK (dbin);
      GST_LOG_OBJECT (dbin, "setting shutdown flag");
      dbin->shutdown = TRUE;
      unblock_pads (dbin);
      DYN_UNLOCK (dbin);
    default:
      break;
  }

  {
    GstStateChangeReturn bret;

    bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (G_UNLIKELY (bret == GST_STATE_CHANGE_FAILURE))
      goto activate_failed;
    else if (G_UNLIKELY (bret == GST_STATE_CHANGE_NO_PREROLL)) {
      do_async_done (dbin);
      ret = bret;
    }
  }
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      do_async_done (dbin);
      if (dbin->decode_chain) {
        gst_decode_chain_free (dbin->decode_chain);
        dbin->decode_chain = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;

/* ERRORS */
missing_typefind:
  {
    gst_element_post_message (element,
        gst_missing_element_message_new (element, "typefind"));
    GST_ELEMENT_ERROR (dbin, CORE, MISSING_PLUGIN, (NULL), ("no typefind!"));
    return GST_STATE_CHANGE_FAILURE;
  }
activate_failed:
  {
    GST_DEBUG_OBJECT (element,
        "element failed to change states -- activation problem?");
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
gst_decode_bin_handle_message (GstBin * bin, GstMessage * msg)
{
  GstDecodeBin *dbin = GST_DECODE_BIN (bin);
  gboolean drop = FALSE;

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GST_OBJECT_LOCK (dbin);
    drop = (g_list_find (dbin->filtered, GST_MESSAGE_SRC (msg)) != NULL);
    GST_OBJECT_UNLOCK (dbin);
  }

  if (drop)
    gst_message_unref (msg);
  else
    GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}

gboolean
gst_decode_bin_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_decode_bin_debug, "decodebin", 0, "decoder bin");

  /* Register some quarks here for the stream topology message */
  topology_structure_name = g_quark_from_static_string ("stream-topology");
  topology_caps = g_quark_from_static_string ("caps");
  topology_next = g_quark_from_static_string ("next");
  topology_pad = g_quark_from_static_string ("pad");
  topology_element_srcpad = g_quark_from_static_string ("element-srcpad");

  return gst_element_register (plugin, "decodebin", GST_RANK_NONE,
      GST_TYPE_DECODE_BIN);
}
