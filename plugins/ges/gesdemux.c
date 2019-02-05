/* GStreamer GES plugin
 *
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gesdemux.c
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
 *

 **
 * SECTION:gstdemux
 * @short_description: A GstBin subclasses use to use GESTimeline
 * as demux inside any GstPipeline.
 * @see_also: #GESTimeline
 *
 * The gstdemux is a bin that will simply expose the track source pads
 * and implements the GstUriHandler interface using a custom ges://0Xpointer
 * uri scheme.
 **/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <glib/gstdio.h>
#include <gst/pbutils/pbutils.h>
#include "gesdemux.h"

GST_DEBUG_CATEGORY_STATIC (gesdemux);
#define GST_CAT_DEFAULT gesdemux

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw(ANY)"));

static GstStaticPadTemplate audio_src_template =
    GST_STATIC_PAD_TEMPLATE ("audio_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw(ANY);"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/xges"));

G_DEFINE_TYPE (GESDemux, ges_demux, GST_TYPE_BIN);

enum
{
  PROP_0,
  PROP_TIMELINE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static gboolean
ges_demux_set_timeline (GESDemux * self, GESTimeline * timeline)
{
  GList *tmp;
  guint naudiopad = 0, nvideopad = 0;
  GstBin *sbin = GST_BIN (self);

  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  if (self->timeline) {
    GST_ERROR_OBJECT (self, "Implement changing timeline support");

    return FALSE;
  }

  GST_INFO_OBJECT (self, "Setting timeline: %" GST_PTR_FORMAT, timeline);
  self->timeline = gst_object_ref (timeline);

  if (!gst_bin_add (sbin, GST_ELEMENT (self->timeline))) {
    GST_ERROR_OBJECT (self, "Could not add timeline to myself!");

    return FALSE;
  }
  for (tmp = self->timeline->tracks; tmp; tmp = tmp->next) {
    GstPad *gpad;
    gchar *name = NULL;
    GstElement *queue;
    GESTrack *track = GES_TRACK (tmp->data);
    GstPad *tmppad, *pad =
        ges_timeline_get_pad_for_track (self->timeline, track);
    GstStaticPadTemplate *template;

    if (!pad) {
      GST_WARNING_OBJECT (self, "No pad for track: %" GST_PTR_FORMAT, track);

      continue;
    }

    if (track->type == GES_TRACK_TYPE_AUDIO) {
      name = g_strdup_printf ("audio_%u", naudiopad++);
      template = &audio_src_template;
    } else if (track->type == GES_TRACK_TYPE_VIDEO) {
      name = g_strdup_printf ("video_%u", nvideopad++);
      template = &video_src_template;
    } else {
      GST_INFO_OBJECT (self, "Track type not handled: %" GST_PTR_FORMAT, track);
      continue;
    }

    queue = gst_element_factory_make ("queue", NULL);
    /* Add queues the same way as in GESPipeline */
    g_object_set (G_OBJECT (queue), "max-size-buffers", 0,
        "max-size-bytes", 0, "max-size-time", (gint64) 2 * GST_SECOND, NULL);
    gst_bin_add (GST_BIN (self), queue);
    gst_element_sync_state_with_parent (GST_ELEMENT (queue));

    tmppad = gst_element_get_static_pad (queue, "sink");
    if (gst_pad_link (pad, tmppad) != GST_PAD_LINK_OK) {
      GST_ERROR_OBJECT (self, "Could not link %s:%s and %s:%s",
          GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (tmppad));

      gst_object_unref (tmppad);
      gst_object_unref (queue);
      continue;
    }

    tmppad = gst_element_get_static_pad (queue, "src");
    gpad = gst_ghost_pad_new_from_template (name, tmppad,
        gst_static_pad_template_get (template));

    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (self), gpad);
    GST_DEBUG_OBJECT (self, "Adding pad: %" GST_PTR_FORMAT, gpad);
  }

  gst_element_sync_state_with_parent (GST_ELEMENT (self->timeline));

  return TRUE;
}

static void
ges_demux_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESDemux *self = GES_DEMUX (object);

  switch (property_id) {
    case PROP_TIMELINE:
      g_value_set_object (value, self->timeline);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_demux_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_demux_dispose (GObject * object)
{
  GESDemux *self = GES_DEMUX (object);

  if (self->timeline)
    gst_clear_object (&self->timeline);
}

static void
ges_demux_class_init (GESDemuxClass * self_class)
{
  GObjectClass *gclass = G_OBJECT_CLASS (self_class);
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (self_class);

  GST_DEBUG_CATEGORY_INIT (gesdemux, "gesdemux", 0, "ges demux element");

  gclass->get_property = ges_demux_get_property;
  gclass->set_property = ges_demux_set_property;
  gclass->dispose = ges_demux_dispose;

  /**
   * GESDemux:timeline:
   *
   * Timeline to use in this source.
   */
  properties[PROP_TIMELINE] = g_param_spec_object ("timeline", "Timeline",
      "Timeline to use in this source.",
      GES_TYPE_TIMELINE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gclass, PROP_LAST, properties);

  gst_element_class_set_static_metadata (gstelement_klass,
      "GStreamer Editing Services based 'demuxer'",
      "Codec/Demux/Editing",
      "Demuxer for complex timeline file formats using GES.",
      "Thibault Saunier <tsaunier@igalia.com");

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&audio_src_template));
}

