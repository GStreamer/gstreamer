/* GStreamer Stream Combiner
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
 *           (C) 2009 Nokia Corporation
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

#include "gststreamcombiner.h"
#include "gst/glib-compat-private.h"

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_stream_combiner_debug);
#define GST_CAT_DEFAULT gst_stream_combiner_debug

G_DEFINE_TYPE (GstStreamCombiner, gst_stream_combiner, GST_TYPE_ELEMENT);

#define STREAMS_LOCK(obj) (g_mutex_lock(obj->lock))
#define STREAMS_UNLOCK(obj) (g_mutex_unlock(obj->lock))

static void gst_stream_combiner_dispose (GObject * object);

static GstPad *gst_stream_combiner_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_stream_combiner_release_pad (GstElement * element,
    GstPad * pad);

static void
gst_stream_combiner_class_init (GstStreamCombinerClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  gobject_klass->dispose = gst_stream_combiner_dispose;

  GST_DEBUG_CATEGORY_INIT (gst_stream_combiner_debug, "streamcombiner", 0,
      "Stream Combiner");

  gst_element_class_add_static_pad_template (gstelement_klass, &src_template);
  gst_element_class_add_static_pad_template (gstelement_klass, &sink_template);

  gstelement_klass->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_stream_combiner_request_new_pad);
  gstelement_klass->release_pad =
      GST_DEBUG_FUNCPTR (gst_stream_combiner_release_pad);

  gst_element_class_set_details_simple (gstelement_klass,
      "streamcombiner", "Generic",
      "Recombines streams splitted by the streamsplitter element",
      "Edward Hervey <edward.hervey@collabora.co.uk>");
}

static void
gst_stream_combiner_dispose (GObject * object)
{
  GstStreamCombiner *stream_combiner = (GstStreamCombiner *) object;

  if (stream_combiner->lock) {
    g_mutex_free (stream_combiner->lock);
    stream_combiner->lock = NULL;
  }

  G_OBJECT_CLASS (gst_stream_combiner_parent_class)->dispose (object);
}

static GstFlowReturn
gst_stream_combiner_chain (GstPad * pad, GstBuffer * buf)
{
  GstStreamCombiner *stream_combiner =
      (GstStreamCombiner *) GST_PAD_PARENT (pad);
  /* FIXME : IMPLEMENT */

  /* with lock taken, check if we're the active stream, if not drop */
  return gst_pad_push (stream_combiner->srcpad, buf);
}

static gboolean
gst_stream_combiner_sink_event (GstPad * pad, GstEvent * event)
{
  GstStreamCombiner *stream_combiner =
      (GstStreamCombiner *) GST_PAD_PARENT (pad);
  /* FIXME : IMPLEMENT */

  GST_DEBUG_OBJECT (pad, "Got event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      if (gst_event_has_name (event, "stream-switching-eos")) {
        gst_event_unref (event);
        event = gst_event_new_eos ();
      }
      break;
    default:
      break;
  }

  /* NEW_SEGMENT : lock, wait for other stream to EOS, select stream, unlock, push */
  /* EOS : lock, mark pad as unused, unlock , drop event */
  /* CUSTOM_REAL_EOS : push EOS downstream */
  /* FLUSH_START : lock, mark as flushing, unlock. if wasn't flushing forward */
  /* FLUSH_STOP : lock, unmark as flushing, unlock, if was flushing forward */
  /* OTHER : if selected pad forward */
  return gst_pad_push_event (stream_combiner->srcpad, event);
}

static GstCaps *
gst_stream_combiner_sink_getcaps (GstPad * pad)
{
  GstStreamCombiner *stream_combiner =
      (GstStreamCombiner *) GST_PAD_PARENT (pad);

  return gst_pad_peer_get_caps_reffed (stream_combiner->srcpad);
}

static gboolean
gst_stream_combiner_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStreamCombiner *stream_combiner =
      (GstStreamCombiner *) GST_PAD_PARENT (pad);
  GstPad *peer;
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (pad, "caps:%" GST_PTR_FORMAT, caps);

  peer = gst_pad_get_peer (stream_combiner->srcpad);
  if (peer) {
    GST_DEBUG_OBJECT (peer, "Setting caps");
    res = gst_pad_set_caps (peer, caps);
    gst_object_unref (peer);
  } else
    GST_WARNING_OBJECT (stream_combiner, "sourcepad has no peer !");
  return res;
}

