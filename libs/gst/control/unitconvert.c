/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * unitconvert.c: Conversion between units of measurement
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "unitconvert.h"
#include <gst/gstinfo.h>

static GHashTable *_gst_units;
static GHashTable *_gst_unit_domain_defaults;
static gboolean _gst_unitconv_init_done = FALSE;

typedef struct _GstUnit GstUnit;

struct _GstUnit
{
  GParamSpec *unit_spec;
  const gchar *domain_name;
  gboolean domain_default;
  gboolean logarithmic;
  GHashTable *convert_to_funcs;
  GSList *convert_paramspecs;
};

static void gst_unitconv_add_core_converters (void);

static void gst_unitconv_class_init (GstUnitConvertClass * klass);
static void gst_unitconv_init (GstUnitConvert * unitconv);
static void gst_unitconv_dispose (GObject * object);

GType
gst_unitconv_get_type (void)
{
  static GType unitconv_type = 0;

  if (!unitconv_type) {
    static const GTypeInfo unitconv_info = {
      sizeof (GstUnitConvertClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_unitconv_class_init,
      NULL,
      NULL,
      sizeof (GstUnitConvert),
      0,
      (GInstanceInitFunc) gst_unitconv_init,
    };

    unitconv_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstUnitConvert",
        &unitconv_info, 0);
  }
  return unitconv_type;
}

