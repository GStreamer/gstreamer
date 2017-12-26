/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000 Wim Taymans <wtay@chello.be>
 *               2004 Thomas Vander Stichele <thomas@apestaart.org>
 *
 * gst-inspect.c: tool to inspect the GStreamer registry
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* FIXME 2.0: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "tools.h"
#include <gst/gst_private.h>    /* for internal Factories */

#include <string.h>
#include <locale.h>
#include <glib/gprintf.h>

static char *_name = NULL;
static int indent = 0;

static int print_element_info (GstPluginFeature * feature,
    gboolean print_names);
static int print_typefind_info (GstPluginFeature * feature,
    gboolean print_names);
static int print_tracer_info (GstPluginFeature * feature, gboolean print_names);

#define push_indent() push_indent_n(1)
#define pop_indent() push_indent_n(-1)
#define pop_indent_n(n) push_indent_n(-n)

static void
push_indent_n (int n)
{
  g_assert (n > 0 || indent > 0);
  indent += n;
}

/* *INDENT-OFF* */
G_GNUC_PRINTF (1, 2)
/* *INDENT-ON* */

static void
n_print (const char *format, ...)
{
  va_list args;
  int i;

  if (_name)
    g_print ("%s", _name);

  for (i = 0; i < indent; ++i)
    g_print ("  ");

  va_start (args, format);
  g_vprintf (format, args);
  va_end (args);
}

