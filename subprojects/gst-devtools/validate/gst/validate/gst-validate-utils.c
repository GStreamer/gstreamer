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

#include "config.h"

#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <sys/wait.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "gst-validate-utils.h"
#include "gst-validate-internal.h"
#include "validate.h"
#include <gst/gst.h>

#define PARSER_BOOLEAN_EQUALITY_THRESHOLD (1e-10)
#define PARSER_MAX_TOKEN_SIZE 256
#define PARSER_MAX_ARGUMENT_COUNT 10

static GRegex *_variables_regex = NULL;
static GstStructure *global_vars = NULL;

static GQuark debug_quark = 0;
static GQuark lineno_quark = 0;
static GQuark filename_quark = 0;

typedef struct
{
  const gchar *str;
  gint len;
  gint pos;
  jmp_buf err_jmp_buf;
  const gchar *error;
  void *user_data;
  GstValidateParseVariableFunc variable_func;
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
    GstValidateParseVariableFunc variable_func, void *user_data)
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
  }

  return -1.0;
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
  if (isalpha (c) || c == '_' || c == '$') {
    while (isalpha (c) || isdigit (c) || c == '_' || c == '$') {
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
        gchar *tmp =
            g_strdup_printf ("Tried to call unknown built-in function: %s",
            token);
        _error (parser, tmp);
        g_free (tmp);
      }

      if (_next (parser) != ')')
        _error (parser, "Expected ')' in built-in call!");
    } else {
      if (parser->variable_func != NULL
          && parser->variable_func (token, &v1, parser->user_data)) {
        v0 = v1;
      } else {
        gchar *err =
            g_strdup_printf ("Could not look up value for variable %s!", token);
        _error (parser, err);
        g_free (err);
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

/**
 * gst_validate_utils_parse_expression: (skip):
 */
gdouble
gst_validate_utils_parse_expression (const gchar * expr,
    GstValidateParseVariableFunc variable_func, gpointer user_data,
    gchar ** error)
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
  guint flags;
  GValue value = G_VALUE_INIT;
  g_value_init (&value, type);

  if (!gst_value_deserialize (&value, str_flags)) {
    gst_validate_abort ("Invalid flags: %s", str_flags);

    return 0;
  }

  flags = g_value_get_flags (&value);
  g_value_unset (&value);

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
  GValue value = G_VALUE_INIT;
  g_value_init (&value, type);

  if (!gst_value_deserialize (&value, str_enum)) {
    gst_validate_abort ("Invalid enum: %s", str_enum);

    return FALSE;
  }

  *enum_value = g_value_get_enum (&value);
  g_value_unset (&value);

  return TRUE;
}


static gchar *
skip_spaces (gchar * c)
{
  /* For some reason newlines are considered as space, we do not want that! */
  while (g_ascii_isspace (*c) && *c != '\n')
    c++;

  return c;
}

static void
setup_quarks (void)
{
  if (filename_quark)
    return;

  filename_quark = g_quark_from_static_string ("__filename__");
  lineno_quark = g_quark_from_static_string ("__lineno__");
  debug_quark = g_quark_from_static_string ("__debug__");
}

gboolean
gst_validate_has_colored_output (void)
{
  return g_log_writer_supports_color (fileno (stdout));
}

/* Parse file that contains a list of GStructures */
#define GST_STRUCT_LINE_CONTINUATION_CHARS ",{\\[<"
static GList *
_file_get_structures (GFile * file, gchar ** err,
    GstValidateGetIncludePathsFunc get_include_paths_func)
{
  gsize size;

  GError *error = NULL;
  gchar *content = NULL, *tmp;
  gchar *filename = NULL;
  gint lineno = 1, current_lineno;
  GList *structures = NULL;
  GString *errstr = NULL;
  gchar *red = NULL, *bold = NULL;
  const gchar *endcolor = "";

  if (err)
    errstr = g_string_new (NULL);

  if (gst_validate_has_colored_output ()) {
    red = gst_debug_construct_term_color (GST_DEBUG_FG_RED);
    bold = gst_debug_construct_term_color (GST_DEBUG_BOLD);
    endcolor = "\033[0m";
  } else {
    red = g_strdup ("");
    bold = g_strdup ("");
  }


  filename = g_file_get_path (file);
  /* TODO Handle GCancellable */
  if (!g_file_load_contents (file, NULL, &content, &size, NULL, &error)) {
    if (errstr && !get_include_paths_func)
      g_string_append_printf (errstr,
          "\n%s%s:%s %sFailed to load content%s\n      | %s",
          bold, filename, endcolor, red, endcolor, error->message);
    else
      GST_WARNING ("Failed to load contents: %d %s", error->code,
          error->message);
    g_error_free (error);
    goto failed;
  }
  if (g_strcmp0 (content, "") == 0) {
    goto done;
  }

  tmp = content;
  while (*tmp) {
    GString *l, *debug_line;
    GstStructure *structure;

    tmp = skip_spaces (tmp);

    if (*tmp == '\n') {
      tmp++;
      lineno++;
      continue;
    }

    if (*tmp == '#') {
      while (*tmp && *tmp != '\n')
        tmp++;
      if (*tmp)
        tmp++;
      lineno++;
      continue;
    }

    l = g_string_new (NULL);
    debug_line = g_string_new (NULL);
    current_lineno = lineno;
    g_string_append_printf (debug_line, "  %4d | ", lineno);
    while (*tmp != '\n' && *tmp) {
      gchar next;

      if (*tmp == '#') {
        while (*tmp && *tmp != '\n') {
          g_string_append_c (debug_line, *tmp);
          tmp++;
        }
        tmp++;
        g_string_append_printf (debug_line, "\n  %4d | ", lineno + 1);
        lineno++;
        continue;
      }

      next = *(tmp + 1);
      if (next && (next == '\n' || next == '\r')
          && strchr (GST_STRUCT_LINE_CONTINUATION_CHARS, *tmp)) {
        g_string_append_c (debug_line, *tmp);
        g_string_append_printf (debug_line, "\n  %4d | ", lineno + 1);
        if (*tmp != '\\')
          g_string_append_c (l, *tmp);

        tmp++;
        while (*tmp == '\n' || *tmp == '\r')
          tmp++;
        lineno++;
        continue;
      }

      g_string_append_c (debug_line, *tmp);
      g_string_append_c (l, *tmp);
      tmp += 1;
    }

    /* Blank lines at EOF */
    if (!*l->str) {
      g_string_free (l, TRUE);
      g_string_free (debug_line, TRUE);
      continue;
    }

    structure = gst_structure_from_string (l->str, NULL);
    if (structure == NULL) {
      if (errstr) {
        g_string_append_printf (errstr,
            "\n%s%s:%d-%d:%s %sInvalid structure%s\n%s",
            bold, filename, current_lineno, lineno, endcolor,
            red, endcolor, debug_line->str);

        if (strchr (debug_line->str, '\n'))
          g_string_append_printf (errstr, "\n       > %s\n", l->str);

        g_string_append_c (errstr, '\n');
      } else {
        g_string_free (l, TRUE);
        g_string_free (debug_line, TRUE);
        goto failed;
      }
    } else {
      if (gst_structure_has_name (structure, "include")) {
        gchar *included_err = NULL;
        const gchar *location =
            gst_structure_get_string (structure, "location");

        if (location == NULL) {
          if (errstr) {
            g_string_append_printf (errstr,
                "\n%s%s:%d-%d:%s %sMissing field 'location' in `include` structure%s\n%s",
                bold, filename, current_lineno, lineno, endcolor,
                red, endcolor, debug_line->str);

            if (strchr (debug_line->str, '\n'))
              g_string_append_printf (errstr, "\n       > %s\n", l->str);

            g_string_append_c (errstr, '\n');
          } else {
            g_string_free (l, TRUE);
            g_string_free (debug_line, TRUE);
            goto failed;
          }
        } else {
          gchar *included_path = NULL;
          GFile *included = NULL;
          GList *tmpstructures;
          gchar **include_dirs = NULL;

          if (!get_include_paths_func
              && g_str_has_suffix (location, GST_VALIDATE_SCENARIO_SUFFIX)) {
            GST_INFO
                ("Trying to include a scenario, take into account scenario include dir");

            get_include_paths_func = (GstValidateGetIncludePathsFunc)
                gst_validate_scenario_get_include_paths;
          }

          if (get_include_paths_func)
            include_dirs = get_include_paths_func (filename);

          if (!include_dirs) {
            GFile *dir = g_file_get_parent (file);
            included = g_file_resolve_relative_path (dir, location);

            g_object_unref (dir);
          } else {
            gint i;

            for (i = 0; include_dirs[i]; i++) {
              g_clear_object (&included);
              included =
                  g_file_new_build_filename (include_dirs[i], location, NULL);
              if (g_file_query_exists (included, NULL))
                break;

              /* We let the last attempt fail and report an error in the
               * including code path */
            }
            g_strfreev (include_dirs);
          }

          included_path = g_file_get_path (included);
          GST_INFO ("%s including %s", filename, included_path);
          g_free (included_path);

          tmpstructures = _file_get_structures (included, &included_err,
              get_include_paths_func);
          if (included_err) {
            if (errstr) {
              gchar *c;

              g_string_append_printf (errstr,
                  "\n%s%s:%d-%d:%s %sError including %s%s\n%s",
                  bold, filename, current_lineno, lineno, endcolor,
                  red, location, endcolor, debug_line->str);

              if (strchr (debug_line->str, '\n'))
                g_string_append_printf (errstr, "\n       > %s\n", l->str);

              for (c = included_err; *c != '\0' && *(c + 1) != '\0'; c++) {
                g_string_append_c (errstr, *c);
                if (*c == '\n')
                  g_string_append (errstr, "       | ");
              }
              g_free (included_err);
            } else {
              g_free (included_err);
              g_string_free (l, TRUE);
              g_string_free (debug_line, TRUE);
              g_object_unref (included);
              goto failed;
            }
          }
          g_object_unref (included);
          structures = g_list_concat (structures, tmpstructures);
        }
        gst_structure_free (structure);
      } else {
        setup_quarks ();
        gst_structure_id_set (structure,
            lineno_quark, G_TYPE_INT, current_lineno,
            filename_quark, G_TYPE_STRING, filename,
            debug_quark, G_TYPE_STRING, debug_line->str, NULL);
        structures = g_list_append (structures, structure);
      }
    }

    g_string_free (l, TRUE);
    g_string_free (debug_line, TRUE);
    lineno++;
    if (*tmp)
      tmp++;
  }

done:
  if (err)
    *err = g_string_free (errstr, errstr->len ? FALSE : TRUE);
  g_free (content);
  g_free (filename);
  g_free (bold);
  g_free (red);
  return structures;

failed:
  if (structures)
    g_list_free_full (structures, (GDestroyNotify) gst_structure_free);
  structures = NULL;

  goto done;
}

static GList *
_get_structures (const gchar * structured_file, gchar ** file_path,
    GstValidateGetIncludePathsFunc get_include_paths_func, gchar ** err)
{
  GFile *file = NULL;
  GList *structs = NULL;

  GST_DEBUG ("Trying to load %s", structured_file);
  if ((file = g_file_new_for_path (structured_file)) == NULL) {
    GST_WARNING ("%s wrong uri", structured_file);
    if (err)
      *err = g_strdup_printf ("%s wrong uri", structured_file);
    return NULL;
  }

  if (file_path)
    *file_path = g_file_get_path (file);

  structs = _file_get_structures (file, err, get_include_paths_func);

  g_object_unref (file);

  return structs;
}

/**
 * gst_validate_utils_structs_parse_from_filename: (skip):
 */
GList *
gst_validate_utils_structs_parse_from_filename (const gchar * structured_file,
    GstValidateGetIncludePathsFunc get_include_paths_func, gchar ** file_path)
{
  GList *res;
  gchar *err = NULL;

  res =
      _get_structures (structured_file, file_path, get_include_paths_func,
      &err);

  if (err)
    gst_validate_abort ("Could not get structures from %s:\n%s\n",
        structured_file, err);

  return res;
}

/**
 * gst_validate_structs_parse_from_gfile: (skip):
 */
GList *
gst_validate_structs_parse_from_gfile (GFile * structured_file,
    GstValidateGetIncludePathsFunc get_include_paths_func)
{
  gchar *err = NULL;
  GList *res;

  res = _file_get_structures (structured_file, &err, get_include_paths_func);
  if (err)
    gst_validate_abort ("Could not get structures from %s:\n%s\n",
        g_file_get_uri (structured_file), err);

  return res;
}

gboolean
gst_validate_element_has_klass (GstElement * element, const gchar * klass)
{
  const gchar *tmp;
  gchar **a, **b;
  gboolean result = FALSE;
  guint i;

  tmp = gst_element_class_get_metadata (GST_ELEMENT_GET_CLASS (element),
      GST_ELEMENT_METADATA_KLASS);

  a = g_strsplit (klass, "/", -1);
  b = g_strsplit (tmp, "/", -1);

  /* All the elements in 'a' have to be in 'b' */
  for (i = 0; a[i] != NULL; i++)
    if (!g_strv_contains ((const char *const *) b, a[i]))
      goto done;
  result = TRUE;

done:
  g_strfreev (a);
  g_strfreev (b);
  return result;
}

static gboolean
gst_validate_convert_string_to_clocktime (const gchar * strtime,
    GstClockTime * retval)
{
  guint h, m, s, ns;
  gchar *other = g_strdup (strtime);
  gboolean res = TRUE;

  if (sscanf (strtime, "%" GST_TIME_FORMAT "%s", &h, &m, &s, &ns, other) < 4) {
    GST_DEBUG ("Can not sscanf %s", strtime);

    goto fail;
  }

  *retval = (h * 3600 + m * 60 + s) * GST_SECOND + ns;

done:
  g_free (other);
  return res;

fail:
  res = FALSE;
  goto done;
}

/**
 * gst_validate_utils_get_clocktime:
 * @structure: A #GstStructure to retrieve @name as a GstClockTime.
 * @name: The name of the field containing a #GstClockTime
 * @retval: (out): The clocktime contained in @structure
 *
 * Get @name from @structure as a #GstClockTime, it handles various types
 * for the value, if it is a double, it considers the value to be in second
 * it can be a gint, gint64 a guint, a gint64.
 *
 * Return: %TRUE in case of success, %FALSE otherwise.
 */
gboolean
gst_validate_utils_get_clocktime (GstStructure * structure, const gchar * name,
    GstClockTime * retval)
{
  gdouble val;
  const GValue *gvalue = gst_structure_get_value (structure, name);

  *retval = GST_CLOCK_TIME_NONE;
  if (gvalue == NULL) {
    return FALSE;
  }

  if (G_VALUE_TYPE (gvalue) == GST_TYPE_CLOCK_TIME) {
    *retval = g_value_get_uint64 (gvalue);

    return TRUE;
  }

  if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT64) {
    *retval = g_value_get_uint64 (gvalue);

    return TRUE;
  }

  if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT) {
    *retval = (GstClockTime) g_value_get_uint (gvalue);

    return TRUE;
  }

  if (G_VALUE_TYPE (gvalue) == G_TYPE_INT) {
    *retval = (GstClockTime) g_value_get_int (gvalue);

    return TRUE;
  }

  if (G_VALUE_TYPE (gvalue) == G_TYPE_INT64) {
    *retval = (GstClockTime) g_value_get_int64 (gvalue);

    return TRUE;
  }

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING) {
    return
        gst_validate_convert_string_to_clocktime (g_value_get_string (gvalue),
        retval);
  }


  if (!gst_structure_get_double (structure, name, &val)) {
    return FALSE;
  }

  if (val == -1.0)
    *retval = GST_CLOCK_TIME_NONE;
  else {
    *retval = val * GST_SECOND;
    *retval = GST_ROUND_UP_4 (*retval);
  }

  return TRUE;
}

