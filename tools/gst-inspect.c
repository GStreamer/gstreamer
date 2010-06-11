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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/controller/gstcontroller.h>

#include "tools.h"

#include <string.h>
#include <locale.h>
#include <glib/gprintf.h>

static char *_name = NULL;

static int print_element_info (GstElementFactory * factory,
    gboolean print_names);

static void
n_print (const char *format, ...)
{
  va_list args;

  if (_name)
    g_print ("%s", _name);

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

    n_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

#if 0
static void
print_formats (const GstFormat * formats)
{
  while (formats && *formats) {
    const GstFormatDefinition *definition;

    definition = gst_format_get_details (*formats);
    if (definition)
      n_print ("\t\t(%d):\t%s (%s)\n", *formats,
          definition->nick, definition->description);
    else
      n_print ("\t\t(%d):\tUnknown format\n", *formats);

    formats++;
  }
}
#endif

static void
print_query_types (const GstQueryType * types)
{
  while (types && *types) {
    const GstQueryTypeDefinition *definition;

    definition = gst_query_type_get_details (*types);
    if (definition)
      n_print ("\t\t(%d):\t%s (%s)\n", *types,
          definition->nick, definition->description);
    else
      n_print ("\t\t(%d):\tUnknown query format\n", *types);

    types++;
  }
}

#if 0
static void
print_event_masks (const GstEventMask * masks)
{
  GType event_type;
  GEnumClass *klass;
  GType event_flags;
  GFlagsClass *flags_class = NULL;

  event_type = gst_event_type_get_type ();
  klass = (GEnumClass *) g_type_class_ref (event_type);

  while (masks && masks->type) {
    GEnumValue *value;
    gint flags = 0, index = 0;

    switch (masks->type) {
      case GST_EVENT_SEEK:
        flags = masks->flags;
        event_flags = gst_seek_type_get_type ();
        flags_class = (GFlagsClass *) g_type_class_ref (event_flags);
        break;
      default:
        break;
    }

    value = g_enum_get_value (klass, masks->type);
    g_print ("\t\t%s ", value->value_nick);

    while (flags) {
      GFlagsValue *value;

      if (flags & 1) {
        value = g_flags_get_first_value (flags_class, 1 << index);

        if (value)
          g_print ("| %s ", value->value_nick);
        else
          g_print ("| ? ");
      }
      flags >>= 1;
      index++;
    }
    g_print ("\n");

    masks++;
  }
}
#endif

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
  char s[20];

  n_print ("Factory Details:\n");
  n_print ("  Long name:\t%s\n", factory->details.longname);
  n_print ("  Class:\t%s\n", factory->details.klass);
  n_print ("  Description:\t%s\n", factory->details.description);
  n_print ("  Author(s):\t%s\n", factory->details.author);
  n_print ("  Rank:\t\t%s (%d)\n",
      get_rank_name (s, GST_PLUGIN_FEATURE (factory)->rank),
      GST_PLUGIN_FEATURE (factory)->rank);
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
      if (_name)
        g_print ("%s", _name);
      g_print (_("Implemented Interfaces:\n"));
      iface = ifaces;
      while (*iface) {
        if (_name)
          g_print ("%s", _name);
        g_print ("  %s\n", g_type_name (*iface));
        iface++;
      }
      if (_name)
        g_print ("%s", _name);
      g_print ("\n");
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
        g_string_append (s, " | ");
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

static void
print_element_properties_info (GstElement * element)
{
  GParamSpec **property_specs;
  guint num_properties, i;
  gboolean readable;
  gboolean first_flag;

  property_specs = g_object_class_list_properties
      (G_OBJECT_GET_CLASS (element), &num_properties);
  n_print ("\n");
  n_print ("Element Properties:\n");

  for (i = 0; i < num_properties; i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];

    readable = FALSE;

    g_value_init (&value, param->value_type);

    n_print ("  %-20s: %s\n", g_param_spec_get_name (param),
        g_param_spec_get_blurb (param));

    first_flag = TRUE;
    n_print ("%-23.23s flags: ", "");
    if (param->flags & G_PARAM_READABLE) {
      g_object_get_property (G_OBJECT (element), param->name, &value);
      readable = TRUE;
      if (!first_flag)
        g_print (", ");
      else
        first_flag = FALSE;
      g_print (_("readable"));
    }
    if (param->flags & G_PARAM_WRITABLE) {
      if (!first_flag)
        g_print (", ");
      else
        first_flag = FALSE;
      g_print (_("writable"));
    }
    if (param->flags & GST_PARAM_CONTROLLABLE) {
      if (!first_flag)
        g_print (", ");
      g_print (_("controllable"));
    }
    n_print ("\n");

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING:
      {
        GParamSpecString *pstring = G_PARAM_SPEC_STRING (param);

        n_print ("%-23.23s String. ", "");

        if (pstring->default_value == NULL)
          g_print ("Default: null ");
        else
          g_print ("Default: \"%s\" ", pstring->default_value);

        if (readable) {
          const char *string_val = g_value_get_string (&value);

          if (string_val == NULL)
            g_print ("Current: null");
          else
            g_print ("Current: \"%s\"", string_val);
        }
        break;
      }
      case G_TYPE_BOOLEAN:
      {
        GParamSpecBoolean *pboolean = G_PARAM_SPEC_BOOLEAN (param);

        n_print ("%-23.23s Boolean. ", "");
        g_print ("Default: %s ", (pboolean->default_value ? "true" : "false"));
        if (readable)
          g_print ("Current: %s",
              (g_value_get_boolean (&value) ? "true" : "false"));
        break;
      }
      case G_TYPE_ULONG:
      {
        GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (param);

        n_print ("%-23.23s Unsigned Long. ", "");
        g_print ("Range: %lu - %lu Default: %lu ",
            pulong->minimum, pulong->maximum, pulong->default_value);
        if (readable)
          g_print ("Current: %lu", g_value_get_ulong (&value));
        break;
      }
      case G_TYPE_LONG:
      {
        GParamSpecLong *plong = G_PARAM_SPEC_LONG (param);

        n_print ("%-23.23s Long. ", "");
        g_print ("Range: %ld - %ld Default: %ld ",
            plong->minimum, plong->maximum, plong->default_value);
        if (readable)
          g_print ("Current: %ld", g_value_get_long (&value));
        break;
      }
      case G_TYPE_UINT:
      {
        GParamSpecUInt *puint = G_PARAM_SPEC_UINT (param);

        n_print ("%-23.23s Unsigned Integer. ", "");
        g_print ("Range: %u - %u Default: %u ",
            puint->minimum, puint->maximum, puint->default_value);
        if (readable)
          g_print ("Current: %u", g_value_get_uint (&value));
        break;
      }
      case G_TYPE_INT:
      {
        GParamSpecInt *pint = G_PARAM_SPEC_INT (param);

        n_print ("%-23.23s Integer. ", "");
        g_print ("Range: %d - %d Default: %d ",
            pint->minimum, pint->maximum, pint->default_value);
        if (readable)
          g_print ("Current: %d", g_value_get_int (&value));
        break;
      }
      case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (param);

        n_print ("%-23.23s Unsigned Integer64. ", "");
        g_print ("Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT
            " Default: %" G_GUINT64_FORMAT " ",
            puint64->minimum, puint64->maximum, puint64->default_value);
        if (readable)
          g_print ("Current: %" G_GUINT64_FORMAT, g_value_get_uint64 (&value));
        break;
      }
      case G_TYPE_INT64:
      {
        GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (param);

        n_print ("%-23.23s Integer64. ", "");
        g_print ("Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT
            " Default: %" G_GINT64_FORMAT " ",
            pint64->minimum, pint64->maximum, pint64->default_value);
        if (readable)
          g_print ("Current: %" G_GINT64_FORMAT, g_value_get_int64 (&value));
        break;
      }
      case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (param);

        n_print ("%-23.23s Float. ", "");
        g_print ("Range: %15.7g - %15.7g Default: %15.7g ",
            pfloat->minimum, pfloat->maximum, pfloat->default_value);
        if (readable)
          g_print ("Current: %15.7g", g_value_get_float (&value));
        break;
      }
      case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (param);

        n_print ("%-23.23s Double. ", "");
        g_print ("Range: %15.7g - %15.7g Default: %15.7g ",
            pdouble->minimum, pdouble->maximum, pdouble->default_value);
        if (readable)
          g_print ("Current: %15.7g", g_value_get_double (&value));
        break;
      }
      default:
        if (param->value_type == GST_TYPE_CAPS) {
          const GstCaps *caps = gst_value_get_caps (&value);

          if (!caps)
            n_print ("%-23.23s Caps (NULL)", "");
          else {
            print_caps (caps, "                           ");
          }
        } else if (G_IS_PARAM_SPEC_ENUM (param)) {
          GParamSpecEnum *penum = G_PARAM_SPEC_ENUM (param);
          GEnumValue *values;
          guint j = 0;
          gint enum_value;
          const gchar *def_val_nick = "", *cur_val_nick = "";

          values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
          enum_value = g_value_get_enum (&value);

          while (values[j].value_name) {
            if (values[j].value == enum_value)
              cur_val_nick = values[j].value_nick;
            if (values[j].value == penum->default_value)
              def_val_nick = values[j].value_nick;
            j++;
          }

          n_print
              ("%-23.23s Enum \"%s\" Default: %d, \"%s\" Current: %d, \"%s\"",
              "", g_type_name (G_VALUE_TYPE (&value)), penum->default_value,
              def_val_nick, enum_value, cur_val_nick);

          j = 0;
          while (values[j].value_name) {
            g_print ("\n");
            if (_name)
              g_print ("%s", _name);
            g_print ("%-23.23s    (%d): %-16s - %s", "",
                values[j].value, values[j].value_nick, values[j].value_name);
            j++;
          }
          /* g_type_class_unref (ec); */
        } else if (G_IS_PARAM_SPEC_FLAGS (param)) {
          GParamSpecFlags *pflags = G_PARAM_SPEC_FLAGS (param);
          GFlagsValue *vals;
          gchar *cur, *def;

          vals = pflags->flags_class->values;

          cur = flags_to_string (vals, g_value_get_flags (&value));
          def = flags_to_string (vals, pflags->default_value);

          n_print
              ("%-23.23s Flags \"%s\" Default: 0x%08x, \"%s\" Current: 0x%08x, \"%s\"",
              "", g_type_name (G_VALUE_TYPE (&value)), pflags->default_value,
              def, g_value_get_flags (&value), cur);

          while (vals[0].value_name) {
            g_print ("\n");
            if (_name)
              g_print ("%s", _name);
            g_print ("%-23.23s    (0x%08x): %-16s - %s", "",
                vals[0].value, vals[0].value_nick, vals[0].value_name);
            ++vals;
          }

          g_free (cur);
          g_free (def);
        } else if (G_IS_PARAM_SPEC_OBJECT (param)) {
          n_print ("%-23.23s Object of type \"%s\"", "",
              g_type_name (param->value_type));
        } else if (G_IS_PARAM_SPEC_BOXED (param)) {
          n_print ("%-23.23s Boxed pointer of type \"%s\"", "",
              g_type_name (param->value_type));
        } else if (G_IS_PARAM_SPEC_POINTER (param)) {
          if (param->value_type != G_TYPE_POINTER) {
            n_print ("%-23.23s Pointer of type \"%s\".", "",
                g_type_name (param->value_type));
          } else {
            n_print ("%-23.23s Pointer.", "");
          }
        } else if (param->value_type == G_TYPE_VALUE_ARRAY) {
          GParamSpecValueArray *pvarray = G_PARAM_SPEC_VALUE_ARRAY (param);

          if (pvarray->element_spec) {
            n_print ("%-23.23s Array of GValues of type \"%s\"", "",
                g_type_name (pvarray->element_spec->value_type));
          } else {
            n_print ("%-23.23s Array of GValues", "");
          }
        } else if (GST_IS_PARAM_SPEC_FRACTION (param)) {
          GstParamSpecFraction *pfraction = GST_PARAM_SPEC_FRACTION (param);

          n_print ("%-23.23s Fraction. ", "");

          g_print ("Range: %d/%d - %d/%d Default: %d/%d ",
              pfraction->min_num, pfraction->min_den,
              pfraction->max_num, pfraction->max_den,
              pfraction->def_num, pfraction->def_den);
          if (readable)
            g_print ("Current: %d/%d",
                gst_value_get_fraction_numerator (&value),
                gst_value_get_fraction_denominator (&value));

        } else if (GST_IS_PARAM_SPEC_MINI_OBJECT (param)) {
          n_print ("%-23.23s MiniObject of type \"%s\"", "",
              g_type_name (param->value_type));
        } else {
          n_print ("%-23.23s Unknown type %ld \"%s\"", "", param->value_type,
              g_type_name (param->value_type));
        }
        break;
    }
    if (!readable)
      g_print (" Write only\n");
    else
      g_print ("\n");

    g_value_reset (&value);
  }
  if (num_properties == 0)
    n_print ("  none\n");

  g_free (property_specs);
}

