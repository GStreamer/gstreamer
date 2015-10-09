/* Fake HTTP source element
 *
 * Copyright (c) <2015> YouView TV Ltd
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

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_FAKE_SOUP_HTTP_SRC            (gst_fake_soup_http_src_get_type ())

/* structure used by tests to configure the GstFakeSoupHTTPSrc plugin.
 * It specifies what data to be fed for the given uri.
 * For the requested uri, it will return the data from payload.
 * If the payload is NULL, it will fake a buffer of size bytes and return data from it.
 * The buffer will contain a pattern, numbers 0, 4, 8, ... etc written on
 * sizeof(int) bytes, in little endian format (eg if sizeof(int)=4, the first 12
 * bytes are 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00)
 * Size is used only if payload is NULL.
 */
typedef struct _GstFakeHttpSrcInputData
{
  /* the uri for which data is being requested */
  const gchar *uri;
  /* the payload to be returned */
  const gchar *payload;
  /* the size of data to fake if payload is NULL */
  guint64 size;
} GstFakeHttpSrcInputData;

/* GstFakeSoupHTTPSrc will send buffers up to this size */
#define GST_FAKE_SOUP_HTTP_SRC_MAX_BUF_SIZE (1024)

GType gst_fake_soup_http_src_get_type (void);

gboolean gst_fake_soup_http_src_register_plugin (GstRegistry * registry, const gchar * name);

void gst_fake_soup_http_src_set_input_data (const GstFakeHttpSrcInputData *input);
