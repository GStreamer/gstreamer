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
#include "gstplaybasebin.h"
#include "gstplay-marshal.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_base_bin_debug);
#define GST_CAT_DEFAULT gst_play_base_bin_debug

#define DEFAULT_QUEUE_SIZE  (3 * GST_SECOND)

/* props */
enum
{
  ARG_0,
  ARG_URI,
  ARG_THREADED,
  ARG_NSTREAMS,
  ARG_QUEUE_SIZE,
  ARG_STREAMINFO,
};

/* signals */
enum
{
  MUTE_STREAM_SIGNAL,
  LINK_STREAM_SIGNAL,
  UNLINK_STREAM_SIGNAL,
  LAST_SIGNAL
};

static void gst_play_base_bin_class_init (GstPlayBaseBinClass * klass);
static void gst_play_base_bin_init (GstPlayBaseBin * play_base_bin);
static void gst_play_base_bin_dispose (GObject * object);

static void gst_play_base_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_play_base_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);
static GstElementStateReturn gst_play_base_bin_change_state (GstElement *
    element);

static void gst_play_base_bin_add_element (GstBin * bin, GstElement * element);
static void gst_play_base_bin_remove_element (GstBin * bin,
    GstElement * element);

extern GstElementStateReturn gst_element_set_state_func (GstElement * element,
    GstElementState state);

static void gst_play_base_bin_error (GstElement * element,
    GstElement * source, GError * error, gchar * debug, gpointer data);
static void gst_play_base_bin_found_tag (GstElement * element,
    GstElement * source, const GstTagList * taglist, gpointer data);

static GstElementClass *element_class;
static GstElementClass *parent_class;
static guint gst_play_base_bin_signals[LAST_SIGNAL] = { 0 };

GType
gst_play_base_bin_get_type (void)
{
  static GType gst_play_base_bin_type = 0;

  if (!gst_play_base_bin_type) {
    static const GTypeInfo gst_play_base_bin_info = {
      sizeof (GstPlayBaseBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_play_base_bin_class_init,
      NULL,
      NULL,
      sizeof (GstPlayBaseBin),
      0,
      (GInstanceInitFunc) gst_play_base_bin_init,
      NULL
    };

    gst_play_base_bin_type = g_type_register_static (GST_TYPE_BIN,
        "GstPlayBaseBin", &gst_play_base_bin_info, 0);
  }

  return gst_play_base_bin_type;
}

static void
gst_play_base_bin_class_init (GstPlayBaseBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstbin_klass = (GstBinClass *) klass;

  element_class = g_type_class_ref (gst_element_get_type ());
  parent_class = g_type_class_ref (gst_bin_get_type ());

  gobject_klass->set_property = gst_play_base_bin_set_property;
  gobject_klass->get_property = gst_play_base_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_NSTREAMS,
      g_param_spec_int ("nstreams", "NStreams", "number of streams",
          0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_QUEUE_SIZE,
      g_param_spec_uint64 ("queue-size", "Queue size",
          "Size of internal queues in nanoseconds", 0, G_MAXINT64,
          DEFAULT_QUEUE_SIZE, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_STREAMINFO,
      g_param_spec_pointer ("stream-info", "Stream info", "List of streaminfo",
          G_PARAM_READABLE));

  GST_DEBUG_CATEGORY_INIT (gst_play_base_bin_debug, "playbasebin", 0,
      "playbasebin");

  gst_play_base_bin_signals[LINK_STREAM_SIGNAL] =
      g_signal_new ("link-stream", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, link_stream),
      NULL, NULL, gst_play_marshal_VOID__OBJECT_OBJECT, G_TYPE_NONE, 2,
      G_TYPE_OBJECT, GST_TYPE_PAD);
  gst_play_base_bin_signals[UNLINK_STREAM_SIGNAL] =
      g_signal_new ("unlink-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstPlayBaseBinClass, unlink_stream),
      NULL, NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_base_bin_dispose);

  /* we handle state changes like an element */
  gstelement_klass->set_state = GST_ELEMENT_CLASS (element_class)->set_state;
  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_change_state);

  gstbin_klass->add_element = GST_DEBUG_FUNCPTR (gst_play_base_bin_add_element);
  gstbin_klass->remove_element =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_remove_element);

  klass->link_stream = gst_play_base_bin_link_stream;
  klass->unlink_stream = gst_play_base_bin_unlink_stream;
}