/**
 * gst_validate_object_set_property_full:
 * @reporter: The #GstValidateReporter to use to report errors
 * @object: The #GObject to set the property on
 * @property: The name of the property to set
 * @value: The value to set the property to
 * @flags: The #GstValidateObjectSetPropertyFlags to use
 *
 * Since: 1.24
 */
GstValidateActionReturn
gst_validate_object_set_property_full (GstValidateReporter * reporter,
    GObject * object, const gchar * property,
    const GValue * value, GstValidateObjectSetPropertyFlags flags)
{
  GParamSpec *paramspec;
  GObjectClass *klass = G_OBJECT_GET_CLASS (object);
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  GValue cvalue = G_VALUE_INIT, nvalue = G_VALUE_INIT;

  paramspec = g_object_class_find_property (klass, property);
  if (paramspec == NULL) {
    if (!!(flags & GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_OPTIONAL))
      return TRUE;
    GST_ERROR ("Target doesn't have property %s", property);
    return FALSE;
  }

  g_value_init (&cvalue, paramspec->value_type);
  if (paramspec->value_type != G_VALUE_TYPE (value) &&
      (G_VALUE_TYPE (value) == G_TYPE_STRING)) {
    if (!gst_value_deserialize (&cvalue, g_value_get_string (value))) {
      GST_VALIDATE_REPORT (reporter, SCENARIO_ACTION_EXECUTION_ERROR,
          "Could not set %" GST_PTR_FORMAT "::%s as value %s"
          " could not be deserialize to %s", object, property,
          g_value_get_string (value), G_PARAM_SPEC_TYPE_NAME (paramspec));

      return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    }
  } else {
    if (!g_value_transform (value, &cvalue)) {
      GST_VALIDATE_REPORT (reporter, SCENARIO_ACTION_EXECUTION_ERROR,
          "Could not set %" GST_PTR_FORMAT " property %s to type %s"
          " (wanted type %s)", object, property, G_VALUE_TYPE_NAME (value),
          G_PARAM_SPEC_TYPE_NAME (paramspec));

      return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    }

  }

  g_object_set_property (object, property, &cvalue);

  g_value_init (&nvalue, paramspec->value_type);
  g_object_get_property (object, property, &nvalue);

  if (!(flags & GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_NO_VALUE_CHECK)) {
    if (gst_value_compare (&cvalue, &nvalue) != GST_VALUE_EQUAL) {
      gchar *nvalstr = gst_value_serialize (&nvalue);
      gchar *cvalstr = gst_value_serialize (&cvalue);
      GST_VALIDATE_REPORT (reporter, SCENARIO_ACTION_EXECUTION_ERROR,
          "Setting value %" GST_PTR_FORMAT "::%s failed, expected value: %s"
          " value after setting %s", object, property, cvalstr, nvalstr);

      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
      g_free (nvalstr);
      g_free (cvalstr);
    }
  }

  g_value_reset (&cvalue);
  g_value_reset (&nvalue);
  return res;
}