typedef struct
{
  GESTimeline *timeline;
  gchar *uri;
  GMainLoop *ml;
  GError *error;
  GMutex lock;
  GCond cond;
  gulong loaded_sigid;
  gulong error_sigid;
} TimelineConstructionData;

static void
project_loaded_cb (GESProject * project, GESTimeline * timeline,
    TimelineConstructionData * data)
{
  g_mutex_lock (&data->lock);
  data->timeline = timeline;
  g_signal_handler_disconnect (project, data->loaded_sigid);
  data->loaded_sigid = 0;
  g_mutex_unlock (&data->lock);

  g_main_loop_quit (data->ml);
}

static void
error_loading_asset_cb (GESProject * project, GError * error, gchar * id,
    GType extractable_type, TimelineConstructionData * data)
{
  g_mutex_lock (&data->lock);
  data->error = g_error_copy (error);
  g_signal_handler_disconnect (project, data->error_sigid);
  data->error_sigid = 0;
  g_mutex_unlock (&data->lock);

  g_main_loop_quit (data->ml);
}

/* TODO: Add a way to run a function in the right GES thread */
static gboolean
ges_timeline_new_from_uri_from_main_thread (TimelineConstructionData * data)
{
  GESProject *project = ges_project_new (data->uri);
  GESUriClipAssetClass *klass = g_type_class_peek (GES_TYPE_URI_CLIP_ASSET);
  GstDiscoverer *previous_discoverer = klass->discoverer;
  GstClockTime timeout;
  G_GNUC_UNUSED void *unused;

  g_object_get (previous_discoverer, "timeout", &timeout, NULL);

  /* Make sure to use a new discoverer in case we are being discovered,
   * as discovering is done one by one, and the global discoverer won't
   * have the chance to discover the project assets */
  g_mutex_lock (&data->lock);
  klass->discoverer = gst_discoverer_new (timeout, &data->error);
  if (data->error) {
    klass->discoverer = previous_discoverer;
    g_mutex_unlock (&data->lock);

    goto done;
  }
  g_signal_connect (klass->discoverer, "discovered",
      G_CALLBACK (klass->discovered), NULL);
  gst_discoverer_start (klass->discoverer);

  data->ml = g_main_loop_new (NULL, TRUE);
  data->loaded_sigid =
      g_signal_connect (project, "loaded", G_CALLBACK (project_loaded_cb),
      data);
  data->error_sigid =
      g_signal_connect (project, "error-loading-asset",
      G_CALLBACK (error_loading_asset_cb), data);

  unused = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), &data->error));
  if (data->error) {
    g_mutex_unlock (&data->lock);

    goto done;
  }
  g_mutex_unlock (&data->lock);

  g_main_loop_run (data->ml);
  g_main_loop_unref (data->ml);