static void
gst_play_base_bin_init (GstPlayBaseBin * play_base_bin)
{
  play_base_bin->uri = NULL;
  play_base_bin->threaded = FALSE;
  play_base_bin->need_rebuild = TRUE;
  play_base_bin->source = NULL;
  play_base_bin->decoder = NULL;

  play_base_bin->preroll_lock = g_mutex_new ();
  play_base_bin->preroll_cond = g_cond_new ();
  play_base_bin->preroll_elems = NULL;
  play_base_bin->queue_size = DEFAULT_QUEUE_SIZE;

  GST_FLAG_SET (play_base_bin, GST_BIN_SELF_SCHEDULABLE);
}

static void
gst_play_base_bin_dispose (GObject * object)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (object);
  g_free (play_base_bin->uri);
  play_base_bin->uri = NULL;

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}

/* this signal will be fired when one of the queues with raw
 * data is filled. This means that the preroll stage is over and
 * playback should start */
static void
queue_overrun (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  GST_DEBUG ("queue %s overrun", gst_element_get_name (element));
  g_mutex_lock (play_base_bin->preroll_lock);
  GST_DEBUG ("signal preroll done");
  g_cond_signal (play_base_bin->preroll_cond);
  GST_DEBUG ("signaled preroll done");
  g_mutex_unlock (play_base_bin->preroll_lock);
}

/* generate a preroll element which is simply a queue. While there
 * are still dynamic elements in the pipeline, we wait for one
 * of the queues to fill. The assumption is that all the dynamic
 * streams will be detected by that time. 
 */
static GstElement *
gen_preroll_element (GstPlayBaseBin * play_base_bin, GstPad * pad)
{
  GstElement *element;
  gchar *name;
  guint sig;

  name = g_strdup_printf ("preroll_%s", gst_pad_get_name (pad));
  element = gst_element_factory_make ("queue", name);
  g_object_set (G_OBJECT (element), "max-size-buffers", 0, NULL);
  g_object_set (G_OBJECT (element), "max-size-bytes", 0, NULL);
  g_object_set (G_OBJECT (element), "max-size-time", play_base_bin->queue_size,
      NULL);
  sig =
      g_signal_connect (G_OBJECT (element), "overrun",
      G_CALLBACK (queue_overrun), play_base_bin);
  /* keep a ref to the signal id so that we can disconnect the signal callback
   * when we are done with the preroll */
  g_object_set_data (G_OBJECT (element), "signal_id", GINT_TO_POINTER (sig));
  g_free (name);

  return element;
}

static void
remove_prerolls (GstPlayBaseBin * play_base_bin)
{
  GList *prerolls, *infos;

  /* remove the preroll queues */
  for (prerolls = play_base_bin->preroll_elems; prerolls;
      prerolls = g_list_next (prerolls)) {
    GstElement *element = GST_ELEMENT (prerolls->data);

    GST_LOG ("removing preroll element %s", gst_element_get_name (element));
    gst_bin_remove (GST_BIN (play_base_bin->thread), element);
  }
  g_list_free (play_base_bin->preroll_elems);
  play_base_bin->preroll_elems = NULL;

  /* free the streaminfo too */
  for (infos = play_base_bin->streaminfo; infos; infos = g_list_next (infos)) {
    GstStreamInfo *info = GST_STREAM_INFO (infos->data);

    g_object_unref (info);
  }
  g_list_free (play_base_bin->streaminfo);
  play_base_bin->streaminfo = NULL;
  play_base_bin->nstreams = 0;
  play_base_bin->naudiopads = 0;
  play_base_bin->nvideopads = 0;
  play_base_bin->nunknownpads = 0;
}

/* signal fired when an unknown stream is found. We create a new
 * UNKNOWN streaminfo object 
 */
static void
unknown_type (GstElement * element, GstPad * pad, GstCaps * caps,
    GstPlayBaseBin * play_base_bin)
{
  gchar *capsstr;
  GstStreamInfo *info;

  capsstr = gst_caps_to_string (caps);
  g_warning ("don't know how to handle %s", capsstr);

  /* add the stream to the list */
  info = gst_stream_info_new (GST_OBJECT (pad), GST_STREAM_TYPE_UNKNOWN, NULL);
  play_base_bin->streaminfo = g_list_append (play_base_bin->streaminfo, info);

  g_free (capsstr);
}

