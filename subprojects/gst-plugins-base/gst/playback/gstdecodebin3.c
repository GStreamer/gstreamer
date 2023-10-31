/* GStreamer
 *
 * Copyright (C) <2015> Centricular Ltd
 *  @author: Edward Hervey <edward@centricular.com>
 *  @author: Jan Schmidt <jan@centricular.com>
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

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "gstplaybackelements.h"
#include "gstplay-enum.h"
#include "gstrawcaps.h"

/**
 * SECTION:element-decodebin3
 * @title: decodebin3
 *
 * #GstBin that auto-magically constructs a decoding pipeline using available
 * decoders and demuxers via auto-plugging. The output is raw audio, video
 * or subtitle streams.
 *
 * decodebin3 differs from the previous decodebin (decodebin2) in important ways:
 *
 * * supports publication and selection of stream information via
 * GstStreamCollection messages and #GST_EVENT_SELECT_STREAMS events.
 *
 * * dynamically switches stream connections internally, and
 * reuses decoder elements when stream selections change, so that in
 * the normal case it maintains 1 decoder of each type (video/audio/subtitle)
 * and only creates new elements when streams change and an existing decoder
 * is not capable of handling the new format.
 *
 * * supports multiple input pads for the parallel decoding of auxiliary streams
 * not muxed with the primary stream.
 *
 * * does not handle network stream buffering. decodebin3 expects that network stream
 * buffering is handled upstream, before data is passed to it.
 *
 */

/*
 * Global design
 *
 * 1) From sink pad to elementary streams (GstParseBin or identity)
 *
 * Note : If the incoming streams are push-based-only and are compatible with
 * either the output caps or a potential decoder, the usage of parsebin is
 * replaced by a simple passthrough identity element.
 *
 * The input sink pads are fed to GstParseBin. GstParseBin will feed them
 * through typefind. When the caps are detected (or changed) we recursively
 * figure out which demuxer, parser or depayloader is needed until we get to
 * elementary streams.
 *
 * All elementary streams (whether decoded or not, whether exposed or not) are
 * fed through multiqueue. There is only *one* multiqueue in decodebin3.
 *
 * => MultiQueue is the cornerstone.
 * => No buffering before multiqueue
 *
 * 2) Elementary streams
 *
 * After GstParseBin, there are 3 main components:
 *  1) Input Streams (provided by GstParseBin)
 *  2) Multiqueue slots
 *  3) Output Streams
 *
 * Input Streams correspond to the stream coming from GstParseBin and that gets
 * fed into a multiqueue slot.
 *
 * Output Streams correspond to the combination of a (optional) decoder and an
 * output ghostpad. Output Streams can be moved from one multiqueue slot to
 * another, can reconfigure itself (different decoders), and can be
 * added/removed depending on the configuration (all streams outputted, only one
 * of each type, ...).
 *
 * Multiqueue slots correspond to a pair of sink/src pad from multiqueue. For
 * each 'active' Input Stream there is a corresponding slot.
 * Slots might have different streams on input and output (due to internal
 * buffering).
 *
 * Due to internal queuing/buffering/..., all those components (might) behave
 * asynchronously. Therefore probes will be used on each component source pad to
 * detect various key-points:
 *  * EOS :
 *     the stream is done => Mark that component as done, optionally freeing/removing it
 *  * STREAM_START :
 *     a new stream is starting => link it further if needed
 *
 * 3) Gradual replacement
 *
 * If the caps change at any point in decodebin (input sink pad, demuxer output,
 * multiqueue output, ..), we gradually replace (if needed) the following elements.
 *
 * This is handled by the probes in various locations:
 *  a) typefind output
 *  b) multiqueue input (source pad of Input Streams)
 *  c) multiqueue output (source pad of Multiqueue Slots)
 *  d) final output (target of source ghostpads)
 *
 * When CAPS event arrive at those points, one of three things can happen:
 * a) There is no elements downstream yet, just create/link-to following elements
 * b) There are downstream elements, do a ACCEPT_CAPS query
 *  b.1) The new CAPS are accepted, keep current configuration
 *  b.2) The new CAPS are not accepted, remove following elements then do a)
 *
 *    Components:
 *
 *                                                   MultiQ     Output
 *                     Input(s)                      Slots      Streams
 *  /-------------------------------------------\   /-----\  /------------- \
 *
 * +-------------------------------------------------------------------------+
 * |                                                                         |
 * | +---------------------------------------------+                         |
 * | |   GstParseBin(s)                            |                         |
 * | |                +--------------+             |  +-----+                |
 * | |                |              |---[parser]-[|--| Mul |---[ decoder ]-[|
 * |]--[ typefind ]---|  demuxer(s)  |------------[|  | ti  |                |
 * | |                |  (if needed) |---[parser]-[|--| qu  |                |
 * | |                |              |---[parser]-[|--| eu  |---[ decoder ]-[|
 * | |                +--------------+             |  +------             ^  |
 * | +---------------------------------------------+        ^             |  |
 * |                                               ^        |             |  |
 * +-----------------------------------------------+--------+-------------+--+
 *                                                 |        |             |
 *                                                 |        |             |
 *                                       Probes  --/--------/-------------/
 *
 * ATOMIC SWITCHING
 *
 * We want to ensure we re-use decoders when switching streams. This takes place
 * at the multiqueue output level.
 *
 * MAIN CONCEPTS
 *  1) Activating a stream (i.e. linking a slot to an output) is only done within
 *    the streaming thread in the multiqueue_src_probe() and only if the
 *    stream is in the REQUESTED selection.
 *  2) Deactivating a stream (i.e. unlinking a slot from an output) is also done
 *    within the stream thread, but only in a purposefully called IDLE probe
 *    that calls reassign_slot().
 *
 * Based on those two principles, 3 "selection" of streams (stream-id) are used:
 * 1) requested_selection
 *    All streams within that list should be activated
 * 2) active_selection
 *    List of streams that are exposed by decodebin
 * 3) to_activate
 *    List of streams that will be moved to requested_selection in the
 *    reassign_slot() method (i.e. once a stream was deactivated, and the output
 *    was retargetted)
 */


GST_DEBUG_CATEGORY_STATIC (decodebin3_debug);
#define GST_CAT_DEFAULT decodebin3_debug

#define GST_TYPE_DECODEBIN3	 (gst_decodebin3_get_type ())

#define EXTRA_DEBUG 1

#define CUSTOM_FINAL_EOS_QUARK _custom_final_eos_quark_get ()
#define CUSTOM_FINAL_EOS_QUARK_DATA "custom-final-eos"
static GQuark
_custom_final_eos_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark =
        (gsize) g_quark_from_static_string ("decodebin3-custom-final-eos");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

typedef struct _GstDecodebin3 GstDecodebin3;
typedef struct _GstDecodebin3Class GstDecodebin3Class;

typedef struct _DecodebinInputStream DecodebinInputStream;
typedef struct _DecodebinInput DecodebinInput;
typedef struct _DecodebinOutputStream DecodebinOutputStream;

typedef struct
{
  GstElement *element;
  GstMessage *error;            // Last error message seen for that element
  GstMessage *latency;          // Last latency message seen for that element
} CandidateDecoder;

struct _GstDecodebin3
{
  GstBin bin;

  /* input_lock protects the following variables */
  GMutex input_lock;
  /* Main input (static sink pad) */
  DecodebinInput *main_input;
  /* Supplementary input (request sink pads) */
  GList *other_inputs;
  /* counter for input */
  guint32 input_counter;
  /* Current stream group_id (default : GST_GROUP_ID_INVALID) */
  guint32 current_group_id;
  /* End of variables protected by input_lock */

  GstElement *multiqueue;
  GstClockTime default_mq_min_interleave;
  GstClockTime current_mq_min_interleave;

  /* selection_lock protects access to following variables */
  GMutex selection_lock;
  GList *input_streams;         /* List of DecodebinInputStream for active collection */
  GList *output_streams;        /* List of DecodebinOutputStream used for output */
  GList *slots;                 /* List of MultiQueueSlot */
  guint slot_id;

  /* Active collection */
  GstStreamCollection *collection;
  /* requested selection of stream-id to activate post-multiqueue */
  GList *requested_selection;
  /* list of stream-id currently activated in output */
  GList *active_selection;
  /* List of stream-id that need to be activated (after a stream switch for ex) */
  GList *to_activate;
  /* Pending select streams event */
  guint32 select_streams_seqnum;
  /* pending list of streams to select (from downstream) */
  GList *pending_select_streams;
  /* TRUE if requested_selection was updated, will become FALSE once
   * it has fully transitioned to active */
  gboolean selection_updated;
  /* End of variables protected by selection_lock */
  gboolean upstream_selected;

  /* Factories */
  GMutex factories_lock;
  guint32 factories_cookie;
  /* All DECODABLE factories */
  GList *factories;
  /* Only DECODER factories */
  GList *decoder_factories;
  /* DECODABLE but not DECODER factories */
  GList *decodable_factories;

  /* counters for pads */
  guint32 apadcount, vpadcount, tpadcount, opadcount;

  /* Properties */
  GstCaps *caps;

  GList *candidate_decoders;
};

struct _GstDecodebin3Class
{
  GstBinClass class;

    gint (*select_stream) (GstDecodebin3 * dbin,
      GstStreamCollection * collection, GstStream * stream);
};

/* Input of decodebin, controls input pad and parsebin */
struct _DecodebinInput
{
  GstDecodebin3 *dbin;

  gboolean is_main;

  GstPad *ghost_sink;
  GstPad *parsebin_sink;

  GstStreamCollection *collection;      /* Active collection */
  gboolean upstream_selected;

  guint group_id;

  /* Either parsebin or identity is used */
  GstElement *parsebin;
  GstElement *identity;

  gulong pad_added_sigid;
  gulong pad_removed_sigid;
  gulong drained_sigid;

  /* TRUE if the input got drained */
  gboolean drained;

  /* TEMPORARY HACK for knowing if upstream is already parsed and identity can
   * be avoided */
  gboolean input_is_parsed;
};

/* Multiqueue Slots */
typedef struct _MultiQueueSlot
{
  guint id;

  GstDecodebin3 *dbin;
  /* Type of stream handled by this slot */
  GstStreamType type;

  /* Linked input and output */
  DecodebinInputStream *input;

  /* pending => last stream received on sink pad */
  GstStream *pending_stream;
  /* active => last stream outputted on source pad */
  GstStream *active_stream;

  GstPad *sink_pad, *src_pad;

  /* id of the MQ src_pad event probe */
  gulong probe_id;

  /* TRUE if EOS was pushed out by multiqueue */
  gboolean is_drained;

  DecodebinOutputStream *output;
} MultiQueueSlot;

/* Streams that are exposed downstream (i.e. output) */
struct _DecodebinOutputStream
{
  GstDecodebin3 *dbin;
  /* The type of stream handled by this output stream */
  GstStreamType type;

  /* The slot to which this output stream is currently connected to */
  MultiQueueSlot *slot;

  GstElement *decoder;          /* Optional */
  GstPad *decoder_sink, *decoder_src;
  gboolean linked;

  /* ghostpad */
  GstPad *src_pad;
  /* Flag if ghost pad is exposed */
  gboolean src_exposed;

  /* Reported decoder latency */
  GstClockTime decoder_latency;

  /* keyframe dropping probe */
  gulong drop_probe_id;
};

/* properties */
enum
{
  PROP_0,
  PROP_CAPS
};

/* signals */
enum
{
  SIGNAL_SELECT_STREAM,
  SIGNAL_ABOUT_TO_FINISH,
  LAST_SIGNAL
};
static guint gst_decodebin3_signals[LAST_SIGNAL] = { 0 };

#define SELECTION_LOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "selection locking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_lock (&dbin->selection_lock);				\
    GST_LOG_OBJECT (dbin,						\
		    "selection locked from thread %p",			\
		    g_thread_self ());					\
  } G_STMT_END

#define SELECTION_UNLOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "selection unlocking from thread %p",		\
		    g_thread_self ());					\
    g_mutex_unlock (&dbin->selection_lock);				\
  } G_STMT_END

#define INPUT_LOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "input locking from thread %p",			\
		    g_thread_self ());					\
    g_mutex_lock (&dbin->input_lock);				\
    GST_LOG_OBJECT (dbin,						\
		    "input locked from thread %p",			\
		    g_thread_self ());					\
  } G_STMT_END

#define INPUT_UNLOCK(dbin) G_STMT_START {				\
    GST_LOG_OBJECT (dbin,						\
		    "input unlocking from thread %p",		\
		    g_thread_self ());					\
    g_mutex_unlock (&dbin->input_lock);				\
  } G_STMT_END

GType gst_decodebin3_get_type (void);
#define gst_decodebin3_parent_class parent_class
G_DEFINE_TYPE (GstDecodebin3, gst_decodebin3, GST_TYPE_BIN);
#define _do_init \
    GST_DEBUG_CATEGORY_INIT (decodebin3_debug, "decodebin3", 0, "decoder bin");\
    playback_element_init (plugin);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (decodebin3, "decodebin3", GST_RANK_NONE,
    GST_TYPE_DECODEBIN3, _do_init);

static GstStaticCaps default_raw_caps = GST_STATIC_CAPS (DEFAULT_RAW_CAPS);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate request_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate text_src_template =
GST_STATIC_PAD_TEMPLATE ("text_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);


static void gst_decodebin3_dispose (GObject * object);
static void gst_decodebin3_finalize (GObject * object);
static void gst_decodebin3_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_decodebin3_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean parsebin_autoplug_continue_cb (GstElement *
    parsebin, GstPad * pad, GstCaps * caps, GstDecodebin3 * dbin);

static gint
gst_decodebin3_select_stream (GstDecodebin3 * dbin,
    GstStreamCollection * collection, GstStream * stream)
{
  GST_LOG_OBJECT (dbin, "default select-stream, returning -1");

  return -1;
}

static GstPad *gst_decodebin3_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * name, const GstCaps * caps);
static void gst_decodebin3_release_pad (GstElement * element, GstPad * pad);
static void handle_stream_collection (GstDecodebin3 * dbin,
    GstStreamCollection * collection, DecodebinInput * input);
static void gst_decodebin3_handle_message (GstBin * bin, GstMessage * message);
static GstStateChangeReturn gst_decodebin3_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_decodebin3_send_event (GstElement * element,
    GstEvent * event);

static void gst_decode_bin_update_factories_list (GstDecodebin3 * dbin);

static void reset_input (GstDecodebin3 * dbin, DecodebinInput * input);
static void free_input (GstDecodebin3 * dbin, DecodebinInput * input);
static DecodebinInput *create_new_input (GstDecodebin3 * dbin, gboolean main);
static gboolean set_input_group_id (DecodebinInput * input, guint32 * group_id);

static gboolean reconfigure_output_stream (DecodebinOutputStream * output,
    MultiQueueSlot * slot, GstMessage ** msg);
static void free_output_stream (GstDecodebin3 * dbin,
    DecodebinOutputStream * output);
static DecodebinOutputStream *create_output_stream (GstDecodebin3 * dbin,
    GstStreamType type);

static GstPadProbeReturn slot_unassign_probe (GstPad * pad,
    GstPadProbeInfo * info, MultiQueueSlot * slot);
static gboolean reassign_slot (GstDecodebin3 * dbin, MultiQueueSlot * slot);
static MultiQueueSlot *get_slot_for_input (GstDecodebin3 * dbin,
    DecodebinInputStream * input);
static void link_input_to_slot (DecodebinInputStream * input,
    MultiQueueSlot * slot);
static void free_multiqueue_slot (GstDecodebin3 * dbin, MultiQueueSlot * slot);
static void free_multiqueue_slot_async (GstDecodebin3 * dbin,
    MultiQueueSlot * slot);

