/* GStreamer
 *
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-utils.c - Some utility functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include<math.h>
#include<ctype.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include "gst-validate-utils.h"
#include <gst/gst.h>

#define PARSER_BOOLEAN_EQUALITY_THRESHOLD (1e-10)
#define PARSER_MAX_TOKEN_SIZE 256
#define PARSER_MAX_ARGUMENT_COUNT 10

static GRegex *_clean_structs_lines = NULL;

typedef struct
{
  const gchar *str;
  gint len;
  gint pos;
  jmp_buf err_jmp_buf;
  const gchar *error;
  void *user_data;
  ParseVariableFunc variable_func;
} MathParser;

static gdouble _read_power (MathParser * parser);

static void
_error (MathParser * parser, const gchar * err)
{
  parser->error = err;
  longjmp (parser->err_jmp_buf, 1);
}

static gchar
_peek (MathParser * parser)
{
  if (parser->pos < parser->len)
    return parser->str[parser->pos];
  _error (parser, "Tried to read past end of string!");
  return '\0';
}

static gchar
_peek_n (MathParser * parser, gint n)
{
  if (parser->pos + n < parser->len)
    return parser->str[parser->pos + n];
  _error (parser, "Tried to read past end of string!");
  return '\0';
}

static gchar
_next (MathParser * parser)
{
  if (parser->pos < parser->len)
    return parser->str[parser->pos++];
  _error (parser, "Tried to read past end of string!");
  return '\0';
}

static gdouble
_read_double (MathParser * parser)
{
  gchar c, token[PARSER_MAX_TOKEN_SIZE];
  gint pos = 0;
  gdouble val = 0.0;

  c = _peek (parser);
  if (c == '+' || c == '-')
    token[pos++] = _next (parser);

  while (isdigit (_peek (parser)))
    token[pos++] = _next (parser);

  c = _peek (parser);
  if (c == '.')
    token[pos++] = _next (parser);

  while (isdigit (_peek (parser)))
    token[pos++] = _next (parser);

  c = _peek (parser);
  if (c == 'e' || c == 'E') {
    token[pos++] = _next (parser);

    c = _peek (parser);
    if (c == '+' || c == '-') {
      token[pos++] = _next (parser);
    }
  }

  while (isdigit (_peek (parser)))
    token[pos++] = _next (parser);

  token[pos] = '\0';

  if (pos == 0 || sscanf (token, "%lf", &val) != 1)
    _error (parser, "Failed to read real number");

  return val;
}

static gdouble
_read_term (MathParser * parser)
{
  gdouble v0;
  gchar c;

  v0 = _read_power (parser);
  c = _peek (parser);

  while (c == '*' || c == '/') {
    _next (parser);
    if (c == '*') {
      v0 *= _read_power (parser);
    } else if (c == '/') {
      v0 /= _read_power (parser);
    }
    c = _peek (parser);
  }
  return v0;
}

static gdouble
_read_expr (MathParser * parser)
{
  gdouble v0 = 0.0;
  gchar c;

  c = _peek (parser);
  if (c == '+' || c == '-') {
    _next (parser);
    if (c == '+')
      v0 += _read_term (parser);
    else if (c == '-')
      v0 -= _read_term (parser);
  } else {
    v0 = _read_term (parser);
  }

  c = _peek (parser);
  while (c == '+' || c == '-') {
    _next (parser);
    if (c == '+') {
      v0 += _read_term (parser);
    } else if (c == '-') {
      v0 -= _read_term (parser);
    }

    c = _peek (parser);
  }

  return v0;
}

static gdouble
_read_boolean_comparison (MathParser * parser)
{
  gchar c, oper[] = { '\0', '\0', '\0' };
  gdouble v0, v1;


  v0 = _read_expr (parser);
  c = _peek (parser);
  if (c == '>' || c == '<') {
    oper[0] = _next (parser);
    c = _peek (parser);
    if (c == '=')
      oper[1] = _next (parser);


    v1 = _read_expr (parser);

    if (g_strcmp0 (oper, "<") == 0) {
      v0 = (v0 < v1) ? 1.0 : 0.0;
    } else if (g_strcmp0 (oper, ">") == 0) {
      v0 = (v0 > v1) ? 1.0 : 0.0;
    } else if (g_strcmp0 (oper, "<=") == 0) {
      v0 = (v0 <= v1) ? 1.0 : 0.0;
    } else if (g_strcmp0 (oper, ">=") == 0) {
      v0 = (v0 >= v1) ? 1.0 : 0.0;
    } else {
      _error (parser, "Unknown operation!");
    }
  }
  return v0;
}

static gdouble
_read_boolean_equality (MathParser * parser)
{
  gchar c, oper[] = { '\0', '\0', '\0' };
  gdouble v0, v1;

  v0 = _read_boolean_comparison (parser);
  c = _peek (parser);
  if (c == '=' || c == '!') {
    if (c == '!') {
      if (_peek_n (parser, 1) == '=') {
        oper[0] = _next (parser);
        oper[1] = _next (parser);
      } else {
        return v0;
      }
    } else {
      oper[0] = _next (parser);
      c = _peek (parser);
      if (c != '=')
        _error (parser, "Expected a '=' for boolean '==' operator!");
      oper[1] = _next (parser);
    }
    v1 = _read_boolean_comparison (parser);
    if (g_strcmp0 (oper, "==") == 0) {
      v0 = (fabs (v0 - v1) < PARSER_BOOLEAN_EQUALITY_THRESHOLD) ? 1.0 : 0.0;
    } else if (g_strcmp0 (oper, "!=") == 0) {
      v0 = (fabs (v0 - v1) > PARSER_BOOLEAN_EQUALITY_THRESHOLD) ? 1.0 : 0.0;
    } else {
      _error (parser, "Unknown operation!");
    }
  }
  return v0;
}

static gdouble
_read_boolean_and (MathParser * parser)
{
  gchar c;
  gdouble v0, v1;

  v0 = _read_boolean_equality (parser);

  c = _peek (parser);
  while (c == '&') {
    _next (parser);

    c = _peek (parser);
    if (c != '&')
      _error (parser, "Expected '&' to follow '&' in logical and operation!");
    _next (parser);

    v1 = _read_boolean_equality (parser);
    v0 = (fabs (v0) >= PARSER_BOOLEAN_EQUALITY_THRESHOLD
        && fabs (v1) >= PARSER_BOOLEAN_EQUALITY_THRESHOLD) ? 1.0 : 0.0;

    c = _peek (parser);
  }

  return v0;
}

static gdouble
_read_boolean_or (MathParser * parser)
{
  gchar c;
  gdouble v0, v1;

  v0 = _read_boolean_and (parser);

  c = _peek (parser);
  while (c == '|') {
    _next (parser);
    c = _peek (parser);
    if (c != '|')
      _error (parser, "Expected '|' to follow '|' in logical or operation!");
    _next (parser);
    v1 = _read_boolean_and (parser);
    v0 = (fabs (v0) >= PARSER_BOOLEAN_EQUALITY_THRESHOLD
        || fabs (v1) >= PARSER_BOOLEAN_EQUALITY_THRESHOLD) ? 1.0 : 0.0;
    c = _peek (parser);
  }

  return v0;
}

static gboolean
_init (MathParser * parser, const gchar * str,
    ParseVariableFunc variable_func, void *user_data)
{
  parser->str = str;
  parser->len = strlen (str) + 1;
  parser->pos = 0;
  parser->error = NULL;
  parser->user_data = user_data;
  parser->variable_func = variable_func;

  return TRUE;
}

static gdouble
_parse (MathParser * parser)
{
  gdouble result = 0.0;

  if (!setjmp (parser->err_jmp_buf)) {
    result = _read_expr (parser);
    if (parser->pos < parser->len - 1) {
      _error (parser,
          "Failed to reach end of input expression, likely malformed input");
    } else
      return result;
  } else {
    return sqrt (-1.0);
  }
  return sqrt (-1.0);
}

static gdouble
_read_argument (MathParser * parser)
{
  gchar c;
  gdouble val;

  val = _read_expr (parser);
  c = _peek (parser);
  if (c == ',')
    _next (parser);

  return val;
}

static gdouble
_read_builtin (MathParser * parser)
{
  gdouble v0 = 0.0, v1 = 0.0;
  gchar c, token[PARSER_MAX_TOKEN_SIZE];
  gint pos = 0;

  c = _peek (parser);
  if (isalpha (c) || c == '_') {
    while (isalpha (c) || isdigit (c) || c == '_') {
      token[pos++] = _next (parser);
      c = _peek (parser);
    }
    token[pos] = '\0';

    if (_peek (parser) == '(') {
      _next (parser);
      if (g_strcmp0 (token, "min") == 0) {
        v0 = _read_argument (parser);
        v1 = _read_argument (parser);
        v0 = MIN (v0, v1);
      } else if (g_strcmp0 (token, "max") == 0) {
        v0 = _read_argument (parser);
        v1 = _read_argument (parser);
        v0 = MAX (v0, v1);
      } else {
        _error (parser, "Tried to call unknown built-in function!");
      }

      if (_next (parser) != ')')
        _error (parser, "Expected ')' in built-in call!");
    } else {
      if (parser->variable_func != NULL
          && parser->variable_func (token, &v1, parser->user_data)) {
        v0 = v1;
      } else {
        _error (parser, "Could not look up value for variable %s!");
      }
    }
  } else {
    v0 = _read_double (parser);
  }

  return v0;
}

static gdouble
_read_parenthesis (MathParser * parser)
{
  gdouble val;

  if (_peek (parser) == '(') {
    _next (parser);
    val = _read_boolean_or (parser);
    if (_peek (parser) != ')')
      _error (parser, "Expected ')'!");
    _next (parser);
  } else {
    val = _read_builtin (parser);
  }

  return val;
}

static gdouble
_read_unary (MathParser * parser)
{
  gchar c;
  gdouble v0 = 0.0;

  c = _peek (parser);
  if (c == '!') {
    _error (parser, "Expected '+' or '-' for unary expression, got '!'");
  } else if (c == '-') {
    _next (parser);
    v0 = -_read_parenthesis (parser);
  } else if (c == '+') {
    _next (parser);
    v0 = _read_parenthesis (parser);
  } else {
    v0 = _read_parenthesis (parser);
  }
  return v0;
}

static gdouble
_read_power (MathParser * parser)
{
  gdouble v0, v1 = 1.0, s = 1.0;

  v0 = _read_unary (parser);

  while (_peek (parser) == '^') {
    _next (parser);
    if (_peek (parser) == '-') {
      _next (parser);
      s = -1.0;
    }
    v1 = s * _read_power (parser);
    v0 = pow (v0, v1);
  }

  return v0;
}

gdouble
gst_validate_utils_parse_expression (const gchar * expr,
    ParseVariableFunc variable_func, gpointer user_data, gchar ** error)
{
  gdouble val;
  MathParser parser;
  gchar **spl = g_strsplit (expr, " ", -1);
  gchar *expr_nospace = g_strjoinv ("", spl);

  _init (&parser, expr_nospace, variable_func, user_data);
  val = _parse (&parser);
  g_strfreev (spl);
  g_free (expr_nospace);

  if (error) {
    if (parser.error)
      *error = g_strdup (parser.error);
    else
      *error = NULL;
  }
  return val;
}

/**
 * gst_validate_utils_flags_from_str:
 * @type: The #GType of the flags we are trying to retrieve the flags from
 * @str_flags: The string representation of the value
 *
 * Returns: The flags set in @str_flags
 */