static gboolean
print_field (GQuark field, const GValue * value, gpointer pfx)
{
  gchar *str = gst_value_serialize (value);

  n_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void
print_caps (const GstCaps * caps, const gchar * pfx)
{
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    n_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    n_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    GstCapsFeatures *features = gst_caps_get_features (caps, i);

    if (features && (gst_caps_features_is_any (features) ||
            !gst_caps_features_is_equal (features,
                GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))) {
      gchar *features_string = gst_caps_features_to_string (features);

      n_print ("%s%s(%s)\n", pfx, gst_structure_get_name (structure),
          features_string);
      g_free (features_string);
    } else {
      n_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    }
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

static const char *
get_rank_name (char *s, gint rank)
{
  static const int ranks[4] = {
    GST_RANK_NONE, GST_RANK_MARGINAL, GST_RANK_SECONDARY, GST_RANK_PRIMARY
  };
  static const char *rank_names[4] = { "none", "marginal", "secondary",
    "primary"
  };
  int i;
  int best_i;

  best_i = 0;
  for (i = 0; i < 4; i++) {
    if (rank == ranks[i])
      return rank_names[i];
    if (abs (rank - ranks[i]) < abs (rank - ranks[best_i])) {
      best_i = i;
    }
  }

  sprintf (s, "%s %c %d", rank_names[best_i],
      (rank - ranks[best_i] > 0) ? '+' : '-', abs (ranks[best_i] - rank));

  return s;
}

static void
print_factory_details_info (GstElementFactory * factory)
{
  gchar **keys, **k;
  GstRank rank;
  char s[20];

  rank = gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE (factory));
  n_print ("Factory Details:\n");

  push_indent ();
  n_print ("%-25s%s (%d)\n", "Rank", get_rank_name (s, rank), rank);

  keys = gst_element_factory_get_metadata_keys (factory);
  if (keys != NULL) {
    for (k = keys; *k != NULL; ++k) {
      const gchar *val;
      gchar *key = *k;

      val = gst_element_factory_get_metadata (factory, key);
      key[0] = g_ascii_toupper (key[0]);
      n_print ("%-25s%s\n", key, val);
    }
    g_strfreev (keys);
  }
  pop_indent ();
  n_print ("\n");
}

static void
print_hierarchy (GType type, gint level, gint * maxlevel)
{
  GType parent;
  gint i;

  parent = g_type_parent (type);

  *maxlevel = *maxlevel + 1;
  level++;

  if (parent)
    print_hierarchy (parent, level, maxlevel);

  if (_name)
    g_print ("%s", _name);

  for (i = 1; i < *maxlevel - level; i++)
    g_print ("      ");
  if (*maxlevel - level)
    g_print (" +----");

  g_print ("%s\n", g_type_name (type));

  if (level == 1)
    n_print ("\n");
}

static void
print_interfaces (GType type)
{
  guint n_ifaces;
  GType *iface, *ifaces = g_type_interfaces (type, &n_ifaces);

  if (ifaces) {
    if (n_ifaces) {
      n_print (_("Implemented Interfaces:\n"));
      push_indent ();
      iface = ifaces;
      while (*iface) {
        n_print ("%s\n", g_type_name (*iface));
        iface++;
      }
      pop_indent ();
      n_print ("\n");
    }
    g_free (ifaces);
  }
}

static gchar *
flags_to_string (GFlagsValue * vals, guint flags)
{
  GString *s = NULL;
  guint flags_left, i;

  /* first look for an exact match and count the number of values */
  for (i = 0; vals[i].value_name != NULL; ++i) {
    if (vals[i].value == flags)
      return g_strdup (vals[i].value_nick);
  }

  s = g_string_new (NULL);

  /* we assume the values are sorted from lowest to highest value */
  flags_left = flags;
  while (i > 0) {
    --i;
    if (vals[i].value != 0 && (flags_left & vals[i].value) == vals[i].value) {
      if (s->len > 0)
        g_string_append_c (s, '+');
      g_string_append (s, vals[i].value_nick);
      flags_left -= vals[i].value;
      if (flags_left == 0)
        break;
    }
  }

  if (s->len == 0)
    g_string_assign (s, "(none)");

  return g_string_free (s, FALSE);
}

#define KNOWN_PARAM_FLAGS \
  (G_PARAM_CONSTRUCT | G_PARAM_CONSTRUCT_ONLY | \
  G_PARAM_LAX_VALIDATION |  G_PARAM_STATIC_STRINGS | \
  G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_DEPRECATED | \
  GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | \
  GST_PARAM_MUTABLE_PAUSED | GST_PARAM_MUTABLE_READY)

/* obj will be NULL if we're printing properties of pad template pads */
static void
print_object_properties_info (GObject * obj, GObjectClass * obj_class,
    const gchar * desc)
{
  GParamSpec **property_specs;
  guint num_properties, i;
  gboolean readable;
  gboolean first_flag;

  property_specs = g_object_class_list_properties (obj_class, &num_properties);
  n_print ("%s:\n", desc);

  push_indent ();

  for (i = 0; i < num_properties; i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];
    GType owner_type = param->owner_type;

    /* We're printing pad properties */
    if (obj == NULL && (owner_type == G_TYPE_OBJECT
            || owner_type == GST_TYPE_OBJECT || owner_type == GST_TYPE_PAD))
      continue;

    readable = FALSE;

    g_value_init (&value, param->value_type);

    n_print ("%-20s: %s\n", g_param_spec_get_name (param),
        g_param_spec_get_blurb (param));

    push_indent_n (11);

    first_flag = TRUE;
    n_print ("flags: ");
    readable = ! !(param->flags & G_PARAM_READABLE);
    if (readable && obj != NULL) {
      g_object_get_property (obj, param->name, &value);
    } else {
      /* if we can't read the property value, assume it's set to the default
       * (which might not be entirely true for sub-classes, but that's an
       * unlikely corner-case anyway) */
      g_param_value_set_default (param, &value);
    }
    if (readable) {
      g_print ("%s%s", (first_flag) ? "" : ", ", _("readable"));
      first_flag = FALSE;
    }
    if (param->flags & G_PARAM_WRITABLE) {
      g_print ("%s%s", (first_flag) ? "" : ", ", _("writable"));
      first_flag = FALSE;
    }
    if (param->flags & G_PARAM_DEPRECATED) {
      g_print ("%s%s", (first_flag) ? "" : ", ", _("deprecated"));
      first_flag = FALSE;
    }
    if (param->flags & GST_PARAM_CONTROLLABLE) {
      g_print (", %s", _("controllable"));
      first_flag = FALSE;
    }
    if (param->flags & GST_PARAM_MUTABLE_PLAYING) {
      g_print (", %s", _("changeable in NULL, READY, PAUSED or PLAYING state"));
    } else if (param->flags & GST_PARAM_MUTABLE_PAUSED) {
      g_print (", %s", _("changeable only in NULL, READY or PAUSED state"));
    } else if (param->flags & GST_PARAM_MUTABLE_READY) {
      g_print (", %s", _("changeable only in NULL or READY state"));
    }
    if (param->flags & ~KNOWN_PARAM_FLAGS) {
      g_print ("%s0x%0x", (first_flag) ? "" : ", ",
          param->flags & ~KNOWN_PARAM_FLAGS);
    }
    g_print ("\n");

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING:
      {
        const char *string_val = g_value_get_string (&value);

        n_print ("String. ");

        if (string_val == NULL)
          g_print ("Default: null");
        else
          g_print ("Default: \"%s\"", string_val);
        break;
      }
      case G_TYPE_BOOLEAN:
      {
        gboolean bool_val = g_value_get_boolean (&value);

        n_print ("Boolean. Default: %s", bool_val ? "true" : "false");
        break;
      }
      case G_TYPE_ULONG:
      {
        GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (param);

        n_print ("Unsigned Long. Range: %lu - %lu Default: %lu ",
            pulong->minimum, pulong->maximum, g_value_get_ulong (&value));

        GST_ERROR ("%s: property '%s' of type ulong: consider changing to "
            "uint/uint64", G_OBJECT_CLASS_NAME (obj_class),
            g_param_spec_get_name (param));
        break;
      }
      case G_TYPE_LONG:
      {
        GParamSpecLong *plong = G_PARAM_SPEC_LONG (param);

        n_print ("Long. Range: %ld - %ld Default: %ld ",
            plong->minimum, plong->maximum, g_value_get_long (&value));

        GST_ERROR ("%s: property '%s' of type long: consider changing to "
            "int/int64", G_OBJECT_CLASS_NAME (obj_class),
            g_param_spec_get_name (param));
        break;
      }
      case G_TYPE_UINT:
      {
        GParamSpecUInt *puint = G_PARAM_SPEC_UINT (param);

        n_print ("Unsigned Integer. Range: %u - %u Default: %u ",
            puint->minimum, puint->maximum, g_value_get_uint (&value));
        break;
      }
      case G_TYPE_INT:
      {
        GParamSpecInt *pint = G_PARAM_SPEC_INT (param);

        n_print ("Integer. Range: %d - %d Default: %d ",
            pint->minimum, pint->maximum, g_value_get_int (&value));
        break;
      }
      case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (param);

        n_print ("Unsigned Integer64. Range: %" G_GUINT64_FORMAT " - "
            "%" G_GUINT64_FORMAT " Default: %" G_GUINT64_FORMAT " ",
            puint64->minimum, puint64->maximum, g_value_get_uint64 (&value));
        break;
      }
      case G_TYPE_INT64:
      {
        GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (param);

        n_print ("Integer64. Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT
            " Default: %" G_GINT64_FORMAT " ",
            pint64->minimum, pint64->maximum, g_value_get_int64 (&value));
        break;
      }
      case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (param);

        n_print ("Float. Range: %15.7g - %15.7g Default: %15.7g ",
            pfloat->minimum, pfloat->maximum, g_value_get_float (&value));
        break;
      }
      case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (param);

        n_print ("Double. Range: %15.7g - %15.7g Default: %15.7g ",
            pdouble->minimum, pdouble->maximum, g_value_get_double (&value));
        break;
      }
      case G_TYPE_CHAR:
      case G_TYPE_UCHAR:
        GST_ERROR ("%s: property '%s' of type char: consider changing to "
            "int/string", G_OBJECT_CLASS_NAME (obj_class),
            g_param_spec_get_name (param));
        /* fall through */
      default:
        if (param->value_type == GST_TYPE_CAPS) {
          const GstCaps *caps = gst_value_get_caps (&value);

          if (!caps)
            n_print ("Caps (NULL)");
          else {
            print_caps (caps, "                           ");
          }
        } else if (G_IS_PARAM_SPEC_ENUM (param)) {
          GEnumValue *values;
          guint j = 0;
          gint enum_value;
          const gchar *value_nick = "";

          values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
          enum_value = g_value_get_enum (&value);

          while (values[j].value_name) {
            if (values[j].value == enum_value)
              value_nick = values[j].value_nick;
            j++;
          }

          n_print ("Enum \"%s\" Default: %d, \"%s\"",
              g_type_name (G_VALUE_TYPE (&value)), enum_value, value_nick);

          j = 0;
          while (values[j].value_name) {
            g_print ("\n");
            n_print ("   (%d): %-16s - %s",
                values[j].value, values[j].value_nick, values[j].value_name);
            j++;
          }
          /* g_type_class_unref (ec); */
        } else if (G_IS_PARAM_SPEC_FLAGS (param)) {
          GParamSpecFlags *pflags = G_PARAM_SPEC_FLAGS (param);
          GFlagsValue *vals;
          gchar *cur;

          vals = pflags->flags_class->values;

          cur = flags_to_string (vals, g_value_get_flags (&value));

          n_print ("Flags \"%s\" Default: 0x%08x, \"%s\"",
              g_type_name (G_VALUE_TYPE (&value)),
              g_value_get_flags (&value), cur);

          while (vals[0].value_name) {
            g_print ("\n");
            n_print ("   (0x%08x): %-16s - %s",
                vals[0].value, vals[0].value_nick, vals[0].value_name);
            ++vals;
          }

          g_free (cur);
        } else if (G_IS_PARAM_SPEC_OBJECT (param)) {
          n_print ("Object of type \"%s\"", g_type_name (param->value_type));
        } else if (G_IS_PARAM_SPEC_BOXED (param)) {
          n_print ("Boxed pointer of type \"%s\"",
              g_type_name (param->value_type));
          if (param->value_type == GST_TYPE_STRUCTURE) {
            const GstStructure *s = gst_value_get_structure (&value);
            if (s)
              gst_structure_foreach (s, print_field,
                  (gpointer) "                           ");
          }
        } else if (G_IS_PARAM_SPEC_POINTER (param)) {
          if (param->value_type != G_TYPE_POINTER) {
            n_print ("Pointer of type \"%s\".",
                g_type_name (param->value_type));
          } else {
            n_print ("Pointer.");
          }
        } else if (param->value_type == G_TYPE_VALUE_ARRAY) {
          GParamSpecValueArray *pvarray = G_PARAM_SPEC_VALUE_ARRAY (param);

          if (pvarray->element_spec) {
            n_print ("Array of GValues of type \"%s\"",
                g_type_name (pvarray->element_spec->value_type));
          } else {
            n_print ("Array of GValues");
          }
        } else if (GST_IS_PARAM_SPEC_FRACTION (param)) {
          GstParamSpecFraction *pfraction = GST_PARAM_SPEC_FRACTION (param);

          n_print ("Fraction. Range: %d/%d - %d/%d Default: %d/%d ",
              pfraction->min_num, pfraction->min_den,
              pfraction->max_num, pfraction->max_den,
              gst_value_get_fraction_numerator (&value),
              gst_value_get_fraction_denominator (&value));
        } else if (param->value_type == GST_TYPE_ARRAY) {
          GstParamSpecArray *parray = GST_PARAM_SPEC_ARRAY_LIST (param);

          if (parray->element_spec) {
            n_print ("GstValueArray of GValues of type \"%s\"",
                g_type_name (parray->element_spec->value_type));
          } else {
            n_print ("GstValueArray of GValues");
          }
        } else {
          n_print ("Unknown type %ld \"%s\"",
              (glong) param->value_type, g_type_name (param->value_type));
        }
        break;
    }
    if (!readable)
      g_print (" Write only\n");
    else
      g_print ("\n");

    pop_indent_n (11);

    g_value_reset (&value);
  }
  if (num_properties == 0)
    n_print ("none\n");

  pop_indent ();

  g_free (property_specs);
}

