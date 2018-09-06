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
#define YY_NO_UNISTD_H
#include "ges-parse-lex.h"

struct _GESCommandLineFormatterPrivate
{
  gpointer dummy;
};


G_DEFINE_TYPE_WITH_PRIVATE (GESCommandLineFormatter, ges_command_line_formatter,
    GES_TYPE_FORMATTER);

static gboolean
_ges_command_line_formatter_add_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error);
static gboolean
_ges_command_line_formatter_add_effect (GESTimeline * timeline,
    GstStructure * structure, GError ** error);
static gboolean
_ges_command_line_formatter_add_test_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error);
static gboolean
_ges_command_line_formatter_add_title_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error);

typedef struct
{
  const gchar *long_name;
  const gchar *short_name;
  GType type;
  const gchar *new_name;
  const gchar *desc;
} Property;

// Currently Clip has the most properties.. adapt as needed
#define MAX_PROPERTIES 8
typedef struct
{
  const gchar *long_name;
  gchar short_name;
  ActionFromStructureFunc callback;
  const gchar *description;
  /* The first property must be the ID on the command line */
  Property properties[MAX_PROPERTIES];
} GESCommandLineOption;

/*  *INDENT-OFF* */
static GESCommandLineOption options[] = {
  {"clip", 'c', (ActionFromStructureFunc) _ges_command_line_formatter_add_clip,
    "<clip uri> - Adds a clip in the timeline.",
    {
      {
        "uri", "n", 0, "asset-id",
        "The URI of the media file."
      },
      {
        "name", "n", 0, NULL,
        "The name of the clip, can be used as an ID later."
      },
      {
        "start", "s",GST_TYPE_CLOCK_TIME, NULL,
        "The starting position of the clip in the timeline."
      },
      {
        "duration", "d", GST_TYPE_CLOCK_TIME, NULL,
        "The duration of the clip."
      },
      {
        "inpoint", "i", GST_TYPE_CLOCK_TIME, NULL,
        "The inpoint of the clip (time in the input file to start playing from)."
      },
      {
        "track-types", "tt", 0, NULL,
        "The type of the tracks where the clip should be used (audio or video or audio+video)."
      },
      {
        "layer", "l", 0, NULL,
        "The priority of the layer into which the clip should be added."
      },
      {NULL, 0, 0, NULL, FALSE},
    },
  },
  {"effect", 'e', (ActionFromStructureFunc) _ges_command_line_formatter_add_effect,
    "<effect bin description> - Adds an effect as specified by 'bin-description',\n"
    "similar to gst-launch-style pipeline description, without setting properties\n"
    "(see `set-` for information about how to set properties).\n",
    {
      {
        "bin-description", "d", 0, "asset-id",
        "gst-launch style bin description."
      },
      {
        "element-name", "e", 0, NULL,
        "The name of the element to apply the effect on."
      },
      {
        "name", "n", 0, "child-name",
        "The name to be given to the effect."
      },
      {NULL, NULL, 0, NULL, FALSE},
    },
  },
  {"test-clip", 0, (ActionFromStructureFunc) _ges_command_line_formatter_add_test_clip,
    "<test clip pattern> - Add a test clip in the timeline.",
    {
      {
        "pattern", "p", 0, NULL,
        "The testsource pattern name."
      },
      {
        "name", "n", 0, NULL,
        "The name of the clip, can be used as an ID later."
      },
      {
        "start", "s",GST_TYPE_CLOCK_TIME, NULL,
        "The starting position of the clip in the timeline."
      },
      {
        "duration", "d", GST_TYPE_CLOCK_TIME, NULL,
        "The duration of the clip."
      },
      {
        "inpoint", "i", GST_TYPE_CLOCK_TIME, NULL,
        "The inpoint of the clip (time in the input file to start playing)."
      },
      {
        "layer", "l", 0, NULL,
        "The priority of the layer into which the clip should be added."
      },
      {NULL, 0, 0, NULL, FALSE},
    },
  },
  {"title", 'c', (ActionFromStructureFunc) _ges_command_line_formatter_add_title_clip,
    "<title text> - Adds a clip in the timeline.",
    {
      {
        "text", "n", 0, NULL,
        "The text to be used as title."
      },
      {
        "name", "n", 0, NULL,
        "The name of the clip, can be used as an ID later."
      },
      {
        "start", "s",GST_TYPE_CLOCK_TIME, NULL,
        "The starting position of the clip in the timeline."
      },
      {
        "duration", "d", GST_TYPE_CLOCK_TIME, NULL,
        "The duration of the clip."
      },
      {
        "inpoint", "i", GST_TYPE_CLOCK_TIME, NULL,
        "The inpoint of the clip (time in the input file to start playing from)."
      },
      {
        "track-types", "tt", 0, NULL,
        "The type of the tracks where the clip should be used (audio or video or audio+video)."
      },
      {
        "layer", "l", 0, NULL,
        "The priority of the layer into which the clip should be added."
      },
      {NULL, 0, 0, NULL, FALSE},
    },
  },
  {
    "set-", 0, NULL,
    "<property name> <value> - Set a property on the last added element.\n"
    "Any child property that exists on the previously added element\n"
    "can be used as <property name>",
    {
      {NULL, NULL, 0, NULL, FALSE},
    },
  },
};
/*  *INDENT-ON* */

