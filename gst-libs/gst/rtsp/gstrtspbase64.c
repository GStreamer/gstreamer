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

/**
 * gst_rtsp_base64_encode:
 * @data: the binary data to encode
 * @len: the length of @data
 *
 * Encode a sequence of binary data into its Base-64 stringified representation.
 *
 * Deprecated: Use g_base64_encode()
 *
 * Returns: a newly allocated, zero-terminated Base-64 encoded string
 * representing @data.
 */
/* This isn't efficient, but it doesn't need to be */
#ifndef GST_REMOVE_DEPRECATED
#ifdef GST_DISABLE_DEPRECATED
gchar *gst_rtsp_base64_encode (const gchar * data, gsize len);
#endif
gchar *
gst_rtsp_base64_encode (const gchar * data, gsize len)
{
  return g_base64_encode ((const guchar *) data, len);
}
#endif

/**
 * gst_rtsp_base64_decode_ip:
 * @data: the base64 encoded data
 * @len: location for output length or NULL
 *
 * Decode the base64 string pointed to by @data in-place. When @len is not #NULL
 * it will contain the length of the decoded data.
 *
 * Deprecated: use g_base64_decode_inplace() instead.
 */
#ifndef GST_REMOVE_DEPRECATED
#ifdef GST_DISABLE_DEPRECATED
void gst_rtsp_base64_decode_ip (gchar * data, gsize * len);
#endif
void
gst_rtsp_base64_decode_ip (gchar * data, gsize * len)
{
  gint input_length, output_length, state = 0;
  guint save = 0;

  g_return_if_fail (data != NULL);

  input_length = strlen (data);

  g_return_if_fail (input_length > 1);

  output_length =
      g_base64_decode_step (data, input_length, (guchar *) data, &state, &save);

  if (len)
    *len = output_length;
}
#endif