guint
gst_validate_utils_flags_from_str (GType type, const gchar * str_flags)
{
  guint i;
  gint flags = 0;
  GFlagsClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (class->values[i].value_nick == NULL)
      continue;

    if (g_strrstr (str_flags, class->values[i].value_nick)) {
      flags |= class->values[i].value;
    }
  }
  g_type_class_unref (class);

  return flags;
}

/**
 * gst_validate_utils_enum_from_str:
 * @type: The #GType of the enum we are trying to retrieve the enum value from
 * @str_enum: The string representation of the value
 * @enum_value: (out): The value of the enum
 *
 * Returns: %TRUE on success %FALSE otherwise
 */
gboolean
gst_validate_utils_enum_from_str (GType type, const gchar * str_enum,
    guint * enum_value)
{
  guint i;
  GEnumClass *class = g_type_class_ref (type);
  gboolean ret = FALSE;

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_enum, class->values[i].value_nick)) {
      *enum_value = class->values[i].value;
      ret = TRUE;
    }
  }

  g_type_class_unref (class);

  return ret;
}

/* Parse file that contains a list of GStructures */
static gchar **
_file_get_lines (GFile * file)
{
  gsize size;

  GError *err = NULL;
  gchar *content = NULL, *escaped_content = NULL, **lines = NULL;

  /* TODO Handle GCancellable */
  if (!g_file_load_contents (file, NULL, &content, &size, NULL, &err))
    goto failed;

  if (g_strcmp0 (content, "") == 0)
    goto failed;

  if (_clean_structs_lines == NULL)
    _clean_structs_lines =
        g_regex_new ("\\\\\n|#.*\n", G_REGEX_CASELESS, 0, NULL);

  escaped_content =
      g_regex_replace (_clean_structs_lines, content, -1, 0, "", 0, NULL);
  g_free (content);

  lines = g_strsplit (escaped_content, "\n", 0);
  g_free (escaped_content);

done:

  return lines;

failed:
  if (err) {
    GST_WARNING ("Failed to load contents: %d %s", err->code, err->message);
    g_error_free (err);
  }

  if (content)
    g_free (content);
  content = NULL;

  if (escaped_content)
    g_free (escaped_content);
  escaped_content = NULL;

  if (lines)
    g_strfreev (lines);
  lines = NULL;

  goto done;
}

