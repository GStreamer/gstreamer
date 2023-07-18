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

#define CUSTOM_EOS_QUARK _custom_eos_quark_get ()
#define CUSTOM_EOS_QUARK_DATA "custom-eos"
static GQuark
_custom_eos_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("decodebin3-custom-eos");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

/* Streams that come from parsebin or identity */
/* FIXME : All this is hardcoded. Switch to tree of chains */
struct _DecodebinInputStream
{
  GstDecodebin3 *dbin;

  GstStream *active_stream;

  DecodebinInput *input;

  GstPad *srcpad;               /* From parsebin or identity */

  /* id of the pad event probe */
  gulong output_event_probe_id;

  /* id of the buffer blocking probe on the parsebin srcpad pad */
  gulong buffer_probe_id;

  /* Whether we saw an EOS on input. This should be treated accordingly
   * when the stream is no longer used */
  gboolean saw_eos;
};

static void unblock_pending_input (DecodebinInput * input,
    gboolean unblock_other_inputs);
static void parsebin_pad_added_cb (GstElement * demux, GstPad * pad,
    DecodebinInput * input);
static void parsebin_pad_removed_cb (GstElement * demux, GstPad * pad,
    DecodebinInput * input);

/* WITH SELECTION_LOCK TAKEN! */
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

  GST_DEBUG_OBJECT (dbin, "All input streams are EOS");
  return TRUE;
}

/* WITH SELECTION_LOCK TAKEN! */
static void
check_all_streams_for_eos (GstDecodebin3 * dbin, GstEvent * event)
{
  GList *tmp;
  GList *outputpads = NULL;

  if (!all_inputs_are_eos (dbin))
    return;

  /* We know all streams are EOS, properly clean up everything */

  /* We grab all peer pads *while* the selection lock is taken and then we will
     push EOS downstream with the selection lock released */
  for (tmp = dbin->input_streams; tmp; tmp = tmp->next) {
    DecodebinInputStream *input = (DecodebinInputStream *) tmp->data;
    GstPad *peer = gst_pad_get_peer (input->srcpad);

    /* Keep a reference to the peer pad */
    if (peer)
      outputpads = g_list_append (outputpads, peer);
  }

  SELECTION_UNLOCK (dbin);

  for (tmp = outputpads; tmp; tmp = tmp->next) {
    GstPad *peer = (GstPad *) tmp->data;

    /* Send EOS and then remove elements */
    gst_pad_send_event (peer, gst_event_ref (event));
    GST_FIXME_OBJECT (peer, "Remove input stream");
    gst_object_unref (peer);
  }
  SELECTION_LOCK (dbin);

  g_list_free (outputpads);
}

/* Get the intersection of parser caps and available (sorted) decoders */
static GstCaps *
get_parser_caps_filter (GstDecodebin3 * dbin, GstCaps * caps)
{
  GList *tmp;
  GstCaps *filter_caps;

  /* If no filter was provided, it can handle anything */
  if (!caps || gst_caps_is_any (caps))
    return gst_caps_new_any ();

  filter_caps = gst_caps_new_empty ();

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
  GstCaps *default_raw = gst_static_caps_get (&default_raw_caps);

  if (gst_caps_can_intersect (caps, default_raw)) {
    GST_INFO_OBJECT (dbin, "Dealing with raw stream from the demuxer, "
        " we can handle them even if we won't expose then");
    gst_caps_unref (default_raw);

    return TRUE;
  }
  gst_caps_unref (default_raw);

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
  GST_DEBUG_OBJECT (dbin, "Can intersect %" GST_PTR_FORMAT ": %d", caps, res);
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
        guint group_id = GST_GROUP_ID_INVALID;

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
            SELECTION_LOCK (input->dbin);
            slot = get_slot_for_input (input->dbin, input);
            link_input_to_slot (input, slot);
            SELECTION_UNLOCK (input->dbin);
          } else
            gst_object_unref (stream);
        }
      }
        break;
      case GST_EVENT_GAP:
      {
        /* If we are still waiting to be unblocked and we get a gap, unblock */
        if (input->buffer_probe_id) {
          GST_DEBUG_OBJECT (pad, "Got a gap event! Unblocking input(s) !");
          unblock_pending_input (input->input, TRUE);
        }
        break;
      }
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
          SELECTION_LOCK (input->dbin);
          check_all_streams_for_eos (input->dbin, ev);
          SELECTION_UNLOCK (input->dbin);
        } else {
          GstPad *peer = gst_pad_get_peer (input->srcpad);
          if (peer) {
            /* Send custom-eos event to multiqueue slot */
            GstEvent *event;

            GST_DEBUG_OBJECT (pad,
                "Got EOS end of input stream, post custom-eos");
            event = gst_event_new_eos ();
            gst_event_set_seqnum (event, gst_event_get_seqnum (ev));
            gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (event),
                CUSTOM_EOS_QUARK, (gchar *) CUSTOM_EOS_QUARK_DATA, NULL);
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
    if (input->input && input->input->identity) {
      GST_DEBUG_OBJECT (pad, "Letting query through");
    } else {
      GstQuery *q = GST_PAD_PROBE_INFO_QUERY (info);
      GST_DEBUG_OBJECT (pad, "Seeing query %" GST_PTR_FORMAT, q);
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
        if (gst_caps_can_intersect (prop, input->dbin->caps)) {
          gst_query_set_accept_caps_result (q, TRUE);
        } else {
          gboolean accepted = check_parser_caps_filter (input->dbin, prop);
          /* check against caps filter */
          gst_query_set_accept_caps_result (q, accepted);
          GST_DEBUG_OBJECT (pad, "ACCEPT_CAPS query, returning %d", accepted);
        }
        ret = GST_PAD_PROBE_HANDLED;
      }
    }
  }

  return ret;
}

