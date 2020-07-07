/* GStreamer
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

/* TODO/FIXME:
 *
 * * BUFFERING MESSAGES
 * ** How/Where do we deal with buffering messages from a new/prerolling
 *    source ? Ideally we want to re-use the same sourcebin ?
 * ** Remember last buffering messages per source handler, if the SourceEntry
 *    group_id is the one being currently outputted on the source ghostpads,
 *    post the (last) buffering messages.
 *    If no group_id is being outputted (still prerolling), then output
 *    the messages directly
 *
 * * ASYNC HANDLING
 * ** URIDECODEBIN3 is not async-aware.
 *
 * * GAPLESS HANDLING
 * ** Correlate group_id and URI to know when/which stream is being outputted/started
 */

/**
 * SECTION:element-uridecodebin3
 * @title: uridecodebin3
 *
 * Decodes data from a URI into raw media. It selects a source element that can
 * handle the given #GstURIDecodeBin3:uri scheme and connects it to a decodebin.
 */

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

#define GST_TYPE_URI_DECODE_BIN3 \
  (gst_uri_decode_bin3_get_type())
#define GST_URI_DECODE_BIN3(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_URI_DECODE_BIN3,GstURIDecodeBin3))
#define GST_URI_DECODE_BIN3_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_URI_DECODE_BIN3,GstURIDecodeBin3Class))
#define GST_IS_URI_DECODE_BIN3(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_URI_DECODE_BIN3))
#define GST_IS_URI_DECODE_BIN3_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_URI_DECODE_BIN3))
#define GST_URI_DECODE_BIN3_CAST(obj) ((GstURIDecodeBin3 *) (obj))

typedef struct _GstSourceGroup GstSourceGroup;
typedef struct _GstURIDecodeBin3 GstURIDecodeBin3;
typedef struct _GstURIDecodeBin3Class GstURIDecodeBin3Class;

#define GST_URI_DECODE_BIN3_LOCK(dec) (g_mutex_lock(&((GstURIDecodeBin3*)(dec))->lock))
#define GST_URI_DECODE_BIN3_UNLOCK(dec) (g_mutex_unlock(&((GstURIDecodeBin3*)(dec))->lock))

typedef struct _GstPlayItem GstPlayItem;
typedef struct _GstSourceItem GstSourceItem;
typedef struct _GstSourceHandler GstSourceHandler;
typedef struct _OutputPad OutputPad;

/* A structure describing a play item, which travels through the elements
 * over time. */
struct _GstPlayItem
{
  GstURIDecodeBin3 *uridecodebin;

  /* Main URI */
  GstSourceItem *main_item;

  /* Auxiliary URI */
  /* FIXME : Replace by a list later */
  GstSourceItem *sub_item;

  /* The group_id used to identify this play item via STREAM_START events
   * This is the group_id which will be used externally (i.e. rewritten
   * to outgoing STREAM_START events and in emitted signals).
   * The urisourcebin-specific group_id is located in GstSourceItem */
  guint group_id;

  /* Is this play item the one being currently outputted by decodebin3
   * and on our source ghostpads */
  gboolean currently_outputted;
};

struct _GstSourceItem
{
  /* The GstPlayItem to which this GstSourceItem belongs to */
  GstPlayItem *play_item;

  gchar *uri;

  /* The urisourcebin controlling this uri
   * Can be NULL */
  GstSourceHandler *handler;

  /* Last buffering information */
  gint last_perc;
  GstMessage *last_buffering_message;

  /* The groupid created by urisourcebin for this uri */
  guint internal_groupid;

  /* FIXME : Add tag lists and other uri-specific items here ? */
};

/* Structure wrapping everything related to a urisourcebin */
struct _GstSourceHandler
{
  GstURIDecodeBin3 *uridecodebin;

  GstElement *urisourcebin;

  /* Signal handlers */
  gulong pad_added_id;
  gulong pad_removed_id;
  gulong source_setup_id;
  gulong about_to_finish_id;

  /* TRUE if the controlled urisourcebin was added to uridecodebin */
  gboolean active;

