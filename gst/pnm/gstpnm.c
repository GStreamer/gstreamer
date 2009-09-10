/* GStreamer
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

#include "gstpnmdec.h"
#include "gstpnmenc.h"
#include "gstpnmutils.h"

#include <gst/gstplugin.h>
#include <gst/gstversion.h>
#include <gst/gsttypefind.h>

#include <string.h>

static GstStaticCaps bitmap_caps = GST_STATIC_CAPS (MIME_BM);
static GstStaticCaps graymap_caps = GST_STATIC_CAPS (MIME_GM);
static GstStaticCaps pixmap_caps = GST_STATIC_CAPS (MIME_PM);

#define BITMAP_CAPS (gst_static_caps_get (&bitmap_caps))
#define GRAYMAP_CAPS (gst_static_caps_get (&graymap_caps))
#define PIXMAP_CAPS (gst_static_caps_get (&pixmap_caps))

static void
gst_my_typefind_function (GstTypeFind * tf, gpointer d)
{
  GstPnmInfoMngrResult r = GST_PNM_INFO_MNGR_RESULT_READING;
  GstPnmInfoMngr mngr = { 0, };
  guint i;
  guint8 *data = NULL;

  for (i = 0; r == GST_PNM_INFO_MNGR_RESULT_READING; i++) {
    data = gst_type_find_peek (tf, i, 1);
    if (!data)
      break;
    r = gst_pnm_info_mngr_scan (&mngr, data, 1);
  }
  switch (r) {
    case GST_PNM_INFO_MNGR_RESULT_READING:
    case GST_PNM_INFO_MNGR_RESULT_FAILED:
      return;
    case GST_PNM_INFO_MNGR_RESULT_FINISHED:
      switch (mngr.info.type) {
        case GST_PNM_TYPE_BITMAP_ASCII:
        case GST_PNM_TYPE_BITMAP_RAW:
          gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, BITMAP_CAPS);
          return;
        case GST_PNM_TYPE_GRAYMAP_ASCII:
        case GST_PNM_TYPE_GRAYMAP_RAW:
          gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, GRAYMAP_CAPS);
          return;
        case GST_PNM_TYPE_PIXMAP_ASCII:
        case GST_PNM_TYPE_PIXMAP_RAW:
          gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, PIXMAP_CAPS);
          return;
      }
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  static gchar *exts[] = { "pnm", NULL };

  if (!gst_type_find_register (plugin, "", GST_RANK_PRIMARY,
          gst_my_typefind_function, exts, gst_caps_new_simple (MIME_ALL, NULL),
          NULL, NULL))
    return FALSE;

  if (!gst_element_register (plugin, "pnmdec", GST_RANK_PRIMARY,
          GST_TYPE_PNMDEC))
    return FALSE;
  if (!gst_element_register (plugin, "pnmenc", GST_RANK_PRIMARY,
          GST_TYPE_PNMENC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "pnm",
    "PNM plugin", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