done:

  g_mutex_lock (&data->lock);

  /* Set previous discoverer back! */

  if (klass->discoverer)
    gst_object_unref (klass->discoverer);
  klass->discoverer = previous_discoverer;

  if (data->timeline)
    ges_timeline_commit (data->timeline);

  if (data->loaded_sigid)
    g_signal_handler_disconnect (project, data->loaded_sigid);

  if (data->error_sigid)
    g_signal_handler_disconnect (project, data->error_sigid);

  gst_clear_object (&project);

  g_cond_broadcast (&data->cond);
  g_mutex_unlock (&data->lock);

  return G_SOURCE_REMOVE;
}

static gboolean
ges_demux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GESDemux *self = GES_DEMUX (parent);

  switch (event->type) {
    case GST_EVENT_EOS:{
      GstMapInfo map;
      GstBuffer *xges_buffer;
      gboolean ret = TRUE;
      gsize available;

      available = gst_adapter_available (self->input_adapter);
      if (available == 0) {
        GST_WARNING_OBJECT (self,
            "Received EOS without any serialized timeline.");

        return gst_pad_event_default (pad, parent, event);
      }

      xges_buffer = gst_adapter_take_buffer (self->input_adapter, available);
      if (gst_buffer_map (xges_buffer, &map, GST_MAP_READ)) {
        GError *err = NULL;
        gchar *filename = NULL, *uri = NULL;
        TimelineConstructionData data = { 0, };
        gint f = g_file_open_tmp (NULL, &filename, &err);
        GMainContext *main_context = g_main_context_default ();

        GST_ERROR ("Loading %s", filename);
        if (err) {
          GST_ELEMENT_ERROR (self, RESOURCE, OPEN_WRITE,
              ("Could not open temporary file to write timeline description"),
              ("%s", err->message));

          goto error;
        }

        g_file_set_contents (filename, (gchar *) map.data, map.size, &err);
        if (err) {
          GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
              ("Could not write temporary timeline description file"),
              ("%s", err->message));

          goto error;
        }

        uri = gst_uri_construct ("file", filename);
        data.uri = uri;

        g_main_context_invoke (main_context,
            (GSourceFunc) ges_timeline_new_from_uri_from_main_thread, &data);
        g_mutex_lock (&data.lock);
        while (!data.error && !data.timeline)
          g_cond_wait (&data.cond, &data.lock);
        data.loaded_sigid = 0;
        data.error_sigid = 0;
        g_mutex_unlock (&data.lock);

        if (data.error) {
          GST_ELEMENT_ERROR (self, STREAM, DEMUX,
              ("Could not create timeline from description"),
              ("%s", data.error->message));
          g_clear_error (&data.error);

          goto error;
        }

        GST_INFO_OBJECT (self, "Timeline properly loaded: %" GST_PTR_FORMAT,
            data.timeline);
        ges_demux_set_timeline (self, data.timeline);
      done:
        g_free (filename);
        g_free (uri);
        g_close (f, NULL);
        return ret;
      error:
        ret = FALSE;
        goto done;
      } else {
        GST_ELEMENT_ERROR (self, RESOURCE, READ,
            ("Could not map buffer containing timeline description"),
            ("Not info"));
      }
      GST_ERROR_OBJECT (xges_buffer, ":YAY");
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
ges_demux_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GESDemux *self = GES_DEMUX (parent);

  gst_adapter_push (self->input_adapter, buffer);

  GST_INFO_OBJECT (self, "Received buffer, total size is %i bytes",
      (gint) gst_adapter_available (self->input_adapter));

  return GST_FLOW_OK;
}

static void
ges_demux_init (GESDemux * self)
{
  ges_init ();
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->input_adapter = gst_adapter_new ();

  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (ges_demux_sink_chain));

  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (ges_demux_sink_event));
}
