/* GStreamer
 *
 * unit testing helper lib
 *
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

/**
 * SECTION:gstcheckconsistencychecker
 * @short_description: Data flow consistency checker for GStreamer unit tests.
 *
 * These macros and functions are for internal use of the unit tests found
 * inside the 'check' directories of various GStreamer packages.
 *
 * Since: 0.10.24
 */

#include "gstconsistencychecker.h"

struct _GstStreamConsistency
{
  gboolean flushing;
  gboolean newsegment;
  gboolean eos;
  gulong probeid;
  GstPad *pad;
};

static gboolean
source_pad_data_cb (GstPad * pad, GstMiniObject * data,
    GstStreamConsistency * consist)
{
  if (GST_IS_BUFFER (data)) {
    GST_DEBUG_OBJECT (pad, "Buffer %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (GST_BUFFER (data))));
    /* If an EOS went through, a buffer would be invalid */
    fail_if (consist->eos, "Buffer received after EOS");
    /* Buffers need to be preceded by a newsegment event */
    fail_unless (consist->newsegment, "Buffer received without newsegment");
  } else if (GST_IS_EVENT (data)) {
    GstEvent *event = (GstEvent *) data;

    GST_DEBUG_OBJECT (pad, "%s", GST_EVENT_TYPE_NAME (event));
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_FLUSH_START:
        consist->flushing = TRUE;
        break;
      case GST_EVENT_FLUSH_STOP:
        /* Receiving a flush-stop is only valid after receiving a flush-start */
        fail_unless (consist->flushing,
            "Received a FLUSH_STOP without a FLUSH_START");
        fail_if (consist->eos, "Received a FLUSH_STOP after an EOS");
        consist->flushing = FALSE;
        break;
      case GST_EVENT_NEWSEGMENT:
        consist->newsegment = TRUE;
        consist->eos = FALSE;
        break;
      case GST_EVENT_EOS:
        /* FIXME : not 100% sure about whether two eos in a row is valid */
        fail_if (consist->eos, "Received EOS just after another EOS");
        consist->eos = TRUE;
        consist->newsegment = FALSE;
        break;
      case GST_EVENT_TAG:
        GST_DEBUG_OBJECT (pad, "tag %" GST_PTR_FORMAT, event->structure);
        /* fall through */
      default:
        if (GST_EVENT_IS_SERIALIZED (event) && GST_EVENT_IS_DOWNSTREAM (event)) {
          fail_if (consist->eos, "Event received after EOS");
          fail_unless (consist->newsegment, "Event received before newsegment");
        }
        /* FIXME : Figure out what to do for other events */
        break;
    }
  }

  return TRUE;
}

/**
 * gst_consistency_checker_new:
 * @pad: The #GstPad on which the dataflow will be checked.
 *
 * Sets up a data probe on the given pad which will raise assertions if the
 * data flow is inconsistent.
 *
 * Currently only works for source pads.
 *
 * Returns: A #GstStreamConsistency structure used to track data flow.
 *
 * Since: 0.10.24
 */

GstStreamConsistency *
gst_consistency_checker_new (GstPad * pad)
{
  GstStreamConsistency *consist;

  g_return_val_if_fail (pad != NULL, NULL);

  consist = g_new0 (GstStreamConsistency, 1);
  consist->pad = g_object_ref (pad);
  consist->probeid =
      gst_pad_add_data_probe (pad, (GCallback) source_pad_data_cb, consist);

  return consist;
}

/**
 * gst_consistency_checker_reset:
 * @consist: The #GstStreamConsistency to reset.
 *
 * Reset the stream checker's internal variables.
 *
 * Since: 0.10.24
 */

void
gst_consistency_checker_reset (GstStreamConsistency * consist)
{
  consist->eos = FALSE;
  consist->flushing = FALSE;
  consist->newsegment = FALSE;
}

/**
 * gst_consistency_checker_free:
 * @consist: The #GstStreamConsistency to free.
 *
 * Frees the allocated data and probe associated with @consist.
 *
 * Since: 0.10.24
 */

void
gst_consistency_checker_free (GstStreamConsistency * consist)
{
  /* Remove the data probe */
  gst_pad_remove_data_probe (consist->pad, consist->probeid);
  g_object_unref (consist->pad);
  g_free (consist);
}
