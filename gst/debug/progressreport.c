/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2004> Jan Schmidt <thaytan@mad.scientist.com>
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
#include <gst/base/gstbasetransform.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define GST_TYPE_PROGRESS_REPORT \
  (gst_progress_report_get_type())
#define GST_PROGRESS_REPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PROGRESS_REPORT,GstProgressReport))
#define GST_PROGRESS_REPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PROGRESS_REPORT,GstProgressReportClass))
#define GST_IS_PROGRESS_REPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PROGRESS_REPORT))
#define GST_IS_PROGRESS_REPORT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PROGRESS_REPORT))

typedef struct _GstProgressReport GstProgressReport;
typedef struct _GstProgressReportClass GstProgressReportClass;

struct _GstProgressReport
{
  GstBaseTransform basetransform;

  gint update_freq;
  gboolean silent;
  GTimeVal start_time;
  GTimeVal last_report;
};

struct _GstProgressReportClass
{
  GstBaseTransformClass parent_class;
};

enum
{
  ARG_0,
  ARG_UPDATE_FREQ,
  ARG_SILENT
};

GstStaticPadTemplate progress_report_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstStaticPadTemplate progress_report_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstElementDetails progress_report_details =
GST_ELEMENT_DETAILS ("Progress Report",
    "Testing",
    "Periodically query and report on processing progress",
    "Jan Schmidt <thaytan@mad.scientist.com>");

#define DEFAULT_UPDATE_FREQ  5
#define DEFAULT_SILENT       FALSE

static void gst_progress_report_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_progress_report_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_progress_report_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_progress_report_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

static gboolean gst_progress_report_start (GstBaseTransform * trans);
static gboolean gst_progress_report_stop (GstBaseTransform * trans);

GST_BOILERPLATE (GstProgressReport, gst_progress_report, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_progress_report_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&progress_report_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&progress_report_src_template));

  gst_element_class_set_details (element_class, &progress_report_details);
}

static void
gst_progress_report_class_init (GstProgressReportClass * g_class)
{
  GstBaseTransformClass *gstbasetrans_class;
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (g_class);

  gobject_class->set_property = gst_progress_report_set_property;
  gobject_class->get_property = gst_progress_report_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (g_class),
      ARG_UPDATE_FREQ, g_param_spec_int ("update-freq", "Update Frequency",
          "Number of seconds between reports when data is flowing", 1, G_MAXINT,
          DEFAULT_UPDATE_FREQ, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (g_class),
      ARG_SILENT, g_param_spec_boolean ("silent",
          "Do not print output to stdout", "Do not print output to stdout",
          DEFAULT_SILENT, G_PARAM_READWRITE));

  gstbasetrans_class->event = GST_DEBUG_FUNCPTR (gst_progress_report_event);
  gstbasetrans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_progress_report_transform_ip);
  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_progress_report_start);
  gstbasetrans_class->stop = GST_DEBUG_FUNCPTR (gst_progress_report_stop);
}

static void
gst_progress_report_init (GstProgressReport * report,
    GstProgressReportClass * g_class)
{
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (report), TRUE);

  report->update_freq = DEFAULT_UPDATE_FREQ;
  report->silent = DEFAULT_SILENT;
}