static void
print_element_properties_info (GstElement * element)
{
  g_print ("\n");
  print_object_properties_info (G_OBJECT (element),
      G_OBJECT_GET_CLASS (element), "Element Properties");
}

static void
print_pad_templates_info (GstElement * element, GstElementFactory * factory)
{
  const GList *pads;
  GstStaticPadTemplate *padtemplate;
  GstPadTemplate *tmpl;

  n_print ("Pad Templates:\n");

  push_indent ();

  if (gst_element_factory_get_num_pad_templates (factory) == 0) {
    n_print ("none\n");
    goto done;
  }

  pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    padtemplate = (GstStaticPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    if (padtemplate->direction == GST_PAD_SRC)
      n_print ("SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      n_print ("SINK template: '%s'\n", padtemplate->name_template);
    else
      n_print ("UNKNOWN template: '%s'\n", padtemplate->name_template);

    push_indent ();

    if (padtemplate->presence == GST_PAD_ALWAYS)
      n_print ("Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      n_print ("Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST) {
      n_print ("Availability: On request\n");
    } else
      n_print ("Availability: UNKNOWN\n");

    if (padtemplate->static_caps.string) {
      GstCaps *caps = gst_static_caps_get (&padtemplate->static_caps);

      n_print ("Capabilities:\n");

      push_indent ();
      print_caps (caps, "");    // FIXME
      pop_indent ();

      gst_caps_unref (caps);
    }

    tmpl = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (element),
        padtemplate->name_template);
    if (tmpl != NULL) {
      GType pad_type = GST_PAD_TEMPLATE_GTYPE (tmpl);

      if (pad_type != G_TYPE_NONE && pad_type != GST_TYPE_PAD) {
        gpointer pad_klass;

        pad_klass = g_type_class_ref (pad_type);
        n_print ("Type: %s\n", g_type_name (pad_type));
        print_object_properties_info (NULL, pad_klass, "Pad Properties");
        g_type_class_unref (pad_klass);
      }
    }

    pop_indent ();

    if (pads != NULL)
      n_print ("\n");
  }

done:
  pop_indent ();
}

static void
print_clocking_info (GstElement * element)
{
  gboolean requires_clock, provides_clock;

  requires_clock =
      GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
  provides_clock =
      GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_FLAG_PROVIDE_CLOCK);

  if (!requires_clock && !provides_clock) {
    n_print ("\n");
    n_print ("Element has no clocking capabilities.\n");
    return;
  }

  n_print ("\n");
  n_print ("Clocking Interaction:\n");

  push_indent ();

  if (requires_clock) {
    n_print ("element requires a clock\n");
  }

  if (provides_clock) {
    GstClock *clock;

    clock = gst_element_get_clock (element);
    if (clock) {
      n_print ("element provides a clock: %s\n", GST_OBJECT_NAME (clock));
      gst_object_unref (clock);
    } else
      n_print ("element is supposed to provide a clock but returned NULL\n");
  }

  pop_indent ();
}

static void
print_uri_handler_info (GstElement * element)
{
  if (GST_IS_URI_HANDLER (element)) {
    const gchar *const *uri_protocols;
    const gchar *uri_type;

    if (gst_uri_handler_get_uri_type (GST_URI_HANDLER (element)) == GST_URI_SRC)
      uri_type = "source";
    else if (gst_uri_handler_get_uri_type (GST_URI_HANDLER (element)) ==
        GST_URI_SINK)
      uri_type = "sink";
    else
      uri_type = "unknown";

    uri_protocols = gst_uri_handler_get_protocols (GST_URI_HANDLER (element));

    n_print ("\n");
    n_print ("URI handling capabilities:\n");

    push_indent ();

    n_print ("Element can act as %s.\n", uri_type);

    if (uri_protocols && *uri_protocols) {
      n_print ("Supported URI protocols:\n");
      push_indent ();
      for (; *uri_protocols != NULL; uri_protocols++)
        n_print ("%s\n", *uri_protocols);
      pop_indent ();
    } else {
      n_print ("No supported URI protocols\n");
    }

    pop_indent ();
  } else {
    n_print ("Element has no URI handling capabilities.\n");
  }
}

