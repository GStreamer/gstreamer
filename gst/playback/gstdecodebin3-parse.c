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

#if 0
/* Not needed for now - we're including gstdecodebin3-parse.c into gstdecodebin3.c */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "gstplayback.h"
#endif

/* Streams that come from demuxers (input/upstream) */
/* FIXME : All this is hardcoded. Switch to tree of chains */
struct _DecodebinInputStream
{
  GstDecodebin3 *dbin;
  GstStream *pending_stream;    /* Extra ref */
  GstStream *active_stream;

  DecodebinInput *input;

  GstPad *srcpad;               /* From demuxer */

  /* id of the pad event probe */
  gulong output_event_probe_id;

  /* id of the buffer blocking probe on the input (demuxer src) pad */
  gulong input_buffer_probe_id;

  /* Whether we saw an EOS on input. This should be treated accordingly
   * when the stream is no longer used */
  gboolean saw_eos;
};

static void parsebin_pad_added_cb (GstElement * demux, GstPad * pad,
    DecodebinInput * input);
static void parsebin_pad_removed_cb (GstElement * demux, GstPad * pad,
    DecodebinInput * input);

static gboolean
pending_pads_are_eos (DecodebinInput * input)
{
  GList *tmp;

  for (tmp = input->pending_pads; tmp; tmp = tmp->next) {
    PendingPad *ppad = (PendingPad *) tmp->data;
    if (ppad->saw_eos == FALSE)
      return FALSE;
  }

  return TRUE;
}

static gboolean
all_inputs_are_eos (GstDecodebin3 * dbin)
{
  GList *tmp;
  /* First check input streams */
  for (tmp = dbin->input_streams; tmp; tmp = tmp->next) {
    DecodebinInputStream *input = (DecodebinInputStream *) tmp->data;
    if (input->saw_eos == FALSE)
      return FALSE;
  }

  /* Check pending pads */
  if (!pending_pads_are_eos (dbin->main_input))
    return FALSE;
  for (tmp = dbin->other_inputs; tmp; tmp = tmp->next)
    if (!pending_pads_are_eos ((DecodebinInput *) tmp->data))
      return FALSE;

  GST_DEBUG_OBJECT (dbin, "All streams are EOS");
  return TRUE;
}

static void
check_all_streams_for_eos (GstDecodebin3 * dbin)
{
  GList *tmp;

  if (!all_inputs_are_eos (dbin))
    return;

  /* We know all streams are EOS, properly clean up everything */
  for (tmp = dbin->input_streams; tmp; tmp = tmp->next) {
    DecodebinInputStream *input = (DecodebinInputStream *) tmp->data;
    GstPad *peer = gst_pad_get_peer (input->srcpad);

    /* Send EOS and then remove elements */
    if (peer) {
      gst_pad_send_event (peer, gst_event_new_eos ());
      gst_object_unref (peer);
    }
    GST_FIXME_OBJECT (input->srcpad, "Remove input stream");
  }
}