static GstPadProbeReturn
parsebin_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    DecodebinInput * input)
{
  /* We have at least one buffer pending, unblock parsebin(s) */
  GST_DEBUG_OBJECT (pad, "Got a buffer ! unblocking");
  unblock_pending_input (input, TRUE);

  return GST_PAD_PROBE_OK;
}

/* Call with selection lock */
static DecodebinInputStream *
create_input_stream (GstDecodebin3 * dbin, GstPad * pad, DecodebinInput * input)
{
  DecodebinInputStream *res = g_new0 (DecodebinInputStream, 1);

  GST_DEBUG_OBJECT (dbin, "Creating input stream for %" GST_PTR_FORMAT, pad);

  res->dbin = dbin;
  res->input = input;
  res->srcpad = gst_object_ref (pad);

  /* Put probe on output source pad (for detecting EOS/STREAM_START/FLUSH) */
  res->output_event_probe_id =
      gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM
      | GST_PAD_PROBE_TYPE_EVENT_FLUSH,
      (GstPadProbeCallback) parse_chain_output_probe, res, NULL);

  /* Install a blocking buffer probe */
  res->buffer_probe_id =
      gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) parsebin_buffer_probe, input, NULL);

  /* Add to list of current input streams */
  dbin->input_streams = g_list_append (dbin->input_streams, res);
  GST_DEBUG_OBJECT (pad, "Done creating input stream");

  return res;
}

/* WITH SELECTION_LOCK TAKEN! */
static void
remove_input_stream (GstDecodebin3 * dbin, DecodebinInputStream * stream)
{
  MultiQueueSlot *slot;

  GST_DEBUG_OBJECT (dbin, "Removing input stream %p (%s)", stream,
      stream->active_stream ? gst_stream_get_stream_id (stream->active_stream) :
      "<NONE>");

  gst_object_replace ((GstObject **) & stream->active_stream, NULL);

  /* Unlink from slot */
  if (stream->srcpad) {
    GstPad *peer;
    peer = gst_pad_get_peer (stream->srcpad);
    if (peer) {
      gst_pad_unlink (stream->srcpad, peer);
      gst_object_unref (peer);
    }
    if (stream->buffer_probe_id)
      gst_pad_remove_probe (stream->srcpad, stream->buffer_probe_id);
    gst_object_unref (stream->srcpad);
  }

  slot = get_slot_for_input (dbin, stream);
  if (slot) {
    slot->pending_stream = NULL;
    slot->input = NULL;
    GST_DEBUG_OBJECT (dbin, "slot %p cleared", slot);
  }

  dbin->input_streams = g_list_remove (dbin->input_streams, stream);

  g_free (stream);
}