static void
print_pad_templates_info (GstElement * element, GstElementFactory * factory)
{
  GstElementClass *gstelement_class;
  const GList *pads;
  GstStaticPadTemplate *padtemplate;

  n_print ("Pad Templates:\n");
  if (!factory->numpadtemplates) {
    n_print ("  none\n");
    return;
  }

  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  pads = factory->staticpadtemplates;
  while (pads) {
    padtemplate = (GstStaticPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    if (padtemplate->direction == GST_PAD_SRC)
      n_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      n_print ("  SINK template: '%s'\n", padtemplate->name_template);
    else
      n_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

    if (padtemplate->presence == GST_PAD_ALWAYS)
      n_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      n_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST) {
      n_print ("    Availability: On request\n");
      n_print ("      Has request_new_pad() function: %s\n",
          GST_DEBUG_FUNCPTR_NAME (gstelement_class->request_new_pad));
    } else
      n_print ("    Availability: UNKNOWN!!!\n");

    if (padtemplate->static_caps.string) {
      n_print ("    Capabilities:\n");
      print_caps (gst_static_caps_get (&padtemplate->static_caps), "      ");
    }

    n_print ("\n");
  }
}

static void
print_element_flag_info (GstElement * element)
{
  gboolean have_flags = FALSE;

  n_print ("\n");
  n_print ("Element Flags:\n");

  if (!have_flags)
    n_print ("  no flags set\n");

  if (GST_IS_BIN (element)) {
    n_print ("\n");
    n_print ("Bin Flags:\n");
    if (!have_flags)
      n_print ("  no flags set\n");
  }
}