static GstStreamCollection *get_merged_collection (GstDecodebin3 * dbin);
static void update_requested_selection (GstDecodebin3 * dbin);

/* FIXME: Really make all the parser stuff a self-contained helper object */
#include "gstdecodebin3-parse.c"

static gboolean
_gst_int_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gint res = g_value_get_int (handler_return);

  g_value_set_int (return_accu, res);

  if (res == -1)
    return TRUE;

  return FALSE;
}

static void
gst_decodebin3_class_init (GstDecodebin3Class * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBinClass *bin_klass = (GstBinClass *) klass;

  gobject_klass->dispose = gst_decodebin3_dispose;
  gobject_klass->finalize = gst_decodebin3_finalize;
  gobject_klass->set_property = gst_decodebin3_set_property;
  gobject_klass->get_property = gst_decodebin3_get_property;

  g_object_class_install_property (gobject_klass, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "The caps on which to stop decoding. (NULL = default)",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecodebin3::select-stream
   * @decodebin: a #GstDecodebin3
   * @collection: a #GstStreamCollection
   * @stream: a #GstStream
   *
   * This signal is emitted whenever @decodebin needs to decide whether
   * to expose a @stream of a given @collection.
   *
   * Note that the prefered way to select streams is to listen to
   * GST_MESSAGE_STREAM_COLLECTION on the bus and send a
   * GST_EVENT_SELECT_STREAMS with the streams the user wants.
   *
   * Returns: 1 if the stream should be selected, 0 if it shouldn't be selected.
   * A value of -1 (default) lets @decodebin decide what to do with the stream.
   * */
  gst_decodebin3_signals[SIGNAL_SELECT_STREAM] =
      g_signal_new ("select-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDecodebin3Class, select_stream),
      _gst_int_accumulator, NULL, NULL,
      G_TYPE_INT, 2, GST_TYPE_STREAM_COLLECTION, GST_TYPE_STREAM);

  /**
   * GstDecodebin3::about-to-finish:
   *
   * This signal is emitted when the data for the selected URI is
   * entirely buffered and it is safe to specify another URI.
   */
  gst_decodebin3_signals[SIGNAL_ABOUT_TO_FINISH] =
      g_signal_new ("about-to-finish", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0, G_TYPE_NONE);


  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_decodebin3_request_new_pad);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_decodebin3_change_state);
  element_class->send_event = GST_DEBUG_FUNCPTR (gst_decodebin3_send_event);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_decodebin3_release_pad);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&request_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&text_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class,
      "Decoder Bin 3", "Generic/Bin/Decoder",
      "Autoplug and decode to raw media",
      "Edward Hervey <edward@centricular.com>");

  bin_klass->handle_message = gst_decodebin3_handle_message;

  klass->select_stream = gst_decodebin3_select_stream;
}

static void
gst_decodebin3_init (GstDecodebin3 * dbin)
{
  /* Create main input */
  dbin->main_input = create_new_input (dbin, TRUE);

  dbin->multiqueue = gst_element_factory_make ("multiqueue", NULL);
  g_object_get (dbin->multiqueue, "min-interleave-time",
      &dbin->default_mq_min_interleave, NULL);
  dbin->current_mq_min_interleave = dbin->default_mq_min_interleave;
  g_object_set (dbin->multiqueue, "sync-by-running-time", TRUE,
      "max-size-buffers", 0, "use-interleave", TRUE, NULL);
  gst_bin_add ((GstBin *) dbin, dbin->multiqueue);

  dbin->current_group_id = GST_GROUP_ID_INVALID;

  g_mutex_init (&dbin->factories_lock);
  g_mutex_init (&dbin->selection_lock);
  g_mutex_init (&dbin->input_lock);

  dbin->caps = gst_static_caps_get (&default_raw_caps);

  GST_OBJECT_FLAG_SET (dbin, GST_BIN_FLAG_STREAMS_AWARE);
}

static void
gst_decodebin3_reset (GstDecodebin3 * dbin)
{
  GList *tmp;

  GST_DEBUG_OBJECT (dbin, "Resetting");

  /* Free output streams */
  for (tmp = dbin->output_streams; tmp; tmp = tmp->next) {
    DecodebinOutputStream *output = (DecodebinOutputStream *) tmp->data;
    free_output_stream (dbin, output);
  }
  g_list_free (dbin->output_streams);
  dbin->output_streams = NULL;

  /* Free multiqueue slots */
  for (tmp = dbin->slots; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    free_multiqueue_slot (dbin, slot);
  }
  g_list_free (dbin->slots);
  dbin->slots = NULL;
  dbin->current_group_id = GST_GROUP_ID_INVALID;

  /* Reset the inputs */
  reset_input (dbin, dbin->main_input);
  for (tmp = dbin->other_inputs; tmp; tmp = tmp->next) {
    reset_input (dbin, tmp->data);
  }

  /* Reset multiqueue to default interleave */
  g_object_set (dbin->multiqueue, "min-interleave-time",
      dbin->default_mq_min_interleave, NULL);
  dbin->current_mq_min_interleave = dbin->default_mq_min_interleave;
  dbin->upstream_selected = FALSE;

  g_list_free_full (dbin->requested_selection, g_free);
  dbin->requested_selection = NULL;

  g_list_free_full (dbin->active_selection, g_free);
  dbin->active_selection = NULL;

  g_list_free (dbin->to_activate);
  dbin->to_activate = NULL;

  g_list_free (dbin->pending_select_streams);
  dbin->pending_select_streams = NULL;

  dbin->selection_updated = FALSE;
}

static void
gst_decodebin3_dispose (GObject * object)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) object;
  GList *walk, *next;

  gst_decodebin3_reset (dbin);

  g_mutex_lock (&dbin->factories_lock);
  if (dbin->factories) {
    gst_plugin_feature_list_free (dbin->factories);
    dbin->factories = NULL;
  }
  if (dbin->decoder_factories) {
    g_list_free (dbin->decoder_factories);
    dbin->decoder_factories = NULL;
  }
  if (dbin->decodable_factories) {
    g_list_free (dbin->decodable_factories);
    dbin->decodable_factories = NULL;
  }
  g_mutex_unlock (&dbin->factories_lock);

  SELECTION_LOCK (dbin);
  if (dbin->collection) {
    gst_clear_object (&dbin->collection);
  }
  SELECTION_UNLOCK (dbin);

  INPUT_LOCK (dbin);
  if (dbin->main_input) {
    free_input (dbin, dbin->main_input);
    dbin->main_input = NULL;
  }

  for (walk = dbin->other_inputs; walk; walk = next) {
    DecodebinInput *input = walk->data;

    next = g_list_next (walk);

    free_input (dbin, input);
    dbin->other_inputs = g_list_delete_link (dbin->other_inputs, walk);
  }
  INPUT_UNLOCK (dbin);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decodebin3_finalize (GObject * object)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) object;

  g_mutex_clear (&dbin->factories_lock);
  g_mutex_clear (&dbin->selection_lock);
  g_mutex_clear (&dbin->input_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_decodebin3_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) object;

  switch (prop_id) {
    case PROP_CAPS:
      GST_OBJECT_LOCK (dbin);
      if (dbin->caps)
        gst_caps_unref (dbin->caps);
      dbin->caps = g_value_dup_boxed (value);
      GST_OBJECT_UNLOCK (dbin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_decodebin3_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) object;

  switch (prop_id) {
    case PROP_CAPS:
      GST_OBJECT_LOCK (dbin);
      g_value_set_boxed (value, dbin->caps);
      GST_OBJECT_UNLOCK (dbin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
parsebin_autoplug_continue_cb (GstElement * parsebin, GstPad * pad,
    GstCaps * caps, GstDecodebin3 * dbin)
{
  GST_DEBUG_OBJECT (pad, "caps %" GST_PTR_FORMAT, caps);

  /* If it matches our target caps, expose it */
  if (gst_caps_can_intersect (caps, dbin->caps))
    return FALSE;

  return TRUE;
}

/* This method should be called whenever a STREAM_START event
 * comes out of a given parsebin.
 * The caller shall replace the group_id if the function returns TRUE */
static gboolean
set_input_group_id (DecodebinInput * input, guint32 * group_id)
{
  GstDecodebin3 *dbin = input->dbin;

  if (input->group_id != *group_id) {
    if (input->group_id != GST_GROUP_ID_INVALID)
      GST_WARNING_OBJECT (dbin,
          "Group id changed (%" G_GUINT32_FORMAT " -> %" G_GUINT32_FORMAT
          ") on input %p ", input->group_id, *group_id, input);
    input->group_id = *group_id;
  }

  if (*group_id != dbin->current_group_id) {
    /* The input is being re-used with a different incoming stream, we do want
     * to change/unify to this new group-id */
    if (dbin->current_group_id == GST_GROUP_ID_INVALID) {
      GST_DEBUG_OBJECT (dbin,
          "Setting current group id to %" G_GUINT32_FORMAT, *group_id);
      dbin->current_group_id = *group_id;
    } else {
      GST_DEBUG_OBJECT (dbin, "Returning global group id %" G_GUINT32_FORMAT,
          dbin->current_group_id);
    }
    *group_id = dbin->current_group_id;
    return TRUE;
  }

  return FALSE;
}

static void
parsebin_drained_cb (GstElement * parsebin, DecodebinInput * input)
{
  GstDecodebin3 *dbin = input->dbin;
  gboolean all_drained;
  GList *tmp;

  GST_INFO_OBJECT (dbin, "input %p drained", input);
  input->drained = TRUE;

  all_drained = dbin->main_input->drained;
  for (tmp = dbin->other_inputs; tmp; tmp = tmp->next) {
    DecodebinInput *data = (DecodebinInput *) tmp->data;

    all_drained &= data->drained;
  }

  if (all_drained) {
    GST_INFO_OBJECT (dbin, "All inputs drained. Posting about-to-finish");
    g_signal_emit (dbin, gst_decodebin3_signals[SIGNAL_ABOUT_TO_FINISH], 0,
        NULL);
  }
}

static gboolean
clear_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GST_DEBUG_OBJECT (pad, "clearing sticky event %" GST_PTR_FORMAT, *event);
  gst_event_unref (*event);
  *event = NULL;
  return TRUE;
}

static gboolean
copy_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstPad *gpad = GST_PAD_CAST (user_data);

  GST_DEBUG_OBJECT (gpad, "store sticky event %" GST_PTR_FORMAT, *event);
  gst_pad_store_sticky_event (gpad, *event);

  return TRUE;
}

static gboolean
decode_pad_set_target (GstGhostPad * pad, GstPad * target)
{
  gboolean res = gst_ghost_pad_set_target (pad, target);
  if (!res)
    return res;

  if (target == NULL)
    gst_pad_sticky_events_foreach (GST_PAD_CAST (pad), clear_sticky_events,
        NULL);
  else
    gst_pad_sticky_events_foreach (target, copy_sticky_events, pad);

  return res;
}

typedef struct
{
  gboolean ret;
  GstPad *peer;
} SendStickyEventsData;

static gboolean
send_sticky_event (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  SendStickyEventsData *data = user_data;

  data->ret &= gst_pad_send_event (data->peer, gst_event_ref (*event));

  return data->ret;
}

static gboolean
send_sticky_events (GstDecodebin3 * dbin, GstPad * pad)
{
  SendStickyEventsData data;

  data.ret = TRUE;
  data.peer = gst_pad_get_peer (pad);

  gst_pad_sticky_events_foreach (pad, send_sticky_event, &data);

  gst_object_unref (data.peer);

  return data.ret;
}

static CandidateDecoder *
add_candidate_decoder (GstDecodebin3 * dbin, GstElement * element)
{
  GST_OBJECT_LOCK (dbin);
  CandidateDecoder *candidate;
  candidate = g_new0 (CandidateDecoder, 1);
  candidate->element = element;
  dbin->candidate_decoders =
      g_list_prepend (dbin->candidate_decoders, candidate);
  GST_OBJECT_UNLOCK (dbin);
  return candidate;
}

static void
remove_candidate_decoder (GstDecodebin3 * dbin, CandidateDecoder * candidate)
{
  GST_OBJECT_LOCK (dbin);
  dbin->candidate_decoders =
      g_list_remove (dbin->candidate_decoders, candidate);
  if (candidate->error)
    gst_message_unref (candidate->error);
  g_free (candidate);
  GST_OBJECT_UNLOCK (dbin);
}

/* Call with INPUT_LOCK taken */
static gboolean
ensure_input_parsebin (GstDecodebin3 * dbin, DecodebinInput * input)
{
  gboolean set_state = FALSE;

  if (input->parsebin == NULL) {
    input->parsebin = gst_element_factory_make ("parsebin", NULL);
    if (input->parsebin == NULL)
      goto no_parsebin;
    input->parsebin = gst_object_ref (input->parsebin);
    input->parsebin_sink = gst_element_get_static_pad (input->parsebin, "sink");
    input->pad_added_sigid =
        g_signal_connect (input->parsebin, "pad-added",
        (GCallback) parsebin_pad_added_cb, input);
    input->pad_removed_sigid =
        g_signal_connect (input->parsebin, "pad-removed",
        (GCallback) parsebin_pad_removed_cb, input);
    input->drained_sigid =
        g_signal_connect (input->parsebin, "drained",
        (GCallback) parsebin_drained_cb, input);
    g_signal_connect (input->parsebin, "autoplug-continue",
        (GCallback) parsebin_autoplug_continue_cb, dbin);
  }

  if (GST_OBJECT_PARENT (GST_OBJECT (input->parsebin)) != GST_OBJECT (dbin)) {
    /* The state lock is taken so that we ensure we are the one (de)activating
     * parsebin. We need to do this to ensure any activation taking place in
     * parsebin (including by elements doing upstream activation) are done
     * within the same thread. */
    GST_STATE_LOCK (input->parsebin);
    gst_bin_add (GST_BIN (dbin), input->parsebin);
    set_state = TRUE;
  }

  gst_ghost_pad_set_target (GST_GHOST_PAD (input->ghost_sink),
      input->parsebin_sink);

  if (set_state) {
    gst_element_sync_state_with_parent (input->parsebin);
    GST_STATE_UNLOCK (input->parsebin);
  }

  return TRUE;

  /* ERRORS */
no_parsebin:
  {
    gst_element_post_message ((GstElement *) dbin,
        gst_missing_element_message_new ((GstElement *) dbin, "parsebin"));
    return FALSE;
  }
}

static GstPadLinkReturn
gst_decodebin3_input_pad_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) parent;
  GstQuery *query;
  gboolean pull_mode = FALSE;
  gboolean has_caps = TRUE;
  GstPadLinkReturn res = GST_PAD_LINK_OK;
  DecodebinInput *input = g_object_get_data (G_OBJECT (pad), "decodebin.input");

  g_return_val_if_fail (input, GST_PAD_LINK_REFUSED);

  GST_LOG_OBJECT (parent, "Got link on input pad %" GST_PTR_FORMAT, pad);

  query = gst_query_new_scheduling ();
  if (gst_pad_query (peer, query)
      && gst_query_has_scheduling_mode_with_flags (query, GST_PAD_MODE_PULL,
          GST_SCHEDULING_FLAG_SEEKABLE))
    pull_mode = TRUE;
  gst_query_unref (query);

  GST_DEBUG_OBJECT (dbin, "Upstream can do pull-based : %d", pull_mode);

  if (!pull_mode) {
    /* If push-based, query if it will provide some caps */
    query = gst_query_new_caps (NULL);
    if (gst_pad_query (peer, query)) {
      GstCaps *rescaps = NULL;
      gst_query_parse_caps_result (query, &rescaps);
      if (!rescaps || gst_caps_is_any (rescaps) || gst_caps_is_empty (rescaps)) {
        GST_DEBUG_OBJECT (dbin, "Upstream can't provide caps");
        has_caps = FALSE;
      }
    }
    gst_query_unref (query);
  }

  /* If upstream *can* do pull-based OR it doesn't have any caps, we always use
   * a parsebin. If not, we will delay that decision to a later stage
   * (caps/stream/collection event processing) to figure out if one is really
   * needed or whether an identity element will be enough */
  INPUT_LOCK (dbin);
  if (pull_mode || !has_caps) {
    if (!ensure_input_parsebin (dbin, input))
      res = GST_PAD_LINK_REFUSED;
    else if (input->identity) {
      GST_ERROR_OBJECT (dbin,
          "Can't reconfigure input from push-based to pull-based");
      res = GST_PAD_LINK_REFUSED;
    }
  }

  /* Clear stream-collection corresponding to current INPUT.  We do not
   * recalculate the global one yet, it will be done when at least one
   * collection is received/computed for this input.
   */
  if (input->collection) {
    GST_DEBUG_OBJECT (pad, "Clearing input collection");
    gst_object_unref (input->collection);
    input->collection = NULL;
  }

  INPUT_UNLOCK (dbin);

  return res;
}