/* Get the intersection of parser caps and available (sorted) decoders */
static GstCaps *
get_parser_caps_filter (GstDecodebin3 * dbin, GstCaps * caps)
{
  GList *tmp;
  GstCaps *filter_caps = gst_caps_new_empty ();

  g_mutex_lock (&dbin->factories_lock);
  gst_decode_bin_update_factories_list (dbin);
  for (tmp = dbin->decoder_factories; tmp; tmp = tmp->next) {
    GstElementFactory *factory = (GstElementFactory *) tmp->data;
    GstCaps *tcaps, *intersection;
    const GList *tmps;

    GST_LOG ("Trying factory %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
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
  g_mutex_unlock (&dbin->factories_lock);
  GST_DEBUG_OBJECT (dbin, "Got filter caps %" GST_PTR_FORMAT, filter_caps);
  return filter_caps;
}

static gboolean
check_parser_caps_filter (GstDecodebin3 * dbin, GstCaps * caps)
{
  GList *tmp;
  gboolean res = FALSE;

  g_mutex_lock (&dbin->factories_lock);
  gst_decode_bin_update_factories_list (dbin);
  for (tmp = dbin->decoder_factories; tmp; tmp = tmp->next) {
    GstElementFactory *factory = (GstElementFactory *) tmp->data;
    GstCaps *tcaps;
    const GList *tmps;

    GST_LOG ("Trying factory %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
    for (tmps = gst_element_factory_get_static_pad_templates (factory); tmps;
        tmps = tmps->next) {
      GstStaticPadTemplate *st = (GstStaticPadTemplate *) tmps->data;
      if (st->direction != GST_PAD_SINK || st->presence != GST_PAD_ALWAYS)
        continue;
      tcaps = gst_static_pad_template_get_caps (st);
      if (gst_caps_can_intersect (tcaps, caps)) {
        res = TRUE;
        gst_caps_unref (tcaps);
        goto beach;
      }
      gst_caps_unref (tcaps);
    }
  }
beach:
  g_mutex_unlock (&dbin->factories_lock);
  GST_DEBUG_OBJECT (dbin, "Can intersect : %d", res);
  return res;
}

/* Probe on the output of a parser chain (the last
 * src pad) */
static GstPadProbeReturn
parse_chain_output_probe (GstPad * pad, GstPadProbeInfo * info,
    DecodebinInputStream * input)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);

    GST_DEBUG_OBJECT (pad, "Got event %s", GST_EVENT_TYPE_NAME (ev));
    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_STREAM_START:
      {
        GstStream *stream = NULL;
        guint group_id = G_MAXUINT32;

        if (!gst_event_parse_group_id (ev, &group_id)) {
          GST_FIXME_OBJECT (pad,
              "Consider implementing group-id handling on stream-start event");
          group_id = gst_util_group_id_next ();
        }

        GST_DEBUG_OBJECT (pad, "Got stream-start, group_id:%d, input %p",
            group_id, input->input);
        if (set_input_group_id (input->input, &group_id)) {
          ev = gst_event_make_writable (ev);
          gst_event_set_group_id (ev, group_id);
          GST_PAD_PROBE_INFO_DATA (info) = ev;
        }
        input->saw_eos = FALSE;

        gst_event_parse_stream (ev, &stream);
        /* FIXME : Would we ever end up with a stream already set on the input ?? */
        if (stream) {
          if (input->active_stream != stream) {
            MultiQueueSlot *slot;
            if (input->active_stream)
              gst_object_unref (input->active_stream);
            input->active_stream = stream;
            /* We have the beginning of a stream, get a multiqueue slot and link to it */
            g_mutex_lock (&input->dbin->selection_lock);
            slot = get_slot_for_input (input->dbin, input);
            link_input_to_slot (input, slot);
            g_mutex_unlock (&input->dbin->selection_lock);
          } else
            gst_object_unref (stream);
        }
      }
        break;
      case GST_EVENT_CAPS:
      {
        GstCaps *caps = NULL;
        gst_event_parse_caps (ev, &caps);
        GST_DEBUG_OBJECT (pad, "caps %" GST_PTR_FORMAT, caps);
        if (caps && input->active_stream)
          gst_stream_set_caps (input->active_stream, caps);
      }
        break;
      case GST_EVENT_EOS:
        input->saw_eos = TRUE;
        if (all_inputs_are_eos (input->dbin)) {
          GST_DEBUG_OBJECT (pad, "real input pad, marking as EOS");
          check_all_streams_for_eos (input->dbin);
        } else {
          GstPad *peer = gst_pad_get_peer (input->srcpad);
          if (peer) {
            /* Send custom-eos event to multiqueue slot */
            GstStructure *s;
            GstEvent *event;

            GST_DEBUG_OBJECT (pad,
                "Got EOS end of input stream, post custom-eos");
            s = gst_structure_new_empty ("decodebin3-custom-eos");
            event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);
            gst_pad_send_event (peer, event);
            gst_object_unref (peer);
          } else {
            GST_FIXME_OBJECT (pad, "No peer, what should we do ?");
          }
        }
        ret = GST_PAD_PROBE_DROP;
        break;
      case GST_EVENT_FLUSH_STOP:
        GST_DEBUG_OBJECT (pad, "Clear saw_eos flag");
        input->saw_eos = FALSE;
      default:
        break;
    }
  } else if (GST_IS_QUERY (GST_PAD_PROBE_INFO_DATA (info))) {
    GstQuery *q = GST_PAD_PROBE_INFO_QUERY (info);
    GST_DEBUG_OBJECT (pad, "Seeing query %s", GST_QUERY_TYPE_NAME (q));
    /* If we have a parser, we want to reply to the caps query */
    /* FIXME: Set a flag when the input stream is created for
     * streams where we shouldn't reply to these queries */
    if (GST_QUERY_TYPE (q) == GST_QUERY_CAPS
        && (info->type & GST_PAD_PROBE_TYPE_PULL)) {
      GstCaps *filter = NULL;
      GstCaps *allowed;
      gst_query_parse_caps (q, &filter);
      allowed = get_parser_caps_filter (input->dbin, filter);
      GST_DEBUG_OBJECT (pad,
          "Intercepting caps query, setting %" GST_PTR_FORMAT, allowed);
      gst_query_set_caps_result (q, allowed);
      gst_caps_unref (allowed);
      ret = GST_PAD_PROBE_HANDLED;
    } else if (GST_QUERY_TYPE (q) == GST_QUERY_ACCEPT_CAPS) {
      GstCaps *prop = NULL;
      gst_query_parse_accept_caps (q, &prop);
      /* Fast check against target caps */
      if (gst_caps_can_intersect (prop, input->dbin->caps))
        gst_query_set_accept_caps_result (q, TRUE);
      else {
        gboolean accepted = check_parser_caps_filter (input->dbin, prop);
        /* check against caps filter */
        gst_query_set_accept_caps_result (q, accepted);
        GST_DEBUG_OBJECT (pad, "ACCEPT_CAPS query, returning %d", accepted);
      }
      ret = GST_PAD_PROBE_HANDLED;
    }
  }

  return ret;
}

