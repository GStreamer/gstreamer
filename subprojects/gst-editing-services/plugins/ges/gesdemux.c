/* GStreamer GES plugin
 *
 * Copyright (C) 2019 Igalia S.L
 *     Author: 2019 Thibault Saunier <tsaunier@igalia.com>
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
 */

 /**
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

#include "gesbasebin.h"

#include <gst/gst.h>
#include <glib/gstdio.h>
#include <gst/pbutils/pbutils.h>
#include <gst/base/gstadapter.h>
#include <ges/ges.h>

GST_DEBUG_CATEGORY_STATIC (gesdemux);
#define GST_CAT_DEFAULT gesdemux

G_DECLARE_FINAL_TYPE (GESDemux, ges_demux, GES, DEMUX, GESBaseBin);
#define GES_DEMUX_DOC_CAPS \
  "application/xges;" \
  "text/x-xptv;" \
  "application/vnd.pixar.opentimelineio+json;" \
  "application/vnd.apple-xmeml+xml;" \
  "application/vnd.apple-fcp+xml;" \

struct _GESDemux
{
  GESBaseBin parent;

  GESTimeline *timeline;
  GstPad *sinkpad;

  GstAdapter *input_adapter;

  gchar *upstream_uri;
  GStatBuf stats;
};

G_DEFINE_TYPE (GESDemux, ges_demux, ges_base_bin_get_type ());
#define GES_DEMUX(obj) ((GESDemux*)obj)

enum
{
  PROP_0,
  PROP_TIMELINE,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static GstCaps *
ges_demux_get_sinkpad_caps ()
{
  GList *tmp, *formatters;
  GstCaps *sinkpad_caps = gst_caps_new_empty ();

  formatters = ges_list_assets (GES_TYPE_FORMATTER);
  for (tmp = formatters; tmp; tmp = tmp->next) {
    GstCaps *caps;
    const gchar *mimetype =
        ges_meta_container_get_string (GES_META_CONTAINER (tmp->data),
        GES_META_FORMATTER_MIMETYPE);
    if (!mimetype)
      continue;

    caps = gst_caps_from_string (mimetype);

    if (!caps) {
      GST_INFO_OBJECT (tmp->data,
          "%s - could not create caps from mimetype: %s",
          ges_meta_container_get_string (GES_META_CONTAINER (tmp->data),
              GES_META_FORMATTER_NAME), mimetype);

      continue;
    }

    gst_caps_append (sinkpad_caps, caps);
  }
  g_list_free (formatters);

  return sinkpad_caps;
}

static gchar *
ges_demux_get_extension (GstStructure * _struct)
{
  GList *tmp, *formatters;
  gchar *ext = NULL;

  formatters = ges_list_assets (GES_TYPE_FORMATTER);
  for (tmp = formatters; tmp; tmp = tmp->next) {
    gchar **extensions_a;
    gint i, n_exts;
    GstCaps *caps;
    const gchar *mimetype =
        ges_meta_container_get_string (GES_META_CONTAINER (tmp->data),
        GES_META_FORMATTER_MIMETYPE);
    const gchar *extensions =
        ges_meta_container_get_string (GES_META_CONTAINER (tmp->data),
        GES_META_FORMATTER_EXTENSION);
    if (!mimetype)
      continue;

    if (!extensions)
      continue;

    caps = gst_caps_from_string (mimetype);
    if (!caps) {
      GST_INFO_OBJECT (tmp->data,
          "%s - could not create caps from mimetype: %s",
          ges_meta_container_get_string (GES_META_CONTAINER (tmp->data),
              GES_META_FORMATTER_NAME), mimetype);

      continue;
    }

    extensions_a = g_strsplit (extensions, ",", -1);
    n_exts = g_strv_length (extensions_a);
    for (i = 0; i < gst_caps_get_size (caps) && i < n_exts; i++) {
      GstStructure *structure = gst_caps_get_structure (caps, i);

      if (gst_structure_has_name (_struct, gst_structure_get_name (structure))) {
        ext = g_strdup (extensions_a[i]);
        g_strfreev (extensions_a);
        gst_caps_unref (caps);
        goto done;
      }
    }
    g_strfreev (extensions_a);
  }
done:
  g_list_free (formatters);

  return ext;
}

static void
ges_demux_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESDemux *self = GES_DEMUX (object);

  switch (property_id) {
    case PROP_TIMELINE:
      g_value_set_object (value,
          ges_base_bin_get_timeline (GES_BASE_BIN (self)));
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
ges_demux_finalize (GObject * object)
{
  GESDemux *demux = (GESDemux *) object;
  g_free (demux->upstream_uri);
  G_OBJECT_CLASS (ges_demux_parent_class)->finalize (object);
}

static void
ges_demux_class_init (GESDemuxClass * self_class)
{
  GstPadTemplate *pad_template;
  GObjectClass *gclass = G_OBJECT_CLASS (self_class);
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (self_class);
  GstCaps *sinkpad_caps, *doc_caps;

  GST_DEBUG_CATEGORY_INIT (gesdemux, "gesdemux", 0, "ges demux element");

  sinkpad_caps = ges_demux_get_sinkpad_caps ();

  gclass->finalize = ges_demux_finalize;
  gclass->get_property = ges_demux_get_property;
  gclass->set_property = ges_demux_set_property;

  /**
   * GESDemux:timeline:
   *
   * Timeline to use in this source.
   */
  properties[PROP_TIMELINE] = g_param_spec_object ("timeline", "Timeline",
      "Timeline to use in this source.",
      GES_TYPE_TIMELINE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_override_property (gclass, PROP_TIMELINE, "timeline");

  gst_element_class_set_static_metadata (gstelement_klass,
      "GStreamer Editing Services based 'demuxer'",
      "Codec/Demux/Editing",
      "Demuxer for complex timeline file formats using GES.",
      "Thibault Saunier <tsaunier@igalia.com");

  pad_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkpad_caps);
  doc_caps = gst_caps_from_string (GES_DEMUX_DOC_CAPS);
  gst_pad_template_set_documentation_caps (pad_template, doc_caps);
  gst_clear_caps (&doc_caps);
  gst_element_class_add_pad_template (gstelement_klass, pad_template);
  gst_caps_unref (sinkpad_caps);
}