static void
gst_unitconv_class_init (GstUnitConvertClass * klass)
{
  GObjectClass *gobject_class;
  GstUnitConvertClass *unitconv_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  unitconv_class = (GstUnitConvertClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  gobject_class->dispose = gst_unitconv_dispose;

}

static void
gst_unitconv_init (GstUnitConvert * unitconv)
{
  g_return_if_fail (unitconv != NULL);

  unitconv->convert_func_chain = NULL;
  unitconv->convert_params = g_hash_table_new (g_str_hash, g_str_equal);
}

GstUnitConvert *
gst_unitconv_new ()
{
  GstUnitConvert *unitconv;

  unitconv = g_object_new (gst_unitconv_get_type (), NULL);

  return unitconv;
}

static void
gst_unitconv_init_for_change_state (GstUnitConvert * unitconv)
{
  unitconv->convert_func_chain = NULL;
}

gboolean
gst_unitconv_set_convert_units (GstUnitConvert * unitconv,
    gchar * from_unit_named, gchar * to_unit_named)
{
  GHashTable *convert_funcs;
  GstUnit *from_unit, *to_unit;
  GstUnitConvertFunc convert_func;

  g_return_val_if_fail (unitconv != NULL, FALSE);
  g_return_val_if_fail (from_unit_named != NULL, FALSE);
  g_return_val_if_fail (to_unit_named != NULL, FALSE);
  g_return_val_if_fail (GST_IS_UNIT_CONVERT (unitconv), FALSE);

  from_unit = g_hash_table_lookup (_gst_units, from_unit_named);
  to_unit = g_hash_table_lookup (_gst_units, to_unit_named);

  g_return_val_if_fail (from_unit != NULL, FALSE);
  g_return_val_if_fail (to_unit != NULL, FALSE);

  convert_funcs = from_unit->convert_to_funcs;

  convert_func = g_hash_table_lookup (convert_funcs, to_unit);
  if (convert_func == NULL) {
    g_warning ("cannot convert from %s to %s", from_unit_named, to_unit_named);
  }

  gst_unitconv_init_for_change_state (unitconv);

  unitconv->convert_func_chain =
      g_slist_append (unitconv->convert_func_chain, convert_func);


  return TRUE;
}

gboolean
gst_unitconv_convert_value (GstUnitConvert * unitconv, GValue * from_value,
    GValue * to_value)
{
  GstUnitConvertFunc convert_func;
  GSList *convert_func_chain;

  g_return_val_if_fail (unitconv->convert_func_chain != NULL, FALSE);

  /* only do this until we can chain convert funcs */
  g_return_val_if_fail (g_slist_length (unitconv->convert_func_chain) == 1,
      FALSE);

  convert_func_chain = unitconv->convert_func_chain;

  convert_func = (GstUnitConvertFunc) (convert_func_chain);
  convert_func (unitconv, from_value, to_value);

  return TRUE;
}

gboolean
gst_unitconv_unit_exists (gchar * unit_name)
{
  g_return_val_if_fail (unit_name != NULL, FALSE);
  return (g_hash_table_lookup (_gst_units, unit_name) != NULL);
}

gboolean
gst_unitconv_unit_is_logarithmic (gchar * unit_name)
{
  GstUnit *unit;

  g_return_val_if_fail (unit_name != NULL, FALSE);

  unit = g_hash_table_lookup (_gst_units, unit_name);
  g_return_val_if_fail (unit != NULL, FALSE);

  return unit->logarithmic;
}

GParamSpec *
gst_unitconv_unit_spec (gchar * unit_name)
{
  GstUnit *unit;

  g_return_val_if_fail (unit_name != NULL, FALSE);

  unit = g_hash_table_lookup (_gst_units, unit_name);
  g_return_val_if_fail (unit != NULL, FALSE);

  return unit->unit_spec;
}

static void
gst_unitconv_dispose (GObject * object)
{

}


void
_gst_unitconv_initialize (void)
{
  if (_gst_unitconv_init_done)
    return;

  _gst_unitconv_init_done = TRUE;

  _gst_units = g_hash_table_new (g_str_hash, g_str_equal);
  _gst_unit_domain_defaults = g_hash_table_new (g_str_hash, g_str_equal);

  /* frequency based units */

  gst_unitconv_register_unit ("frequency", TRUE, TRUE,
      g_param_spec_float ("hertz", "Hz", "Frequency in hertz",
          0, G_MAXFLOAT, 0, 0));

  gst_unitconv_register_unit ("frequency", FALSE, TRUE,
      g_param_spec_float ("hertz-rate-bound", "Hz",
          "Frequency in hertz, bound by the sample rate", 0.0, G_MAXFLOAT, 0.0,
          0));

  gst_unitconv_register_unit ("frequency", FALSE, FALSE,
      g_param_spec_string ("twelve-tone-scale", "note",
          "Name of the note from the western twelve tone scale", "C", 0));

  gst_unitconv_register_unit ("frequency", FALSE, FALSE,
      g_param_spec_int ("midi-note", "midi note",
          "MIDI note value of the frequency", 1, 127, 1, 0));

  /* time based units */
  gst_unitconv_register_unit ("time", TRUE, FALSE,
      g_param_spec_float ("seconds", "s", "Time in seconds",
          -G_MAXFLOAT, G_MAXFLOAT, 0, 0));

  gst_unitconv_register_unit ("time", FALSE, FALSE,
      g_param_spec_int64 ("nanoseconds", "ns", "Time in nanoseconds",
          G_MININT64, G_MAXINT64, 0, 0));

  gst_unitconv_register_unit ("time", FALSE, FALSE,
      g_param_spec_int64 ("samples", "samples", "Time in number of samples",
          G_MININT64, G_MAXINT64, 0, 0));

  gst_unitconv_register_convert_property ("samples",
      g_param_spec_int ("samplerate", "samplerate", "samplerate",
          0, G_MAXINT, 0, G_PARAM_READWRITE));


  /* magnitude based units */
  gst_unitconv_register_unit ("magnitude", TRUE, FALSE,
      g_param_spec_float ("scalar", "scalar", "Magnitude as a scalar",
          -G_MAXFLOAT, G_MAXFLOAT, 0, 0));

  gst_unitconv_register_unit ("magnitude", FALSE, FALSE,
      g_param_spec_int ("scalar-int", "scalar int",
          "Magnitude as an integer scalar", G_MININT, G_MAXINT, 0, 0));

  gst_unitconv_register_unit ("magnitude", FALSE, TRUE,
      g_param_spec_float ("decibel", "dB", "Magnitude in decibels",
          -G_MAXFLOAT, G_MAXFLOAT, 0, 0));

  gst_unitconv_register_unit ("magnitude", FALSE, FALSE,
      g_param_spec_float ("percent", "%", "Magnitude in percent",
          -G_MAXFLOAT, G_MAXFLOAT, 0, 0));

  /* generic units */
  gst_unitconv_register_unit ("float_default", TRUE, FALSE,
      g_param_spec_float ("float", "float", "Float value",
          -G_MAXFLOAT, G_MAXFLOAT, 0, 0));

  gst_unitconv_register_unit ("int_default", TRUE, FALSE,
      g_param_spec_int ("int", "int", "Integer value",
          G_MININT, G_MAXINT, 0, 0));

  gst_unitconv_register_unit ("int64_default", TRUE, FALSE,
      g_param_spec_int64 ("int64", "int64", "64 bit integer value",
          G_MININT, G_MAXINT, 0, 0));


  gst_unitconv_add_core_converters ();

}

gboolean
gst_unitconv_register_unit (const gchar * domain_name,
    gboolean is_domain_default, gboolean is_logarithmic, GParamSpec * unit_spec)
{
  GstUnit *unit;
  gchar *unit_name;

  g_return_val_if_fail (unit_spec != NULL, FALSE);
  g_return_val_if_fail (G_IS_PARAM_SPEC (unit_spec), FALSE);
  g_return_val_if_fail (domain_name != NULL, FALSE);

  unit_name = g_strdup (g_param_spec_get_name (unit_spec));

  /* check if this unit name already exists */
  if (g_hash_table_lookup (_gst_units, unit_name) != NULL) {
    g_free (unit_name);
    return FALSE;
  }
  if (is_domain_default) {
    /* check if an default unit already exists for this domain */
    g_return_val_if_fail (g_hash_table_lookup (_gst_unit_domain_defaults,
            domain_name) == NULL, FALSE);
  }

  GST_DEBUG ("creating unit: %s", unit_name);

  unit = g_new0 (GstUnit, 1);

  unit->unit_spec = unit_spec;
  unit->domain_name = domain_name;
  unit->domain_default = is_domain_default;
  unit->logarithmic = is_logarithmic;
  unit->convert_to_funcs = g_hash_table_new (NULL, NULL);
  /* unit->convert_properties = g_hash_table_new(g_str_hash,g_str_equal); */

  g_hash_table_insert (_gst_units, unit_name, unit);

  if (is_domain_default) {
    g_hash_table_insert (_gst_unit_domain_defaults, g_strdup (domain_name),
        unit);
  }

  return TRUE;
}

gboolean
gst_unitconv_register_convert_func (gchar * from_unit_named,
    gchar * to_unit_named, GstUnitConvertFunc convert_func)
{
  GHashTable *convert_funcs;
  GstUnit *from_unit, *to_unit;

  g_return_val_if_fail (from_unit_named != NULL, FALSE);
  g_return_val_if_fail (to_unit_named != NULL, FALSE);

  from_unit = g_hash_table_lookup (_gst_units, from_unit_named);
  to_unit = g_hash_table_lookup (_gst_units, to_unit_named);

  g_return_val_if_fail (from_unit != NULL, FALSE);
  g_return_val_if_fail (to_unit != NULL, FALSE);

  convert_funcs = from_unit->convert_to_funcs;

  g_return_val_if_fail (g_hash_table_lookup (convert_funcs, to_unit) == NULL,
      FALSE);

  GST_DEBUG ("adding unit converter from %s to %s\n",
      g_param_spec_get_name (from_unit->unit_spec),
      g_param_spec_get_name (to_unit->unit_spec));

  g_hash_table_insert (convert_funcs, to_unit, convert_func);

  return TRUE;
}

gboolean
gst_unitconv_register_convert_property (gchar * unit_name,
    GParamSpec * convert_prop_spec)
{
  GstUnit *unit;

  g_return_val_if_fail (unit_name != NULL, FALSE);
  g_return_val_if_fail (convert_prop_spec != NULL, FALSE);
  unit = g_hash_table_lookup (_gst_units, unit_name);

  g_return_val_if_fail (unit != NULL, FALSE);

  unit->convert_paramspecs =
      g_slist_append (unit->convert_paramspecs, convert_prop_spec);

  return TRUE;
}

static void
gst_unitconv_time_seconds_to_nanoseconds (GstUnitConvert * unitconv,
    GValue * seconds_val, GValue * nanos_val)
{

  g_value_set_int64 (nanos_val,
      (gint64) (g_value_get_float (seconds_val) * 1000000000.0));
}

static void
gst_unitconv_time_nanoseconds_to_seconds (GstUnitConvert * unitconv,
    GValue * nanos_val, GValue * seconds_val)
{
  g_value_set_float (seconds_val,
      ((gfloat) g_value_get_int64 (nanos_val)) / 1000000000.0);
}

static void
gst_unitconv_time_seconds_to_samples (GstUnitConvert * unitconv,
    GValue * seconds_val, GValue * samples_val)
{
  /* GValue *samplerate;
     GValue *samplerate = g_hash_table_lookup(unitconv->currentToUnit->convert_properties, "samplerate");
     g_value_set_int64(samples_val,
     (gint64)(g_value_get_float(seconds_val) * (gfloat)g_value_get_int(samplerate))); */
}

static void
gst_unitconv_time_samples_to_seconds (GstUnitConvert * unitconv,
    GValue * samples_val, GValue * seconds_val)
{
  /* GValue *samplerate;
     GValue *samplerate = g_hash_table_lookup(unitconv->currentFromUnit->convert_properties, "samplerate"); 
     g_value_set_float(seconds_val,
     ((gfloat)g_value_get_int64(samples_val)) / (gfloat)g_value_get_int(samplerate)); */
}

static void
gst_unitconv_magnitude_scalar_to_percent (GstUnitConvert * unitconv,
    GValue * scalar_val, GValue * percent_val)
{
  g_value_set_float (percent_val, g_value_get_float (scalar_val) * 100.0);
}

static void
gst_unitconv_magnitude_percent_to_scalar (GstUnitConvert * unitconv,
    GValue * percent_val, GValue * scalar_val)
{
  g_value_set_float (scalar_val, g_value_get_float (percent_val) / 100.0);
}

static void
gst_unitconv_add_core_converters (void)
{

  gst_unitconv_register_convert_func ("nanoseconds", "seconds",
      gst_unitconv_time_nanoseconds_to_seconds);
  gst_unitconv_register_convert_func ("seconds", "nanoseconds",
      gst_unitconv_time_seconds_to_nanoseconds);
  gst_unitconv_register_convert_func ("seconds", "samples",
      gst_unitconv_time_seconds_to_samples);
  gst_unitconv_register_convert_func ("samples", "seconds",
      gst_unitconv_time_samples_to_seconds);

  gst_unitconv_register_convert_func ("scalar", "percent",
      gst_unitconv_magnitude_scalar_to_percent);
  gst_unitconv_register_convert_func ("percent", "scalar",
      gst_unitconv_magnitude_percent_to_scalar);
}
