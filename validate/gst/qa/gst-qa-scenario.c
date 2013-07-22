/* GStreamer
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-qa-scenario.c - QA Scenario class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include <gio/gio.h>
#include <string.h>

#include "gst-qa-scenario.h"
#include "gst-qa-reporter.h"
#include "gst-qa-report.h"

#define GST_QA_SCENARIO_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_QA_SCENARIO, GstQaScenarioPrivate))

#define GST_QA_SCENARIO_SUFFIX ".xml"
#define GST_QA_SCERNARIO_DIRECTORY "qa-scenario"

GST_DEBUG_CATEGORY_STATIC (gst_qa_scenario);
#define GST_CAT_DEFAULT gst_qa_scenario


#define DEFAULT_SEEK_TOLERANCE (0.1 * GST_SECOND)       /* tolerance seek interval
                                                           TODO make it overridable  */
enum
{
  PROP_0,
  PROP_RUNNER,
  PROP_LAST
};

static void gst_qa_scenario_class_init (GstQaScenarioClass * klass);
static void gst_qa_scenario_init (GstQaScenario * scenario);
static void gst_qa_scenario_dispose (GObject * object);
static void gst_qa_scenario_finalize (GObject * object);

G_DEFINE_TYPE_WITH_CODE (GstQaScenario, gst_qa_scenario, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_QA_REPORTER, NULL));

typedef struct _SeekInfo
{
  gchar *name;
  GstClockTime seeking_time;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type;
  GstClockTime start;
  GstSeekType stop_type;
  GstClockTime stop;

} SeekInfo;

struct _GstQaScenarioPrivate
{
  GstElement *pipeline;
  GstQaRunner *runner;

  GList *seeks;
  gint64 seeked_position;       /* last seeked position */
  GstClockTime seek_pos_tol;
};

/* Some helper method that are missing iin Json itscenario */
static guint
get_flags_from_string (GType type, const gchar * str_flags)
{
  guint i;
  gint flags = 0;
  GFlagsClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_flags, class->values[i].value_nick)) {
      flags |= class->values[i].value;
    }
  }
  g_type_class_unref (class);

  return flags;
}

static void
get_enum_from_string (GType type, const gchar * str_enum, guint * enum_value)
{
  guint i;
  GEnumClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_enum, class->values[i].value_nick)) {
      *enum_value = class->values[i].value;
      break;
    }
  }

  g_type_class_unref (class);
}

static SeekInfo *
_new_seek_info (void)
{
  SeekInfo *info = g_slice_new (SeekInfo);

  info->rate = 1.0;
  info->format = GST_FORMAT_TIME;
  info->start_type = GST_SEEK_TYPE_SET;
  info->stop_type = GST_SEEK_TYPE_SET;
  info->flags = GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH;
  info->seeking_time = GST_SECOND;
  info->start = 0;
  info->stop = GST_CLOCK_TIME_NONE;

  return info;
}

static void
_free_seek_info (SeekInfo * info)
{
  g_slice_free (SeekInfo, info);
}

static inline void
_parse_seek (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GstQaScenario * scenario, GError ** error)
{
  GstQaScenarioPrivate *priv = scenario->priv;
  const char *seeking_time, *format, *rate, *flags, *start_type, *start,
      *stop_type, *stop;

  SeekInfo *info = _new_seek_info ();

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRDUP, "name", &info->name,
          G_MARKUP_COLLECT_STRING, "seeking_time", &seeking_time,
          G_MARKUP_COLLECT_STRING, "format", &format,
          G_MARKUP_COLLECT_STRING, "rate", &rate,
          G_MARKUP_COLLECT_STRING, "flags", &flags,
          G_MARKUP_COLLECT_STRING, "start_type", &start_type,
          G_MARKUP_COLLECT_STRING, "start", &start,
          G_MARKUP_COLLECT_STRING, "stop_type", &stop_type,
          G_MARKUP_COLLECT_STRING, "stop", &stop, G_MARKUP_COLLECT_INVALID))
    return;

  get_enum_from_string (GST_TYPE_FORMAT, format, &info->format);

  info->rate = g_ascii_strtoull (rate, NULL, 10);
  info->flags = get_flags_from_string (GST_TYPE_SEEK_FLAGS, flags);
  info->seeking_time = g_ascii_strtoull (seeking_time, NULL, 10);
  get_enum_from_string (GST_TYPE_SEEK_TYPE, start_type, &info->start_type);
  info->start = g_ascii_strtoull (start, NULL, 10);
  get_enum_from_string (GST_TYPE_SEEK_TYPE, stop_type, &info->stop_type);
  info->stop = g_ascii_strtoull (stop, NULL, 10);

  priv->seeks = g_list_append (priv->seeks, info);
}