static gchar **
_get_lines (const gchar * scenario_file)
{
  GFile *file = NULL;
  gchar **lines = NULL;

  GST_DEBUG ("Trying to load %s", scenario_file);
  if ((file = g_file_new_for_path (scenario_file)) == NULL) {
    GST_WARNING ("%s wrong uri", scenario_file);
    return NULL;
  }

  lines = _file_get_lines (file);

  g_object_unref (file);

  return lines;
}

/* Returns: (transfer full): a #GList of #GstStructure */
static GList *
_lines_get_strutures (gchar ** lines)
{
  gint i;
  GList *structures = NULL;

  for (i = 0; lines[i]; i++) {
    GstStructure *structure;

    if (g_strcmp0 (lines[i], "") == 0)
      continue;

    structure = gst_structure_from_string (lines[i], NULL);
    if (structure == NULL) {
      GST_ERROR ("Could not parse action %s", lines[i]);
      goto failed;
    }

    structures = g_list_append (structures, structure);
  }

done:
  if (lines)
    g_strfreev (lines);

  return structures;

failed:
  if (structures)
    g_list_free_full (structures, (GDestroyNotify) gst_structure_free);
  structures = NULL;

  goto done;
}

GList *
structs_parse_from_filename (const gchar * scenario_file)
{
  gchar **lines;

  lines = _get_lines (scenario_file);

  if (lines == NULL) {
    GST_DEBUG ("Got no line for file: %s", scenario_file);
    return NULL;
  }

  return _lines_get_strutures (lines);
}

GList *
structs_parse_from_gfile (GFile * scenario_file)
{
  gchar **lines;

  lines = _file_get_lines (scenario_file);

  if (lines == NULL)
    return NULL;

  return _lines_get_strutures (lines);
}