static void
print_implementation_info (GstElement * element)
{
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;

  gstobject_class = GST_OBJECT_CLASS (G_OBJECT_GET_CLASS (element));
  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  n_print ("\n");
  n_print ("Element Implementation:\n");

  n_print ("  Has change_state() function: %s\n",
      GST_DEBUG_FUNCPTR_NAME (gstelement_class->change_state));
#ifndef GST_DISABLE_LOADSAVE
  n_print ("  Has custom save_thyself() function: %s\n",
      GST_DEBUG_FUNCPTR_NAME (gstobject_class->save_thyself));
  n_print ("  Has custom restore_thyself() function: %s\n",
      GST_DEBUG_FUNCPTR_NAME (gstobject_class->restore_thyself));
#endif
}

static void
print_clocking_info (GstElement * element)
{
  if (!gst_element_requires_clock (element) &&
      !(gst_element_provides_clock (element) &&
          gst_element_get_clock (element))) {
    n_print ("\n");
    n_print ("Element has no clocking capabilities.");
    return;
  }

  n_print ("\n");
  n_print ("Clocking Interaction:\n");
  if (gst_element_requires_clock (element)) {
    n_print ("  element requires a clock\n");
  }

  if (gst_element_provides_clock (element)) {
    GstClock *clock;

    clock = gst_element_get_clock (element);
    if (clock)
      n_print ("  element provides a clock: %s\n", GST_OBJECT_NAME (clock));
    else
      n_print ("  element is supposed to provide a clock but returned NULL\n");
  }
}