/* Drop duration query during _input_pad_unlink */
static GstPadProbeReturn
query_duration_drop_probe (GstPad * pad, GstPadProbeInfo * info,
    DecodebinInput * input)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (GST_IS_QUERY (GST_PAD_PROBE_INFO_DATA (info))) {
    GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);
    if (GST_QUERY_TYPE (query) == GST_QUERY_DURATION) {
      GST_LOG_OBJECT (pad, "stop forwarding query duration");
      ret = GST_PAD_PROBE_HANDLED;
    }
  }

  return ret;
}

/* CALL with INPUT LOCK */
static void
recalculate_group_id (GstDecodebin3 * dbin)
{
  guint32 common_group_id;
  GList *iter;

  GST_DEBUG_OBJECT (dbin,
      "recalculating, current global group_id: %" G_GUINT32_FORMAT,
      dbin->current_group_id);

  common_group_id = dbin->main_input->group_id;

  for (iter = dbin->other_inputs; iter; iter = iter->next) {
    DecodebinInput *input = iter->data;

    if (input->group_id != common_group_id) {
      if (common_group_id != GST_GROUP_ID_INVALID)
        return;

      common_group_id = input->group_id;
    }
  }

  if (common_group_id == dbin->current_group_id) {
    GST_DEBUG_OBJECT (dbin, "Global group_id hasn't changed");
  } else {
    GST_DEBUG_OBJECT (dbin, "Updating global group_id to %" G_GUINT32_FORMAT,
        common_group_id);
    dbin->current_group_id = common_group_id;
  }
}

/* CALL with INPUT LOCK */
static void
reset_input_parsebin (GstDecodebin3 * dbin, DecodebinInput * input)
{
  GList *iter;

  if (input->parsebin == NULL)
    return;

  GST_DEBUG_OBJECT (dbin, "Resetting %" GST_PTR_FORMAT, input->parsebin);

  GST_STATE_LOCK (dbin);
  gst_element_set_state (input->parsebin, GST_STATE_NULL);
  input->drained = FALSE;
  input->group_id = GST_GROUP_ID_INVALID;
  recalculate_group_id (dbin);
  for (iter = dbin->input_streams; iter; iter = iter->next) {
    DecodebinInputStream *istream = iter->data;
    if (istream->input == input)
      istream->saw_eos = TRUE;
  }
  gst_element_sync_state_with_parent (input->parsebin);
  GST_STATE_UNLOCK (dbin);
}


static void
gst_decodebin3_input_pad_unlink (GstPad * pad, GstPad * peer,
    DecodebinInput * input)
{
  GstDecodebin3 *dbin = input->dbin;

  g_return_if_fail (input);

  GST_LOG_OBJECT (dbin, "Got unlink on input pad %" GST_PTR_FORMAT, pad);

  INPUT_LOCK (dbin);

  if (input->parsebin && GST_PAD_MODE (pad) == GST_PAD_MODE_PULL) {
    GST_DEBUG_OBJECT (dbin, "Resetting parsebin since it's pull-based");
    reset_input_parsebin (dbin, input);
  }
  /* In all cases we will be receiving new stream-start and data */
  input->group_id = GST_GROUP_ID_INVALID;
  input->drained = FALSE;
  recalculate_group_id (dbin);

  INPUT_UNLOCK (dbin);
}

static void
gst_decodebin3_release_pad (GstElement * element, GstPad * pad)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) element;
  DecodebinInput *input = g_object_get_data (G_OBJECT (pad), "decodebin.input");
  GstStreamCollection *collection = NULL;
  gulong probe_id = 0;
  GstMessage *msg;

  g_return_if_fail (input);
  GST_LOG_OBJECT (dbin, "Releasing pad %" GST_PTR_FORMAT, pad);

  INPUT_LOCK (dbin);

  /* Clear stream-collection corresponding to current INPUT and post new
   * stream-collection message, if needed */
  if (input->collection) {
    gst_object_unref (input->collection);
    input->collection = NULL;
  }

  SELECTION_LOCK (dbin);
  collection = get_merged_collection (dbin);
  if (!collection) {
    SELECTION_UNLOCK (dbin);
    goto beach;
  }
  if (collection == dbin->collection) {
    SELECTION_UNLOCK (dbin);
    gst_object_unref (collection);
    goto beach;
  }

  GST_DEBUG_OBJECT (dbin, "Update Stream Collection");

  if (dbin->collection)
    gst_object_unref (dbin->collection);
  dbin->collection = collection;
  dbin->select_streams_seqnum = GST_SEQNUM_INVALID;

  msg =
      gst_message_new_stream_collection ((GstObject *) dbin, dbin->collection);

  if (input->parsebin)
    /* Drop duration queries that the application might be doing while this message is posted */
    probe_id = gst_pad_add_probe (input->parsebin_sink,
        GST_PAD_PROBE_TYPE_QUERY_UPSTREAM,
        (GstPadProbeCallback) query_duration_drop_probe, input, NULL);

  SELECTION_UNLOCK (dbin);
  gst_element_post_message (GST_ELEMENT_CAST (dbin), msg);
  update_requested_selection (dbin);

  if (input->parsebin) {
    gst_pad_remove_probe (input->parsebin_sink, probe_id);
  }

beach:
  if (!input->is_main) {
    dbin->other_inputs = g_list_remove (dbin->other_inputs, input);
    free_input (dbin, input);
  } else
    reset_input (dbin, input);

  INPUT_UNLOCK (dbin);
  return;
}

/* Call with INPUT LOCK */
static void
reset_input (GstDecodebin3 * dbin, DecodebinInput * input)
{
  GST_LOG_OBJECT (dbin, "Resetting input %p", input);

  gst_ghost_pad_set_target (GST_GHOST_PAD (input->ghost_sink), NULL);

  if (input->parsebin) {
    g_signal_handler_disconnect (input->parsebin, input->pad_removed_sigid);
    g_signal_handler_disconnect (input->parsebin, input->pad_added_sigid);
    g_signal_handler_disconnect (input->parsebin, input->drained_sigid);
    gst_element_set_state (input->parsebin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (dbin), input->parsebin);
    gst_clear_object (&input->parsebin);
    gst_clear_object (&input->parsebin_sink);
  }
  if (input->identity) {
    GstPad *idpad = gst_element_get_static_pad (input->identity, "src");
    DecodebinInputStream *stream = find_input_stream_for_pad (dbin, idpad);
    gst_object_unref (idpad);
    remove_input_stream (dbin, stream);
    gst_element_set_state (input->identity, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (dbin), input->identity);
    gst_clear_object (&input->identity);
  }
  if (input->collection)
    gst_clear_object (&input->collection);

  input->group_id = GST_GROUP_ID_INVALID;
}

/* Call with INPUT LOCK */
static void
free_input (GstDecodebin3 * dbin, DecodebinInput * input)
{
  reset_input (dbin, input);

  GST_LOG_OBJECT (dbin, "Freeing input %p", input);

  INPUT_UNLOCK (dbin);
  gst_element_remove_pad (GST_ELEMENT (dbin), input->ghost_sink);
  INPUT_LOCK (dbin);

  g_free (input);
}

static gboolean
sink_query_function (GstPad * sinkpad, GstDecodebin3 * dbin, GstQuery * query)
{
  DecodebinInput *input =
      g_object_get_data (G_OBJECT (sinkpad), "decodebin.input");

  g_return_val_if_fail (input, FALSE);

  GST_DEBUG_OBJECT (sinkpad, "query %" GST_PTR_FORMAT, query);

  /* We accept any caps, since we will reconfigure ourself internally if the new
   * stream is incompatible */
  if (GST_QUERY_TYPE (query) == GST_QUERY_ACCEPT_CAPS) {
    GST_DEBUG_OBJECT (dbin, "Accepting ACCEPT_CAPS query");
    gst_query_set_accept_caps_result (query, TRUE);
    return TRUE;
  }
  return gst_pad_query_default (sinkpad, GST_OBJECT (dbin), query);
}

static gboolean
is_parsebin_required_for_input (GstDecodebin3 * dbin, DecodebinInput * input,
    GstCaps * newcaps, GstPad * sinkpad)
{
  gboolean parsebin_needed = TRUE;
  GstStream *stream;

  stream = gst_pad_get_stream (sinkpad);

  if (stream == NULL) {
    /* If upstream didn't provide a `GstStream` we will need to create a
     * parsebin to handle that stream */
    GST_DEBUG_OBJECT (sinkpad,
        "Need to create parsebin since upstream doesn't provide GstStream");
  } else if (gst_caps_can_intersect (newcaps, dbin->caps)) {
    /* If the incoming caps match decodebin3 output, no processing is needed */
    GST_FIXME_OBJECT (sinkpad, "parsebin not needed (matches output caps) !");
    parsebin_needed = FALSE;
  } else if (input->input_is_parsed) {
    GST_DEBUG_OBJECT (sinkpad, "input is parsed, no parsebin needed");
    parsebin_needed = FALSE;
  } else {
    GList *decoder_list;
    /* If the incoming caps are compatible with a decoder, we don't need to
     * process it before */
    g_mutex_lock (&dbin->factories_lock);
    gst_decode_bin_update_factories_list (dbin);
    decoder_list =
        gst_element_factory_list_filter (dbin->decoder_factories, newcaps,
        GST_PAD_SINK, TRUE);
    g_mutex_unlock (&dbin->factories_lock);
    if (decoder_list) {
      GST_FIXME_OBJECT (sinkpad, "parsebin not needed (available decoders) !");
      gst_plugin_feature_list_free (decoder_list);
      parsebin_needed = FALSE;
    }
  }
  if (stream)
    gst_object_unref (stream);

  return parsebin_needed;
}

static void
setup_identify_for_input (GstDecodebin3 * dbin, DecodebinInput * input,
    GstPad * sinkpad)
{
  GstPad *idsrc, *idsink;
  DecodebinInputStream *inputstream;

  GST_DEBUG_OBJECT (sinkpad, "Adding identity for new input stream");

  input->identity = gst_element_factory_make ("identity", NULL);
  /* We drop allocation queries due to our usage of multiqueue just
   * afterwards. It is just too dangerous.
   *
   * If application users want to have optimal raw source <=> sink allocations
   * they should not use decodebin3
   */
  g_object_set (input->identity, "drop-allocation", TRUE, NULL);
  input->identity = gst_object_ref (input->identity);
  idsink = gst_element_get_static_pad (input->identity, "sink");
  idsrc = gst_element_get_static_pad (input->identity, "src");
  gst_bin_add (GST_BIN (dbin), input->identity);

  SELECTION_LOCK (dbin);
  inputstream = create_input_stream (dbin, idsrc, input);
  /* Forward any existing GstStream directly on the input stream */
  inputstream->active_stream = gst_pad_get_stream (sinkpad);
  SELECTION_UNLOCK (dbin);

  gst_object_unref (idsrc);
  gst_object_unref (idsink);
  gst_ghost_pad_set_target (GST_GHOST_PAD (input->ghost_sink), idsink);
  gst_element_sync_state_with_parent (input->identity);
}

static gboolean
sink_event_function (GstPad * sinkpad, GstDecodebin3 * dbin, GstEvent * event)
{
  DecodebinInput *input =
      g_object_get_data (G_OBJECT (sinkpad), "decodebin.input");

  g_return_val_if_fail (input, FALSE);

  GST_DEBUG_OBJECT (sinkpad, "event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    {
      GstQuery *q = gst_query_new_selectable ();
      const GstStructure *s = gst_event_get_structure (event);

      /* Query whether upstream can handle stream selection or not */
      if (gst_pad_peer_query (sinkpad, q)) {
        gst_query_parse_selectable (q, &input->upstream_selected);
        GST_DEBUG_OBJECT (sinkpad, "Upstream is selectable : %d",
            input->upstream_selected);
      } else {
        input->upstream_selected = FALSE;
        GST_DEBUG_OBJECT (sinkpad, "Upstream does not handle SELECTABLE query");
      }
      gst_query_unref (q);

      /* FIXME : We force `decodebin3` to upstream selection mode if *any* of the
         inputs is. This means things might break if there's a mix */
      if (input->upstream_selected)
        dbin->upstream_selected = TRUE;

      input->input_is_parsed = s
          && gst_structure_has_field (s, "urisourcebin-parsed-data");

      /* Make sure group ids will be recalculated */
      input->group_id = GST_GROUP_ID_INVALID;
      INPUT_LOCK (dbin);
      recalculate_group_id (dbin);
      INPUT_UNLOCK (dbin);
      break;
    }
    case GST_EVENT_STREAM_COLLECTION:
    {
      GstStreamCollection *collection = NULL;

      gst_event_parse_stream_collection (event, &collection);
      if (collection) {
        INPUT_LOCK (dbin);
        handle_stream_collection (dbin, collection, input);
        gst_object_unref (collection);
        INPUT_UNLOCK (dbin);
        SELECTION_LOCK (dbin);
        /* Post the (potentially) updated collection */
        if (dbin->collection) {
          GstMessage *msg;
          msg =
              gst_message_new_stream_collection ((GstObject *) dbin,
              dbin->collection);
          SELECTION_UNLOCK (dbin);
          gst_element_post_message (GST_ELEMENT_CAST (dbin), msg);
          update_requested_selection (dbin);
        } else
          SELECTION_UNLOCK (dbin);
      }

      /* If we are waiting to create an identity passthrough, do it now */
      if (!input->parsebin && !input->identity)
        setup_identify_for_input (dbin, input, sinkpad);
      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *newcaps = NULL;

      gst_event_parse_caps (event, &newcaps);
      if (!newcaps)
        break;
      GST_DEBUG_OBJECT (sinkpad, "new caps %" GST_PTR_FORMAT, newcaps);

      /* No parsebin or identity present, check if we can avoid creating one */
      if (!input->parsebin && !input->identity) {
        if (is_parsebin_required_for_input (dbin, input, newcaps, sinkpad)) {
          GST_DEBUG_OBJECT (sinkpad, "parsebin is required for input");
          ensure_input_parsebin (dbin, input);
          break;
        }
        GST_DEBUG_OBJECT (sinkpad,
            "parsebin not required. Will create identity passthrough element once we get the collection");
        break;
      }

      if (input->identity) {
        if (is_parsebin_required_for_input (dbin, input, newcaps, sinkpad)) {
          GST_ERROR_OBJECT (sinkpad,
              "Switching from passthrough to parsebin on inputs is not supported !");
          gst_event_unref (event);
          return FALSE;
        }
        /* Nothing else to do here */
        break;
      }

      /* Check if the parsebin present can handle the new caps */
      g_assert (input->parsebin);
      GST_DEBUG_OBJECT (sinkpad,
          "New caps, checking if they are compatible with existing parsebin");
      if (!gst_pad_query_accept_caps (input->parsebin_sink, newcaps)) {
        GST_DEBUG_OBJECT (sinkpad,
            "Parsebin doesn't accept the new caps %" GST_PTR_FORMAT, newcaps);
        /* Reset parsebin so that it reconfigures itself for the new stream format */
        INPUT_LOCK (dbin);
        reset_input_parsebin (dbin, input);
        INPUT_UNLOCK (dbin);
      } else {
        GST_DEBUG_OBJECT (sinkpad, "Parsebin accepts new caps");
      }
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment = NULL;
      gst_event_parse_segment (event, &segment);

      /* All data reaching multiqueue must be in time format. If it's not, we
       * need to use a parsebin on the incoming stream.
       */
      if (segment && segment->format != GST_FORMAT_TIME && !input->parsebin) {
        GST_DEBUG_OBJECT (sinkpad,
            "Got a non-time segment, forcing parsebin handling");
        ensure_input_parsebin (dbin, input);
      }
      break;
    }
    default:
      break;
  }

  /* Chain to parent function */
  return gst_pad_event_default (sinkpad, GST_OBJECT (dbin), event);
}

