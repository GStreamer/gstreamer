/* GStreamer
 *
 * Copyright (C) 2018 Igalia S.L. All rights reserved.
 *  @author: Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION:element-testsrcbin
 * @title: testsrc
 *
 * This is a simple GstBin source that wraps audiotestsrc/videotestsrc
 * following specification passed in the URI (it implements the #GstURIHandler interface)
 * in the form of `testbin://audio+video` or setting the "stream-types" property
 * with the same format.
 *
 * This element also provides GstStream and GstStreamCollection and
 * thus the element is useful for testing the new playbin3 infrastructure.
 *
 * Example pipeline:
 * ```
 * gst-launch-1.0 playbin uri=testbin://audio,volume=0.5+video,pattern=white
 * ```
 */
#include <gst/gst.h>
#include <gst/base/gstflowcombiner.h>
#include <gst/app/gstappsink.h>

static GstStaticPadTemplate video_src_template =
GST_STATIC_PAD_TEMPLATE ("video_src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw(ANY)"));

static GstStaticPadTemplate audio_src_template =
    GST_STATIC_PAD_TEMPLATE ("audio_src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw(ANY);"));

#define GST_TYPE_TEST_SRC_BIN  gst_test_src_bin_get_type()
#define GST_TEST_SRC_BIN(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GST_TYPE_TEST_SRC_BIN, GstTestSrcBin))

typedef struct _GstTestSrcBin GstTestSrcBin;
typedef struct _GstTestSrcBinClass GstTestSrcBinClass;

/* *INDENT-OFF* */
GType gst_test_src_bin_get_type (void) G_GNUC_CONST;
/* *INDENT-ON* */

struct _GstTestSrcBinClass
{
  GstBinClass parent_class;
};

struct _GstTestSrcBin
{
  GstBin parent;

  gchar *uri;
  GstStreamCollection *collection;
  gint group_id;
  GstFlowCombiner *flow_combiner;
};

enum
{
  PROP_0,
  PROP_STREAM_TYPES,
  PROP_LAST
};

#define DEFAULT_TYPES GST_STREAM_TYPE_AUDIO & GST_STREAM_TYPE_VIDEO

static GstURIType
gst_test_src_bin_uri_handler_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_test_src_bin_uri_handler_get_protocols (GType type)
{
  static const gchar *protocols[] = { "testbin", NULL };

  return protocols;
}

static gchar *
gst_test_src_bin_uri_handler_get_uri (GstURIHandler * handler)
{
  GstTestSrcBin *self = GST_TEST_SRC_BIN (handler);
  gchar *uri;

  GST_OBJECT_LOCK (self);
  uri = g_strdup (self->uri);
  GST_OBJECT_UNLOCK (self);

  return uri;
}

typedef struct
{
  GstEvent *stream_start;
  GstStreamCollection *collection;
} ProbeData;

static ProbeData *
_probe_data_new (GstEvent * stream_start, GstStreamCollection * collection)
{
  ProbeData *data = g_malloc0 (sizeof (ProbeData));

  data->stream_start = gst_event_ref (stream_start);
  data->collection = gst_object_ref (collection);

  return data;
}

static void
_probe_data_free (ProbeData * data)
{
  gst_event_replace (&data->stream_start, NULL);
  gst_object_replace ((GstObject **) & data->collection, NULL);

  g_free (data);
}

static GstPadProbeReturn
src_pad_probe_cb (GstPad * pad, GstPadProbeInfo * info, ProbeData * data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:{
      gst_event_unref (event);
      info->data = gst_event_ref (data->stream_start);
      return GST_PAD_PROBE_OK;
    }
    case GST_EVENT_CAPS:{
      if (data->collection) {
        GstStreamCollection *collection = data->collection;
        /* Make sure the collection is NULL so that when caps get unstickied
         * we let them pass through. */
        data->collection = NULL;
        gst_pad_push_event (pad, gst_event_new_stream_collection (collection));
        gst_object_unref (collection);
      }
      return GST_PAD_PROBE_REMOVE;
    }
    default:
      break;
  }

  return GST_PAD_PROBE_OK;
}