/* add a streaminfo that indicates that the stream is handled by the
 * given element. This usually means that a stream without actual data is
 * produced but one that is sunken by an element. Examples of this are:
 * cdaudio, a hardware decoder/sink, dvd meta bins etc...
 */
static void
add_element_stream (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  GstStreamInfo *info;

  /* add the stream to the list */
  info =
      gst_stream_info_new (GST_OBJECT (element), GST_STREAM_TYPE_ELEMENT, NULL);
  play_base_bin->streaminfo = g_list_append (play_base_bin->streaminfo, info);
}

/* when the decoder element signals that no more pads will be generated, we
 * can stop the preroll
 */
static void
no_more_pads (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  GST_DEBUG ("no more pads");
  g_mutex_lock (play_base_bin->preroll_lock);
  GST_DEBUG ("signal preroll done");
  g_cond_signal (play_base_bin->preroll_cond);
  GST_DEBUG ("signaled preroll done");
  g_mutex_unlock (play_base_bin->preroll_lock);
}

/* signal fired when decodebin has found a new raw pad. We create
 * a preroll element if needed and the appropriate streaminfo.
 */
static void
new_decoded_pad (GstElement * element, GstPad * pad, gboolean last,
    GstPlayBaseBin * play_base_bin)
{
  GstStructure *structure;
  const gchar *mimetype;
  GstCaps *caps;
  GstElement *new_element = NULL;
  GstStreamInfo *info;
  GstStreamType type;
  GstPad *srcpad;
  gboolean need_preroll;

  GST_DEBUG ("play base: new decoded pad %d", last);

  /* first see if this pad has interesting caps */
  caps = gst_pad_get_caps (pad);
  if (caps == NULL || gst_caps_is_empty (caps)) {
    g_warning ("no type on pad %s:%s", GST_DEBUG_PAD_NAME (pad));
    return;
  }

  /* get the mime type */
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  play_base_bin->nstreams++;

  need_preroll = FALSE;

  if (g_str_has_prefix (mimetype, "audio/")) {
    type = GST_STREAM_TYPE_AUDIO;
    play_base_bin->naudiopads++;
    /* first audio pad gets a preroll element */
    if (play_base_bin->naudiopads == 1) {
      need_preroll = TRUE;
    }
  } else if (g_str_has_prefix (mimetype, "video/")) {
    type = GST_STREAM_TYPE_VIDEO;
    play_base_bin->nvideopads++;
    /* first video pad gets a preroll element */
    if (play_base_bin->nvideopads == 1) {
      need_preroll = TRUE;
    }
  } else {
    type = GST_STREAM_TYPE_UNKNOWN;
    play_base_bin->nunknownpads++;
  }

  if (last || !need_preroll) {
    GST_DEBUG ("play base: pad does not need preroll");
    srcpad = pad;
  } else {
    GST_DEBUG ("play base: pad needs preroll");

    new_element = gen_preroll_element (play_base_bin, pad);
    srcpad = gst_element_get_pad (new_element, "src");
    gst_bin_add (GST_BIN (play_base_bin->thread), new_element);
    play_base_bin->threaded = TRUE;

    play_base_bin->preroll_elems =
        g_list_prepend (play_base_bin->preroll_elems, new_element);

    gst_element_set_state (new_element, GST_STATE_READY);

    gst_pad_link (pad, gst_element_get_pad (new_element, "sink"));

    gst_element_set_state (new_element, GST_STATE_PAUSED);
  }

  /* add the stream to the list */
  info = gst_stream_info_new (GST_OBJECT (srcpad), type, NULL);
  play_base_bin->streaminfo = g_list_append (play_base_bin->streaminfo, info);

  /* signal the no more pads after adding the stream */
  if (last)
    no_more_pads (NULL, play_base_bin);
}

/* signal fired when the internal thread performed an unexpected  
 * state change. This usually indicated an error occured. We stop the
 * preroll stage.
 */