/* Should always be in the same order as the options */
typedef enum
{
  CLIP,
  EFFECT,
  TEST_CLIP,
  TITLE,
  SET,
} GESCommandLineOptionType;

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
_cleanup_fields (const Property * field_names, GstStructure * structure,
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
  if (!_cleanup_fields (options[CLIP].properties, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESUriClip", NULL);

  return _ges_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_test_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  if (!_cleanup_fields (options[TEST_CLIP].properties, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESTestClip", NULL);
  gst_structure_set (structure, "asset-id", G_TYPE_STRING,
      gst_structure_get_string (structure, "pattern"), NULL);

  return _ges_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_title_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  if (!_cleanup_fields (options[TEST_CLIP].properties, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESTitleClip", NULL);
  gst_structure_set (structure, "asset-id", G_TYPE_STRING, "GESTitleClip",
      NULL);
  GST_ERROR ("Structure: %" GST_PTR_FORMAT, structure);

  return _ges_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_effect (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  if (!_cleanup_fields (options[EFFECT].properties, structure, error))
    return FALSE;

  gst_structure_set (structure, "child-type", G_TYPE_STRING, "GESEffect", NULL);

  return _ges_container_add_child_from_struct (timeline, structure, error);
}

gchar *
ges_command_line_formatter_get_help (gint nargs, gchar ** commands)
{
  gint i;
  GString *help = g_string_new (NULL);

  for (i = 0; i < G_N_ELEMENTS (options); i++) {
    gboolean print = nargs == 0;
    GESCommandLineOption option = options[i];

    if (!print) {
      gint j;

      for (j = 0; j < nargs; j++) {
        gchar *cname = commands[j][0] == '+' ? &commands[j][1] : commands[j];

        if (!g_strcmp0 (cname, option.long_name)) {
          print = TRUE;
          break;
        }
      }
    }

    if (print) {
      g_string_append_printf (help, "%s%s %s\n",
          option.properties[0].long_name ? "+" : "",
          option.long_name, option.description);

      if (option.properties[0].long_name) {
        gint j;

        g_string_append (help, "  Properties:\n");

        for (j = 1; option.properties[j].long_name; j++) {
          Property prop = option.properties[j];
          g_string_append_printf (help, "    * %s: %s\n", prop.long_name,
              prop.desc);
        }
      }

      g_string_append (help, "\n");
    }
  }

  return g_string_free (help, FALSE);
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

    for (i = 0; i < G_N_ELEMENTS (options); i++) {
      if (gst_structure_has_name (tmp->data, options[i].long_name)
          || (strlen (name) == 1 && *name == options[i].short_name)) {
        EXEC (((ActionFromStructureFunc) options[i].callback), tmp->data, &err);
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
ges_command_line_formatter_init (GESCommandLineFormatter * formatter)
{
  formatter->priv = ges_command_line_formatter_get_instance_private (formatter);
}

static void
ges_command_line_formatter_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_command_line_formatter_parent_class)->finalize (object);
}

static void
ges_command_line_formatter_class_init (GESCommandLineFormatterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESFormatterClass *formatter_klass = GES_FORMATTER_CLASS (klass);

  object_class->finalize = ges_command_line_formatter_finalize;

  formatter_klass->can_load_uri = _can_load;
  formatter_klass->load_from_uri = _load;
  formatter_klass->rank = GST_RANK_MARGINAL;
}