static void
unblock_pending_input (DecodebinInput * input, gboolean unblock_other_inputs)
{
  GstDecodebin3 *dbin = input->dbin;
  GList *tmp, *unused_slot = NULL;

  GST_DEBUG_OBJECT (dbin,
      "DecodebinInput for %" GST_PTR_FORMAT " , unblock_other_inputs:%d",
      input->parsebin, unblock_other_inputs);

  /* Re-use existing streams if/when possible */
  GST_FIXME_OBJECT (dbin, "Re-use existing input streams if/when possible");

  /* Unblock all input streams and link to a slot if needed */
  SELECTION_LOCK (dbin);
  tmp = dbin->input_streams;
  while (tmp != NULL) {
    DecodebinInputStream *input_stream = (DecodebinInputStream *) tmp->data;
    GList *next = tmp->next;
    MultiQueueSlot *slot;

    if (input_stream->input != input) {
      tmp = next;
      continue;
    }

    GST_DEBUG_OBJECT (dbin, "Checking input stream %p", input_stream);

    if (!input_stream->active_stream)
      input_stream->active_stream = gst_pad_get_stream (input_stream->srcpad);

    /* Ensure the stream has an associated slot */
    slot = get_slot_for_input (dbin, input_stream);
    if (slot->input != input_stream)
      link_input_to_slot (input_stream, slot);

    if (input_stream->buffer_probe_id) {
      GST_DEBUG_OBJECT (dbin,
          "Removing pad block on input %p pad %" GST_PTR_FORMAT, input_stream,
          input_stream->srcpad);
      gst_pad_remove_probe (input_stream->srcpad,
          input_stream->buffer_probe_id);
      input_stream->buffer_probe_id = 0;
    }

    if (input_stream->saw_eos) {
      GST_DEBUG_OBJECT (dbin, "Removing EOS'd stream");
      remove_input_stream (dbin, input_stream);
      tmp = dbin->input_streams;
    } else
      tmp = next;
  }

  /* Weed out unused multiqueue slots */
  for (tmp = dbin->slots; tmp; tmp = tmp->next) {
    MultiQueueSlot *slot = (MultiQueueSlot *) tmp->data;
    GST_LOG_OBJECT (dbin, "Slot %d input:%p", slot->id, slot->input);
    if (slot->input == NULL) {
      unused_slot =
          g_list_append (unused_slot, gst_object_ref (slot->sink_pad));
    }
  }
  SELECTION_UNLOCK (dbin);

  if (unused_slot) {
    for (tmp = unused_slot; tmp; tmp = tmp->next) {
      GstPad *sink_pad = (GstPad *) tmp->data;
      GST_DEBUG_OBJECT (sink_pad, "Sending EOS to unused slot");
      gst_pad_send_event (sink_pad, gst_event_new_eos ());
    }
    g_list_free_full (unused_slot, (GDestroyNotify) gst_object_unref);
  }

  if (unblock_other_inputs) {
    GList *tmp;
    /* If requrested, unblock inputs which are targetting the same collection */
    if (dbin->main_input != input) {
      if (dbin->main_input->collection == input->collection) {
        GST_DEBUG_OBJECT (dbin, "Unblock main input");
        unblock_pending_input (dbin->main_input, FALSE);
      }
    }
    for (tmp = dbin->other_inputs; tmp; tmp = tmp->next) {
      DecodebinInput *other = tmp->data;
      if (other->collection == input->collection) {
        GST_DEBUG_OBJECT (dbin, "Unblock other input");
        unblock_pending_input (other, FALSE);
      }
    }
  }
}

static void
parsebin_pad_added_cb (GstElement * demux, GstPad * pad, DecodebinInput * input)
{
  GstDecodebin3 *dbin = input->dbin;

  GST_DEBUG_OBJECT (dbin, "New pad %s:%s (input:%p)", GST_DEBUG_PAD_NAME (pad),
      input);

  SELECTION_LOCK (dbin);
  create_input_stream (dbin, pad, input);
  SELECTION_UNLOCK (dbin);
}

static DecodebinInputStream *
find_input_stream_for_pad (GstDecodebin3 * dbin, GstPad * pad)
{
  GList *tmp;

  for (tmp = dbin->input_streams; tmp; tmp = tmp->next) {
    DecodebinInputStream *cand = (DecodebinInputStream *) tmp->data;
    if (cand->srcpad == pad)
      return cand;
  }
  return NULL;
}

static void
parsebin_pad_removed_cb (GstElement * demux, GstPad * pad, DecodebinInput * inp)
{
  GstDecodebin3 *dbin = inp->dbin;
  DecodebinInputStream *input = NULL;
  MultiQueueSlot *slot;

  if (!GST_PAD_IS_SRC (pad))
    return;

  GST_DEBUG_OBJECT (pad, "removed");
  input = find_input_stream_for_pad (dbin, pad);
  g_assert (input);

  /* If there are no pending pads, this means we will definitely not need this
   * stream anymore */

  GST_DEBUG_OBJECT (pad, "Remove input stream %p", input);

  SELECTION_LOCK (dbin);
  slot = get_slot_for_input (dbin, input);
  remove_input_stream (dbin, input);

  if (slot && g_list_find (dbin->slots, slot) && slot->is_drained) {
    /* if slot is still there and already drained, remove it in here */
    if (slot->output) {
      DecodebinOutputStream *output = slot->output;
      GST_DEBUG_OBJECT (pad, "Multiqueue was drained, Remove output stream");

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
}
