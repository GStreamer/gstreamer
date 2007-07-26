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

/**
 * SECTION:gstrtspbase64
 * @short_description: Helper functions to handle Base64
 *
 * Last reviewed on 2007-07-24 (0.10.14)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstrtspbase64.h"

static char base64table[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
  'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e',
  'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u',
  'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

/**
 * gst_rtsp_base64_encode:
 * @data: the binary data to encode
 * @len: the length of @data
 *
 * Encode a sequence of binary data into its Base-64 stringified representation.
 *
 * Returns: a newly allocated, zero-terminated Base-64 encoded string
 * representing @data.
 */
/* This isn't efficient, but it doesn't need to be */
gchar *
gst_rtsp_base64_encode (const gchar * data, gsize len)
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

/**
 * gst_rtsp_base64_decode_ip:
 * @data: the base64 encoded data
 * @len: location for output length or NULL
 *
 * Decode the base64 string pointed to by @data in-place. When @len is not #NULL
 * it will contain the length of the decoded data.
 */
void
gst_rtsp_base64_decode_ip (gchar * data, gsize * len)
{
  char dtable[256];
  int i, j, k = 0, n = strlen (data);

  for (i = 0; i < 255; i++)
    dtable[i] = 0x80;
  for (i = 'A'; i <= 'Z'; i++)
    dtable[i] = 0 + (i - 'A');
  for (i = 'a'; i <= 'z'; i++)
    dtable[i] = 26 + (i - 'a');
  for (i = '0'; i <= '9'; i++)
    dtable[i] = 52 + (i - '0');
  dtable['+'] = 62;
  dtable['/'] = 63;
  dtable['='] = 0;

  for (j = 0; j < n; j += 4) {
    char a[4], b[4];

    for (i = 0; i < 4; i++) {
      int c = data[i + j];

      if (dtable[c] & 0x80) {
        if (len)
          *len = 0;
        return;
      }
      a[i] = (char) c;
      b[i] = (char) dtable[c];
    }
    data[k++] = (b[0] << 2) | (b[1] >> 4);
    data[k++] = (b[1] << 4) | (b[2] >> 2);
    data[k++] = (b[2] << 6) | b[3];
    i = a[2] == '=' ? 1 : (a[3] == '=' ? 2 : 3);
    if (i < 3) {
      data[k] = 0;
      if (len)
        *len = k;
      return;
    }
  }
  data[k] = 0;
  if (len)
    *len = k;
  return;
}