/* Call with INPUT_LOCK taken */
static DecodebinInput *
create_new_input (GstDecodebin3 * dbin, gboolean main)
{
  DecodebinInput *input;

  input = g_new0 (DecodebinInput, 1);
  input->dbin = dbin;
  input->is_main = main;
  input->group_id = GST_GROUP_ID_INVALID;
  if (main)
    input->ghost_sink = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  else {
    gchar *pad_name = g_strdup_printf ("sink_%u", dbin->input_counter++);
    input->ghost_sink = gst_ghost_pad_new_no_target (pad_name, GST_PAD_SINK);
    g_free (pad_name);
  }
  input->upstream_selected = FALSE;
  g_object_set_data (G_OBJECT (input->ghost_sink), "decodebin.input", input);
  gst_pad_set_event_function (input->ghost_sink,
      (GstPadEventFunction) sink_event_function);
  gst_pad_set_query_function (input->ghost_sink,
      (GstPadQueryFunction) sink_query_function);
  gst_pad_set_link_function (input->ghost_sink, gst_decodebin3_input_pad_link);
  g_signal_connect (input->ghost_sink, "unlinked",
      (GCallback) gst_decodebin3_input_pad_unlink, input);

  gst_pad_set_active (input->ghost_sink, TRUE);
  gst_element_add_pad ((GstElement *) dbin, input->ghost_sink);

  return input;

}

static GstPad *
gst_decodebin3_request_new_pad (GstElement * element, GstPadTemplate * temp,
    const gchar * name, const GstCaps * caps)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) element;
  DecodebinInput *input;
  GstPad *res = NULL;

  /* We are ignoring names for the time being, not sure it makes any sense
   * within the context of decodebin3 ... */
  input = create_new_input (dbin, FALSE);
  if (input) {
    INPUT_LOCK (dbin);
    dbin->other_inputs = g_list_append (dbin->other_inputs, input);
    res = input->ghost_sink;
    INPUT_UNLOCK (dbin);
  }

  return res;
}

/* Must be called with factories lock! */
static void
gst_decode_bin_update_factories_list (GstDecodebin3 * dbin)
{
  guint cookie;

  cookie = gst_registry_get_feature_list_cookie (gst_registry_get ());
  if (!dbin->factories || dbin->factories_cookie != cookie) {
    GList *tmp;
    if (dbin->factories)
      gst_plugin_feature_list_free (dbin->factories);
    if (dbin->decoder_factories)
      g_list_free (dbin->decoder_factories);
    if (dbin->decodable_factories)
      g_list_free (dbin->decodable_factories);
    dbin->factories =
        gst_element_factory_list_get_elements
        (GST_ELEMENT_FACTORY_TYPE_DECODABLE, GST_RANK_MARGINAL);
    dbin->factories =
        g_list_sort (dbin->factories, gst_plugin_feature_rank_compare_func);
    dbin->factories_cookie = cookie;

    /* Filter decoder and other decodables */
    dbin->decoder_factories = NULL;
    dbin->decodable_factories = NULL;
    for (tmp = dbin->factories; tmp; tmp = tmp->next) {
      GstElementFactory *fact = (GstElementFactory *) tmp->data;
      if (gst_element_factory_list_is_type (fact,
              GST_ELEMENT_FACTORY_TYPE_DECODER))
        dbin->decoder_factories = g_list_append (dbin->decoder_factories, fact);
      else
        dbin->decodable_factories =
            g_list_append (dbin->decodable_factories, fact);
    }
  }
}

/* Must be called with appropriate lock if list is a protected variable */
static const gchar *
stream_in_list (GList * list, const gchar * sid)
{
  GList *tmp;

#if EXTRA_DEBUG
  for (tmp = list; tmp; tmp = tmp->next) {
    gchar *osid = (gchar *) tmp->data;
    GST_DEBUG ("Checking %s against %s", sid, osid);
  }
#endif

  for (tmp = list; tmp; tmp = tmp->next) {
    const gchar *osid = (gchar *) tmp->data;
    if (!g_strcmp0 (sid, osid))
      return osid;
  }

  return NULL;
}

static GList *
remove_from_list (GList * list, const gchar * sid)
{
  GList *tmp;

  for (tmp = list; tmp; tmp = tmp->next) {
    gchar *osid = tmp->data;
    if (!g_strcmp0 (sid, osid)) {
      g_free (osid);
      return g_list_delete_link (list, tmp);
    }
  }
  return list;
}

static gboolean
stream_list_equal (GList * lista, GList * listb)
{
  GList *tmp;

  if (g_list_length (lista) != g_list_length (listb))
    return FALSE;

  for (tmp = lista; tmp; tmp = tmp->next) {
    gchar *osid = tmp->data;
    if (!stream_in_list (listb, osid))
      return FALSE;
  }

  return TRUE;
}

static void
update_requested_selection (GstDecodebin3 * dbin)
{
  guint i, nb;
  GList *tmp = NULL;
  gboolean all_user_selected = TRUE;
  GstStreamType used_types = 0;
  GstStreamCollection *collection;

  /* 1. Is there a pending SELECT_STREAMS we can return straight away since
   *  the switch handler will take care of the pending selection */
  SELECTION_LOCK (dbin);
  if (dbin->pending_select_streams) {
    GST_DEBUG_OBJECT (dbin,
        "No need to create pending selection, SELECT_STREAMS underway");
    goto beach;
  }

  collection = dbin->collection;
  if (G_UNLIKELY (collection == NULL)) {
    GST_DEBUG_OBJECT (dbin, "No current GstStreamCollection");
    goto beach;
  }
  nb = gst_stream_collection_get_size (collection);

  /* 2. If not, are we in EXPOSE_ALL_MODE ? If so, match everything */
  GST_FIXME_OBJECT (dbin, "Implement EXPOSE_ALL_MODE");

  /* 3. If not, check if we already have some of the streams in the
   * existing active/requested selection */
  for (i = 0; i < nb; i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    const gchar *sid = gst_stream_get_stream_id (stream);
    gint request = -1;
    /* Fire select-stream signal to see if outside components want to
     * hint at which streams should be selected */
    g_signal_emit (G_OBJECT (dbin),
        gst_decodebin3_signals[SIGNAL_SELECT_STREAM], 0, collection, stream,
        &request);
    GST_DEBUG_OBJECT (dbin, "stream %s , request:%d", sid, request);

    if (request == -1)
      all_user_selected = FALSE;
    if (request == 1 || (request == -1
            && (stream_in_list (dbin->requested_selection, sid)
                || stream_in_list (dbin->active_selection, sid)))) {
      GstStreamType curtype = gst_stream_get_stream_type (stream);
      if (request == 1)
        GST_DEBUG_OBJECT (dbin,
            "Using stream requested by 'select-stream' signal : %s", sid);
      else
        GST_DEBUG_OBJECT (dbin,
            "Re-using stream already present in requested or active selection : %s",
            sid);
      tmp = g_list_append (tmp, (gchar *) sid);
      used_types |= curtype;
    }
  }

  /* 4. If the user didn't explicitly selected all streams, match one stream of each type */
  if (!all_user_selected && dbin->select_streams_seqnum == GST_SEQNUM_INVALID) {
    for (i = 0; i < nb; i++) {
      GstStream *stream = gst_stream_collection_get_stream (collection, i);
      GstStreamType curtype = gst_stream_get_stream_type (stream);
      if (curtype != GST_STREAM_TYPE_UNKNOWN && !(used_types & curtype)) {
        const gchar *sid = gst_stream_get_stream_id (stream);
        GST_DEBUG_OBJECT (dbin,
            "Automatically selecting stream '%s' of type %s", sid,
            gst_stream_type_get_name (curtype));
        tmp = g_list_append (tmp, (gchar *) sid);
        used_types |= curtype;
      }
    }
  }

beach:
  if (stream_list_equal (tmp, dbin->requested_selection)) {
    /* If the selection is equal, there is nothign to do */
    GST_DEBUG_OBJECT (dbin, "Dropping duplicate selection");
    g_list_free (tmp);
    tmp = NULL;
  }

  if (tmp) {
    /* Finally set the requested selection */
    if (dbin->requested_selection) {
      GST_FIXME_OBJECT (dbin,
          "Replacing non-NULL requested_selection, what should we do ??");
      g_list_free_full (dbin->requested_selection, g_free);
    }
    dbin->requested_selection =
        g_list_copy_deep (tmp, (GCopyFunc) g_strdup, NULL);
    dbin->selection_updated = TRUE;
    g_list_free (tmp);
  }
  SELECTION_UNLOCK (dbin);
}

/* sort_streams:
 * GCompareFunc to use with lists of GstStream.
 * Sorts GstStreams by stream type and SELECT flag and stream-id
 * First video, then audio, then others.
 *
 * Return: negative if a<b, 0 if a==b, positive if a>b
 */
static gint
sort_streams (GstStream * sa, GstStream * sb)
{
  GstStreamType typea, typeb;
  GstStreamFlags flaga, flagb;
  const gchar *ida, *idb;
  gint ret = 0;

  typea = gst_stream_get_stream_type (sa);
  typeb = gst_stream_get_stream_type (sb);

  GST_LOG ("sa(%s), sb(%s)", gst_stream_get_stream_id (sa),
      gst_stream_get_stream_id (sb));

  /* Sort by stream type. First video, then audio, then others(text, container, unknown) */
  if (typea != typeb) {
    if (typea & GST_STREAM_TYPE_VIDEO)
      ret = -1;
    else if (typea & GST_STREAM_TYPE_AUDIO)
      ret = (!(typeb & GST_STREAM_TYPE_VIDEO)) ? -1 : 1;
    else if (typea & GST_STREAM_TYPE_TEXT)
      ret = (!(typeb & GST_STREAM_TYPE_VIDEO)
          && !(typeb & GST_STREAM_TYPE_AUDIO)) ? -1 : 1;
    else if (typea & GST_STREAM_TYPE_CONTAINER)
      ret = (typeb & GST_STREAM_TYPE_UNKNOWN) ? -1 : 1;
    else
      ret = 1;

    if (ret != 0) {
      GST_LOG ("Sort by stream-type: %d", ret);
      return ret;
    }
  }

  /* Sort by SELECT flag, if stream type is same. */
  flaga = gst_stream_get_stream_flags (sa);
  flagb = gst_stream_get_stream_flags (sb);

  ret =
      (flaga & GST_STREAM_FLAG_SELECT) ? ((flagb & GST_STREAM_FLAG_SELECT) ? 0 :
      -1) : ((flagb & GST_STREAM_FLAG_SELECT) ? 1 : 0);

  if (ret != 0) {
    GST_LOG ("Sort by SELECT flag: %d", ret);
    return ret;
  }

  /* Sort by stream-id, if otherwise the same. */
  ida = gst_stream_get_stream_id (sa);
  idb = gst_stream_get_stream_id (sb);
  ret = g_strcmp0 (ida, idb);

  GST_LOG ("Sort by stream-id: %d", ret);

  return ret;
}

/* Call with INPUT_LOCK taken */
static GstStreamCollection *
get_merged_collection (GstDecodebin3 * dbin)
{
  gboolean needs_merge = FALSE;
  GstStreamCollection *res = NULL;
  GList *tmp;
  GList *unsorted_streams = NULL;
  guint i, nb_stream;

  /* First check if we need to do a merge or just return the only collection */
  res = dbin->main_input->collection;

  for (tmp = dbin->other_inputs; tmp; tmp = tmp->next) {
    DecodebinInput *input = (DecodebinInput *) tmp->data;
    GST_LOG_OBJECT (dbin, "Comparing res %p input->collection %p", res,
        input->collection);
    if (input->collection && input->collection != res) {
      if (res) {
        needs_merge = TRUE;
        break;
      }
      res = input->collection;
    }
  }

  if (!needs_merge) {
    GST_DEBUG_OBJECT (dbin, "No need to merge, returning %p", res);
    return res ? gst_object_ref (res) : NULL;
  }

  /* We really need to create a new collection */
  /* FIXME : Some numbering scheme maybe ?? */
  res = gst_stream_collection_new ("decodebin3");
  if (dbin->main_input->collection) {
    nb_stream = gst_stream_collection_get_size (dbin->main_input->collection);
    GST_DEBUG_OBJECT (dbin, "main input %p %d", dbin->main_input, nb_stream);
    for (i = 0; i < nb_stream; i++) {
      GstStream *stream =
          gst_stream_collection_get_stream (dbin->main_input->collection, i);
      unsorted_streams = g_list_append (unsorted_streams, stream);
    }
  }

  for (tmp = dbin->other_inputs; tmp; tmp = tmp->next) {
    DecodebinInput *input = (DecodebinInput *) tmp->data;
    GST_DEBUG_OBJECT (dbin, "input %p , collection %p", input,
        input->collection);
    if (input->collection) {
      nb_stream = gst_stream_collection_get_size (input->collection);
      GST_DEBUG_OBJECT (dbin, "nb_stream : %d", nb_stream);
      for (i = 0; i < nb_stream; i++) {
        GstStream *stream =
            gst_stream_collection_get_stream (input->collection, i);
        /* Only add if not already present in the list */
        if (!g_list_find (unsorted_streams, stream))
          unsorted_streams = g_list_append (unsorted_streams, stream);
      }
    }
  }

  /* re-order streams : video, then audio, then others */
  unsorted_streams =
      g_list_sort (unsorted_streams, (GCompareFunc) sort_streams);
  for (tmp = unsorted_streams; tmp; tmp = tmp->next) {
    GstStream *stream = (GstStream *) tmp->data;
    GST_DEBUG_OBJECT (dbin, "Adding #stream(%s) to collection",
        gst_stream_get_stream_id (stream));
    gst_stream_collection_add_stream (res, gst_object_ref (stream));
  }

  if (unsorted_streams)
    g_list_free (unsorted_streams);

  return res;
}

/* Call with INPUT_LOCK taken */
static DecodebinInput *
find_message_parsebin (GstDecodebin3 * dbin, GstElement * child)
{
  DecodebinInput *input = NULL;
  GstElement *parent = gst_object_ref (child);
  GList *tmp;

  do {
    GstElement *next_parent;

    GST_DEBUG_OBJECT (dbin, "parent %s",
        parent ? GST_ELEMENT_NAME (parent) : "<NONE>");

    if (parent == dbin->main_input->parsebin) {
      input = dbin->main_input;
      break;
    }
    for (tmp = dbin->other_inputs; tmp; tmp = tmp->next) {
      DecodebinInput *cur = (DecodebinInput *) tmp->data;
      if (parent == cur->parsebin) {
        input = cur;
        break;
      }
    }
    next_parent = (GstElement *) gst_element_get_parent (parent);
    gst_object_unref (parent);
    parent = next_parent;

  } while (parent && parent != (GstElement *) dbin);

  if (parent)
    gst_object_unref (parent);

  return input;
}