static void
state_change (GstElement * element,
    GstElementState old_state, GstElementState new_state, gpointer data)
{
  GstPlayBaseBin *play_base_bin = GST_PLAY_BASE_BIN (data);

  if (old_state > new_state) {
    /* EOS or error occurred */
    GST_DEBUG ("state changed downwards");
    g_mutex_lock (play_base_bin->preroll_lock);
    GST_DEBUG ("signal preroll done");
    g_cond_signal (play_base_bin->preroll_cond);
    GST_DEBUG ("signaled preroll done");
    g_mutex_unlock (play_base_bin->preroll_lock);
  }
}

/* construct and run the source and decoder elements until we found
 * all the streams or until a preroll queue has been filled.
 */
static gboolean
setup_source (GstPlayBaseBin * play_base_bin)
{
  GstElement *old_src;
  GstElement *old_dec;
  GstPad *srcpad = NULL;

  if (!play_base_bin->need_rebuild)
    return TRUE;

  /* keep ref to old souce in case creating the new source fails */
  old_src = play_base_bin->source;

  /* create and configure an element that can handle the uri */
  play_base_bin->source =
      gst_element_make_from_uri (GST_URI_SRC, play_base_bin->uri, "source");

  if (!play_base_bin->source) {
    /* whoops, could not create the source element */
    g_warning ("don't know how to read %s", play_base_bin->uri);
    play_base_bin->source = old_src;
    return FALSE;
  } else {
    if (old_src) {
      GST_LOG ("removing old src element %s", gst_element_get_name (old_src));
      gst_bin_remove (GST_BIN (play_base_bin->thread), old_src);
    }
    gst_bin_add (GST_BIN (play_base_bin->thread), play_base_bin->source);
    /* make sure the new element has the same state as the parent */
    if (gst_bin_sync_children_state (GST_BIN (play_base_bin->thread)) ==
        GST_STATE_FAILURE) {
      return FALSE;
    }
  }

  /* remove the old decoder now, if any */
  old_dec = play_base_bin->decoder;
  if (old_dec) {
    GST_LOG ("removing old decoder element %s", gst_element_get_name (old_dec));
    gst_bin_remove (GST_BIN (play_base_bin->thread), old_dec);
  }

  /* remove our previous preroll queues */
  remove_prerolls (play_base_bin);

  /* now see if the source element emits raw audio/video all by itself,
   * if so, we can create streams for the pads and be done with it.
   * Also check that is has source pads, if now, we assume it will
   * do everything itself.
   */
  {
    const GList *pads;
    gboolean is_raw = FALSE;

    /* assume we are going to have no output streams */
    gboolean no_out = TRUE;

    for (pads = gst_element_get_pad_list (play_base_bin->source);
        pads; pads = g_list_next (pads)) {
      GstPad *pad = GST_PAD (pads->data);
      GstStructure *structure;
      const gchar *mimetype;
      GstCaps *caps;

      if (GST_PAD_IS_SINK (pad))
        continue;

      no_out = FALSE;

      srcpad = pad;
      caps = gst_pad_get_caps (pad);

      if (caps == NULL || gst_caps_is_empty (caps) ||
          gst_caps_get_size (caps) == 0)
        continue;

      structure = gst_caps_get_structure (caps, 0);
      mimetype = gst_structure_get_name (structure);

      if (g_str_has_prefix (mimetype, "audio/x-raw") ||
          g_str_has_prefix (mimetype, "video/x-raw")) {
        new_decoded_pad (play_base_bin->source, pad, g_list_next (pads) == NULL,
            play_base_bin);
        is_raw = TRUE;
      }
    }
    if (is_raw) {
      return TRUE;
    }
    if (no_out) {
      /* create a stream to indicate that this uri is handled by a self
       * contained element */
      add_element_stream (play_base_bin->source, play_base_bin);
      return TRUE;
    }
  }

  {
    gboolean res;
    gint sig1, sig2, sig3, sig4;

    /* now create the decoder element */
    play_base_bin->decoder = gst_element_factory_make ("decodebin", "decoder");
    if (!play_base_bin->decoder) {
      g_warning ("can't find decoder element");
      play_base_bin->decoder = old_dec;
      return FALSE;
    } else {
      /* ref decoder so that the bin does not take ownership */
      gst_object_ref (GST_OBJECT (play_base_bin->decoder));
      gst_bin_add (GST_BIN (play_base_bin->thread), play_base_bin->decoder);
    }

    res = gst_pad_link (srcpad,
        gst_element_get_pad (play_base_bin->decoder, "sink"));
    if (!res) {
      g_warning ("can't link source to decoder element");
      return FALSE;
    }
    sig1 = g_signal_connect (G_OBJECT (play_base_bin->decoder),
        "new-decoded-pad", G_CALLBACK (new_decoded_pad), play_base_bin);
    sig2 = g_signal_connect (G_OBJECT (play_base_bin->decoder), "no-more-pads",
        G_CALLBACK (no_more_pads), play_base_bin);
    sig3 = g_signal_connect (G_OBJECT (play_base_bin->decoder), "unknown-type",
        G_CALLBACK (unknown_type), play_base_bin);

    /* either when the queues are filled or when the decoder element has no more
     * dynamic streams, the cond is unlocked. We can remove the signal handlers then
     */
    g_mutex_lock (play_base_bin->preroll_lock);
    if (gst_element_set_state (play_base_bin->thread, GST_STATE_PLAYING) ==
        GST_STATE_SUCCESS) {
      GList *prerolls;

      GST_DEBUG ("waiting for preroll...");
      sig4 = g_signal_connect (G_OBJECT (play_base_bin->thread),
          "state-change", G_CALLBACK (state_change), play_base_bin);
      g_cond_wait (play_base_bin->preroll_cond, play_base_bin->preroll_lock);
      GST_DEBUG ("preroll done !");

      /* remove signals */
      for (prerolls = play_base_bin->preroll_elems; prerolls;
          prerolls = g_list_next (prerolls)) {
        GstElement *element = GST_ELEMENT (prerolls->data);
        guint sig_id;

        sig_id =
            GPOINTER_TO_INT (g_object_get_data (G_OBJECT (element),
                "signal_id"));

        GST_LOG ("removing preroll signal %s", gst_element_get_name (element));
        g_signal_handler_disconnect (G_OBJECT (element), sig_id);
      }
    } else {
      GST_DEBUG ("state change failed, media cannot be loaded");
      sig4 = 0;
    }
    g_mutex_unlock (play_base_bin->preroll_lock);

    if (sig4 != 0)
      g_signal_handler_disconnect (G_OBJECT (play_base_bin->thread), sig4);
    g_signal_handler_disconnect (G_OBJECT (play_base_bin->decoder), sig3);
    g_signal_handler_disconnect (G_OBJECT (play_base_bin->decoder), sig2);
    g_signal_handler_disconnect (G_OBJECT (play_base_bin->decoder), sig1);

    play_base_bin->need_rebuild = FALSE;
  }

  return TRUE;
}