  /* whether urisourcebin is drained or not.
   * Reset if/when setting a new URI */
  gboolean drained;

  /* Whether urisourcebin posted EOS on all pads and
   * there is no pending entry */
  gboolean is_eos;

  /* TRUE if the urisourcebin handles main item */
  gboolean is_main_source;

  /* buffering message stored for after switching */
  GstMessage *pending_buffering_msg;
};

/* Controls an output source pad */
struct _OutputPad
{
  GstURIDecodeBin3 *uridecodebin;

  GstPad *target_pad;
  GstPad *ghost_pad;

  /* Downstream event probe id */
  gulong probe_id;

  /* TRUE if the pad saw EOS. Reset to FALSE on STREAM_START */
  gboolean is_eos;

  /* The last seen (i.e. current) group_id
   * Can be (guint)-1 if no group_id was seen yet */
  guint current_group_id;
};

/**
 * GstURIDecodeBin3
 *
 * uridecodebin3 element struct
 */
struct _GstURIDecodeBin3
{
  GstBin parent_instance;

  GMutex lock;                  /* lock for constructing */

  /* Properties */
  GstElement *source;
  guint64 connection_speed;     /* In bits/sec (0 = unknown) */
  GstCaps *caps;
  guint64 buffer_duration;      /* When buffering, buffer duration (ns) */
  guint buffer_size;            /* When buffering, buffer size (bytes) */
  gboolean download;
  gboolean use_buffering;
  guint64 ring_buffer_max_size;

  GList *play_items;            /* List of GstPlayItem ordered by time of
                                 * creation. Head of list is therefore the
                                 * current (or pending if initial) one being
                                 * outputted */
  GstPlayItem *current;         /* Currently active GstPlayItem. Can be NULL
                                 * if no entry is active yet (i.e. no source
                                 * pads) */

  /* sources.
   * FIXME : Replace by a more modular system later on */
  GstSourceHandler *main_handler;
  GstSourceHandler *sub_handler;

  /* URI handling
   * FIXME : Switch to a playlist-based API */
  gchar *uri;
  gboolean uri_changed;         /* TRUE if uri changed */
  gchar *suburi;
  gboolean suburi_changed;      /* TRUE if suburi changed */

  /* A global decodebin3 that's used to actually do decoding */
  GstElement *decodebin;

  /* db3 signals */
  gulong db_pad_added_id;
  gulong db_pad_removed_id;
  gulong db_select_stream_id;
  gulong db_about_to_finish_id;

  GList *output_pads;           /* List of OutputPad */

  GList *source_handlers;       /* List of SourceHandler */

  /* Whether we already signalled about-to-finish or not
   * FIXME: Track this by group-id ! */
  gboolean posted_about_to_finish;
};

static gint
gst_uridecodebin3_select_stream (GstURIDecodeBin3 * dbin,
    GstStreamCollection * collection, GstStream * stream)
{
  GST_LOG_OBJECT (dbin, "default select-stream, returning -1");

  return -1;
}

struct _GstURIDecodeBin3Class
{
  GstBinClass parent_class;

    gint (*select_stream) (GstURIDecodeBin3 * dbin,
      GstStreamCollection * collection, GstStream * stream);
};

GST_DEBUG_CATEGORY_STATIC (gst_uri_decode_bin3_debug);
#define GST_CAT_DEFAULT gst_uri_decode_bin3_debug

/* signals */
enum
{
  SIGNAL_SELECT_STREAM,
  SIGNAL_SOURCE_SETUP,
  SIGNAL_ABOUT_TO_FINISH,
  LAST_SIGNAL
};

#if 0
static GstStaticCaps raw_audio_caps = GST_STATIC_CAPS ("audio/x-raw(ANY)");
static GstStaticCaps raw_video_caps = GST_STATIC_CAPS ("video/x-raw(ANY)");
#endif