static const gchar *
stream_in_collection (GstDecodebin3 * dbin, gchar * sid)
{
  guint i, len;

  if (dbin->collection == NULL)
    return NULL;
  len = gst_stream_collection_get_size (dbin->collection);
  for (i = 0; i < len; i++) {
    GstStream *stream = gst_stream_collection_get_stream (dbin->collection, i);
    const gchar *osid = gst_stream_get_stream_id (stream);
    if (!g_strcmp0 (sid, osid))
      return osid;
  }

  return NULL;
}

/* Call with INPUT_LOCK taken */
static void
handle_stream_collection (GstDecodebin3 * dbin,
    GstStreamCollection * collection, DecodebinInput * input)
{
#ifndef GST_DISABLE_GST_DEBUG
  const gchar *upstream_id;
  guint i;
#endif
  if (!input) {
    GST_DEBUG_OBJECT (dbin,
        "Couldn't find corresponding input, most likely shutting down");
    return;
  }

  /* Replace collection in input */
  if (input->collection)
    gst_object_unref (input->collection);
  input->collection = gst_object_ref (collection);
  GST_DEBUG_OBJECT (dbin, "Setting collection %p on input %p", collection,
      input);

  /* Merge collection if needed */
  collection = get_merged_collection (dbin);

#ifndef GST_DISABLE_GST_DEBUG
  /* Just some debugging */
  upstream_id = gst_stream_collection_get_upstream_id (collection);
  GST_DEBUG ("Received Stream Collection. Upstream_id : %s", upstream_id);
  GST_DEBUG ("From input %p", input);
  GST_DEBUG ("  %d streams", gst_stream_collection_get_size (collection));
  for (i = 0; i < gst_stream_collection_get_size (collection); i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    GstTagList *taglist;
    GstCaps *caps;

    GST_DEBUG ("   Stream '%s'", gst_stream_get_stream_id (stream));
    GST_DEBUG ("     type  : %s",
        gst_stream_type_get_name (gst_stream_get_stream_type (stream)));
    GST_DEBUG ("     flags : 0x%x", gst_stream_get_stream_flags (stream));
    taglist = gst_stream_get_tags (stream);
    GST_DEBUG ("     tags  : %" GST_PTR_FORMAT, taglist);
    caps = gst_stream_get_caps (stream);
    GST_DEBUG ("     caps  : %" GST_PTR_FORMAT, caps);
    if (taglist)
      gst_tag_list_unref (taglist);
    if (caps)
      gst_caps_unref (caps);
  }
#endif

  /* Store collection for later usage */
  SELECTION_LOCK (dbin);
  if (dbin->collection == NULL) {
    dbin->collection = collection;
  } else {
    /* We need to check who emitted this collection (the owner).
     * If we already had a collection from that user, this one is an update,
     * that is to say that we need to figure out how we are going to re-use
     * the streams/slot */
    GST_FIXME_OBJECT (dbin, "New collection but already had one ...");
    /* FIXME : When do we switch from pending collection to active collection ?
     * When all streams from active collection are drained in multiqueue output ? */
    gst_object_unref (dbin->collection);
    dbin->collection = collection;
  }
  dbin->select_streams_seqnum = GST_SEQNUM_INVALID;
  SELECTION_UNLOCK (dbin);
}

/* Must be called with the selection lock taken */
static void
gst_decodebin3_update_min_interleave (GstDecodebin3 * dbin)
{
  GstClockTime max_latency = GST_CLOCK_TIME_NONE;
  GList *tmp;

  GST_DEBUG_OBJECT (dbin, "Recalculating max latency of decoders");
  for (tmp = dbin->output_streams; tmp; tmp = tmp->next) {
    DecodebinOutputStream *out = (DecodebinOutputStream *) tmp->data;
    if (GST_CLOCK_TIME_IS_VALID (out->decoder_latency)) {
      if (max_latency == GST_CLOCK_TIME_NONE
          || out->decoder_latency > max_latency)
        max_latency = out->decoder_latency;
    }
  }
  GST_DEBUG_OBJECT (dbin, "max latency of all decoders: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (max_latency));

  if (!GST_CLOCK_TIME_IS_VALID (max_latency))
    return;

  /* Make sure we keep an extra overhead */
  max_latency += 100 * GST_MSECOND;
  if (max_latency == dbin->current_mq_min_interleave)
    return;

  dbin->current_mq_min_interleave = max_latency;
  GST_DEBUG_OBJECT (dbin, "Setting mq min-interleave to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (dbin->current_mq_min_interleave));
  g_object_set (dbin->multiqueue, "min-interleave-time",
      dbin->current_mq_min_interleave, NULL);
}

static void
gst_decodebin3_handle_message (GstBin * bin, GstMessage * message)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) bin;
  gboolean posting_collection = FALSE;
  GList *l;

  GST_DEBUG_OBJECT (bin, "Got Message %s", GST_MESSAGE_TYPE_NAME (message));

  GST_OBJECT_LOCK (dbin);
  for (l = dbin->candidate_decoders; l; l = l->next) {
    CandidateDecoder *candidate = l->data;
    if (GST_OBJECT_CAST (candidate->element) == GST_MESSAGE_SRC (message)) {
      if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
        if (candidate->error)
          gst_message_unref (candidate->error);
        candidate->error = message;
        GST_OBJECT_UNLOCK (dbin);
        return;
      } else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_LATENCY) {
        if (candidate->latency)
          gst_message_unref (candidate->latency);
        GST_DEBUG_OBJECT (bin, "store latency message for %" GST_PTR_FORMAT,
            candidate->element);
        candidate->latency = message;
        GST_OBJECT_UNLOCK (dbin);
        return;
      }
      break;
    }
  }
  GST_OBJECT_UNLOCK (dbin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STREAM_COLLECTION:
    {
      GstStreamCollection *collection = NULL;
      DecodebinInput *input;

      INPUT_LOCK (dbin);
      input =
          find_message_parsebin (dbin,
          (GstElement *) GST_MESSAGE_SRC (message));
      if (input == NULL) {
        GST_DEBUG_OBJECT (dbin,
            "Couldn't find corresponding input, most likely shutting down");
        INPUT_UNLOCK (dbin);
        break;
      }
      if (input->upstream_selected) {
        GST_DEBUG_OBJECT (dbin,
            "Upstream handles selection, not using/forwarding collection");
        INPUT_UNLOCK (dbin);
        goto drop_message;
      }
      gst_message_parse_stream_collection (message, &collection);
      if (collection) {
        handle_stream_collection (dbin, collection, input);
        posting_collection = TRUE;
      }
      INPUT_UNLOCK (dbin);

      SELECTION_LOCK (dbin);
      if (dbin->collection) {
        /* Replace collection message, we most likely aggregated it */
        GstMessage *new_msg;
        new_msg =
            gst_message_new_stream_collection ((GstObject *) dbin,
            dbin->collection);
        gst_message_unref (message);
        message = new_msg;
      }
      SELECTION_UNLOCK (dbin);

      if (collection)
        gst_object_unref (collection);
      break;
    }
    case GST_MESSAGE_LATENCY:
    {
      GList *tmp;
      /* Check if this is from one of our decoders */
      SELECTION_LOCK (dbin);
      for (tmp = dbin->output_streams; tmp; tmp = tmp->next) {
        DecodebinOutputStream *out = (DecodebinOutputStream *) tmp->data;
        if (out->decoder == (GstElement *) GST_MESSAGE_SRC (message)) {
          GstClockTime min, max;
          if (GST_IS_VIDEO_DECODER (out->decoder)) {
            gst_video_decoder_get_latency (GST_VIDEO_DECODER (out->decoder),
                &min, &max);
            GST_DEBUG_OBJECT (dbin,
                "Got latency update from one of our decoders. min: %"
                GST_TIME_FORMAT " max: %" GST_TIME_FORMAT, GST_TIME_ARGS (min),
                GST_TIME_ARGS (max));
            out->decoder_latency = min;
            /* Trigger recalculation */
            gst_decodebin3_update_min_interleave (dbin);
          }
          break;
        }
      }
      SELECTION_UNLOCK (dbin);
    }
    default:
      break;
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);

  if (posting_collection) {
    /* Figure out a selection for that collection */
    update_requested_selection (dbin);
  }

  return;

drop_message:
  {
    GST_DEBUG_OBJECT (bin, "dropping message");
    gst_message_unref (message);
  }
}

static void
handle_stored_latency_message (GstDecodebin3 * dbin,
    DecodebinOutputStream * output, CandidateDecoder * candidate)
{
  GstClockTime min, max;
  if (candidate->latency && GST_IS_VIDEO_DECODER (candidate->element)) {
    gst_video_decoder_get_latency (GST_VIDEO_DECODER (candidate->element),
        &min, &max);
    GST_DEBUG_OBJECT (dbin,
        "Got latency update from %" GST_PTR_FORMAT ". min: %"
        GST_TIME_FORMAT " max: %" GST_TIME_FORMAT, candidate->element,
        GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    output->decoder_latency = min;
    /* Trigger recalculation */
    gst_decodebin3_update_min_interleave (dbin);

    GST_BIN_CLASS (parent_class)->handle_message (GST_BIN_CAST (dbin),
        candidate->latency);
  }
}

static DecodebinOutputStream *
find_free_compatible_output (GstDecodebin3 * dbin, GstStream * stream)
{
  GList *tmp;
  GstStreamType stype = gst_stream_get_stream_type (stream);

  for (tmp = dbin->output_streams; tmp; tmp = tmp->next) {
    DecodebinOutputStream *output = (DecodebinOutputStream *) tmp->data;
    if (output->type == stype && output->slot && output->slot->active_stream) {
      GstStream *tstream = output->slot->active_stream;
      if (!stream_in_list (dbin->requested_selection,
              (gchar *) gst_stream_get_stream_id (tstream))) {
        return output;
      }
    }
  }

  return NULL;
}

/* Give a certain slot, figure out if it should be linked to an
 * output stream
 * CALL WITH SELECTION LOCK TAKEN !*/
static DecodebinOutputStream *
get_output_for_slot (MultiQueueSlot * slot)
{
  GstDecodebin3 *dbin = slot->dbin;
  DecodebinOutputStream *output = NULL;
  const gchar *stream_id;
  GstCaps *caps;
  gchar *id_in_list = NULL;

  /* If we already have a configured output, just use it */
  if (slot->output != NULL)
    return slot->output;

  /*
   * FIXME
   *
   * This method needs to be split into multiple parts
   *
   * 1) Figure out whether stream should be exposed or not
   *   This is based on autoplug-continue, EXPOSE_ALL_MODE, or presence
   *   in the default stream attribution
   *
   * 2) Figure out whether an output stream should be created, whether
   *   we can re-use the output stream already linked to the slot, or
   *   whether we need to get re-assigned another (currently used) output
   *   stream.
   */

  stream_id = gst_stream_get_stream_id (slot->active_stream);
  caps = gst_stream_get_caps (slot->active_stream);
  GST_DEBUG_OBJECT (dbin, "stream %s , %" GST_PTR_FORMAT, stream_id, caps);
  gst_caps_unref (caps);

  /* 0. Emit autoplug-continue signal for pending caps ? */
  GST_FIXME_OBJECT (dbin, "emit autoplug-continue");

  /* 1. if in EXPOSE_ALL_MODE, just accept */
  GST_FIXME_OBJECT (dbin, "Handle EXPOSE_ALL_MODE");

  /* 3. In default mode check if we should expose */
  id_in_list = (gchar *) stream_in_list (dbin->requested_selection, stream_id);
  if (id_in_list || dbin->upstream_selected) {
    /* Check if we can steal an existing output stream we could re-use.
     * that is:
     * * an output stream whose slot->stream is not in requested
     * * and is of the same type as this stream
     */
    output = find_free_compatible_output (dbin, slot->active_stream);
    if (output) {
      /* Move this output from its current slot to this slot */
      dbin->to_activate =
          g_list_append (dbin->to_activate, (gchar *) stream_id);
      dbin->requested_selection =
          g_list_remove (dbin->requested_selection, id_in_list);
      g_free (id_in_list);
      SELECTION_UNLOCK (dbin);
      gst_pad_add_probe (output->slot->src_pad, GST_PAD_PROBE_TYPE_IDLE,
          (GstPadProbeCallback) slot_unassign_probe, output->slot, NULL);
      SELECTION_LOCK (dbin);
      return NULL;
    }

    output = create_output_stream (dbin, slot->type);
    output->slot = slot;
    GST_DEBUG ("Linking slot %p to new output %p", slot, output);
    slot->output = output;
    GST_DEBUG ("Adding '%s' to active_selection", stream_id);
    dbin->active_selection =
        g_list_append (dbin->active_selection, (gchar *) g_strdup (stream_id));
  } else
    GST_DEBUG ("Not creating any output for slot %p", slot);

  return output;
}

/* Returns SELECTED_STREAMS message if active_selection is equal to
 * requested_selection, else NULL.
 * Must be called with LOCK taken */
static GstMessage *
is_selection_done (GstDecodebin3 * dbin)
{
  GList *tmp;
  GstMessage *msg;

  if (!dbin->selection_updated)
    return NULL;

  GST_LOG_OBJECT (dbin, "Checking");

  if (dbin->upstream_selected) {
    GST_DEBUG ("Upstream handles stream selection, returning");
    return NULL;
  }

  if (dbin->to_activate != NULL) {
    GST_DEBUG ("Still have streams to activate");
    return NULL;
  }
  for (tmp = dbin->requested_selection; tmp; tmp = tmp->next) {
    GST_DEBUG ("Checking requested stream %s", (gchar *) tmp->data);
    if (!stream_in_list (dbin->active_selection, (gchar *) tmp->data)) {
      GST_DEBUG ("Not in active selection, returning");
      return NULL;
    }
  }

  GST_DEBUG_OBJECT (dbin, "Selection active, creating message");

  /* We are completely active */
  msg = gst_message_new_streams_selected ((GstObject *) dbin, dbin->collection);
  if (dbin->select_streams_seqnum != GST_SEQNUM_INVALID) {
    gst_message_set_seqnum (msg, dbin->select_streams_seqnum);
  }
  for (tmp = dbin->output_streams; tmp; tmp = tmp->next) {
    DecodebinOutputStream *output = (DecodebinOutputStream *) tmp->data;
    if (output->slot) {
      const gchar *output_streamid =
          gst_stream_get_stream_id (output->slot->active_stream);
      GST_DEBUG_OBJECT (dbin, "Adding stream %s", output_streamid);
      if (stream_in_list (dbin->requested_selection, output_streamid))
        gst_message_streams_selected_add (msg, output->slot->active_stream);
      else
        GST_WARNING_OBJECT (dbin,
            "Output slot still active for old selection ?");
    } else
      GST_WARNING_OBJECT (dbin, "No valid slot for output %p", output);
  }
  dbin->selection_updated = FALSE;
  return msg;
}

/* Must be called with SELECTION_LOCK taken
 *
 * This code is used to propagate the final EOS if all slots and inputs are
 * drained.
 **/
static void
check_inputs_and_slots_for_eos (GstDecodebin3 * dbin, GstEvent * ev)
{
  GList *iter;

  GST_DEBUG_OBJECT (dbin, "checking slots for eos");

  for (iter = dbin->slots; iter; iter = iter->next) {
    MultiQueueSlot *slot = iter->data;

    if (slot->output && !slot->is_drained) {
      GST_LOG_OBJECT (slot->sink_pad, "Not drained, not all slots are done");
      return;
    }
  }

  /* Also check with the inputs, data might be pending */
  if (!all_inputs_are_eos (dbin))
    return;

  GST_DEBUG_OBJECT (dbin,
      "All active slots are drained, and no pending input, push EOS");

  for (iter = dbin->input_streams; iter; iter = iter->next) {
    DecodebinInputStream *input = (DecodebinInputStream *) iter->data;
    GstPad *peer = gst_pad_get_peer (input->srcpad);

    /* Send EOS to all slots */
    if (peer) {
      GstEvent *stream_start, *eos;

      stream_start =
          gst_pad_get_sticky_event (input->srcpad, GST_EVENT_STREAM_START, 0);

      /* First forward a custom STREAM_START event to reset the EOS status (if
       * any) */
      if (stream_start) {
        GstStructure *s;
        GstEvent *custom_stream_start = gst_event_copy (stream_start);
        gst_event_unref (stream_start);
        s = (GstStructure *) gst_event_get_structure (custom_stream_start);
        gst_structure_set (s, "decodebin3-flushing-stream-start",
            G_TYPE_BOOLEAN, TRUE, NULL);
        gst_pad_send_event (peer, custom_stream_start);
      }

      eos = gst_event_new_eos ();
      gst_event_set_seqnum (eos, gst_event_get_seqnum (ev));
      gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (eos),
          CUSTOM_FINAL_EOS_QUARK, (gchar *) CUSTOM_FINAL_EOS_QUARK_DATA, NULL);
      gst_pad_send_event (peer, eos);
      gst_object_unref (peer);
    } else
      GST_DEBUG_OBJECT (dbin, "no output");
  }
}