typedef struct
{
  GESTimeline *timeline;
  GMainLoop *ml;
  GError *error;
  gulong loaded_sigid;
  gulong error_sigid;
  gulong error_asset_sigid;
} TimelineConstructionData;

static void
project_loaded_cb (GESProject * project, GESTimeline * timeline,
    TimelineConstructionData * data)
{
  data->timeline = timeline;
  g_signal_handler_disconnect (project, data->loaded_sigid);
  data->loaded_sigid = 0;

  g_main_loop_quit (data->ml);
}

static void
error_loading_cb (GESProject * project, GESTimeline * timeline,
    GError * error, TimelineConstructionData * data)
{
  data->error = g_error_copy (error);
  g_signal_handler_disconnect (project, data->error_sigid);
  data->error_sigid = 0;

  g_main_loop_quit (data->ml);
}

static void
error_loading_asset_cb (GESProject * project, GError * error, gchar * id,
    GType extractable_type, TimelineConstructionData * data)
{
  data->error = g_error_copy (error);
  g_signal_handler_disconnect (project, data->error_asset_sigid);
  data->error_asset_sigid = 0;

  g_main_loop_quit (data->ml);
}

static gboolean
ges_demux_src_probe (GstPad * pad, GstPadProbeInfo * info, GstElement * parent)
{
  GESDemux *self = GES_DEMUX (parent);
  GstStructure *structure =
      (GstStructure *) gst_query_get_structure (info->data);

  if (gst_structure_has_name (structure, "NleCompositionQueryNeedsTearDown")) {
    GstQuery *uri_query = gst_query_new_uri ();

    if (gst_pad_peer_query (self->sinkpad, uri_query)) {
      gchar *upstream_uri = NULL;
      GStatBuf stats;
      gst_query_parse_uri (uri_query, &upstream_uri);

      if (gst_uri_has_protocol (upstream_uri, "file")) {
        gchar *location = gst_uri_get_location (upstream_uri);

        if (g_stat (location, &stats) < 0) {
          GST_INFO_OBJECT (self, "Could not stat %s - not updating", location);

          g_free (location);
          g_free (upstream_uri);
          goto done;
        }

        g_free (location);
        GST_OBJECT_LOCK (self);
        if (g_strcmp0 (upstream_uri, self->upstream_uri)
            || stats.st_mtime != self->stats.st_mtime
            || stats.st_size != self->stats.st_size) {
          GST_INFO_OBJECT (self,
              "Underlying file changed, asking for an update");
          gst_structure_set (structure, "result", G_TYPE_BOOLEAN, TRUE, NULL);
          g_free (self->upstream_uri);
          self->upstream_uri = upstream_uri;
          self->stats = stats;
        } else {
          g_free (upstream_uri);
        }
        GST_OBJECT_UNLOCK (self);
      }
    }
  done:
    gst_query_unref (uri_query);
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
ges_demux_set_srcpad_probe (GstElement * element, GstPad * pad,
    gpointer user_data)
{
  gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_QUERY_UPSTREAM,
      (GstPadProbeCallback) ges_demux_src_probe, element, NULL);
  return TRUE;
}

static void
ges_demux_adapt_timeline_duration (GESDemux * self, GESTimeline * timeline)
{
  GType nleobject_type = g_type_from_name ("NleObject");
  GstObject *parent, *tmpparent;

  parent = gst_object_get_parent (GST_OBJECT (self));
  while (parent) {
    if (g_type_is_a (G_OBJECT_TYPE (parent), nleobject_type)) {
      GstClockTime duration, inpoint, timeline_duration;

      g_object_get (parent, "duration", &duration, "inpoint", &inpoint, NULL);
      g_object_get (timeline, "duration", &timeline_duration, NULL);

      if (inpoint + duration > timeline_duration) {
        GESLayer *layer = ges_timeline_get_layer (timeline, 0);

        if (layer) {
          GESClip *clip = GES_CLIP (ges_test_clip_new ());
          GList *tmp, *tracks = ges_timeline_get_tracks (timeline);

          g_object_set (clip, "start", timeline_duration, "duration",
              inpoint + duration, "vpattern", GES_VIDEO_TEST_PATTERN_SMPTE75,
              NULL);
          ges_layer_add_clip (layer, clip);
          for (tmp = tracks; tmp; tmp = tmp->next) {
            if (GES_IS_VIDEO_TRACK (tmp->data)) {
              GESEffect *text;
              GstCaps *caps;
              gchar *effect_str_full = NULL;
              const gchar *effect_str =
                  "textoverlay text=\"Nested timeline too short, please FIX!\" halignment=center valignment=center";

              g_object_get (tmp->data, "restriction-caps", &caps, NULL);
              if (caps) {
                gchar *caps_str = gst_caps_to_string (caps);
                effect_str = effect_str_full =
                    g_strdup_printf ("capsfilter caps=\"%s\" ! %s", caps_str,
                    effect_str);
                g_free (caps_str);
                gst_caps_unref (caps);
              }
              text = ges_effect_new (effect_str);
              g_free (effect_str_full);

              if (!ges_container_add (GES_CONTAINER (clip),
                      GES_TIMELINE_ELEMENT (text))) {
                GST_ERROR ("Could not add text overlay to ending clip!");
              }
            }

          }
          g_list_free_full (tracks, gst_object_unref);
          GST_INFO_OBJECT (timeline,
              "Added test clip with duration: %" GST_TIME_FORMAT " - %"
              GST_TIME_FORMAT " to match parent nleobject duration",
              GST_TIME_ARGS (timeline_duration),
              GST_TIME_ARGS (inpoint + duration - timeline_duration));
        }
      }
      gst_object_unref (parent);

      return;
    }

    tmpparent = parent;
    parent = gst_object_get_parent (GST_OBJECT (parent));
    gst_object_unref (tmpparent);
  }
}

static gboolean
ges_demux_create_timeline (GESDemux * self, gchar * uri, GError ** error)
{
  GESProject *project = ges_project_new (uri);
  G_GNUC_UNUSED void *unused;
  TimelineConstructionData data = { 0, };
  GMainContext *ctx = g_main_context_new ();
  GstQuery *query;

  g_main_context_push_thread_default (ctx);
  data.ml = g_main_loop_new (ctx, TRUE);

  data.loaded_sigid =
      g_signal_connect (project, "loaded", G_CALLBACK (project_loaded_cb),
      &data);
  data.error_asset_sigid =
      g_signal_connect_after (project, "error-loading-asset",
      G_CALLBACK (error_loading_asset_cb), &data);
  data.error_sigid =
      g_signal_connect_after (project, "error-loading",
      G_CALLBACK (error_loading_cb), &data);

  unused = GES_TIMELINE (ges_asset_extract (GES_ASSET (project), &data.error));
  if (data.error) {
    *error = data.error;

    goto done;
  }

  g_main_loop_run (data.ml);
  g_main_loop_unref (data.ml);
  if (data.error)
    goto done;

  ges_demux_adapt_timeline_duration (self, data.timeline);

  query = gst_query_new_uri ();
  if (gst_pad_peer_query (self->sinkpad, query)) {
    GList *assets, *tmp;

    GST_OBJECT_LOCK (self);
    g_free (self->upstream_uri);
    gst_query_parse_uri (query, &self->upstream_uri);
    if (gst_uri_has_protocol (self->upstream_uri, "file")) {
      gchar *location = gst_uri_get_location (self->upstream_uri);

      if (g_stat (location, &self->stats) < 0)
        GST_INFO_OBJECT (self, "Could not stat file: %s", location);
      g_free (location);
    }

    assets = ges_project_list_assets (project, GES_TYPE_URI_CLIP);
    for (tmp = assets; tmp; tmp = tmp->next) {
      const gchar *id = ges_asset_get_id (tmp->data);

      if (!g_strcmp0 (id, self->upstream_uri)) {
        g_set_error (error, GST_STREAM_ERROR, GST_STREAM_ERROR_DEMUX,
            "Recursively loading uri: %s", self->upstream_uri);
        break;
      }
    }
    GST_OBJECT_UNLOCK (self);
    g_list_free_full (assets, g_object_unref);
  }

done:
  if (data.loaded_sigid)
    g_signal_handler_disconnect (project, data.loaded_sigid);

  if (data.error_sigid)
    g_signal_handler_disconnect (project, data.error_sigid);

  if (data.error_asset_sigid)
    g_signal_handler_disconnect (project, data.error_asset_sigid);

  g_clear_object (&project);

  GST_INFO_OBJECT (self, "Timeline properly loaded: %" GST_PTR_FORMAT,
      data.timeline);

  if (!data.error) {
    ges_base_bin_set_timeline (GES_BASE_BIN (self), data.timeline);
    gst_element_foreach_src_pad (GST_ELEMENT (self), ges_demux_set_srcpad_probe,
        NULL);
  } else {
    *error = data.error;
  }

  g_main_context_pop_thread_default (ctx);

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
        gint f;
        GError *err = NULL;
        gchar *template = NULL;
        gchar *filename = NULL, *uri = NULL;
        GstCaps *caps = gst_pad_get_current_caps (pad);
        GstStructure *structure = gst_caps_get_structure (caps, 0);
        gchar *ext = ges_demux_get_extension (structure);

        gst_caps_unref (caps);
        if (ext) {
          template = g_strdup_printf ("XXXXXX.%s", ext);
          g_free (ext);
        }

        f = g_file_open_tmp (template, &filename, &err);
        g_free (template);

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

        uri = gst_filename_to_uri (filename, NULL);
        GST_INFO_OBJECT (self, "Pre loading the timeline.");

        ges_demux_create_timeline (self, uri, &err);
        if (err)
          goto error;

      done:
        g_free (filename);
        g_free (uri);
        g_close (f, NULL);
        return ret;

      error:
        ret = FALSE;
        gst_element_post_message (GST_ELEMENT (self),
            gst_message_new_error (parent, err,
                "Could not create timeline from description"));
        g_clear_error (&err);

        goto done;
      } else {
        GST_ELEMENT_ERROR (self, RESOURCE, READ,
            ("Could not map buffer containing timeline description"),
            ("Not info"));
      }
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
  SUPRESS_UNUSED_WARNING (GES_DEMUX);
  SUPRESS_UNUSED_WARNING (GES_IS_DEMUX);
#if defined(g_autoptr)
  SUPRESS_UNUSED_WARNING (glib_autoptr_cleanup_GESDemux);
#endif

  self->sinkpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (self), "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->input_adapter = gst_adapter_new ();

  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (ges_demux_sink_chain));

  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (ges_demux_sink_event));
}
