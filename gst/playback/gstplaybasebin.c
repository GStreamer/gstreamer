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

GST_DEBUG_CATEGORY_STATIC (gst_play_base_bin_debug);
#define GST_CAT_DEFAULT gst_play_base_bin_debug

/* props */
enum
{
  ARG_0,
  ARG_URI,
  ARG_THREADED,
  ARG_NSTREAMS,
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

  parent_class = g_type_class_ref (gst_bin_get_type ());

  gobject_klass->set_property = gst_play_base_bin_set_property;
  gobject_klass->get_property = gst_play_base_bin_get_property;

  g_object_class_install_property (gobject_klass, ARG_URI,
      g_param_spec_string ("uri", "URI", "URI of the media to play",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, ARG_NSTREAMS,
      g_param_spec_int ("nstreams", "NStreams", "number of streams",
          0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_klass, ARG_STREAMINFO,
      g_param_spec_pointer ("stream-info", "Stream info", "List of streaminfo",
          G_PARAM_READABLE));

  gst_play_base_bin_signals[MUTE_STREAM_SIGNAL] =
      g_signal_new ("mute-stream", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, mute_stream),
      NULL, NULL, gst_marshal_VOID__OBJECT_POINTER, G_TYPE_NONE, 2,
      G_TYPE_OBJECT, G_TYPE_BOOLEAN);
  gst_play_base_bin_signals[LINK_STREAM_SIGNAL] =
      g_signal_new ("link-stream", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPlayBaseBinClass, link_stream),
      NULL, NULL, gst_marshal_VOID__OBJECT_POINTER, G_TYPE_NONE, 2,
      G_TYPE_OBJECT, GST_TYPE_PAD);
  gst_play_base_bin_signals[UNLINK_STREAM_SIGNAL] =
      g_signal_new ("unlink-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstPlayBaseBinClass, unlink_stream),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_play_base_bin_dispose);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_change_state);

  gstbin_klass->add_element = GST_DEBUG_FUNCPTR (gst_play_base_bin_add_element);
  gstbin_klass->remove_element =
      GST_DEBUG_FUNCPTR (gst_play_base_bin_remove_element);

  klass->mute_stream = gst_play_base_bin_mute_stream;
  klass->link_stream = gst_play_base_bin_link_stream;
  klass->unlink_stream = gst_play_base_bin_unlink_stream;
}

static void
gst_play_base_bin_init (GstPlayBaseBin * play_base_bin)
{
  play_base_bin->uri = NULL;
  play_base_bin->threaded = FALSE;

  play_base_bin->preroll_lock = g_mutex_new ();
  play_base_bin->preroll_cond = g_cond_new ();

  GST_FLAG_SET (play_base_bin, GST_BIN_SELF_SCHEDULABLE);
}

static void
gst_play_base_bin_dispose (GObject * object)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (object);
  g_free (play_base_bin->uri);

  if (G_OBJECT_CLASS (parent_class)->dispose) {
    G_OBJECT_CLASS (parent_class)->dispose (object);
  }
}

static void
rebuild_pipeline (GstPlayBaseBin * play_base_bin)
{
  GstElementState oldstate;

  if (play_base_bin->thread == NULL)
    return;

  oldstate = gst_element_get_state (play_base_bin->thread);

  gst_element_set_state (play_base_bin->thread, GST_STATE_NULL);
  /* remove old elements */

  /* set to old state again */
  gst_element_set_state (play_base_bin->thread, oldstate);
}

static void
queue_overrun (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  g_mutex_lock (play_base_bin->preroll_lock);
  g_cond_signal (play_base_bin->preroll_cond);
  g_mutex_unlock (play_base_bin->preroll_lock);
  //g_signal_handlers_disconnect_by_func(G_OBJECT (element),
//                G_CALLBACK (queue_overrun), play_base_bin);
}

static GstElement *
gen_preroll_element (GstPlayBaseBin * play_base_bin, GstPad * pad)
{
  GstElement *element;
  gchar *name;

  name = g_strdup_printf ("preroll_%s", gst_pad_get_name (pad));
  element = gst_element_factory_make ("queue", name);
  g_signal_connect (G_OBJECT (element), "overrun",
      G_CALLBACK (queue_overrun), play_base_bin);
  g_free (name);

  return element;
}

static void
no_more_pads (GstElement * element, GstPlayBaseBin * play_base_bin)
{
  g_mutex_lock (play_base_bin->preroll_lock);
  g_cond_signal (play_base_bin->preroll_cond);
  g_mutex_unlock (play_base_bin->preroll_lock);
}

static void
new_stream (GstElement * element, GstPad * pad, gboolean last,
    GstPlayBaseBin * play_base_bin)
{
  GstStructure *structure;
  const gchar *mimetype;
  GstCaps *caps;
  GstElement *new_element = NULL;
  GstStreamInfo *info;
  GstStreamType type;
  GstPad *srcpad;

  caps = gst_pad_get_caps (pad);

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  play_base_bin->nstreams++;

  if (g_str_has_prefix (mimetype, "audio/")) {
    type = GST_STREAM_TYPE_AUDIO;
  } else if (g_str_has_prefix (mimetype, "video/")) {
    type = GST_STREAM_TYPE_VIDEO;
  } else {
    type = GST_STREAM_TYPE_UNKNOWN;
  }

  if (last) {
    srcpad = pad;
    no_more_pads (NULL, play_base_bin);
  } else {
    new_element = gen_preroll_element (play_base_bin, pad);
    srcpad = gst_element_get_pad (new_element, "src");
    gst_bin_add (GST_BIN (play_base_bin->thread), new_element);
    play_base_bin->threaded = TRUE;

    gst_pad_link (pad, gst_element_get_pad (new_element, "sink"));
    gst_element_sync_state_with_parent (new_element);
  }

  info = gst_stream_info_new (srcpad, type, NULL);

  play_base_bin->streaminfo = g_list_append (play_base_bin->streaminfo, info);
}