#if defined (G_OS_UNIX) && !defined (__APPLE__)
static void
fault_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGINT, &action, NULL);
  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}

static void
fault_spin (void)
{
  int spinning = TRUE;

  g_on_error_stack_trace ("GstValidate");

  wait (NULL);

  g_printerr ("Please run 'gdb <process-name> %d' to "
      "continue debugging, Ctrl-C to quit, or Ctrl-\\ to dump core.\n",
      (gint) getpid ());

  while (spinning)
    g_usleep (1000000);
}

static void
fault_handler_sighandler (int signum)
{
  fault_restore ();

  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (signum) {
    case SIGSEGV:
      g_printerr ("<Caught SIGNAL: SIGSEGV>\n");
      break;
    case SIGQUIT:
      gst_validate_printf (NULL, "<Caught SIGNAL: SIGQUIT>\n");
      break;
    default:
      g_printerr ("<Caught SIGNAL: %d>\n", signum);
      break;
  }

  fault_spin ();
}

static void
fault_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = fault_handler_sighandler;

  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}
#endif /* G_OS_UNIX && !__APPLE__ */

void
gst_validate_spin_on_fault_signals (void)
{
#if defined (G_OS_UNIX) && !defined (__APPLE__)
  fault_setup ();
#endif
}

/**
 * gst_validate_element_matches_target:
 * @element: a #GstElement to check
 * @s: a #GstStructure to use for matching
 *
 * Check if @element matches one of the 'target-element-name',
 * 'target-element-klass' or 'target-element-factory-name' defined in @s.
 *
 * Return: %TRUE if it matches, %FALSE otherwise or if @s doesn't contain any
 * target-element field.
 */
