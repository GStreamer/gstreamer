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
#include <string.h>
#include <math.h>
#include <time.h>

#define GST_TYPE_PROGRESSREPORT \
  (gst_progressreport_get_type())
#define GST_PROGRESSREPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PROGRESSREPORT,GstProgressReport))
#define GST_PROGRESSREPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PROGRESSREPORT,GstProgressReportClass))
#define GST_IS_PROGRESSREPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PROGRESSREPORT))
#define GST_IS_PROGRESSREPORT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PROGRESSREPORT))

typedef struct _GstProgressReport GstProgressReport;
typedef struct _GstProgressReportClass GstProgressReportClass;

struct _GstProgressReport
{
  GstElement element;
  GstPad *sinkpad;
  GstPad *srcpad;

  gint update_freq;
  GTimeVal start_time;
  GTimeVal last_report;
};

struct _GstProgressReportClass
{
  GstElementClass parent_class;
};

/* GstProgressReport signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_UPDATE_FREQ
      /* FILL ME */
};

GstStaticPadTemplate progressreport_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GstStaticPadTemplate progressreport_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_progressreport_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_progressreport_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_progressreport_chain (GstPad * pad, GstData * _data);

GST_BOILERPLATE (GstProgressReport, gst_progressreport, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_progressreport_base_init (gpointer g_class)
{
  static GstElementDetails progressreport_details =
      GST_ELEMENT_DETAILS ("Progress Report",
      "Testing",
      "Periodically query and report on processing progress",
      "Jan Schmidt <thaytan@mad.scientist.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&progressreport_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&progressreport_src_template));

  gst_element_class_set_details (element_class, &progressreport_details);
}

static void
gst_progressreport_class_init (GstProgressReportClass * g_class)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (g_class);

  g_object_class_install_property (G_OBJECT_CLASS (g_class),
      ARG_UPDATE_FREQ, g_param_spec_int ("update-freq", "Update Frequency",
          "Number of seconds between reports when data is flowing", 1, G_MAXINT,
          5, G_PARAM_READWRITE));

  gobject_class->set_property = gst_progressreport_set_property;
  gobject_class->get_property = gst_progressreport_get_property;
}

static void
gst_progressreport_init (GstProgressReport * instance)
{
  GstProgressReport *progressreport = GST_PROGRESSREPORT (instance);

  progressreport->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&progressreport_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (progressreport), progressreport->sinkpad);
  gst_pad_set_chain_function (progressreport->sinkpad,
      gst_progressreport_chain);
  gst_pad_set_link_function (progressreport->sinkpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (progressreport->sinkpad, gst_pad_proxy_getcaps);

  progressreport->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&progressreport_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (progressreport), progressreport->srcpad);
  gst_pad_set_link_function (progressreport->srcpad, gst_pad_proxy_pad_link);
  gst_pad_set_getcaps_function (progressreport->srcpad, gst_pad_proxy_getcaps);

  g_get_current_time (&(progressreport->last_report));
  progressreport->start_time = progressreport->last_report;
  progressreport->update_freq = 5;
}

static void
gst_progressreport_report (GstProgressReport * progressreport,
    GTimeVal cur_time)
{
  /* Query for the current time then attempt to set to time + offset */
  gint64 cur_progress;
  gint64 total_progress;
  GstFormat peer_format = GST_FORMAT_DEFAULT;
  gint hh, mm, ss;
  glong run_time = cur_time.tv_sec - progressreport->start_time.tv_sec;

  hh = (run_time / 3600) % 100;
  mm = (run_time / 60) % 60;
  ss = (run_time % 60);

  if (gst_pad_query (gst_pad_get_peer (progressreport->sinkpad),
          GST_QUERY_POSITION, &peer_format, &cur_progress)) {
    GstFormat peer_format2 = peer_format;
    gchar *format_name = NULL;

    switch (peer_format) {
      case GST_FORMAT_BYTES:
        format_name = "bytes";
        break;
      case GST_FORMAT_TIME:
        format_name = "seconds";
        cur_progress /= GST_SECOND;
        total_progress /= GST_SECOND;
        break;
      case GST_FORMAT_BUFFERS:
        format_name = "buffers";
        break;
      case GST_FORMAT_PERCENT:
        format_name = "percent";
        break;
      default:
        format_name = "unknown";
        break;
    }

    if ((gst_pad_query (gst_pad_get_peer (progressreport->sinkpad),
                GST_QUERY_TOTAL, &peer_format2, &total_progress)) &&
        (peer_format == peer_format2)) {
      g_print ("%s (%2d:%2d:%2d): %lld / %lld %s (%3.2g %%)\n",
          gst_object_get_name (GST_OBJECT (progressreport)), hh, mm, ss,
          cur_progress, total_progress, format_name,
          ((gdouble) (cur_progress)) / total_progress * 100);
    } else {
      g_print ("%s (%2d:%2d:%2d): %lld %s\n",
          gst_object_get_name (GST_OBJECT (progressreport)), hh, mm, ss,
          cur_progress, format_name);
    }
  } else {
    g_print ("%s (%2d:%2d:%2d): Could not query current position.\n",
        gst_object_get_name (GST_OBJECT (progressreport)), hh, mm, ss);
  }
}

static void
gst_progressreport_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstProgressReport *src;

  g_return_if_fail (GST_IS_PROGRESSREPORT (object));
  src = GST_PROGRESSREPORT (object);

  switch (prop_id) {
    case ARG_UPDATE_FREQ:
      src->update_freq = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_progressreport_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstProgressReport *src;

  g_return_if_fail (GST_IS_PROGRESSREPORT (object));
  src = GST_PROGRESSREPORT (object);

  switch (prop_id) {
    case ARG_UPDATE_FREQ:
      g_value_set_int (value, src->update_freq);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_progressreport_chain (GstPad * pad, GstData * _data)
{
  GstProgressReport *progressreport;
  GTimeVal cur_time;

  g_get_current_time (&cur_time);

  progressreport = GST_PROGRESSREPORT (gst_pad_get_parent (pad));

  /* Check if update_freq seconds have passed since the last update */
  if ((cur_time.tv_sec - progressreport->last_report.tv_sec) >=
      progressreport->update_freq) {
    gst_progressreport_report (progressreport, cur_time);
    progressreport->last_report = cur_time;
  }

  gst_pad_push (progressreport->srcpad, _data);
}

gboolean
gst_progressreport_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "progressreport", GST_RANK_NONE,
      GST_TYPE_PROGRESSREPORT);
}
