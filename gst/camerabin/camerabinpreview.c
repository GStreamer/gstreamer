/*
* GStreamer
* Copyright (C) 2009 Nokia Corporation <multimedia@maemo.org>
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

#include <gst/gst.h>
#include <string.h>

#include "camerabindebug.h"
#include "camerabingeneral.h"
#include "camerabinpreview.h"

static void
save_result (GstElement * sink, GstBuffer * buf, GstPad * pad, gpointer data)
{
  GstBuffer **p_buf = (GstBuffer **) data;

  *p_buf = gst_buffer_ref (buf);

  GST_DEBUG ("received converted buffer %p with caps %" GST_PTR_FORMAT,
      *p_buf, GST_BUFFER_CAPS (*p_buf));
}

static gboolean
create_element (const gchar * factory_name, const gchar * elem_name,
    GstElement ** element, GError ** err)
{
  *element = gst_element_factory_make (factory_name, elem_name);
  if (*element)
    return TRUE;

  if (err && *err == NULL) {
    *err = g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN,
        "cannot create element '%s' - please check your GStreamer installation",
        factory_name);
  }

  return FALSE;
}


/**
 * gst_camerabin_preview_create_pipeline:
 * @element: #GstCameraBin element
 * @caps: pointer to the caps used in pipeline
 * @src_filter: source filter element
 *
 * Create a preview converter pipeline that outputs the format defined in
 * @caps parameter.
 *
 * Returns: New pipeline data structure, or NULL if error occured.
 */
GstCameraBinPreviewPipelineData *
gst_camerabin_preview_create_pipeline (GstElement * element, GstCaps * caps,
    GstElement * src_filter)
{
  GstElement *csp = NULL, *vscale = NULL;
  GError *error = NULL;
  GstCameraBinPreviewPipelineData *data;

  g_return_val_if_fail (caps != NULL, NULL);

  GST_DEBUG ("creating elements");

  data = g_new (GstCameraBinPreviewPipelineData, 1);

  /* We have multiple pipelines created by using this function, so we can't
   * give a name to them. Another way would to ensure the uniqueness of the
   * name here*/
  data->pipeline = gst_pipeline_new (NULL);
  if (!data->pipeline)
    goto create_error;

  if (!create_element ("appsrc", "prev_src", &data->appsrc, &error) ||
      !create_element ("videoscale", NULL, &vscale, &error) ||
      !create_element ("ffmpegcolorspace", NULL, &csp, &error) ||
      !create_element ("capsfilter", NULL, &data->capsfilter, &error) ||
      !create_element ("fakesink", "prev_sink", &data->appsink, &error))
    goto create_error;

  GST_DEBUG ("adding elements");
  gst_bin_add_many (GST_BIN (data->pipeline), data->appsrc, csp,
      data->capsfilter, vscale, data->appsink, NULL);
  if (src_filter) {
    gst_bin_add (GST_BIN (data->pipeline), src_filter);
  }

  data->element = element;

  GST_DEBUG ("preview format is: %" GST_PTR_FORMAT, caps);

  g_object_set (data->capsfilter, "caps", caps, NULL);
  g_object_set (data->appsink, "preroll-queue-len", 1, "signal-handoffs", TRUE,
      NULL);
  g_object_set (vscale, "method", 0, NULL);

  GST_DEBUG ("linking src->vscale");
  if (!gst_element_link_pads (data->appsrc, "src", vscale, "sink"))
    goto link_error;

  if (src_filter) {
    GST_DEBUG ("linking vscale->src_filter");
    if (!gst_element_link_pads (vscale, "src", src_filter, "sink")) {
      goto link_error;
    }
    GST_DEBUG ("linking filter->csp");
    if (!gst_element_link_pads (src_filter, "src", csp, "sink")) {
      goto link_error;
    }
  } else {
    GST_DEBUG ("linking vscale->csp");
    if (!gst_element_link_pads (vscale, "src", csp, "sink"))
      goto link_error;
  }

  GST_DEBUG ("linking csp->capsfilter");
  if (!gst_element_link_pads (csp, "src", data->capsfilter, "sink"))
    goto link_error;

  GST_DEBUG ("linking capsfilter->sink");
  if (!gst_element_link_pads (data->capsfilter, "src", data->appsink, "sink"))
    goto link_error;

  return data;

create_error:
  if (error) {
    GST_WARNING ("Preview pipeline element creation failed: %s",
        error->message);
    g_error_free (error);
  }
  if (csp)
    gst_object_unref (csp);
  if (vscale)
    gst_object_unref (vscale);
  if (data->appsrc)
    gst_object_unref (data->appsrc);
  if (data->capsfilter)
    gst_object_unref (data->capsfilter);
  if (data->appsink)
    gst_object_unref (data->appsink);

link_error:
  GST_WARNING ("Could not create preview pipeline");
  gst_camerabin_preview_destroy_pipeline (data);

  return NULL;
}


/**
 * gst_camerabin_preview_destroy_pipeline:
 * @data: the pipeline data to be destroyed
 *
 * Destroy preview converter pipeline.
 */
void
gst_camerabin_preview_destroy_pipeline (GstCameraBinPreviewPipelineData * data)
{
  if (data->pipeline) {
    gst_element_set_state (data->pipeline, GST_STATE_NULL);
    gst_object_unref (data->pipeline);
  }
  g_free (data);
}