gboolean
gst_validate_element_matches_target (GstElement * element, GstStructure * s)
{
  const gchar *tmp;

  tmp = gst_structure_get_string (s, "target-element-name");
  if (tmp != NULL && !g_strcmp0 (tmp, GST_ELEMENT_NAME (element)))
    return TRUE;

  tmp = gst_structure_get_string (s, "target-element-klass");
  if (tmp != NULL && gst_validate_element_has_klass (element, tmp))
    return TRUE;

  tmp = gst_structure_get_string (s, "target-element-factory-name");
  if (tmp != NULL && gst_element_get_factory (element)
      && !g_strcmp0 (GST_OBJECT_NAME (gst_element_get_factory (element)), tmp))
    return TRUE;

  return FALSE;
}

static gchar *
gst_structure_get_value_as_string (GstStructure * structure,
    const gchar * field)
{
  const GValue *val = gst_structure_get_value (structure, field);

  if (!val)
    return NULL;

  if (G_VALUE_HOLDS_STRING (val))
    return g_value_dup_string (val);

  return gst_value_serialize (val);
}

gchar *
gst_validate_replace_variables_in_string (gpointer source,
    GstStructure * local_vars, const gchar * in_string,
    GstValidateStructureResolveVariablesFlags flags)
{
  gint varname_len;
  GMatchInfo *match_info = NULL;
  gchar *tmpstring, *string = g_strdup (in_string);

  if (!_variables_regex)
    _variables_regex = g_regex_new ("\\$\\((\\w+)\\)", 0, 0, NULL);

  gst_validate_set_globals (NULL);

  while (g_regex_match (_variables_regex, string, 0, &match_info)) {
    gchar *var_value = NULL;

    if (g_match_info_matches (match_info)) {
      GRegex *replace_regex;
      gchar *tmp, *varname, *pvarname = g_match_info_fetch (match_info, 0);

      varname_len = strlen (pvarname);
      varname = g_malloc (sizeof (gchar) * (varname_len - 2));
      strncpy (varname, &pvarname[2], varname_len - 3);
      varname[varname_len - 3] = '\0';

      if (local_vars && gst_structure_has_field_typed (local_vars, varname,
              G_TYPE_DOUBLE)) {
        var_value = g_strdup (varname);
      } else {
        if (local_vars)
          var_value = gst_structure_get_value_as_string (local_vars, varname);

        if (!var_value
            && !(flags & GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_LOCAL_ONLY))
          var_value = gst_structure_get_value_as_string (global_vars, varname);
      }

      if (!var_value) {
        g_free (varname);
        g_free (pvarname);
        g_free (string);
        g_clear_pointer (&match_info, g_match_info_free);
        if (!(flags & GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_NO_FAILURE)) {
          gst_validate_error_structure (source,
              "Trying to use undefined variable `%s`.\n"
              "  Available vars:\n"
              "    - locals%s\n"
              "    - globals%s\n",
              varname, gst_structure_to_string (local_vars),
              (flags & GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_LOCAL_ONLY) ?
              ": unused" : gst_structure_to_string (global_vars));

        }

        return NULL;
      }

      tmp = g_strdup_printf ("\\$\\(%s\\)", varname);
      replace_regex = g_regex_new (tmp, 0, 0, NULL);
      g_free (tmp);
      tmpstring = string;
      string =
          g_regex_replace_literal (replace_regex, string, -1, 0, var_value, 0,
          NULL);

      GST_INFO ("Setting variable %s to %s", varname, var_value);
      g_free (tmpstring);
      g_free (var_value);
      g_regex_unref (replace_regex);
      g_free (pvarname);
      g_free (varname);
    }
    g_clear_pointer (&match_info, g_match_info_free);
  }

  if (match_info)
    g_match_info_free (match_info);

  return string;
}