static void
print_pad_info (GstElement * element)
{
  const GList *pads;
  GstPad *pad;

  n_print ("\n");
  n_print ("Pads:\n");

  push_indent ();

  if (!element->numpads) {
    n_print ("none\n");
    goto done;
  }

  pads = element->pads;
  while (pads) {
    gchar *name;
    GstCaps *caps;

    pad = GST_PAD (pads->data);
    pads = g_list_next (pads);

    name = gst_pad_get_name (pad);
    if (gst_pad_get_direction (pad) == GST_PAD_SRC)
      n_print ("SRC: '%s'\n", name);
    else if (gst_pad_get_direction (pad) == GST_PAD_SINK)
      n_print ("SINK: '%s'\n", name);
    else
      n_print ("UNKNOWN: '%s'\n", name);

    g_free (name);

    if (pad->padtemplate) {
      push_indent ();
      n_print ("Pad Template: '%s'\n", pad->padtemplate->name_template);
      pop_indent ();
    }

    caps = gst_pad_get_current_caps (pad);
    if (caps) {
      n_print ("Capabilities:\n");
      push_indent ();
      print_caps (caps, "");    // FIXME
      pop_indent ();
      gst_caps_unref (caps);
    }
  }

done:
  pop_indent ();
}

static gboolean
has_sometimes_template (GstElement * element)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GList *l;

  for (l = klass->padtemplates; l != NULL; l = l->next) {
    if (GST_PAD_TEMPLATE (l->data)->presence == GST_PAD_SOMETIMES)
      return TRUE;
  }

  return FALSE;
}

static gboolean
gtype_needs_ptr_marker (GType type)
{
  if (type == G_TYPE_POINTER)
    return FALSE;

  if (G_TYPE_FUNDAMENTAL (type) == G_TYPE_POINTER || G_TYPE_IS_BOXED (type)
      || G_TYPE_IS_OBJECT (type))
    return TRUE;

  return FALSE;
}

static void
print_signal_info (GstElement * element)
{
  /* Signals/Actions Block */
  guint *signals;
  guint nsignals;
  gint i = 0, j, k;
  GSignalQuery *query = NULL;
  GType type;
  GSList *found_signals, *l;

  for (k = 0; k < 2; k++) {
    found_signals = NULL;

    /* For elements that have sometimes pads, also list a few useful GstElement
     * signals. Put these first, so element-specific ones come later. */
    if (k == 0 && has_sometimes_template (element)) {
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (g_signal_lookup ("pad-added", GST_TYPE_ELEMENT), query);
      found_signals = g_slist_append (found_signals, query);
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (g_signal_lookup ("pad-removed", GST_TYPE_ELEMENT), query);
      found_signals = g_slist_append (found_signals, query);
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (g_signal_lookup ("no-more-pads", GST_TYPE_ELEMENT),
          query);
      found_signals = g_slist_append (found_signals, query);
    }

    for (type = G_OBJECT_TYPE (element); type; type = g_type_parent (type)) {
      if (type == GST_TYPE_ELEMENT || type == GST_TYPE_OBJECT)
        break;

      if (type == GST_TYPE_BIN && G_OBJECT_TYPE (element) != GST_TYPE_BIN)
        continue;

      signals = g_signal_list_ids (type, &nsignals);
      for (i = 0; i < nsignals; i++) {
        query = g_new0 (GSignalQuery, 1);
        g_signal_query (signals[i], query);

        if ((k == 0 && !(query->signal_flags & G_SIGNAL_ACTION)) ||
            (k == 1 && (query->signal_flags & G_SIGNAL_ACTION)))
          found_signals = g_slist_append (found_signals, query);
        else
          g_free (query);
      }
      g_free (signals);
      signals = NULL;
    }

    if (found_signals) {
      n_print ("\n");
      if (k == 0)
        n_print ("Element Signals:\n");
      else
        n_print ("Element Actions:\n");
    } else {
      continue;
    }

    for (l = found_signals; l; l = l->next) {
      gchar *indent;
      const gchar *pmark;
      int indent_len;

      query = (GSignalQuery *) l->data;
      indent_len = strlen (query->signal_name) +
          strlen (g_type_name (query->return_type)) + 24;

      if (gtype_needs_ptr_marker (query->return_type)) {
        pmark = "* ";
        indent_len += 2;
      } else {
        pmark = "";
      }

      indent = g_new0 (gchar, indent_len + 1);
      memset (indent, ' ', indent_len);

      n_print ("  \"%s\" :  %s %suser_function (%s* object",
          query->signal_name, g_type_name (query->return_type), pmark,
          g_type_name (type));

      for (j = 0; j < query->n_params; j++) {
        const gchar *type_name, *asterisk;

        type_name = g_type_name (query->param_types[j]);
        asterisk = gtype_needs_ptr_marker (query->param_types[j]) ? "*" : "";

        g_print (",\n");
        n_print ("%s%s%s arg%d", indent, type_name, asterisk, j);
      }

      if (k == 0) {
        g_print (",\n");
        n_print ("%sgpointer user_data);\n", indent);
      } else
        g_print (");\n");

      g_free (indent);
    }

    if (found_signals) {
      g_slist_foreach (found_signals, (GFunc) g_free, NULL);
      g_slist_free (found_signals);
    }
  }
}

static void
print_children_info (GstElement * element)
{
  GList *children;

  if (!GST_IS_BIN (element))
    return;

  children = (GList *) GST_BIN (element)->children;
  if (children) {
    n_print ("\n");
    n_print ("Children:\n");
  }

  while (children) {
    n_print ("  %s\n", GST_ELEMENT_NAME (GST_ELEMENT (children->data)));
    children = g_list_next (children);
  }
}

static void
print_preset_list (GstElement * element)
{
  gchar **presets, **preset;

  if (!GST_IS_PRESET (element))
    return;

  presets = gst_preset_get_preset_names (GST_PRESET (element));
  if (presets && *presets) {
    n_print ("\n");
    n_print ("Presets:\n");
    for (preset = presets; *preset; preset++) {
      n_print ("  \"%s\"\n", *preset);
    }
    g_strfreev (presets);
  }
}

static void
print_blacklist (void)
{
  GList *plugins, *cur;
  gint count = 0;

  g_print ("%s\n", _("Blacklisted files:"));

  plugins = gst_registry_get_plugin_list (gst_registry_get ());
  for (cur = plugins; cur != NULL; cur = g_list_next (cur)) {
    GstPlugin *plugin = (GstPlugin *) (cur->data);
    if (GST_OBJECT_FLAG_IS_SET (plugin, GST_PLUGIN_FLAG_BLACKLISTED)) {
      g_print ("  %s\n", gst_plugin_get_name (plugin));
      count++;
    }
  }

  g_print ("\n");
  g_print (_("Total count: "));
  g_print (ngettext ("%d blacklisted file", "%d blacklisted files", count),
      count);
  g_print ("\n");
  gst_plugin_list_free (plugins);
}