static GstFlowReturn
gst_test_src_bin_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstFlowReturn res, chain_res;

  GstTestSrcBin *self = GST_TEST_SRC_BIN (gst_object_get_parent (object));

  chain_res = gst_proxy_pad_chain_default (pad, GST_OBJECT (self), buffer);
  res = gst_flow_combiner_update_pad_flow (self->flow_combiner, pad, chain_res);
  gst_object_unref (self);

  if (res == GST_FLOW_FLUSHING)
    return chain_res;

  return res;
}

static gboolean
gst_test_src_bin_set_element_property (GQuark property_id, const GValue * value,
    GObject * element)
{
  if (G_VALUE_HOLDS_STRING (value))
    gst_util_set_object_arg (element, g_quark_to_string (property_id),
        g_value_get_string (value));
  else
    g_object_set_property (element, g_quark_to_string (property_id), value);

  return TRUE;
}

typedef struct
{
  GstEvent *event;
  gboolean res;
  GstObject *parent;
} ForwardEventData;


static gboolean
forward_seeks (GstElement * element, GstPad * pad, ForwardEventData * data)
{
  data->res &=
      gst_pad_event_default (pad, data->parent, gst_event_ref (data->event));

  return TRUE;
}

static gboolean
gst_test_src_event_function (GstPad * pad, GstObject * parent, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      ForwardEventData data = { event, TRUE, parent };

      gst_element_foreach_src_pad (GST_ELEMENT (parent),
          (GstElementForeachPadFunc) forward_seeks, &data);
      return data.res;
    }
    default:
      break;
  }
  return gst_pad_event_default (pad, parent, event);
}

static void
gst_test_src_bin_setup_src (GstTestSrcBin * self, const gchar * srcfactory,
    GstStaticPadTemplate * template, GstStreamType stype,
    GstStreamCollection * collection, gint * n_stream, GstStructure * props)
{
  GstElement *src = gst_element_factory_make (srcfactory, NULL);
  GstPad *proxypad, *ghost, *pad = gst_element_get_static_pad (src, "src");
  gchar *stream_id = g_strdup_printf ("%s_stream_%d", srcfactory, *n_stream);
  gchar *pad_name = g_strdup_printf (template->name_template, *n_stream);
  GstStream *stream = gst_stream_new (stream_id, NULL, stype,
      (*n_stream == 0) ? GST_STREAM_FLAG_SELECT : GST_STREAM_FLAG_UNSELECT);
  GstEvent *stream_start =
      gst_event_new_stream_start (gst_stream_get_stream_id (stream));

  gst_structure_foreach (props,
      (GstStructureForeachFunc) gst_test_src_bin_set_element_property, src);

  gst_event_set_stream (stream_start, stream);
  gst_event_set_group_id (stream_start, self->group_id);

  gst_pad_add_probe (pad, (GstPadProbeType) GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) src_pad_probe_cb, _probe_data_new (stream_start,
          collection), (GDestroyNotify) _probe_data_free);

  gst_stream_collection_add_stream (collection, stream);
  g_free (stream_id);

  gst_bin_add (GST_BIN (self), src);

  ghost =
      gst_ghost_pad_new_from_template (pad_name, pad,
      gst_static_pad_template_get (template));
  proxypad = GST_PAD (gst_proxy_pad_get_internal (GST_PROXY_PAD (ghost)));
  gst_flow_combiner_add_pad (self->flow_combiner, ghost);
  gst_pad_set_chain_function (proxypad,
      (GstPadChainFunction) gst_test_src_bin_chain);
  gst_pad_set_event_function (ghost,
      (GstPadEventFunction) gst_test_src_event_function);
  gst_object_unref (proxypad);
  gst_element_add_pad (GST_ELEMENT (self), ghost);
  gst_object_unref (pad);
  gst_element_sync_state_with_parent (src);
  *n_stream += 1;
}

static void
gst_test_src_bin_remove_child (GValue * val, GstBin * self)
{
  GstElement *child = g_value_get_object (val);

  gst_bin_remove (self, child);
}

