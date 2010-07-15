/* Small helper element for format conversion
 * Copyright (C) 2005 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

#include <gst/gst.h>
#include <glib-object.h>
#include <string.h>
#include "ges-screenshot.h"

/* FIXME (merge into core): This code is duplicated from gstscreenshot.c and
 * gstplaysink.c, adapted to handle encoding the converted frame into
 *  arbitrary formats using gst-convenience.
 */

/* takes ownership of the input buffer */
GstBuffer *gst_play_frame_conv_convert (GstBuffer * buf, GstCaps * to_caps);

static gboolean
create_element (const gchar * factory_name, GstElement ** element,
    GError ** err);

GstBuffer *
gst_play_sink_convert_frame (GstElement * playsink, GstCaps * caps)
{
  GstBuffer *result = NULL;

  g_object_get (G_OBJECT (playsink), "frame", (GstMiniObject *) & result, NULL);

  GST_DEBUG ("got buffer %p from playsink", result);

  if (result != NULL && caps != NULL) {
    GstBuffer *temp;

    temp = gst_play_frame_conv_convert (result, caps);
    gst_buffer_unref (result);
    result = temp;
  }
  return result;
}

static gboolean
create_element (const gchar * factory_name, GstElement ** element,
    GError ** err)
{
  *element = gst_element_factory_make (factory_name, NULL);
  if (*element)
    return TRUE;

  if (err && *err == NULL) {
    *err = g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN,
        "cannot create element '%s' - please check your GStreamer installation",
        factory_name);
  }

  return FALSE;
}

/* takes ownership of the input buffer */
GstBuffer *
gst_play_frame_conv_convert (GstBuffer * buf, GstCaps * to_caps)
{
  GstElement *src, *csp, *vscale, *sink, *pipeline;
  GstMessage *msg;
  GstBuffer *result = NULL;
  GError *error = NULL;
  GstBus *bus;
  GstCaps *from_caps;
  GstFlowReturn ret;

  from_caps = GST_BUFFER_CAPS (buf);

  g_return_val_if_fail (from_caps != NULL, NULL);

  /* videoscale is here to correct for the pixel-aspect-ratio for us */
  GST_DEBUG ("creating elements");
  if (!create_element ("appsrc", &src, &error) ||
      !create_element ("ffmpegcolorspace", &csp, &error) ||
      !create_element ("videoscale", &vscale, &error) ||
      !create_element ("appsink", &sink, &error))
    goto no_elements;

  pipeline = gst_pipeline_new ("screenshot-pipeline");
  if (pipeline == NULL)
    goto no_pipeline;

  GST_DEBUG ("adding elements");
  gst_bin_add_many (GST_BIN (pipeline), src, csp, vscale, sink, NULL);

  /* set caps */
  g_object_set (src, "caps", from_caps, NULL);
  g_object_set (sink, "caps", to_caps, NULL);

  /* FIXME: linking is still way too expensive, profile this properly */
  GST_DEBUG ("linking src->csp");
  if (!gst_element_link_pads (src, "src", csp, "sink"))
    goto link_failed;

  GST_DEBUG ("linking csp->vscale");
  if (!gst_element_link_pads (csp, "src", vscale, "sink"))
    goto link_failed;

  GST_DEBUG ("linking vscale->sink");
  if (!gst_element_link_pads (vscale, "src", sink, "sink"))
    goto link_failed;

  /* now set the pipeline to the paused state, after we push the buffer into
   * appsrc, this should preroll the converted buffer in appsink */
  GST_DEBUG ("running conversion pipeline to caps %" GST_PTR_FORMAT, to_caps);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  /* feed buffer in appsrc */
  GST_DEBUG ("feeding buffer %p, size %u, caps %" GST_PTR_FORMAT,
      buf, GST_BUFFER_SIZE (buf), from_caps);
  g_signal_emit_by_name (src, "push-buffer", buf, &ret);

  /* now see what happens. We either got an error somewhere or the pipeline
   * prerolled */
  bus = gst_element_get_bus (pipeline);
  msg =
      gst_bus_poll (bus, GST_MESSAGE_ERROR | GST_MESSAGE_ASYNC_DONE,
      25 * GST_SECOND);

  if (msg) {
    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ASYNC_DONE:
      {
        /* we're prerolled, get the frame from appsink */
        g_signal_emit_by_name (sink, "pull-preroll", &result);

        if (result) {
          GST_DEBUG ("conversion successful: result = %p", result);
        } else {
          GST_WARNING ("prerolled but no result frame?!");
        }
        break;
      }
      case GST_MESSAGE_ERROR:{
        gchar *dbg = NULL;

        gst_message_parse_error (msg, &error, &dbg);
        if (error) {
          g_warning ("Could not take screenshot: %s", error->message);
          GST_DEBUG ("%s [debug: %s]", error->message, GST_STR_NULL (dbg));
          g_error_free (error);
        } else {
          g_warning ("Could not take screenshot (and NULL error!)");
        }
        g_free (dbg);
        break;
      }
      default:{
        g_return_val_if_reached (NULL);
      }
    }
    gst_message_unref (msg);
  } else {
    g_warning ("Could not take screenshot: %s", "timeout during conversion");
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  return result;

  /* ERRORS */
no_elements:
  {
    g_warning ("Could not take screenshot: %s", error->message);
    g_error_free (error);
    return NULL;
  }
no_pipeline:
  {
    g_warning ("Could not take screenshot: %s", "no pipeline (unknown error)");
    return NULL;
  }
link_failed:
  {
    g_warning ("Could not take screenshot: %s", "failed to link elements");
    gst_object_unref (pipeline);
    return NULL;
  }
}
