/* GStreamer gst-xmlinspect
 * Copyright (C) 2003 Wim Taymans <wtay@chello.be>
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

/* FIXME 0.11: remove gst-xmlinspect and gst-feedback etc. */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <locale.h>
#include <glib/gprintf.h>

#include "tools.h"

#define PUT_START_TAG(pfx,tag)                                  \
G_STMT_START{                                                   \
  g_print ("%*.*s<%s>\n", pfx, pfx, "", tag);                   \
}G_STMT_END

#define PUT_END_TAG(pfx,tag)                                    \
G_STMT_START{                                                   \
  g_print ("%*.*s</%s>\n", pfx, pfx, "", tag);                  \
}G_STMT_END

#define PUT_ESCAPED(pfx,tag,value)                              \
G_STMT_START{                                                   \
  const gchar *toconv = value;                                  \
  if (value) {                                                  \
    gchar *v = g_markup_escape_text (toconv, strlen (toconv));  \
    g_print ("%*.*s<%s>%s</%s>\n", pfx, pfx, "", tag, v, tag);  \
    g_free (v);                                                 \
  }                                                             \
}G_STMT_END

#ifdef G_HAVE_ISO_VARARGS

#define PUT_STRING(pfx, ...)                                    \
G_STMT_START{                                                   \
  gchar *ps_val = g_strdup_printf(__VA_ARGS__);                 \
  g_print ("%*.*s%s\n", pfx, pfx, "", ps_val);                  \
  g_free(ps_val);                                               \
}G_STMT_END

#elif defined(G_HAVE_GNUC_VARARGS)

#define PUT_STRING(pfx, str, a...)                              \
G_STMT_START{                                                   \
  g_print ("%*.*s"str"\n", pfx, pfx, "" , ##a);                 \
}G_STMT_END

#else

static inline void
PUT_STRING (int pfx, const char *format, ...)
{
  va_list varargs;

  g_print ("%*.*s", pfx, pfx, "");
  va_start (varargs, format);
  g_vprintf (format, varargs);
  va_end (varargs);
  g_print ("\n");
}

#endif

static void
print_caps (const GstCaps * caps, gint pfx)
{
  char *s;

  if (!caps)
    return;

  s = gst_caps_to_string (caps);
  PUT_ESCAPED (pfx, "caps", s);
  g_free (s);
}

#if 0
static void
print_formats (const GstFormat * formats, gint pfx)
{
  while (formats && *formats) {
    const GstFormatDefinition *definition;

    definition = gst_format_get_details (*formats);
    if (definition)
      PUT_STRING (pfx, "<format id=\"%d\" nick=\"%s\">%s</format>",
          *formats, definition->nick, definition->description);
    else
      PUT_STRING (pfx, "<format id=\"%d\">unknown</format>", *formats);

    formats++;
  }
}
#endif

static void
print_query_types (const GstQueryType * types, gint pfx)
{
  while (types && *types) {
    const GstQueryTypeDefinition *definition;

    definition = gst_query_type_get_details (*types);
    if (definition)
      PUT_STRING (pfx, "<query-type id=\"%d\" nick=\"%s\">%s</query-type>",
          *types, definition->nick, definition->description);
    else
      PUT_STRING (pfx, "<query-type id=\"%d\">unknown</query-type>", *types);

    types++;
  }
}

#if 0
static void
print_event_masks (const GstEventMask * masks, gint pfx)
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
    PUT_STRING (pfx, "<event type=\"%s\">", value->value_nick);

    while (flags) {
      GFlagsValue *value;

      if (flags & 1) {
        value = g_flags_get_first_value (flags_class, 1 << index);

        if (value)
          PUT_ESCAPED (pfx + 1, "flag", value->value_nick);
        else
          PUT_ESCAPED (pfx + 1, "flag", "?");
      }
      flags >>= 1;
      index++;
    }
    PUT_END_TAG (pfx, "event");

    masks++;
  }
}
#endif