typedef struct
{
  gpointer source;
  GstStructure *local_vars;
  GstValidateStructureResolveVariablesFlags flags;
} ReplaceData;

static void
_resolve_expression (gpointer source, GValue * value)
{
  gdouble new_value;
  GMatchInfo *match_info = NULL;
  gchar *error = NULL;
  gchar *v, *expr, *tmp;

  g_assert (G_VALUE_HOLDS_STRING (value));

  tmp = expr = v = g_value_dup_string (value);
  tmp = skip_spaces (tmp);
  expr = strstr (v, "expr(");
  if (expr != tmp)
    goto done;

  expr = &expr[5];
  tmp = &expr[strlen (expr) - 1];
  while (g_ascii_isspace (*tmp) && tmp != expr)
    tmp--;

  if (tmp == expr || *tmp != ')')
    goto done;

  *tmp = '\0';
  new_value = gst_validate_utils_parse_expression (expr, NULL, NULL, &error);
  if (error)
    gst_validate_error_structure (source, "Could not parse expression %s: %s",
        expr, error);
  g_value_unset (value);
  g_value_init (value, G_TYPE_DOUBLE);
  g_value_set_double (value, new_value);

done:
  g_free (v);
  g_clear_pointer (&match_info, g_match_info_free);
}

static gboolean
_structure_set_variables (GQuark field_id, GValue * value, ReplaceData * data)
{
  if (field_id == filename_quark || field_id == debug_quark
      || field_id == debug_quark)
    return TRUE;

  if (GST_VALUE_HOLDS_LIST (value)) {
    gint i;

    for (i = 0; i < gst_value_list_get_size (value); i++)
      _structure_set_variables (0, (GValue *) gst_value_list_get_value (value,
              i), data);

    return TRUE;
  }

  if (!G_VALUE_HOLDS_STRING (value))
    return TRUE;

  if (!_variables_regex)
    _variables_regex = g_regex_new ("\\$\\((\\w+)\\)", 0, 0, NULL);

  /* Don't replace string contents unless really needed */
  if (g_regex_match (_variables_regex, g_value_get_string (value), 0, NULL)) {
    gchar *str = gst_validate_replace_variables_in_string (data->source,
        data->local_vars,
        g_value_get_string (value), data->flags);
    if (str) {
      g_value_set_string (value, str);
      g_free (str);
    }
  }

  if (!(data->flags & GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_NO_EXPRESSION))
    _resolve_expression (data->source, value);

  return TRUE;
}

