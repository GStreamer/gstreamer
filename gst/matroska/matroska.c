/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * matroska.c: plugin loader
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

#include "matroska-demux.h"
#include "matroska-mux.h"
#include "matroska-ids.h"
#include "webm-mux.h"

#include <string.h>

/*** video/webm typefinder (temporary) ***/
static GstStaticCaps webm_caps = GST_STATIC_CAPS ("video/webm");

#define WEBM_CAPS (gst_static_caps_get(&webm_caps))
static void
webm_type_find (GstTypeFind * tf, gpointer ununsed)
{
  static const guint8 webm_doctype[] = { 'w', 'e', 'b', 'm' };
  guint8 *data;
  gint len_mask = 0x80, size = 1, n = 1, total;

  /* 4 bytes for EBML ID, 1 byte for header length identifier */
  data = gst_type_find_peek (tf, 0, 4 + 1);
  if (!data)
    return;

  /* ebml header? */
  if (data[0] != 0x1A || data[1] != 0x45 || data[2] != 0xDF || data[3] != 0xA3)
    return;

  /* length of header */
  total = data[4];
  while (size <= 8 && !(total & len_mask)) {
    size++;
    len_mask >>= 1;
  }
  if (size > 8)
    return;
  total &= (len_mask - 1);
  while (n < size)
    total = (total << 8) | data[4 + n++];

  /* get new data for full header, 4 bytes for EBML ID,
   * EBML length tag and the actual header */
  data = gst_type_find_peek (tf, 0, 4 + size + total);
  if (!data)
    return;

  /* the header must contain the document type 'webm'. For now,
   * we don't parse the whole header but simply check for the
   * availability of that array of characters inside the header.
   * Not fully fool-proof, but good enough. */
  for (n = 4 + size; n <= 4 + size + total - sizeof (webm_doctype); n++) {
    if (!memcmp (&data[n], webm_doctype, sizeof (webm_doctype))) {
      gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, WEBM_CAPS);
      return;
    }
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  static const gchar *webm_exts[] = { "webm", "weba", "webv", NULL };
  gboolean ret;

  gst_matroska_register_tags ();

  ret = gst_matroska_demux_plugin_init (plugin);
  ret &= gst_element_register (plugin, "matroskamux", GST_RANK_PRIMARY,
      GST_TYPE_MATROSKA_MUX);
  ret &= gst_element_register (plugin, "webmmux", GST_RANK_PRIMARY,
      GST_TYPE_WEBM_MUX);
  ret &= gst_type_find_register (plugin, "good-webm", GST_RANK_MARGINAL,
      webm_type_find, (gchar **) webm_exts, WEBM_CAPS, NULL, NULL);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "matroska",
    "Matroska and WebM stream handling",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