static void
gst_play_base_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlayBaseBin *play_base_bin;

  g_return_if_fail (GST_IS_PLAY_BASE_BIN (object));

  play_base_bin = GST_PLAY_BASE_BIN (object);

  switch (prop_id) {
    case ARG_URI:
    {
      const gchar *uri = g_value_get_string (value);

      if (uri == NULL) {
        g_warning ("cannot set NULL uri");
        return;
      }
      /* if we have no previous uri, or the new uri is different from the
       * old one, replug */
      if (play_base_bin->uri == NULL || strcmp (play_base_bin->uri, uri) != 0) {
        g_free (play_base_bin->uri);
        play_base_bin->uri = g_strdup (uri);

        GST_DEBUG ("setting new uri to %s", uri);

        play_base_bin->need_rebuild = TRUE;
      }
      break;
    }
    case ARG_QUEUE_SIZE:
      play_base_bin->queue_size = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_base_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPlayBaseBin *play_base_bin;

  g_return_if_fail (GST_IS_PLAY_BASE_BIN (object));

  play_base_bin = GST_PLAY_BASE_BIN (object);

  switch (prop_id) {
    case ARG_URI:
      g_value_set_string (value, play_base_bin->uri);
      break;
    case ARG_NSTREAMS:
      g_value_set_int (value, play_base_bin->nstreams);
      break;
    case ARG_QUEUE_SIZE:
      g_value_set_uint64 (value, play_base_bin->queue_size);
      break;
    case ARG_STREAMINFO:
      g_value_set_pointer (value, play_base_bin->streaminfo);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
play_base_eos (GstBin * bin, GstPlayBaseBin * play_base_bin)
{
  no_more_pads (GST_ELEMENT (bin), play_base_bin);

  gst_element_set_eos (GST_ELEMENT (play_base_bin));
}

static GstElementStateReturn
gst_play_base_bin_change_state (GstElement * element)
{
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    {
      GstScheduler *sched;

      play_base_bin->thread = gst_thread_new ("internal_thread");
      sched = gst_scheduler_factory_make ("opt", play_base_bin->thread);
      if (sched) {
        gst_element_set_scheduler (play_base_bin->thread, sched);

        //gst_object_set_parent (GST_OBJECT (play_base_bin->thread), GST_OBJECT (play_base_bin));

        gst_element_set_state (play_base_bin->thread, GST_STATE_READY);

        g_signal_connect (G_OBJECT (play_base_bin->thread), "eos",
            G_CALLBACK (play_base_eos), play_base_bin);
        g_signal_connect (play_base_bin->thread, "found_tag",
            G_CALLBACK (gst_play_base_bin_found_tag), play_base_bin);
      } else {
        g_warning ("could not get 'opt' scheduler");
        gst_object_unref (GST_OBJECT (play_base_bin->thread));
        play_base_bin->thread = NULL;

        ret = GST_STATE_FAILURE;
      }
      break;
    }
    case GST_STATE_READY_TO_PAUSED:
    {
      if (!setup_source (play_base_bin)) {
        GST_ELEMENT_ERROR (GST_ELEMENT (play_base_bin), STREAM,
            CODEC_NOT_FOUND,
            ("cannot open file \"%s\"", play_base_bin->uri), (NULL));
        ret = GST_STATE_FAILURE;
      } else if (!play_base_bin->streaminfo) {
        GST_ELEMENT_ERROR (GST_ELEMENT (play_base_bin), STREAM,
            CODEC_NOT_FOUND,
            ("Failed to find any supported stream in file \"%s\"",
                play_base_bin->uri), (NULL));
        ret = GST_STATE_FAILURE;
      } else {
        ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED);
      }
      if (ret == GST_STATE_SUCCESS) {
        /* error forwarding:
         * we only connect after the stream has been set up. If that failed,
         * we simply emit our own error. This also prevents us from failing
         * because one stream was unrecognized. */
        g_signal_connect (play_base_bin->thread, "error",
            G_CALLBACK (gst_play_base_bin_error), play_base_bin);
      } else {
        /* in case of no preroll, we might have streaminfo already... */
        g_list_foreach (play_base_bin->streaminfo,
            (GFunc) g_object_unref, NULL);
        g_list_free (play_base_bin->streaminfo);
        play_base_bin->streaminfo = NULL;
      }
      break;
    }
    case GST_STATE_PAUSED_TO_PLAYING:
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PLAYING);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED);
      break;
    case GST_STATE_PAUSED_TO_READY:
      g_signal_handlers_disconnect_by_func (play_base_bin->thread,
          G_CALLBACK (gst_play_base_bin_error), play_base_bin);
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_READY);
      play_base_bin->need_rebuild = TRUE;
      break;
    case GST_STATE_READY_TO_NULL:
      gst_object_unref (GST_OBJECT (play_base_bin->thread));
      break;
    default:
      break;
  }

  if (ret == GST_STATE_SUCCESS) {
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  }

  return ret;
}

