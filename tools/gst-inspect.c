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

#include <gst/gst.h>
#include <gst/control/control.h>

#include "gst/gst-i18n-app.h"

#include <string.h>
#include <locale.h>
#include <glib/gprintf.h>

static char *_name;

static int print_element_info (GstElementFactory * factory,
    gboolean print_names);

void
n_print (const char *format, ...)
{
  va_list args;
  gint retval;

  if (_name)
    g_print (_name);

  va_start (args, format);
  retval = g_vprintf (format, args);
  va_end (args);
}

static gboolean
print_field (GQuark field, GValue * value, gpointer pfx)
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

#ifndef GST_DISABLE_ENUMTYPES
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
#else
static void
print_event_masks (const GstEventMask * masks)
{
}
#endif

static char *
get_rank_name (gint rank)
{
  switch (rank) {
    case GST_RANK_NONE:
      return "none";
    case GST_RANK_MARGINAL:
      return "marginal";
    case GST_RANK_SECONDARY:
      return "secondary";
    case GST_RANK_PRIMARY:
      return "primary";
    default:
      return "unknown";
  }
}

static void
print_factory_details_info (GstElementFactory * factory)
{
  n_print ("Factory Details:\n");
  n_print ("  Long name:\t%s\n", factory->details.longname);
  n_print ("  Class:\t%s\n", factory->details.klass);
  n_print ("  Description:\t%s\n", factory->details.description);
  n_print ("  Author(s):\t%s\n", factory->details.author);
  n_print ("  Rank:\t\t%s (%d)\n",
      get_rank_name (GST_PLUGIN_FEATURE (factory)->rank),
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
    g_print (_name);

  for (i = 1; i < *maxlevel - level; i++)
    g_print ("      ");
  if (*maxlevel - level)
    g_print (" +----");

  g_print ("%s\n", g_type_name (type));

  if (level == 1)
    n_print ("\n");
}

static void
print_element_properties_info (GstElement * element)
{
  GParamSpec **property_specs;
  gint num_properties, i;
  gboolean readable;
  const char *string_val;

  property_specs = g_object_class_list_properties
      (G_OBJECT_GET_CLASS (element), &num_properties);
  n_print ("\n");
  n_print ("Element Properties:\n");

  for (i = 0; i < num_properties; i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];

    readable = FALSE;

    g_value_init (&value, param->value_type);
    if (param->flags & G_PARAM_READABLE) {
      g_object_get_property (G_OBJECT (element), param->name, &value);
      readable = TRUE;
    }

    n_print ("  %-20s: %s\n", g_param_spec_get_name (param),
        g_param_spec_get_blurb (param));

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING:
        string_val = g_value_get_string (&value);
        n_print ("%-23.23s String. ", "");
        if (readable) {
          if (string_val == NULL)
            g_print ("(Default \"\")");
          else
            g_print ("(Default \"%s\")", g_value_get_string (&value));
        }
        break;
      case G_TYPE_BOOLEAN:
        n_print ("%-23.23s Boolean. ", "");
        if (readable)
          g_print ("(Default %s)",
              (g_value_get_boolean (&value) ? "true" : "false"));
        break;
      case G_TYPE_ULONG:
      {
        GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (param);

        n_print ("%-23.23s Unsigned Long. ", "");
        if (readable)
          g_print ("Range: %lu - %lu (Default %lu)",
              pulong->minimum, pulong->maximum, g_value_get_ulong (&value));
        break;
      }
      case G_TYPE_LONG:
      {
        GParamSpecLong *plong = G_PARAM_SPEC_LONG (param);

        n_print ("%-23.23s Long. ", "");
        if (readable)
          g_print ("Range: %ld - %ld (Default %ld)",
              plong->minimum, plong->maximum, g_value_get_long (&value));
        break;
      }
      case G_TYPE_UINT:
      {
        GParamSpecUInt *puint = G_PARAM_SPEC_UINT (param);

        n_print ("%-23.23s Unsigned Integer. ", "");
        if (readable)
          g_print ("Range: %u - %u (Default %u)",
              puint->minimum, puint->maximum, g_value_get_uint (&value));
        break;
      }
      case G_TYPE_INT:
      {
        GParamSpecInt *pint = G_PARAM_SPEC_INT (param);

        n_print ("%-23.23s Integer. ", "");
        if (readable)
          g_print ("Range: %d - %d (Default %d)",
              pint->minimum, pint->maximum, g_value_get_int (&value));
        break;
      }
      case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (param);

        n_print ("%-23.23s Unsigned Integer64. ", "");
        if (readable)
          g_print ("Range: %" G_GUINT64_FORMAT " - %"
              G_GUINT64_FORMAT " (Default %" G_GUINT64_FORMAT ")",
              puint64->minimum, puint64->maximum, g_value_get_uint64 (&value));
        break;
      }
      case G_TYPE_INT64:
      {
        GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (param);

        n_print ("%-23.23s Integer64. ", "");
        if (readable)
          g_print ("Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT
              " (Default %" G_GINT64_FORMAT ")", pint64->minimum,
              pint64->maximum, g_value_get_int64 (&value));
        break;
      }
      case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (param);

        n_print ("%-23.23s Float. Default: %-8.8s %15.7g\n", "", "",
            g_value_get_float (&value));
        n_print ("%-23.23s Range: %15.7g - %15.7g", "",
            pfloat->minimum, pfloat->maximum);
        break;
      }
      case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (param);

        n_print ("%-23.23s Double. Default: %-8.8s %15.7g\n", "", "",
            g_value_get_double (&value));
        n_print ("%-23.23s Range: %15.7g - %15.7g", "",
            pdouble->minimum, pdouble->maximum);
        break;
      }
      default:
        if (param->value_type == GST_TYPE_URI) {
          n_print ("%-23.23s URI", "");
        }
        if (param->value_type == GST_TYPE_CAPS) {
          const GstCaps *caps = gst_value_get_caps (&value);

          if (!caps)
            n_print ("%-23.23s Caps (NULL)", "");
          else {
            print_caps (caps, "                           ");
          }
        } else if (G_IS_PARAM_SPEC_ENUM (param)) {
          GEnumValue *values;
          guint j = 0;
          gint enum_value;

          values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
          enum_value = g_value_get_enum (&value);

          while (values[j].value_name) {
            if (values[j].value == enum_value)
              break;
            j++;
          }

          n_print ("%-23.23s Enum \"%s\" (default %d, \"%s\")", "",
              g_type_name (G_VALUE_TYPE (&value)),
              enum_value, values[j].value_nick);

          j = 0;
          while (values[j].value_name) {
            g_print ("\n%s%-23.23s    (%d): \t%s", "",
                _name, values[j].value, values[j].value_nick);
            j++;
          }
          /* g_type_class_unref (ec); */
        } else if (G_IS_PARAM_SPEC_FLAGS (param)) {
          GFlagsValue *values;
          guint j = 0;
          gint flags_value;
          GString *flags = NULL;

          values = G_FLAGS_CLASS (g_type_class_ref (param->value_type))->values;
          flags_value = g_value_get_flags (&value);

          while (values[j].value_name) {
            if (values[j].value & flags_value) {
              if (flags) {
                g_string_append_printf (flags, " | %s", values[j].value_nick);
              } else {
                flags = g_string_new (values[j].value_nick);
              }
            }
            j++;
          }

          n_print ("%-23.23s Flags \"%s\" (default %d, \"%s\")", "",
              g_type_name (G_VALUE_TYPE (&value)),
              flags_value, (flags ? flags->str : "(none)"));

          j = 0;
          while (values[j].value_name) {
            g_print ("\n%s%-23.23s    (%d): \t%s", "",
                _name, values[j].value, values[j].value_nick);
            j++;
          }

          if (flags)
            g_string_free (flags, TRUE);
        } else if (G_IS_PARAM_SPEC_OBJECT (param)) {
          n_print ("%-23.23s Object of type \"%s\"", "",
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
  }
  if (num_properties == 0)
    n_print ("  none\n");
}