static void
print_typefind_extensions (const gchar * const *extensions)
{
  guint i = 0;

  while (extensions[i]) {
    g_print ("%s%s", i > 0 ? ", " : "", extensions[i]);
    i++;
  }
}

static void
print_element_list (gboolean print_all, gchar * ftypes)
{
  int plugincount = 0, featurecount = 0, blacklistcount = 0;
  GList *plugins, *orig_plugins;
  gchar **types = NULL;

  if (ftypes) {
    gint i;

    types = g_strsplit (ftypes, "/", -1);
    for (i = 0; types[i]; i++)
      *types[i] = g_ascii_toupper (*types[i]);

  }

  orig_plugins = plugins = gst_registry_get_plugin_list (gst_registry_get ());
  while (plugins) {
    GList *features, *orig_features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);
    plugins = g_list_next (plugins);
    plugincount++;

    if (GST_OBJECT_FLAG_IS_SET (plugin, GST_PLUGIN_FLAG_BLACKLISTED)) {
      blacklistcount++;
      continue;
    }

    orig_features = features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get (),
        gst_plugin_get_name (plugin));
    while (features) {
      GstPluginFeature *feature;

      if (G_UNLIKELY (features->data == NULL))
        goto next;
      feature = GST_PLUGIN_FEATURE (features->data);
      featurecount++;

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        const gchar *klass;
        GstElementFactory *factory;

        factory = GST_ELEMENT_FACTORY (feature);
        if (types) {
          gint i;
          gboolean all_found = TRUE;

          klass =
              gst_element_factory_get_metadata (factory,
              GST_ELEMENT_METADATA_KLASS);
          for (i = 0; types[i]; i++) {
            if (!strstr (klass, types[i])) {
              all_found = FALSE;
              break;
            }
          }

          if (!all_found)
            goto next;
        }
        if (print_all)
          print_element_info (feature, TRUE);
        else
          g_print ("%s:  %s: %s\n", gst_plugin_get_name (plugin),
              GST_OBJECT_NAME (factory),
              gst_element_factory_get_metadata (factory,
                  GST_ELEMENT_METADATA_LONGNAME));
      } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
        GstTypeFindFactory *factory;
        const gchar *const *extensions;

        if (types)
          goto next;
        factory = GST_TYPE_FIND_FACTORY (feature);
        if (!print_all)
          g_print ("%s: %s: ", gst_plugin_get_name (plugin),
              gst_plugin_feature_get_name (feature));

        extensions = gst_type_find_factory_get_extensions (factory);
        if (extensions != NULL) {
          if (!print_all) {
            print_typefind_extensions (extensions);
            g_print ("\n");
          }
        } else {
          if (!print_all)
            g_print ("no extensions\n");
        }
      } else {
        if (types)
          goto next;
        if (!print_all)
          n_print ("%s:  %s (%s)\n", gst_plugin_get_name (plugin),
              GST_OBJECT_NAME (feature), g_type_name (G_OBJECT_TYPE (feature)));
      }

    next:
      features = g_list_next (features);
    }

    gst_plugin_feature_list_free (orig_features);
  }

  gst_plugin_list_free (orig_plugins);
  g_strfreev (types);

  g_print ("\n");
  g_print (_("Total count: "));
  g_print (ngettext ("%d plugin", "%d plugins", plugincount), plugincount);
  if (blacklistcount) {
    g_print (" (");
    g_print (ngettext ("%d blacklist entry", "%d blacklist entries",
            blacklistcount), blacklistcount);
    g_print (" not shown)");
  }
  g_print (", ");
  g_print (ngettext ("%d feature", "%d features", featurecount), featurecount);
  g_print ("\n");
}

static void
print_all_uri_handlers (void)
{
  GList *plugins, *p, *features, *f;

  plugins = gst_registry_get_plugin_list (gst_registry_get ());

  for (p = plugins; p; p = p->next) {
    GstPlugin *plugin = (GstPlugin *) (p->data);

    features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get (),
        gst_plugin_get_name (plugin));

    for (f = features; f; f = f->next) {
      GstPluginFeature *feature = GST_PLUGIN_FEATURE (f->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;
        GstElement *element;

        factory = GST_ELEMENT_FACTORY (gst_plugin_feature_load (feature));
        if (!factory) {
          g_print ("element plugin %s couldn't be loaded\n",
              gst_plugin_get_name (plugin));
          continue;
        }

        element = gst_element_factory_create (factory, NULL);
        if (!element) {
          g_print ("couldn't construct element for %s for some reason\n",
              GST_OBJECT_NAME (factory));
          gst_object_unref (factory);
          continue;
        }

        if (GST_IS_URI_HANDLER (element)) {
          const gchar *const *uri_protocols;
          const gchar *dir;
          gchar *joined;

          switch (gst_uri_handler_get_uri_type (GST_URI_HANDLER (element))) {
            case GST_URI_SRC:
              dir = "read";
              break;
            case GST_URI_SINK:
              dir = "write";
              break;
            default:
              dir = "unknown";
              break;
          }

          uri_protocols =
              gst_uri_handler_get_protocols (GST_URI_HANDLER (element));
          joined = g_strjoinv (", ", (gchar **) uri_protocols);

          g_print ("%s (%s, rank %u): %s\n",
              gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)), dir,
              gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE (factory)),
              joined);

          g_free (joined);
        }

        gst_object_unref (element);
        gst_object_unref (factory);
      }
    }

    gst_plugin_feature_list_free (features);
  }

  gst_plugin_list_free (plugins);
}

static void
print_plugin_info (GstPlugin * plugin)
{
  const gchar *release_date = gst_plugin_get_release_date_string (plugin);
  const gchar *filename = gst_plugin_get_filename (plugin);

  n_print ("Plugin Details:\n");

  push_indent ();

  n_print ("%-25s%s\n", "Name", gst_plugin_get_name (plugin));
  n_print ("%-25s%s\n", "Description", gst_plugin_get_description (plugin));
  n_print ("%-25s%s\n", "Filename", (filename != NULL) ? filename : "(null)");
  n_print ("%-25s%s\n", "Version", gst_plugin_get_version (plugin));
  n_print ("%-25s%s\n", "License", gst_plugin_get_license (plugin));
  n_print ("%-25s%s\n", "Source module", gst_plugin_get_source (plugin));

  if (release_date != NULL) {
    const gchar *tz = "(UTC)";
    gchar *str, *sep;

    /* may be: YYYY-MM-DD or YYYY-MM-DDTHH:MMZ */
    /* YYYY-MM-DDTHH:MMZ => YYYY-MM-DD HH:MM (UTC) */
    str = g_strdup (release_date);
    sep = strstr (str, "T");
    if (sep != NULL) {
      *sep = ' ';
      sep = strstr (sep + 1, "Z");
      if (sep != NULL)
        *sep = ' ';
    } else {
      tz = "";
    }
    n_print ("%-25s%s%s\n", "Source release date", str, tz);
    g_free (str);
  }
  n_print ("%-25s%s\n", "Binary package", gst_plugin_get_package (plugin));
  n_print ("%-25s%s\n", "Origin URL", gst_plugin_get_origin (plugin));

  pop_indent ();

  n_print ("\n");
}