/* virtual function to add elements to this bin. The idea is to
 * wrap the element in a thread automatically.
 */
static void
gst_play_base_bin_add_element (GstBin * bin, GstElement * element)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (bin);

  if (play_base_bin->thread) {
    GstScheduler *sched;
    GstClock *clock;

    if (play_base_bin->threaded) {
      gchar *name;
      GstElement *thread;

      name = g_strdup_printf ("thread_%s", gst_element_get_name (element));
      thread = gst_thread_new (name);
      g_free (name);

      gst_bin_add (GST_BIN (thread), element);
      element = thread;
    }
    gst_bin_add (GST_BIN (play_base_bin->thread), element);

    /* hack, the clock is not correctly distributed in the core */
    sched = gst_element_get_scheduler (GST_ELEMENT (play_base_bin->thread));
    clock = gst_scheduler_get_clock (sched);
    gst_scheduler_set_clock (sched, clock);

    /* FIXME set element to READY so that negotiation can happen. This
     * currently fails because of weird negotiation problems. */
    /* gst_element_set_state (element, GST_STATE_READY); */

  } else {
    g_warning ("adding elements is not allowed in NULL");
  }
}

/* virtual function to remove an element from this bin. We have to make
 * sure that we also remove the thread that we used as a container for
 * this element.
 */