void
gst_validate_structure_resolve_variables (gpointer source,
    GstStructure * structure, GstStructure * local_variables,
    GstValidateStructureResolveVariablesFlags flags)
{
  ReplaceData d = { source ? source : structure, local_variables, flags };

  gst_structure_filter_and_map_in_place (structure,
      (GstStructureFilterMapFunc) _structure_set_variables, &d);
}

static gboolean
_set_vars_func (GQuark field_id, const GValue * value, GstStructure * vars)
{
  gst_structure_id_set_value (vars, field_id, value);

  return TRUE;
}

static void
structure_set_string_literal (GstStructure * structure, const gchar * fieldname,
    const gchar * str)
{
  gint i;
  GString *escaped = g_string_sized_new (strlen (str) + 1);

  for (i = 0; str[i] != '\0'; i++) {
    g_string_append_c (escaped, str[i]);
    if (str[i] == '\\')
      g_string_append_c (escaped, '\\');
  }
  gst_structure_set (structure, fieldname, G_TYPE_STRING, escaped->str, NULL);
  g_string_free (escaped, TRUE);
}

void
gst_validate_set_globals (GstStructure * structure)
{
  if (!global_vars) {
    const gchar *logsdir = g_getenv ("GST_VALIDATE_LOGSDIR");

    if (!logsdir)
      logsdir = g_get_tmp_dir ();

    global_vars = gst_structure_new_empty ("vars");
    structure_set_string_literal (global_vars, "TMPDIR", g_get_tmp_dir ());
    structure_set_string_literal (global_vars, "LOGSDIR", logsdir);
    structure_set_string_literal (global_vars, "tmpdir", g_get_tmp_dir ());
    structure_set_string_literal (global_vars, "logsdir", logsdir);
  }

  if (!structure)
    return;

  gst_structure_foreach (structure,
      (GstStructureForeachFunc) _set_vars_func, global_vars);
}