/* properties */
#define DEFAULT_PROP_URI            NULL
#define DEFAULT_PROP_SUBURI            NULL
#define DEFAULT_CONNECTION_SPEED    0
#define DEFAULT_CAPS                (gst_static_caps_get (&default_raw_caps))
#define DEFAULT_BUFFER_DURATION     -1
#define DEFAULT_BUFFER_SIZE         -1
#define DEFAULT_DOWNLOAD            FALSE
#define DEFAULT_USE_BUFFERING       FALSE
#define DEFAULT_RING_BUFFER_MAX_SIZE 0

enum
{
  PROP_0,
  PROP_URI,
  PROP_CURRENT_URI,
  PROP_SUBURI,
  PROP_CURRENT_SUBURI,
  PROP_SOURCE,
  PROP_CONNECTION_SPEED,
  PROP_BUFFER_SIZE,
  PROP_BUFFER_DURATION,
  PROP_DOWNLOAD,
  PROP_USE_BUFFERING,
  PROP_RING_BUFFER_MAX_SIZE,
  PROP_CAPS
};

static guint gst_uri_decode_bin3_signals[LAST_SIGNAL] = { 0 };

static GstStaticCaps default_raw_caps = GST_STATIC_CAPS (DEFAULT_RAW_CAPS);

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

GType gst_uri_decode_bin3_get_type (void);
#define gst_uri_decode_bin3_parent_class parent_class
G_DEFINE_TYPE (GstURIDecodeBin3, gst_uri_decode_bin3, GST_TYPE_BIN);

#define REMOVE_SIGNAL(obj,id)            \
if (id) {                                \
  g_signal_handler_disconnect (obj, id); \
  id = 0;                                \
}

static void gst_uri_decode_bin3_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_uri_decode_bin3_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_uri_decode_bin3_finalize (GObject * obj);
static GstSourceHandler *new_source_handler (GstURIDecodeBin3 * uridecodebin,
    gboolean is_main);

static GstStateChangeReturn gst_uri_decode_bin3_change_state (GstElement *
    element, GstStateChange transition);

static gboolean
_gst_int_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gint res = g_value_get_int (handler_return);

  if (!(ihint->run_type & G_SIGNAL_RUN_CLEANUP))
    g_value_set_int (return_accu, res);

  if (res == -1)
    return TRUE;

  return FALSE;
}