static void
_parse_element_start (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    gpointer scenario, GError ** error)
{
  if (g_strcmp0 (element_name, "seek") == 0) {
    _parse_seek (context, element_name, attribute_names,
        attribute_values, scenario, error);
  }
}

static gboolean
get_position (GstQaScenario * scenario)
{
  GList *tmp;
  gint64 position;
  GstFormat format = GST_FORMAT_TIME;
  GstQaScenarioPrivate *priv = scenario->priv;
  GstElement *pipeline = scenario->priv->pipeline;

  gst_element_query_position (pipeline, &format, &position);

  tmp = scenario->priv->seeks;
  GST_DEBUG ("Current position: %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
  while (tmp) {
    SeekInfo *seek = tmp->data;

    if ((position >= (seek->seeking_time - priv->seek_pos_tol))
        && (position <= (seek->seeking_time + priv->seek_pos_tol))) {

      if (GST_CLOCK_TIME_IS_VALID (priv->seeked_position))
        GST_QA_REPORT_ISSUE (scenario, TRUE, SEEK, TIMING,
            "Previous seek to %" GST_TIME_FORMAT " was not handled",
            GST_TIME_ARGS (priv->seeked_position));

      GST_LOG ("seeking to: %" GST_TIME_FORMAT " stop: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (seek->start), GST_TIME_ARGS (seek->stop));

      if (gst_element_seek (pipeline, seek->rate,
              seek->format, seek->flags,
              seek->start_type, seek->start,
              seek->stop_type, seek->stop) == FALSE) {
        GST_QA_REPORT_ISSUE (scenario, TRUE, SEEK, UNKNOWN,
            "Could not seek to position %" GST_TIME_FORMAT,
            GST_TIME_ARGS (priv->seeked_position));
      }

      priv->seeked_position = seek->start;
      priv->seeks = g_list_remove_link (priv->seeks, tmp);
      g_slice_free (SeekInfo, seek);
      g_list_free (tmp);
      break;
    }
    tmp = tmp->next;
  }
  return TRUE;
}

static gboolean
async_done_cb (GstBus * bus, GstMessage * message, GstQaScenario * scenario)
{
  GstQaScenarioPrivate *priv = scenario->priv;

  if (GST_CLOCK_TIME_IS_VALID (priv->seeked_position)) {
    gint64 position;
    GstFormat format = GST_FORMAT_TIME;

    gst_element_query_position (priv->pipeline, &format, &position);
    if (position > (priv->seeked_position + priv->seek_pos_tol) ||
        position < (MAX (0,
                ((gint64) (priv->seeked_position - priv->seek_pos_tol))))) {

      GST_QA_REPORT_ISSUE (scenario, TRUE, SEEK, TIMING,
          "Seeked position %" GST_TIME_FORMAT
          "not in the expected range [%" GST_TIME_FORMAT " -- %"
          GST_TIME_FORMAT, GST_TIME_ARGS (position),
          GST_TIME_ARGS (((MAX (0,
                          ((gint64) (priv->seeked_position -
                                  priv->seek_pos_tol)))))),
          GST_TIME_ARGS ((priv->seeked_position + priv->seek_pos_tol)));
    }
    priv->seeked_position = GST_CLOCK_TIME_NONE;
  }

  return TRUE;
}

static gboolean
_load_scenario_file (GstQaScenario * scenario, const gchar * scenario_file)
{
  gsize xmlsize;
  GFile *file = NULL;
  GError *err = NULL;
  gboolean ret = TRUE;
  gchar *xmlcontent = NULL;
  GMarkupParseContext *parsecontext = NULL;
  GstQaScenarioClass *self_class = GST_QA_SCENARIO_GET_CLASS (scenario);
  gchar *uri = gst_filename_to_uri (scenario_file, &err);

  if (uri == NULL)
    goto failed;

  GST_DEBUG ("Trying to load %s", scenario_file);
  if ((file = g_file_new_for_path (scenario_file)) == NULL)
    goto wrong_uri;

  /* TODO Handle GCancellable */
  if (!g_file_load_contents (file, NULL, &xmlcontent, &xmlsize, NULL, &err))
    goto failed;

  if (g_strcmp0 (xmlcontent, "") == 0)
    goto failed;

  parsecontext = g_markup_parse_context_new (&self_class->content_parser,
      G_MARKUP_TREAT_CDATA_AS_TEXT, scenario, NULL);

  if (g_markup_parse_context_parse (parsecontext, xmlcontent, xmlsize,
          &err) == FALSE)
    goto failed;

done:
  if (xmlcontent)
    g_free (xmlcontent);

  if (file)
    gst_object_unref (file);

  if (parsecontext) {
    g_markup_parse_context_free (parsecontext);
    parsecontext = NULL;
  }

  return ret;

wrong_uri:
  GST_WARNING ("%s wrong uri", scenario_file);

  ret = FALSE;
  goto done;

failed:
  ret = FALSE;
  goto done;
}

gboolean
gst_qa_scenario_load (GstQaScenario * scenario, const gchar * scenario_name)
{
  gboolean ret = TRUE;
  gchar *lfilename = NULL, *tldir = NULL;

  if (!scenario_name)
    goto invalid_name;

  lfilename = g_strdup_printf ("%s" GST_QA_SCENARIO_SUFFIX, scenario_name);

  /* Try from local profiles */
  tldir =
      g_build_filename (g_get_user_data_dir (), "gstreamer-" GST_API_VERSION,
      GST_QA_SCERNARIO_DIRECTORY, lfilename, NULL);

  if (!(ret = _load_scenario_file (scenario, tldir))) {
    g_free (tldir);
    /* Try from system-wide profiles */
    tldir = g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION,
        GST_QA_SCERNARIO_DIRECTORY, lfilename, NULL);
    ret = _load_scenario_file (scenario, tldir);
  }

  /* Hack to make it work uninstalled */
  if (ret == FALSE) {
    g_free (tldir);

    tldir = g_build_filename ("data/", lfilename, NULL);
    ret = _load_scenario_file (scenario, tldir);
  }

done:
  if (tldir)
    g_free (tldir);
  if (lfilename)
    g_free (lfilename);

  return ret;

invalid_name:
  {
    GST_ERROR ("Invalid name for scenario '%s'", scenario_name);
    ret = FALSE;
    goto done;
  }
}


static void
gst_qa_scenario_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQaScenarioPrivate *priv = GST_QA_SCENARIO (object)->priv;

  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      priv->runner = g_value_get_object (value);
      break;
    default:
      break;
  }
}