static void
check_slot_reconfiguration (GstDecodebin3 * dbin, MultiQueueSlot * slot)
{
  DecodebinOutputStream *output;
  GstMessage *msg = NULL;

  SELECTION_LOCK (dbin);
  output = get_output_for_slot (slot);
  if (!output) {
    SELECTION_UNLOCK (dbin);
    return;
  }

  if (!reconfigure_output_stream (output, slot, &msg)) {
    GST_DEBUG_OBJECT (dbin, "Removing failing stream from selection: %s ",
        gst_stream_get_stream_id (slot->active_stream));
    slot->dbin->requested_selection =
        remove_from_list (slot->dbin->requested_selection,
        gst_stream_get_stream_id (slot->active_stream));
    dbin->selection_updated = TRUE;
    SELECTION_UNLOCK (dbin);
    if (msg)
      gst_element_post_message ((GstElement *) slot->dbin, msg);
    reassign_slot (dbin, slot);
  } else {
    GstMessage *selection_msg = is_selection_done (dbin);
    SELECTION_UNLOCK (dbin);
    if (selection_msg)
      gst_element_post_message ((GstElement *) slot->dbin, selection_msg);
  }
}

static GstPadProbeReturn
multiqueue_src_probe (GstPad * pad, GstPadProbeInfo * info,
    MultiQueueSlot * slot)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstDecodebin3 *dbin = slot->dbin;

  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);

    GST_DEBUG_OBJECT (pad, "Got event %p %s", ev, GST_EVENT_TYPE_NAME (ev));
    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_STREAM_START:
      {
        GstStream *stream = NULL;
        const GstStructure *s = gst_event_get_structure (ev);

        /* Drop STREAM_START events used to cleanup multiqueue */
        if (s
            && gst_structure_has_field (s,
                "decodebin3-flushing-stream-start")) {
          ret = GST_PAD_PROBE_HANDLED;
          gst_event_unref (ev);
          break;
        }

        gst_event_parse_stream (ev, &stream);
        if (stream == NULL) {
          GST_ERROR_OBJECT (pad,
              "Got a STREAM_START event without a GstStream");
          break;
        }
        slot->is_drained = FALSE;
        GST_DEBUG_OBJECT (pad, "Stream Start '%s'",
            gst_stream_get_stream_id (stream));
        if (slot->active_stream == NULL) {
          slot->active_stream = stream;
        } else if (slot->active_stream != stream) {
          gboolean stream_type_changed =
              gst_stream_get_stream_type (stream) !=
              gst_stream_get_stream_type (slot->active_stream);

          GST_DEBUG_OBJECT (pad, "Stream change (%s => %s) !",
              gst_stream_get_stream_id (slot->active_stream),
              gst_stream_get_stream_id (stream));
          gst_object_unref (slot->active_stream);
          slot->active_stream = stream;

          if (stream_type_changed) {
            /* The stream type has changed, we get rid of the current output. A
             * new one (targetting the new stream type) will be created once the
             * caps are received. */
            GST_DEBUG_OBJECT (pad,
                "Stream type change, discarding current output stream");
            if (slot->output) {
              DecodebinOutputStream *output = slot->output;
              SELECTION_LOCK (dbin);
              dbin->output_streams =
                  g_list_remove (dbin->output_streams, output);
              free_output_stream (dbin, output);
              SELECTION_UNLOCK (dbin);
            }
          }
        } else
          gst_object_unref (stream);
      }
        break;
      case GST_EVENT_CAPS:
      {
        /* Configure the output slot if needed */
        check_slot_reconfiguration (dbin, slot);
      }
        break;
      case GST_EVENT_EOS:
      {
        gboolean was_drained = slot->is_drained;
        slot->is_drained = TRUE;

        /* Custom EOS handling first */
        if (gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (ev),
                CUSTOM_EOS_QUARK)) {
          /* remove custom-eos */
          ev = gst_event_make_writable (ev);
          GST_PAD_PROBE_INFO_DATA (info) = ev;
          gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (ev),
              CUSTOM_EOS_QUARK, NULL, NULL);

          GST_LOG_OBJECT (pad, "Received custom EOS");
          ret = GST_PAD_PROBE_HANDLED;
          SELECTION_LOCK (dbin);
          if (slot->input == NULL) {
            GST_DEBUG_OBJECT (pad,
                "Got custom-eos from null input stream, remove output stream");
            /* Remove the output */
            if (slot->output) {
              DecodebinOutputStream *output = slot->output;
              dbin->output_streams =
                  g_list_remove (dbin->output_streams, output);
              free_output_stream (dbin, output);
              /* Reacalculate min interleave */
              gst_decodebin3_update_min_interleave (dbin);
            }
            slot->probe_id = 0;
            dbin->slots = g_list_remove (dbin->slots, slot);
            free_multiqueue_slot_async (dbin, slot);
            ret = GST_PAD_PROBE_REMOVE;
          } else if (!was_drained) {
            check_inputs_and_slots_for_eos (dbin, ev);
          }
          if (ret == GST_PAD_PROBE_HANDLED)
            gst_event_unref (ev);
          SELECTION_UNLOCK (dbin);
          break;
        }

        GST_FIXME_OBJECT (pad, "EOS on multiqueue source pad. input:%p",
            slot->input);
        if (slot->input == NULL) {
          GstPad *peer;
          GST_DEBUG_OBJECT (pad,
              "last EOS for input, forwarding and removing slot");
          peer = gst_pad_get_peer (pad);
          if (peer) {
            gst_pad_send_event (peer, gst_event_ref (ev));
            gst_object_unref (peer);
          }
          SELECTION_LOCK (dbin);
          /* FIXME : Shouldn't we try to re-assign the output instead of just
           * removing it ? */
          /* Remove the output */
          if (slot->output) {
            DecodebinOutputStream *output = slot->output;
            dbin->output_streams = g_list_remove (dbin->output_streams, output);
            free_output_stream (dbin, output);
          }
          slot->probe_id = 0;
          dbin->slots = g_list_remove (dbin->slots, slot);
          SELECTION_UNLOCK (dbin);

          /* FIXME: Removing the slot is async, which means actually
           * unlinking the pad is async. Other things like stream-start
           * might flow through this (now unprobed) link before it actually
           * gets released */
          free_multiqueue_slot_async (dbin, slot);
          ret = GST_PAD_PROBE_REMOVE;
        } else if (gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (ev),
                CUSTOM_FINAL_EOS_QUARK)) {
          GST_DEBUG_OBJECT (pad, "Got final eos, propagating downstream");
        } else {
          GST_DEBUG_OBJECT (pad, "Got regular eos (all_inputs_are_eos)");
          /* drop current event as eos will be sent in check_all_slot_for_eos
           * when all output streams are also eos */
          ret = GST_PAD_PROBE_DROP;
          SELECTION_LOCK (dbin);
          check_inputs_and_slots_for_eos (dbin, ev);
          SELECTION_UNLOCK (dbin);
        }
      }
        break;
      default:
        break;
    }
  } else if (GST_IS_QUERY (GST_PAD_PROBE_INFO_DATA (info))) {
    GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);
    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_CAPS:
      {
        GST_DEBUG_OBJECT (pad, "Intercepting CAPS query");
        gst_query_set_caps_result (query, GST_CAPS_ANY);
        ret = GST_PAD_PROBE_HANDLED;
      }
        break;

      case GST_QUERY_ACCEPT_CAPS:
      {
        GST_DEBUG_OBJECT (pad, "Intercepting Accept Caps query");
        /* If the current decoder doesn't accept caps, we'll reconfigure
         * on the actual caps event. So accept any caps. */
        gst_query_set_accept_caps_result (query, TRUE);
        ret = GST_PAD_PROBE_HANDLED;
      }
      default:
        break;
    }
  }

  return ret;
}

/* Create a new multiqueue slot for the given type
 *
 * It is up to the caller to know whether that slot is needed or not
 * (and release it when no longer needed) */
static MultiQueueSlot *
create_new_slot (GstDecodebin3 * dbin, GstStreamType type)
{
  MultiQueueSlot *slot;
  GstIterator *it = NULL;
  GValue item = { 0, };

  GST_DEBUG_OBJECT (dbin, "Creating new slot for type %s",
      gst_stream_type_get_name (type));
  slot = g_new0 (MultiQueueSlot, 1);
  slot->dbin = dbin;

  slot->id = dbin->slot_id++;

  slot->type = type;
  slot->sink_pad = gst_element_request_pad_simple (dbin->multiqueue, "sink_%u");
  if (slot->sink_pad == NULL)
    goto fail;

  it = gst_pad_iterate_internal_links (slot->sink_pad);
  if (!it || (gst_iterator_next (it, &item)) != GST_ITERATOR_OK
      || ((slot->src_pad = g_value_dup_object (&item)) == NULL)) {
    GST_ERROR ("Couldn't get srcpad from multiqueue for sink pad %s:%s",
        GST_DEBUG_PAD_NAME (slot->src_pad));
    goto fail;
  }
  gst_iterator_free (it);
  g_value_reset (&item);

  g_object_set (slot->sink_pad, "group-id", (guint) type, NULL);

  /* Add event probe */
  slot->probe_id =
      gst_pad_add_probe (slot->src_pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
      (GstPadProbeCallback) multiqueue_src_probe, slot, NULL);

  GST_DEBUG ("Created new slot %u (%p) (%s:%s)", slot->id, slot,
      GST_DEBUG_PAD_NAME (slot->src_pad));

  dbin->slots = g_list_append (dbin->slots, slot);

  return slot;

  /* ERRORS */
fail:
  {
    if (slot->sink_pad)
      gst_element_release_request_pad (dbin->multiqueue, slot->sink_pad);
    g_free (slot);
    return NULL;
  }
}

/* Must be called with SELECTION_LOCK */
static MultiQueueSlot *
get_slot_for_input (GstDecodebin3 * dbin, DecodebinInputStream * input)
{
  GList *tmp;
  MultiQueueSlot *empty_slot = NULL;
  GstStreamType input_type = 0;
  gchar *stream_id = NULL;

  GST_DEBUG_OBJECT (dbin, "input %p (stream %p %s)",
      input, input->active_stream,
      input->
      active_stream ? gst_stream_get_stream_id (input->active_stream) : "");

  if (input->active_stream) {
    input_type = gst_stream_get_stream_type (input->active_stream);
    stream_id = (gchar *) gst_stream_get_stream_id (input->active_stream);
  }

  /* Go over existing slots and check if there is already one for it */
  for (tmp = dbin->slots; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    /* Already used input, return that one */
    if (slot->input == input) {
      GST_DEBUG_OBJECT (dbin, "Returning already specified slot %d", slot->id);
      if (input_type && slot->type != input_type) {
        /* The input stream type has changed. It is the responsibility of the
         * user of decodebin3 to ensure that the inputs are coherent.
         *
         * The only case where the stream type will change is when switching
         * between sources which have non-intersecting stream types (ex:
         * switching from audio-only file to video-only file)
         *
         * NOTE : We need to change the slot type here, since it is notified as
         * soon as the *input* of the slot changes.
         */
        GST_DEBUG_OBJECT (dbin, "Changing multiqueue slot stream type");
        slot->type = input_type;
      }
      return slot;
    }
  }

  /* Go amongst all unused slots of the right type and try to find a candidate */
  for (tmp = dbin->slots; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    if (slot->input == NULL && input_type == slot->type) {
      /* Remember this empty slot for later */
      empty_slot = slot;
      /* Check if available slot is of the same stream_id */
      GST_LOG_OBJECT (dbin, "Checking candidate slot %d (active_stream:%p)",
          slot->id, slot->active_stream);
      if (stream_id && slot->active_stream) {
        gchar *ostream_id =
            (gchar *) gst_stream_get_stream_id (slot->active_stream);
        GST_DEBUG_OBJECT (dbin, "Checking slot %d %s against %s", slot->id,
            ostream_id, stream_id);
        if (!g_strcmp0 (stream_id, ostream_id))
          break;
      }
    }
  }

  if (empty_slot) {
    GST_DEBUG_OBJECT (dbin, "Re-using existing unused slot %d", empty_slot->id);
    return empty_slot;
  }

  if (input_type)
    return create_new_slot (dbin, input_type);

  return NULL;
}

static void
link_input_to_slot (DecodebinInputStream * input, MultiQueueSlot * slot)
{
  if (slot->input != NULL && slot->input != input) {
    GST_ERROR_OBJECT (slot->dbin,
        "Trying to link input to an already used slot");
    return;
  }
  gst_pad_link_full (input->srcpad, slot->sink_pad, GST_PAD_LINK_CHECK_NOTHING);
  slot->pending_stream = input->active_stream;
  slot->input = input;
}

static GList *
create_decoder_factory_list (GstDecodebin3 * dbin, GstCaps * caps)
{
  GList *res;

  g_mutex_lock (&dbin->factories_lock);
  gst_decode_bin_update_factories_list (dbin);
  res = gst_element_factory_list_filter (dbin->decoder_factories,
      caps, GST_PAD_SINK, TRUE);
  g_mutex_unlock (&dbin->factories_lock);
  return res;
}

static GstPadProbeReturn
keyframe_waiter_probe (GstPad * pad, GstPadProbeInfo * info,
    DecodebinOutputStream * output)
{
  GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER (info);

  /* If we have a keyframe, remove the probe and let all data through */
  if (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT) ||
      GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_HEADER)) {
    GST_DEBUG_OBJECT (pad,
        "Buffer is keyframe or header, letting through and removing probe");
    output->drop_probe_id = 0;
    return GST_PAD_PROBE_REMOVE;
  }
  GST_DEBUG_OBJECT (pad, "Buffer is not a keyframe, dropping");
  return GST_PAD_PROBE_DROP;
}