static void
gst_uri_decode_bin3_class_init (GstURIDecodeBin3Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_uri_decode_bin3_set_property;
  gobject_class->get_property = gst_uri_decode_bin3_get_property;
  gobject_class->finalize = gst_uri_decode_bin3_finalize;

  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI to decode",
          DEFAULT_PROP_URI, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CURRENT_URI,
      g_param_spec_string ("current-uri", "Current URI",
          "The currently playing URI", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBURI,
      g_param_spec_string ("suburi", ".sub-URI", "Optional URI of a subtitle",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CURRENT_SUBURI,
      g_param_spec_string ("current-suburi", "Current .sub-URI",
          "The currently playing URI of a subtitle",
          NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

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
   * GstURIDecodeBin3::download:
   *
   * For certain media type, enable download buffering.
   */
  g_object_class_install_property (gobject_class, PROP_DOWNLOAD,
      g_param_spec_boolean ("download", "Download",
          "Attempt download buffering when buffering network streams",
          DEFAULT_DOWNLOAD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstURIDecodeBin3::use-buffering:
   *
   * Emit BUFFERING messages based on low-/high-percent thresholds of the
   * demuxed or parsed data.
   * When download buffering is activated and used for the current media
   * type, this property does nothing. Otherwise perform buffering on the
   * demuxed or parsed media.
   */
  g_object_class_install_property (gobject_class, PROP_USE_BUFFERING,
      g_param_spec_boolean ("use-buffering", "Use Buffering",
          "Perform buffering on demuxed/parsed media",
          DEFAULT_USE_BUFFERING, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstURIDecodeBin3::ring-buffer-max-size
   *
   * The maximum size of the ring buffer in kilobytes. If set to 0, the ring
   * buffer is disabled. Default is 0.
   */
  g_object_class_install_property (gobject_class, PROP_RING_BUFFER_MAX_SIZE,
      g_param_spec_uint64 ("ring-buffer-max-size",
          "Max. ring buffer size (bytes)",
          "Max. amount of data in the ring buffer (bytes, 0 = ring buffer disabled)",
          0, G_MAXUINT, DEFAULT_RING_BUFFER_MAX_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CAPS,
      g_param_spec_boxed ("caps", "Caps",
          "The caps on which to stop decoding. (NULL = default)",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstURIDecodebin3::select-stream
   * @decodebin: a #GstURIDecodebin3
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
  gst_uri_decode_bin3_signals[SIGNAL_SELECT_STREAM] =
      g_signal_new ("select-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstURIDecodeBin3Class, select_stream),
      _gst_int_accumulator, NULL, NULL, G_TYPE_INT, 2,
      GST_TYPE_STREAM_COLLECTION, GST_TYPE_STREAM);

  /**
   * GstURIDecodeBin3::source-setup:
   * @bin: the uridecodebin.
   * @source: source element
   *
   * This signal is emitted after a source element has been created, so
   * it can be configured by setting additional properties (e.g. set a
   * proxy server for an http source, or set the device and read speed for
   * an audio cd source).
   */
  gst_uri_decode_bin3_signals[SIGNAL_SOURCE_SETUP] =
      g_signal_new ("source-setup", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);
  /**
   * GstURIDecodeBin3::about-to-finish:
   *
   * This signal is emitted when the data for the selected URI is
   * entirely buffered and it is safe to specify another URI.
   */
  gst_uri_decode_bin3_signals[SIGNAL_ABOUT_TO_FINISH] =
      g_signal_new ("about-to-finish", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0, G_TYPE_NONE);


  gst_element_class_add_static_pad_template (gstelement_class,
      &video_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &audio_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &text_src_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_set_static_metadata (gstelement_class,
      "URI Decoder", "Generic/Bin/Decoder",
      "Autoplug and decode an URI to raw media",
      "Edward Hervey <edward@centricular.com>, Jan Schmidt <jan@centricular.com>");

  gstelement_class->change_state = gst_uri_decode_bin3_change_state;

  klass->select_stream = gst_uridecodebin3_select_stream;
}

static GstPadProbeReturn
db_src_probe (GstPad * pad, GstPadProbeInfo * info, OutputPad * output)
{
  /* FIXME : IMPLEMENT */

  /* EOS : Mark pad as EOS */

  /* STREAM_START : Store group_id and check if currently active
   *  PlayEntry changed */

  return GST_PAD_PROBE_OK;
}

static OutputPad *
add_output_pad (GstURIDecodeBin3 * dec, GstPad * target_pad)
{
  OutputPad *output;
  gchar *pad_name;

  output = g_slice_new0 (OutputPad);

  GST_LOG_OBJECT (dec, "Created output %p", output);

  output->uridecodebin = dec;
  output->target_pad = target_pad;
  output->current_group_id = (guint) - 1;
  pad_name = gst_pad_get_name (target_pad);
  output->ghost_pad = gst_ghost_pad_new (pad_name, target_pad);
  g_free (pad_name);

  gst_pad_set_active (output->ghost_pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (dec), output->ghost_pad);

  output->probe_id =
      gst_pad_add_probe (output->target_pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, (GstPadProbeCallback) db_src_probe,
      output, NULL);

  /* FIXME: LOCK TO PROTECT PAD LIST */
  dec->output_pads = g_list_append (dec->output_pads, output);

  return output;
}

static void
db_pad_added_cb (GstElement * element, GstPad * pad, GstURIDecodeBin3 * dec)
{
  GST_DEBUG_OBJECT (dec, "Wrapping new pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (GST_PAD_IS_SRC (pad))
    add_output_pad (dec, pad);
}

static void
db_pad_removed_cb (GstElement * element, GstPad * pad, GstURIDecodeBin3 * dec)
{
  GList *tmp;
  OutputPad *output = NULL;

  if (!GST_PAD_IS_SRC (pad))
    return;

  GST_DEBUG_OBJECT (dec, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));
  /* FIXME: LOCK for list access */

  for (tmp = dec->output_pads; tmp; tmp = tmp->next) {
    OutputPad *cand = (OutputPad *) tmp->data;

    if (cand->target_pad == pad) {
      output = cand;
      dec->output_pads = g_list_delete_link (dec->output_pads, tmp);
      break;
    }
  }

  if (output) {
    GST_LOG_OBJECT (element, "Removing output %p", output);
    /* Remove source ghost pad */
    gst_ghost_pad_set_target ((GstGhostPad *) output->ghost_pad, NULL);
    gst_element_remove_pad ((GstElement *) dec, output->ghost_pad);

    /* FIXME : Update global/current PlayEntry group_id (did we switch ?) */

    /* Remove event probe */
    gst_pad_remove_probe (output->target_pad, output->probe_id);

    g_slice_free (OutputPad, output);
  }
}

static gint
db_select_stream_cb (GstElement * decodebin,
    GstStreamCollection * collection, GstStream * stream,
    GstURIDecodeBin3 * uridecodebin)
{
  gint response = -1;

  g_signal_emit (uridecodebin,
      gst_uri_decode_bin3_signals[SIGNAL_SELECT_STREAM], 0, collection, stream,
      &response);
  return response;
}

static void
db_about_to_finish_cb (GstElement * decodebin, GstURIDecodeBin3 * uridecodebin)
{
  if (!uridecodebin->posted_about_to_finish) {
    uridecodebin->posted_about_to_finish = TRUE;
    g_signal_emit (uridecodebin,
        gst_uri_decode_bin3_signals[SIGNAL_ABOUT_TO_FINISH], 0, NULL);
  }
}

static void
gst_uri_decode_bin3_init (GstURIDecodeBin3 * dec)
{
  g_mutex_init (&dec->lock);

  dec->uri = DEFAULT_PROP_URI;
  dec->suburi = DEFAULT_PROP_SUBURI;
  dec->connection_speed = DEFAULT_CONNECTION_SPEED;
  dec->caps = DEFAULT_CAPS;
  dec->buffer_duration = DEFAULT_BUFFER_DURATION;
  dec->buffer_size = DEFAULT_BUFFER_SIZE;
  dec->download = DEFAULT_DOWNLOAD;
  dec->use_buffering = DEFAULT_USE_BUFFERING;
  dec->ring_buffer_max_size = DEFAULT_RING_BUFFER_MAX_SIZE;

  dec->decodebin = gst_element_factory_make ("decodebin3", NULL);
  gst_bin_add (GST_BIN_CAST (dec), dec->decodebin);
  dec->db_pad_added_id =
      g_signal_connect (dec->decodebin, "pad-added",
      G_CALLBACK (db_pad_added_cb), dec);
  dec->db_pad_removed_id =
      g_signal_connect (dec->decodebin, "pad-removed",
      G_CALLBACK (db_pad_removed_cb), dec);
  dec->db_select_stream_id =
      g_signal_connect (dec->decodebin, "select-stream",
      G_CALLBACK (db_select_stream_cb), dec);
  dec->db_about_to_finish_id =
      g_signal_connect (dec->decodebin, "about-to-finish",
      G_CALLBACK (db_about_to_finish_cb), dec);

  GST_OBJECT_FLAG_SET (dec, GST_ELEMENT_FLAG_SOURCE);
  gst_bin_set_suppressed_flags (GST_BIN (dec),
      GST_ELEMENT_FLAG_SOURCE | GST_ELEMENT_FLAG_SINK);
}

static void
gst_uri_decode_bin3_finalize (GObject * obj)
{
  GstURIDecodeBin3 *dec = GST_URI_DECODE_BIN3 (obj);

  g_mutex_clear (&dec->lock);
  g_free (dec->uri);
  g_free (dec->suburi);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstStateChangeReturn
activate_source_item (GstSourceItem * item)
{
  GstSourceHandler *handler = item->handler;

  if (handler == NULL) {
    GST_WARNING ("Can't activate item without a handler");
    return GST_STATE_CHANGE_FAILURE;
  }

  g_object_set (handler->urisourcebin, "uri", item->uri, NULL);
  if (!handler->active) {
    gst_bin_add ((GstBin *) handler->uridecodebin, handler->urisourcebin);
    /* if (!gst_element_sync_state_with_parent (handler->urisourcebin)) */
    /*   return GST_STATE_CHANGE_FAILURE; */
    handler->active = TRUE;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static void
src_pad_added_cb (GstElement * element, GstPad * pad,
    GstSourceHandler * handler)
{
  GstURIDecodeBin3 *uridecodebin;
  GstPad *sinkpad = NULL;
  GstPadLinkReturn res;
  GstPlayItem *current_play_item;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  uridecodebin = handler->uridecodebin;
  current_play_item = uridecodebin->current;

  GST_DEBUG_OBJECT (uridecodebin,
      "New pad %" GST_PTR_FORMAT " from source %" GST_PTR_FORMAT, pad, element);

  /* FIXME: Add probe to unify group_id and detect EOS */

  /* Try to link to main sink pad only if it's from a main handler */
  if (handler->is_main_source) {
    sinkpad = gst_element_get_static_pad (uridecodebin->decodebin, "sink");
    if (gst_pad_is_linked (sinkpad)) {
      gst_object_unref (sinkpad);
      sinkpad = NULL;
    }
  }

  if (sinkpad == NULL)
    sinkpad = gst_element_get_request_pad (uridecodebin->decodebin, "sink_%u");

  if (sinkpad) {
    GST_DEBUG_OBJECT (uridecodebin,
        "Linking %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT, pad, sinkpad);
    res = gst_pad_link (pad, sinkpad);
    gst_object_unref (sinkpad);
    if (GST_PAD_LINK_FAILED (res))
      goto link_failed;
  }

  /* Activate sub_item after the main source activation was finished */
  if (handler->is_main_source && current_play_item->sub_item
      && !current_play_item->sub_item->handler) {
    current_play_item->sub_item->handler =
        new_source_handler (uridecodebin, FALSE);
    ret = activate_source_item (current_play_item->sub_item);
    if (ret == GST_STATE_CHANGE_FAILURE)
      goto sub_item_activation_failed;
  }

  return;

link_failed:
  {
    GST_ERROR_OBJECT (uridecodebin,
        "failed to link pad %s:%s to decodebin, reason %s (%d)",
        GST_DEBUG_PAD_NAME (pad), gst_pad_link_get_name (res), res);
    return;
  }
sub_item_activation_failed:
  {
    GST_ERROR_OBJECT (uridecodebin,
        "failed to activate subtitle playback item");
    return;
  }
}

static void
src_pad_removed_cb (GstElement * element, GstPad * pad,
    GstSourceHandler * handler)
{
  /* FIXME : IMPLEMENT */
}

static void
src_source_setup_cb (GstElement * element, GstElement * source,
    GstSourceHandler * handler)
{
  g_signal_emit (handler->uridecodebin,
      gst_uri_decode_bin3_signals[SIGNAL_SOURCE_SETUP], 0, source, NULL);
}

static void
src_about_to_finish_cb (GstElement * element, GstSourceHandler * handler)
{
  /* FIXME : check if all sources are done */
  if (!handler->uridecodebin->posted_about_to_finish) {
    handler->uridecodebin->posted_about_to_finish = TRUE;
    g_signal_emit (handler->uridecodebin,
        gst_uri_decode_bin3_signals[SIGNAL_ABOUT_TO_FINISH], 0, NULL);
  }
}

static GstSourceHandler *
new_source_handler (GstURIDecodeBin3 * uridecodebin, gboolean is_main)
{
  GstSourceHandler *handler;

  handler = g_slice_new0 (GstSourceHandler);

  handler->uridecodebin = uridecodebin;
  handler->is_main_source = is_main;
  handler->urisourcebin = gst_element_factory_make ("urisourcebin", NULL);
  /* Set pending properties */
  g_object_set (handler->urisourcebin,
      "connection-speed", uridecodebin->connection_speed / 1000,
      "download", uridecodebin->download,
      "use-buffering", uridecodebin->use_buffering,
      "buffer-duration", uridecodebin->buffer_duration,
      "buffer-size", uridecodebin->buffer_size,
      "ring-buffer-max-size", uridecodebin->ring_buffer_max_size, NULL);

  handler->pad_added_id =
      g_signal_connect (handler->urisourcebin, "pad-added",
      (GCallback) src_pad_added_cb, handler);
  handler->pad_removed_id =
      g_signal_connect (handler->urisourcebin, "pad-removed",
      (GCallback) src_pad_removed_cb, handler);
  handler->source_setup_id =
      g_signal_connect (handler->urisourcebin, "source-setup",
      (GCallback) src_source_setup_cb, handler);
  handler->about_to_finish_id =
      g_signal_connect (handler->urisourcebin, "about-to-finish",
      (GCallback) src_about_to_finish_cb, handler);

  uridecodebin->source_handlers =
      g_list_append (uridecodebin->source_handlers, handler);

  return handler;
}

static void
gst_uri_decode_bin3_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstURIDecodeBin3 *dec = GST_URI_DECODE_BIN3 (object);

  switch (prop_id) {
    case PROP_URI:
      if (dec->uri)
        g_free (dec->uri);
      dec->uri = g_value_dup_string (value);
      break;
    case PROP_SUBURI:
      if (dec->suburi)
        g_free (dec->suburi);
      dec->suburi = g_value_dup_string (value);
      break;
    case PROP_CONNECTION_SPEED:
      GST_URI_DECODE_BIN3_LOCK (dec);
      dec->connection_speed = g_value_get_uint64 (value) * 1000;
      GST_URI_DECODE_BIN3_UNLOCK (dec);
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
    case PROP_CAPS:
      GST_OBJECT_LOCK (dec);
      if (dec->caps)
        gst_caps_unref (dec->caps);
      dec->caps = g_value_dup_boxed (value);
      GST_OBJECT_UNLOCK (dec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_uri_decode_bin3_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstURIDecodeBin3 *dec = GST_URI_DECODE_BIN3 (object);

  switch (prop_id) {
    case PROP_URI:
    {
      g_value_set_string (value, dec->uri);
      break;
    }
    case PROP_CURRENT_URI:
    {
      if (dec->current && dec->current->main_item) {
        g_value_set_string (value, dec->current->main_item->uri);
      } else {
        g_value_set_string (value, NULL);
      }
      break;
    }
    case PROP_SUBURI:
    {
      g_value_set_string (value, dec->suburi);
      break;
    }
    case PROP_CURRENT_SUBURI:
    {
      if (dec->current && dec->current->sub_item) {
        g_value_set_string (value, dec->current->sub_item->uri);
      } else {
        g_value_set_string (value, NULL);
      }
      break;
    }
    case PROP_SOURCE:
    {
      GST_OBJECT_LOCK (dec);
      g_value_set_object (value, dec->source);
      GST_OBJECT_UNLOCK (dec);
      break;
    }
    case PROP_CONNECTION_SPEED:
      GST_URI_DECODE_BIN3_LOCK (dec);
      g_value_set_uint64 (value, dec->connection_speed / 1000);
      GST_URI_DECODE_BIN3_UNLOCK (dec);
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
    case PROP_CAPS:
      GST_OBJECT_LOCK (dec);
      g_value_set_boxed (value, dec->caps);
      GST_OBJECT_UNLOCK (dec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
free_source_handler (GstURIDecodeBin3 * uridecodebin,
    GstSourceHandler * handler)
{
  GST_LOG_OBJECT (uridecodebin, "source handler %p", handler);
  if (handler->active) {
    GST_LOG_OBJECT (uridecodebin, "Removing %" GST_PTR_FORMAT,
        handler->urisourcebin);
    gst_element_set_state (handler->urisourcebin, GST_STATE_NULL);
    gst_bin_remove ((GstBin *) uridecodebin, handler->urisourcebin);
  }
  uridecodebin->source_handlers =
      g_list_remove (uridecodebin->source_handlers, handler);
  g_slice_free (GstSourceHandler, handler);
}

static GstSourceItem *
new_source_item (GstURIDecodeBin3 * dec, GstPlayItem * item, gchar * uri)
{
  GstSourceItem *sourceitem = g_slice_new0 (GstSourceItem);

  sourceitem->play_item = item;
  sourceitem->uri = uri;

  return sourceitem;
}

static void
free_source_item (GstURIDecodeBin3 * uridecodebin, GstSourceItem * item)
{
  GST_LOG_OBJECT (uridecodebin, "source item %p", item);
  if (item->handler)
    free_source_handler (uridecodebin, item->handler);
  g_slice_free (GstSourceItem, item);
}

static GstPlayItem *
new_play_item (GstURIDecodeBin3 * dec, gchar * uri, gchar * suburi)
{
  GstPlayItem *item = g_slice_new0 (GstPlayItem);

  item->uridecodebin = dec;
  item->main_item = new_source_item (dec, item, uri);
  if (suburi)
    item->sub_item = new_source_item (dec, item, suburi);

  return item;
}

static void
free_play_item (GstURIDecodeBin3 * dec, GstPlayItem * item)
{
  GST_LOG_OBJECT (dec, "play item %p", item);
  if (item->main_item)
    free_source_item (dec, item->main_item);
  if (item->sub_item)
    free_source_item (dec, item->sub_item);

  g_slice_free (GstPlayItem, item);
}

/* Sync source handlers for the given play item. Might require creating/removing some
 * and/or configure the handlers accordingly */
static GstStateChangeReturn
assign_handlers_to_item (GstURIDecodeBin3 * dec, GstPlayItem * item)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  /* FIXME : Go over existing handlers to see if we can assign some to the
   * given item */

  /* Create missing handlers */
  if (item->main_item->handler == NULL) {
    item->main_item->handler = new_source_handler (dec, TRUE);
    ret = activate_source_item (item->main_item);
    if (ret == GST_STATE_CHANGE_FAILURE)
      return ret;
  }

  return ret;
}

/* Called to activate the next play item */
static GstStateChangeReturn
activate_next_play_item (GstURIDecodeBin3 * dec)
{
  GstPlayItem *item;
  GstStateChangeReturn ret;

  /* If there is no current play entry, create one from the uri/suburi
   * FIXME : Use a playlist API in the future */
  item = new_play_item (dec, dec->uri, dec->suburi);

  ret = assign_handlers_to_item (dec, item);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    free_play_item (dec, item);
    return ret;
  }

  dec->play_items = g_list_append (dec->play_items, item);
  dec->current = dec->play_items->data;

  return ret;
}

static void
free_play_items (GstURIDecodeBin3 * dec)
{
  GList *tmp;

  for (tmp = dec->play_items; tmp; tmp = tmp->next) {
    GstPlayItem *item = (GstPlayItem *) tmp->data;
    free_play_item (dec, item);
  }

  g_list_free (dec->play_items);
  dec->play_items = NULL;
}

static GstStateChangeReturn
gst_uri_decode_bin3_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstURIDecodeBin3 *uridecodebin = (GstURIDecodeBin3 *) element;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ret = activate_next_play_item (uridecodebin);
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto failure;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto failure;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* FIXME: Cleanup everything */
      free_play_items (uridecodebin);
      /* Free play item */
      uridecodebin->posted_about_to_finish = FALSE;
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
failure:
  {
    if (transition == GST_STATE_CHANGE_READY_TO_PAUSED)
      free_play_items (uridecodebin);
    return ret;
  }
}


gboolean
gst_uri_decode_bin3_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_uri_decode_bin3_debug, "uridecodebin3", 0,
      "URI decoder element 3");

  return gst_element_register (plugin, "uridecodebin3", GST_RANK_NONE,
      GST_TYPE_URI_DECODE_BIN3);
}