static void
print_plugin_features (GstPlugin * plugin)
{
  GList *features, *origlist;
  gint num_features = 0;
  gint num_elements = 0;
  gint num_tracers = 0;
  gint num_typefinders = 0;
  gint num_devproviders = 0;
  gint num_other = 0;

  origlist = features =
      gst_registry_get_feature_list_by_plugin (gst_registry_get (),
      gst_plugin_get_name (plugin));

  while (features) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (features->data);

    if (GST_IS_ELEMENT_FACTORY (feature)) {
      GstElementFactory *factory;

      factory = GST_ELEMENT_FACTORY (feature);
      n_print ("  %s: %s\n", GST_OBJECT_NAME (factory),
          gst_element_factory_get_metadata (factory,
              GST_ELEMENT_METADATA_LONGNAME));
      num_elements++;
    } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
      GstTypeFindFactory *factory;
      const gchar *const *extensions;

      factory = GST_TYPE_FIND_FACTORY (feature);
      extensions = gst_type_find_factory_get_extensions (factory);
      if (extensions) {
        guint i = 0;

        g_print ("  %s: %s: ", gst_plugin_get_name (plugin),
            gst_plugin_feature_get_name (feature));
        while (extensions[i]) {
          g_print ("%s%s", i > 0 ? ", " : "", extensions[i]);
          i++;
        }
        g_print ("\n");
      } else
        g_print ("  %s: %s: no extensions\n", gst_plugin_get_name (plugin),
            gst_plugin_feature_get_name (feature));

      num_typefinders++;
    } else if (GST_IS_DEVICE_PROVIDER_FACTORY (feature)) {
      GstDeviceProviderFactory *factory;

      factory = GST_DEVICE_PROVIDER_FACTORY (feature);
      n_print ("  %s: %s\n", GST_OBJECT_NAME (factory),
          gst_device_provider_factory_get_metadata (factory,
              GST_ELEMENT_METADATA_LONGNAME));
      num_devproviders++;
    } else if (GST_IS_TRACER_FACTORY (feature)) {
      n_print ("  %s (%s)\n", gst_object_get_name (GST_OBJECT (feature)),
          g_type_name (G_OBJECT_TYPE (feature)));
      num_tracers++;
    } else if (feature) {
      n_print ("  %s (%s)\n", gst_object_get_name (GST_OBJECT (feature)),
          g_type_name (G_OBJECT_TYPE (feature)));
      num_other++;
    }
    num_features++;
    features = g_list_next (features);
  }

  gst_plugin_feature_list_free (origlist);

  n_print ("\n");
  n_print ("  %d features:\n", num_features);
  if (num_elements > 0)
    n_print ("  +-- %d elements\n", num_elements);
  if (num_typefinders > 0)
    n_print ("  +-- %d typefinders\n", num_typefinders);
  if (num_devproviders > 0)
    n_print ("  +-- %d device providers\n", num_devproviders);
  if (num_tracers > 0)
    n_print ("  +-- %d tracers\n", num_tracers);
  if (num_other > 0)
    n_print ("  +-- %d other objects\n", num_other);

  n_print ("\n");
}

static int
print_feature_info (const gchar * feature_name, gboolean print_all)
{
  GstPluginFeature *feature;
  GstRegistry *registry = gst_registry_get ();
  int ret;

  if ((feature = gst_registry_find_feature (registry, feature_name,
              GST_TYPE_ELEMENT_FACTORY))) {
    ret = print_element_info (feature, print_all);
    goto handled;
  }
  if ((feature = gst_registry_find_feature (registry, feature_name,
              GST_TYPE_TYPE_FIND_FACTORY))) {
    ret = print_typefind_info (feature, print_all);
    goto handled;
  }
  if ((feature = gst_registry_find_feature (registry, feature_name,
              GST_TYPE_TRACER_FACTORY))) {
    ret = print_tracer_info (feature, print_all);
    goto handled;
  }

  /* TODO: handle DEVICE_PROVIDER_FACTORY */

  return -1;

handled:
  gst_object_unref (feature);
  return ret;
}

static int
print_element_info (GstPluginFeature * feature, gboolean print_names)
{
  GstElementFactory *factory;
  GstElement *element;
  GstPlugin *plugin;
  gint maxlevel = 0;

  factory = GST_ELEMENT_FACTORY (gst_plugin_feature_load (feature));
  if (!factory) {
    g_print ("element plugin couldn't be loaded\n");
    return -1;
  }

  element = gst_element_factory_create (factory, NULL);
  if (!element) {
    gst_object_unref (factory);
    g_print ("couldn't construct element for some reason\n");
    return -1;
  }

  if (print_names)
    _name = g_strdup_printf ("%s: ", GST_OBJECT_NAME (factory));
  else
    _name = NULL;

  print_factory_details_info (factory);

  plugin = gst_plugin_feature_get_plugin (GST_PLUGIN_FEATURE (factory));
  if (plugin) {
    print_plugin_info (plugin);
    gst_object_unref (plugin);
  }

  print_hierarchy (G_OBJECT_TYPE (element), 0, &maxlevel);
  print_interfaces (G_OBJECT_TYPE (element));

  print_pad_templates_info (element, factory);
  print_clocking_info (element);
  print_uri_handler_info (element);
  print_pad_info (element);
  print_element_properties_info (element);
  print_signal_info (element);
  print_children_info (element);
  print_preset_list (element);

  gst_object_unref (element);
  gst_object_unref (factory);
  g_free (_name);
  return 0;
}