static void
print_index_info (GstElement * element)
{
  if (gst_element_is_indexable (element)) {
    n_print ("\n");
    n_print ("Indexing capabilities:\n");
    n_print ("  element can do indexing\n");
  } else {
    n_print ("\n");
    n_print ("Element has no indexing capabilities.\n");
  }
}

static void
print_uri_handler_info (GstElement * element)
{
  if (GST_IS_URI_HANDLER (element)) {
    const gchar *uri_type;
    gchar **uri_protocols;

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
    n_print ("  Element can act as %s.\n", uri_type);

    if (uri_protocols && *uri_protocols) {
      n_print ("  Supported URI protocols:\n");
      for (; *uri_protocols != NULL; uri_protocols++)
        n_print ("    %s\n", *uri_protocols);
    } else {
      n_print ("  No supported URI protocols\n");
    }
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

  if (!element->numpads) {
    n_print ("  none\n");
    return;
  }

  pads = element->pads;
  while (pads) {
    gchar *name;

    pad = GST_PAD (pads->data);
    pads = g_list_next (pads);

    n_print ("");

    name = gst_pad_get_name (pad);
    if (gst_pad_get_direction (pad) == GST_PAD_SRC)
      g_print ("  SRC: '%s'", name);
    else if (gst_pad_get_direction (pad) == GST_PAD_SINK)
      g_print ("  SINK: '%s'", name);
    else
      g_print ("  UNKNOWN!!!: '%s'", name);

    g_free (name);

    g_print ("\n");

    n_print ("    Implementation:\n");
    if (pad->chainfunc)
      n_print ("      Has chainfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (pad->chainfunc));
    if (pad->getrangefunc)
      n_print ("      Has getrangefunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (pad->getrangefunc));
    if (pad->eventfunc != gst_pad_event_default)
      n_print ("      Has custom eventfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (pad->eventfunc));
    if (pad->queryfunc != gst_pad_query_default)
      n_print ("      Has custom queryfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (pad->queryfunc));
    if (pad->querytypefunc != gst_pad_get_query_types_default) {
      n_print ("        Provides query types:\n");
      print_query_types (gst_pad_get_query_types (pad));
    }

    if (pad->iterintlinkfunc != gst_pad_iterate_internal_links_default)
      n_print ("      Has custom iterintlinkfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (pad->iterintlinkfunc));

    if (pad->bufferallocfunc)
      n_print ("      Has bufferallocfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (pad->bufferallocfunc));

    if (pad->padtemplate)
      n_print ("    Pad Template: '%s'\n", pad->padtemplate->name_template);

    if (pad->caps) {
      n_print ("    Capabilities:\n");
      print_caps (pad->caps, "      ");
    }
  }
}

#if 0
static gint
compare_signal_names (GSignalQuery * a, GSignalQuery * b)
{
  return strcmp (a->signal_name, b->signal_name);
}
#endif

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
      int indent_len;

      query = (GSignalQuery *) l->data;
      indent_len = strlen (query->signal_name) +
          strlen (g_type_name (query->return_type)) + 24;

      indent = g_new0 (gchar, indent_len + 1);
      memset (indent, ' ', indent_len);

      n_print ("  \"%s\" :  %s user_function (%s* object",
          query->signal_name,
          g_type_name (query->return_type), g_type_name (type));

      for (j = 0; j < query->n_params; j++) {
        if (_name)
          g_print ("%s", _name);
        if (G_TYPE_IS_FUNDAMENTAL (query->param_types[j])) {
          g_print (",\n%s%s arg%d", indent,
              g_type_name (query->param_types[j]), j);
        } else if (G_TYPE_IS_ENUM (query->param_types[j])) {
          g_print (",\n%s%s arg%d", indent,
              g_type_name (query->param_types[j]), j);
        } else {
          g_print (",\n%s%s* arg%d", indent,
              g_type_name (query->param_types[j]), j);
        }
      }

      if (k == 0) {
        if (_name)
          g_print ("%s", _name);
        g_print (",\n%sgpointer user_data);\n", indent);
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
    g_print ("Children:\n");
  }

  while (children) {
    n_print ("  %s\n", GST_ELEMENT_NAME (GST_ELEMENT (children->data)));
    children = g_list_next (children);
  }
}

static void
print_blacklist (void)
{
  GList *plugins, *cur;
  gint count = 0;

  g_print ("%s\n", _("Blacklisted files:"));

  plugins = gst_default_registry_get_plugin_list ();
  for (cur = plugins; cur != NULL; cur = g_list_next (cur)) {
    GstPlugin *plugin = (GstPlugin *) (cur->data);
    if (plugin->flags & GST_PLUGIN_FLAG_BLACKLISTED) {
      g_print ("  %s\n", plugin->desc.name);
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
print_element_list (gboolean print_all)
{
  int plugincount = 0, featurecount = 0, blacklistcount = 0;
  GList *plugins, *orig_plugins;

  orig_plugins = plugins = gst_default_registry_get_plugin_list ();
  while (plugins) {
    GList *features, *orig_features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);
    plugins = g_list_next (plugins);
    plugincount++;

    if (plugin->flags & GST_PLUGIN_FLAG_BLACKLISTED) {
      blacklistcount++;
      continue;
    }

    orig_features = features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get_default (),
        plugin->desc.name);
    while (features) {
      GstPluginFeature *feature;

      if (G_UNLIKELY (features->data == NULL))
        goto next;
      feature = GST_PLUGIN_FEATURE (features->data);
      featurecount++;

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;

        factory = GST_ELEMENT_FACTORY (feature);
        if (print_all)
          print_element_info (factory, TRUE);
        else
          g_print ("%s:  %s: %s\n", plugin->desc.name,
              GST_PLUGIN_FEATURE_NAME (factory),
              gst_element_factory_get_longname (factory));
      } else if (GST_IS_INDEX_FACTORY (feature)) {
        GstIndexFactory *factory;

        factory = GST_INDEX_FACTORY (feature);
        if (!print_all)
          g_print ("%s:  %s: %s\n", plugin->desc.name,
              GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
        GstTypeFindFactory *factory;

        factory = GST_TYPE_FIND_FACTORY (feature);
        if (!print_all)
          g_print ("%s: %s: ", plugin->desc.name,
              gst_plugin_feature_get_name (feature));
        if (factory->extensions) {
          guint i = 0;

          while (factory->extensions[i]) {
            if (!print_all)
              g_print ("%s%s", i > 0 ? ", " : "", factory->extensions[i]);
            i++;
          }
          if (!print_all)
            g_print ("\n");
        } else {
          if (!print_all)
            g_print ("no extensions\n");
        }
      } else {
        if (!print_all)
          n_print ("%s:  %s (%s)\n", plugin->desc.name,
              GST_PLUGIN_FEATURE_NAME (feature),
              g_type_name (G_OBJECT_TYPE (feature)));
      }

    next:
      features = g_list_next (features);
    }

    gst_plugin_feature_list_free (orig_features);
  }

  gst_plugin_list_free (orig_plugins);

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

  plugins = gst_default_registry_get_plugin_list ();

  for (p = plugins; p; p = p->next) {
    GstPlugin *plugin = (GstPlugin *) (p->data);

    features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get_default (),
        plugin->desc.name);

    for (f = features; f; f = f->next) {
      GstPluginFeature *feature = GST_PLUGIN_FEATURE (f->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;
        GstElement *element;

        factory = GST_ELEMENT_FACTORY (gst_plugin_feature_load (feature));
        if (!factory) {
          g_print ("element plugin %s couldn't be loaded\n", plugin->desc.name);
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
          const gchar *dir;
          gchar **uri_protocols, *joined;

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
          joined = g_strjoinv (", ", uri_protocols);

          g_print ("%s (%s, rank %u): %s\n", GST_OBJECT_NAME (factory), dir,
              gst_plugin_feature_get_rank (GST_PLUGIN_FEATURE (factory)),
              joined);

          g_free (joined);
          //g_strfreev (uri_protocols);
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
  n_print ("Plugin Details:\n");
  n_print ("  Name:\t\t\t%s\n", plugin->desc.name);
  n_print ("  Description:\t\t%s\n", plugin->desc.description);
  n_print ("  Filename:\t\t%s\n",
      plugin->filename ? plugin->filename : "(null)");
  n_print ("  Version:\t\t%s\n", plugin->desc.version);
  n_print ("  License:\t\t%s\n", plugin->desc.license);
  n_print ("  Source module:\t%s\n", plugin->desc.source);
  n_print ("  Binary package:\t%s\n", plugin->desc.package);
  n_print ("  Origin URL:\t\t%s\n", plugin->desc.origin);
  n_print ("\n");
}

static void
print_plugin_features (GstPlugin * plugin)
{
  GList *features;
  gint num_features = 0;
  gint num_elements = 0;
  gint num_typefinders = 0;
  gint num_indexes = 0;
  gint num_other = 0;

  features =
      gst_registry_get_feature_list_by_plugin (gst_registry_get_default (),
      plugin->desc.name);

  while (features) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (features->data);

    if (GST_IS_ELEMENT_FACTORY (feature)) {
      GstElementFactory *factory;

      factory = GST_ELEMENT_FACTORY (feature);
      n_print ("  %s: %s\n", GST_PLUGIN_FEATURE_NAME (factory),
          gst_element_factory_get_longname (factory));
      num_elements++;
    } else if (GST_IS_INDEX_FACTORY (feature)) {
      GstIndexFactory *factory;

      factory = GST_INDEX_FACTORY (feature);
      n_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
      num_indexes++;
    } else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
      GstTypeFindFactory *factory;

      factory = GST_TYPE_FIND_FACTORY (feature);
      if (factory->extensions) {
        guint i = 0;

        g_print ("%s: %s: ", plugin->desc.name,
            gst_plugin_feature_get_name (feature));
        while (factory->extensions[i]) {
          g_print ("%s%s", i > 0 ? ", " : "", factory->extensions[i]);
          i++;
        }
        g_print ("\n");
      } else
        g_print ("%s: %s: no extensions\n", plugin->desc.name,
            gst_plugin_feature_get_name (feature));

      num_typefinders++;
    } else if (feature) {
      n_print ("  %s (%s)\n", gst_object_get_name (GST_OBJECT (feature)),
          g_type_name (G_OBJECT_TYPE (feature)));
      num_other++;
    }
    num_features++;
    features = g_list_next (features);
  }
  n_print ("\n");
  n_print ("  %d features:\n", num_features);
  if (num_elements > 0)
    n_print ("  +-- %d elements\n", num_elements);
  if (num_typefinders > 0)
    n_print ("  +-- %d typefinders\n", num_typefinders);
  if (num_indexes > 0)
    n_print ("  +-- %d indexes\n", num_indexes);
  if (num_other > 0)
    n_print ("  +-- %d other objects\n", num_other);

  n_print ("\n");
}

static int
print_element_features (const gchar * element_name)
{
  GstPluginFeature *feature;

  /* FIXME implement other pretty print function for these */
  feature = gst_default_registry_find_feature (element_name,
      GST_TYPE_INDEX_FACTORY);
  if (feature) {
    n_print ("%s: an index\n", element_name);
    return 0;
  }
  feature = gst_default_registry_find_feature (element_name,
      GST_TYPE_TYPE_FIND_FACTORY);
  if (feature) {
    n_print ("%s: a typefind function\n", element_name);
    return 0;
  }

  return -1;
}

static int
print_element_info (GstElementFactory * factory, gboolean print_names)
{
  GstElement *element;
  gint maxlevel = 0;

  factory =
      GST_ELEMENT_FACTORY (gst_plugin_feature_load (GST_PLUGIN_FEATURE
          (factory)));

  if (!factory) {
    g_print ("element plugin couldn't be loaded\n");
    return -1;
  }

  element = gst_element_factory_create (factory, NULL);
  if (!element) {
    g_print ("couldn't construct element for some reason\n");
    return -1;
  }

  if (print_names)
    _name = g_strdup_printf ("%s: ", GST_PLUGIN_FEATURE (factory)->name);
  else
    _name = NULL;

  print_factory_details_info (factory);
  if (GST_PLUGIN_FEATURE (factory)->plugin_name) {
    GstPlugin *plugin;

    plugin = gst_registry_find_plugin (gst_registry_get_default (),
        GST_PLUGIN_FEATURE (factory)->plugin_name);
    if (plugin) {
      print_plugin_info (plugin);
    }
  }

  print_hierarchy (G_OBJECT_TYPE (element), 0, &maxlevel);
  print_interfaces (G_OBJECT_TYPE (element));

  print_pad_templates_info (element, factory);
  print_element_flag_info (element);
  print_implementation_info (element);
  print_clocking_info (element);
  print_index_info (element);
  print_uri_handler_info (element);
  print_pad_info (element);
  print_element_properties_info (element);
  print_signal_info (element);
  print_children_info (element);

  gst_object_unref (element);
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

  klass = gst_element_factory_get_klass (factory);
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
        type_name, GST_PLUGIN_FEATURE_NAME (factory));
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
  gchar **protocols, **p;

  protocols = gst_element_factory_get_uri_protocols (factory);
  if (protocols != NULL && *protocols != NULL) {
    switch (gst_element_factory_get_uri_type (factory)) {
      case GST_URI_SINK:
        for (p = protocols; *p != NULL; ++p)
          g_print ("urisink-%s\n", *p);
        break;
      case GST_URI_SRC:
        for (p = protocols; *p != NULL; ++p)
          g_print ("urisource-%s\n", *p);
        break;
      default:
        break;
    }
    g_strfreev (protocols);
  }
}

static void
print_plugin_automatic_install_info (GstPlugin * plugin)
{
  const gchar *plugin_name;
  GList *features, *l;

  plugin_name = gst_plugin_get_name (plugin);

  /* not interested in typefind factories, only element factories */
  features = gst_registry_get_feature_list (gst_registry_get_default (),
      GST_TYPE_ELEMENT_FACTORY);

  for (l = features; l != NULL; l = l->next) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (l->data);

    /* only interested in the ones that are in the plugin we just loaded */
    if (g_str_equal (plugin_name, feature->plugin_name)) {
      GstElementFactory *factory;

      g_print ("element-%s\n", gst_plugin_feature_get_name (feature));

      factory = GST_ELEMENT_FACTORY (feature);
      print_plugin_automatic_install_info_protocols (factory);
      print_plugin_automatic_install_info_codecs (factory);
    }
  }

  g_list_foreach (features, (GFunc) gst_object_unref, NULL);
  g_list_free (features);
}

static void
print_all_plugin_automatic_install_info (void)
{
  GList *plugins, *orig_plugins;

  orig_plugins = plugins = gst_default_registry_get_plugin_list ();
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

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_thread_init (NULL);

  gst_tools_set_prgname ("gst-inspect");

#ifndef GST_DISABLE_OPTION_PARSING
  ctx = g_option_context_new ("[ELEMENT-NAME | PLUGIN-NAME]");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }
  g_option_context_free (ctx);
#else
  gst_init (&argc, &argv);
#endif

  gst_tools_print_version ("gst-inspect");

  if (print_all && argc > 1) {
    g_print ("-a requires no extra arguments\n");
    return 1;
  }

  if (uri_handlers && argc > 1) {
    g_print ("-u requires no extra arguments\n");
    exit (1);
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
        print_element_list (print_all);
    }
  } else {
    /* else we try to get a factory */
    GstElementFactory *factory;
    GstPlugin *plugin;
    const char *arg = argv[argc - 1];
    int retval;

    if (!plugin_name) {
      factory = gst_element_factory_find (arg);

      /* if there's a factory, print out the info */
      if (factory) {
        retval = print_element_info (factory, print_all);
        gst_object_unref (factory);
      } else {
        retval = print_element_features (arg);
      }
    } else {
      retval = -1;
    }

    /* otherwise check if it's a plugin */
    if (retval) {
      plugin = gst_default_registry_find_plugin (arg);

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
            g_print (_("Could not load plugin file: %s\n"), error->message);
            g_error_free (error);
            return -1;
          }
        } else {
          g_print (_("No such element or plugin '%s'\n"), arg);
          return -1;
        }
      }
    }
  }

  return 0;
}