static DecodebinInputStream *
create_input_stream (GstDecodebin3 * dbin, GstStream * stream, GstPad * pad,
    DecodebinInput * input)
{
  DecodebinInputStream *res = g_new0 (DecodebinInputStream, 1);

  GST_DEBUG_OBJECT (pad, "Creating input stream for stream %p %s (input:%p)",
      stream, gst_stream_get_stream_id (stream), input);

  res->dbin = dbin;
  res->input = input;
  res->pending_stream = gst_object_ref (stream);
  res->srcpad = pad;

  /* Put probe on output source pad (for detecting EOS/STREAM_START/FLUSH) */
  res->output_event_probe_id =
      gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM
      | GST_PAD_PROBE_TYPE_EVENT_FLUSH,
      (GstPadProbeCallback) parse_chain_output_probe, res, NULL);

  /* Add to list of current input streams */
  dbin->input_streams = g_list_append (dbin->input_streams, res);
  GST_DEBUG_OBJECT (pad, "Done creating input stream");

  return res;
}

static void
remove_input_stream (GstDecodebin3 * dbin, DecodebinInputStream * stream)
{
  MultiQueueSlot *slot;

  GST_DEBUG_OBJECT (dbin, "Removing input stream %p (%s)", stream,
      stream->active_stream ? gst_stream_get_stream_id (stream->active_stream) :
      "<NONE>");

  /* Unlink from slot */
  if (stream->srcpad) {
    GstPad *peer;
    peer = gst_pad_get_peer (stream->srcpad);
    if (peer) {
      gst_pad_unlink (stream->srcpad, peer);
      gst_object_unref (peer);
    }
  }

  g_mutex_lock (&dbin->selection_lock);
  slot = get_slot_for_input (dbin, stream);
  g_mutex_unlock (&dbin->selection_lock);
  if (slot) {
    slot->pending_stream = NULL;
    slot->input = NULL;
    GST_DEBUG_OBJECT (dbin, "slot %p cleared", slot);
  }

  if (stream->active_stream)
    gst_object_unref (stream->active_stream);
  if (stream->pending_stream)
    gst_object_unref (stream->pending_stream);

  dbin->input_streams = g_list_remove (dbin->input_streams, stream);

  g_free (stream);
}