static int
print_typefind_info (GstPluginFeature * feature, gboolean print_names)
{
  GstTypeFindFactory *factory;
  GstPlugin *plugin;
  GstCaps *caps;
  GstRank rank;
  char s[20];
  const gchar *const *extensions;

  factory = GST_TYPE_FIND_FACTORY (gst_plugin_feature_load (feature));
  if (!factory) {
    g_print ("typefind plugin couldn't be loaded\n");
    return -1;
  }

  if (print_names)
    _name = g_strdup_printf ("%s: ", GST_OBJECT_NAME (factory));
  else
    _name = NULL;

  n_print ("Factory Details:\n");
  rank = gst_plugin_feature_get_rank (feature);
  n_print ("  %-25s%s (%d)\n", "Rank", get_rank_name (s, rank), rank);
  n_print ("  %-25s%s\n", "Name", GST_OBJECT_NAME (factory));
  caps = gst_type_find_factory_get_caps (factory);
  if (caps) {
    gchar *caps_str = gst_caps_to_string (factory->caps);

    n_print ("  %-25s%s\n", "Caps", caps_str);
    g_free (caps_str);
  }
  extensions = gst_type_find_factory_get_extensions (factory);
  if (extensions) {
    n_print ("  %-25s", "Extensions");
    print_typefind_extensions (extensions);
    n_print ("\n");
  }
  n_print ("\n");

  plugin = gst_plugin_feature_get_plugin (GST_PLUGIN_FEATURE (factory));
  if (plugin) {
    print_plugin_info (plugin);
    gst_object_unref (plugin);
  }

  gst_object_unref (factory);
  g_free (_name);
  return 0;
}

static int
print_tracer_info (GstPluginFeature * feature, gboolean print_names)
{
  GstTracerFactory *factory;
  GstTracer *tracer;
  GstPlugin *plugin;
  gint maxlevel = 0;

  factory = GST_TRACER_FACTORY (gst_plugin_feature_load (feature));
  if (!factory) {
    g_print ("tracer plugin couldn't be loaded\n");
    return -1;
  }

  tracer = (GstTracer *) g_object_new (factory->type, NULL);
  if (!tracer) {
    gst_object_unref (factory);
    g_print ("couldn't construct tracer for some reason\n");
    return -1;
  }

  if (print_names)
    _name = g_strdup_printf ("%s: ", GST_OBJECT_NAME (factory));
  else
    _name = NULL;

  n_print ("Factory Details:\n");
  n_print ("  %-25s%s\n", "Name", GST_OBJECT_NAME (factory));
  n_print ("\n");

  plugin = gst_plugin_feature_get_plugin (GST_PLUGIN_FEATURE (factory));
  if (plugin) {
    print_plugin_info (plugin);
    gst_object_unref (plugin);
  }

  print_hierarchy (G_OBJECT_TYPE (tracer), 0, &maxlevel);
  print_interfaces (G_OBJECT_TYPE (tracer));

  /* TODO: list what hooks it registers
   * - the data is available in gsttracerutils, we need to iterate the
   *   _priv_tracers hashtable for each probe and then check the list of hooks
   *  for each probe whether hook->tracer == tracer :/
   */

  /* TODO: list what records it emits
   * - in class_init tracers can create GstTracerRecord instances
   * - those only get logged right now and there is no association with the
   *   tracer that created them
   * - we'd need to add them to GstTracerFactory
   *   gst_tracer_class_add_record (klass, record);
   *   - needs work in gstregistrychunks to (de)serialize specs
   *   - gst_tracer_register() would need to iterate the list of records and
   *     copy the record->spec into the factory
   */

  gst_object_unref (tracer);
  gst_object_unref (factory);
  g_free (_name);
  return 0;
}

static void
print_plugin_automatic_install_info_codecs (GstElementFactory * factory)
{
  GstPadDirection direction;
  const gchar *type_name;
  const gchar *klass;
  const GList *static_templates, *l;
  GstCaps *caps = NULL;
  guint i, num;

  klass =
      gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);
  g_return_if_fail (klass != NULL);

  if (strstr (klass, "Demuxer") ||
      strstr (klass, "Decoder") ||
      strstr (klass, "Depay") || strstr (klass, "Parser")) {
    type_name = "decoder";
    direction = GST_PAD_SINK;
  } else if (strstr (klass, "Muxer") ||
      strstr (klass, "Encoder") || strstr (klass, "Pay")) {
    type_name = "encoder";
    direction = GST_PAD_SRC;
  } else {
    return;
  }

  /* decoder/demuxer sink pads should always be static and there should only
   * be one, the same applies to encoders/muxers and source pads */
  static_templates = gst_element_factory_get_static_pad_templates (factory);
  for (l = static_templates; l != NULL; l = l->next) {
    GstStaticPadTemplate *tmpl = NULL;

    tmpl = (GstStaticPadTemplate *) l->data;
    if (tmpl->direction == direction) {
      caps = gst_static_pad_template_get_caps (tmpl);
      break;
    }
  }

  if (caps == NULL) {
    g_printerr ("Couldn't find static pad template for %s '%s'\n",
        type_name, GST_OBJECT_NAME (factory));
    return;
  }

  caps = gst_caps_make_writable (caps);
  num = gst_caps_get_size (caps);
  for (i = 0; i < num; ++i) {
    GstStructure *s;
    gchar *s_str;

    s = gst_caps_get_structure (caps, i);
    /* remove fields that are almost always just MIN-MAX of some sort
     * in order to make the caps look less messy */
    gst_structure_remove_field (s, "pixel-aspect-ratio");
    gst_structure_remove_field (s, "framerate");
    gst_structure_remove_field (s, "channels");
    gst_structure_remove_field (s, "width");
    gst_structure_remove_field (s, "height");
    gst_structure_remove_field (s, "rate");
    gst_structure_remove_field (s, "depth");
    gst_structure_remove_field (s, "clock-rate");
    s_str = gst_structure_to_string (s);
    g_print ("%s-%s\n", type_name, s_str);
    g_free (s_str);
  }
  gst_caps_unref (caps);
}

static void
print_plugin_automatic_install_info_protocols (GstElementFactory * factory)
{
  const gchar *const *protocols;

  protocols = gst_element_factory_get_uri_protocols (factory);
  if (protocols != NULL && *protocols != NULL) {
    switch (gst_element_factory_get_uri_type (factory)) {
      case GST_URI_SINK:
        while (*protocols != NULL) {
          g_print ("urisink-%s\n", *protocols);
          ++protocols;
        }
        break;
      case GST_URI_SRC:
        while (*protocols != NULL) {
          g_print ("urisource-%s\n", *protocols);
          ++protocols;
        }
        break;
      default:
        break;
    }
  }
}

