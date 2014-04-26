/* GStreamer
 * Copyright (C) <2009> Руслан Ижбулатов <lrn1986 _at_ gmail _dot_ com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

/**
 * SECTION:element-measurecollector
 *
 * This plugin collects measurements from measuring elemtns and calculates
 * total measure for the whole sequence and also outputs measurements to a file
 * <classname>&quot;GstMeasureCollector&quot;</classname>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include "gstvideomeasure_collector.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <gst/video/video.h>

/* GstMeasureCollector signals and args */

enum
{
  PROP_0,
  PROP_FLAGS,
  PROP_FILENAME
};

GST_DEBUG_CATEGORY_STATIC (measure_collector_debug);
#define GST_CAT_DEFAULT measure_collector_debug

static GstStaticPadTemplate gst_measure_collector_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_measure_collector_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

//static GstBaseTransformClass *parent_class = NULL;

static void gst_measure_collector_finalize (GObject * object);
static gboolean gst_measure_collector_event (GstBaseTransform * base,
    GstEvent * event);
static void gst_measure_collector_save_csv (GstMeasureCollector * mc);

static void gst_measure_collector_post_message (GstMeasureCollector * mc);

GST_BOILERPLATE (GstMeasureCollector, gst_measure_collector, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_measure_collector_collect (GstMeasureCollector * mc, GstEvent * gstevent)
{
  const GstStructure *str;
  const gchar *event, *metric;
  guint64 framenumber = G_MAXUINT64;
  const GValue *framenumber_v;

  str = gst_event_get_structure (gstevent);

  event = gst_structure_get_string (str, "event");
  metric = gst_structure_get_string (str, "metric");

  if (strcmp (event, "frame-measured") == 0 && metric != NULL) {
    GstStructure *cpy;
    cpy = gst_structure_copy (str);

    framenumber_v = gst_structure_get_value (str, "offset");
    if (framenumber_v) {
      if (G_VALUE_TYPE (framenumber_v) == G_TYPE_UINT64)
        framenumber = g_value_get_uint64 (framenumber_v);
      else if (G_VALUE_TYPE (framenumber_v) == G_TYPE_INT64)
        framenumber = g_value_get_int64 (framenumber_v);
    }

    if (framenumber == G_MAXUINT64)
      framenumber = mc->nextoffset++;

    if (mc->measurements->len <= framenumber)
      g_ptr_array_set_size (mc->measurements, framenumber + 1);
    g_ptr_array_index (mc->measurements, framenumber) = cpy;

    mc->nextoffset = framenumber + 1;

    if (!mc->metric)
      mc->metric = g_strdup (metric);
  }
}

static void
gst_measure_collector_post_message (GstMeasureCollector * mc)
{
  GstMessage *m;
  guint64 i;

  g_return_if_fail (mc->metric);

  if (strcmp (mc->metric, "SSIM") == 0) {
    gfloat dresult = 0;
    guint64 mlen;
    g_free (mc->result);
    mc->result = g_new0 (GValue, 1);
    g_value_init (mc->result, G_TYPE_FLOAT);
    mlen = mc->measurements->len;
    for (i = 0; i < mc->measurements->len; i++) {
      const GValue *v;
      GstStructure *str =
          (GstStructure *) g_ptr_array_index (mc->measurements, i);
      if (str) {
        v = gst_structure_get_value (str, "mean");
        dresult += g_value_get_float (v);
      } else {
        GST_WARNING_OBJECT (mc,
            "No measurement info for frame %" G_GUINT64_FORMAT, i);
        mlen--;
      }
    }
    g_value_set_float (mc->result, dresult / mlen);
  }

  m = gst_message_new_element (GST_OBJECT_CAST (mc),
      gst_structure_new ("GstMeasureCollector",
          "measure-result", G_TYPE_VALUE, mc->result, NULL));

  gst_element_post_message (GST_ELEMENT_CAST (mc), m);
}

static void
gst_measure_collector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMeasureCollector *measurecollector;

  measurecollector = GST_MEASURE_COLLECTOR (object);

  switch (prop_id) {
    case PROP_FLAGS:
      measurecollector->flags = g_value_get_uint64 (value);
      break;
    case PROP_FILENAME:
      measurecollector->filename = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_measure_collector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMeasureCollector *measurecollector;

  measurecollector = GST_MEASURE_COLLECTOR (object);

  switch (prop_id) {
    case PROP_FLAGS:
      g_value_set_uint64 (value, measurecollector->flags);
      break;
    case PROP_FILENAME:
      g_value_set_string (value, measurecollector->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_measure_collector_event (GstBaseTransform * base, GstEvent * event)
{
  GstMeasureCollector *mc = GST_MEASURE_COLLECTOR (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      if (gst_event_has_name (event, GST_EVENT_VIDEO_MEASURE))
        gst_measure_collector_collect (mc, event);
      break;
    case GST_EVENT_EOS:
      gst_measure_collector_post_message (mc);
      gst_measure_collector_save_csv (mc);
      break;
    default:
      break;
  }

  return parent_class->event (base, event);
}

static void
gst_measure_collector_save_csv (GstMeasureCollector * mc)
{
  gchar *name_local;
  FILE *file;
  guint64 i, j;
  GstStructure *str;
  GValue tmp = { 0 };
  g_value_init (&tmp, G_TYPE_STRING);

  if (!(mc->flags & GST_MEASURE_COLLECTOR_WRITE_CSV))
    return;

  if (mc->measurements->len <= 0)
    goto empty;

  /* open the file */
  if (mc->filename == NULL || mc->filename[0] == '\0')
    goto no_filename;

  name_local = g_filename_from_utf8 ((const gchar *) mc->filename,
      -1, NULL, NULL, NULL);

  /* open the file */
  if (name_local == NULL || name_local[0] == '\0')
    goto not_good_filename;


  /* FIXME, can we use g_fopen here? some people say that the FILE object is
   * local to the .so that performed the fopen call, which would not be us when
   * we use g_fopen. */
  file = fopen (name_local, "wb");

  g_free (name_local);

  if (file == NULL)
    goto open_failed;

  str = (GstStructure *) g_ptr_array_index (mc->measurements, 0);

  for (j = 0; j < gst_structure_n_fields (str); j++) {
    const gchar *fieldname;
    fieldname = gst_structure_nth_field_name (str, j);
    if (G_LIKELY (j > 0))
      fprintf (file, ";");
    fprintf (file, "%s", fieldname);
  }

  for (i = 0; i < mc->measurements->len; i++) {
    fprintf (file, "\n");
    str = (GstStructure *) g_ptr_array_index (mc->measurements, i);
    if (str != NULL) {
      for (j = 0; j < gst_structure_n_fields (str); j++) {
        const gchar *fieldname;
        fieldname = gst_structure_nth_field_name (str, j);
        if (G_LIKELY (j > 0))
          fprintf (file, ";");
        if (G_LIKELY (g_value_transform (gst_structure_get_value (str,
                        fieldname), &tmp)))
          fprintf (file, "%s", g_value_get_string (&tmp));
        else
          fprintf (file, "<untranslatable>");
      }
    }
  }

  fclose (file);

  /* ERRORS */
empty:
  {
    return;
  }
no_filename:
  {
    GST_ELEMENT_ERROR (mc, RESOURCE, NOT_FOUND,
        (_("No file name specified for writing.")), (NULL));
    return;
  }
not_good_filename:
  {
    GST_ELEMENT_ERROR (mc, RESOURCE, NOT_FOUND,
        (_("Given file name \"%s\" can't be converted to local file name \
encoding."), mc->filename), (NULL));
    return;
  }
open_failed:
  {
    GST_ELEMENT_ERROR (mc, RESOURCE, OPEN_WRITE,
        (_("Could not open file \"%s\" for writing."), mc->filename),
        GST_ERROR_SYSTEM);
    return;
  }
}

static void
gst_measure_collector_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_static_metadata (element_class,
      "Video measure collector", "Filter/Effect/Video",
      "Collect measurements from a measuring element",
      "Руслан Ижбулатов <lrn _at_ gmail _dot_ com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_measure_collector_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_measure_collector_src_template));
}

static void
gst_measure_collector_class_init (GstMeasureCollectorClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "measurecollect", 0,
      "measurement collector");

  gobject_class->set_property = gst_measure_collector_set_property;
  gobject_class->get_property = gst_measure_collector_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_measure_collector_finalize);

  g_object_class_install_property (gobject_class, PROP_FLAGS,
      g_param_spec_uint64 ("flags", "Flags",
          "Flags that control the operation of the element",
          0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FILENAME,
      g_param_spec_string ("filename", "Output file name",
          "A name of a file into which element will write the measurement"
          " information", "",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  trans_class->event = GST_DEBUG_FUNCPTR (gst_measure_collector_event);

  trans_class->passthrough_on_same_caps = TRUE;

}

static void
gst_measure_collector_init (GstMeasureCollector * instance,
    GstMeasureCollectorClass * g_class)
{
  GstMeasureCollector *measurecollector;

  measurecollector = GST_MEASURE_COLLECTOR (instance);

  GST_DEBUG_OBJECT (measurecollector, "gst_measure_collector_init");

  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (measurecollector),
      FALSE);

  measurecollector->measurements = g_ptr_array_new ();
  measurecollector->metric = NULL;
  measurecollector->inited = TRUE;
  measurecollector->filename = NULL;
  measurecollector->flags = 0;
  measurecollector->nextoffset = 0;
  measurecollector->result = NULL;
}

static void
gst_measure_collector_finalize (GObject * object)
{
  gint i;
  GstMeasureCollector *mc = GST_MEASURE_COLLECTOR (object);

  for (i = 0; i < mc->measurements->len; i++) {
    if (g_ptr_array_index (mc->measurements, i) != NULL)
      gst_structure_free ((GstStructure *) g_ptr_array_index (mc->measurements,
              i));
  }

  g_ptr_array_free (mc->measurements, TRUE);
  mc->measurements = NULL;

  g_free (mc->result);
  mc->result = NULL;

  g_free (mc->metric);
  mc->metric = NULL;

  g_free (mc->filename);
  mc->filename = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}