static gboolean
gst_test_src_bin_uri_handler_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error)
{
  GstTestSrcBin *self = GST_TEST_SRC_BIN (handler);
  gchar *tmp, *location = gst_uri_get_location (uri);
  gint i, n_audio = 0, n_video = 0;
  GstStreamCollection *collection = gst_stream_collection_new (NULL);
  GstIterator *it;
  GstCaps *streams_defs;

  for (tmp = location; *tmp != '\0'; tmp++)
    if (*tmp == '+')
      *tmp = ';';

  streams_defs = gst_caps_from_string (location);
  g_free (location);

  if (!streams_defs)
    goto failed;

  /* Clear us up */
  it = gst_bin_iterate_elements (GST_BIN (self));
  while (gst_iterator_foreach (it,
          (GstIteratorForeachFunction) gst_test_src_bin_remove_child,
          self) == GST_ITERATOR_RESYNC)
    gst_iterator_resync (it);

  gst_iterator_free (it);

  self->group_id = gst_util_group_id_next ();
  for (i = 0; i < gst_caps_get_size (streams_defs); i++) {
    GstStructure *stream_def = gst_caps_get_structure (streams_defs, i);

    if (gst_structure_has_name (stream_def, "video"))
      gst_test_src_bin_setup_src (self, "videotestsrc", &video_src_template,
          GST_STREAM_TYPE_VIDEO, collection, &n_video, stream_def);
    else if (gst_structure_has_name (stream_def, "audio"))
      gst_test_src_bin_setup_src (self, "audiotestsrc", &audio_src_template,
          GST_STREAM_TYPE_AUDIO, collection, &n_audio, stream_def);
    else
      GST_ERROR_OBJECT (self, "Unknown type %s",
          gst_structure_get_name (stream_def));
  }

  if (!n_video && !n_audio)
    goto failed;

  self->uri = g_strdup (uri);
  gst_element_post_message (GST_ELEMENT (self),
      gst_message_new_stream_collection (GST_OBJECT (self), collection));

  return TRUE;

failed:
  if (error)
    *error =
        g_error_new_literal (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "No media type specified in the testbin:// URL.");

  return FALSE;
}

static void
gst_test_src_bin_uri_handler_init (gpointer g_iface, gpointer unused)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_test_src_bin_uri_handler_get_type;
  iface->get_protocols = gst_test_src_bin_uri_handler_get_protocols;
  iface->get_uri = gst_test_src_bin_uri_handler_get_uri;
  iface->set_uri = gst_test_src_bin_uri_handler_set_uri;
}

/* *INDENT-OFF* */
G_DEFINE_TYPE_WITH_CODE (GstTestSrcBin, gst_test_src_bin, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_test_src_bin_uri_handler_init))
/* *INDENT-ON* */

static void
gst_test_src_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTestSrcBin *self = GST_TEST_SRC_BIN (object);

  switch (prop_id) {
    case PROP_STREAM_TYPES:
    {
      gchar *uri = g_strdup_printf ("testbin://%s", g_value_get_string (value));

      g_assert (gst_uri_handler_set_uri (GST_URI_HANDLER (self), uri, NULL));
      g_free (uri);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_test_src_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTestSrcBin *self = GST_TEST_SRC_BIN (object);

  switch (prop_id) {
    case PROP_STREAM_TYPES:
    {
      gchar *uri = gst_uri_handler_get_uri (GST_URI_HANDLER (self));
      if (uri) {
        gchar *types = gst_uri_get_location (uri);
        g_value_set_string (value, types);
        g_free (uri);
        g_free (types);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_test_src_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstTestSrcBin *self = GST_TEST_SRC_BIN (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  result =
      GST_ELEMENT_CLASS (gst_test_src_bin_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gst_flow_combiner_reset (self->flow_combiner);
      break;
    }
    default:
      break;
  }

  return result;
}

static void
gst_test_src_bin_finalize (GObject * object)
{
  GstTestSrcBin *self = GST_TEST_SRC_BIN (object);

  g_free (self->uri);
  gst_flow_combiner_free (self->flow_combiner);
}

static void
gst_test_src_bin_init (GstTestSrcBin * self)
{
  self->flow_combiner = gst_flow_combiner_new ();
}

static void
gst_test_src_bin_class_init (GstTestSrcBinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_klass = (GstElementClass *) klass;

  gobject_class->finalize = gst_test_src_bin_finalize;
  gobject_class->get_property = gst_test_src_bin_get_property;
  gobject_class->set_property = gst_test_src_bin_set_property;

  /**
   * GstTestSrcBin::stream-types:
   *
   * String describing the stream types to expose, eg. "video+audio".
   */
  g_object_class_install_property (gobject_class, PROP_STREAM_TYPES,
      g_param_spec_string ("stream-types", "Stream types",
          "String describing the stream types to expose, eg. \"video+audio\".",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_test_src_bin_change_state);
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&audio_src_template));
}