/**
 * gst_validate_utils_get_strv:
 * @str: A GstStructure
 * @fieldname: A fieldname containing a GstValueList or is not defined
 *
 * Returns: (transfer full): An array of strings from the GstValueList defined in @fieldname
 */
gchar **
gst_validate_utils_get_strv (GstStructure * str, const gchar * fieldname)
{
  const GValue *value;
  gchar **parsed_list;
  guint i, size;

  value = gst_structure_get_value (str, fieldname);
  if (!value)
    return NULL;

  if (G_VALUE_HOLDS_STRING (value)) {
    parsed_list = g_new0 (gchar *, 2);
    parsed_list[0] = g_value_dup_string (value);

    return parsed_list;
  }

  if (!GST_VALUE_HOLDS_LIST (value)) {
    g_error
        ("%s must have type list of string (or a string), e.g. %s={ val1, val2 }, got: \"%s\" in %s",
        fieldname, fieldname, gst_value_serialize (value),
        gst_structure_to_string (str));
    return NULL;
  }

  size = gst_value_list_get_size (value);
  parsed_list = g_malloc_n (size + 1, sizeof (gchar *));
  for (i = 0; i < size; i++)
    parsed_list[i] = g_value_dup_string (gst_value_list_get_value (value, i));
  parsed_list[i] = NULL;
  return parsed_list;
}

