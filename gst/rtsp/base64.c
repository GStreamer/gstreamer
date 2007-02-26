/* GStreamer
 * Copyright (C) <2007> Mike Smith <msmith@xiph.org>
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

#include "base64.h"

static char base64table[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
      'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e',
      'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u',
      'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

/* This isn't efficient, but it doesn't need to be */
gchar *
util_base64_encode (gchar * data, gint len)
{
  gchar *out = g_malloc (len * 4 / 3 + 4);
  gchar *result = out;
  int chunk;

  while (len > 0) {
    chunk = (len > 3) ? 3 : len;
    *out++ = base64table[(*data & 0xFC) >> 2];
    *out++ = base64table[((*data & 0x03) << 4) | ((*(data + 1) & 0xF0) >> 4)];
    switch (chunk) {
      case 3:
        *out++ =
            base64table[((*(data + 1) & 0x0F) << 2) | ((*(data +
                        2) & 0xC0) >> 6)];
        *out++ = base64table[(*(data + 2)) & 0x3F];
        break;
      case 2:
        *out++ = base64table[((*(data + 1) & 0x0F) << 2)];
        *out++ = '=';
        break;
      case 1:
        *out++ = '=';
        *out++ = '=';
        break;
    }
    data += chunk;
    len -= chunk;
  }
  *out = 0;

  return result;
}