static gboolean
gst_stream_combiner_src_event (GstPad * pad, GstEvent * event)
{
  GstStreamCombiner *stream_combiner =
      (GstStreamCombiner *) GST_PAD_PARENT (pad);
  GstPad *sinkpad = NULL;

  STREAMS_LOCK (stream_combiner);
  if (stream_combiner->current)
    sinkpad = stream_combiner->current;
  else if (stream_combiner->sinkpads)
    sinkpad = (GstPad *) stream_combiner->sinkpads->data;
  STREAMS_UNLOCK (stream_combiner);

  if (sinkpad)
    /* Forward upstream as is */
    return gst_pad_push_event (sinkpad, event);
  return FALSE;
}

static gboolean
gst_stream_combiner_src_query (GstPad * pad, GstQuery * query)
{
  GstStreamCombiner *stream_combiner =
      (GstStreamCombiner *) GST_PAD_PARENT (pad);

  GstPad *sinkpad = NULL;

  STREAMS_LOCK (stream_combiner);
  if (stream_combiner->current)
    sinkpad = stream_combiner->current;
  else if (stream_combiner->sinkpads)
    sinkpad = (GstPad *) stream_combiner->sinkpads->data;
  STREAMS_UNLOCK (stream_combiner);

  if (sinkpad)
    /* Forward upstream as is */
    return gst_pad_peer_query (sinkpad, query);
  return FALSE;
}

static void
gst_stream_combiner_init (GstStreamCombiner * stream_combiner)
{
  stream_combiner->srcpad =
      gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (stream_combiner->srcpad,
      gst_stream_combiner_src_event);
  gst_pad_set_query_function (stream_combiner->srcpad,
      gst_stream_combiner_src_query);
  gst_element_add_pad (GST_ELEMENT (stream_combiner), stream_combiner->srcpad);

  stream_combiner->lock = g_mutex_new ();
}

static GstPad *
gst_stream_combiner_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstStreamCombiner *stream_combiner = (GstStreamCombiner *) element;
  GstPad *sinkpad;

  GST_DEBUG_OBJECT (element, "templ:%p, name:%s", templ, name);

  sinkpad = gst_pad_new_from_static_template (&sink_template, name);
  /* FIXME : No buffer alloc for the time being, it will resort to the fallback */
  /* gst_pad_set_bufferalloc_function (sinkpad, gst_stream_combiner_buffer_alloc); */
  gst_pad_set_chain_function (sinkpad, gst_stream_combiner_chain);
  gst_pad_set_event_function (sinkpad, gst_stream_combiner_sink_event);
  gst_pad_set_getcaps_function (sinkpad, gst_stream_combiner_sink_getcaps);
  gst_pad_set_setcaps_function (sinkpad, gst_stream_combiner_sink_setcaps);

  STREAMS_LOCK (stream_combiner);
  stream_combiner->sinkpads =
      g_list_append (stream_combiner->sinkpads, sinkpad);
  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (element, sinkpad);
  stream_combiner->cookie++;
  STREAMS_UNLOCK (stream_combiner);

  GST_DEBUG_OBJECT (element, "Returning pad %p", sinkpad);

  return sinkpad;
}

static void
gst_stream_combiner_release_pad (GstElement * element, GstPad * pad)
{
  GstStreamCombiner *stream_combiner = (GstStreamCombiner *) element;
  GList *tmp;

  GST_DEBUG_OBJECT (element, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  STREAMS_LOCK (stream_combiner);
  tmp = g_list_find (stream_combiner->sinkpads, pad);
  if (tmp) {
    GstPad *pad = (GstPad *) tmp->data;

    stream_combiner->sinkpads =
        g_list_delete_link (stream_combiner->sinkpads, tmp);
    stream_combiner->cookie++;

    if (pad == stream_combiner->current) {
      /* Deactivate current flow */
      GST_DEBUG_OBJECT (element, "Removed pad was the current one");
      stream_combiner->current = NULL;
    }
    GST_DEBUG_OBJECT (element, "Removing pad from ourself");
    gst_element_remove_pad (element, pad);
  }
  STREAMS_UNLOCK (stream_combiner);

  return;
}
