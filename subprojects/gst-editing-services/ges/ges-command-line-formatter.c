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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
static gboolean
_ges_command_line_formatter_add_track (GESTimeline * timeline,
    GstStructure * structure, GError ** error);
static gboolean
_ges_command_line_formatter_add_keyframes (GESTimeline * timeline,
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
  const gchar *synopsis;
  const gchar *description;
  const gchar *examples;
  /* The first property must be the ID on the command line */
  Property properties[MAX_PROPERTIES];
} GESCommandLineOption;

/*  *INDENT-OFF* */
static GESCommandLineOption options[] = {
  {
    .long_name = "clip",
    .short_name='c',
    .callback=(ActionFromStructureFunc) _ges_command_line_formatter_add_clip,
    .synopsis="<clip uri>",
    .description="Adds a clip in the timeline. "
                 "See documentation for the --track-types option to ges-launch-1.0, as it "
                 " will affect the result of this command.",
    .examples="    ges-launch-1.0 +clip /path/to/media\n\n"
              "This will simply play the sample from its beginning to its end.\n\n"
              "    ges-launch-1.0 +clip /path/to/media inpoint=4.0\n\n"
              "Assuming 'media' is a 10 second long media sample, this will play the sample\n"
              "from the 4th second to the 10th, resulting in a 6-seconds long playback.\n\n"
              "    ges-launch-1.0 +clip /path/to/media inpoint=4.0 duration=2.0 start=4.0\n\n"
              "Assuming \"media\" is an audio video sample longer than 6 seconds, this will play\n"
              "a black frame and silence for 4 seconds, then the sample from its 4th second to\n"
              "its sixth second, resulting in a 6-seconds long playback.\n\n"
              "    ges-launch-1.0 --track-types=audio +clip /path/to/media\n\n"
              "Assuming \"media\" is an audio video sample, this will only play the audio of the\n"
              "sample in its entirety.\n\n"
              "    ges-launch-1.0 +clip /path/to/media1 layer=1 set-alpha 0.9 +clip /path/to/media2 layer=0\n\n"
              "Assume media1 and media2 both contain audio and video and last for 10 seconds.\n\n"
              "This will first add media1 in a new layer of \"priority\" 1, thus implicitly\n"
              "creating a layer of \"priority\" 0, the start of the clip will be 0 as no clip\n"
              "had been added in that layer before.\n\n"
              "It will then add media2 in the layer of \"priority\" 0 which was created\n"
              "previously, the start of this new clip will also be 0 as no clip has been added\n"
              "in this layer before.\n\n"
              "Both clips will thus overlap on two layers for 10 seconds.\n\n"
              "The \"alpha\" property of the second clip will finally be set to a value of 0.9.\n\n"
              "All this will result in a 10 seconds playback, where media2 is barely visible\n"
              "through media1, which is nearly opaque. If alpha was set to 0.5, both clips\n"
              "would be equally visible, and if it was set to 0.0, media1 would be invisible\n"
              "and media2 completely opaque.\n",
    .properties={
      {
        "uri", 0, 0, "asset-id",
        "The URI of the media file."
      },
      {
        "name", "n", 0, NULL,
        "The name of the clip, can be used as an ID later."
      },
      {
        "start", "s", GST_TYPE_CLOCK_TIME, NULL,
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
    .long_name="effect",
    .short_name='e',
    .callback=(ActionFromStructureFunc) _ges_command_line_formatter_add_effect,
    .synopsis="<effect bin description>",
    .description="Adds an effect as specified by 'bin-description', similar to gst-launch-style"
                 " pipeline description, without setting properties (see `set-<property-name>` for information"
                 " about how to set properties).",
    .examples="    ges-launch-1.0 +clip /path/to/media +effect \"agingtv\"\n\n"
              "This will apply the agingtv effect to \"media\" and play it back.",
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
        "inpoint", "i", GST_TYPE_CLOCK_TIME, NULL,
        "Implies that the effect has 'internal content'"
        "(see [ges_track_element_set_has_internal_source](ges_track_element_set_has_internal_source))",
      },
      {
        "name", "n", 0, "child-name",
        "The name to be given to the effect."
      },
      {NULL, NULL, 0, NULL, FALSE},
    },
  },
  {
    .long_name="test-clip",
    .short_name=0,
    .callback=(ActionFromStructureFunc) _ges_command_line_formatter_add_test_clip,
    .synopsis="<test clip pattern>",
    .description="Add a test clip in the timeline.",
    .examples=NULL,
    .properties={
      {
        "vpattern", "p", 0, NULL,
        "The testsource pattern name."
      },
      {
        "name", "n", 0, NULL,
        "The name of the clip, can be used as an ID later."
      },
      {
        "start", "s", GST_TYPE_CLOCK_TIME, NULL,
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
  {
    .long_name="title",
    .short_name='c',
    .callback=(ActionFromStructureFunc) _ges_command_line_formatter_add_title_clip,
    .synopsis="<title text>",
    .description="Adds a clip in the timeline.",
    .examples=NULL,
    .properties={
      {
        "text", "t", 0, NULL,
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
        "layer", "l", G_TYPE_INT, NULL,
        "The priority of the layer into which the clip should be added."
      },
      {NULL, 0, 0, NULL, FALSE},
    },
  },
  {
    .long_name="track",
    .short_name='t',
    .callback=(ActionFromStructureFunc) _ges_command_line_formatter_add_track,
    .synopsis="<track type>",
    .description="Adds a track to the timeline.",
    .examples=NULL,
    .properties={
      {"track-type", 0, 0, NULL, NULL},
      {
        "restrictions", "r", 0, NULL,
        "The restriction caps to set on the track."
      },
      {NULL, 0, 0, NULL, FALSE},
    },
  },
  {
    .long_name="keyframes",
    .short_name='k',
    .callback=(ActionFromStructureFunc) _ges_command_line_formatter_add_keyframes,
    .synopsis="<property name>",
    .description="Adds keyframes for the specified property in the form:\n\n",
    .examples="    ges-launch-1.0 +test-clip blue d=1.0 +keyframes posx 0=0 1.0=1280 t=direct-absolute +k posy 0=0 1.0=720 t=direct-absolute\n\n"
              "This add a testclip that will disappear in the bottom right corner",
    .properties={
      {"property-name", 0, 0, NULL, NULL},
      {
        "binding-type", "t", 0, NULL,
        "The type of binding to use, eg. 'direct-absolute', 'direct'"
      },
      {
        "interpolation-mode", "m", 0, NULL,
        "The GstInterpolationMode to user."
      },
      {
        "...", 0, 0, NULL,
        "The list of keyframe_timestamp=value to be set."
      },
      {NULL, 0, 0, NULL, FALSE},
    },
  },
  {
    .long_name="set-",
    .short_name=0,
    .callback=NULL,
    .synopsis="<property name> <value>",
    .description="Set a property on the last added element."
                 " Any child property that exists on the previously added element"
                 " can be used as <property name>"
                 "By default, set-<property-name> will lookup the property on the last added"
                  "object.",
    .examples="    ges-launch-1.0 +clip /path/to/media set-alpha 0.3\n\n"
              "This will set the alpha property on \"media\" then play it back, assuming \"media\""
              "contains a video stream.\n\n"
              "    ges-launch-1.0 +clip /path/to/media +effect \"agingtv\" set-dusts false\n\n"
              "This will set the \"dusts\" property of the agingtv to false and play the\n"
              "timeline back.",
    .properties={
      {NULL, 0, 0, NULL, FALSE},
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
  TRACK,
  KEYFRAMES,
  SET,
} GESCommandLineOptionType;

static gint                     /*  -1: not present, 0: failure, 1: OK */
_convert_to_clocktime (GstStructure * structure, const gchar * name,
    GstClockTime default_value)
{
  gint res = 1;
  gdouble val;
  GValue d_val = G_VALUE_INIT, converted = G_VALUE_INIT;
  GstClockTime timestamp;
  const GValue *gvalue = gst_structure_get_value (structure, name);

  if (gvalue == NULL) {
    timestamp = default_value;

    res = -1;

    goto done;
  }

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING) {
    const gchar *val_string = g_value_get_string (gvalue);
    /* if starts with an 'f', interpret as a frame number, keep as
     * a string for now */
    if (val_string && val_string[0] == 'f')
      return 1;
    /* else, try convert to a GstClockTime, or a double */
    g_value_init (&converted, GST_TYPE_CLOCK_TIME);
    if (!gst_value_deserialize (&converted, val_string)) {
      g_value_unset (&converted);
      g_value_init (&converted, G_TYPE_DOUBLE);
      if (!gst_value_deserialize (&converted, val_string)) {
        GST_ERROR ("Could not get timestamp for %s by deserializing %s",
            name, val_string);
        goto error;
      }
    }
  } else {
    g_value_init (&converted, G_VALUE_TYPE (gvalue));
    g_value_copy (gvalue, &converted);
  }

  if (G_VALUE_TYPE (&converted) == GST_TYPE_CLOCK_TIME) {
    timestamp = g_value_get_uint64 (&converted);
    goto done;
  }

  g_value_init (&d_val, G_TYPE_DOUBLE);

  if (!g_value_transform (&converted, &d_val)) {
    GST_ERROR ("Could not get timestamp for %s", name);
    goto error;
  }

  val = g_value_get_double ((const GValue *) &d_val);
  g_value_unset (&d_val);

  if (val == -1.0)
    timestamp = GST_CLOCK_TIME_NONE;
  else
    timestamp = val * GST_SECOND;

done:
  gst_structure_set (structure, name, G_TYPE_UINT64, timestamp, NULL);
  g_value_unset (&converted);

  return res;

error:
  g_value_unset (&converted);

  return 0;
}

static gboolean
_cleanup_fields (const Property * field_names, GstStructure * structure,
    GError ** error)
{
  guint i;

  for (i = 0; field_names[i].long_name; i++) {
    gboolean exists = FALSE;

    /* Move shortly named fields to longname variante */
    if (field_names[i].short_name &&
        gst_structure_has_field (structure, field_names[i].short_name)) {
      exists = TRUE;

      if (gst_structure_has_field (structure, field_names[i].long_name)) {
        gchar *str_info = gst_structure_serialize_full (structure, 0);

        *error =
            g_error_new (GES_ERROR, 0,
            "Using short (%s) and long name (%s)"
            " at the same time s in %s, which one should I use?!",
            field_names[i].short_name, field_names[i].long_name, str_info);
        g_free (str_info);

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
  GESProject *proj;
  GESAsset *asset;
  if (!_cleanup_fields (options[CLIP].properties, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESUriClip", NULL);

  if (!_ges_add_clip_from_struct (timeline, structure, error))
    return FALSE;

  proj = GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE (timeline)));
  asset = _ges_get_asset_from_timeline (timeline, GES_TYPE_URI_CLIP,
      gst_structure_get_string (structure, "asset-id"), NULL);
  ges_project_add_asset (proj, asset);

  return TRUE;
}

static gboolean
_ges_command_line_formatter_add_test_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  if (!_cleanup_fields (options[TEST_CLIP].properties, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESTestClip", NULL);

  if (!gst_structure_has_field_typed (structure, "asset-id", G_TYPE_STRING))
    gst_structure_set (structure, "asset-id", G_TYPE_STRING, "GESTestClip",
        NULL);

  return _ges_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_title_clip (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  if (!_cleanup_fields (options[TITLE].properties, structure, error))
    return FALSE;

  gst_structure_set (structure, "type", G_TYPE_STRING, "GESTitleClip", NULL);
  gst_structure_set (structure, "asset-id", G_TYPE_STRING, "GESTitleClip",
      NULL);

  return _ges_add_clip_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_keyframes (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  if (!_cleanup_fields (options[KEYFRAMES].properties, structure, error))
    return FALSE;

  if (!_ges_set_control_source_from_struct (timeline, structure, error))
    return FALSE;

  return _ges_add_remove_keyframe_from_struct (timeline, structure, error);
}

static gboolean
_ges_command_line_formatter_add_track (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  if (!_cleanup_fields (options[TRACK].properties, structure, error))
    return FALSE;

  return _ges_add_track_from_struct (timeline, structure, error);
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

/**
 * ges_command_line_formatter_get_help:
 * @nargs: Number of commands in @commands
 * @commands: (array length=nargs): Commands
 *
 * Creates a help string based on @commands.
 *
 * Result: (transfer full): A help string.
 *
 * Since: 1.10
 */
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
      gint j;

      gchar *tmp = g_strdup_printf ("  `%s%s` - %s\n",
          option.properties[0].long_name ? "+" : "",
          option.long_name, option.synopsis);

      g_string_append (help, tmp);
      g_string_append (help, "  ");
      g_string_append (help, "\n\n  ");
      g_free (tmp);

      for (j = 0; option.description[j] != '\0'; j++) {

        if (j && (j % 80) == 0) {
          while (option.description[j] != '\0' && option.description[j] != ' ')
            g_string_append_c (help, option.description[j++]);
          g_string_append (help, "\n  ");
          continue;
        }

        g_string_append_c (help, option.description[j]);
      }
      g_string_append_c (help, '\n');

      if (option.properties[0].long_name) {
        gint j;

        g_string_append (help, "\n  Properties:\n\n");

        for (j = 1; option.properties[j].long_name; j++) {
          Property prop = option.properties[j];
          g_string_append_printf (help, "    * `%s`: %s\n", prop.long_name,
              prop.desc);
        }
      }
      if (option.examples) {
        gint j;
        gchar **examples = g_strsplit (option.examples, "\n", -1);

        g_string_append (help, "\n  Examples:\n\n");
        for (j = 0; examples[j]; j++) {
          if (examples[j])
            g_string_append_printf (help, "    %s", examples[j]);
          g_string_append_c (help, '\n');
        }
        g_strfreev (examples);
      }

      g_string_append_c (help, '\n');
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

/* @uri: (transfer full): */
static gchar *
get_timeline_desc_from_uri (GstUri * uri)
{
  gchar *res, *path;

  if (!uri)
    return NULL;

  /* Working around parser requiring a space to begin with */
  path = gst_uri_get_path (uri);
  res = g_strconcat (" ", path, NULL);
  g_free (path);

  gst_uri_unref (uri);

  return res;
}

static gboolean
_can_load (GESFormatter * dummy_formatter, const gchar * string,
    GError ** error)
{
  gboolean res = FALSE;
  GstUri *uri;
  const gchar *scheme;
  gchar *timeline_desc = NULL;
  GESStructureParser *parser;

  if (string == NULL) {
    GST_ERROR ("No URI!");
    return FALSE;
  }

  uri = gst_uri_from_string (string);
  if (!uri) {
    GST_INFO_OBJECT (dummy_formatter, "Wrong uri: %s", string);
    return FALSE;
  }

  scheme = gst_uri_get_scheme (uri);
  if (!g_strcmp0 (scheme, "ges:")) {
    GST_INFO_OBJECT (dummy_formatter, "Wrong scheme: %s", string);
    gst_uri_unref (uri);

    return FALSE;
  }

  timeline_desc = get_timeline_desc_from_uri (uri);
  parser = _parse_structures (timeline_desc);
  if (parser->structures)
    res = TRUE;

  gst_object_unref (parser);
  g_free (timeline_desc);

  return res;
}

static gboolean
_set_project_loaded (GESFormatter * self)
{
  ges_project_set_loaded (self->project, self, NULL);
  gst_object_unref (self);

  return FALSE;
}

static gboolean
_load (GESFormatter * self, GESTimeline * timeline, const gchar * string,
    GError ** error)
{
  guint i;
  GList *tmp;
  GError *err;
  gchar *timeline_desc =
      get_timeline_desc_from_uri (gst_uri_from_string (string));
  GESStructureParser *parser = _parse_structures (timeline_desc);

  g_free (timeline_desc);

  err = ges_structure_parser_get_error (parser);

  if (err) {
    if (error)
      *error = err;

    return FALSE;
  }

  g_object_set (timeline, "auto-transition", TRUE, NULL);

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
        break;
      }
    }
  }

  gst_object_unref (parser);

  ges_idle_add ((GSourceFunc) _set_project_loaded, g_object_ref (self), NULL);

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

/* Copy of GST_ASCII_IS_STRING */
#define ASCII_IS_STRING(c) (g_ascii_isalnum((c)) || ((c) == '_') || \
    ((c) == '-') || ((c) == '+') || ((c) == '/') || ((c) == ':') || \
    ((c) == '.'))

static void
_sanitize_argument (const gchar * arg, GString * res)
{
  gboolean need_wrap = FALSE;
  const gchar *tmp_string;

  for (tmp_string = arg; *tmp_string != '\0'; tmp_string++) {
    if (!ASCII_IS_STRING (*tmp_string) || (*tmp_string == '\n')) {
      need_wrap = TRUE;
      break;
    }
  }

  if (!need_wrap) {
    g_string_append (res, arg);
    return;
  }

  g_string_append_c (res, '"');
  while (*arg != '\0') {
    if (*arg == '"' || *arg == '\\') {
      g_string_append_c (res, '\\');
    } else if (*arg == '\n') {
      g_string_append (res, "\\n");
      arg++;
      continue;
    }

    g_string_append_c (res, *(arg++));
  }
  g_string_append_c (res, '"');
}

static gboolean
_serialize_control_binding (GESTrackElement * e, const gchar * prop,
    GString * res)
{
  GstInterpolationMode mode;
  GstControlSource *source = NULL;
  GList *timed_values, *tmp;
  gboolean absolute = FALSE;
  GstControlBinding *binding = ges_track_element_get_control_binding (e, prop);

  if (!binding)
    return FALSE;

  if (!GST_IS_DIRECT_CONTROL_BINDING (binding)) {
    g_warning ("Unsupported control binding type: %s",
        G_OBJECT_TYPE_NAME (binding));
    goto done;
  }

  g_object_get (binding, "control-source", &source,
      "absolute", &absolute, NULL);

  if (!GST_IS_INTERPOLATION_CONTROL_SOURCE (source)) {
    g_warning ("Unsupported control source type: %s",
        G_OBJECT_TYPE_NAME (source));
    goto done;
  }

  g_object_get (source, "mode", &mode, NULL);
  g_string_append_printf (res, " +keyframes %s t=%s",
      prop, absolute ? "direct-absolute" : "direct");

  if (mode != GST_INTERPOLATION_MODE_LINEAR)
    g_string_append_printf (res, " mode=%s",
        g_enum_get_value (g_type_class_peek (GST_TYPE_INTERPOLATION_MODE),
            mode)->value_nick);

  timed_values =
      gst_timed_value_control_source_get_all
      (GST_TIMED_VALUE_CONTROL_SOURCE (source));
  for (tmp = timed_values; tmp; tmp = tmp->next) {
    gchar strbuf[G_ASCII_DTOSTR_BUF_SIZE];
    GstTimedValue *value;

    value = (GstTimedValue *) tmp->data;
    g_string_append_printf (res, " %f=%s",
        (gdouble) value->timestamp / (gdouble) GST_SECOND,
        g_ascii_dtostr (strbuf, G_ASCII_DTOSTR_BUF_SIZE, value->value));
  }
  g_list_free (timed_values);

done:
  g_clear_object (&source);
  return TRUE;
}

static void
_serialize_object_properties (GObject * object, GESCommandLineOption * option,
    gboolean children_props, GString * res)
{
  guint n_props, j;
  GParamSpec *spec, **pspecs;
  GObjectClass *class = G_OBJECT_GET_CLASS (object);
  const gchar *ignored_props[] = {
    "max-duration", "supported-formats", "priority", "video-direction",
    "is-image", NULL,
  };

  if (!children_props)
    pspecs = g_object_class_list_properties (class, &n_props);
  else {
    pspecs =
        ges_timeline_element_list_children_properties (GES_TIMELINE_ELEMENT
        (object), &n_props);
    g_assert (GES_IS_TRACK_ELEMENT (object));
  }

  for (j = 0; j < n_props; j++) {
    const gchar *name;
    gchar *value_str = NULL;
    GValue val = { 0 };
    gint i;

    spec = pspecs[j];
    if (!ges_util_can_serialize_spec (spec))
      continue;

    g_value_init (&val, spec->value_type);
    if (!children_props)
      g_object_get_property (object, spec->name, &val);
    else
      ges_timeline_element_get_child_property_by_pspec (GES_TIMELINE_ELEMENT
          (object), spec, &val);

    if (gst_value_compare (g_param_spec_get_default_value (spec),
            &val) == GST_VALUE_EQUAL) {
      GST_INFO ("Ignoring %s as it is using the default value", spec->name);
      goto next;
    }

    name = spec->name;
    if (!children_props && !g_strcmp0 (name, "in-point"))
      name = "inpoint";

    for (i = 0; option->properties[i].long_name; i++) {
      if (!g_strcmp0 (spec->name, option->properties[i].long_name)) {
        if (children_props) {
          name = NULL;
        } else {
          name = option->properties[i].short_name;
          if (option->properties[i].type == GST_TYPE_CLOCK_TIME)
            value_str =
                g_strdup_printf ("%f",
                (gdouble) (g_value_get_uint64 (&val) / GST_SECOND));
        }
        break;
      } else if (!g_strcmp0 (spec->name, option->properties[0].long_name)) {
        name = NULL;
        break;
      }
    }

    for (i = 0; i < G_N_ELEMENTS (ignored_props); i++) {
      if (!g_strcmp0 (spec->name, ignored_props[i])) {
        name = NULL;
        break;
      }
    }

    if (!name) {
      g_free (value_str);
      continue;
    }

    if (GES_IS_TRACK_ELEMENT (object) &&
        _serialize_control_binding (GES_TRACK_ELEMENT (object), name, res)) {
      g_free (value_str);
      continue;
    }

    if (!value_str)
      value_str = gst_value_serialize (&val);

    g_string_append_printf (res, " %s%s%s",
        children_props ? "set-" : "", name, children_props ? " " : "=");
    _sanitize_argument (value_str, res);
    g_free (value_str);

  next:
    g_value_unset (&val);
  }
  g_free (pspecs);
}

static void
_serialize_clip_track_types (GESClip * clip, GESTrackType tt, GString * res)
{
  GValue v = G_VALUE_INIT;
  gchar *ttype_str;

  if (ges_clip_get_supported_formats (clip) == tt)
    return;

  g_value_init (&v, GES_TYPE_TRACK_TYPE);
  g_value_set_flags (&v, ges_clip_get_supported_formats (clip));

  ttype_str = gst_value_serialize (&v);

  g_string_append_printf (res, " tt=%s", ttype_str);
  g_value_reset (&v);
  g_free (ttype_str);
}

static void
_serialize_clip_effects (GESClip * clip, GString * res)
{
  GList *tmpeffect, *effects;

  effects = ges_clip_get_top_effects (clip);
  for (tmpeffect = effects; tmpeffect; tmpeffect = tmpeffect->next) {
    gchar *bin_desc;

    g_object_get (tmpeffect->data, "bin-description", &bin_desc, NULL);

    g_string_append_printf (res, " +effect %s", bin_desc);
    g_free (bin_desc);
  }
  g_list_free_full (effects, gst_object_unref);

}

/**
 * ges_command_line_formatter_get_timeline_uri:
 * @timeline: A GESTimeline to serialize
 *
 * Since: 1.10
 */
gchar *
ges_command_line_formatter_get_timeline_uri (GESTimeline * timeline)
{
  gchar *tmpstr;
  GList *tmp;
  gint i;
  GString *res = g_string_new ("ges:");
  GESTrackType tt = 0;

  if (!timeline)
    goto done;

  for (tmp = timeline->tracks; tmp; tmp = tmp->next) {
    GstCaps *caps, *default_caps;
    GESTrack *tmptrack, *track = tmp->data;

    if (GES_IS_VIDEO_TRACK (track))
      tmptrack = GES_TRACK (ges_video_track_new ());
    else if (GES_IS_AUDIO_TRACK (track))
      tmptrack = GES_TRACK (ges_audio_track_new ());
    else {
      g_warning ("Unhandled track type: %s", G_OBJECT_TYPE_NAME (track));
      continue;
    }

    tt |= track->type;

    g_string_append_printf (res, " +track %s",
        (track->type == GES_TRACK_TYPE_VIDEO) ? "video" : "audio");

    default_caps = ges_track_get_restriction_caps (tmptrack);
    caps = ges_track_get_restriction_caps (track);
    if (!gst_caps_is_equal (caps, default_caps)) {
      tmpstr = gst_caps_serialize (caps, 0);

      g_string_append (res, " restrictions=");
      _sanitize_argument (tmpstr, res);
      g_free (tmpstr);
    }
    gst_caps_unref (default_caps);
    gst_caps_unref (caps);
    gst_object_unref (tmptrack);
  }

  for (tmp = timeline->layers, i = 0; tmp; tmp = tmp->next, i++) {
    GList *tmpclip, *clips = ges_layer_get_clips (tmp->data);
    GList *tmptrackelem;

    for (tmpclip = clips; tmpclip; tmpclip = tmpclip->next) {
      GESClip *clip = tmpclip->data;
      GESCommandLineOption *option = NULL;

      if (GES_IS_TEST_CLIP (clip)) {
        GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (clip));
        const gchar *id = ges_asset_get_id (asset);

        g_string_append (res, " +test-clip ");

        _sanitize_argument (g_enum_get_value (g_type_class_peek
                (GES_VIDEO_TEST_PATTERN_TYPE),
                ges_test_clip_get_vpattern (GES_TEST_CLIP (clip)))->value_nick,
            res);

        if (g_strcmp0 (id, "GESTestClip")) {
          g_string_append (res, " asset-id=");
          _sanitize_argument (id, res);
        }

        option = &options[TEST_CLIP];
      } else if (GES_IS_TITLE_CLIP (clip)) {
        g_string_append (res, " +title ");
        _sanitize_argument (ges_title_clip_get_text (GES_TITLE_CLIP (clip)),
            res);
        option = &options[TITLE];
      } else if (GES_IS_URI_CLIP (clip)) {
        g_string_append (res, " +clip ");

        _sanitize_argument (ges_uri_clip_get_uri (GES_URI_CLIP (clip)), res);
        option = &options[CLIP];
      } else {
        g_warning ("Unhandled clip type: %s", G_OBJECT_TYPE_NAME (clip));
        continue;
      }

      _serialize_clip_track_types (clip, tt, res);

      if (i)
        g_string_append_printf (res, " layer=%d", i);

      _serialize_object_properties (G_OBJECT (clip), option, FALSE, res);
      _serialize_clip_effects (clip, res);

      for (tmptrackelem = GES_CONTAINER_CHILDREN (clip); tmptrackelem;
          tmptrackelem = tmptrackelem->next)
        _serialize_object_properties (G_OBJECT (tmptrackelem->data), option,
            TRUE, res);
    }
    g_list_free_full (clips, gst_object_unref);
  }

done:
  return g_string_free (res, FALSE);
  {
    GstUri *uri = gst_uri_from_string (res->str);
    gchar *uri_str = gst_uri_to_string (uri);

    g_string_free (res, TRUE);

    return uri_str;
  }
}
