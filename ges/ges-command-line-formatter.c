/* GStreamer Editing Services
 *
 * Copyright (C) <2015> Thibault Saunier <tsaunier@gnome.org>
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

#include "ges-command-line-formatter.h"

#include "ges/ges-structured-interface.h"
#include "ges-structure-parser.h"
#include "ges-internal.h"
#include "parse_lex.h"

struct _GESCommandLineFormatterPrivate
{
  gpointer dummy;
};


G_DEFINE_TYPE (GESCommandLineFormatter, ges_command_line_formatter,
    GES_TYPE_FORMATTER);

typedef struct
{
  const gchar *long_name;
  const gchar *short_name;
  GType type;
  const gchar *new_name;
} Properties;

static gint                     /*  -1: not present, 0: failure, 1: OK */
_convert_to_clocktime (GstStructure * structure, const gchar * name,
    GstClockTime default_value)
{
  gint res = 1;
  gdouble val;
  GValue d_val = { 0 };
  GstClockTime timestamp;
  const GValue *gvalue = gst_structure_get_value (structure, name);

  if (gvalue == NULL) {
    timestamp = default_value;

    res = -1;

    goto done;
  }

  if (G_VALUE_TYPE (gvalue) == GST_TYPE_CLOCK_TIME)
    return 1;

  g_value_init (&d_val, G_TYPE_DOUBLE);
  if (!g_value_transform (gvalue, &d_val)) {
    GST_ERROR ("Could not get timestamp for %s", name);

    return 0;
  }
  val = g_value_get_double ((const GValue *) &d_val);

  if (val == -1.0)
    timestamp = GST_CLOCK_TIME_NONE;
  else
    timestamp = val * GST_SECOND;

done:
  gst_structure_set (structure, name, G_TYPE_UINT64, timestamp, NULL);

  return res;
}

static gboolean
_cleanup_fields (const Properties * field_names, GstStructure * structure,
    GError ** error)
{
  guint i;

  for (i = 0; field_names[i].long_name; i++) {
    gboolean exists = FALSE;

    /* Move shortly named fields to longname variante */
    if (gst_structure_has_field (structure, field_names[i].short_name)) {
      exists = TRUE;

      if (gst_structure_has_field (structure, field_names[i].long_name)) {
        *error = g_error_new (GES_ERROR, 0, "Using short and long name"
            " at the same time for property: %s, which one should I use?!",
            field_names[i].long_name);

        return FALSE;
      } else {
        const GValue *val =
            gst_structure_get_value (structure, field_names[i].short_name);

        gst_structure_set_value (structure, field_names[i].long_name, val);
        gst_structure_remove_field (structure, field_names[i].short_name);
      }
    } else if (gst_structure_has_field (structure, field_names[i].long_name)) {
      exists = TRUE;
    }

    if (exists) {
      if (field_names[i].type == GST_TYPE_CLOCK_TIME) {
        if (_convert_to_clocktime (structure, field_names[i].long_name, 0) == 0) {
          *error = g_error_new (GES_ERROR, 0, "Could not convert"
              " %s to GstClockTime", field_names[i].long_name);

          return FALSE;
        }
      }
    }

    if (field_names[i].new_name
        && gst_structure_has_field (structure, field_names[i].long_name)) {
      const GValue *val =
          gst_structure_get_value (structure, field_names[i].long_name);

      gst_structure_set_value (structure, field_names[i].new_name, val);
      gst_structure_remove_field (structure, field_names[i].long_name);
    }
  }

  return TRUE;
}

