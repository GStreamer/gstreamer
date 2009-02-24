/* GStreamer
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <config.h>
#endif

#include <gst/gst.h>
#include "xdgmime/xdgmime.h"

GST_DEBUG_CATEGORY (xdgmime_debug);
#define GST_CAT_DEFAULT xdgmime_debug

static void
xdgmime_typefind (GstTypeFind * find, gpointer user_data)
{
  const gchar *mimetype;
  gsize length = 16384;
  guint64 tf_length;
  gint prio;
  guint8 *data;
  GstCaps *caps;

  if ((tf_length = gst_type_find_get_length (find)) > 0)
    length = MIN (length, tf_length);

  if ((data = gst_type_find_peek (find, 0, length)) == NULL)
    return;

  mimetype = xdg_mime_get_mime_type_for_data (data, length, &prio);
  if (mimetype == NULL || g_str_equal (mimetype, XDG_MIME_TYPE_UNKNOWN))
    return;

  GST_DEBUG ("Got mimetype '%s' with prio %d", mimetype, prio);

  caps = gst_caps_new_simple (mimetype, NULL);
  gst_type_find_suggest (find, GST_TYPE_FIND_LIKELY, caps);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (xdgmime_debug, "xdgmime", 0, "XDG-MIME");

  return gst_type_find_register (plugin,
      "xdgmime", GST_RANK_MARGINAL, xdgmime_typefind, NULL, NULL, NULL, NULL);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "xdgmime",
    "XDG-MIME",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