static void
print_pad_templates_info (GstElement * element, GstElementFactory * factory)
{
  GstElementClass *gstelement_class;
  const GList *pads;
  GstPadTemplate *padtemplate;

  n_print ("Pad Templates:\n");
  if (!factory->numpadtemplates) {
    n_print ("  none\n");
    return;
  }

  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  pads = factory->padtemplates;
  while (pads) {
    padtemplate = (GstPadTemplate *) (pads->data);
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

    if (padtemplate->caps) {
      n_print ("    Capabilities:\n");
      print_caps (padtemplate->caps, "      ");
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

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_COMPLEX)) {
    n_print ("  GST_ELEMENT_COMPLEX\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    n_print ("  GST_ELEMENT_DECOUPLED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_THREAD_SUGGESTED)) {
    n_print ("  GST_ELEMENT_THREADSUGGESTED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_EVENT_AWARE)) {
    n_print ("  GST_ELEMENT_EVENT_AWARE\n");
    have_flags = TRUE;
  }
  if (!have_flags)
    n_print ("  no flags set\n");

  if (GST_IS_BIN (element)) {
    n_print ("\n");
    n_print ("Bin Flags:\n");
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      n_print ("  GST_BIN_FLAG_MANAGER\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET (element, GST_BIN_SELF_SCHEDULABLE)) {
      n_print ("  GST_BIN_SELF_SCHEDULABLE\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_PREFER_COTHREADS)) {
      n_print ("  GST_BIN_FLAG_PREFER_COTHREADS\n");
      have_flags = TRUE;
    }
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

  if (element->loopfunc)
    n_print ("  loopfunc()-based element: %s\n",
        GST_DEBUG_FUNCPTR_NAME (element->loopfunc));
  else
    n_print ("  No loopfunc(), must be chain-based or not configured yet\n");

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

#ifndef GST_DISABLE_INDEX
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
#else
static void
print_index_info (GstElement * element)
{
}
#endif

static void
print_pad_info (GstElement * element)
{
  const GList *pads;
  GstPad *pad;
  GstRealPad *realpad;

  n_print ("\n");
  n_print ("Pads:\n");

  if (!element->numpads) {
    n_print ("  none\n");
    return;
  }

  pads = gst_element_get_pad_list (element);
  while (pads) {
    pad = GST_PAD (pads->data);
    pads = g_list_next (pads);
    realpad = GST_PAD_REALIZE (pad);

    n_print ("");

    if (gst_pad_get_direction (pad) == GST_PAD_SRC)
      g_print ("  SRC: '%s'", gst_pad_get_name (pad));
    else if (gst_pad_get_direction (pad) == GST_PAD_SINK)
      g_print ("  SINK: '%s'", gst_pad_get_name (pad));
    else
      g_print ("  UNKNOWN!!!: '%s'\n", gst_pad_get_name (pad));

    if (GST_IS_GHOST_PAD (pad))
      g_print (", ghost of real pad %s:%s\n", GST_DEBUG_PAD_NAME (realpad));
    else
      g_print ("\n");

    n_print ("    Implementation:\n");
    if (realpad->chainfunc)
      n_print ("      Has chainfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->chainfunc));
    if (realpad->getfunc)
      n_print ("      Has getfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->getfunc));
    if (realpad->formatsfunc != gst_pad_get_formats_default) {
      n_print ("      Supports seeking/conversion/query formats:\n");
      print_formats (gst_pad_get_formats (GST_PAD (realpad)));
    }
    if (realpad->convertfunc != gst_pad_convert_default)
      n_print ("      Has custom convertfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->convertfunc));
    if (realpad->eventfunc != gst_pad_event_default)
      n_print ("      Has custom eventfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->eventfunc));
    if (realpad->eventmaskfunc != gst_pad_get_event_masks_default) {
      n_print ("        Provides event masks:\n");
      print_event_masks (gst_pad_get_event_masks (GST_PAD (realpad)));
    }
    if (realpad->queryfunc != gst_pad_query_default)
      n_print ("      Has custom queryfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->queryfunc));
    if (realpad->querytypefunc != gst_pad_get_query_types_default) {
      n_print ("        Provides query types:\n");
      print_query_types (gst_pad_get_query_types (GST_PAD (realpad)));
    }

    if (realpad->intlinkfunc != gst_pad_get_internal_links_default)
      n_print ("      Has custom intconnfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->intlinkfunc));

    if (realpad->bufferallocfunc)
      n_print ("      Has bufferallocfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->bufferallocfunc));

    if (pad->padtemplate)
      n_print ("    Pad Template: '%s'\n", pad->padtemplate->name_template);

    if (realpad->caps) {
      n_print ("    Capabilities:\n");
      print_caps (realpad->caps, "      ");
    }
  }
}

static void
print_dynamic_parameters_info (GstElement * element)
{
  GstDParamManager *dpman;
  GParamSpec **specs = NULL;
  gint x;

  if ((dpman = gst_dpman_get_manager (element))) {
    specs = gst_dpman_list_dparam_specs (dpman);
  }

  if (specs && specs[0] != NULL) {
    n_print ("\n");
    n_print ("Dynamic Parameters:\n");

    for (x = 0; specs[x] != NULL; x++) {
      g_print ("  %-20.20s: ", g_param_spec_get_name (specs[x]));

      switch (G_PARAM_SPEC_VALUE_TYPE (specs[x])) {
        case G_TYPE_INT64:
          g_print ("64 Bit Integer (Default %" G_GINT64_FORMAT ", Range %"
              G_GINT64_FORMAT " -> %" G_GINT64_FORMAT ")",
              ((GParamSpecInt64 *) specs[x])->default_value,
              ((GParamSpecInt64 *) specs[x])->minimum,
              ((GParamSpecInt64 *) specs[x])->maximum);
          break;
        case G_TYPE_INT:
          g_print ("Integer (Default %d, Range %d -> %d)",
              ((GParamSpecInt *) specs[x])->default_value,
              ((GParamSpecInt *) specs[x])->minimum,
              ((GParamSpecInt *) specs[x])->maximum);
          break;
        case G_TYPE_FLOAT:
          g_print ("Float. Default: %-8.8s %15.7g\n", "",
              ((GParamSpecFloat *) specs[x])->default_value);
          g_print ("%-23.23s Range: %15.7g - %15.7g", "",
              ((GParamSpecFloat *) specs[x])->minimum,
              ((GParamSpecFloat *) specs[x])->maximum);
          break;
        case G_TYPE_DOUBLE:
          g_print ("Double. Default: %-8.8s %15.7g\n", "",
              ((GParamSpecDouble *) specs[x])->default_value);
          g_print ("%-23.23s Range: %15.7g - %15.7g", "",
              ((GParamSpecDouble *) specs[x])->minimum,
              ((GParamSpecDouble *) specs[x])->maximum);
          break;
        default:
          g_print ("unknown %ld", G_PARAM_SPEC_VALUE_TYPE (specs[x]));
      }
      g_print ("\n");
    }
    g_free (specs);
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
      }
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

      for (j = 0; j < query->n_params; j++)
        g_print (",\n%s%s%s arg%d", _name, indent,
            g_type_name (query->param_types[j]), j);

      if (k == 0)
        g_print (",\n%s%sgpointer user_data);\n", _name, indent);
      else
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

  children = (GList *) gst_bin_get_list (GST_BIN (element));
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
print_element_list (gboolean print_all)
{
  GList *plugins;

  plugins = gst_registry_pool_plugin_list ();
  while (plugins) {
    GList *features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);
    plugins = g_list_next (plugins);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;

        factory = GST_ELEMENT_FACTORY (feature);
        if (print_all)
          print_element_info (factory, TRUE);
        else
          g_print ("%s:  %s: %s\n", plugin->desc.name,
              GST_PLUGIN_FEATURE_NAME (factory), factory->details.longname);
      }
#ifndef GST_DISABLE_INDEX
      else if (GST_IS_INDEX_FACTORY (feature)) {
        GstIndexFactory *factory;

        factory = GST_INDEX_FACTORY (feature);
        if (!print_all)
          g_print ("%s:  %s: %s\n", plugin->desc.name,
              GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      }
#endif
      else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
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
      } else if (GST_IS_SCHEDULER_FACTORY (feature)) {
        GstSchedulerFactory *factory;

        factory = GST_SCHEDULER_FACTORY (feature);
        if (!print_all)
          g_print ("%s:  %s: %s\n", plugin->desc.name,
              GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      } else {
        if (!print_all)
          n_print ("%s:  %s (%s)\n", plugin->desc.name,
              GST_PLUGIN_FEATURE_NAME (feature),
              g_type_name (G_OBJECT_TYPE (feature)));
      }

      features = g_list_next (features);
    }
  }
}

static void
print_plugin_info (GstPlugin * plugin)
{
  GList *features;
  gint num_features = 0;
  gint num_elements = 0;
  gint num_types = 0;
  gint num_schedulers = 0;
  gint num_indexes = 0;
  gint num_other = 0;

  n_print ("Plugin Details:\n");
  n_print ("  Name:\t\t%s\n", plugin->desc.name);
  n_print ("  Description:\t%s\n", plugin->desc.description);
  n_print ("  Filename:\t%s\n", plugin->filename);
  n_print ("  Version:\t%s\n", plugin->desc.version);
  n_print ("  License:\t%s\n", plugin->desc.license);
  n_print ("  Package:\t%s\n", plugin->desc.package);
  n_print ("  Origin URL:\t%s\n", plugin->desc.origin);
  n_print ("\n");

  features = gst_plugin_get_feature_list (plugin);

  while (features) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (features->data);

    if (GST_IS_ELEMENT_FACTORY (feature)) {
      GstElementFactory *factory;

      factory = GST_ELEMENT_FACTORY (feature);
      n_print ("  %s: %s\n", GST_OBJECT_NAME (factory),
          factory->details.longname);
      num_elements++;
    }
#ifndef GST_DISABLE_INDEX
    else if (GST_IS_INDEX_FACTORY (feature)) {
      GstIndexFactory *factory;

      factory = GST_INDEX_FACTORY (feature);
      n_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
      num_indexes++;
    }
#endif
    else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
      GstTypeFindFactory *factory;

      factory = GST_TYPE_FIND_FACTORY (feature);
      if (factory->extensions) {
        guint i = 0;

        g_print ("%s type: ", plugin->desc.name);
        while (factory->extensions[i]) {
          g_print ("%s%s", i > 0 ? ", " : "", factory->extensions[i]);
          i++;
        }
      } else
        g_print ("%s type: N/A\n", plugin->desc.name);

      num_types++;
    } else if (GST_IS_SCHEDULER_FACTORY (feature)) {
      GstSchedulerFactory *factory;

      factory = GST_SCHEDULER_FACTORY (feature);
      n_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
      num_schedulers++;
    } else {
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
  if (num_types > 0)
    n_print ("  +-- %d types\n", num_types);
  if (num_schedulers > 0)
    n_print ("  +-- %d schedulers\n", num_schedulers);
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
  feature = gst_registry_pool_find_feature (element_name,
      GST_TYPE_SCHEDULER_FACTORY);
  if (feature) {
    n_print ("%s: a scheduler\n", element_name);
    return 0;
  }
#ifndef GST_DISABLE_INDEX
  feature = gst_registry_pool_find_feature (element_name,
      GST_TYPE_INDEX_FACTORY);
  if (feature) {
    n_print ("%s: an index\n", element_name);
    return 0;
  }
#endif
  feature = gst_registry_pool_find_feature (element_name,
      GST_TYPE_TYPE_FIND_FACTORY);
  if (feature) {
    n_print ("%s: a typefind function\n", element_name);
    return 0;
  }
#ifndef GST_DISABLE_URI
  feature = gst_registry_pool_find_feature (element_name, GST_TYPE_URI_HANDLER);
  if (feature) {
    n_print ("%s: an uri handler\n", element_name);
    return 0;
  }
#endif

  return -1;
}

static int
print_element_info (GstElementFactory * factory, gboolean print_names)
{
  GstElement *element;
  gint maxlevel = 0;

  element = gst_element_factory_create (factory, NULL);
  if (!element) {
    g_print ("couldn't construct element for some reason\n");
    return -1;
  }

  if (print_names)
    _name = g_strdup_printf ("%s: ", GST_PLUGIN_FEATURE (factory)->name);
  else
    _name = "";

  print_factory_details_info (factory);

  print_hierarchy (G_OBJECT_TYPE (element), 0, &maxlevel);

  print_pad_templates_info (element, factory);
  print_element_flag_info (element);
  print_implementation_info (element);
  print_clocking_info (element);
  print_index_info (element);
  print_pad_info (element);
  print_element_properties_info (element);
  print_dynamic_parameters_info (element);
  print_signal_info (element);
  print_children_info (element);

  if (_name != "")
    g_free (_name);

  return 0;
}

int
main (int argc, char *argv[])
{
  GstElementFactory *factory;
  GstPlugin *plugin;
  gchar *so;
  gboolean print_all = FALSE;
  struct poptOption options[] = {
    {"print-all", 'a', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &print_all, 0,
        N_("Print all elements"), NULL},
    POPT_TABLEEND
  };

#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  gst_init_with_popt_table (&argc, &argv, options);
  gst_control_init (&argc, &argv);

  if (print_all && argc > 2) {
    g_print ("-a requires no extra arguments\n");
    return 1;
  }

  /* if no arguments, print out list of elements */
  if (argc == 1 || print_all) {
    print_element_list (print_all);
    /* else we try to get a factory */
  } else {
    const char *arg = argv[argc - 1];

    /* only search for a factory if there's not a '.so' */
    if (!strstr (argv[1], ".so")) {
      int retval;

      factory = gst_element_factory_find (arg);
      /* if there's a factory, print out the info */
      if (factory) {
        retval = print_element_info (factory, print_all);
      } else {
        retval = print_element_features (arg);
      }

      if (retval)
        g_print ("No such element: '%s'\n", arg);

      return retval;
    } else {
      /* strip the .so */
      so = strstr (argv[argc - 1], ".so");
      so[0] = '\0';
    }

    /* otherwise assume it's a plugin */
    plugin = gst_registry_pool_find_plugin (arg);

    /* if there is such a plugin, print out info */

    if (plugin) {
      print_plugin_info (plugin);

    } else {
      g_print ("No such plugin '%s'\n", arg);
      return -1;
    }
  }

  return 0;
}