static void
output_hierarchy (GType type, gint level, gint * maxlevel)
{
  GType parent;

  parent = g_type_parent (type);

  *maxlevel = *maxlevel + 1;
  level++;

  PUT_STRING (level, "<object name=\"%s\">", g_type_name (type));

  if (parent)
    output_hierarchy (parent, level, maxlevel);

  PUT_END_TAG (level, "object");
}

static void
print_element_properties (GstElement * element, gint pfx)
{
  GParamSpec **property_specs;
  guint num_properties;
  gint i;
  gboolean readable;

  property_specs = g_object_class_list_properties
      (G_OBJECT_GET_CLASS (element), &num_properties);

  PUT_START_TAG (pfx, "element-properties");

  for (i = 0; i < num_properties; i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];

    readable = FALSE;

    g_value_init (&value, param->value_type);
    if (param->flags & G_PARAM_READABLE) {
      g_object_get_property (G_OBJECT (element), param->name, &value);
      readable = TRUE;
    }
    PUT_START_TAG (pfx + 1, "element-property");
    PUT_ESCAPED (pfx + 2, "name", g_param_spec_get_name (param));
    PUT_ESCAPED (pfx + 2, "type", g_type_name (param->value_type));
    PUT_ESCAPED (pfx + 2, "nick", g_param_spec_get_nick (param));
    PUT_ESCAPED (pfx + 2, "blurb", g_param_spec_get_blurb (param));
    if (readable) {
      PUT_ESCAPED (pfx + 2, "flags", "RW");
    } else {
      PUT_ESCAPED (pfx + 2, "flags", "W");
    }

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING:
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      case G_TYPE_BOOLEAN:
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      case G_TYPE_ULONG:
      {
        GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (param);

        PUT_STRING (pfx + 2, "<range min=\"%lu\" max=\"%lu\"/>",
            pulong->minimum, pulong->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      case G_TYPE_LONG:
      {
        GParamSpecLong *plong = G_PARAM_SPEC_LONG (param);

        PUT_STRING (pfx + 2, "<range min=\"%ld\" max=\"%ld\"/>",
            plong->minimum, plong->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      case G_TYPE_UINT:
      {
        GParamSpecUInt *puint = G_PARAM_SPEC_UINT (param);

        PUT_STRING (pfx + 2, "<range min=\"%u\" max=\"%u\"/>",
            puint->minimum, puint->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      case G_TYPE_INT:
      {
        GParamSpecInt *pint = G_PARAM_SPEC_INT (param);

        PUT_STRING (pfx + 2, "<range min=\"%d\" max=\"%d\"/>",
            pint->minimum, pint->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (param);

        PUT_STRING (pfx + 2,
            "<range min=\"%" G_GUINT64_FORMAT "\" max=\"%" G_GUINT64_FORMAT
            "\"/>", puint64->minimum, puint64->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      case G_TYPE_INT64:
      {
        GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (param);

        PUT_STRING (pfx + 2,
            "<range min=\"%" G_GINT64_FORMAT "\" max=\"%" G_GINT64_FORMAT
            "\"/>", pint64->minimum, pint64->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (param);

        PUT_STRING (pfx + 2, "<range min=\"%f\" max=\"%f\"/>",
            pfloat->minimum, pfloat->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (param);

        PUT_STRING (pfx + 2, "<range min=\"%g\" max=\"%g\"/>",
            pdouble->minimum, pdouble->maximum);
        PUT_ESCAPED (pfx + 2, "default", g_strdup_value_contents (&value));
        break;
      }
      default:
        if (param->value_type == GST_TYPE_CAPS) {
          GstCaps *caps = g_value_peek_pointer (&value);

          if (!caps)
            PUT_ESCAPED (pfx + 2, "default", "NULL");
          else {
            print_caps (caps, 2);
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
          PUT_STRING (pfx + 2, "<default>%d</default>", values[j].value);

          PUT_START_TAG (pfx + 2, "enum-values");
          j = 0;
          while (values[j].value_name) {
            PUT_STRING (pfx + 3, "<value value=\"%d\" nick=\"%s\"/>",
                values[j].value, values[j].value_nick);
            j++;
          }
          PUT_END_TAG (pfx + 2, "enum-values");
        } else if (G_IS_PARAM_SPEC_FLAGS (param)) {
          GFlagsValue *values;
          guint j = 0;
          gint flags_value;

          values = G_FLAGS_CLASS (g_type_class_ref (param->value_type))->values;
          flags_value = g_value_get_flags (&value);

          PUT_STRING (pfx + 2, "<default>%d</default>", flags_value);

          PUT_START_TAG (pfx + 2, "flags");
          j = 0;
          while (values[j].value_name) {
            PUT_STRING (pfx + 3, "<flag value=\"%d\" nick=\"%s\"/>",
                values[j].value, values[j].value_nick);
            j++;
          }
          PUT_END_TAG (pfx + 2, "flags");
        } else if (G_IS_PARAM_SPEC_OBJECT (param)) {
          PUT_ESCAPED (pfx + 2, "object-type", g_type_name (param->value_type));
        }
        break;
    }

    PUT_END_TAG (pfx + 1, "element-property");
  }
  PUT_END_TAG (pfx, "element-properties");
  g_free (property_specs);
}

static void
print_element_signals (GstElement * element, gint pfx)
{
  guint *signals;
  guint nsignals;
  gint i, k;
  GSignalQuery *query;

  signals = g_signal_list_ids (G_OBJECT_TYPE (element), &nsignals);
  for (k = 0; k < 2; k++) {
    gint counted = 0;

    if (k == 0)
      PUT_START_TAG (pfx, "element-signals");
    else
      PUT_START_TAG (pfx, "element-actions");

    for (i = 0; i < nsignals; i++) {
      gint n_params;
      GType return_type;
      const GType *param_types;
      gint j;

      query = g_new0 (GSignalQuery, 1);
      g_signal_query (signals[i], query);

      if ((k == 0 && !(query->signal_flags & G_SIGNAL_ACTION)) ||
          (k == 1 && (query->signal_flags & G_SIGNAL_ACTION))) {
        n_params = query->n_params;
        return_type = query->return_type;
        param_types = query->param_types;

        PUT_START_TAG (pfx + 1, "signal");
        PUT_ESCAPED (pfx + 2, "name", query->signal_name);
        PUT_ESCAPED (pfx + 2, "return-type", g_type_name (return_type));
        PUT_ESCAPED (pfx + 2, "object-type",
            g_type_name (G_OBJECT_TYPE (element)));

        PUT_START_TAG (pfx + 2, "params");
        for (j = 0; j < n_params; j++) {
          PUT_ESCAPED (pfx + 3, "type", g_type_name (param_types[j]));
        }

        PUT_END_TAG (pfx + 2, "params");

        PUT_END_TAG (pfx + 1, "signal");

        counted++;
      }

      g_free (query);
    }
    if (k == 0)
      PUT_END_TAG (pfx, "element-signals");
    else
      PUT_END_TAG (pfx, "element-actions");
  }
}

static gint
print_element_info (GstElementFactory * factory)
{
  GstElement *element;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;
  GList *pads;
  GstPad *pad;
  GstStaticPadTemplate *padtemplate;
  gint maxlevel = 0;

  element = gst_element_factory_create (factory, "element");
  if (!element) {
    g_print ("couldn't construct element for some reason\n");
    return -1;
  }
  PUT_START_TAG (0, "element");
  PUT_ESCAPED (1, "name", GST_PLUGIN_FEATURE_NAME (factory));

  gstobject_class = GST_OBJECT_CLASS (G_OBJECT_GET_CLASS (element));
  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  PUT_START_TAG (1, "details");
  PUT_ESCAPED (2, "long-name", factory->details.longname);
  PUT_ESCAPED (2, "class", factory->details.klass);
  PUT_ESCAPED (2, "description", factory->details.description);
  PUT_ESCAPED (2, "authors", factory->details.author);
  PUT_END_TAG (1, "details");

  output_hierarchy (G_OBJECT_TYPE (element), 0, &maxlevel);

  PUT_START_TAG (1, "pad-templates");
  if (factory->numpadtemplates) {
    pads = factory->staticpadtemplates;
    while (pads) {
      padtemplate = (GstStaticPadTemplate *) (pads->data);
      pads = g_list_next (pads);

      PUT_START_TAG (2, "pad-template");
      PUT_ESCAPED (3, "name", padtemplate->name_template);

      if (padtemplate->direction == GST_PAD_SRC)
        PUT_ESCAPED (3, "direction", "src");
      else if (padtemplate->direction == GST_PAD_SINK)
        PUT_ESCAPED (3, "direction", "sink");
      else
        PUT_ESCAPED (3, "direction", "unknown");

      if (padtemplate->presence == GST_PAD_ALWAYS)
        PUT_ESCAPED (3, "presence", "always");
      else if (padtemplate->presence == GST_PAD_SOMETIMES)
        PUT_ESCAPED (3, "presence", "sometimes");
      else if (padtemplate->presence == GST_PAD_REQUEST) {
        PUT_ESCAPED (3, "presence", "request");
        PUT_ESCAPED (3, "request-function",
            GST_DEBUG_FUNCPTR_NAME (gstelement_class->request_new_pad));
      } else
        PUT_ESCAPED (3, "presence", "unknown");

      if (padtemplate->static_caps.string) {
        print_caps (gst_static_caps_get (&padtemplate->static_caps), 3);
      }
      PUT_END_TAG (2, "pad-template");
    }
  }
  PUT_END_TAG (1, "pad-templates");

  PUT_START_TAG (1, "element-flags");
  PUT_END_TAG (1, "element-flags");

  if (GST_IS_BIN (element)) {
    PUT_START_TAG (1, "bin-flags");

    PUT_END_TAG (1, "bin-flags");
  }


  PUT_START_TAG (1, "element-implementation");

  PUT_STRING (2, "<state-change function=\"%s\"/>",
      GST_DEBUG_FUNCPTR_NAME (gstelement_class->change_state));

#ifndef GST_DISABLE_LOADSAVE
  PUT_STRING (2, "<save function=\"%s\"/>",
      GST_DEBUG_FUNCPTR_NAME (gstobject_class->save_thyself));
  PUT_STRING (2, "<load function=\"%s\"/>",
      GST_DEBUG_FUNCPTR_NAME (gstobject_class->restore_thyself));
#endif
  PUT_END_TAG (1, "element-implementation");

  PUT_START_TAG (1, "clocking-interaction");
  if (gst_element_requires_clock (element)) {
    PUT_STRING (2, "<requires-clock/>");
  }
  if (gst_element_provides_clock (element)) {
    GstClock *clock;

    clock = gst_element_get_clock (element);
    if (clock)
      PUT_STRING (2, "<provides-clock name=\"%s\"/>", GST_OBJECT_NAME (clock));
  }
  PUT_END_TAG (1, "clocking-interaction");

  if (gst_element_is_indexable (element)) {
    PUT_STRING (1, "<indexing-capabilities/>");
  }

  PUT_START_TAG (1, "pads");
  if (element->numpads) {
    const GList *pads;

    pads = element->pads;
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);

      PUT_START_TAG (2, "pad");
      PUT_ESCAPED (3, "name", gst_pad_get_name (pad));

      if (gst_pad_get_direction (pad) == GST_PAD_SRC)
        PUT_ESCAPED (3, "direction", "src");
      else if (gst_pad_get_direction (pad) == GST_PAD_SINK)
        PUT_ESCAPED (3, "direction", "sink");
      else
        PUT_ESCAPED (3, "direction", "unknown");

      if (pad->padtemplate)
        PUT_ESCAPED (3, "template", pad->padtemplate->name_template);

      PUT_START_TAG (3, "implementation");
      if (pad->chainfunc)
        PUT_STRING (4, "<chain-based function=\"%s\"/>",
            GST_DEBUG_FUNCPTR_NAME (pad->chainfunc));
      if (pad->getrangefunc)
        PUT_STRING (4, "<get-range-based function=\"%s\"/>",
            GST_DEBUG_FUNCPTR_NAME (pad->getrangefunc));
      if (pad->eventfunc != gst_pad_event_default)
        PUT_STRING (4, "<event-function function=\"%s\"/>",
            GST_DEBUG_FUNCPTR_NAME (pad->eventfunc));
      if (pad->queryfunc != gst_pad_query_default)
        PUT_STRING (4, "<query-function function=\"%s\"/>",
            GST_DEBUG_FUNCPTR_NAME (pad->queryfunc));
      if (pad->querytypefunc != gst_pad_get_query_types_default) {
        PUT_STRING (4, "<query-type-func function=\"%s\">",
            GST_DEBUG_FUNCPTR_NAME (pad->querytypefunc));
        print_query_types (gst_pad_get_query_types (pad), 5);
        PUT_END_TAG (4, "query-type-func");
      }

      if (pad->iterintlinkfunc != gst_pad_iterate_internal_links_default)
        PUT_STRING (4, "<iterintlink-function function=\"%s\"/>",
            GST_DEBUG_FUNCPTR_NAME (pad->iterintlinkfunc));

      if (pad->bufferallocfunc)
        PUT_STRING (4, "<bufferalloc-function function=\"%s\"/>",
            GST_DEBUG_FUNCPTR_NAME (pad->bufferallocfunc));
      PUT_END_TAG (3, "implementation");

      if (pad->caps) {
        print_caps (pad->caps, 3);
      }
      PUT_END_TAG (2, "pad");
    }
  }
  PUT_END_TAG (1, "pads");

  print_element_properties (element, 1);
  print_element_signals (element, 1);

  /* for compound elements */
  /* FIXME: gst_bin_get_list does not exist anymore
     if (GST_IS_BIN (element)) {
     GList *children;
     GstElement *child;
     PUT_START_TAG (1, "children");
     children = (GList *) gst_bin_get_list (GST_BIN (element));
     while (children) {
     child = GST_ELEMENT (children->data);
     children = g_list_next (children);

     PUT_ESCAPED (2, "child", GST_ELEMENT_NAME (child));
     }
     PUT_END_TAG (1, "children");
     }
   */
  PUT_END_TAG (0, "element");

  return 0;
}

int
main (int argc, char *argv[])
{
  GstElementFactory *factory;
  GOptionEntry options[] = {
    GST_TOOLS_GOPTION_VERSION,
    {NULL}
  };
  GOptionContext *ctx;
  GError *err = NULL;

  setlocale (LC_ALL, "");

  g_thread_init (NULL);

  gst_tools_set_prgname ("gst-xmlinspect");

  ctx = g_option_context_new ("[ELEMENT-NAME]");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    exit (1);
  }
  g_option_context_free (ctx);

  gst_tools_print_version ("gst-xmlinspect");

  /* if no arguments, print out all elements */
  if (argc == 1) {
    GList *features, *f;

    features = gst_registry_get_feature_list (gst_registry_get_default (),
        GST_TYPE_ELEMENT_FACTORY);

    PUT_STRING (0, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>");

    for (f = features; f != NULL; f = f->next)
      print_element_info (GST_ELEMENT_FACTORY (f->data));

    gst_plugin_feature_list_free (features);
    return 0;
  }

  /* else we try to get a factory */
  factory = gst_element_factory_find (argv[1]);

  /* if there's a factory, print out the info */
  if (factory) {
    PUT_STRING (0, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>");
    return print_element_info (factory);
  }

  /* otherwise, error out */
  g_printerr ("no such element '%s'\n", argv[1]);
  return -1;
}