static void
gst_progress_report_report (GstProgressReport * filter, GTimeVal cur_time)
{
  GstFormat try_formats[] = { GST_FORMAT_TIME, GST_FORMAT_BYTES,
    GST_FORMAT_PERCENT, GST_FORMAT_BUFFERS,
    GST_FORMAT_DEFAULT
  };
  GstPad *peer_pad;
  gint64 cur_progress;
  gint64 total_progress;
  gint hh, mm, ss, i;
  glong run_time;

  GST_LOCK (filter);
  run_time = cur_time.tv_sec - filter->start_time.tv_sec;

  hh = (run_time / 3600) % 100;
  mm = (run_time / 60) % 60;
  ss = (run_time % 60);

  peer_pad = gst_pad_get_peer (GST_BASE_TRANSFORM (filter)->sinkpad);
  /* Query for the current time then attempt to set to time + offset */
  for (i = 0; i < G_N_ELEMENTS (try_formats); ++i) {
    const gchar *format_name = NULL;
    GstFormat format;

    format = try_formats[i];

    if (gst_pad_query_position (peer_pad, &format, &cur_progress,
            &total_progress)) {
      switch (format) {
        case GST_FORMAT_BYTES:
          format_name = "bytes";
          break;
        case GST_FORMAT_BUFFERS:
          format_name = "buffers";
          break;
        case GST_FORMAT_PERCENT:
          format_name = "percent";
          break;
        case GST_FORMAT_TIME:
          format_name = "seconds";
          cur_progress /= GST_SECOND;
          total_progress /= GST_SECOND;
          break;
        case GST_FORMAT_DEFAULT:
        {
          GstCaps *caps;

          format_name = "bogounits";
          caps = GST_PAD_CAPS (GST_BASE_TRANSFORM (filter)->sinkpad);
          if (caps && gst_caps_is_fixed (caps) && !gst_caps_is_any (caps)) {
            GstStructure *s = gst_caps_get_structure (caps, 0);
            const gchar *mime_type = gst_structure_get_name (s);

            if (g_str_has_prefix (mime_type, "video/")
                || g_str_has_prefix (mime_type, "image/")) {
              format_name = "frames";
            } else if (g_str_has_prefix (mime_type, "audio/")) {
              format_name = "samples";
            }
          }
          break;
        }
        default:
        {
          const GstFormatDefinition *details;

          details = gst_format_get_details (format);
          if (details) {
            format_name = details->nick;
          } else {
            format_name = "unknown";
          }
          break;
        }
      }

      if (!filter->silent) {
        if (total_progress > 0) {
          g_print ("%s (%02d:%02d:%02d): %" G_GINT64_FORMAT " / %"
              G_GINT64_FORMAT " %s (%4.1f %%)\n", GST_OBJECT_NAME (filter), hh,
              mm, ss, cur_progress, total_progress, format_name,
              (gdouble) cur_progress / total_progress * 100.0);
        } else {
          g_print ("%s (%02d:%02d:%02d): %" G_GINT64_FORMAT " %s\n",
              GST_OBJECT_NAME (filter), hh, mm, ss, cur_progress, format_name);
        }
      }
      break;
    }
  }

  if (i == G_N_ELEMENTS (try_formats)) {
    g_print ("%s (%2d:%2d:%2d): Could not query current position.\n",
        GST_OBJECT_NAME (filter), hh, mm, ss);
  }

  GST_UNLOCK (filter);

  gst_object_unref (peer_pad);
}

static gboolean
gst_progress_report_event (GstBaseTransform * trans, GstEvent * event)
{
  GstProgressReport *filter;

  filter = GST_PROGRESS_REPORT (trans);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GTimeVal cur_time;

    g_get_current_time (&cur_time);
    gst_progress_report_report (filter, cur_time);
  }
  return TRUE;
}

static GstFlowReturn
gst_progress_report_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstProgressReport *filter;
  gboolean need_update;
  GTimeVal cur_time;

  g_get_current_time (&cur_time);

  filter = GST_PROGRESS_REPORT (trans);

  /* Check if update_freq seconds have passed since the last update */
  GST_LOCK (filter);
  need_update =
      ((cur_time.tv_sec - filter->last_report.tv_sec) >= filter->update_freq);
  GST_UNLOCK (filter);

  if (need_update) {
    gst_progress_report_report (filter, cur_time);
    GST_LOCK (filter);
    filter->last_report = cur_time;
    GST_UNLOCK (filter);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_progress_report_start (GstBaseTransform * trans)
{
  GstProgressReport *filter;

  filter = GST_PROGRESS_REPORT (trans);

  g_get_current_time (&filter->last_report);
  filter->start_time = filter->last_report;

  return TRUE;
}

static gboolean
gst_progress_report_stop (GstBaseTransform * trans)
{
  /* anything we should be doing here? */
  return TRUE;
}

static void
gst_progress_report_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstProgressReport *filter;

  filter = GST_PROGRESS_REPORT (object);

  switch (prop_id) {
    case ARG_UPDATE_FREQ:
      GST_LOCK (filter);
      filter->update_freq = g_value_get_int (value);
      GST_UNLOCK (filter);
      break;
    case ARG_SILENT:
      GST_LOCK (filter);
      filter->silent = g_value_get_boolean (value);
      GST_UNLOCK (filter);
      break;
    default:
      break;
  }
}

static void
gst_progress_report_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstProgressReport *filter;

  filter = GST_PROGRESS_REPORT (object);

  switch (prop_id) {
    case ARG_UPDATE_FREQ:
      GST_LOCK (filter);
      g_value_set_int (value, filter->update_freq);
      GST_UNLOCK (filter);
      break;
    case ARG_SILENT:
      GST_LOCK (filter);
      g_value_set_boolean (value, filter->silent);
      GST_UNLOCK (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


gboolean
gst_progress_report_plugin_init (GstPlugin * plugin, GstPluginClass * g_class)
{
  return gst_element_register (plugin, "progressreport", GST_RANK_NONE,
      GST_TYPE_PROGRESS_REPORT);
}