static void
gst_qa_scenario_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQaScenarioPrivate *priv = GST_QA_SCENARIO (object)->priv;

  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      g_value_set_object (value, priv->runner);
      break;
    default:
      break;
  }
}

static void
gst_qa_scenario_class_init (GstQaScenarioClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_qa_scenario, "gstqascenario",
      GST_DEBUG_FG_MAGENTA, "gst qa scenario");

  g_type_class_add_private (klass, sizeof (GstQaScenarioPrivate));

  object_class->dispose = gst_qa_scenario_dispose;
  object_class->finalize = gst_qa_scenario_finalize;

  object_class->get_property = gst_qa_scenario_get_property;
  object_class->set_property = gst_qa_scenario_set_property;

  g_object_class_install_property (object_class, PROP_RUNNER,
      g_param_spec_object ("qa-runner", "QA Runner", "The QA runner to "
          "report errors to", GST_TYPE_QA_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  klass->content_parser.start_element = _parse_element_start;
}

static void
gst_qa_scenario_init (GstQaScenario * scenario)
{
  GstQaScenarioPrivate *priv = scenario->priv =
      GST_QA_SCENARIO_GET_PRIVATE (scenario);


  priv->seeked_position = GST_CLOCK_TIME_NONE;
  priv->seek_pos_tol = DEFAULT_SEEK_TOLERANCE;
}

static void
gst_qa_scenario_dispose (GObject * object)
{
  GstQaScenarioPrivate *priv = GST_QA_SCENARIO (object)->priv;

  gst_object_unref (priv->pipeline);
  g_list_free_full (priv->seeks, (GDestroyNotify) _free_seek_info);

  G_OBJECT_CLASS (gst_qa_scenario_parent_class)->dispose (object);
}

static void
gst_qa_scenario_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_qa_scenario_parent_class)->finalize (object);
}

GstQaScenario *
gst_qa_scenario_factory_create (GstQaRunner * runner,
    const gchar * scenario_name)
{
  GstBus *bus;
  GstQaScenario *scenario = g_object_new (GST_TYPE_QA_SCENARIO, "qa-runner",
      runner, NULL);

  GST_LOG ("Creating scenario %s", scenario_name);
  if (!gst_qa_scenario_load (scenario, scenario_name)) {
    g_object_unref (scenario);

    return NULL;
  }

  scenario->priv->pipeline = gst_object_ref (runner->pipeline);
  gst_qa_reporter_set_name (GST_QA_REPORTER (scenario),
      g_strdup (scenario_name));

  bus = gst_element_get_bus (runner->pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::async-done", (GCallback) async_done_cb,
      scenario);
  gst_object_unref (bus);

  g_timeout_add (50, (GSourceFunc) get_position, scenario);

  g_print ("\n=========================================\n"
      "Running scenario %s on pipeline %s"
      "\n=========================================\n", scenario_name,
      GST_OBJECT_NAME (runner->pipeline));

  return scenario;
}
