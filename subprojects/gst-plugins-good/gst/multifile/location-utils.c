/* GStreamer multifile location template pattern checking utilities
 *
 * Copyright (C) 2025 Tim-Philipp Müller <tim centricular com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "location-utils.h"

#include <gst/gst.h>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("multifile", 0,
        "Multifile plugin utility functions");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */


// Find next % identifier, but skip %%
// Note: could be a partial identifier, i.e. just % followed by NUL.
static const gchar *
next_identifier (const gchar * s)
{
  const gchar *next;

  while ((next = strchr (s, '%'))) {
    if (next[1] != '%')
      return next;

    s = next + 2;               // skip %% and try again
  }

  return NULL;
}

typedef enum
{
  NO_STRING,
  NO_IDENTIFIER,
  BAD_IDENTIFIER,
  MULTIPLE_IDENTIFIERS,
  SINGLE_IDENTIFIER,            // May not be the actual identifier, just first non-numeric character after the '%'
} IdResult;

static IdResult
printf_string_template_get_single_identifier (const gchar * s, gchar * id_found)
{
  *id_found = '\0';

  if (s == NULL)
    return NO_STRING;

  s = next_identifier (s);      // find first non-%% identifier

  if (s == NULL)                // special case: no format identifier (but maybe some %%)
    return NO_IDENTIFIER;

  ++s;                          // skip '%'

  if (next_identifier (s + 1) != NULL)  // is there another identifier?
    return MULTIPLE_IDENTIFIERS;

  // Skip certain flags
  if (*s == '*')
    return BAD_IDENTIFIER;

  if (*s == '#' || *s == '-' || *s == '+' || *s == ' ')
    ++s;

  if (*s == '*')
    return BAD_IDENTIFIER;

  // Numbers before precision dot
  while (*s != '\0' && *s >= '0' && *s <= '9')
    ++s;

  // Precision
  if (*s == '.')
    ++s;

  if (*s == '*')
    return BAD_IDENTIFIER;

  // Skip certain format modifiers like 0-padding and width.
  // Anything else is probably unexpected in this context.
  // We certainly don't want to allow modifiers that change the size of the
  // parameter expected (like %lld or %zd or such).
  while (*s != '\0' && *s >= '0' && *s <= '9')
    ++s;

  if (g_ascii_isalpha (*s)) {
    *id_found = *s;
    return SINGLE_IDENTIFIER;
  }

  return BAD_IDENTIFIER;
}

gchar *
multifile_utils_printf_string_from_template (gpointer log_object,
    const gchar * tmpl, gint n)
{
  gchar id;

  GST_TRACE_OBJECT (log_object, "Checking template string '%s'...", tmpl);

  switch (printf_string_template_get_single_identifier (tmpl, &id)) {
    case NO_STRING:
      GST_ERROR_OBJECT (log_object, "No template string");
      return NULL;

    case MULTIPLE_IDENTIFIERS:
      GST_ERROR_OBJECT (log_object, "Unexpected number of format identifiers "
          "in template string '%s'", tmpl);
      return NULL;

    case BAD_IDENTIFIER:
      GST_ERROR_OBJECT (log_object, "Bad or unexpected format identifier "
          "in template string '%s'", tmpl);
      return NULL;

    case NO_IDENTIFIER:
      // No identifiers is fine, it will just always be the same filename then
      break;

    case SINGLE_IDENTIFIER:
      // May not be the actual identifier, just first non-numeric character after the '%'
      switch (id) {
        case 'u':              // allow in the spirit of 'be generous what you accept'
        case 'x':              // ditto
        case 'X':              // ditto
        case 'i':              // ditto
        case 'o':              // ditto
        case 'd':
          break;
        default:
          GST_ERROR_OBJECT (log_object, "Bad or unexpected format identifier "
              "in template string '%s'", tmpl);
          return NULL;
      }
      break;
  }

  gchar *str = g_strdup_printf (tmpl, n);

  GST_TRACE_OBJECT (log_object, "Template '%s' @ n=%d => '%s'", tmpl, n, str);

  return str;
}

gboolean
multifile_utils_check_template_string (gpointer log_object, const gchar * tmpl)
{
  gchar *str;

  str = multifile_utils_printf_string_from_template (log_object, tmpl, 1);

  if (str == NULL)
    return FALSE;

  g_free (str);
  return TRUE;
}
