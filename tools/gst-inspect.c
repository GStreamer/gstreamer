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

static gboolean
print_field (GQuark field, GValue * value, gpointer pfx)
{
  gchar *str = gst_value_serialize (value);

  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void
print_caps (const GstCaps * caps, const gchar * pfx)
{
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
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
      g_print ("\t\t(%d):\t%s (%s)\n", *formats,
          definition->nick, definition->description);
    else
      g_print ("\t\t(%d):\tUnknown format\n", *formats);

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
      g_print ("\t\t(%d):\t%s (%s)\n", *types,
          definition->nick, definition->description);
    else
      g_print ("\t\t(%d):\tUnknown query format\n", *types);

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
  g_print ("Factory Details:\n");
  g_print ("  Long name:\t%s\n", factory->details.longname);
  g_print ("  Class:\t%s\n", factory->details.klass);
  g_print ("  Description:\t%s\n", factory->details.description);
  g_print ("  Author(s):\t%s\n", factory->details.author);
  g_print ("  Rank:\t\t%s (%d)\n",
      get_rank_name (GST_PLUGIN_FEATURE (factory)->rank),
      GST_PLUGIN_FEATURE (factory)->rank);
  g_print ("\n");
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

  for (i = 1; i < *maxlevel - level; i++)
    g_print ("      ");
  if (*maxlevel - level)
    g_print (" +----");

  g_print ("%s\n", g_type_name (type));

  if (level == 1)
    g_print ("\n");
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
  g_print ("\nElement Properties:\n");

  for (i = 0; i < num_properties; i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];

    readable = FALSE;

    g_value_init (&value, param->value_type);
    if (param->flags & G_PARAM_READABLE) {
      g_object_get_property (G_OBJECT (element), param->name, &value);
      readable = TRUE;
    }

    g_print ("  %-20s: %s\n", g_param_spec_get_name (param),
        g_param_spec_get_blurb (param));

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING:
        string_val = g_value_get_string (&value);
        g_print ("%-23.23s String. ", "");
        if (readable) {
          if (string_val == NULL)
            g_print ("(Default \"\")");
          else
            g_print ("(Default \"%s\")", g_value_get_string (&value));
        }
        break;
      case G_TYPE_BOOLEAN:
        g_print ("%-23.23s Boolean. ", "");
        if (readable)
          g_print ("(Default %s)",
              (g_value_get_boolean (&value) ? "true" : "false"));
        break;
      case G_TYPE_ULONG:
      {
        GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (param);

        g_print ("%-23.23s Unsigned Long. ", "");
        if (readable)
          g_print ("Range: %lu - %lu (Default %lu)",
              pulong->minimum, pulong->maximum, g_value_get_ulong (&value));
        break;
      }
      case G_TYPE_LONG:
      {
        GParamSpecLong *plong = G_PARAM_SPEC_LONG (param);

        g_print ("%-23.23s Long. ", "");
        if (readable)
          g_print ("Range: %ld - %ld (Default %ld)",
              plong->minimum, plong->maximum, g_value_get_long (&value));
        break;
      }
      case G_TYPE_UINT:
      {
        GParamSpecUInt *puint = G_PARAM_SPEC_UINT (param);

        g_print ("%-23.23s Unsigned Integer. ", "");
        if (readable)
          g_print ("Range: %u - %u (Default %u)",
              puint->minimum, puint->maximum, g_value_get_uint (&value));
        break;
      }
      case G_TYPE_INT:
      {
        GParamSpecInt *pint = G_PARAM_SPEC_INT (param);

        g_print ("%-23.23s Integer. ", "");
        if (readable)
          g_print ("Range: %d - %d (Default %d)",
              pint->minimum, pint->maximum, g_value_get_int (&value));
        break;
      }
      case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (param);

        g_print ("%-23.23s Unsigned Integer64. ", "");
        if (readable)
          g_print ("Range: %" G_GUINT64_FORMAT " - %"
              G_GUINT64_FORMAT " (Default %" G_GUINT64_FORMAT ")",
              puint64->minimum, puint64->maximum, g_value_get_uint64 (&value));
        break;
      }
      case G_TYPE_INT64:
      {
        GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (param);

        g_print ("%-23.23s Integer64. ", "");
        if (readable)
          g_print ("Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT
              " (Default %" G_GINT64_FORMAT ")", pint64->minimum,
              pint64->maximum, g_value_get_int64 (&value));
        break;
      }
      case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (param);

        g_print ("%-23.23s Float. Default: %-8.8s %15.7g\n", "", "",
            g_value_get_float (&value));
        g_print ("%-23.23s Range: %15.7g - %15.7g", "",
            pfloat->minimum, pfloat->maximum);
        break;
      }
      case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (param);

        g_print ("%-23.23s Double. Default: %-8.8s %15.7g\n", "", "",
            g_value_get_double (&value));
        g_print ("%-23.23s Range: %15.7g - %15.7g", "",
            pdouble->minimum, pdouble->maximum);
        break;
      }
      default:
        if (param->value_type == GST_TYPE_URI) {
          g_print ("%-23.23s URI", "");
        }
        if (param->value_type == GST_TYPE_CAPS) {
          const GstCaps *caps = gst_value_get_caps (&value);

          if (!caps)
            g_print ("%-23.23s Caps (NULL)", "");
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

          g_print ("%-23.23s Enum \"%s\" (default %d, \"%s\")", "",
              g_type_name (G_VALUE_TYPE (&value)),
              enum_value, values[j].value_nick);

          j = 0;
          while (values[j].value_name) {
            g_print ("\n%-23.23s    (%d): \t%s", "",
                values[j].value, values[j].value_nick);
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

          g_print ("%-23.23s Flags \"%s\" (default %d, \"%s\")", "",
              g_type_name (G_VALUE_TYPE (&value)),
              flags_value, (flags ? flags->str : "(none)"));

          j = 0;
          while (values[j].value_name) {
            g_print ("\n%-23.23s    (%d): \t%s", "",
                values[j].value, values[j].value_nick);
            j++;
          }

          if (flags)
            g_string_free (flags, TRUE);
        } else if (G_IS_PARAM_SPEC_OBJECT (param)) {
          g_print ("%-23.23s Object of type \"%s\"", "",
              g_type_name (param->value_type));
        } else {
          g_print ("%-23.23s Unknown type %ld \"%s\"", "", param->value_type,
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
    g_print ("  none\n");
}

static void
print_pad_templates_info (GstElementFactory * factory, GstElement * element)
{
  GstElementClass *gstelement_class;
  const GList *pads;
  GstPadTemplate *padtemplate;

  g_print ("Pad Templates:\n");
  if (!factory->numpadtemplates) {
    g_print ("  none\n");
    return;
  }

  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  pads = factory->padtemplates;
  while (pads) {
    padtemplate = (GstPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    if (padtemplate->direction == GST_PAD_SRC)
      g_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      g_print ("  SINK template: '%s'\n", padtemplate->name_template);
    else
      g_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

    if (padtemplate->presence == GST_PAD_ALWAYS)
      g_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      g_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST) {
      g_print ("    Availability: On request\n");
      g_print ("      Has request_new_pad() function: %s\n",
          GST_DEBUG_FUNCPTR_NAME (gstelement_class->request_new_pad));
    } else
      g_print ("    Availability: UNKNOWN!!!\n");

    if (padtemplate->caps) {
      g_print ("    Capabilities:\n");
      print_caps (padtemplate->caps, "      ");
    }

    g_print ("\n");
  }
}

static void
print_element_flag_info (GstElement * element)
{
  gboolean have_flags = FALSE;

  g_print ("\nElement Flags:\n");

  if (GST_FLAG_IS_SET (element, GST_ELEMENT_COMPLEX)) {
    g_print ("  GST_ELEMENT_COMPLEX\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    g_print ("  GST_ELEMENT_DECOUPLED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_THREAD_SUGGESTED)) {
    g_print ("  GST_ELEMENT_THREADSUGGESTED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_EVENT_AWARE)) {
    g_print ("  GST_ELEMENT_EVENT_AWARE\n");
    have_flags = TRUE;
  }
  if (!have_flags)
    g_print ("  no flags set\n");

  if (GST_IS_BIN (element)) {
    g_print ("\nBin Flags:\n");
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      g_print ("  GST_BIN_FLAG_MANAGER\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET (element, GST_BIN_SELF_SCHEDULABLE)) {
      g_print ("  GST_BIN_SELF_SCHEDULABLE\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_PREFER_COTHREADS)) {
      g_print ("  GST_BIN_FLAG_PREFER_COTHREADS\n");
      have_flags = TRUE;
    }
    if (!have_flags)
      g_print ("  no flags set\n");
  }
}

static void
print_implementation_info (GstElement * element)
{
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;

  gstobject_class = GST_OBJECT_CLASS (G_OBJECT_GET_CLASS (element));
  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  g_print ("\nElement Implementation:\n");

  if (element->loopfunc)
    g_print ("  loopfunc()-based element: %s\n",
        GST_DEBUG_FUNCPTR_NAME (element->loopfunc));
  else
    g_print ("  No loopfunc(), must be chain-based or not configured yet\n");

  g_print ("  Has change_state() function: %s\n",
      GST_DEBUG_FUNCPTR_NAME (gstelement_class->change_state));
#ifndef GST_DISABLE_LOADSAVE
  g_print ("  Has custom save_thyself() function: %s\n",
      GST_DEBUG_FUNCPTR_NAME (gstobject_class->save_thyself));
  g_print ("  Has custom restore_thyself() function: %s\n",
      GST_DEBUG_FUNCPTR_NAME (gstobject_class->restore_thyself));
#endif
}

static void
print_clocking_info (GstElement * element)
{
  if (!gst_element_requires_clock (element) &&
      !(gst_element_provides_clock (element) &&
          gst_element_get_clock (element))) {
    g_print ("\nElement has no clocking capabilities.");
    return;
  }

  g_print ("\nClocking Interaction:\n");
  if (gst_element_requires_clock (element)) {
    g_print ("  element requires a clock\n");
  }

  if (gst_element_provides_clock (element)) {
    GstClock *clock;

    clock = gst_element_get_clock (element);
    if (clock)
      g_print ("  element provides a clock: %s\n", GST_OBJECT_NAME (clock));
    else
      g_print ("  element is supposed to provide a clock but returned NULL\n");
  }
}

#ifndef GST_DISABLE_INDEX
static void
print_index_info (GstElement * element)
{
  if (gst_element_is_indexable (element)) {
    g_print ("\nIndexing capabilities:\n");
    g_print ("  element can do indexing\n");
  } else {
    g_print ("\nElement has no indexing capabilities.\n");
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

  g_print ("\nPads:\n");

  if (!element->numpads) {
    g_print ("  none\n");
    return;
  }

  pads = gst_element_get_pad_list (element);
  while (pads) {
    pad = GST_PAD (pads->data);
    pads = g_list_next (pads);
    realpad = GST_PAD_REALIZE (pad);

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

    g_print ("    Implementation:\n");
    if (realpad->chainfunc)
      g_print ("      Has chainfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->chainfunc));
    if (realpad->getfunc)
      g_print ("      Has getfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->getfunc));
    if (realpad->formatsfunc != gst_pad_get_formats_default) {
      g_print ("      Supports seeking/conversion/query formats:\n");
      print_formats (gst_pad_get_formats (GST_PAD (realpad)));
    }
    if (realpad->convertfunc != gst_pad_convert_default)
      g_print ("      Has custom convertfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->convertfunc));
    if (realpad->eventfunc != gst_pad_event_default)
      g_print ("      Has custom eventfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->eventfunc));
    if (realpad->eventmaskfunc != gst_pad_get_event_masks_default) {
      g_print ("        Provides event masks:\n");
      print_event_masks (gst_pad_get_event_masks (GST_PAD (realpad)));
    }
    if (realpad->queryfunc != gst_pad_query_default)
      g_print ("      Has custom queryfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->queryfunc));
    if (realpad->querytypefunc != gst_pad_get_query_types_default) {
      g_print ("        Provides query types:\n");
      print_query_types (gst_pad_get_query_types (GST_PAD (realpad)));
    }

    if (realpad->intlinkfunc != gst_pad_get_internal_links_default)
      g_print ("      Has custom intconnfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->intlinkfunc));

    if (realpad->bufferallocfunc)
      g_print ("      Has bufferallocfunc(): %s\n",
          GST_DEBUG_FUNCPTR_NAME (realpad->bufferallocfunc));

    if (pad->padtemplate)
      g_print ("    Pad Template: '%s'\n", pad->padtemplate->name_template);

    if (realpad->caps) {
      g_print ("    Capabilities:\n");
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
    g_print ("\nDynamic Parameters:\n");

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
      if (k == 0)
        g_print ("\nElement Signals:\n");
      else
        g_print ("\nElement Actions:\n");
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

      g_print ("  \"%s\" :  %s user_function (%s* object",
          query->signal_name,
          g_type_name (query->return_type), g_type_name (type));

      for (j = 0; j < query->n_params; j++)
        g_print (",\n%s%s arg%d", indent,
            g_type_name (query->param_types[j]), j);

      if (k == 0)
        g_print (",\n%sgpointer user_data);\n", indent);
      else
        g_print (");\n");

      g_free (indent);
    }

    g_free (query);
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
  if (children)
    g_print ("\nChildren:\n");

  while (children) {
    g_print ("  %s\n", GST_ELEMENT_NAME (GST_ELEMENT (children->data)));
    children = g_list_next (children);
  }
}

static void
print_element_list (void)
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
        g_print ("%s:  %s: %s\n", plugin->desc.name,
            GST_PLUGIN_FEATURE_NAME (factory), factory->details.longname);
      }
#ifndef GST_DISABLE_INDEX
      else if (GST_IS_INDEX_FACTORY (feature)) {
        GstIndexFactory *factory;

        factory = GST_INDEX_FACTORY (feature);
        g_print ("%s:  %s: %s\n", plugin->desc.name,
            GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      }
#endif
      else if (GST_IS_TYPE_FIND_FACTORY (feature)) {
        GstTypeFindFactory *factory;

        factory = GST_TYPE_FIND_FACTORY (feature);
        g_print ("%s: %s: ", plugin->desc.name,
            gst_plugin_feature_get_name (feature));
        if (factory->extensions) {
          guint i = 0;

          while (factory->extensions[i]) {
            g_print ("%s%s", i > 0 ? ", " : "", factory->extensions[i]);
            i++;
          }
          g_print ("\n");
        } else {
          g_print ("no extensions\n");
        }
      } else if (GST_IS_SCHEDULER_FACTORY (feature)) {
        GstSchedulerFactory *factory;

        factory = GST_SCHEDULER_FACTORY (feature);
        g_print ("%s:  %s: %s\n", plugin->desc.name,
            GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      } else {
        g_print ("%s:  %s (%s)\n", plugin->desc.name,
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

  g_print ("Plugin Details:\n");
  g_print ("  Name:\t\t%s\n", plugin->desc.name);
  g_print ("  Description:\t%s\n", plugin->desc.description);
  g_print ("  Filename:\t%s\n", plugin->filename);
  g_print ("  Version:\t%s\n", plugin->desc.version);
  g_print ("  License:\t%s\n", plugin->desc.license);
  g_print ("  Package:\t%s\n", plugin->desc.package);
  g_print ("  Origin URL:\t%s\n", plugin->desc.origin);
  g_print ("\n");

  features = gst_plugin_get_feature_list (plugin);

  while (features) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (features->data);

    if (GST_IS_ELEMENT_FACTORY (feature)) {
      GstElementFactory *factory;

      factory = GST_ELEMENT_FACTORY (feature);
      g_print ("  %s: %s\n", GST_OBJECT_NAME (factory),
          factory->details.longname);
      num_elements++;
    }
#ifndef GST_DISABLE_INDEX
    else if (GST_IS_INDEX_FACTORY (feature)) {
      GstIndexFactory *factory;

      factory = GST_INDEX_FACTORY (feature);
      g_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
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
      g_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
      num_schedulers++;
    } else {
      g_print ("  %s (%s)\n", gst_object_get_name (GST_OBJECT (feature)),
          g_type_name (G_OBJECT_TYPE (feature)));
      num_other++;
    }
    num_features++;
    features = g_list_next (features);
  }
  g_print ("\n  %d features:\n", num_features);
  if (num_elements > 0)
    g_print ("  +-- %d elements\n", num_elements);
  if (num_types > 0)
    g_print ("  +-- %d types\n", num_types);
  if (num_schedulers > 0)
    g_print ("  +-- %d schedulers\n", num_schedulers);
  if (num_indexes > 0)
    g_print ("  +-- %d indexes\n", num_indexes);
  if (num_other > 0)
    g_print ("  +-- %d other objects\n", num_other);

  g_print ("\n");
}

static int
print_element_features (const gchar * element_name)
{
  GstPluginFeature *feature;

  /* FIXME implement other pretty print function for these */
  feature = gst_registry_pool_find_feature (element_name,
      GST_TYPE_SCHEDULER_FACTORY);
  if (feature) {
    g_print ("%s: a scheduler\n", element_name);
    return 0;
  }
#ifndef GST_DISABLE_INDEX
  feature = gst_registry_pool_find_feature (element_name,
      GST_TYPE_INDEX_FACTORY);
  if (feature) {
    g_print ("%s: an index\n", element_name);
    return 0;
  }
#endif
  feature = gst_registry_pool_find_feature (element_name,
      GST_TYPE_TYPE_FIND_FACTORY);
  if (feature) {
    g_print ("%s: a typefind function\n", element_name);
    return 0;
  }
#ifndef GST_DISABLE_URI
  feature = gst_registry_pool_find_feature (element_name, GST_TYPE_URI_HANDLER);
  if (feature) {
    g_print ("%s: an uri handler\n", element_name);
    return 0;
  }
#endif

  return -1;
}

int
main (int argc, char *argv[])
{
  GstElementFactory *factory;
  GstPlugin *plugin;
  gchar *so;
  struct poptOption options[] = {
    POPT_TABLEEND
  };

#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  gst_init_with_popt_table (&argc, &argv, options);
  gst_control_init (&argc, &argv);

  /* if no arguments, print out list of elements */
  if (argc == 1) {
    print_element_list ();
    /* else we try to get a factory */
  } else {
    /* only search for a factory if there's not a '.so' */
    if (!strstr (argv[1], ".so")) {
      factory = gst_element_factory_find (argv[1]);
      /* if there's a factory, print out the info */
      if (factory) {
        GstElement *element;
        gint maxlevel = 0;

        element = gst_element_factory_create (factory, "element");
        if (!element) {
          g_print ("couldn't construct element for some reason\n");
          return -1;
        }

        print_factory_details_info (factory);

        print_hierarchy (G_OBJECT_TYPE (element), 0, &maxlevel);

        print_pad_templates_info (factory, element);
        print_element_flag_info (element);
        print_implementation_info (element);
        print_clocking_info (element);
        print_index_info (element);
        print_pad_info (element);
        print_element_properties_info (element);
        print_dynamic_parameters_info (element);
        print_signal_info (element);
        print_children_info (element);

        return 0;
      } else {
        return print_element_features (argv[1]);
      }
    } else {
      /* strip the .so */
      so = strstr (argv[1], ".so");
      so[0] = '\0';
    }

    /* otherwise assume it's a plugin */
    plugin = gst_registry_pool_find_plugin (argv[1]);

    /* if there is such a plugin, print out info */

    if (plugin) {
      print_plugin_info (plugin);

    } else {
      g_print ("no such element or plugin '%s'\n", argv[1]);
      return -1;
    }
  }

  return 0;
}
