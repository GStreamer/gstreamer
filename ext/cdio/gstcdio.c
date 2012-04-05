/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#include "gstcdio.h"
#include "gstcdiocddasrc.h"

#include <cdio/logging.h>

GST_DEBUG_CATEGORY (gst_cdio_debug);

void
gst_cdio_add_cdtext_field (GstObject * src, cdtext_t * cdtext,
    cdtext_field_t field, const gchar * gst_tag, GstTagList ** p_tags)
{
  const gchar *txt;

  txt = cdtext_get_const (field, cdtext);
  if (txt == NULL || *txt == '\0') {
    GST_DEBUG_OBJECT (src, "empty CD-TEXT field %u (%s)", field, gst_tag);
    return;
  }

  /* FIXME: beautify strings (they might be all uppercase for example)? */
  /* FIXME: what encoding are these strings in? Let's hope ASCII or UTF-8 */
  if (!g_utf8_validate (txt, -1, NULL)) {
    GST_WARNING_OBJECT (src, "CD-TEXT string is not UTF-8! (%s)", gst_tag);
    return;
  }

  if (*p_tags == NULL)
    *p_tags = gst_tag_list_new_empty ();

  gst_tag_list_add (*p_tags, GST_TAG_MERGE_REPLACE, gst_tag, txt, NULL);

  GST_DEBUG_OBJECT (src, "CD-TEXT: %s = %s", gst_tag, txt);
}

GstTagList *
gst_cdio_get_cdtext (GstObject * src, CdIo * cdio, track_t track)
{
  GstTagList *tags = NULL;
  cdtext_t *t;

  t = cdio_get_cdtext (cdio, track);
  if (t == NULL) {
    GST_DEBUG_OBJECT (src, "no CD-TEXT for track %u", track);
    return NULL;
  }

  gst_cdio_add_cdtext_field (src, t, CDTEXT_PERFORMER, GST_TAG_ARTIST, &tags);
  gst_cdio_add_cdtext_field (src, t, CDTEXT_TITLE, GST_TAG_TITLE, &tags);

  return tags;
}

void
gst_cdio_add_cdtext_album_tags (GstObject * src, CdIo * cdio, GstTagList * tags)
{
  cdtext_t *t;

  t = cdio_get_cdtext (cdio, 0);
  if (t == NULL) {
    GST_DEBUG_OBJECT (src, "no CD-TEXT for album");
    return;
  }

  /* FIXME: map CDTEXT_PERFORMER to GST_TAG_ALBUM_ARTIST once we have that */
  gst_cdio_add_cdtext_field (src, t, CDTEXT_TITLE, GST_TAG_ALBUM, &tags);
  gst_cdio_add_cdtext_field (src, t, CDTEXT_GENRE, GST_TAG_GENRE, &tags);

  GST_DEBUG ("CD-TEXT album tags: %" GST_PTR_FORMAT, tags);
}

static void
gst_cdio_log_handler (cdio_log_level_t level, const char *msg)
{
  const gchar *level_str[] = { "DEBUG", "INFO", "WARN", "ERROR", "ASSERT" };
  const gchar *s;

  s = level_str[CLAMP (level, 1, G_N_ELEMENTS (level_str)) - 1];
  GST_DEBUG ("CDIO-%s: %s", s, GST_STR_NULL (msg));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "cdiocddasrc", GST_RANK_SECONDARY - 1,
          GST_TYPE_CDIO_CDDA_SRC))
    return FALSE;

  cdio_log_set_handler (gst_cdio_log_handler);

  GST_DEBUG_CATEGORY_INIT (gst_cdio_debug, "cdio", 0, "libcdio elements");

  return TRUE;
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    cdio,
    "Read audio from audio CDs",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