static gboolean
_ges_command_line_formatter_add_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  const Properties field_names[] = {
    {"uri", "n", 0, "asset-id"},
    {"name", "n", 0, NULL},
    {"start", "s", GST_TYPE_CLOCK_TIME, NULL},
    {"duration", "d", GST_TYPE_CLOCK_TIME, NULL},
    {"inpoint", "i", GST_TYPE_CLOCK_TIME, NULL},
    {"track-types", "tt", 0, NULL},
    {"layer", "l", 0, NULL},
    {NULL, 0, 0, NULL},
  };

  if (!_cleanup_fields (field_names, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESUriClip", NULL);

  return _ges_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_test_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  const Properties field_names[] = {
    {"pattern", "p", G_TYPE_STRING, NULL},
    {"name", "n", 0, NULL},
    {"start", "s", GST_TYPE_CLOCK_TIME, NULL},
    {"duration", "d", GST_TYPE_CLOCK_TIME, NULL},
    {"inpoint", "i", GST_TYPE_CLOCK_TIME, NULL},
    {"layer", "l", 0, NULL},
    {NULL, 0, 0, NULL},
  };

  if (!_cleanup_fields (field_names, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESTestClip", NULL);
  gst_structure_set (structure, "asset-id", G_TYPE_STRING,
      gst_structure_get_string (structure, "pattern"), NULL);

  return _ges_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_effect (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  const Properties field_names[] = {
    {"element-name", "e", 0, NULL},
    {"bin-description", "d", 0, "asset-id"},
    {"name", "n", 0, "child-name"},
    {NULL, NULL, 0, NULL},
  };

  if (!_cleanup_fields (field_names, structure, error))
    return FALSE;

  gst_structure_set (structure, "child-type", G_TYPE_STRING, "GESEffect", NULL);

  return _ges_container_add_child_from_struct (timeline, structure, error);
}

static GOptionEntry timeline_parsing_options[] = {
  {"clip", 'c', 0.0, G_OPTION_ARG_CALLBACK,
        &_ges_command_line_formatter_add_clip,
        "",
      "Adds a clip in the timeline\n"
        "       * start - s   : The start position of the element inside the layer.\n"
        "       * duration - d: The duration of the clip.\n"
        "       * inpoint - i     : The inpoint of the clip.\n"
        "       * track-types - tt: The type of the tracks where the clip should be used:\n"
        "          Examples:\n"
        "           * audio  / a\n"
        "           * video / v\n"
        "           * audio+video / a+v\n"
        "         Will default to all the media types in the clip that match the global track-types\n"},
  {"effect", 'e', 0.0, G_OPTION_ARG_CALLBACK,
        &_ges_command_line_formatter_add_effect, "",
      "Adds an effect as specified by 'bin-description'\n"
        "       * bin-description - d: The description of the effect bin with a gst-launch-style pipeline description.\n"
        "       * element-name - e   : The name of the element to apply the effect on.\n"},
  {"test-clip", 0, 0.0, G_OPTION_ARG_CALLBACK,
        &_ges_command_line_formatter_add_test_clip,
        "",
      "Add a test clip in the timeline\n"
        "           * start -s : The start position of the element inside the layer.\n"
        "           * duration -d : The duration of the clip."
        "           * inpoint - i : The inpoint of the clip.\n"},
};

GOptionGroup *
_ges_command_line_formatter_get_option_group (void)
{
  GOptionGroup *group;

  group = g_option_group_new ("GESCommandLineFormatter",
      "GStreamer Editing Services command line options to describe a timeline",
      "Show GStreamer Options", NULL, NULL);
  g_option_group_add_entries (group, timeline_parsing_options);

  return group;
}


static gboolean
_set_child_property (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  return _ges_set_child_property_from_struct (timeline, structure, error);
}

#define EXEC(func,structure,error) G_STMT_START { \
  gboolean res = ((ActionFromStructureFunc)func)(timeline, structure, error); \
  if (!res) {\
    GST_ERROR ("Could not execute: %" GST_PTR_FORMAT ", error: %s", structure, (*error)->message); \
    goto fail; \
  } \
} G_STMT_END


static GESStructureParser *
_parse_structures (const gchar * string)
{
  yyscan_t scanner;
  GESStructureParser *parser = ges_structure_parser_new ();

  priv_ges_parse_yylex_init_extra (parser, &scanner);
  priv_ges_parse_yy_scan_string (string, scanner);
  priv_ges_parse_yylex (scanner);
  priv_ges_parse_yylex_destroy (scanner);

  ges_structure_parser_end_of_file (parser);
  return parser;
}

static gboolean
_can_load (GESFormatter * dummy_formatter, const gchar * string,
    GError ** error)
{
  gboolean res = FALSE;
  GESStructureParser *parser;

  if (string == NULL)
    return FALSE;

  parser = _parse_structures (string);

  if (parser->structures)
    res = TRUE;

  gst_object_unref (parser);

  return res;
}

static gboolean
_load (GESFormatter * self, GESTimeline * timeline, const gchar * string,
    GError ** error)
{
  guint i;
  GList *tmp;
  GError *err;
  GESStructureParser *parser = _parse_structures (string);

  err = ges_structure_parser_get_error (parser);

  if (err) {
    if (error)
      *error = err;

    return FALSE;
  }

  g_object_set (timeline, "auto-transition", TRUE, NULL);
  if (!(ges_timeline_add_track (timeline, GES_TRACK (ges_video_track_new ()))))
    goto fail;

  if (!(ges_timeline_add_track (timeline, GES_TRACK (ges_audio_track_new ()))))
    goto fail;

  /* Here we've finished initializing our timeline, we're
   * ready to start using it... by solely working with the layer !*/
  for (tmp = parser->structures; tmp; tmp = tmp->next) {
    const gchar *name = gst_structure_get_name (tmp->data);
    if (g_str_has_prefix (name, "set-")) {
      EXEC (_set_child_property, tmp->data, &err);
      continue;
    }

    for (i = 0; i < G_N_ELEMENTS (timeline_parsing_options); i++) {
      if (gst_structure_has_name (tmp->data,
              timeline_parsing_options[i].long_name)
          || (strlen (name) == 1 &&
              *name == timeline_parsing_options[i].short_name)) {
        EXEC (((ActionFromStructureFunc) timeline_parsing_options[i].arg_data),
            tmp->data, &err);
      }
    }
  }

  gst_object_unref (parser);

  return TRUE;

fail:
  gst_object_unref (parser);
  if (err) {
    if (error)
      *error = err;
  }

  return FALSE;
}

static void
ges_command_line_formatter_init (GESCommandLineFormatter *
    ges_command_line_formatter)
{
  ges_command_line_formatter->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (ges_command_line_formatter,
      GES_TYPE_COMMAND_LINE_FORMATTER, GESCommandLineFormatterPrivate);

  /* TODO: Add initialization code here */
}

static void
ges_command_line_formatter_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (ges_command_line_formatter_parent_class)->finalize (object);
}

static void
ges_command_line_formatter_class_init (GESCommandLineFormatterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESFormatterClass *formatter_klass = GES_FORMATTER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESCommandLineFormatterPrivate));

  object_class->finalize = ges_command_line_formatter_finalize;

  formatter_klass->can_load_uri = _can_load;
  formatter_klass->load_from_uri = _load;
  formatter_klass->rank = GST_RANK_MARGINAL;
}