static void
strip_ext (char *fname)
{
  char *end = fname + strlen (fname);

  while (end > fname && *end != '.')
    --end;

  if (end > fname)
    *end = '\0';
}

/* NOTE: vars == NULL implies that we are working on a testfile and the variables
 * will be set globally */
void
gst_validate_structure_set_variables_from_struct_file (GstStructure * vars,
    const gchar * struct_file)
{
  gchar *config_dir;
  gchar *config_fname;
  gchar *config_name;
  gchar *t, *config_name_dir;
  gchar *validateflow, *expectations_dir, *actual_result_dir;
  const gchar *logdir;
  gboolean local = !!vars;

  if (!struct_file)
    return;


  if (!vars)
    vars = global_vars;

  config_dir = g_path_get_dirname (struct_file);
  config_fname = g_path_get_basename (struct_file);
  config_name = g_strdup (config_fname);

  gst_validate_set_globals (NULL);
  logdir = gst_structure_get_string (global_vars, "logsdir");
  g_assert (logdir);

  strip_ext (config_name);
  config_name_dir = g_strdup (config_name);
  for (t = config_name_dir; *t != '\0'; t++) {
    if (*t == '.')
      *t = '/';
  }

  expectations_dir =
      g_build_path ("/", config_dir, config_name, "flow-expectations", NULL);
  actual_result_dir = g_build_path ("/", logdir, config_name_dir, NULL);
  validateflow =
      g_strdup_printf
      ("validateflow, expectations-dir=\"%s\", actual-results-dir=\"%s\"",
      expectations_dir, actual_result_dir);

  structure_set_string_literal (vars, "gst_api_version", GST_API_VERSION);
  structure_set_string_literal (vars, !local ? "test_dir" : "CONFIG_DIR",
      config_dir);
  structure_set_string_literal (vars, !local ? "test_name" : "CONFIG_NAME",
      config_name);
  structure_set_string_literal (vars,
      !local ? "test_name_dir" : "CONFIG_NAME_DIR", config_name_dir);
  structure_set_string_literal (vars, !local ? "test_path" : "CONFIG_PATH",
      struct_file);
  structure_set_string_literal (vars, "validateflow", validateflow);

  g_free (config_dir);
  g_free (config_name_dir);
  g_free (config_fname);
  g_free (config_name);
  g_free (validateflow);
  g_free (actual_result_dir);
  g_free (expectations_dir);
}

void
gst_validate_set_test_file_globals (GstStructure * meta, const gchar * testfile,
    gboolean use_fakesinks)
{
  gboolean needs_sync = FALSE;
  const gchar *videosink, *audiosink;

  if (!use_fakesinks) {
    videosink = "autovideosink";
    audiosink = "autoaudiosink";
  } else if (gst_structure_get_boolean (meta, "need-clock-sync", &needs_sync)
      && needs_sync) {
    videosink = "fakevideosink qos=true max-lateness=20000000";
    audiosink = "fakesink sync=true";
  } else {
    videosink = "fakevideosink sync=false";
    audiosink = "fakesink";
  }

  gst_structure_set (global_vars,
      "videosink", G_TYPE_STRING, videosink,
      "audiosink", G_TYPE_STRING, audiosink, NULL);
}

gboolean
gst_validate_fail_on_missing_plugin (void)
{

  GList *config;

  for (config = gst_validate_plugin_get_config (NULL); config;
      config = config->next) {
    gboolean fail_on_missing_plugin;

    if (gst_structure_get_boolean (config->data,
            "fail-on-missing-plugin", &fail_on_missing_plugin))
      return fail_on_missing_plugin;
  }
  return FALSE;
}

GstValidateActionReturn
gst_validate_object_set_property (GstValidateReporter * reporter,
    GObject * object,
    const gchar * property, const GValue * value, gboolean optional)
{
  return gst_validate_object_set_property_full (reporter, object, property,
      value, optional ? GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_OPTIONAL : 0);
}