/* FIXME : HACK, REMOVE, USE INPUT CHAINS */
static GstPadProbeReturn
parsebin_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    DecodebinInput * input)
{
  GstDecodebin3 *dbin = input->dbin;
  GList *tmp, *unused_slot = NULL;

  GST_FIXME_OBJECT (dbin, "Need a lock !");

  GST_DEBUG_OBJECT (pad, "Got a buffer ! UNBLOCK !");

  /* Any data out the demuxer means it's not creating pads
   * any more right now */

  /* 1. Re-use existing streams if/when possible */
  GST_FIXME_OBJECT (dbin, "Re-use existing input streams if/when possible");

  /* 2. Remove unused streams (push EOS) */
  GST_DEBUG_OBJECT (dbin, "Removing unused streams");
  tmp = dbin->input_streams;
  while (tmp != NULL) {
    DecodebinInputStream *input_stream = (DecodebinInputStream *) tmp->data;
    GList *next = tmp->next;

    GST_DEBUG_OBJECT (dbin, "Checking input stream %p", input_stream);
    if (input_stream->input_buffer_probe_id) {
      GST_DEBUG_OBJECT (dbin,
          "Removing pad block on input %p pad %" GST_PTR_FORMAT, input_stream,
          input_stream->srcpad);
      gst_pad_remove_probe (input_stream->srcpad,
          input_stream->input_buffer_probe_id);
    }
    input_stream->input_buffer_probe_id = 0;

    if (input_stream->saw_eos) {
      remove_input_stream (dbin, input_stream);
      tmp = dbin->input_streams;
    } else
      tmp = next;
  }

  GST_DEBUG_OBJECT (dbin, "Creating new streams (if needed)");
  /* 3. Create new streams */
  for (tmp = input->pending_pads; tmp; tmp = tmp->next) {
    GstStream *stream;
    PendingPad *ppad = (PendingPad *) tmp->data;

    stream = gst_pad_get_stream (ppad->pad);
    if (stream == NULL) {
      GST_ERROR_OBJECT (dbin, "No stream for pad ????");
    } else {
      MultiQueueSlot *slot;
      DecodebinInputStream *input_stream;
      /* The remaining pads in pending_pads are the ones that require a new
       * input stream */
      input_stream = create_input_stream (dbin, stream, ppad->pad, ppad->input);
      /* See if we can link it straight away */
      input_stream->active_stream = stream;

      g_mutex_lock (&dbin->selection_lock);
      slot = get_slot_for_input (dbin, input_stream);
      link_input_to_slot (input_stream, slot);
      g_mutex_unlock (&dbin->selection_lock);

      /* Remove the buffer and event probe */
      gst_pad_remove_probe (ppad->pad, ppad->buffer_probe);
      gst_pad_remove_probe (ppad->pad, ppad->event_probe);
      g_free (ppad);
    }
  }

  g_list_free (input->pending_pads);
  input->pending_pads = NULL;

  /* Weed out unused multiqueue slots */
  g_mutex_lock (&dbin->selection_lock);
  for (tmp = dbin->slots; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    GST_LOG_OBJECT (dbin, "Slot %d input:%p", slot->id, slot->input);
    if (slot->input == NULL) {
      unused_slot =
          g_list_append (unused_slot, gst_object_ref (slot->sink_pad));
    }
  }
  g_mutex_unlock (&dbin->selection_lock);

  for (tmp = unused_slot; tmp; tmp = tmp->next) {
    GstPad *sink_pad = (GstPad *) tmp->data;
    GST_DEBUG_OBJECT (sink_pad, "Sending EOS to unused slot");
    gst_pad_send_event (sink_pad, gst_event_new_eos ());
  }

  if (unused_slot)
    g_list_free_full (unused_slot, (GDestroyNotify) gst_object_unref);

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
parsebin_pending_event_probe (GstPad * pad, GstPadProbeInfo * info,
    PendingPad * ppad)
{
  GstDecodebin3 *dbin = ppad->dbin;
  /* We drop all events by default */
  GstPadProbeReturn ret = GST_PAD_PROBE_DROP;
  GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);

  GST_DEBUG_OBJECT (pad, "Got event %p %s", ev, GST_EVENT_TYPE_NAME (ev));
  switch (GST_EVENT_TYPE (ev)) {
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (pad, "Pending pad marked as EOS, removing");
      ppad->input->pending_pads =
          g_list_remove (ppad->input->pending_pads, ppad);
      gst_pad_remove_probe (ppad->pad, ppad->buffer_probe);
      gst_pad_remove_probe (ppad->pad, ppad->event_probe);
      g_free (ppad);

      check_all_streams_for_eos (dbin);
    }
      break;
    default:
      break;
  }

  return ret;
}