static void
remove_decoder_link (DecodebinOutputStream * output, MultiQueueSlot * slot)
{
  GstDecodebin3 *dbin = output->dbin;

  if (gst_pad_is_linked (output->decoder_sink)) {
    gst_pad_unlink (slot->src_pad, output->decoder_sink);
  }
  if (output->drop_probe_id) {
    gst_pad_remove_probe (slot->src_pad, output->drop_probe_id);
    output->drop_probe_id = 0;
  }

  gst_element_set_locked_state (output->decoder, TRUE);
  gst_element_set_state (output->decoder, GST_STATE_NULL);

  gst_bin_remove ((GstBin *) dbin, output->decoder);
  output->decoder = NULL;
}

/* Returns FALSE if the output couldn't be properly configured and the
 * associated GstStreams should be disabled */
static gboolean
reconfigure_output_stream (DecodebinOutputStream * output,
    MultiQueueSlot * slot, GstMessage ** msg)
{
  GstDecodebin3 *dbin = output->dbin;
  GstCaps *new_caps = (GstCaps *) gst_stream_get_caps (slot->active_stream);
  gboolean needs_decoder;
  gboolean ret = TRUE;

  needs_decoder = gst_caps_can_intersect (new_caps, dbin->caps) != TRUE;

  GST_DEBUG_OBJECT (dbin,
      "Reconfiguring output %p to slot %p, needs_decoder:%d", output, slot,
      needs_decoder);

  /* FIXME : Maybe make the output un-hook itself automatically ? */
  if (output->slot != NULL && output->slot != slot) {
    GST_WARNING_OBJECT (dbin,
        "Output still linked to another slot (%p)", output->slot);
    gst_caps_unref (new_caps);
    return ret;
  }

  /* Check if existing config is reusable as-is by checking if
   * the existing decoder accepts the new caps, if not delete
   * it and create a new one */
  if (output->decoder) {
    gboolean can_reuse_decoder;

    if (needs_decoder) {
      can_reuse_decoder =
          gst_pad_query_accept_caps (output->decoder_sink, new_caps);
    } else
      can_reuse_decoder = FALSE;

    if (can_reuse_decoder) {
      if (output->type & GST_STREAM_TYPE_VIDEO && output->drop_probe_id == 0) {
        GST_DEBUG_OBJECT (dbin, "Adding keyframe-waiter probe");
        output->drop_probe_id =
            gst_pad_add_probe (slot->src_pad, GST_PAD_PROBE_TYPE_BUFFER,
            (GstPadProbeCallback) keyframe_waiter_probe, output, NULL);
      }
      GST_DEBUG_OBJECT (dbin, "Reusing existing decoder for slot %p", slot);
      if (output->linked == FALSE) {
        gst_pad_link_full (slot->src_pad, output->decoder_sink,
            GST_PAD_LINK_CHECK_NOTHING);
        output->linked = TRUE;
      }
      gst_caps_unref (new_caps);
      return ret;
    }

    GST_DEBUG_OBJECT (dbin, "Removing old decoder for slot %p", slot);

    if (output->linked)
      gst_pad_unlink (slot->src_pad, output->decoder_sink);
    output->linked = FALSE;
    if (output->drop_probe_id) {
      gst_pad_remove_probe (slot->src_pad, output->drop_probe_id);
      output->drop_probe_id = 0;
    }

    if (!decode_pad_set_target ((GstGhostPad *) output->src_pad, NULL)) {
      GST_ERROR_OBJECT (dbin, "Could not release decoder pad");
      gst_caps_unref (new_caps);
      goto cleanup;
    }

    gst_element_set_locked_state (output->decoder, TRUE);
    gst_element_set_state (output->decoder, GST_STATE_NULL);

    gst_bin_remove ((GstBin *) dbin, output->decoder);
    output->decoder = NULL;
    output->decoder_latency = GST_CLOCK_TIME_NONE;
  } else if (output->linked) {
    /* Otherwise if we have no decoder yet but the output is linked make
     * sure that the ghost pad is really unlinked in case no decoder was
     * needed previously */
    if (!decode_pad_set_target ((GstGhostPad *) output->src_pad, NULL)) {
      GST_ERROR_OBJECT (dbin, "Could not release ghost pad");
      gst_caps_unref (new_caps);
      goto cleanup;
    }
  }

  gst_object_replace ((GstObject **) & output->decoder_sink, NULL);
  gst_object_replace ((GstObject **) & output->decoder_src, NULL);

  /* If a decoder is required, create one */
  if (needs_decoder) {
    GList *factories, *next_factory;

    factories = next_factory = create_decoder_factory_list (dbin, new_caps);
    if (!next_factory) {
      GST_DEBUG ("Could not find an element for caps %" GST_PTR_FORMAT,
          new_caps);
      g_assert (output->decoder == NULL);
      ret = FALSE;
      goto missing_decoder;
    }

    while (next_factory) {
      gboolean decoder_failed = FALSE;
      CandidateDecoder *candidate = NULL;

      /* If we don't have a decoder yet, instantiate one */
      output->decoder = gst_element_factory_create ((GstElementFactory *)
          next_factory->data, NULL);
      GST_DEBUG ("Trying decoder %" GST_PTR_FORMAT, output->decoder);

      if (output->decoder == NULL)
        goto try_next;

      if (!gst_bin_add ((GstBin *) dbin, output->decoder)) {
        GST_WARNING_OBJECT (dbin, "could not add decoder '%s' to pipeline",
            GST_ELEMENT_NAME (output->decoder));
        goto try_next;
      }
      output->decoder_sink =
          gst_element_get_static_pad (output->decoder, "sink");
      output->decoder_src = gst_element_get_static_pad (output->decoder, "src");
      if (output->type & GST_STREAM_TYPE_VIDEO) {
        GST_DEBUG_OBJECT (dbin, "Adding keyframe-waiter probe");
        output->drop_probe_id =
            gst_pad_add_probe (slot->src_pad, GST_PAD_PROBE_TYPE_BUFFER,
            (GstPadProbeCallback) keyframe_waiter_probe, output, NULL);
      }

      candidate = add_candidate_decoder (dbin, output->decoder);
      if (gst_pad_link_full (slot->src_pad, output->decoder_sink,
              GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK) {
        GST_WARNING_OBJECT (dbin, "could not link to %s:%s",
            GST_DEBUG_PAD_NAME (output->decoder_sink));
        decoder_failed = TRUE;
        goto try_next;
      }

      if (gst_element_set_state (output->decoder,
              GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
        GST_WARNING_OBJECT (dbin,
            "Decoder '%s' failed to reach READY state",
            GST_ELEMENT_NAME (output->decoder));
        decoder_failed = TRUE;
        goto try_next;
      }

      if (!gst_pad_query_accept_caps (output->decoder_sink, new_caps)) {
        GST_DEBUG_OBJECT (dbin,
            "Decoder '%s' did not accept the caps, trying the next type",
            GST_ELEMENT_NAME (output->decoder));
        decoder_failed = TRUE;
        goto try_next;
      }

      /* First lock element's sinkpad stream lock so no data reaches
       * the possible new element added when caps are sent by element
       * while we're still sending sticky events */
      GST_PAD_STREAM_LOCK (output->decoder_sink);

      if (gst_element_set_state (output->decoder,
              GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE ||
          !send_sticky_events (dbin, slot->src_pad)) {

        GST_PAD_STREAM_UNLOCK (output->decoder_sink);
        GST_WARNING_OBJECT (dbin,
            "Decoder '%s' failed to reach PAUSED state",
            GST_ELEMENT_NAME (output->decoder));
        decoder_failed = TRUE;
        goto try_next;
      } else {
        /* Everything went well */
        GST_PAD_STREAM_UNLOCK (output->decoder_sink);
        output->linked = TRUE;
        GST_DEBUG ("created decoder %" GST_PTR_FORMAT, output->decoder);

        handle_stored_latency_message (dbin, output, candidate);
        remove_candidate_decoder (dbin, candidate);
      }

      break;

    try_next:
      {
        if (decoder_failed)
          remove_decoder_link (output, slot);
        if (candidate)
          remove_candidate_decoder (dbin, candidate);

        if (!next_factory->next) {
          ret = FALSE;
          if (!decoder_failed)
            goto cleanup;
          if (output->decoder == NULL)
            goto missing_decoder;
        } else {
          next_factory = next_factory->next;
        }
      }
    }
    gst_plugin_feature_list_free (factories);
  } else {
    output->decoder_src = gst_object_ref (slot->src_pad);
    output->decoder_sink = NULL;
  }
  gst_caps_unref (new_caps);

  if (!decode_pad_set_target ((GstGhostPad *) output->src_pad,
          output->decoder_src)) {
    GST_ERROR_OBJECT (dbin, "Could not expose decoder pad");
    ret = FALSE;
    goto cleanup;
  }

  output->linked = TRUE;

  if (output->src_exposed == FALSE) {
    GstEvent *stream_start;

    stream_start = gst_pad_get_sticky_event (slot->src_pad,
        GST_EVENT_STREAM_START, 0);

    /* Ensure GstStream is accesiable from pad-added callback */
    if (stream_start) {
      gst_pad_store_sticky_event (output->src_pad, stream_start);
      gst_event_unref (stream_start);
    } else {
      GST_WARNING_OBJECT (slot->src_pad,
          "Pad has no stored stream-start event");
    }

    output->src_exposed = TRUE;
    gst_element_add_pad (GST_ELEMENT_CAST (dbin), output->src_pad);
  }

  if (output->decoder)
    gst_element_sync_state_with_parent (output->decoder);

  output->slot = slot;
  return ret;

missing_decoder:
  {
    GstCaps *caps;
    caps = gst_stream_get_caps (slot->active_stream);
    *msg = gst_missing_decoder_message_new (GST_ELEMENT_CAST (dbin), caps);
    gst_caps_unref (caps);
  }

cleanup:
  {
    GST_DEBUG_OBJECT (dbin, "Cleanup");
    if (output->decoder_sink) {
      gst_object_unref (output->decoder_sink);
      output->decoder_sink = NULL;
    }
    if (output->decoder_src) {
      gst_object_unref (output->decoder_src);
      output->decoder_src = NULL;
    }
    if (output->decoder) {
      gst_element_set_state (output->decoder, GST_STATE_NULL);
      gst_bin_remove ((GstBin *) dbin, output->decoder);
      output->decoder = NULL;
    }
    return ret;
  }
}

static GstPadProbeReturn
idle_reconfigure (GstPad * pad, GstPadProbeInfo * info, MultiQueueSlot * slot)
{
  GstDecodebin3 *dbin = slot->dbin;

  check_slot_reconfiguration (dbin, slot);

  return GST_PAD_PROBE_REMOVE;
}

static MultiQueueSlot *
find_slot_for_stream_id (GstDecodebin3 * dbin, const gchar * sid)
{
  GList *tmp;

  for (tmp = dbin->slots; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    const gchar *stream_id;
    if (slot->active_stream) {
      stream_id = gst_stream_get_stream_id (slot->active_stream);
      if (!g_strcmp0 (sid, stream_id))
        return slot;
    }
    if (slot->pending_stream && slot->pending_stream != slot->active_stream) {
      stream_id = gst_stream_get_stream_id (slot->pending_stream);
      if (!g_strcmp0 (sid, stream_id))
        return slot;
    }
  }

  return NULL;
}

/* This function handles the reassignment of a slot. Call this from
 * the streaming thread of a slot. */
static gboolean
reassign_slot (GstDecodebin3 * dbin, MultiQueueSlot * slot)
{
  DecodebinOutputStream *output;
  MultiQueueSlot *target_slot = NULL;
  GList *tmp;
  const gchar *sid, *tsid;

  SELECTION_LOCK (dbin);
  output = slot->output;

  if (G_UNLIKELY (slot->active_stream == NULL)) {
    GST_DEBUG_OBJECT (slot->src_pad,
        "Called on inactive slot (active_stream == NULL)");
    SELECTION_UNLOCK (dbin);
    return FALSE;
  }

  if (G_UNLIKELY (output == NULL)) {
    GST_DEBUG_OBJECT (slot->src_pad,
        "Slot doesn't have any output to be removed");
    SELECTION_UNLOCK (dbin);
    return FALSE;
  }

  sid = gst_stream_get_stream_id (slot->active_stream);
  GST_DEBUG_OBJECT (slot->src_pad, "slot %s %p", sid, slot);

  /* Recheck whether this stream is still in the list of streams to deactivate */
  if (stream_in_list (dbin->requested_selection, sid)) {
    /* Stream is in the list of requested streams, don't remove */
    SELECTION_UNLOCK (dbin);
    GST_DEBUG_OBJECT (slot->src_pad,
        "Stream '%s' doesn't need to be deactivated", sid);
    return FALSE;
  }

  /* Unlink slot from output */
  /* FIXME : Handle flushing ? */
  /* FIXME : Handle outputs without decoders */
  GST_DEBUG_OBJECT (slot->src_pad, "Unlinking from decoder %p",
      output->decoder_sink);
  if (output->decoder_sink)
    gst_pad_unlink (slot->src_pad, output->decoder_sink);
  output->linked = FALSE;
  slot->output = NULL;
  output->slot = NULL;
  /* Remove sid from active selection */
  GST_DEBUG ("Removing '%s' from active_selection", sid);
  for (tmp = dbin->active_selection; tmp; tmp = tmp->next)
    if (!g_strcmp0 (sid, tmp->data)) {
      g_free (tmp->data);
      dbin->active_selection = g_list_delete_link (dbin->active_selection, tmp);
      break;
    }

  /* Can we re-assign this output to a requested stream ? */
  GST_DEBUG_OBJECT (slot->src_pad, "Attempting to re-assing output stream");
  for (tmp = dbin->to_activate; tmp; tmp = tmp->next) {
    MultiQueueSlot *tslot = find_slot_for_stream_id (dbin, tmp->data);
    GST_LOG_OBJECT (tslot->src_pad, "Checking slot %p (output:%p , stream:%s)",
        tslot, tslot->output, gst_stream_get_stream_id (tslot->active_stream));
    if (tslot && tslot->type == output->type && tslot->output == NULL) {
      GST_DEBUG_OBJECT (tslot->src_pad, "Using as reassigned slot");
      target_slot = tslot;
      tsid = tmp->data;
      /* Pass target stream id to requested selection */
      dbin->requested_selection =
          g_list_append (dbin->requested_selection, g_strdup (tmp->data));
      dbin->to_activate = g_list_delete_link (dbin->to_activate, tmp);
      break;
    }
  }

  if (target_slot) {
    GST_DEBUG_OBJECT (slot->src_pad, "Assigning output to slot %p '%s'",
        target_slot, tsid);
    target_slot->output = output;
    output->slot = target_slot;
    GST_DEBUG ("Adding '%s' to active_selection", tsid);
    dbin->active_selection =
        g_list_append (dbin->active_selection, (gchar *) g_strdup (tsid));
    SELECTION_UNLOCK (dbin);

    /* Wakeup the target slot so that it retries to send events/buffers
     * thereby triggering the output reconfiguration codepath */
    gst_pad_add_probe (target_slot->src_pad, GST_PAD_PROBE_TYPE_IDLE,
        (GstPadProbeCallback) idle_reconfigure, target_slot, NULL);
    /* gst_pad_send_event (target_slot->src_pad, gst_event_new_reconfigure ()); */
  } else {
    GstMessage *msg;

    dbin->output_streams = g_list_remove (dbin->output_streams, output);
    free_output_stream (dbin, output);
    msg = is_selection_done (slot->dbin);
    SELECTION_UNLOCK (dbin);

    if (msg)
      gst_element_post_message ((GstElement *) slot->dbin, msg);
  }

  return TRUE;
}

/* Idle probe called when a slot should be unassigned from its output stream.
 * This is needed to ensure nothing is flowing when unlinking the slot.
 *
 * Also, this method will search for a pending stream which could re-use
 * the output stream. */
static GstPadProbeReturn
slot_unassign_probe (GstPad * pad, GstPadProbeInfo * info,
    MultiQueueSlot * slot)
{
  GstDecodebin3 *dbin = slot->dbin;

  reassign_slot (dbin, slot);

  return GST_PAD_PROBE_REMOVE;
}

static gboolean
handle_stream_switch (GstDecodebin3 * dbin, GList * select_streams,
    guint32 seqnum)
{
  gboolean ret = TRUE;
  GList *tmp;
  /* List of slots to (de)activate. */
  GList *to_deactivate = NULL;
  GList *to_activate = NULL;
  /* List of unknown stream id, most likely means the event
   * should be sent upstream so that elements can expose the requested stream */
  GList *unknown = NULL;
  GList *to_reassign = NULL;
  GList *future_request_streams = NULL;
  GList *pending_streams = NULL;
  GList *slots_to_reassign = NULL;

  SELECTION_LOCK (dbin);
  if (G_UNLIKELY (seqnum != dbin->select_streams_seqnum)) {
    GST_DEBUG_OBJECT (dbin, "New SELECT_STREAMS has arrived in the meantime");
    SELECTION_UNLOCK (dbin);
    return TRUE;
  }
  /* Remove pending select_streams */
  g_list_free (dbin->pending_select_streams);
  dbin->pending_select_streams = NULL;

  /* COMPARE the requested streams to the active and requested streams
   * on multiqueue. */

  /* First check the slots to activate and which ones are unknown */
  for (tmp = select_streams; tmp; tmp = tmp->next) {
    const gchar *sid = (const gchar *) tmp->data;
    MultiQueueSlot *slot;
    GST_DEBUG_OBJECT (dbin, "Checking stream '%s'", sid);
    slot = find_slot_for_stream_id (dbin, sid);
    /* Find the corresponding slot */
    if (slot == NULL) {
      if (stream_in_collection (dbin, (gchar *) sid)) {
        pending_streams = g_list_append (pending_streams, (gchar *) sid);
      } else {
        GST_DEBUG_OBJECT (dbin, "We don't have a slot for stream '%s'", sid);
        unknown = g_list_append (unknown, (gchar *) sid);
      }
    } else if (slot->output == NULL) {
      GST_DEBUG_OBJECT (dbin, "We need to activate slot %p for stream '%s')",
          slot, sid);
      to_activate = g_list_append (to_activate, slot);
    } else {
      GST_DEBUG_OBJECT (dbin,
          "Stream '%s' from slot %p is already active on output %p", sid, slot,
          slot->output);
      future_request_streams =
          g_list_append (future_request_streams, (gchar *) sid);
    }
  }

  for (tmp = dbin->slots; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    /* For slots that have an output, check if it's part of the streams to
     * be active */
    if (slot->output) {
      gboolean slot_to_deactivate = TRUE;

      if (slot->active_stream) {
        if (stream_in_list (select_streams,
                gst_stream_get_stream_id (slot->active_stream)))
          slot_to_deactivate = FALSE;
      }
      if (slot_to_deactivate && slot->pending_stream
          && slot->pending_stream != slot->active_stream) {
        if (stream_in_list (select_streams,
                gst_stream_get_stream_id (slot->pending_stream)))
          slot_to_deactivate = FALSE;
      }
      if (slot_to_deactivate) {
        GST_DEBUG_OBJECT (dbin,
            "Slot %p (%s) should be deactivated, no longer used", slot,
            slot->
            active_stream ? gst_stream_get_stream_id (slot->active_stream) :
            "NULL");
        to_deactivate = g_list_append (to_deactivate, slot);
      }
    }
  }

  if (to_deactivate != NULL) {
    GST_DEBUG_OBJECT (dbin, "Check if we can reassign slots");
    /* We need to compare what needs to be activated and deactivated in order
     * to determine whether there are outputs that can be transferred */
    /* Take the stream-id of the slots that are to be activated, for which there
     * is a slot of the same type that needs to be deactivated */
    tmp = to_deactivate;
    while (tmp) {
      MultiQueueSlot *slot_to_deactivate = (MultiQueueSlot *) tmp->data;
      gboolean removeit = FALSE;
      GList *tmp2, *next;
      GST_DEBUG_OBJECT (dbin,
          "Checking if slot to deactivate (%p) has a candidate slot to activate",
          slot_to_deactivate);
      for (tmp2 = to_activate; tmp2; tmp2 = tmp2->next) {
        MultiQueueSlot *slot_to_activate = (MultiQueueSlot *) tmp2->data;
        GST_DEBUG_OBJECT (dbin, "Comparing to slot %p", slot_to_activate);
        if (slot_to_activate->type == slot_to_deactivate->type) {
          GST_DEBUG_OBJECT (dbin, "Re-using");
          to_reassign = g_list_append (to_reassign, (gchar *)
              gst_stream_get_stream_id (slot_to_activate->active_stream));
          slots_to_reassign =
              g_list_append (slots_to_reassign, slot_to_deactivate);
          to_activate = g_list_remove (to_activate, slot_to_activate);
          removeit = TRUE;
          break;
        }
      }
      next = tmp->next;
      if (removeit)
        to_deactivate = g_list_delete_link (to_deactivate, tmp);
      tmp = next;
    }
  }

  for (tmp = to_deactivate; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    GST_DEBUG_OBJECT (dbin,
        "Really need to deactivate slot %p, but no available alternative",
        slot);

    slots_to_reassign = g_list_append (slots_to_reassign, slot);
  }

  /* The only slots left to activate are the ones that won't be reassigned and
   * therefore really need to have a new output created */
  for (tmp = to_activate; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    if (slot->active_stream)
      future_request_streams =
          g_list_append (future_request_streams,
          (gchar *) gst_stream_get_stream_id (slot->active_stream));
    else if (slot->pending_stream)
      future_request_streams =
          g_list_append (future_request_streams,
          (gchar *) gst_stream_get_stream_id (slot->pending_stream));
    else
      GST_ERROR_OBJECT (dbin, "No stream for slot %p !!", slot);
  }

  if (to_activate == NULL && pending_streams != NULL) {
    GST_DEBUG_OBJECT (dbin, "Stream switch requested for future collection");
    if (dbin->requested_selection)
      g_list_free_full (dbin->requested_selection, g_free);
    dbin->requested_selection =
        g_list_copy_deep (select_streams, (GCopyFunc) g_strdup, NULL);
    g_list_free (to_deactivate);
    g_list_free (pending_streams);
    to_deactivate = NULL;
    pending_streams = NULL;
  } else {
    if (dbin->requested_selection)
      g_list_free_full (dbin->requested_selection, g_free);
    dbin->requested_selection =
        g_list_copy_deep (future_request_streams, (GCopyFunc) g_strdup, NULL);
    dbin->requested_selection =
        g_list_concat (dbin->requested_selection,
        g_list_copy_deep (pending_streams, (GCopyFunc) g_strdup, NULL));
    if (dbin->to_activate)
      g_list_free (dbin->to_activate);
    dbin->to_activate = g_list_copy (to_reassign);
  }

  dbin->selection_updated = TRUE;
  SELECTION_UNLOCK (dbin);

  if (unknown) {
    GST_FIXME_OBJECT (dbin, "Got request for an unknown stream");
    g_list_free (unknown);
  }

  if (to_activate && !slots_to_reassign) {
    for (tmp = to_activate; tmp; tmp = tmp->next) {
      MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
      gst_pad_add_probe (slot->src_pad, GST_PAD_PROBE_TYPE_IDLE,
          (GstPadProbeCallback) idle_reconfigure, slot, NULL);
    }
  }

  /* For all streams to deactivate, add an idle probe where we will do
   * the unassignment and switch over */
  for (tmp = slots_to_reassign; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    gst_pad_add_probe (slot->src_pad, GST_PAD_PROBE_TYPE_IDLE,
        (GstPadProbeCallback) slot_unassign_probe, slot, NULL);
  }

  if (to_deactivate)
    g_list_free (to_deactivate);
  if (to_activate)
    g_list_free (to_activate);
  if (to_reassign)
    g_list_free (to_reassign);
  if (future_request_streams)
    g_list_free (future_request_streams);
  if (pending_streams)
    g_list_free (pending_streams);
  if (slots_to_reassign)
    g_list_free (slots_to_reassign);

  return ret;
}

static GstPadProbeReturn
ghost_pad_event_probe (GstPad * pad, GstPadProbeInfo * info,
    DecodebinOutputStream * output)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstDecodebin3 *dbin = output->dbin;
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  GST_DEBUG_OBJECT (pad, "Got event %p %s", event, GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SELECT_STREAMS:
    {
      GstPad *peer;
      GList *streams = NULL;
      guint32 seqnum = gst_event_get_seqnum (event);

      if (dbin->upstream_selected) {
        GST_DEBUG_OBJECT (pad, "Letting select-streams event flow upstream");
        break;
      }

      SELECTION_LOCK (dbin);
      if (seqnum == dbin->select_streams_seqnum) {
        SELECTION_UNLOCK (dbin);
        GST_DEBUG_OBJECT (pad,
            "Already handled/handling that SELECT_STREAMS event");
        gst_event_unref (event);
        ret = GST_PAD_PROBE_HANDLED;
        break;
      }
      dbin->select_streams_seqnum = seqnum;
      if (dbin->pending_select_streams != NULL) {
        GST_LOG_OBJECT (dbin, "Replacing pending select streams");
        g_list_free (dbin->pending_select_streams);
        dbin->pending_select_streams = NULL;
      }
      gst_event_parse_select_streams (event, &streams);
      dbin->pending_select_streams = g_list_copy (streams);
      SELECTION_UNLOCK (dbin);

      /* Send event upstream */
      if ((peer = gst_pad_get_peer (pad))) {
        gst_pad_send_event (peer, event);
        gst_object_unref (peer);
      } else {
        gst_event_unref (event);
      }
      /* Finally handle the switch */
      if (streams) {
        handle_stream_switch (dbin, streams, seqnum);
        g_list_free_full (streams, g_free);
      }
      ret = GST_PAD_PROBE_HANDLED;
    }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_decodebin3_send_event (GstElement * element, GstEvent * event)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) element;

  GST_DEBUG_OBJECT (element, "event %s", GST_EVENT_TYPE_NAME (event));
  if (!dbin->upstream_selected
      && GST_EVENT_TYPE (event) == GST_EVENT_SELECT_STREAMS) {
    GList *streams = NULL;
    guint32 seqnum = gst_event_get_seqnum (event);

    SELECTION_LOCK (dbin);
    if (seqnum == dbin->select_streams_seqnum) {
      SELECTION_UNLOCK (dbin);
      GST_DEBUG_OBJECT (dbin,
          "Already handled/handling that SELECT_STREAMS event");
      return TRUE;
    }
    dbin->select_streams_seqnum = seqnum;
    if (dbin->pending_select_streams != NULL) {
      GST_LOG_OBJECT (dbin, "Replacing pending select streams");
      g_list_free (dbin->pending_select_streams);
      dbin->pending_select_streams = NULL;
    }
    gst_event_parse_select_streams (event, &streams);
    dbin->pending_select_streams = g_list_copy (streams);
    SELECTION_UNLOCK (dbin);

    /* Finally handle the switch */
    if (streams) {
      handle_stream_switch (dbin, streams, seqnum);
      g_list_free_full (streams, g_free);
    }

    gst_event_unref (event);
    return TRUE;
  }
  return GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
}


static void
free_multiqueue_slot (GstDecodebin3 * dbin, MultiQueueSlot * slot)
{
  if (slot->probe_id)
    gst_pad_remove_probe (slot->src_pad, slot->probe_id);
  if (slot->input) {
    if (slot->input->srcpad)
      gst_pad_unlink (slot->input->srcpad, slot->sink_pad);
  }

  gst_element_release_request_pad (dbin->multiqueue, slot->sink_pad);
  gst_object_replace ((GstObject **) & slot->sink_pad, NULL);
  gst_object_replace ((GstObject **) & slot->src_pad, NULL);
  gst_object_replace ((GstObject **) & slot->active_stream, NULL);
  g_free (slot);
}

static void
free_multiqueue_slot_async (GstDecodebin3 * dbin, MultiQueueSlot * slot)
{
  GST_LOG_OBJECT (dbin, "pushing multiqueue slot on thread pool to free");
  gst_element_call_async (GST_ELEMENT_CAST (dbin),
      (GstElementCallAsyncFunc) free_multiqueue_slot, slot, NULL);
}

/* Create a DecodebinOutputStream for a given type
 * Note: It will be empty initially, it needs to be configured
 * afterwards */
static DecodebinOutputStream *
create_output_stream (GstDecodebin3 * dbin, GstStreamType type)
{
  DecodebinOutputStream *res = g_new0 (DecodebinOutputStream, 1);
  gchar *pad_name;
  const gchar *prefix;
  GstStaticPadTemplate *templ;
  GstPadTemplate *ptmpl;
  guint32 *counter;
  GstPad *internal_pad;

  GST_DEBUG_OBJECT (dbin, "Created new output stream %p for type %s",
      res, gst_stream_type_get_name (type));

  res->type = type;
  res->dbin = dbin;
  res->decoder_latency = GST_CLOCK_TIME_NONE;

  if (type & GST_STREAM_TYPE_VIDEO) {
    templ = &video_src_template;
    counter = &dbin->vpadcount;
    prefix = "video";
  } else if (type & GST_STREAM_TYPE_AUDIO) {
    templ = &audio_src_template;
    counter = &dbin->apadcount;
    prefix = "audio";
  } else if (type & GST_STREAM_TYPE_TEXT) {
    templ = &text_src_template;
    counter = &dbin->tpadcount;
    prefix = "text";
  } else {
    templ = &src_template;
    counter = &dbin->opadcount;
    prefix = "src";
  }

  pad_name = g_strdup_printf ("%s_%u", prefix, *counter);
  *counter += 1;
  ptmpl = gst_static_pad_template_get (templ);
  res->src_pad = gst_ghost_pad_new_no_target_from_template (pad_name, ptmpl);
  gst_object_unref (ptmpl);
  g_free (pad_name);
  gst_pad_set_active (res->src_pad, TRUE);
  /* Put an event probe on the internal proxy pad to detect upstream
   * events */
  internal_pad =
      (GstPad *) gst_proxy_pad_get_internal ((GstProxyPad *) res->src_pad);
  gst_pad_add_probe (internal_pad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) ghost_pad_event_probe, res, NULL);
  gst_object_unref (internal_pad);

  dbin->output_streams = g_list_append (dbin->output_streams, res);

  return res;
}

static void
free_output_stream (GstDecodebin3 * dbin, DecodebinOutputStream * output)
{
  if (output->slot) {
    if (output->decoder_sink && output->decoder)
      gst_pad_unlink (output->slot->src_pad, output->decoder_sink);

    output->slot->output = NULL;
    output->slot = NULL;
  }
  gst_object_replace ((GstObject **) & output->decoder_sink, NULL);
  decode_pad_set_target ((GstGhostPad *) output->src_pad, NULL);
  gst_object_replace ((GstObject **) & output->decoder_src, NULL);
  if (output->src_exposed) {
    gst_element_remove_pad ((GstElement *) dbin, output->src_pad);
  }
  if (output->decoder) {
    gst_element_set_locked_state (output->decoder, TRUE);
    gst_element_set_state (output->decoder, GST_STATE_NULL);
    gst_bin_remove ((GstBin *) dbin, output->decoder);
  }
  g_free (output);
}

static GstStateChangeReturn
gst_decodebin3_change_state (GstElement * element, GstStateChange transition)
{
  GstDecodebin3 *dbin = (GstDecodebin3 *) element;
  GstStateChangeReturn ret;

  /* Upwards */
  switch (transition) {
    default:
      break;
  }
  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto beach;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_decodebin3_reset (dbin);
      break;
    default:
      break;
  }
beach:
  return ret;
}