static gboolean
setup_source (GstPlayBaseBin * play_base_bin)
{
  if (play_base_bin->source) {
    gst_bin_remove (GST_BIN (play_base_bin->thread), play_base_bin->source);
    gst_object_unref (GST_OBJECT (play_base_bin->source));
  }

  play_base_bin->source =
      gst_element_make_from_uri (GST_URI_SRC, play_base_bin->uri, "source");
  if (!play_base_bin->source) {
    g_warning ("don't know how to read %s", play_base_bin->uri);
    return FALSE;
  }

  {
    GstElement *decoder;
    gboolean res;

    gst_bin_add (GST_BIN (play_base_bin->thread), play_base_bin->source);

    decoder = gst_element_factory_make ("decodebin", "decoder");
    if (!decoder) {
      g_warning ("can't find decoder element");
      return FALSE;
    }


    gst_bin_add (GST_BIN (play_base_bin->thread), decoder);
    res = gst_element_link_pads (play_base_bin->source, "src", decoder, "sink");
    if (!res) {
      g_warning ("can't link source to typefind element");
      return FALSE;
    }
    g_signal_connect (G_OBJECT (decoder), "new_stream",
        G_CALLBACK (new_stream), play_base_bin);
    g_signal_connect (G_OBJECT (decoder), "no-more-pads",
        G_CALLBACK (no_more_pads), play_base_bin);

    g_mutex_lock (play_base_bin->preroll_lock);
    gst_element_set_state (play_base_bin->thread, GST_STATE_PLAYING);
    g_cond_wait (play_base_bin->preroll_cond, play_base_bin->preroll_lock);
    g_mutex_unlock (play_base_bin->preroll_lock);
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
      if (!play_base_bin->uri || !strcmp (play_base_bin->uri, uri)) {
        g_free (play_base_bin->uri);
        play_base_bin->uri = g_strdup (uri);

        rebuild_pipeline (play_base_bin);
      }
      break;
    }
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
    case ARG_STREAMINFO:
      g_value_set_pointer (value, play_base_bin->streaminfo);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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

        gst_object_set_parent (GST_OBJECT (play_base_bin->thread),
            GST_OBJECT (play_base_bin));

        gst_element_set_state (play_base_bin->thread, GST_STATE_READY);
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
        GST_ELEMENT_ERROR (GST_ELEMENT (play_base_bin), LIBRARY, TOO_LAZY,
            (NULL), ("cannot handle uri \"%s\"", play_base_bin->uri));
        ret = GST_STATE_FAILURE;
      } else {
        ret = gst_element_set_state (play_base_bin->thread, GST_STATE_PAUSED);
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
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_READY);
      break;
    case GST_STATE_READY_TO_NULL:
      ret = gst_element_set_state (play_base_bin->thread, GST_STATE_NULL);

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

    sched = gst_element_get_scheduler (GST_ELEMENT (play_base_bin->thread));
    clock = gst_scheduler_get_clock (sched);
    gst_scheduler_set_clock (sched, clock);

    gst_element_sync_state_with_parent (element);
  } else {
    g_warning ("adding elements is not allowed in NULL");
  }
}

static void
gst_play_base_bin_remove_element (GstBin * bin, GstElement * element)
{
  GstPlayBaseBin *play_base_bin;

  play_base_bin = GST_PLAY_BASE_BIN (bin);

  if (play_base_bin->thread) {
    gst_bin_remove (GST_BIN (play_base_bin->thread), element);
  } else {
    g_warning ("removing elements is not allowed in NULL");
  }
}

void
gst_play_base_bin_mute_stream (GstPlayBaseBin * play_base_bin,
    GstStreamInfo * info, gboolean mute)
{
  g_print ("mute\n");
}

void
gst_play_base_bin_link_stream (GstPlayBaseBin * play_base_bin,
    GstStreamInfo * info, GstPad * pad)
{
  if (info == NULL) {
    GList *streams;

    for (streams = play_base_bin->streaminfo; streams;
        streams = g_list_next (streams)) {
      GstStreamInfo *sinfo = (GstStreamInfo *) streams->data;

      if (gst_pad_is_linked (sinfo->pad))
        continue;

      if (gst_pad_can_link (sinfo->pad, pad)) {
        info = sinfo;
        break;
      }
    }
  }
  if (info) {
    if (!gst_pad_link (info->pad, pad)) {
      g_print ("could not link\n");
    }
  } else {
    g_print ("could not find pad to link\n");
  }
}

void
gst_play_base_bin_unlink_stream (GstPlayBaseBin * play_base_bin,
    GstStreamInfo * info)
{
  g_print ("unlink\n");
}

const GList *
gst_play_base_bin_get_streaminfo (GstPlayBaseBin * play_base_bin)
{
  return play_base_bin->streaminfo;
}