static void
gst_play_base_bin_remove_element (GstBin * bin, GstElement * element)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (bin);

  if (play_base_bin->thread) {
    if (play_base_bin->threaded) {
      gchar *name;
      GstElement *thread;

      name = g_strdup_printf ("thread_%s", gst_element_get_name (element));
      thread = gst_bin_get_by_name (GST_BIN (play_base_bin->thread), name);
      g_free (name);

      if (!thread) {
        g_warning ("cannot find element to remove");
      } else {
        if (gst_element_get_parent (element) == GST_OBJECT (thread)) {
          /* we remove the element from the thread first so that the
           * state is not affected when someone holds a reference to it */
          gst_bin_remove (GST_BIN (thread), element);
        }
        element = thread;
      }
    }
    if (gst_element_get_parent (element) == GST_OBJECT (play_base_bin->thread)) {
      GST_LOG ("removing element %s", gst_element_get_name (element));
      gst_bin_remove (GST_BIN (play_base_bin->thread), element);
    }
  } else {
    g_warning ("removing elements is not allowed in NULL");
  }
}

static void
gst_play_base_bin_error (GstElement * element,
    GstElement * _source, GError * error, gchar * debug, gpointer data)
{
  GstObject *source, *parent;

  source = GST_OBJECT (_source);
  parent = GST_OBJECT (data);

  /* tell ourselves */
  gst_object_ref (source);
  gst_object_ref (parent);
  GST_DEBUG ("forwarding error \"%s\" from %s to %s", error->message,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (parent));

  g_signal_emit_by_name (G_OBJECT (parent), "error", source, error, debug);

  GST_DEBUG ("forwarded error \"%s\" from %s to %s", error->message,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (parent));
  gst_object_unref (source);
  gst_object_unref (parent);
}

static void
gst_play_base_bin_found_tag (GstElement * element,
    GstElement * _source, const GstTagList * taglist, gpointer data)
{
  GstObject *source, *parent;

  source = GST_OBJECT (_source);
  parent = GST_OBJECT (data);

  /* tell ourselves */
  gst_object_ref (source);
  gst_object_ref (parent);
  GST_DEBUG ("forwarding taglist %p from %s to %s", taglist,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (parent));

  g_signal_emit_by_name (G_OBJECT (parent), "found-tag", source, taglist);

  GST_DEBUG ("forwarded taglist %p from %s to %s", taglist,
      GST_ELEMENT_NAME (source), GST_OBJECT_NAME (parent));
  gst_object_unref (source);
  gst_object_unref (parent);
}

void
gst_play_base_bin_link_stream (GstPlayBaseBin * play_base_bin,
    GstStreamInfo * info, GstPad * pad)
{
  GST_DEBUG ("link stream");

  if (info == NULL) {
    GList *streams;

    for (streams = play_base_bin->streaminfo; streams;
        streams = g_list_next (streams)) {
      GstStreamInfo *sinfo = (GstStreamInfo *) streams->data;

      if (sinfo->type == GST_STREAM_TYPE_ELEMENT)
        continue;

      if (gst_pad_is_linked (GST_PAD (sinfo->object)))
        continue;

      if (gst_pad_can_link (GST_PAD (sinfo->object), pad)) {
        info = sinfo;
        break;
      }
    }
  }
  if (info) {
    if (!gst_pad_link (GST_PAD (info->object), pad)) {
      GST_DEBUG ("could not link");
      g_object_set (G_OBJECT (info), "mute", TRUE, NULL);
    }
  } else {
    GST_DEBUG ("could not find pad to link");
  }
}

void
gst_play_base_bin_unlink_stream (GstPlayBaseBin * play_base_bin,
    GstStreamInfo * info)
{
  GST_DEBUG ("unlink");
}

const GList *
gst_play_base_bin_get_streaminfo (GstPlayBaseBin * play_base_bin)
{
  return play_base_bin->streaminfo;
}