static void
print_plugin_automatic_install_info (GstPlugin * plugin)
{
  GList *features, *l;

  /* not interested in typefind factories, only element factories */
  features = gst_registry_get_feature_list (gst_registry_get (),
      GST_TYPE_ELEMENT_FACTORY);

  for (l = features; l != NULL; l = l->next) {
    GstPluginFeature *feature;
    GstPlugin *feature_plugin;

    feature = GST_PLUGIN_FEATURE (l->data);

    /* only interested in the ones that are in the plugin we just loaded */
    feature_plugin = gst_plugin_feature_get_plugin (feature);
    if (feature_plugin == plugin) {
      GstElementFactory *factory;

      g_print ("element-%s\n", gst_plugin_feature_get_name (feature));

      factory = GST_ELEMENT_FACTORY (feature);
      print_plugin_automatic_install_info_protocols (factory);
      print_plugin_automatic_install_info_codecs (factory);
    }
    if (feature_plugin)
      gst_object_unref (feature_plugin);
  }

  g_list_foreach (features, (GFunc) gst_object_unref, NULL);
  g_list_free (features);
}

static void
print_all_plugin_automatic_install_info (void)
{
  GList *plugins, *orig_plugins;

  orig_plugins = plugins = gst_registry_get_plugin_list (gst_registry_get ());
  while (plugins) {
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);
    plugins = g_list_next (plugins);

    print_plugin_automatic_install_info (plugin);
  }
  gst_plugin_list_free (orig_plugins);
}

int
main (int argc, char *argv[])
{
  gboolean print_all = FALSE;
  gboolean do_print_blacklist = FALSE;
  gboolean plugin_name = FALSE;
  gboolean print_aii = FALSE;
  gboolean uri_handlers = FALSE;
  gboolean check_exists = FALSE;
  gchar *min_version = NULL;
  guint minver_maj = GST_VERSION_MAJOR;
  guint minver_min = GST_VERSION_MINOR;
  guint minver_micro = 0;
  gchar *types = NULL;
#ifndef GST_DISABLE_OPTION_PARSING
  GOptionEntry options[] = {
    {"print-all", 'a', 0, G_OPTION_ARG_NONE, &print_all,
        N_("Print all elements"), NULL},
    {"print-blacklist", 'b', 0, G_OPTION_ARG_NONE, &do_print_blacklist,
        N_("Print list of blacklisted files"), NULL},
    {"print-plugin-auto-install-info", '\0', 0, G_OPTION_ARG_NONE, &print_aii,
        N_("Print a machine-parsable list of features the specified plugin "
              "or all plugins provide.\n                                       "
              "Useful in connection with external automatic plugin "
              "installation mechanisms"), NULL},
    {"plugin", '\0', 0, G_OPTION_ARG_NONE, &plugin_name,
        N_("List the plugin contents"), NULL},
    {"types", 't', 0, G_OPTION_ARG_STRING, &types,
        N_("A slashes ('/') separated list of types of elements (also known "
              "as klass) to list. (unordered)"), NULL},
    {"exists", '\0', 0, G_OPTION_ARG_NONE, &check_exists,
        N_("Check if the specified element or plugin exists"), NULL},
    {"atleast-version", '\0', 0, G_OPTION_ARG_STRING, &min_version,
        N_
          ("When checking if an element or plugin exists, also check that its "
              "version is at least the version specified"), NULL},
    {"uri-handlers", 'u', 0, G_OPTION_ARG_NONE, &uri_handlers,
          N_
          ("Print supported URI schemes, with the elements that implement them"),
        NULL},
    GST_TOOLS_GOPTION_VERSION,
    {NULL}
  };
  GOptionContext *ctx;
  GError *err = NULL;
#endif

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  /* avoid glib warnings when inspecting deprecated properties */
  g_setenv ("G_ENABLE_DIAGNOSTIC", "0", FALSE);

  g_set_prgname ("gst-inspect-" GST_API_VERSION);

#ifndef GST_DISABLE_OPTION_PARSING
  ctx = g_option_context_new ("[ELEMENT-NAME | PLUGIN-NAME]");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_clear_error (&err);
    g_option_context_free (ctx);
    return -1;
  }
  g_option_context_free (ctx);
#else
  gst_init (&argc, &argv);
#endif

  gst_tools_print_version ();

  if (print_all && argc > 1) {
    g_printerr ("-a requires no extra arguments\n");
    return -1;
  }

  if (uri_handlers && argc > 1) {
    g_printerr ("-u requires no extra arguments\n");
    return -1;
  }

  /* --atleast-version implies --exists */
  if (min_version != NULL) {
    if (sscanf (min_version, "%u.%u.%u", &minver_maj, &minver_min,
            &minver_micro) < 2) {
      g_printerr ("Can't parse version '%s' passed to --atleast-version\n",
          min_version);
      return -1;
    }
    check_exists = TRUE;
  }

  if (check_exists) {
    int exit_code;

    if (argc == 1) {
      g_printerr ("--exists requires an extra command line argument\n");
      exit_code = -1;
    } else {
      if (!plugin_name) {
        GstPluginFeature *feature;

        feature = gst_registry_lookup_feature (gst_registry_get (), argv[1]);
        if (feature != NULL && gst_plugin_feature_check_version (feature,
                minver_maj, minver_min, minver_micro)) {
          exit_code = 0;
        } else {
          exit_code = 1;
        }

        if (feature)
          gst_object_unref (feature);
      } else {
        /* FIXME: support checking for plugins too */
        g_printerr ("Checking for plugins is not supported yet\n");
        exit_code = -1;
      }
    }
    return exit_code;
  }

  /* if no arguments, print out list of elements */
  if (uri_handlers) {
    print_all_uri_handlers ();
  } else if (argc == 1 || print_all) {
    if (do_print_blacklist)
      print_blacklist ();
    else {
      if (print_aii)
        print_all_plugin_automatic_install_info ();
      else
        print_element_list (print_all, types);
    }
  } else {
    /* else we try to get a factory */
    const char *arg = argv[argc - 1];
    int retval = -1;

    if (!plugin_name) {
      retval = print_feature_info (arg, print_all);
    }

    /* otherwise check if it's a plugin */
    if (retval) {
      GstPlugin *plugin = gst_registry_find_plugin (gst_registry_get (), arg);

      /* if there is such a plugin, print out info */
      if (plugin) {
        if (print_aii) {
          print_plugin_automatic_install_info (plugin);
        } else {
          print_plugin_info (plugin);
          print_plugin_features (plugin);
        }
      } else {
        GError *error = NULL;

        if (g_file_test (arg, G_FILE_TEST_EXISTS)) {
          plugin = gst_plugin_load_file (arg, &error);

          if (plugin) {
            if (print_aii) {
              print_plugin_automatic_install_info (plugin);
            } else {
              print_plugin_info (plugin);
              print_plugin_features (plugin);
            }
          } else {
            g_printerr (_("Could not load plugin file: %s\n"), error->message);
            g_clear_error (&error);
            return -1;
          }
        } else {
          g_printerr (_("No such element or plugin '%s'\n"), arg);
          return -1;
        }
      }
    }
  }

  return 0;
}