/**
 * gst_camerabin_preview_convert:
 * @data: preview pipeline data to use
 * @buf: #GstBuffer that contains the frame to be converted
 *
 * Create a preview image of the given frame.
 *
 * Returns: converted preview image, or NULL if operation failed.
 */
GstBuffer *
gst_camerabin_preview_convert (GstCameraBinPreviewPipelineData * data,
    GstBuffer * buf)
{
  GstMessage *msg;
  GstBuffer *result = NULL;
  GError *error = NULL;
  GstBus *bus;
  GstElement *src, *sink;
  GstBufferFlag bflags;
  GstFlowReturn fret;

  g_return_val_if_fail (GST_BUFFER_CAPS (buf) != NULL, NULL);
  g_return_val_if_fail (data != NULL, NULL);

  if (data->pipeline == NULL) {
    GST_WARNING ("pipeline is NULL");
    goto no_pipeline;
  }

  src = gst_bin_get_by_name (GST_BIN (data->pipeline), "prev_src");
  sink = gst_bin_get_by_name (GST_BIN (data->pipeline), "prev_sink");

  if (!src || !sink) {
    GST_WARNING ("pipeline doesn't have src / sink elements");
    goto missing_elements;
  }

  g_object_set (src, "size", (gint64) GST_BUFFER_SIZE (buf),
      "blocksize", (guint32) GST_BUFFER_SIZE (buf),
      "caps", GST_BUFFER_CAPS (buf), "num-buffers", 1, NULL);

  g_signal_connect (sink, "handoff", G_CALLBACK (save_result), &result);

  bflags = GST_BUFFER_FLAGS (buf);
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);

  GST_DEBUG ("running conversion pipeline, source is: %" GST_PTR_FORMAT,
      GST_BUFFER_CAPS (buf));
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);

  g_signal_emit_by_name (src, "push-buffer", buf, &fret);

  bus = gst_element_get_bus (data->pipeline);
  msg = gst_bus_timed_pop_filtered (bus, (25 * GST_SECOND),
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  gst_object_unref (bus);

  if (msg) {
    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:{
        if (result) {
          GST_DEBUG ("preview image successful: result = %p", result);
        } else {
          GST_WARNING ("EOS but no result frame?!");
        }
        break;
      }
      case GST_MESSAGE_ERROR:{
        gchar *dbg = NULL;

        gst_message_parse_error (msg, &error, &dbg);
        if (error) {
          g_warning ("Could not make preview image: %s", error->message);
          GST_DEBUG ("%s [debug: %s]", error->message, GST_STR_NULL (dbg));
          g_error_free (error);
        } else {
          g_warning ("Could not make preview image (and NULL error!)");
        }
        g_free (dbg);
        result = NULL;
        break;
      }
      default:{
        g_return_val_if_reached (NULL);
      }
    }
    gst_message_unref (msg);
  } else {
    g_warning ("Could not make preview image: %s", "timeout during conversion");
    result = NULL;
  }

  g_signal_handlers_disconnect_by_func (sink, G_CALLBACK (save_result),
      &result);
  gst_element_set_state (data->pipeline, GST_STATE_READY);

  GST_BUFFER_FLAGS (buf) = bflags;

done:
  if (src)
    gst_object_unref (src);
  if (sink)
    gst_object_unref (sink);

  return result;

  /* ERRORS */
missing_elements:
  {
    g_warning ("Could not make preview image: %s",
        "missing elements in pipeline (unknown error)");
    goto done;
  }
no_pipeline:
  {
    g_warning ("Could not make preview image: %s",
        "no pipeline (unknown error)");
    return NULL;
  }
}

/**
 * gst_camerabin_preview_send_event:
 * @data: preview pipeline data to use
 * @evt: The #GstEvent to be pushed, takes ownership
 *
 * Pushes an event to the preview pipeline.
 *
 * Returns: True if the event was handled
 */
gboolean
gst_camerabin_preview_send_event (GstCameraBinPreviewPipelineData * data,
    GstEvent * evt)
{
  GstElement *src;

  src = gst_bin_get_by_name (GST_BIN (data->pipeline), "prev_src");
  if (!src) {
    GST_WARNING ("Preview pipeline doesn't have src element, can't push event");
    gst_event_unref (evt);
    return FALSE;
  }

  GST_DEBUG_OBJECT (data->element, "Pushing event %p to preview pipeline", evt);

  return gst_element_send_event (src, evt);
}

/**
 * gst_camerabin_preview_set_caps:
 * @data: preview pipeline data to use
 * @caps: New #GstCaps to be set for the pipeline
 *
 * Sets new caps for the preview pipeline
 */
void
gst_camerabin_preview_set_caps (GstCameraBinPreviewPipelineData * data,
    GstCaps * caps)
{
  GstState state, pending;
  GstStateChangeReturn ret;

  g_return_if_fail (data->pipeline != NULL);
  g_return_if_fail (caps != NULL);

  ret = gst_element_get_state (data->pipeline, &state, &pending, 0);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    /* make it try again */
    state = GST_STATE_PLAYING;
    pending = GST_STATE_VOID_PENDING;
  }

  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  g_object_set (data->capsfilter, "caps", caps, NULL);
  if (pending != GST_STATE_VOID_PENDING)
    state = pending;
  gst_element_set_state (data->pipeline, state);
}
