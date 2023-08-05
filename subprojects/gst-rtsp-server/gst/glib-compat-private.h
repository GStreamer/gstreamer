/*
 * glib-compat.c
 * Functions copied from glib 2.10
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GLIB_COMPAT_PRIVATE_H__
#define __GLIB_COMPAT_PRIVATE_H__

#include <glib.h>

G_BEGIN_DECLS

/* copies */

#if !GLIB_CHECK_VERSION(2,68,0)
#ifndef g_string_replace
#define g_string_replace gst_g_string_replace
#endif

static inline guint
gst_g_string_replace (GString     *string,
                      const gchar *find,
                      const gchar *replace,
                      guint        limit)
{
  gsize f_len, r_len, pos;
  gchar *cur, *next;
  guint n = 0;

  g_return_val_if_fail (string != NULL, 0);
  g_return_val_if_fail (find != NULL, 0);
  g_return_val_if_fail (replace != NULL, 0);

  f_len = strlen (find);
  r_len = strlen (replace);
  cur = string->str;

  while ((next = strstr (cur, find)) != NULL)
    {
      pos = next - string->str;
      g_string_erase (string, pos, f_len);
      g_string_insert (string, pos, replace);
      cur = string->str + pos + r_len;
      n++;
      /* Only match the empty string once at any given position, to
       * avoid infinite loops */
      if (f_len == 0)
        {
          if (cur[0] == '\0')
            break;
          else
            cur++;
        }
      if (n == limit)
        break;
    }

  return n;
}
#endif /* GLIB_CHECK_VERSION */

/* adaptations */

G_END_DECLS

#endif