static void
parsebin_pad_added_cb (GstElement * demux, GstPad * pad, DecodebinInput * input)
{
  GstDecodebin3 *dbin = input->dbin;
  PendingPad *ppad;
  GList *tmp;

  GST_DEBUG_OBJECT (dbin, "New pad %s:%s (input:%p)", GST_DEBUG_PAD_NAME (pad),
      input);

  ppad = g_new0 (PendingPad, 1);
  ppad->dbin = dbin;
  ppad->input = input;
  ppad->pad = pad;

  ppad->event_probe =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) parsebin_pending_event_probe, ppad, NULL);
  ppad->buffer_probe =
      gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) parsebin_buffer_probe, input, NULL);

  input->pending_pads = g_list_append (input->pending_pads, ppad);

  /* FIXME : ONLY DO FOR THIS PARSEBIN/INPUT ! */
  /* Check if all existing input streams have a buffer probe set */
  for (tmp = dbin->input_streams; tmp; tmp = tmp->next) {
    DecodebinInputStream *input_stream = (DecodebinInputStream *) tmp->data;
    if (input_stream->input_buffer_probe_id == 0) {
      GST_DEBUG_OBJECT (input_stream->srcpad, "Adding blocking buffer probe");
      input_stream->input_buffer_probe_id =
          gst_pad_add_probe (input_stream->srcpad,
          GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
          (GstPadProbeCallback) parsebin_buffer_probe, input_stream->input,
          NULL);
    }
  }
}

static void
parsebin_pad_removed_cb (GstElement * demux, GstPad * pad, DecodebinInput * inp)
{
  GstDecodebin3 *dbin = inp->dbin;
  DecodebinInputStream *input = NULL;
  GList *tmp;
  GST_DEBUG_OBJECT (pad, "removed");

  for (tmp = dbin->input_streams; tmp; tmp = tmp->next) {
    DecodebinInputStream *cand = (DecodebinInputStream *) tmp->data;
    if (cand->srcpad == pad)
      input = cand;
  }
  /* If there are no pending pads, this means we will definitely not need this
   * stream anymore */
  if (input) {
    GST_DEBUG_OBJECT (pad, "stream %p", input);
    if (inp->pending_pads == NULL) {
      MultiQueueSlot *slot;

      GST_DEBUG_OBJECT (pad, "Remove input stream %p", input);

      SELECTION_LOCK (dbin);
      slot = get_slot_for_input (dbin, input);
      SELECTION_UNLOCK (dbin);

      remove_input_stream (dbin, input);

      SELECTION_LOCK (dbin);
      if (slot && g_list_find (dbin->slots, slot) && slot->is_drained) {
        /* if slot is still there and already drained, remove it in here */
        if (slot->output) {
          DecodebinOutputStream *output = slot->output;
          GST_DEBUG_OBJECT (pad,
              "Multiqueue was drained, Remove output stream");

          dbin->output_streams = g_list_remove (dbin->output_streams, output);
          free_output_stream (dbin, output);
        }
        GST_DEBUG_OBJECT (pad, "No pending pad, Remove multiqueue slot");
        if (slot->probe_id)
          gst_pad_remove_probe (slot->src_pad, slot->probe_id);
        slot->probe_id = 0;
        dbin->slots = g_list_remove (dbin->slots, slot);
        free_multiqueue_slot_async (dbin, slot);
      }
      SELECTION_UNLOCK (dbin);
    } else {
      input->srcpad = NULL;
      if (input->input_buffer_probe_id)
        gst_pad_remove_probe (pad, input->input_buffer_probe_id);
      input->input_buffer_probe_id = 0;
    }
  }
}
